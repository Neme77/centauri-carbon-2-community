/* Coarse 3D shape of each print object in a G-code file: the outer wall of a few of its layers ("rings").
 * One pass reads the whole file line by line and collects every object at once; the moves are decoded by
 * the layer-path parser (layer_path_word_line, arcs, layer markers) through its sink, so both read G-code
 * the same way. Objects come from EXCLUDE_OBJECT_DEFINE / _START / _END (Klipper, written by OrcaSlicer,
 * ElegooSlicer and PrusaSlicer). Only the outer wall is kept, and per object at most
 * OBJECT_PREVIEW_RINGS_SHOWN rings of at most OBJECT_PREVIEW_RING_POINTS points each. */
#ifndef CC2_GCODE_OBJECT_PREVIEW_H
#define CC2_GCODE_OBJECT_PREVIEW_H
#include "gcode_layer_path.h"
#include <stdint.h>

#define OBJECT_PREVIEW_BYTES_MAX (50UL * 1024UL * 1024UL) /* larger files are refused unread */
#define OBJECT_PREVIEW_OBJECTS_MAX 64
#define OBJECT_PREVIEW_NAME_MAX 128 /* bytes with the terminator; a longer name is not taken */
#define OBJECT_PREVIEW_RINGS_SHOWN 24
#define OBJECT_PREVIEW_RINGS_KEPT 96 /* while reading; every second one goes when they run over */
#define OBJECT_PREVIEW_STEP_MIN 3    /* never denser than every third layer of the object */
#define OBJECT_PREVIEW_RING_POINTS 128
#define OBJECT_PREVIEW_RING_PATHS (OBJECT_PREVIEW_RING_POINTS / 2)
#define OBJECT_PREVIEW_MIN_DIST 0.5 /* mm between kept points; doubled while a ring runs over */
#define OBJECT_PREVIEW_UNIT 0.02    /* mm per stored step: int16 covers +-655 mm */

enum {
    OBJECT_PREVIEW_OK = 0,
    OBJECT_PREVIEW_TOO_LARGE = 1,
    OBJECT_PREVIEW_LIMIT = 2,
    OBJECT_PREVIEW_IO = 3,
    OBJECT_PREVIEW_NO_MEMORY = 4,
    OBJECT_PREVIEW_BAD_QUERY = 5,
    OBJECT_PREVIEW_BAD_OBJECT = 6,
    OBJECT_PREVIEW_HELD = 7 /* gcode_read_hold was set while reading */
};

typedef struct {
    float z;
    int ordinal;            /* 0-based layer of the object (counting only its layers with outer wall) */
    float min_dist;         /* mm */
    unsigned short count, cap;
    unsigned char paths;
    unsigned char starts[OBJECT_PREVIEW_RING_PATHS];
    int16_t *xy;            /* count pairs, in OBJECT_PREVIEW_UNIT */
} object_preview_ring;

typedef struct {
    char name[OBJECT_PREVIEW_NAME_MAX];
    int layers;             /* layers with outer wall (n) */
    int store_step;         /* while reading: every store_step-th object layer is kept */
    int step;               /* after the end: the drawn step */
    int truncated;
    int have_layer, last_layer, last_marked, current_ring;
    double last_z;
    float z_max, bounds[4];
    int have_bounds;
    int ring_count, ring_cap;
    object_preview_ring *rings;
} object_preview_object;

typedef struct {
    int count, cap;
    int truncated;          /* more objects than OBJECT_PREVIEW_OBJECTS_MAX, or a name too long */
    object_preview_object *objects;
    size_t bytes, peak;     /* bytes requested from the allocator and still held, and the most at any time */
    int failed;             /* an allocation failed */
} object_preview_result;

/* Test seam: the number of allocations that still succeed; negative means no limit. */
static long object_preview_alloc_budget = -1;

static void *object_preview_resize(object_preview_result *r, void *old, size_t old_size, size_t new_size) {
    if (new_size == 0) {
        free(old);
        r->bytes -= old_size;
        return NULL;
    }
    if (new_size > old_size) {
        if (object_preview_alloc_budget == 0) { r->failed = 1; return NULL; }
        if (object_preview_alloc_budget > 0) object_preview_alloc_budget--;
    }
    void *grown = realloc(old, new_size);
    if (!grown) {
        if (new_size > old_size) r->failed = 1;
        return new_size > old_size ? NULL : old;
    }
    r->bytes = r->bytes - old_size + new_size;
    if (r->bytes > r->peak) r->peak = r->bytes;
    return grown;
}

static void object_preview_drop_rings(object_preview_result *r, object_preview_object *o) {
    for (int i = 0; i < o->ring_count; i++)
        object_preview_resize(r, o->rings[i].xy, sizeof(int16_t) * 2 * o->rings[i].cap, 0);
    object_preview_resize(r, o->rings, sizeof(*o->rings) * (size_t)o->ring_cap, 0);
    o->rings = NULL;
    o->ring_count = o->ring_cap = 0;
    o->current_ring = -1;
}

/* Frees everything; the result can be filled again afterwards. bytes then tells what is still held
 * (0 unless the accounting is wrong) and peak the most it held. */
static void object_preview_free(object_preview_result *r) {
    if (!r) return;
    for (int i = 0; i < r->count; i++) object_preview_drop_rings(r, &r->objects[i]);
    object_preview_resize(r, r->objects, sizeof(*r->objects) * (size_t)r->cap, 0);
    size_t bytes = r->bytes, peak = r->peak;
    memset(r, 0, sizeof(*r));
    r->bytes = bytes;
    r->peak = peak;
}

/* Klipper upper-cases object names (EXCLUDE_OBJECT_DEFINE NAME=part_1 is reported as PART_1), so the name a
 * client holds can differ from the file's in case only. The exact name wins; otherwise case is ignored. */
static int object_preview_same_name(const char *a, const char *b) {
    for (; *a && *b; a++, b++)
        if (toupper((unsigned char)*a) != toupper((unsigned char)*b)) return 0;
    return !*a && !*b;
}

static int object_preview_find(const object_preview_result *r, const char *name) {
    for (int i = 0; i < r->count; i++)
        if (!strcmp(r->objects[i].name, name)) return i;
    for (int i = 0; i < r->count; i++)
        if (object_preview_same_name(r->objects[i].name, name)) return i;
    return -1;
}

static void object_preview_restart(object_preview_object *o) {
    o->layers = 0;
    o->store_step = 1;
    o->step = 0;
    o->truncated = 0;
    o->have_layer = 0;
    o->current_ring = -1;
    o->have_bounds = 0;
    o->z_max = 0;
}

static int object_preview_add(object_preview_result *r, const char *name) {
    int found = object_preview_find(r, name);
    if (found >= 0) return found;
    if (r->count >= OBJECT_PREVIEW_OBJECTS_MAX) { r->truncated = 1; return -1; }
    if (r->count == r->cap) {
        int cap = r->cap ? r->cap * 2 : 4;
        if (cap > OBJECT_PREVIEW_OBJECTS_MAX) cap = OBJECT_PREVIEW_OBJECTS_MAX;
        object_preview_object *grown = object_preview_resize(r, r->objects, sizeof(*r->objects) * (size_t)r->cap,
                                                             sizeof(*r->objects) * (size_t)cap);
        if (!grown) return -1;
        r->objects = grown;
        r->cap = cap;
    }
    object_preview_object *o = &r->objects[r->count];
    memset(o, 0, sizeof(*o));
    snprintf(o->name, sizeof(o->name), "%s", name);
    object_preview_restart(o);
    return r->count++;
}

static long long object_preview_d2(const int16_t *a, const int16_t *b) {
    long long dx = (long long)a[0] - b[0], dy = (long long)a[1] - b[1];
    return dx * dx + dy * dy;
}

/* Thins every path of the ring to the ring's minimum distance; the first and the last point of each
 * path stay. */
static void object_preview_rethin(object_preview_ring *g) {
    double units = g->min_dist / OBJECT_PREVIEW_UNIT;
    long long limit = (long long)(units * units);
    int write = 0;
    for (int p = 0; p < g->paths; p++) {
        int from = g->starts[p], to = (p + 1 < g->paths) ? g->starts[p + 1] : g->count;
        int at = write;
        g->starts[p] = (unsigned char)at;
        if (from >= to) continue;
        memmove(&g->xy[2 * write], &g->xy[2 * from], sizeof(int16_t) * 2);
        write++;
        for (int i = from + 1; i < to; i++) {
            int last = (i == to - 1);
            if (!last && object_preview_d2(&g->xy[2 * i], &g->xy[2 * (write - 1)]) < limit) continue;
            if (last && write - at >= 2 && object_preview_d2(&g->xy[2 * i], &g->xy[2 * (write - 1)]) < limit)
                write--; /* the end replaces a kept point too close before it */
            memmove(&g->xy[2 * write], &g->xy[2 * i], sizeof(int16_t) * 2);
            write++;
        }
    }
    g->count = (unsigned short)write;
}

/* Makes room for `need` more points, thinning the ring with a doubled distance while it is full.
 * Returns 0 when the ring cannot take them (its paths are down to their end points). */
static int object_preview_room(object_preview_result *r, object_preview_ring *g, int need) {
    while (g->count + need > OBJECT_PREVIEW_RING_POINTS) {
        if (g->min_dist > 1024) return 0;
        g->min_dist *= 2;
        object_preview_rethin(g);
    }
    if (g->count + need > g->cap) {
        int cap = g->cap ? g->cap * 2 : 16;
        while (cap < g->count + need) cap *= 2;
        if (cap > OBJECT_PREVIEW_RING_POINTS) cap = OBJECT_PREVIEW_RING_POINTS;
        int16_t *grown = object_preview_resize(r, g->xy, sizeof(int16_t) * 2 * g->cap, sizeof(int16_t) * 2 * (size_t)cap);
        if (!grown) return 0;
        g->xy = grown;
        g->cap = (unsigned short)cap;
    }
    return 1;
}

static int object_preview_units(double mm, int16_t *out) {
    double v = floor(mm / OBJECT_PREVIEW_UNIT + 0.5);
    if (!(v >= -32767 && v <= 32767)) return 0;
    *out = (int16_t)v;
    return 1;
}

/* Starts a path at a, or continues the last one; then adds b. Returns 1 when the path is open. */
static int object_preview_ring_move(object_preview_result *r, object_preview_object *o, object_preview_ring *g,
                                    int open, double ax, double ay, double bx, double by) {
    int16_t a[2], b[2];
    if (!object_preview_units(ax, &a[0]) || !object_preview_units(ay, &a[1]) ||
        !object_preview_units(bx, &b[0]) || !object_preview_units(by, &b[1])) { o->truncated = 1; return 0; }
    if (open && g->paths > 0 && g->count > g->starts[g->paths - 1] &&
        (g->xy[2 * (g->count - 1)] != a[0] || g->xy[2 * (g->count - 1) + 1] != a[1])) open = 0;
    if (!open) {
        if (g->paths > 0 && g->count - g->starts[g->paths - 1] < 2) {
            g->count = g->starts[g->paths - 1]; /* a path of one point draws nothing */
            g->paths--;
        }
        if (g->paths >= OBJECT_PREVIEW_RING_PATHS || !object_preview_room(r, g, 2)) { o->truncated = 1; return 0; }
        g->starts[g->paths++] = (unsigned char)g->count;
        g->xy[2 * g->count] = a[0];
        g->xy[2 * g->count + 1] = a[1];
        g->count++;
    }
    int from = g->starts[g->paths - 1];
    int16_t *last = &g->xy[2 * (g->count - 1)];
    if (last[0] == b[0] && last[1] == b[1]) return 1;
    if (g->count - from >= 2) {
        double units = g->min_dist / OBJECT_PREVIEW_UNIT;
        if (object_preview_d2(last - 2, last) < (long long)(units * units)) {
            last[0] = b[0]; /* the end moves on; the point before stays */
            last[1] = b[1];
            return 1;
        }
    }
    if (!object_preview_room(r, g, 1)) {
        if (r->failed) return 0;
        /* Every path is down to its two end points: the end moves on. */
        if (g->count - g->starts[g->paths - 1] >= 2) {
            g->xy[2 * (g->count - 1)] = b[0];
            g->xy[2 * (g->count - 1) + 1] = b[1];
            return 1;
        }
        o->truncated = 1;
        return 0;
    }
    g->xy[2 * g->count] = b[0];
    g->xy[2 * g->count + 1] = b[1];
    g->count++;
    return 1;
}

/* A new layer of the object: counted, and kept when it falls on the storing step. While more than
 * OBJECT_PREVIEW_RINGS_KEPT would be kept, every second kept ring goes and the step doubles. */
static void object_preview_new_layer(object_preview_result *r, object_preview_object *o, double z) {
    int ordinal = o->layers++;
    if (o->layers == 1 || (float)z > o->z_max) o->z_max = (float)z;
    o->current_ring = -1;
    if (ordinal % o->store_step) return;
    if (o->ring_count >= OBJECT_PREVIEW_RINGS_KEPT) {
        int write = 0;
        for (int i = 0; i < o->ring_count; i++) {
            if (i % 2) object_preview_resize(r, o->rings[i].xy, sizeof(int16_t) * 2 * o->rings[i].cap, 0);
            else o->rings[write++] = o->rings[i];
        }
        o->ring_count = write;
        o->store_step *= 2;
        if (ordinal % o->store_step) return;
    }
    if (o->ring_count == o->ring_cap) {
        int cap = o->ring_cap ? o->ring_cap * 2 : 8;
        if (cap > OBJECT_PREVIEW_RINGS_KEPT) cap = OBJECT_PREVIEW_RINGS_KEPT;
        object_preview_ring *grown = object_preview_resize(r, o->rings, sizeof(*o->rings) * (size_t)o->ring_cap,
                                                           sizeof(*o->rings) * (size_t)cap);
        if (!grown) return;
        o->rings = grown;
        o->ring_cap = cap;
    }
    object_preview_ring *g = &o->rings[o->ring_count];
    memset(g, 0, sizeof(*g));
    g->z = (float)z;
    g->ordinal = ordinal;
    g->min_dist = (float)OBJECT_PREVIEW_MIN_DIST;
    o->current_ring = o->ring_count++;
}

/* After the last line: the drawn step is max(3, ceil(n / 24)), rounded up to a multiple of the storing
 * step; only the rings on it stay, each shrunk to its points. */
static void object_preview_finish(object_preview_result *r) {
    for (int k = 0; k < r->count; k++) {
        object_preview_object *o = &r->objects[k];
        int n = o->layers;
        int step = (n + OBJECT_PREVIEW_RINGS_SHOWN - 1) / OBJECT_PREVIEW_RINGS_SHOWN;
        if (step < OBJECT_PREVIEW_STEP_MIN) step = OBJECT_PREVIEW_STEP_MIN;
        if (o->store_step > 1) step = (step + o->store_step - 1) / o->store_step * o->store_step;
        o->step = step;
        int write = 0;
        for (int i = 0; i < o->ring_count; i++) {
            object_preview_ring *g = &o->rings[i];
            if (g->paths > 0 && g->count - g->starts[g->paths - 1] < 2) { g->count = g->starts[g->paths - 1]; g->paths--; }
            if (g->ordinal % step) {
                object_preview_resize(r, g->xy, sizeof(int16_t) * 2 * g->cap, 0);
                continue;
            }
            if (g->count < g->cap) {
                int16_t *fit = object_preview_resize(r, g->xy, sizeof(int16_t) * 2 * g->cap, sizeof(int16_t) * 2 * g->count);
                g->xy = fit;
                g->cap = g->count;
            }
            o->rings[write++] = *g;
        }
        o->ring_count = write;
        if (write < o->ring_cap) {
            object_preview_ring *fit = object_preview_resize(r, o->rings, sizeof(*o->rings) * (size_t)o->ring_cap,
                                                             sizeof(*o->rings) * (size_t)write);
            o->rings = fit;
            o->ring_cap = write;
        }
        o->current_ring = -1;
    }
    if (r->count < r->cap) {
        object_preview_object *fit = object_preview_resize(r, r->objects, sizeof(*r->objects) * (size_t)r->cap,
                                                           sizeof(*r->objects) * (size_t)r->count);
        r->objects = fit;
        r->cap = r->count;
    }
}

typedef struct {
    layer_path_state s;
    object_preview_result *r;
    int object;   /* index of the object being printed, -1 outside */
    int typed;    /* a ;TYPE: comment was seen */
    int outer;    /* the last ;TYPE: is an outer wall */
    int open;     /* the last kept move ended a path that the next one may continue */
} object_preview_scan;

static void object_preview_sink(void *context, double x0, double y0, double x1, double y1, int extruding) {
    object_preview_scan *c = context;
    if (!extruding || c->object < 0 || (c->typed && !c->outer) || c->r->failed) { c->open = 0; return; }
    object_preview_object *o = &c->r->objects[c->object];
    double z = c->s.have_z ? c->s.z : 0;
    int fresh;
    if (c->s.seen_marker) fresh = !o->have_layer || !o->last_marked || o->last_layer != c->s.current;
    else fresh = !o->have_layer || o->last_marked || fabs(o->last_z - z) > 1e-4;
    if (fresh) {
        object_preview_new_layer(c->r, o, z);
        o->have_layer = 1;
        o->last_marked = c->s.seen_marker;
        o->last_layer = c->s.current;
        o->last_z = z;
        c->open = 0;
    }
    float xs[2] = {(float)x0, (float)x1}, ys[2] = {(float)y0, (float)y1};
    for (int i = 0; i < 2; i++) {
        if (!o->have_bounds) {
            o->bounds[0] = o->bounds[2] = xs[i];
            o->bounds[1] = o->bounds[3] = ys[i];
            o->have_bounds = 1;
        }
        if (xs[i] < o->bounds[0]) o->bounds[0] = xs[i];
        if (ys[i] < o->bounds[1]) o->bounds[1] = ys[i];
        if (xs[i] > o->bounds[2]) o->bounds[2] = xs[i];
        if (ys[i] > o->bounds[3]) o->bounds[3] = ys[i];
    }
    if (o->current_ring < 0) { c->open = 0; return; }
    c->open = object_preview_ring_move(c->r, o, &o->rings[o->current_ring], c->open, x0, y0, x1, y1);
}

/* The outer wall as OrcaSlicer/ElegooSlicer, PrusaSlicer and Cura name it. */
static int object_preview_outer_type(const char *type) {
    static const char *names[] = {"Outer wall", "Overhang wall", "External perimeter", "Overhang perimeter", "WALL-OUTER"};
    size_t length = strlen(type);
    while (length && isspace((unsigned char)type[length - 1])) length--;
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (strlen(names[i]) == length && !strncasecmp(type, names[i], length)) return 1;
    return 0;
}

/* EXCLUDE_OBJECT_DEFINE / _START / _END: 1, 2, 3 and the NAME= value (quoted or up to a blank), else 0. */
static int object_preview_marker(const char *line, char name[OBJECT_PREVIEW_NAME_MAX], int *too_long) {
    static const char *words[] = {"EXCLUDE_OBJECT_DEFINE", "EXCLUDE_OBJECT_START", "EXCLUDE_OBJECT_END"};
    *too_long = 0;
    name[0] = 0;
    while (isspace((unsigned char)*line)) line++;
    int kind = 0;
    for (int i = 0; i < 3 && !kind; i++) {
        size_t n = strlen(words[i]);
        if (!strncasecmp(line, words[i], n) && (!line[n] || isspace((unsigned char)line[n]))) { kind = i + 1; line += n; }
    }
    if (!kind) return 0;
    for (const char *p = line; *p && *p != ';'; p++) {
        if (!isspace((unsigned char)p[-1]) || strncasecmp(p, "NAME=", 5)) continue;
        p += 5;
        char quote = (*p == '\'' || *p == '"') ? *p++ : 0;
        size_t used = 0;
        for (; *p && (quote ? *p != quote : !isspace((unsigned char)*p)); p++) {
            if ((unsigned char)*p < 32 || used + 1 >= OBJECT_PREVIEW_NAME_MAX) { *too_long = 1; name[0] = 0; return kind; }
            name[used++] = *p;
        }
        name[used] = 0;
        break;
    }
    return kind;
}

/* Reads the whole file and fills r (which the caller frees with object_preview_free in every case).
 * bytes_max 0 and time_ms 0 mean no limit. */
static int gcode_object_preview_parse(FILE *file, object_preview_result *r,
                                      const struct timespec *start, long time_ms, size_t bytes_max, size_t *bytes_read) {
    if (bytes_read) *bytes_read = 0;
    if (!file || !r) return OBJECT_PREVIEW_IO;
    memset(r, 0, sizeof(*r));
    object_preview_scan c;
    memset(&c, 0, sizeof(c));
    c.s.abs_xy = 1;
    c.s.abs_e = 1;
    c.s.current = -1;
    c.s.max_layer = -1;
    c.s.sink = object_preview_sink;
    c.s.sink_context = &c;
    c.r = r;
    c.object = -1;
    char line[LAYER_PATH_LINE_MAX];
    size_t bytes = 0;
    int continued = 0; /* the line did not fit into the buffer: the rest of it is skipped */
    while (fgets(line, sizeof(line), file)) {
        size_t n = strlen(line);
        int part = continued;
        continued = n > 0 && line[n - 1] != '\n';
        bytes += n;
        if (bytes_read) *bytes_read = bytes;
        if (bytes_max && bytes > bytes_max) return OBJECT_PREVIEW_TOO_LARGE;
        if (layer_path_deadline(start, time_ms)) return OBJECT_PREVIEW_LIMIT;
        if (atomic_load_explicit(&gcode_read_hold, memory_order_relaxed)) return OBJECT_PREVIEW_HELD;
        if (r->failed) return OBJECT_PREVIEW_NO_MEMORY;
        if (part) continue;
        int numbered = 0, kind = layer_path_marker(line, &numbered);
        if (kind) {
            layer_path_apply_marker(&c.s, kind, numbered, 0);
            c.open = 0;
            continue;
        }
        const char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (*p == ';') {
            p++;
            while (*p == ' ' || *p == '\t') p++;
            if (!strncmp(p, "TYPE:", 5)) {
                if (!c.typed) {
                    /* Files with type comments count only the outer wall; anything collected before the
                     * first one (no type yet) is not outer wall. */
                    for (int i = 0; i < r->count; i++) {
                        object_preview_drop_rings(r, &r->objects[i]);
                        object_preview_restart(&r->objects[i]);
                    }
                    c.typed = 1;
                }
                c.outer = object_preview_outer_type(p + 5);
                c.open = 0;
            }
            continue;
        }
        if (*p == 'E' || *p == 'e') {
            char name[OBJECT_PREVIEW_NAME_MAX];
            int too_long = 0, marker = object_preview_marker(p, name, &too_long);
            if (marker) {
                c.open = 0;
                if (marker == 3) c.object = -1;
                else if (too_long || !name[0]) {
                    if (too_long) r->truncated = 1;
                    if (marker == 2) c.object = -1;
                } else {
                    int index = object_preview_add(r, name); /* a START without DEFINE adds it, as Klipper does */
                    if (marker == 2) c.object = index;
                }
                if (r->failed) return OBJECT_PREVIEW_NO_MEMORY;
                continue;
            }
        }
        layer_path_word_line(&c.s, line, 0);
    }
    if (ferror(file)) return OBJECT_PREVIEW_IO;
    if (r->failed) return OBJECT_PREVIEW_NO_MEMORY;
    object_preview_finish(r);
    return r->failed ? OBJECT_PREVIEW_NO_MEMORY : OBJECT_PREVIEW_OK;
}

/* storage (optional, default internal), file and object. Unknown keys fail closed. */
static int gcode_object_preview_query(const char *query, char storage[16], char file[LAYER_PATH_NAME_MAX],
                                      char object[OBJECT_PREVIEW_NAME_MAX]) {
    int seen_storage = 0, seen_file = 0, seen_object = 0;
    if (!query || !storage || !file || !object) return OBJECT_PREVIEW_BAD_QUERY;
    storage[0] = file[0] = object[0] = 0;
    if (!*query) return OBJECT_PREVIEW_BAD_QUERY;
    while (*query) {
        const char *end = strchr(query, '&');
        if (!end) end = query + strlen(query);
        const char *equals = memchr(query, '=', (size_t)(end - query));
        if (!equals) return OBJECT_PREVIEW_BAD_QUERY;
        size_t key = (size_t)(equals - query), length = (size_t)(end - equals - 1);
        if (key == 7 && !memcmp(query, "storage", 7) && !seen_storage++) {
            if (!layer_path_decode(storage, 16, equals + 1, length)) return OBJECT_PREVIEW_BAD_QUERY;
        } else if (key == 4 && !memcmp(query, "file", 4) && !seen_file++) {
            if (!layer_path_decode(file, LAYER_PATH_NAME_MAX, equals + 1, length)) return OBJECT_PREVIEW_BAD_QUERY;
        } else if (key == 6 && !memcmp(query, "object", 6) && !seen_object++) {
            if (!layer_path_decode(object, OBJECT_PREVIEW_NAME_MAX, equals + 1, length)) return OBJECT_PREVIEW_BAD_OBJECT;
        } else return OBJECT_PREVIEW_BAD_QUERY;
        query = *end ? end + 1 : end;
        if (*end && !*query) return OBJECT_PREVIEW_BAD_QUERY;
    }
    if (!seen_storage) memcpy(storage, "internal", 9);
    else if (strcmp(storage, "internal") && strcmp(storage, "usb")) return OBJECT_PREVIEW_BAD_QUERY;
    if (!seen_file || !layer_path_safe_file(file)) return OBJECT_PREVIEW_BAD_QUERY;
    if (!seen_object) return OBJECT_PREVIEW_BAD_OBJECT;
    return OBJECT_PREVIEW_OK;
}

/* A length in hundredths of a millimetre, without trailing zeros: 12.5, -0.04, 7. */
static int object_preview_number(char *out, size_t cap, long long hundredths) {
    int negative = hundredths < 0;
    unsigned long long v = negative ? (unsigned long long)(-hundredths) : (unsigned long long)hundredths;
    unsigned long long whole = v / 100, part = v % 100;
    if (!part) return snprintf(out, cap, "%s%llu", negative ? "-" : "", whole);
    if (part % 10 == 0) return snprintf(out, cap, "%s%llu.%llu", negative ? "-" : "", whole, part / 10);
    return snprintf(out, cap, "%s%llu.%02llu", negative ? "-" : "", whole, part);
}

static long long object_preview_hundredths(double mm) {
    if (!(mm > -1e9 && mm < 1e9)) mm = mm > 0 ? 1e9 : -1e9; /* also NaN: keeps the conversion defined */
    return (long long)floor(mm * 100.0 + 0.5);
}

typedef struct { char *out; size_t used, cap; int failed; } object_preview_text;

static void object_preview_put(object_preview_text *t, const char *text) {
    size_t n = strlen(text);
    if (t->failed || t->used + n + 1 > t->cap) { t->failed = 1; return; }
    memcpy(t->out + t->used, text, n);
    t->used += n;
    t->out[t->used] = 0;
}

static void object_preview_put_number(object_preview_text *t, long long hundredths) {
    char number[32];
    object_preview_number(number, sizeof(number), hundredths);
    object_preview_put(t, number);
}

static void object_preview_put_string(object_preview_text *t, const char *value) {
    if (t->failed) return;
    layer_path_json_string(t->out, &t->used, t->cap, &t->failed, value);
    if (!t->failed) t->out[t->used] = 0;
}

/* The answer for one object, in a buffer the caller frees. NULL when out of memory. */
static char *gcode_object_preview_json(const char *storage, const char *file, const object_preview_result *r,
                                       int index, size_t *length) {
    const object_preview_object *o = &r->objects[index];
    size_t cap = 512 + 6 * (strlen(storage) + strlen(file) + strlen(o->name) + 3);
    for (int i = 0; i < r->count; i++) cap += 6 * strlen(r->objects[i].name) + 4;
    for (int i = 0; i < o->ring_count; i++) cap += 48 + (size_t)o->rings[i].paths * 4 + (size_t)o->rings[i].count * 2 * 9;
    object_preview_text t = {malloc(cap), 0, cap, 0};
    if (!t.out) return NULL;
    t.out[0] = 0;
    char head[96];
    object_preview_put(&t, "{\"storage\":");
    object_preview_put_string(&t, storage);
    object_preview_put(&t, ",\"file\":");
    object_preview_put_string(&t, file);
    object_preview_put(&t, ",\"object\":");
    object_preview_put_string(&t, o->name);
    snprintf(head, sizeof(head), ",\"layers\":%d,\"step\":%d,\"z_max\":", o->layers, o->step);
    object_preview_put(&t, head);
    if (o->layers > 0) object_preview_put_number(&t, object_preview_hundredths(o->z_max));
    else object_preview_put(&t, "null");
    object_preview_put(&t, ",\"bounds\":");
    if (o->have_bounds) {
        for (int i = 0; i < 4; i++) {
            object_preview_put(&t, i ? "," : "[");
            object_preview_put_number(&t, object_preview_hundredths(o->bounds[i]));
        }
        object_preview_put(&t, "]");
    } else object_preview_put(&t, "null");
    object_preview_put(&t, ",\"rings\":[");
    for (int i = 0; i < o->ring_count; i++) {
        const object_preview_ring *g = &o->rings[i];
        object_preview_put(&t, i ? ",{\"z\":" : "{\"z\":");
        object_preview_put_number(&t, object_preview_hundredths(g->z));
        object_preview_put(&t, ",\"paths\":[");
        for (int p = 0; p < g->paths; p++) {
            int from = g->starts[p], to = (p + 1 < g->paths) ? g->starts[p + 1] : g->count;
            object_preview_put(&t, p ? ",[" : "[");
            for (int k = from; k < to; k++) {
                if (k > from) object_preview_put(&t, ",");
                object_preview_put_number(&t, (long long)g->xy[2 * k] * 2);
                object_preview_put(&t, ",");
                object_preview_put_number(&t, (long long)g->xy[2 * k + 1] * 2);
            }
            object_preview_put(&t, "]");
        }
        object_preview_put(&t, "]}");
    }
    object_preview_put(&t, o->truncated || r->truncated ? "],\"truncated\":true,\"objects\":[" : "],\"truncated\":false,\"objects\":[");
    for (int i = 0; i < r->count; i++) {
        if (i) object_preview_put(&t, ",");
        object_preview_put_string(&t, r->objects[i].name);
    }
    object_preview_put(&t, "]}\n");
    if (t.failed) { free(t.out); return NULL; }
    *length = t.used;
    return t.out;
}

#endif
