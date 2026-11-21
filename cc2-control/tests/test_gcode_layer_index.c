/* The layer index must give exactly the layer the full scan gives: same result code, layer
 * count, height, points and path starts, for every layer. Also: storage round trip, a
 * changed file makes the stored index stale, files the index cannot serve are refused.
 * Extra G-code files can be passed as arguments (every layer of each is compared). */
#include "../src/gcode_layer_index.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static layer_point pts_a[LAYER_PATH_COLLECT_MAX], pts_b[LAYER_PATH_COLLECT_MAX];
static int starts_a[LAYER_PATH_PATHS_MAX], starts_b[LAYER_PATH_PATHS_MAX];

static int full(FILE *f, int layer, layer_path_info *info) {
    struct timespec start; clock_gettime(CLOCK_MONOTONIC, &start);
    rewind(f);
    return gcode_layer_path_parse(f, layer, pts_a, LAYER_PATH_COLLECT_MAX, starts_a, LAYER_PATH_PATHS_MAX,
                                  info, &start, 0x7fffffffL, 0);
}

static int jump(FILE *f, const layer_index *index, int layer, layer_path_info *info) {
    struct timespec start; clock_gettime(CLOCK_MONOTONIC, &start);
    return gcode_layer_path_parse_indexed(f, index, layer, pts_b, LAYER_PATH_COLLECT_MAX, starts_b,
                                          LAYER_PATH_PATHS_MAX, info, &start, 0x7fffffffL,
                                          LAYER_INDEX_LAYER_BYTES_MAX);
}

static int same_z(double a, double b) { return (isnan(a) && isnan(b)) || a == b; }


/* Returns the number of differing layers; prints the first few. total_differs counts layers
 * whose only difference is the layer count (see the note in main). */
static int compare_all(const char *name, FILE *f, int *layers_out, int *total_differs) {
    layer_index index;
    assert(layer_index_build(f, &index, 0, NULL, NULL) == LAYER_PATH_OK);
    assert(index.seen_marker && !index.repeats);
    int bad = 0, every_layer_served = 1;
    *total_differs = 0;
    for (int layer = 1; layer <= index.total + 1; layer++) {
        layer_path_info a, b;
        int ra = full(f, layer, &a), rb = jump(f, &index, layer, &b);
        /* The HTTP side asks this before reading: it must agree with what the read then does. */
        assert(layer_index_can_serve(&index, layer) == (rb != LAYER_INDEX_NONE));
        every_layer_served = every_layer_served && rb != LAYER_INDEX_NONE;
        int points_same = ra != LAYER_PATH_OK ||
            (a.point_count == b.point_count && a.path_count == b.path_count &&
             !memcmp(pts_a, pts_b, sizeof(layer_point) * (size_t)a.point_count) &&
             !memcmp(starts_a, starts_b, sizeof(int) * (size_t)a.path_count));
        int ok = ra == rb && points_same && a.truncated == b.truncated && same_z(a.z, b.z);
        if (ok && a.total_layers != b.total_layers) { (*total_differs)++; continue; }
        if (!ok) {
            if (bad < 5)
                fprintf(stderr, "%s layer %d: rc %d/%d points %d/%d paths %d/%d z %g/%g total %d/%d\n", name, layer,
                        ra, rb, a.point_count, b.point_count, a.path_count, b.path_count, a.z, b.z,
                        a.total_layers, b.total_layers);
            bad++;
        }
    }
    assert(layer_index_serves_all(&index) == every_layer_served); /* "serves every layer" is exactly that */
    *layers_out = index.total;
    layer_index_free(&index);
    return bad;
}

static FILE *gcode(const char *dir, const char *name, const char *text) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w+");
    assert(f && fputs(text, f) >= 0 && !fflush(f));
    rewind(f);
    return f;
}

/* Flips one bit of the byte at pos in the stored map. */
static void flip_bit(const char *path, long pos) {
    FILE *f = fopen(path, "r+b"); assert(f);
    assert(!fseek(f, pos, SEEK_SET));
    int c = fgetc(f); assert(c != EOF);
    assert(!fseek(f, pos, SEEK_SET) && fputc(c ^ 1, f) != EOF);
    assert(!fclose(f));
}

/* A map in the first stored format (version 1: no record sizes, no checksum). */
static void write_version_1(const char *path, const layer_index *ix) {
    FILE *f = fopen(path, "wb"); assert(f);
    int32_t version = 1;
    assert(fwrite("CC2LIDX1", 1, 8, f) == 8 && fwrite(&version, sizeof(version), 1, f) == 1 &&
           fwrite(&ix->key, sizeof(ix->key), 1, f) == 1 && fwrite(&ix->total, sizeof(int32_t), 5, f) == 5 &&
           fwrite(ix->entries, sizeof(*ix->entries), (size_t)ix->count, f) == (size_t)ix->count);
    assert(!fclose(f));
}

/* A map in the second stored format (version 2: the same layout, but a 32-byte SHA-256 where version 3 has the 4-byte
 * CRC-32). It is the layout of the map in the first public version of the layer map. */
static void write_version_2(const char *path, const layer_index *ix) {
    FILE *f = fopen(path, "wb"); assert(f);
    int32_t version = 2, sizes[2] = { (int32_t)sizeof(layer_index_entry), (int32_t)sizeof(layer_index_key) };
    int32_t head[5] = { ix->total, ix->max_layer, ix->seen_marker, ix->repeats, ix->count };
    sha256_state h; sha256_init(&h);
    sha256_update(&h, (const unsigned char *)"CC2LIDX1", 8);
    sha256_update(&h, (const unsigned char *)&version, sizeof(version));
    sha256_update(&h, (const unsigned char *)sizes, sizeof(sizes));
    sha256_update(&h, (const unsigned char *)&ix->key, sizeof(ix->key));
    sha256_update(&h, (const unsigned char *)head, sizeof(head));
    sha256_update(&h, (const unsigned char *)ix->entries, sizeof(*ix->entries) * (size_t)ix->count);
    char hex[65]; sha256_hex(&h, hex);
    unsigned char digest[32];
    for (int i = 0; i < 32; i++) { unsigned v; assert(sscanf(hex + 2 * i, "%2x", &v) == 1); digest[i] = (unsigned char)v; }
    assert(fwrite("CC2LIDX1", 1, 8, f) == 8 && fwrite(&version, sizeof(version), 1, f) == 1 &&
           fwrite(sizes, sizeof(int32_t), 2, f) == 2 && fwrite(&ix->key, sizeof(ix->key), 1, f) == 1 &&
           fwrite(head, sizeof(int32_t), 5, f) == 5 && fwrite(digest, 1, sizeof(digest), f) == sizeof(digest) &&
           fwrite(ix->entries, sizeof(*ix->entries), (size_t)ix->count, f) == (size_t)ix->count);
    assert(!fclose(f));
}

/* NUL bytes in a G-code file. A line is read with fgets, so the line the parser sees ends at the first NUL (a marker
 * that holds a NUL is not recognized, the rest of such a line is not parsed). The stored positions, however, must be the
 * real file positions: the number of bytes read, never the length of that shortened string. */
#define NUL_BLOCK_MAX 160
static size_t nul_block(char *out, int layer) { /* every block has the same length, layers 1..4 */
    return (size_t)snprintf(out, NUL_BLOCK_MAX, ";LAYER_CHANGE\n;Z:0.%d\nG1 Z0.%d F600\nG1 X20 Y%d E%d.500 F1800\n"
                            "G1 X30 Y%d E%d.000 F1800\n", layer * 2, layer * 2, 20 + layer, layer, 20 + layer, layer);
}

static size_t nul_text(char *out, const char *head) {
    size_t n = strlen(head);
    memcpy(out, head, n);
    for (int layer = 1; layer <= 4; layer++) n += nul_block(out + n, layer);
    return n;
}

/* Inserts count bytes at the start of the nth (0-based) occurrence of needle. */
static size_t nul_insert(char *buf, size_t len, const char *needle, int nth, const char *bytes, size_t count) {
    size_t m = strlen(needle), at = 0;
    for (int seen = -1; ; at++) {
        assert(at + m <= len);
        if (!memcmp(buf + at, needle, m) && ++seen == nth) break;
    }
    memmove(buf + at + count, buf + at, len - at);
    memcpy(buf + at, bytes, count);
    return len + count;
}

static void nul_check(const char *dir, const char *name, const char *data, size_t len, int markers_expected) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *w = fopen(path, "wb"); assert(w && fwrite(data, 1, len, w) == len && !fclose(w));
    FILE *f = fopen(path, "rb"); assert(f);
    /* The true place of every marker line the parser can see: the text before the first NUL starts with the marker. */
    long expect[16]; int n = 0;
    for (size_t at = 0; at < len;) {
        const char *nl = memchr(data + at, '\n', len - at);
        size_t end = nl ? (size_t)(nl - data) + 1 : len;
        if (!strncmp(data + at, ";LAYER_CHANGE", 13) && memchr(data + at, 0, 13) == NULL) { assert(n < 16); expect[n++] = (long)at; }
        at = end;
    }
    assert(n == markers_expected);
    layer_index index;
    assert(layer_index_build(f, &index, 0, NULL, NULL) == LAYER_PATH_OK && index.seen_marker);
    if (index.count != n) { fprintf(stderr, "%s: map has %d layers, %d markers\n", name, index.count, n); assert(0); }
    for (int i = 0; i < n; i++)
        if (index.entries[i].offset != expect[i]) {
            fprintf(stderr, "%s: layer %d starts at byte %ld in the file, the map says %lld\n", name, i, expect[i],
                    (long long)index.entries[i].offset);
            assert(0);
        }
    /* The read through the map gives what the full scan gives, and the map is used (no fallback to the full scan). */
    for (int layer = 1; layer <= index.total; layer++) {
        layer_path_info a, b;
        int ra = full(f, layer, &a), rb = jump(f, &index, layer, &b);
        assert(rb != LAYER_INDEX_BAD && ra == rb);
        assert(a.point_count == b.point_count && a.path_count == b.path_count && same_z(a.z, b.z) &&
               !memcmp(pts_a, pts_b, sizeof(layer_point) * (size_t)a.point_count));
    }
    /* The marks that parse_at stores for a resumed read hold the true place too, and a read that resumes at one gives
     * what a read from the start of the file gives. */
    layer_path_mark marks[LAYER_PATH_INDEX_MAX]; int mark_count = 0;
    layer_path_info info; struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); rewind(f);
    int rc = gcode_layer_path_parse_at(f, index.total + 1, pts_b, LAYER_PATH_COLLECT_MAX, starts_b, LAYER_PATH_PATHS_MAX, &info,
                                       &t, 0x7fffffffL, 0, NULL, marks, &mark_count, LAYER_PATH_INDEX_MAX);
    assert(rc == LAYER_PATH_OK || rc == LAYER_PATH_OUTSIDE); /* a layer past the last one reads the whole file */
    assert(mark_count == n);
    for (int i = 0; i < mark_count; i++) assert(marks[i].layer == i && marks[i].offset == expect[i]);
    for (int layer = 1; layer <= index.total; layer++) {
        layer_path_info a, b; layer_path_mark resume;
        assert(full(f, layer, &a) == LAYER_PATH_OK);
        assert(layer_path_mark_pick(marks, mark_count, layer - 1, &resume));
        rewind(f); clock_gettime(CLOCK_MONOTONIC, &t);
        assert(gcode_layer_path_parse_at(f, layer, pts_b, LAYER_PATH_COLLECT_MAX, starts_b, LAYER_PATH_PATHS_MAX, &b, &t,
                                         0x7fffffffL, 0, &resume, NULL, NULL, 0) == LAYER_PATH_OK);
        assert(a.point_count == b.point_count && a.path_count == b.path_count && same_z(a.z, b.z) &&
               !memcmp(pts_a, pts_b, sizeof(layer_point) * (size_t)a.point_count));
    }
    layer_index_free(&index); fclose(f); unlink(path);
}

static void nul_cases(const char *dir) {
    static char buf[16384];
    const char *head = "; header\n; total layer number: 4\nG90\nM82\nG28\n";
    size_t len;
    /* N1: NUL in comments (header, and the ;Z: line of layer 2). */
    len = nul_text(buf, head);
    len = nul_insert(buf, len, "er\n", 0, "\0\0\0", 3);
    len = nul_insert(buf, len, "\nG1 Z0.4", 0, " \0 note", 7);
    nul_check(dir, "n1.gcode", buf, len, 4);
    /* N2: NUL in command lines (the word before it is parsed, the rest of the line is not). */
    len = nul_text(buf, head);
    len = nul_insert(buf, len, " Y21 E1.500", 0, "\0", 1);
    len = nul_insert(buf, len, " Y22 E2.500", 0, "\0", 1);
    nul_check(dir, "n2.gcode", buf, len, 4);
    /* N3: the first two marker lines begin with a NUL: they are not markers, the other two are. */
    len = nul_text(buf, head);
    len = nul_insert(buf, len, ";LAYER_CHANGE", 0, "\0", 1);
    len = nul_insert(buf, len, ";LAYER_CHANGE", 1, "\0", 1);
    nul_check(dir, "n3.gcode", buf, len, 2);
    /* N4: a NUL inside the marker word of the first marker. */
    len = nul_text(buf, head);
    len = nul_insert(buf, len, "_CHANGE", 0, "\0", 1);
    nul_check(dir, "n4.gcode", buf, len, 3);
    /* N5: a NUL behind the marker (the marker still counts). */
    len = nul_text(buf, head);
    len = nul_insert(buf, len, "\n;Z:0.2", 0, "\0junk", 5);
    len = nul_insert(buf, len, "\n;Z:0.4", 0, "\0junk", 5);
    nul_check(dir, "n5.gcode", buf, len, 4);
    /* N6: a line that is only NUL bytes. */
    len = nul_text(buf, head);
    len = nul_insert(buf, len, ";Z:0.2", 0, "\0\0\0\n", 4);
    nul_check(dir, "n6.gcode", buf, len, 4);
    /* N7: 3000 NUL bytes in the header, before the first marker. */
    {
        char nuls[3000]; memset(nuls, 0, sizeof(nuls));
        len = nul_text(buf, head);
        len = nul_insert(buf, len, "er\n", 0, nuls, sizeof(nuls));
        nul_check(dir, "n7.gcode", buf, len, 4);
    }
    /* N10: the blocks are all as long as the line of NUL bytes before them; counting it as empty put every stored
     * place exactly one block too early, on the marker of the layer before, which looks right. */
    {
        char block[NUL_BLOCK_MAX]; size_t b = nul_block(block, 1);
        char line[NUL_BLOCK_MAX]; memset(line, 'x', b); line[0] = 0; line[b - 1] = '\n';
        len = (size_t)snprintf(buf, sizeof(buf), "; header\n");
        memcpy(buf + len, line, b); len += b;
        len += nul_text(buf + len, "; total layer number: 4\nG90\nM82\nG28\n");
        nul_check(dir, "n10.gcode", buf, len, 4);
    }
    /* A line of NUL bytes across the end of the 4096-byte read buffer, with and without a line end after it. */
    for (int tail = 0; tail < 2; tail++)
        for (int at = 4090; at <= 4100; at++) {
            len = (size_t)snprintf(buf, sizeof(buf), "; total layer number: 4\n");
            memset(buf + len, 'c', (size_t)at - len); len = (size_t)at;
            buf[len++] = 0; buf[len++] = 0; buf[len++] = ';'; buf[len++] = '\n';
            len += nul_text(buf + len, "G90\n");
            if (tail) { /* the file ends in the middle of a NUL line */
                memcpy(buf + len, "G1 X1\0\0", 7); len += 7;
            }
            nul_check(dir, "edge.gcode", buf, len, 4);
        }
}

int main(int argc, char **argv) {
    (void)layer_path_mark_pick;
    (void)gcode_layer_path_parse_at;
    (void)sha256_fd; (void)gcode_layer_path_query; (void)gcode_layer_path_json;
    char dir[] = "/tmp/cc2-layer-index-XXXXXX";
    assert(mkdtemp(dir));
    int layers, totals;

    /* ElegooSlicer shape: ;LAYER:0 in the start block, then ;LAYER_CHANGE ... ;Z: ... ;LAYER:n. */
    FILE *f = gcode(dir, "elegoo.gcode",
        "; total layer number: 3\nG90\nM82\n;LAYER:0\nG1 X1 Y1 E1\n"
        ";LAYER_CHANGE\n;Z:0.2\n;LAYER:1\nG1 Z0.2\nG1 X10 Y10 E2\nG1 X20 Y10 E3\n"
        ";LAYER_CHANGE\n;Z:0.4\n;LAYER:2\nG1 Z0.4\nM83\nG1 X20 Y20 E1\nG92 E0\nG1 X10 Y20 E0.5\n"
        ";LAYER_CHANGE\n;Z:0.6\n;LAYER:3\nG91\nG1 X5 Y0 E0.2\nG90\nG2 X10 Y10 I5 J0 E0.3\nG1 Z5\n");
    assert(compare_all("elegoo", f, &layers, &totals) == 0 && layers == 3 && totals == 0);
    fclose(f);

    /* Cura shape: ;LAYER:N only, relative extrusion from the start, a layer without moves. */
    f = gcode(dir, "cura.gcode",
        ";LAYER_COUNT:4\nM83\n;LAYER:0\nG0 X0 Y0\nG1 X5 Y0 E1\n;LAYER:1\n;LAYER:2\nG1 X5 Y5 E1\n"
        "G0 X9 Y9\nG1 X9 Y1 E1\n;LAYER:3\nG3 X1 Y1 R4 E1\nN12 G1 X2 Y2 E1*33\n");
    assert(compare_all("cura", f, &layers, &totals) == 0 && layers == 4);
    fclose(f);

    /* The header promises more layers than the file marks: the counted layers win (layer count
     * only differs from the full scan, which still reads the header). */
    f = gcode(dir, "short.gcode", "; total layer number: 4\n;LAYER_CHANGE\nG1 X1 Y1 E1\n;LAYER_CHANGE\nG1 X2 Y2 E2\n");
    assert(compare_all("short", f, &layers, &totals) == 0 && layers == 2);
    fclose(f);

    /* A number the file leaves out is answered by the full scan: the index does not serve every layer. */
    f = gcode(dir, "gap.gcode", ";LAYER:0\nG1 X1 Y1 E1\n;LAYER:1\nG1 X2 Y2 E1\n;LAYER:3\nG1 X3 Y3 E1\n");
    {   layer_index gapped; assert(layer_index_build(f, &gapped, 0, NULL, NULL) == LAYER_PATH_OK);
        struct timespec at; clock_gettime(CLOCK_MONOTONIC, &at); layer_path_info gap_info;
        assert(gapped.count == 3 && !layer_index_serves_all(&gapped) && layer_index_can_serve(&gapped, 2) &&
               !layer_index_can_serve(&gapped, 3) && layer_index_can_serve(&gapped, 4));
        assert(gcode_layer_path_parse_indexed(f, &gapped, 3, pts_b, LAYER_PATH_COLLECT_MAX, starts_b, LAYER_PATH_PATHS_MAX,
                                              &gap_info, &at, 1000, 0) == LAYER_INDEX_NONE); /* the number it skips */
        layer_index_free(&gapped); }
    fclose(f);

    /* Long line across the 4096-byte read buffer and CRLF endings. */
    {
        size_t cap = 20000; char *text = malloc(cap); size_t used = 0;
        used += (size_t)snprintf(text + used, cap - used, ";LAYER_CHANGE\r\n;Z:0.2\r\nG1 X1 Y1 E1 ;");
        for (int i = 0; i < 9000; i++) text[used++] = 'x';
        used += (size_t)snprintf(text + used, cap - used, "\r\nG1 X2 Y2 E2\r\n;LAYER_CHANGE\r\n;Z:0.4\r\nG1 X3 Y3 E3\r\n");
        text[used] = 0;
        f = gcode(dir, "long.gcode", text);
        free(text);
        assert(compare_all("long", f, &layers, &totals) == 0 && layers == 2);
        fclose(f);
    }

    nul_cases(dir);

    /* Files the index cannot serve exactly: they report LAYER_INDEX_NONE. */
    layer_index index; layer_path_info info; struct timespec start; clock_gettime(CLOCK_MONOTONIC, &start);
    f = gcode(dir, "plain.gcode", "G1 X1 Y1 E1\nG1 X2 Y2 E2\n");
    assert(layer_index_build(f, &index, 0, NULL, NULL) == LAYER_PATH_OK && !index.seen_marker);
    assert(!layer_index_can_serve(&index, 1));
    assert(gcode_layer_path_parse_indexed(f, &index, 1, pts_b, LAYER_PATH_COLLECT_MAX, starts_b, LAYER_PATH_PATHS_MAX,
                                          &info, &start, 1000, 0) == LAYER_INDEX_NONE);
    layer_index_free(&index); fclose(f);
    f = gcode(dir, "repeat.gcode", ";LAYER:0\nG1 X1 Y1 E1\n;LAYER:1\nG1 X2 Y2 E2\n;LAYER:0\nG1 X3 Y3 E3\n");
    assert(layer_index_build(f, &index, 0, NULL, NULL) == LAYER_PATH_OK && index.repeats);
    assert(!layer_index_can_serve(&index, 1));
    assert(gcode_layer_path_parse_indexed(f, &index, 1, pts_b, LAYER_PATH_COLLECT_MAX, starts_b, LAYER_PATH_PATHS_MAX,
                                          &info, &start, 1000, 0) == LAYER_INDEX_NONE);
    layer_index_free(&index); fclose(f);

    /* The line at a stored offset must be a layer marker. A map whose offset points elsewhere is not used even though its
     * checksum is right (it is written by layer_index_save after the change, so the CRC-32 fits): the layer comes back as
     * LAYER_INDEX_BAD, the caller reads from the start, and that gives the layer the intact map gave. */
    {
        const char *text = ";LAYER_CHANGE\n;Z:0.2\n;LAYER:0\nG1 X1 Y1 E1\nG1 X2 Y2 E2\n"
                           ";LAYER_CHANGE\n;Z:0.4\n;LAYER:1\nG1 X3 Y3 E3\nG1 X4 Y4 E4\n"
                           ";LAYER_CHANGE\n;Z:0.6\n;LAYER:2\nG1 X5 Y5 E5\n";
        f = gcode(dir, "plausible.gcode", text);
        layer_index good, bent, loaded2;
        assert(layer_index_build(f, &good, 0, NULL, NULL) == LAYER_PATH_OK && good.count == 3 && good.total == 3);
        char mp[600]; snprintf(mp, sizeof(mp), "%s/plausible.idx", dir);
        const long marker = (long)good.entries[1].offset;
        assert(!strncmp(text + marker, ";LAYER_CHANGE", 13));
        layer_path_info want, got;
        assert(jump(f, &good, 2, &want) == LAYER_PATH_OK && want.point_count >= 1);
        layer_point want_points[LAYER_PATH_COLLECT_MAX]; int want_count = want.point_count;
        memcpy(want_points, pts_b, sizeof(layer_point) * (size_t)want_count);
        /* Offsets that are not the start of a marker line: one byte in, the middle of the marker, the newline before it,
         * the line after it (a comment that is no marker), a G-code line, beyond the end of the file, below zero, and
         * exactly at the end. */
        long bad[] = { marker + 1, marker + 6, marker - 1, (long)(strstr(text + marker, ";Z:") - text),
                       (long)(strstr(text + marker, "G1 X3") - text), (long)strlen(text) + 10,
                       -5L, (long)strlen(text) };
        for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
            bent = good;
            bent.entries = malloc(sizeof(*bent.entries) * (size_t)good.count); assert(bent.entries);
            memcpy(bent.entries, good.entries, sizeof(*bent.entries) * (size_t)good.count);
            bent.entries[1].offset = bad[i];
            assert(layer_index_save(mp, &bent) == 0 && layer_index_load(mp, &loaded2) == 0); /* the checksum is right */
            assert(loaded2.entries[1].offset == bad[i]);
            if (gcode_layer_path_parse_indexed(f, &loaded2, 2, pts_b, LAYER_PATH_COLLECT_MAX, starts_b,
                                               LAYER_PATH_PATHS_MAX, &got, &start, 1000, 0) != LAYER_INDEX_BAD) {
                fprintf(stderr, "offset %ld (marker at %ld) was used\n", bad[i], marker);
                assert(0);
            }
            /* The other layers of the same map still come through it, and the full scan gives what the intact map gave. */
            assert(jump(f, &loaded2, 1, &got) == LAYER_PATH_OK && jump(f, &loaded2, 3, &got) == LAYER_PATH_OK);
            assert(full(f, 2, &got) == LAYER_PATH_OK && got.point_count == want_count &&
                   !memcmp(pts_a, want_points, sizeof(layer_point) * (size_t)want_count));
            layer_index_free(&loaded2); free(bent.entries);
        }
        /* The intact map is used (the check does not refuse a right offset), also from the same map file. */
        assert(layer_index_save(mp, &good) == 0 && layer_index_load(mp, &loaded2) == 0 &&
               jump(f, &loaded2, 2, &got) == LAYER_PATH_OK && got.point_count == want_count &&
               !memcmp(pts_b, want_points, sizeof(layer_point) * (size_t)want_count));
        layer_index_free(&loaded2); layer_index_free(&good); fclose(f); unlink(mp);
        /* A file numbered by ";LAYER:N" (Cura): the marker at the offset must also be the entry's own layer. The marker of
         * another layer is a marker line, but not this layer's. */
        const char *cura = ";LAYER:0\nG1 X1 Y1 E1\nG1 X2 Y2 E2\n;LAYER:1\nG1 X3 Y3 E3\nG1 X4 Y4 E4\n;LAYER:2\nG1 X5 Y5 E5\n";
        f = gcode(dir, "plausible-cura.gcode", cura);
        assert(layer_index_build(f, &good, 0, NULL, NULL) == LAYER_PATH_OK && good.count == 3 && !good.entries[0].change_mode);
        assert(jump(f, &good, 2, &want) == LAYER_PATH_OK);
        bent = good; bent.entries = malloc(sizeof(*bent.entries) * (size_t)good.count); assert(bent.entries);
        memcpy(bent.entries, good.entries, sizeof(*bent.entries) * (size_t)good.count);
        bent.entries[1].offset = good.entries[2].offset; /* layer 2's marker where layer 1's should be */
        assert(layer_index_save(mp, &bent) == 0 && layer_index_load(mp, &loaded2) == 0);
        assert(gcode_layer_path_parse_indexed(f, &loaded2, 2, pts_b, LAYER_PATH_COLLECT_MAX, starts_b, LAYER_PATH_PATHS_MAX,
                                              &got, &start, 1000, 0) == LAYER_INDEX_BAD);
        assert(jump(f, &loaded2, 3, &got) == LAYER_PATH_OK); /* its own marker is accepted */
        layer_index_free(&loaded2); free(bent.entries); layer_index_free(&good); fclose(f); unlink(mp);
    }

    /* A layer above its own byte budget is refused; the worker can take the next request. */
    f = gcode(dir, "budget.gcode", ";LAYER_CHANGE\nG1 X1 Y1 E1\nG1 X2 Y2 E2\nG1 X3 Y3 E3\n;LAYER_CHANGE\nG1 X4 Y4 E4\n");
    assert(layer_index_build(f, &index, 0, NULL, NULL) == LAYER_PATH_OK);
    assert(gcode_layer_path_parse_indexed(f, &index, 1, pts_b, LAYER_PATH_COLLECT_MAX, starts_b, LAYER_PATH_PATHS_MAX,
                                          &info, &start, 1000, 20) == LAYER_PATH_LIMIT);
    assert(gcode_layer_path_parse_indexed(f, &index, 2, pts_b, LAYER_PATH_COLLECT_MAX, starts_b, LAYER_PATH_PATHS_MAX,
                                          &info, &start, 1000, 0) == LAYER_PATH_OK && info.point_count >= 1);
    assert(gcode_layer_path_parse_indexed(f, &index, 3, pts_b, LAYER_PATH_COLLECT_MAX, starts_b, LAYER_PATH_PATHS_MAX,
                                          &info, &start, 1000, 0) == LAYER_PATH_OUTSIDE && info.total_layers == 2);
    layer_index_free(&index);
    fclose(f);

    /* Index build must not treat a wall-clock timeout as success when target0 is open (-1). */
    {
        size_t cap = 256 * 1024, used = 0;
        char *text = malloc(cap);
        assert(text);
        for (int i = 0; i < 4000 && used + 80 < cap; i++)
            used += (size_t)snprintf(text + used, cap - used, ";LAYER_CHANGE\n;Z:%g\nG1 X%g Y%g E1\n", i * 0.2, i * 0.1, i * 0.1);
        f = gcode(dir, "timeout.gcode", text);
        free(text);
    }
    assert(layer_index_build(f, &index, 1, NULL, NULL) == LAYER_PATH_LIMIT);
    layer_index_free(&index);
    fclose(f);

    f = gcode(dir, "budget.gcode", ";LAYER_CHANGE\nG1 X1 Y1 E1\nG1 X2 Y2 E2\nG1 X3 Y3 E3\n;LAYER_CHANGE\nG1 X4 Y4 E4\n");
    assert(layer_index_build(f, &index, 0, NULL, NULL) == LAYER_PATH_OK);

    /* Storage: round trip, then a change of the G-code makes it stale. */
    char path[1024], gpath[600];
    snprintf(gpath, sizeof(gpath), "%s/budget.gcode", dir);
    assert(layer_index_path(dir, gpath, path, sizeof(path)) == 0);
    assert(layer_index_save(path, &index) == 0);
    layer_index loaded;
    assert(layer_index_load_valid(path, f, &loaded) == 0);
    assert(loaded.count == index.count && loaded.total == index.total &&
           !memcmp(loaded.entries, index.entries, sizeof(*index.entries) * (size_t)index.count));
    layer_index_free(&loaded);
    /* The stored map is checked against its own CRC-32: one flipped bit anywhere in it (version, key, counts,
     * the checksum, the middle of an entry) is refused, never read as a layer that is slightly wrong. */
    {
        assert(index.count == 2);
        long key_at = 8 + 4 + 2 * 4, head_at = key_at + (long)sizeof(layer_index_key);
        long entries_at = head_at + 5 * 4 + 4, stride = (long)sizeof(layer_index_entry);
        long where[] = { 10, key_at + 3, head_at, head_at + 4 * 4, head_at + 5 * 4 + 2,
                         entries_at + stride, entries_at + stride + 40, entries_at + 2 * stride - 5 };
        for (size_t i = 0; i < sizeof(where) / sizeof(where[0]); i++) {
            flip_bit(path, where[i]);
            if (layer_index_load(path, &loaded) == 0) {
                fprintf(stderr, "a flipped bit at byte %ld of the stored map was read\n", where[i]);
                layer_index_free(&loaded);
                assert(0);
            }
            flip_bit(path, where[i]);
            assert(layer_index_load(path, &loaded) == 0 && loaded.count == 2);
            layer_index_free(&loaded);
        }
        /* And every bit of the whole file, one at a time (a CRC-32 finds every single flipped bit). */
        long file_size = entries_at + 2 * stride;
        for (long byte = 0; byte < file_size; byte++)
            for (int bit = 0; bit < 8; bit++) {
                FILE *g = fopen(path, "r+b"); assert(g);
                int c; assert(!fseek(g, byte, SEEK_SET) && (c = fgetc(g)) != EOF);
                assert(!fseek(g, byte, SEEK_SET) && fputc(c ^ (1 << bit), g) != EOF && !fclose(g));
                if (layer_index_load(path, &loaded) == 0) {
                    fprintf(stderr, "bit %d of byte %ld of the stored map: a damaged map was read\n", bit, byte);
                    layer_index_free(&loaded);
                    assert(0);
                }
                g = fopen(path, "r+b"); assert(g);
                assert(!fseek(g, byte, SEEK_SET) && fputc(c, g) != EOF && !fclose(g));
            }
        assert(layer_index_load(path, &loaded) == 0 && loaded.count == 2);
        layer_index_free(&loaded);
    }
    /* A map written by the first version (no checksum) is refused and counts as stale, so it is rebuilt. */
    write_version_1(path, &index);
    assert(layer_index_load(path, &loaded) != 0 && layer_index_load_valid(path, f, &loaded) == 1);
    assert(layer_index_save(path, &index) == 0 && layer_index_load_valid(path, f, &loaded) == 0);
    layer_index_free(&loaded);
    /* A map of version 2 (SHA-256 instead of the CRC-32, otherwise the same bytes) is refused too, and rebuilt. */
    write_version_2(path, &index);
    assert(layer_index_load(path, &loaded) != 0 && layer_index_load_valid(path, f, &loaded) == 1);
    assert(layer_index_save(path, &index) == 0 && layer_index_load_valid(path, f, &loaded) == 0);
    layer_index_free(&loaded);
    /* A map that names another version or another record size than this build's is refused although its checksum is
     * right: it was written by other code (or the entry struct changed since), so its bytes mean something else. */
    for (int other = 0; other < 2; other++) {
        FILE *g = fopen(path, "wb"); assert(g);
        int32_t version = other ? LAYER_INDEX_VERSION + 1 : LAYER_INDEX_VERSION;
        int32_t sizes[2] = { (int32_t)sizeof(layer_index_entry) + (other ? 0 : 8), (int32_t)sizeof(layer_index_key) };
        int32_t head[5] = { index.total, index.max_layer, index.seen_marker, index.repeats, index.count };
        uint32_t digest = layer_index_digest(version, sizes, &index.key, head, index.entries, index.count);
        assert(fwrite("CC2LIDX1", 1, 8, g) == 8 && fwrite(&version, sizeof(version), 1, g) == 1 &&
               fwrite(sizes, sizeof(int32_t), 2, g) == 2 && fwrite(&index.key, sizeof(index.key), 1, g) == 1 &&
               fwrite(head, sizeof(int32_t), 5, g) == 5 && fwrite(&digest, sizeof(digest), 1, g) == 1 &&
               fwrite(index.entries, sizeof(*index.entries), (size_t)index.count, g) == (size_t)index.count);
        assert(!fclose(g));
        assert(layer_index_load(path, &loaded) != 0);
        assert(layer_index_save(path, &index) == 0 && layer_index_load_valid(path, f, &loaded) == 0);
        layer_index_free(&loaded);
    }
    sleep(1);
    fseek(f, 0, SEEK_END); fputs("G1 X5 Y5 E5\n", f); fflush(f);
    assert(layer_index_load_valid(path, f, &loaded) == 1);
    /* The key ties the map to its file: every field of it, alone, makes the map stale. */
    {
        FILE *g = gcode(dir, "key.gcode", ";LAYER_CHANGE\nG1 X1 Y1 E1\n;LAYER_CHANGE\nG1 X2 Y2 E2\n");
        layer_index_key k, o;
        assert(layer_index_key_of(g, &k) == 0 && layer_index_key_same(&k, &k));
#define KEY_DIFFERS(change) do { o = k; change; assert(!layer_index_key_same(&k, &o)); } while (0)
        KEY_DIFFERS(o.size += 1);
        KEY_DIFFERS(o.mtime_s += 1);
        KEY_DIFFERS(o.mtime_ns ^= 1);
        KEY_DIFFERS(o.ctime_s += 1);
        KEY_DIFFERS(o.ctime_ns ^= 1);
        KEY_DIFFERS(o.ino += 1);
        KEY_DIFFERS(o.edges[0] ^= 1);
        KEY_DIFFERS(o.edges[31] ^= 1);
#undef KEY_DIFFERS
        /* A file of the same size that replaces it, with its old modification time put back, is stale: another
         * inode, another change time, other bytes in the first and last 64 KiB. */
        char key_path[1024], map_path[1024], other_path[1024];
        snprintf(key_path, sizeof(key_path), "%s/key.gcode", dir);
        snprintf(other_path, sizeof(other_path), "%s/key-new.gcode", dir);
        layer_index kept;
        assert(layer_index_build(g, &kept, 0, NULL, NULL) == LAYER_PATH_OK);
        assert(layer_index_path(dir, key_path, map_path, sizeof(map_path)) == 0 && layer_index_save(map_path, &kept) == 0);
        struct stat before; assert(!fstat(fileno(g), &before));
        fclose(g);
        FILE *same_size = fopen(other_path, "w"); assert(same_size);
        assert(fputs(";LAYER_CHANGE\nG1 X9 Y9 E9\n;LAYER_CHANGE\nG1 X8 Y8 E8\n", same_size) >= 0 && !fclose(same_size));
        struct stat check; assert(!stat(other_path, &check) && check.st_size == before.st_size);
        assert(!rename(other_path, key_path));
#ifdef __APPLE__
        struct timespec back[2] = { before.st_mtimespec, before.st_mtimespec };
#else
        struct timespec back[2] = { before.st_mtim, before.st_mtim };
#endif
        assert(!utimensat(AT_FDCWD, key_path, back, 0));
        g = fopen(key_path, "rb"); assert(g);
        layer_index stale;
        assert(layer_index_load_valid(map_path, g, &stale) == 1);
        fclose(g); layer_index_free(&kept);
    }
    /* A damaged index file is refused, never half read. */
    FILE *broken = fopen(path, "r+b"); assert(broken); fseek(broken, -3, SEEK_END); fputc('!', broken);
    fseek(broken, 0, SEEK_END); fputc('!', broken); fclose(broken);
    assert(layer_index_load(path, &loaded) != 0);
    layer_index_free(&index); fclose(f);

    for (int i = 1; i < argc; i++) {
        f = fopen(argv[i], "rb");
        assert(f);
        int bad = compare_all(argv[i], f, &layers, &totals);
        printf("%s: %d layers, %d differing, %d with a different layer count only\n", argv[i], layers, bad, totals);
        assert(bad == 0);
        fclose(f);
    }
    puts("PASS: layer index equals the full scan for every layer (ElegooSlicer, Cura, relative/absolute, arcs, "
         "long lines, CRLF), refuses files without markers or with repeated layers, per-layer budget, "
         "storage round trip, stale and damaged index files, a checksum over the stored map, old versions and other record sizes refused, "
         "an offset that is no marker line is not used");
    return 0;
}
