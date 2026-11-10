/* Stream one G-code layer into thinned bed-coordinate polylines.
 * The file is read line by line; the whole file is never loaded. */
#ifndef CC2_GCODE_LAYER_PATH_H
#define CC2_GCODE_LAYER_PATH_H
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#define LAYER_PATH_POINTS_MAX 4096
#define LAYER_PATH_COLLECT_MAX 8192
#define LAYER_PATH_PATHS_MAX 512
#define LAYER_PATH_JSON_MAX (96 * 1024)
#define LAYER_PATH_TIME_SECONDS 20
#define LAYER_PATH_BYTES_MAX (128UL * 1024UL * 1024UL)
#define LAYER_PATH_LINE_MAX 4096
#define LAYER_PATH_NAME_MAX 512
#define LAYER_PATH_MIN_DIST 0.25
#define LAYER_PATH_DP_EPS 0.15

enum {
    LAYER_PATH_OK = 0,
    LAYER_PATH_OUTSIDE = 1,
    LAYER_PATH_TOO_LARGE = 2,
    LAYER_PATH_LIMIT = 3,
    LAYER_PATH_IO = 4,
    LAYER_PATH_BAD_QUERY = 5,
    LAYER_PATH_BAD_LAYER = 6
};

typedef struct { float x, y; } layer_point;
typedef struct {
    int layer, total_layers, truncated, point_count, path_count;
    double z;
} layer_path_info;

static int layer_path_deadline(const struct timespec *start, int seconds) {
    if (!start || seconds <= 0) return 0;
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec - start->tv_sec >= seconds;
}

static int layer_path_decode(char *out, size_t capacity, const char *in, size_t length) {
    size_t used = 0;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '%') {
            if (i + 2 >= length || !isxdigit((unsigned char)in[i + 1]) || !isxdigit((unsigned char)in[i + 2])) return 0;
            char hex[3] = {in[i + 1], in[i + 2], 0};
            c = (unsigned char)strtoul(hex, NULL, 16);
            i += 2;
        } else if (c == '+') c = ' ';
        if (c < 32 || c == 127 || used + 1 >= capacity) return 0;
        out[used++] = (char)c;
    }
    out[used] = 0;
    return used > 0;
}

static int layer_path_safe_file(const char *relative) {
    if (!relative || !relative[0] || relative[0] == '/' || strchr(relative, '\\')) return 0;
    size_t n = strlen(relative);
    if (n <= 6 || strcasecmp(relative + n - 6, ".gcode") != 0) return 0;
    const char *segment = relative;
    for (const char *cursor = relative; ; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch && ch < 32) return 0;
        if (ch == '/' || ch == '\0') {
            size_t length = (size_t)(cursor - segment);
            if (!length || (length == 1 && segment[0] == '.') ||
                (length == 2 && segment[0] == '.' && segment[1] == '.')) return 0;
            if (!ch) break;
            segment = cursor + 1;
        }
    }
    return 1;
}

/* storage (optional, default internal), file and layer. Unknown keys fail closed. */
static int gcode_layer_path_query(const char *query, char storage[16], char file[LAYER_PATH_NAME_MAX], int *layer) {
    int seen_storage = 0, seen_file = 0, seen_layer = 0;
    if (!query || !storage || !file || !layer) return LAYER_PATH_BAD_QUERY;
    storage[0] = 0;
    file[0] = 0;
    *layer = 0;
    if (!*query) return LAYER_PATH_BAD_QUERY;
    while (*query) {
        const char *end = strchr(query, '&');
        if (!end) end = query + strlen(query);
        const char *equals = memchr(query, '=', (size_t)(end - query));
        if (!equals) return LAYER_PATH_BAD_QUERY;
        size_t key = (size_t)(equals - query), length = (size_t)(end - equals - 1);
        if (key == 7 && !memcmp(query, "storage", 7) && !seen_storage++) {
            if (!layer_path_decode(storage, 16, equals + 1, length)) return LAYER_PATH_BAD_QUERY;
        } else if (key == 4 && !memcmp(query, "file", 4) && !seen_file++) {
            if (!layer_path_decode(file, LAYER_PATH_NAME_MAX, equals + 1, length)) return LAYER_PATH_BAD_QUERY;
        } else if (key == 5 && !memcmp(query, "layer", 5) && !seen_layer++) {
            char text[16];
            if (length == 0 || length >= sizeof(text)) return LAYER_PATH_BAD_LAYER;
            memcpy(text, equals + 1, length);
            text[length] = 0;
            if (text[0] == '%') {
                if (!layer_path_decode(text, sizeof(text), equals + 1, length)) return LAYER_PATH_BAD_LAYER;
            }
            if (!text[0] || !isdigit((unsigned char)text[0])) return LAYER_PATH_BAD_LAYER;
            for (size_t i = 0; text[i]; i++) if (!isdigit((unsigned char)text[i])) return LAYER_PATH_BAD_LAYER;
            char *tail = NULL;
            long value = strtol(text, &tail, 10);
            if (!tail || *tail || value < 1 || value > 1000000) return LAYER_PATH_BAD_LAYER;
            *layer = (int)value;
        } else return LAYER_PATH_BAD_QUERY;
        query = *end ? end + 1 : end;
        if (*end && !*query) return LAYER_PATH_BAD_QUERY;
    }
    if (!seen_storage) memcpy(storage, "internal", 9);
    else if (strcmp(storage, "internal") && strcmp(storage, "usb")) return LAYER_PATH_BAD_QUERY;
    if (!seen_file || !seen_layer) return seen_layer ? LAYER_PATH_BAD_QUERY : (seen_file ? LAYER_PATH_BAD_LAYER : LAYER_PATH_BAD_QUERY);
    if (!layer_path_safe_file(file)) return LAYER_PATH_BAD_QUERY;
    return LAYER_PATH_OK;
}

static const char *layer_path_find_ci(const char *text, const char *needle) {
    size_t length = strlen(needle);
    if (!length) return text;
    for (; *text; ++text)
        if (strncasecmp(text, needle, length) == 0) return text;
    return NULL;
}

static int layer_path_positive_after(const char *line, const char *marker) {
    const char *match = layer_path_find_ci(line, marker);
    if (!match) return 0;
    match += strlen(marker);
    while (*match && !isdigit((unsigned char)*match)) match++;
    if (!*match) return 0;
    long value = strtol(match, NULL, 10);
    return value > 0 && value <= 1000000 ? (int)value : 0;
}

/* 1 = ;LAYER_CHANGE (not BEFORE/AFTER), 2 = ;LAYER:N (0-based slicer index). */
static int layer_path_marker(const char *line, int *numbered) {
    while (isspace((unsigned char)*line)) line++;
    if (*line != ';') return 0;
    line++;
    while (isspace((unsigned char)*line)) line++;
    if (!strncasecmp(line, "LAYER_CHANGE", 12) &&
        !isalnum((unsigned char)line[12]) && line[12] != '_') return 1;
    if (!strncasecmp(line, "LAYER:", 6)) {
        line += 6;
        while (isspace((unsigned char)*line)) line++;
        if (!isdigit((unsigned char)*line)) return 0;
        long value = strtol(line, NULL, 10);
        if (value < 0 || value >= 1000000) return 0;
        *numbered = (int)value;
        return 2;
    }
    return 0;
}

static double layer_path_dist2(layer_point a, layer_point b) {
    double dx = (double)a.x - (double)b.x, dy = (double)a.y - (double)b.y;
    return dx * dx + dy * dy;
}

static double layer_path_seg_dist(layer_point a, layer_point b, layer_point p) {
    double dx = (double)b.x - (double)a.x, dy = (double)b.y - (double)a.y;
    double len2 = dx * dx + dy * dy;
    double px = (double)p.x - (double)a.x, py = (double)p.y - (double)a.y;
    if (len2 < 1e-18) return sqrt(px * px + py * py);
    double t = (px * dx + py * dy) / len2;
    if (t < 0) t = 0;
    else if (t > 1) t = 1;
    double ex = px - t * dx, ey = py - t * dy;
    return sqrt(ex * ex + ey * ey);
}

static void layer_path_dp_keep(const layer_point *pts, int from, int to, unsigned char *keep, int *stack, int stack_cap, double eps) {
    int top = 0;
    if (to - from < 2 || stack_cap < 2) return;
    stack[top++] = from;
    stack[top++] = to - 1;
    while (top >= 2) {
        int last = stack[--top], first = stack[--top];
        double best = -1;
        int at = first;
        for (int i = first + 1; i < last; i++) {
            double d = layer_path_seg_dist(pts[first], pts[last], pts[i]);
            if (d > best) { best = d; at = i; }
        }
        if (best > eps) {
            keep[at] = 1;
            if (top + 4 > stack_cap) continue;
            stack[top++] = first;
            stack[top++] = at;
            stack[top++] = at;
            stack[top++] = last;
        }
    }
}

static void layer_path_compact(layer_point *pts, int *count, int *starts, int paths, unsigned char *keep) {
    int write = 0;
    for (int p = 0; p < paths; p++) {
        int from = starts[p], to = (p + 1 < paths) ? starts[p + 1] : *count;
        int path_write = write;
        if (from < to) keep[from] = 1;
        if (to - from >= 2) keep[to - 1] = 1;
        for (int i = from; i < to; i++) {
            if (!keep[i]) continue;
            if (write != i) pts[write] = pts[i];
            write++;
        }
        starts[p] = path_write;
        if (write - path_write == 1) write = path_write; /* drop degenerate traces */
    }
    *count = write;
}

static void layer_path_simplify(layer_point *pts, int *count, int *starts, int paths, unsigned char *keep, int *stack, int stack_cap, double eps) {
    if (*count <= 2) return;
    memset(keep, 0, (size_t)*count);
    for (int p = 0; p < paths; p++) {
        int from = starts[p], to = (p + 1 < paths) ? starts[p + 1] : *count;
        if (from < to) keep[from] = 1;
        if (to - from >= 2) keep[to - 1] = 1;
        layer_path_dp_keep(pts, from, to, keep, stack, stack_cap, eps);
    }
    layer_path_compact(pts, count, starts, paths, keep);
}

typedef struct {
    double x, y, z, e;
    int abs_xy, abs_e, have_xy, have_e, have_z;
    int current, max_layer, total_hint, changes, seen_marker;
    int change_mode; /* a ;LAYER_CHANGE was seen: it alone numbers the layers */
    int in_path, truncated;
    double layer_z;
    int have_layer_z;
    int z_comment; /* layer_z came from the slicer's ;Z: comment; Z moves no longer override it */
    layer_point *pts;
    int *starts;
    unsigned char *keep;
    int *stack;
    int count, paths, cap, path_cap, stack_cap;
} layer_path_state;

static int layer_path_finite_xy(double x, double y) {
    return isfinite(x) && isfinite(y) && fabs(x) <= 10000 && fabs(y) <= 10000;
}

static int layer_path_close(layer_path_state *s) {
    s->in_path = 0;
    if (s->paths <= 0) return 0;
    int from = s->starts[s->paths - 1];
    if (s->count - from < 2) {
        s->count = from;
        s->paths--;
    }
    return 0;
}

static int layer_path_thin(layer_path_state *s) {
    int was_open = s->in_path;
    layer_point last = {0, 0};
    int have_last = 0;
    if (was_open && s->count > 0) { last = s->pts[s->count - 1]; have_last = 1; }
    layer_path_close(s);
    double eps = LAYER_PATH_DP_EPS;
    while (s->count > LAYER_PATH_POINTS_MAX && eps <= 8.0) {
        layer_path_simplify(s->pts, &s->count, s->starts, s->paths, s->keep, s->stack, s->stack_cap, eps);
        s->truncated = 1;
        eps *= 2;
    }
    if (s->count >= s->cap) {
        layer_path_simplify(s->pts, &s->count, s->starts, s->paths, s->keep, s->stack, s->stack_cap, 8.0);
        s->truncated = 1;
    }
    if (was_open && have_last && s->paths < s->path_cap && s->count + 1 < s->cap) {
        s->starts[s->paths++] = s->count;
        s->pts[s->count++] = last;
        s->in_path = 1;
    }
    return s->count + 2 < s->cap;
}

static int layer_path_push(layer_path_state *s, double x, double y, int start_path) {
    if (!layer_path_finite_xy(x, y)) return 0;
    if (start_path) {
        if (s->in_path) layer_path_close(s);
        if (s->paths >= s->path_cap) { s->truncated = 1; return 0; }
        if (s->count + 2 >= s->cap && !layer_path_thin(s)) { s->truncated = 1; return 0; }
        if (s->paths >= s->path_cap) { s->truncated = 1; return 0; }
        s->starts[s->paths++] = s->count;
        s->in_path = 1;
    }
    if (!s->in_path) return 0;
    layer_point p = {(float)x, (float)y};
    if (s->count > s->starts[s->paths - 1] &&
        layer_path_dist2(s->pts[s->count - 1], p) < LAYER_PATH_MIN_DIST * LAYER_PATH_MIN_DIST)
        return 0;
    if (s->count >= s->cap && !layer_path_thin(s)) { s->truncated = 1; return 0; }
    if (!s->in_path) {
        if (s->paths >= s->path_cap || s->count >= s->cap) { s->truncated = 1; return 0; }
        s->starts[s->paths++] = s->count;
        s->in_path = 1;
    }
    if (s->count >= s->cap) { s->truncated = 1; return 0; }
    s->pts[s->count++] = p;
    return 1;
}

static int layer_path_extrude_to(layer_path_state *s, double x, double y, int extruding) {
    if (!extruding) {
        layer_path_close(s);
        s->x = x;
        s->y = y;
        s->have_xy = 1;
        return 0;
    }
    if (!s->in_path) {
        if (s->have_xy) layer_path_push(s, s->x, s->y, 1);
        else layer_path_push(s, x, y, 1);
    }
    layer_path_push(s, x, y, 0);
    s->x = x;
    s->y = y;
    s->have_xy = 1;
    return 0;
}

static int layer_path_want(const layer_path_state *s, int target0) {
    return (!s->seen_marker && target0 == 0) || (s->seen_marker && s->current == target0);
}

static void layer_path_note_z(layer_path_state *s, int target0) {
    if (layer_path_want(s, target0) && s->have_z && !s->z_comment) {
        s->layer_z = s->z;
        s->have_layer_z = 1;
    }
}

static int layer_path_apply_marker(layer_path_state *s, int kind, int numbered, int target0) {
    int next;
    /* Files with ;LAYER_CHANGE (OrcaSlicer, PrusaSlicer, ElegooSlicer) are numbered by
     * that marker alone. ElegooSlicer also writes Cura-style ;LAYER:N comments, but
     * puts ;LAYER:0 into the start block and numbers the printed layers ;LAYER:1..N,
     * so trusting them shifts every layer by one and hides the last one. The first
     * ;LAYER_CHANGE therefore restarts the numbering at layer 0 and discards anything
     * collected under an earlier ;LAYER:N; later ;LAYER:N comments are ignored.
     * Files without ;LAYER_CHANGE (Cura) keep the 0-based ;LAYER:N numbering. */
    if (kind == 2 && s->change_mode) return 0;
    if (kind == 1 && !s->change_mode) {
        s->change_mode = 1;
        s->changes = 0;
        s->max_layer = -1;
        s->seen_marker = 0;
        s->have_layer_z = 0;
    }
    if (kind == 1) next = s->changes++;
    else next = numbered;
    if (next > s->max_layer) s->max_layer = next;
    if (!s->seen_marker) {
        layer_path_close(s);
        s->count = 0;
        s->paths = 0;
        s->in_path = 0;
        s->seen_marker = 1;
    } else if (s->current == target0 && next != target0) {
        layer_path_close(s);
    }
    s->current = next;
    s->z_comment = 0;
    if (kind == 2 && next + 1 > s->changes) s->changes = next + 1;
    layer_path_note_z(s, target0);
    return 0;
}

static void layer_path_arc(layer_path_state *s, double x1, double y1, double i, double j,
                           int have_ij, double r, int have_r, int clockwise, int extruding, int target0) {
    double x0 = s->x, y0 = s->y, cx, cy, radius;
    if (!s->have_xy) {
        if (layer_path_want(s, target0)) layer_path_extrude_to(s, x1, y1, extruding);
        else { s->x = x1; s->y = y1; s->have_xy = 1; }
        return;
    }
    if (have_ij) {
        cx = x0 + i;
        cy = y0 + j;
    } else if (have_r) {
        double dx = x1 - x0, dy = y1 - y0, d = hypot(dx, dy);
        if (d < 1e-9) {
            if (layer_path_want(s, target0)) layer_path_extrude_to(s, x1, y1, extruding);
            else { s->x = x1; s->y = y1; }
            return;
        }
        double rr = fabs(r);
        double h2 = rr * rr - (d * 0.5) * (d * 0.5);
        if (h2 < 0) h2 = 0;
        double h = sqrt(h2), mx = (x0 + x1) * 0.5, my = (y0 + y1) * 0.5;
        double nx = -dy / d, ny = dx / d;
        int left = clockwise ? (r < 0) : (r >= 0);
        cx = mx + (left ? h : -h) * nx;
        cy = my + (left ? h : -h) * ny;
    } else {
        if (layer_path_want(s, target0)) layer_path_extrude_to(s, x1, y1, extruding);
        else { s->x = x1; s->y = y1; }
        return;
    }
    radius = hypot(x0 - cx, y0 - cy);
    if (radius < 1e-9) {
        if (layer_path_want(s, target0)) layer_path_extrude_to(s, x1, y1, extruding);
        else { s->x = x1; s->y = y1; }
        return;
    }
    double a0 = atan2(y0 - cy, x0 - cx), a1 = atan2(y1 - cy, x1 - cx), sweep = a1 - a0;
    if (hypot(x1 - x0, y1 - y0) < 1e-9) sweep = clockwise ? -2.0 * 3.14159265358979323846 : 2.0 * 3.14159265358979323846;
    else if (clockwise) { while (sweep > 0) sweep -= 2.0 * 3.14159265358979323846; if (sweep > -1e-12) sweep = -2.0 * 3.14159265358979323846; }
    else { while (sweep < 0) sweep += 2.0 * 3.14159265358979323846; if (sweep < 1e-12) sweep = 2.0 * 3.14159265358979323846; }
    int n = (int)(fabs(sweep) / (15.0 * 3.14159265358979323846 / 180.0) + 0.5);
    if (n < 2) n = 2;
    if (n > 16) n = 16;
    for (int k = 1; k <= n; k++) {
        double a = a0 + sweep * (double)k / (double)n;
        double x = cx + radius * cos(a), y = cy + radius * sin(a);
        if (k == n) { x = x1; y = y1; }
        if (layer_path_want(s, target0)) layer_path_extrude_to(s, x, y, extruding);
        else { s->x = x; s->y = y; s->have_xy = 1; }
    }
}

static int layer_path_word_line(layer_path_state *s, const char *line, int target0) {
    const char *p = line;
    while (isspace((unsigned char)*p)) p++;
    if (!*p || *p == ';' || *p == '\0') return 0;
    if (*p == 'N' || *p == 'n') {
        p++;
        while (isdigit((unsigned char)*p)) p++;
        while (isspace((unsigned char)*p)) p++;
    }
    if (*p != 'G' && *p != 'g' && *p != 'M' && *p != 'm') return 0;
    int is_g = (*p == 'G' || *p == 'g');
    char *end = NULL;
    long code = strtol(p + 1, &end, 10);
    if (end == p + 1) return 0;
    p = end;
    if (!is_g) {
        if (code == 82) s->abs_e = 1;
        else if (code == 83) s->abs_e = 0;
        return 0;
    }
    if (code == 90) { s->abs_xy = 1; return 0; }
    if (code == 91) { s->abs_xy = 0; return 0; }
    double x = s->x, y = s->y, z = s->z, e = s->e, i = 0, j = 0, r = 0;
    int hx = 0, hy = 0, hz = 0, he = 0, hi = 0, hj = 0, hr = 0;
    while (*p && *p != ';') {
        unsigned char w = (unsigned char)toupper((unsigned char)*p);
        if (w < 'A' || w > 'Z') { p++; continue; }
        p++;
        double v = strtod(p, &end);
        if (end == p) break;
        p = end;
        if (w == 'X') { x = s->abs_xy ? v : s->x + v; hx = 1; }
        else if (w == 'Y') { y = s->abs_xy ? v : s->y + v; hy = 1; }
        else if (w == 'Z') { z = s->abs_xy ? v : s->z + v; hz = 1; }
        else if (w == 'E') { e = s->abs_e ? v : s->e + v; he = 1; }
        else if (w == 'I') { i = v; hi = 1; }
        else if (w == 'J') { j = v; hj = 1; }
        else if (w == 'R') { r = v; hr = 1; }
    }
    if (code == 92) {
        if (hx) { s->x = x; s->have_xy = 1; }
        if (hy) { s->y = y; s->have_xy = 1; }
        if (hz) { s->z = z; s->have_z = 1; }
        if (he) { s->e = e; s->have_e = 1; }
        return 0;
    }
    if (code == 28) {
        if (!hx && !hy && !hz) { s->x = s->y = 0; s->have_xy = 1; }
        else {
            if (hx) s->x = 0;
            if (hy) s->y = 0;
            if (hx || hy) s->have_xy = 1;
            if (hz) { s->z = 0; s->have_z = 1; }
        }
        layer_path_close(s);
        return 0;
    }
    if (code != 0 && code != 1 && code != 2 && code != 3) return 0;
    if (hz) { s->z = z; s->have_z = 1; layer_path_note_z(s, target0); }
    int travel = (code == 0);
    int extruding = 0;
    if (!travel && he) {
        double delta = e - s->e;
        extruding = isfinite(delta) && delta > 1e-8;
        s->e = e;
        s->have_e = 1;
    }
    if (!hx && !hy) return 0;
    if (!hx) x = s->x;
    if (!hy) y = s->y;
    if ((code == 2 || code == 3) && (hi || hj || hr)) {
        layer_path_arc(s, x, y, i, j, hi || hj, r, hr, code == 2, extruding, target0);
        return 0;
    }
    if (layer_path_want(s, target0)) layer_path_extrude_to(s, x, y, extruding);
    else {
        if (s->in_path) layer_path_close(s);
        s->x = x;
        s->y = y;
        s->have_xy = 1;
    }
    return 0;
}

static int gcode_layer_path_parse(FILE *file, int layer_1based,
                                  layer_point *points, int points_cap,
                                  int *path_start, int paths_cap,
                                  layer_path_info *info,
                                  const struct timespec *start, int time_seconds, size_t bytes_max) {
    memset(info, 0, sizeof(*info));
    info->layer = layer_1based;
    info->z = NAN;
    if (!file || !points || !path_start || !info) return LAYER_PATH_IO;
    if (layer_1based < 1 || layer_1based > 1000000) return LAYER_PATH_OUTSIDE;
    if (points_cap < 2 || paths_cap < 1) return LAYER_PATH_IO;
    unsigned char *keep = calloc((size_t)points_cap, 1);
    int *stack = malloc(sizeof(int) * (size_t)points_cap);
    if (!keep || !stack) { free(keep); free(stack); return LAYER_PATH_IO; }
    layer_path_state s;
    memset(&s, 0, sizeof(s));
    s.abs_xy = 1;
    s.abs_e = 1;
    s.current = -1;
    s.max_layer = -1;
    s.pts = points;
    s.starts = path_start;
    s.keep = keep;
    s.stack = stack;
    s.stack_cap = points_cap;
    s.cap = points_cap < LAYER_PATH_COLLECT_MAX ? points_cap : LAYER_PATH_COLLECT_MAX;
    s.path_cap = paths_cap < LAYER_PATH_PATHS_MAX ? paths_cap : LAYER_PATH_PATHS_MAX;
    int target0 = layer_1based - 1;
    char line[LAYER_PATH_LINE_MAX];
    size_t bytes = 0;
    while (fgets(line, sizeof(line), file)) {
        size_t n = strlen(line);
        bytes += n;
        if (bytes_max && bytes > bytes_max) {
            if (s.seen_marker && s.current > target0) break;
            if (!s.seen_marker && s.count > 0 && target0 == 0) break;
            free(keep); free(stack); return LAYER_PATH_LIMIT;
        }
        if (layer_path_deadline(start, time_seconds)) {
            if ((s.seen_marker && s.max_layer >= target0) || (!s.seen_marker && s.count > 0 && target0 == 0)) break;
            free(keep); free(stack); return LAYER_PATH_LIMIT;
        }
        int numbered = 0, kind = layer_path_marker(line, &numbered);
        if (kind) {
            layer_path_apply_marker(&s, kind, numbered, target0);
            continue;
        }
        const char *comment = line;
        while (isspace((unsigned char)*comment)) comment++;
        if (*comment == ';') {
            /* ;Z:<height> after ;LAYER_CHANGE (Orca/Prusa/ElegooSlicer) is the layer's
             * print height. Prefer it over the last Z move, which can be a z-hop or the
             * end G-code lifting the head after the last layer. */
            const char *zc = comment + 1;
            while (*zc == ' ' || *zc == '\t') zc++;
            if (zc[0] == 'Z' && zc[1] == ':') {
                if (s.seen_marker && layer_path_want(&s, target0)) {
                    char *zend = NULL;
                    double zv = strtod(zc + 2, &zend);
                    if (zend != zc + 2 && isfinite(zv) && zv >= 0 && zv < 10000) {
                        s.layer_z = zv;
                        s.have_layer_z = 1;
                        s.z_comment = 1;
                    }
                }
                continue;
            }
            static const char *markers[] = {
                "total layer number", "total_layer_count", "total layers count",
                "total layers", "layer_count:"
            };
            for (size_t i = 0; i < sizeof(markers) / sizeof(markers[0]); i++) {
                int value = layer_path_positive_after(line, markers[i]);
                if (value > s.total_hint) s.total_hint = value;
            }
            continue;
        }
        if (s.seen_marker && s.current > target0 && s.total_hint) {
            /* ;LAYER_CHANGE numbering only grows and the header gave the layer count, so
             * nothing after the requested layer can change the answer: stop reading. A
             * large file's early layers then cost a fraction of a full scan. Files numbered
             * by ;LAYER:N alone (Cura) can repeat a number, so they are still read to the end. */
            if (s.change_mode) break;
            continue;
        }
        layer_path_word_line(&s, line, target0);
    }
    if (ferror(file)) { free(keep); free(stack); return LAYER_PATH_IO; }
    layer_path_close(&s);
    int total = s.total_hint;
    if (!total && s.max_layer >= 0) total = s.max_layer + 1;
    if (!s.seen_marker) total = 1;
    info->total_layers = total;
    if (target0 >= total || (s.seen_marker && s.max_layer < target0 && s.current != target0 && s.count == 0)) {
        free(keep); free(stack); return LAYER_PATH_OUTSIDE;
    }
    if (!s.seen_marker && target0 != 0) { free(keep); free(stack); return LAYER_PATH_OUTSIDE; }
    double eps = LAYER_PATH_DP_EPS;
    while (s.count > LAYER_PATH_POINTS_MAX && eps <= 8.0) {
        layer_path_simplify(s.pts, &s.count, s.starts, s.paths, s.keep, s.stack, s.stack_cap, eps);
        s.truncated = 1;
        eps *= 2;
    }
    if (s.count > LAYER_PATH_POINTS_MAX) {
        layer_path_simplify(s.pts, &s.count, s.starts, s.paths, s.keep, s.stack, s.stack_cap, 8.0);
        s.truncated = 1;
        if (s.count > LAYER_PATH_POINTS_MAX) {
            s.count = LAYER_PATH_POINTS_MAX;
            while (s.paths > 0 && s.starts[s.paths - 1] >= s.count) s.paths--;
        }
    }
    info->point_count = s.count;
    info->path_count = s.paths;
    info->truncated = s.truncated;
    if (s.have_layer_z && isfinite(s.layer_z)) info->z = s.layer_z;
    else if (s.have_z && isfinite(s.z) && layer_path_want(&s, target0)) info->z = s.z;
    free(keep); free(stack);
    return LAYER_PATH_OK;
}

static void layer_path_json_string(char *out, size_t *used, size_t cap, int *failed, const char *value) {
    if (*failed || *used + 2 >= cap) { *failed = 1; return; }
    out[(*used)++] = '"';
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (*used + 6 >= cap) { *failed = 1; return; }
        if (*p == '"' || *p == '\\') {
            out[(*used)++] = '\\';
            out[(*used)++] = (char)*p;
        } else if (*p < 32) {
            int n = snprintf(out + *used, cap - *used, "\\u%04x", (unsigned)*p);
            if (n < 0 || (size_t)n >= cap - *used) { *failed = 1; return; }
            *used += (size_t)n;
        } else out[(*used)++] = (char)*p;
    }
    if (*used + 1 >= cap) { *failed = 1; return; }
    out[(*used)++] = '"';
}

static int gcode_layer_path_json(char *out, size_t cap, const char *storage, const char *file,
                                 const layer_path_info *info, const layer_point *points, const int *path_start) {
    if (!out || cap < 8 || !info || !points || !path_start) return -1;
    size_t used = 0;
    int failed = 0;
    int n = snprintf(out, cap, "{\"storage\":");
    if (n < 0 || (size_t)n >= cap) return -1;
    used = (size_t)n;
    layer_path_json_string(out, &used, cap, &failed, storage ? storage : "");
    n = snprintf(out + used, cap - used, ",\"file\":");
    if (failed || n < 0 || (size_t)n >= cap - used) return -1;
    used += (size_t)n;
    layer_path_json_string(out, &used, cap, &failed, file ? file : "");
    char zbuf[32];
    if (isfinite(info->z)) snprintf(zbuf, sizeof(zbuf), "%.3f", info->z);
    else snprintf(zbuf, sizeof(zbuf), "null");
    n = snprintf(out + used, cap - used,
                 ",\"layer\":%d,\"total_layers\":%d,\"z\":%s,\"truncated\":%s,\"point_count\":%d,\"paths\":[",
                 info->layer, info->total_layers, zbuf, info->truncated ? "true" : "false", info->point_count);
    if (failed || n < 0 || (size_t)n >= cap - used) return -1;
    used += (size_t)n;
    for (int p = 0; p < info->path_count; p++) {
        int from = path_start[p], to = (p + 1 < info->path_count) ? path_start[p + 1] : info->point_count;
        if (to - from < 2) continue;
        n = snprintf(out + used, cap - used, "%s[", used && out[used - 1] != '[' ? "," : "");
        if (n < 0 || (size_t)n >= cap - used) return -1;
        used += (size_t)n;
        int first = 1;
        for (int i = from; i < to; i++) {
            n = snprintf(out + used, cap - used, "%s[%.2f,%.2f]", first ? "" : ",", points[i].x, points[i].y);
            if (n < 0 || (size_t)n >= cap - used) return -1;
            used += (size_t)n;
            first = 0;
        }
        if (used + 1 >= cap) return -1;
        out[used++] = ']';
    }
    n = snprintf(out + used, cap - used, "]}\n");
    if (n < 0 || (size_t)n >= cap - used) return -1;
    used += (size_t)n;
    return (int)used;
}

#endif
