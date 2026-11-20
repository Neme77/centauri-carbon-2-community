/* Layer index ("layer map") for one G-code file.
 *
 * One full pass of the layer-path parser records, for every layer, the byte offset of the
 * marker line that starts it and the parser state just before that line (position,
 * absolute/relative modes, layer numbering, header layer count). A later request for one
 * layer then seeks to that offset, restores the state and reads only that layer. The
 * result is the same as reading the file from the start (tests/test_gcode_layer_index.c
 * compares both point by point).
 *
 * The index is built when no print is running and stored as a small file next to the other
 * CC2 Control data, never next to the G-code. It is tied to the file by size, modification
 * and change time, inode and a SHA-256 of the first and last 64 KiB; any difference makes
 * it stale and it is rebuilt.
 *
 * Files the index cannot serve exactly fall back to the full scan: no layer markers at all,
 * or ;LAYER:N numbering that returns to a layer after another one (some Cura files). */
#ifndef CC2_GCODE_LAYER_INDEX_H
#define CC2_GCODE_LAYER_INDEX_H
#include "gcode_layer_path.h"
#include "sha256.h"
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <unistd.h>

#define LAYER_INDEX_MAGIC "CC2LIDX1"
#define LAYER_INDEX_VERSION 1
/* 256 mm bed at 0.05 mm layers ≈ 5120; headroom for tall slices and comment markers. */
#define LAYER_INDEX_LAYERS_MAX 8192
#define LAYER_INDEX_EDGE_BYTES (64 * 1024)
/* One layer read after a jump: the scan of a single layer is bounded on its own instead of
 * by a position in the file. The largest layer of the 75 MB test file is far below this. */
#ifndef LAYER_INDEX_LAYER_BYTES_MAX
#define LAYER_INDEX_LAYER_BYTES_MAX (32UL * 1024UL * 1024UL)
#endif

enum { LAYER_INDEX_NONE = 10 }; /* no usable index for this file: use the full scan */

/* The parser state at the start of a layer, without the point buffers. */
typedef struct {
    int64_t offset;
    double x, y, z, e, layer_z;
    int32_t layer, abs_xy, abs_e, have_xy, have_e, have_z, current, max_layer, total_hint, changes,
        seen_marker, change_mode, in_path, have_layer_z, z_comment;
} layer_index_entry;

typedef struct {
    int64_t size, mtime_s, mtime_ns, ctime_s, ctime_ns, ino;
    unsigned char edges[32];
} layer_index_key;

typedef struct {
    layer_index_key key;
    int32_t total, max_layer, seen_marker, repeats, count;
    layer_index_entry *entries; /* sorted by layer */
} layer_index;

static void layer_index_free(layer_index *index) {
    if (!index) return;
    free(index->entries);
    memset(index, 0, sizeof(*index));
}

static void layer_index_snapshot(layer_index_entry *e, const layer_path_state *s, long offset) {
    memset(e, 0, sizeof(*e));
    e->offset = offset;
    e->x = s->x; e->y = s->y; e->z = s->z; e->e = s->e; e->layer_z = s->layer_z;
    e->abs_xy = s->abs_xy; e->abs_e = s->abs_e; e->have_xy = s->have_xy; e->have_e = s->have_e;
    e->have_z = s->have_z; e->current = s->current; e->max_layer = s->max_layer;
    e->total_hint = s->total_hint; e->changes = s->changes; e->seen_marker = s->seen_marker;
    e->change_mode = s->change_mode; e->in_path = s->in_path; e->have_layer_z = s->have_layer_z;
    e->z_comment = s->z_comment;
}

static void layer_index_restore(layer_path_state *s, const layer_index_entry *e) {
    s->x = e->x; s->y = e->y; s->z = e->z; s->e = e->e; s->layer_z = e->layer_z;
    s->abs_xy = e->abs_xy; s->abs_e = e->abs_e; s->have_xy = e->have_xy; s->have_e = e->have_e;
    s->have_z = e->have_z; s->current = e->current; s->max_layer = e->max_layer;
    s->total_hint = e->total_hint; s->changes = e->changes; s->seen_marker = e->seen_marker;
    s->change_mode = e->change_mode; s->in_path = e->in_path; s->have_layer_z = e->have_layer_z;
    s->z_comment = e->z_comment;
    s->count = 0; s->paths = 0; s->truncated = 0;
}

typedef struct layer_index_builder_s {
    layer_index_entry pending;
    int pending_change_mode;
    layer_index_entry *entries;
    int count, cap, repeats, failed;
    unsigned char *seen; /* one byte per layer number */
    int seen_cap;
} layer_index_builder;

typedef struct { int (*keep_going)(void *); void *keep_ctx; struct layer_index_builder_s *builder; } layer_index_gate;

static int layer_index_gate_ok(void *ctx) {
    layer_index_gate *g = ctx;
    return !g->keep_going || g->keep_going(g->keep_ctx);
}

static void layer_index_before(void *ctx, const layer_path_state *s, long offset) {
    layer_index_builder *b = (layer_index_builder *)((layer_index_gate *)ctx)->builder;
    layer_index_snapshot(&b->pending, s, offset);
    b->pending_change_mode = s->change_mode;
}

static void layer_index_after(void *ctx, const layer_path_state *s) {
    layer_index_builder *b = (layer_index_builder *)((layer_index_gate *)ctx)->builder;
    if (b->failed) return;
    int restarted = !b->pending_change_mode && s->change_mode;
    if (restarted) { /* the first ;LAYER_CHANGE renumbers from 0 and drops earlier ;LAYER:N */
        b->count = 0;
        b->repeats = 0;
        if (b->seen) memset(b->seen, 0, (size_t)b->seen_cap);
    } else if (s->current == b->pending.current) {
        return; /* the marker did not move to another layer */
    }
    int layer = s->current;
    if (layer < 0 || layer >= LAYER_INDEX_LAYERS_MAX) { b->failed = 1; return; }
    if (layer >= b->seen_cap) {
        int cap = b->seen_cap ? b->seen_cap : 1024;
        while (cap <= layer) cap *= 2;
        unsigned char *seen = realloc(b->seen, (size_t)cap);
        if (!seen) { b->failed = 1; return; }
        memset(seen + b->seen_cap, 0, (size_t)(cap - b->seen_cap));
        b->seen = seen; b->seen_cap = cap;
    }
    if (b->seen[layer]) { b->repeats = 1; return; } /* back to an earlier layer: no jump possible */
    b->seen[layer] = 1;
    if (b->count >= LAYER_INDEX_LAYERS_MAX) { b->failed = 1; return; }
    if (b->count == b->cap) {
        int cap = b->cap ? b->cap * 2 : 1024;
        if (cap > LAYER_INDEX_LAYERS_MAX) cap = LAYER_INDEX_LAYERS_MAX;
        layer_index_entry *grown = realloc(b->entries, sizeof(*grown) * (size_t)cap);
        if (!grown) { b->failed = 1; return; }
        b->entries = grown; b->cap = cap;
    }
    b->pending.layer = layer;
    b->entries[b->count++] = b->pending;
}

static int layer_index_entry_cmp(const void *a, const void *b) {
    const layer_index_entry *x = a, *y = b;
    return (x->layer > y->layer) - (x->layer < y->layer);
}

/* SHA-256 over the first and last LAYER_INDEX_EDGE_BYTES (the whole file if smaller). */
static int layer_index_edges(FILE *file, int64_t size, unsigned char out[32]) {
    unsigned char *buffer = malloc(LAYER_INDEX_EDGE_BYTES); /* not static: maps are read and built in parallel */
    if (!buffer) return -1;
    sha256_state h; sha256_init(&h);
    int failed = 0;
    int64_t first = size < LAYER_INDEX_EDGE_BYTES ? size : LAYER_INDEX_EDGE_BYTES;
    if (fseek(file, 0, SEEK_SET) != 0 || fread(buffer, 1, (size_t)first, file) != (size_t)first) failed = 1;
    if (!failed) sha256_update(&h, buffer, (size_t)first);
    if (!failed && size > LAYER_INDEX_EDGE_BYTES) {
        int64_t tail = size - LAYER_INDEX_EDGE_BYTES < LAYER_INDEX_EDGE_BYTES ? size - LAYER_INDEX_EDGE_BYTES
                                                                                : LAYER_INDEX_EDGE_BYTES;
        if (fseek(file, (long)(size - tail), SEEK_SET) != 0 || fread(buffer, 1, (size_t)tail, file) != (size_t)tail) failed = 1;
        else sha256_update(&h, buffer, (size_t)tail);
    }
    free(buffer);
    if (failed) return -1;
    char hex[65]; sha256_hex(&h, hex);
    for (int i = 0; i < 32; i++) { unsigned v; sscanf(hex + 2 * i, "%2x", &v); out[i] = (unsigned char)v; }
    return fseek(file, 0, SEEK_SET) == 0 ? 0 : -1;
}

static int layer_index_key_of(FILE *file, layer_index_key *key) {
    struct stat st;
    memset(key, 0, sizeof(*key));
    if (fstat(fileno(file), &st) != 0) return -1;
    key->size = st.st_size; key->ino = (int64_t)st.st_ino;
#ifdef __APPLE__
    key->mtime_s = st.st_mtimespec.tv_sec; key->mtime_ns = st.st_mtimespec.tv_nsec;
    key->ctime_s = st.st_ctimespec.tv_sec; key->ctime_ns = st.st_ctimespec.tv_nsec;
#else
    key->mtime_s = st.st_mtim.tv_sec; key->mtime_ns = st.st_mtim.tv_nsec;
    key->ctime_s = st.st_ctim.tv_sec; key->ctime_ns = st.st_ctim.tv_nsec;
#endif
    return layer_index_edges(file, key->size, key->edges);
}

static int layer_index_key_same(const layer_index_key *a, const layer_index_key *b) {
    return a->size == b->size && a->mtime_s == b->mtime_s && a->mtime_ns == b->mtime_ns &&
        a->ctime_s == b->ctime_s && a->ctime_ns == b->ctime_ns && a->ino == b->ino &&
        !memcmp(a->edges, b->edges, sizeof(a->edges));
}

/* One full pass over file. time_ms 0 = no deadline. keep_going (optional) is asked every
 * 64 KiB; returning 0 stops the pass with LAYER_PATH_LIMIT and no index. */
static int layer_index_build(FILE *file, layer_index *index, long time_ms,
                             int (*keep_going)(void *), void *keep_ctx) {
    memset(index, 0, sizeof(*index));
    if (!file) return LAYER_PATH_IO;
    if (layer_index_key_of(file, &index->key) != 0) return LAYER_PATH_IO;
    layer_point dummy[2]; int dummy_start[1];
    layer_path_state s;
    layer_path_init(&s);
    s.pts = dummy; s.starts = dummy_start; s.cap = 2; s.path_cap = 1; /* target -1: nothing is collected */
    layer_index_builder b; memset(&b, 0, sizeof(b));
    layer_index_gate gate = { keep_going, keep_ctx, &b };
    layer_path_marker_hook hook = { layer_index_before, layer_index_after, layer_index_gate_ok, &gate };
    struct timespec start; clock_gettime(CLOCK_MONOTONIC, &start);
    int rc = layer_path_run(file, &s, -1, &start, time_ms ? time_ms : 0x7fffffffL, 0, 0, &hook);
    if (rc == LAYER_PATH_OK && b.failed) rc = LAYER_PATH_IO;
    free(b.seen);
    if (rc != LAYER_PATH_OK) { free(b.entries); return rc; }
    /* The layer count is the number of layers the file actually marks, not the header: some
     * files promise more than they mark (ELEGOO's Devil sample: header 524, 523 marked;
     * project decision: counted layers win). ;LAYER_CHANGE files count their changes, ;LAYER:N
     * files the highest number + 1. */
    int total = s.change_mode ? s.changes : s.max_layer + 1;
    if (total <= 0 && s.total_hint) total = s.total_hint;
    if (!s.seen_marker) total = 1;
    index->total = total; index->max_layer = s.max_layer; index->seen_marker = s.seen_marker;
    index->repeats = b.repeats; index->count = b.count; index->entries = b.entries;
    /* A file without markers has no entries (and a NULL array): qsort must not see it. */
    if (index->count > 0)
        qsort(index->entries, (size_t)index->count, sizeof(*index->entries), layer_index_entry_cmp);
    return LAYER_PATH_OK;
}

static const layer_index_entry *layer_index_find(const layer_index *index, int layer0) {
    int lo = 0, hi = index->count - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2, v = index->entries[mid].layer;
        if (v == layer0) return &index->entries[mid];
        if (v < layer0) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

/* Read one layer through the index. LAYER_INDEX_NONE: the caller uses gcode_layer_path_parse. */
static int gcode_layer_path_parse_indexed(FILE *file, const layer_index *index, int layer_1based,
                                          layer_point *points, int points_cap,
                                          int *path_start, int paths_cap, layer_path_info *info,
                                          const struct timespec *start, long time_ms, size_t layer_bytes_max) {
    memset(info, 0, sizeof(*info));
    info->layer = layer_1based;
    info->z = NAN;
    if (!file || !index || !points || !path_start) return LAYER_PATH_IO;
    if (layer_1based < 1 || layer_1based > 1000000) return LAYER_PATH_OUTSIDE;
    if (points_cap < 2 || paths_cap < 1) return LAYER_PATH_IO;
    if (!index->seen_marker || index->repeats) return LAYER_INDEX_NONE;
    int target0 = layer_1based - 1;
    if (target0 >= index->total) { info->total_layers = index->total; return LAYER_PATH_OUTSIDE; }
    const layer_index_entry *entry = layer_index_find(index, target0);
    if (!entry && target0 > index->max_layer) {
        /* Beyond the last marked layer (only reachable while the count came from a header). */
        info->total_layers = index->total;
        return LAYER_PATH_OUTSIDE;
    }
    if (!entry) return LAYER_INDEX_NONE; /* a number the file skips: let the full scan decide */
    if (fseek(file, (long)entry->offset, SEEK_SET) != 0) return LAYER_PATH_IO;
    layer_path_state s;
    layer_path_init(&s);
    if (layer_path_buffers(&s, points, points_cap, path_start, paths_cap) != LAYER_PATH_OK) return LAYER_PATH_IO;
    layer_index_restore(&s, entry);
    int rc = layer_path_run(file, &s, target0, start, time_ms, layer_bytes_max, 1, NULL);
    if (rc == LAYER_PATH_OK) rc = layer_path_finish(&s, target0, index->total, index->max_layer, info);
    free(s.keep); free(s.stack);
    return rc;
}

/* ---- storage: <dir>/<sha256 of the G-code path>.idx, written through a temporary file ---- */

static int layer_index_path(const char *dir, const char *gcode_path, char *out, size_t cap) {
    sha256_state h; sha256_init(&h);
    sha256_update(&h, (const unsigned char *)gcode_path, strlen(gcode_path));
    char hex[65]; sha256_hex(&h, hex);
    int n = snprintf(out, cap, "%s/%s.idx", dir, hex);
    return n > 0 && (size_t)n < cap ? 0 : -1;
}

static int layer_index_save(const char *path, const layer_index *index) {
    char temporary[1024];
    int n = snprintf(temporary, sizeof(temporary), "%s.part", path);
    if (n < 0 || (size_t)n >= sizeof(temporary)) return -1;
    FILE *out = fopen(temporary, "wb");
    if (!out) return -1;
    int32_t version = LAYER_INDEX_VERSION;
    int failed = fwrite(LAYER_INDEX_MAGIC, 1, 8, out) != 8 ||
        fwrite(&version, sizeof(version), 1, out) != 1 ||
        fwrite(&index->key, sizeof(index->key), 1, out) != 1 ||
        fwrite(&index->total, sizeof(int32_t), 5, out) != 5 ||
        (index->count && fwrite(index->entries, sizeof(*index->entries), (size_t)index->count, out) != (size_t)index->count);
    if (!failed && fflush(out) != 0) failed = 1;
    if (!failed && fsync(fileno(out)) != 0) failed = 1;
    if (fclose(out) != 0) failed = 1;
    if (failed || rename(temporary, path) != 0) { unlink(temporary); return -1; }
    return 0;
}

static int layer_index_load(const char *path, layer_index *index) {
    memset(index, 0, sizeof(*index));
    FILE *in = fopen(path, "rb");
    if (!in) return -1;
    char magic[8]; int32_t version = 0, head[5];
    int ok = fread(magic, 1, 8, in) == 8 && !memcmp(magic, LAYER_INDEX_MAGIC, 8) &&
        fread(&version, sizeof(version), 1, in) == 1 && version == LAYER_INDEX_VERSION &&
        fread(&index->key, sizeof(index->key), 1, in) == 1 &&
        fread(head, sizeof(int32_t), 5, in) == 5;
    if (ok) {
        index->total = head[0]; index->max_layer = head[1]; index->seen_marker = head[2];
        index->repeats = head[3]; index->count = head[4];
        ok = index->count >= 0 && index->count <= LAYER_INDEX_LAYERS_MAX && index->total >= 0;
    }
    if (ok && index->count) {
        index->entries = malloc(sizeof(*index->entries) * (size_t)index->count);
        ok = index->entries && fread(index->entries, sizeof(*index->entries), (size_t)index->count, in) == (size_t)index->count;
        for (int i = 1; ok && i < index->count; i++) ok = index->entries[i - 1].layer < index->entries[i].layer;
    }
    if (ok) { char extra; ok = fread(&extra, 1, 1, in) == 0; } /* nothing after the entries */
    fclose(in);
    if (!ok) { layer_index_free(index); return -1; }
    return 0;
}

/* Load the stored index if it still matches file, otherwise 1 = stale or missing. */
static int layer_index_load_valid(const char *path, FILE *file, layer_index *index) {
    if (layer_index_load(path, index) != 0) return 1;
    layer_index_key now;
    if (layer_index_key_of(file, &now) != 0 || !layer_index_key_same(&now, &index->key)) {
        layer_index_free(index);
        return 1;
    }
    return 0;
}
#endif
