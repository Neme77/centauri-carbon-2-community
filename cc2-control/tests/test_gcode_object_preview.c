/* The object-preview parser: objects from EXCLUDE_OBJECT_*, only the outer wall, the drawn step
 * max(3, ceil(n / 24)) on at most 24 rings, at most 128 points per ring, relative and absolute extrusion,
 * the time and size limits, out of memory, and that every byte it took is given back. */
#include "../src/gcode_object_preview.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char dir[] = "/tmp/cc2-object-preview-XXXXXX";
static char text[16 * 1024 * 1024];
static size_t used;

static void put(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void put(const char *format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsnprintf(text + used, sizeof(text) - used, format, args);
    va_end(args);
    assert(n > 0 && (size_t)n < sizeof(text) - used);
    used += (size_t)n;
}

static int parse_with(object_preview_result *r, long time_ms, size_t bytes_max, const struct timespec *start) {
    char path[512];
    snprintf(path, sizeof(path), "%s/t.gcode", dir);
    FILE *f = fopen(path, "w+");
    assert(f && fwrite(text, 1, used, f) == used && !fflush(f));
    rewind(f);
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    size_t bytes = 0;
    int rc = gcode_object_preview_parse(f, r, start ? start : &now, time_ms, bytes_max, &bytes);
    if (rc == OBJECT_PREVIEW_OK) assert(bytes == used);
    fclose(f);
    unlink(path);
    return rc;
}

static int parse(object_preview_result *r) { return parse_with(r, LAYER_PATH_TIME_MS, OBJECT_PREVIEW_BYTES_MAX, NULL); }

static void done(object_preview_result *r) {
    object_preview_free(r);
    assert(r->bytes == 0); /* everything taken was given back */
}

static const object_preview_object *object(const object_preview_result *r, const char *name) {
    int i = object_preview_find(r, name);
    assert(i >= 0);
    return &r->objects[i];
}

static int ring_points(const object_preview_object *o) {
    int n = 0;
    for (int i = 0; i < o->ring_count; i++) n += o->rings[i].count;
    return n;
}

/* Every stored point of the object lies within [x0,x1] x [y0,y1]. */
static void inside(const object_preview_object *o, double x0, double y0, double x1, double y1) {
    for (int i = 0; i < o->ring_count; i++)
        for (int k = 0; k < o->rings[i].count; k++) {
            double x = o->rings[i].xy[2 * k] * OBJECT_PREVIEW_UNIT, y = o->rings[i].xy[2 * k + 1] * OBJECT_PREVIEW_UNIT;
            if (x < x0 - 0.011 || x > x1 + 0.011 || y < y0 - 0.011 || y > y1 + 0.011) {
                fprintf(stderr, "%s ring %d point %.2f,%.2f outside %.2f,%.2f..%.2f,%.2f\n", o->name, i, x, y, x0, y0, x1, y1);
                abort();
            }
        }
}

/* A square outline of side `side` at (x, y), relative extrusion. */
static void square(double x, double y, double side) {
    put("G1 X%.3f Y%.3f F9000\n", x, y);
    put("G1 X%.3f Y%.3f E0.5\nG1 X%.3f Y%.3f E0.5\nG1 X%.3f Y%.3f E0.5\nG1 X%.3f Y%.3f E0.5\n",
        x + side, y, x + side, y + side, x, y + side, x, y);
}

/* One object of `layers` layers (Orca style), only outer wall squares. */
static void tower(int layers) {
    used = 0;
    put("; generated\nM83\nEXCLUDE_OBJECT_DEFINE NAME=T CENTER=60,60\n");
    for (int l = 0; l < layers; l++) {
        put(";LAYER_CHANGE\n;Z:%.2f\nG1 Z%.2f\nEXCLUDE_OBJECT_START NAME=T\n;TYPE:Outer wall\n", 0.2 * (l + 1), 0.2 * (l + 1));
        square(50, 50, 20);
        put("EXCLUDE_OBJECT_END NAME=T\n");
    }
}

static void check_steps(int layers, int step, int rings) {
    object_preview_result r;
    tower(layers);
    assert(parse(&r) == OBJECT_PREVIEW_OK);
    const object_preview_object *o = object(&r, "T");
    if (o->layers != layers || o->step != step || o->ring_count != rings) {
        fprintf(stderr, "n=%d: layers %d step %d rings %d, expected step %d rings %d\n", layers, o->layers, o->step,
                o->ring_count, step, rings);
        abort();
    }
    /* The drawn layers are the first one and every step-th after it, at their own height. */
    for (int i = 0; i < o->ring_count; i++) {
        assert(o->rings[i].ordinal == i * step);
        assert(fabs(o->rings[i].z - 0.2 * (i * step + 1)) < 1e-4);
        assert(o->rings[i].paths == 1 && o->rings[i].count == 5);
    }
    assert(fabs(o->z_max - 0.2 * layers) < 1e-4);
    assert(o->ring_count <= 24 && o->step >= 3);
    done(&r);
}

/* A circle of `segments` straight moves, or of `arcs` G3 arcs when arcs > 0. */
static void circle(int segments, int arcs) {
    used = 0;
    put("M83\nEXCLUDE_OBJECT_DEFINE NAME=C\n;LAYER_CHANGE\nG1 Z0.2\nEXCLUDE_OBJECT_START NAME=C\n;TYPE:Outer wall\n");
    put("G1 X140 Y100 F9000\n");
    if (arcs) {
        for (int k = 1; k <= arcs; k++) {
            double a0 = 2 * 3.14159265358979323846 * (k - 1) / arcs, a1 = 2 * 3.14159265358979323846 * k / arcs;
            put("G3 X%.4f Y%.4f I%.4f J%.4f E0.05\n", 100 + 40 * cos(a1), 100 + 40 * sin(a1), -40 * cos(a0), -40 * sin(a0));
        }
    } else {
        for (int k = 1; k <= segments; k++) {
            double a = 2 * 3.14159265358979323846 * k / segments;
            put("G1 X%.4f Y%.4f E0.01\n", 100 + 40 * cos(a), 100 + 40 * sin(a));
        }
    }
    put("G1 X141 Y100 E0.01\nEXCLUDE_OBJECT_END NAME=C\n");
}

static void check_circle(int segments, int arcs, int minimum) {
    object_preview_result r;
    circle(segments, arcs);
    assert(parse(&r) == OBJECT_PREVIEW_OK);
    const object_preview_object *o = object(&r, "C");
    assert(o->ring_count == 1);
    const object_preview_ring *g = &o->rings[0];
    if (g->count > 128 || g->count < minimum) {
        fprintf(stderr, "circle %d/%d: %d points\n", segments, arcs, g->count);
        abort();
    }
    /* The distance starts at 0.5 mm and only doubles. */
    double d = g->min_dist / 0.5;
    assert(d >= 1 && fabs(d - pow(2, floor(log2(d) + 0.5))) < 1e-6);
    /* The start and the end of the path stay exactly. */
    assert(g->paths == 1 && g->xy[0] == 7000 && g->xy[1] == 5000);
    assert(g->xy[2 * (g->count - 1)] == 7050 && g->xy[2 * (g->count - 1) + 1] == 5000);
    /* Thinned evenly: no two neighbours further apart than a few times the final distance. */
    for (int k = 1; k < g->count && segments; k++) {
        double d = sqrt((double)object_preview_d2(&g->xy[2 * k], &g->xy[2 * (k - 1)])) * OBJECT_PREVIEW_UNIT;
        assert(d <= 2 * g->min_dist + 1.0);
    }
    done(&r);
}

int main(void) {
    assert(mkdtemp(dir));
    object_preview_result r;
    (void)gcode_layer_path_query; (void)layer_path_mark_pick; (void)gcode_layer_path_parse_at; (void)gcode_layer_path_json;

    /* ---- Query: storage, file and object; unknown keys and bad paths fail closed. ---- */
    char storage[16], file[LAYER_PATH_NAME_MAX], name[OBJECT_PREVIEW_NAME_MAX];
    assert(gcode_object_preview_query("file=a.gcode&object=Box_id_0", storage, file, name) == OBJECT_PREVIEW_OK);
    assert(!strcmp(storage, "internal") && !strcmp(file, "a.gcode") && !strcmp(name, "Box_id_0"));
    assert(gcode_object_preview_query("storage=usb&file=d%2Fa.gcode&object=A%20B", storage, file, name) == OBJECT_PREVIEW_OK);
    assert(!strcmp(storage, "usb") && !strcmp(file, "d/a.gcode") && !strcmp(name, "A B"));
    assert(gcode_object_preview_query("file=a.gcode", storage, file, name) == OBJECT_PREVIEW_BAD_OBJECT);
    assert(gcode_object_preview_query("file=a.gcode&object=", storage, file, name) == OBJECT_PREVIEW_BAD_OBJECT);
    assert(gcode_object_preview_query("file=../a.gcode&object=A", storage, file, name) == OBJECT_PREVIEW_BAD_QUERY);
    assert(gcode_object_preview_query("file=a.gcode&object=A&layer=1", storage, file, name) == OBJECT_PREVIEW_BAD_QUERY);
    assert(gcode_object_preview_query("storage=sd&file=a.gcode&object=A", storage, file, name) == OBJECT_PREVIEW_BAD_QUERY);

    /* ---- Objects: each move goes to the object between its START and END, nothing outside. ---- */
    used = 0;
    put("M83\nEXCLUDE_OBJECT_DEFINE NAME=A CENTER=10,10 POLYGON=[[0,0],[20,0]]\nEXCLUDE_OBJECT_DEFINE NAME='B b'\n");
    put(";TYPE:Custom\nG1 X0 Y0\nG1 X200 Y0 E5\n"); /* the purge line belongs to no object */
    for (int l = 0; l < 4; l++) {
        put(";LAYER_CHANGE\nG1 Z%.1f\n", 0.2 * (l + 1));
        put("EXCLUDE_OBJECT_START NAME=A\n;TYPE:Outer wall\n"); square(10, 10, 10); put("EXCLUDE_OBJECT_END NAME=A\n");
        put("EXCLUDE_OBJECT_START NAME='B b'\n;TYPE:Outer wall\n"); square(100, 100, 30); put("EXCLUDE_OBJECT_END NAME='B b'\n");
        put(";TYPE:Outer wall\n"); square(200, 200, 5); /* outer wall, but outside any object */
        put("EXCLUDE_OBJECT_START NAME=C\n;TYPE:Outer wall\n"); square(150, 20, 8); put("EXCLUDE_OBJECT_END NAME=C\n");
    }
    assert(parse(&r) == OBJECT_PREVIEW_OK);
    assert(r.count == 3 && !r.truncated);
    assert(!strcmp(r.objects[0].name, "A") && !strcmp(r.objects[1].name, "B b") && !strcmp(r.objects[2].name, "C"));
    inside(object(&r, "A"), 10, 10, 20, 20);
    inside(object(&r, "B b"), 100, 100, 130, 130);
    inside(object(&r, "C"), 150, 20, 158, 28); /* a START without DEFINE adds the object */
    for (int i = 0; i < 3; i++) assert(r.objects[i].layers == 4 && r.objects[i].ring_count == 2);
    const object_preview_object *b = object(&r, "B b");
    assert(b->have_bounds && b->bounds[0] == 100 && b->bounds[1] == 100 && b->bounds[2] == 130 && b->bounds[3] == 130);
    assert(fabs(b->z_max - 0.8) < 1e-5);
    assert(object_preview_find(&r, "D") < 0);
    /* Klipper reports the names in upper case: the same object is found, the exact name first. */
    assert(object_preview_find(&r, "a") == 0 && object_preview_find(&r, "b B") == 1 && object_preview_find(&r, "c") == 2);
    assert(object_preview_find(&r, "A ") < 0 && object_preview_find(&r, "B") < 0 && object_preview_find(&r, "") < 0);
    done(&r);

    /* A START switches to its object also without an END before it. */
    used = 0;
    put("M83\n;LAYER_CHANGE\nG1 Z0.2\n;TYPE:Outer wall\nEXCLUDE_OBJECT_START NAME=A\n"); square(10, 10, 10);
    put("EXCLUDE_OBJECT_START NAME=B\n"); square(100, 100, 10); put("EXCLUDE_OBJECT_END NAME=B\n");
    assert(parse(&r) == OBJECT_PREVIEW_OK && r.count == 2);
    inside(object(&r, "A"), 10, 10, 20, 20);
    inside(object(&r, "B"), 100, 100, 110, 110);
    done(&r);

    /* ---- Only the outer wall, in every known spelling; other types are left out. ---- */
    const char *outer[] = {"Outer wall", "Overhang wall", "External perimeter", "Overhang perimeter", "WALL-OUTER", "outer wall \r"};
    const char *other[] = {"Inner wall", "Perimeter", "WALL-INNER", "Sparse infill", "Outer wall extra", "SKIN"};
    for (size_t t = 0; t < sizeof(outer) / sizeof(outer[0]); t++) {
        used = 0;
        put("M83\nEXCLUDE_OBJECT_DEFINE NAME=W\n;LAYER_CHANGE\nG1 Z0.2\nEXCLUDE_OBJECT_START NAME=W\n");
        put(";TYPE:%s\n", other[t]); square(40, 40, 30);
        put(";TYPE:%s\n", outer[t]); square(60, 60, 5);
        put(";TYPE:%s\n", other[t]); square(45, 45, 20);
        put("EXCLUDE_OBJECT_END NAME=W\n");
        assert(parse(&r) == OBJECT_PREVIEW_OK);
        const object_preview_object *w = object(&r, "W");
        assert(w->ring_count == 1 && w->rings[0].count == 5);
        inside(w, 60, 60, 65, 65);
        assert(w->bounds[0] == 60 && w->bounds[2] == 65);
        done(&r);
    }
    /* Without any ;TYPE: comment every extrusion inside an object counts. */
    used = 0;
    put("M83\nEXCLUDE_OBJECT_DEFINE NAME=W\n;LAYER_CHANGE\nG1 Z0.2\nEXCLUDE_OBJECT_START NAME=W\n");
    square(40, 40, 30); square(60, 60, 5);
    put("EXCLUDE_OBJECT_END NAME=W\n");
    assert(parse(&r) == OBJECT_PREVIEW_OK);
    assert(object(&r, "W")->rings[0].paths == 2 && object(&r, "W")->bounds[0] == 40);
    done(&r);
    /* With type comments, what came before the first one is no outer wall. */
    used = 0;
    put("M83\nEXCLUDE_OBJECT_DEFINE NAME=W\n;LAYER_CHANGE\nG1 Z0.2\nEXCLUDE_OBJECT_START NAME=W\n");
    square(40, 40, 30);
    put(";TYPE:Outer wall\n"); square(60, 60, 5);
    put("EXCLUDE_OBJECT_END NAME=W\n");
    assert(parse(&r) == OBJECT_PREVIEW_OK);
    assert(object(&r, "W")->rings[0].paths == 1 && object(&r, "W")->layers == 1);
    inside(object(&r, "W"), 60, 60, 65, 65);
    done(&r);

    /* ---- Drawn step and ring count: n -> step -> rings. ---- */
    static const int cases[][3] = {
        {1, 3, 1}, {2, 3, 1}, {3, 3, 1}, {21, 3, 7}, {65, 3, 22}, {89, 4, 23}, {97, 6, 17},
        {128, 6, 22}, {193, 12, 17}, {523, 24, 22}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) check_steps(cases[i][0], cases[i][1], cases[i][2]);

    /* ---- At most 128 points per ring: many short moves, and G3 arcs; the path ends stay. ---- */
    check_circle(3600, 0, 64);
    check_circle(20000, 0, 64);
    check_circle(0, 400, 64);
    check_circle(0, 4, 26); /* 4 quarter arcs, 6 pieces each as in the layer path, fit uncut */
    /* Thinning the whole ring again keeps the start and the end of the earlier paths too. */
    used = 0;
    put("M83\nEXCLUDE_OBJECT_DEFINE NAME=R\n;LAYER_CHANGE\nG1 Z0.2\nEXCLUDE_OBJECT_START NAME=R\n;TYPE:Outer wall\n");
    for (int c = 0; c < 2; c++) {
        double radius = c ? 30 : 40;
        put("G0 X%.4f Y100\n", 100 + radius);
        for (int k = 1; k <= 2000; k++)
            put("G1 X%.4f Y%.4f E0.01\n", 100 + radius * cos(k * 0.00314159265), 100 + radius * sin(k * 0.00314159265));
        put("G1 X%.4f Y%.4f E0.01\n", 100 + radius * cos(6.3), 100 + radius * sin(6.3) + 0.3);
    }
    put("EXCLUDE_OBJECT_END NAME=R\n");
    assert(parse(&r) == OBJECT_PREVIEW_OK);
    {
        const object_preview_ring *g = &object(&r, "R")->rings[0];
        assert(g->paths == 2 && g->count <= 128 && g->min_dist > 0.5f);
        int16_t ex, ey; assert(object_preview_units(100 + 40 * cos(6.3), &ex) && object_preview_units(100 + 40 * sin(6.3) + 0.3, &ey));
        assert(g->xy[0] == 7000 && g->xy[1] == 5000);                                 /* first path's start */
        assert(g->xy[2 * (g->starts[1] - 1)] == ex && g->xy[2 * (g->starts[1] - 1) + 1] == ey); /* and its end */
    }
    done(&r);
    /* A ring of many separate paths keeps at most 64 of them and says it was cut. */
    used = 0;
    put("M83\nEXCLUDE_OBJECT_DEFINE NAME=P\n;LAYER_CHANGE\nG1 Z0.2\nEXCLUDE_OBJECT_START NAME=P\n;TYPE:Outer wall\n");
    for (int k = 0; k < 100; k++) put("G0 X%d Y10\nG1 X%d Y12 E0.1\n", k * 2, k * 2);
    put("EXCLUDE_OBJECT_END NAME=P\n");
    assert(parse(&r) == OBJECT_PREVIEW_OK);
    assert(object(&r, "P")->rings[0].paths == 64 && object(&r, "P")->rings[0].count == 128);
    assert(object(&r, "P")->truncated);
    done(&r);
    /* Points closer than 0.5 mm are left out: a 10 mm line of 0.1 mm steps keeps every fifth point and its end. */
    used = 0;
    put("M83\nEXCLUDE_OBJECT_DEFINE NAME=L\n;LAYER_CHANGE\nG1 Z0.2\nEXCLUDE_OBJECT_START NAME=L\n;TYPE:Outer wall\nG1 X10 Y10\n");
    for (int k = 1; k <= 100; k++) put("G1 X%.1f Y10 E0.01\n", 10 + 0.1 * k);
    put("EXCLUDE_OBJECT_END NAME=L\n");
    assert(parse(&r) == OBJECT_PREVIEW_OK);
    {
        const object_preview_ring *g = &object(&r, "L")->rings[0];
        assert(g->count == 21 && g->min_dist == 0.5f);
        for (int k = 0; k < 21; k++) assert(g->xy[2 * k] == 500 + 25 * k && g->xy[2 * k + 1] == 500);
    }
    done(&r);
    /* Per object at most 24 x 128 points: 300 layers of a fine circle. */
    used = 0;
    put("M83\nEXCLUDE_OBJECT_DEFINE NAME=C\n");
    for (int l = 0; l < 300; l++) {
        put(";LAYER_CHANGE\nG1 Z%.2f\nEXCLUDE_OBJECT_START NAME=C\n;TYPE:Outer wall\nG1 X140 Y100\n", 0.2 * (l + 1));
        for (int k = 1; k <= 720; k++) put("G1 X%.3f Y%.3f E0.01\n", 100 + 40 * cos(k * 3.14159265358979323846 / 360), 100 + 40 * sin(k * 3.14159265358979323846 / 360));
        put("EXCLUDE_OBJECT_END NAME=C\n");
    }
    assert(parse(&r) == OBJECT_PREVIEW_OK);
    assert(object(&r, "C")->ring_count <= 24 && ring_points(object(&r, "C")) <= 24 * 128 && object(&r, "C")->layers == 300);
    /* Kept memory: the rings shrunk to their points. */
    assert(r.bytes <= sizeof(object_preview_object) + 24 * (sizeof(object_preview_ring) + 128 * 4));
    done(&r);

    /* ---- Relative and absolute extrusion give the same shape; retractions and travel draw nothing. ---- */
    char relative_json[8192], absolute_json[8192];
    for (int mode = 0; mode < 2; mode++) {
        used = 0;
        put("%s\nEXCLUDE_OBJECT_DEFINE NAME=E\n;LAYER_CHANGE\nG1 Z0.2\nG92 E0\nEXCLUDE_OBJECT_START NAME=E\n;TYPE:Outer wall\n",
            mode ? "M82" : "M83");
        double e = 0;
        put("G1 X10 Y10\n");
        double xs[] = {30, 30, 10, 10}, ys[] = {10, 30, 30, 10};
        for (int k = 0; k < 4; k++) { e += 0.7; put("G1 X%.0f Y%.0f E%.2f\n", xs[k], ys[k], mode ? e : 0.7); }
        e -= 0.8; put("G1 X60 Y60 E%.2f\n", mode ? e : -0.8);           /* a retracting move */
        put("G0 X80 Y80\n");                                                 /* travel */
        put("G1 X90 Y90 E%.2f\n", mode ? e : 0.0);                        /* no extrusion */
        e += 0.8; put("G1 E%.2f\n", mode ? e : 0.8);                       /* unretract, no move */
        e += 0.3; put("G1 X90 Y95 E%.2f\n", mode ? e : 0.3);
        put("EXCLUDE_OBJECT_END NAME=E\n");
        assert(parse(&r) == OBJECT_PREVIEW_OK);
        size_t length = 0;
        char *json = gcode_object_preview_json("internal", "e.gcode", &r, 0, &length);
        assert(json && length < sizeof(relative_json));
        memcpy(mode ? absolute_json : relative_json, json, length + 1);
        free(json);
        assert(object(&r, "E")->rings[0].paths == 2);
        done(&r);
    }
    assert(!strcmp(relative_json, absolute_json));
    assert(strstr(relative_json, "\"paths\":[[10,10,30,10,30,30,10,30,10,10],[90,90,90,95]]"));

    /* ---- The answer: compact, at most two decimals, the object list. ---- */
    used = 0;
    put("M83\nEXCLUDE_OBJECT_DEFINE NAME=A\nEXCLUDE_OBJECT_DEFINE NAME=Q\"x\n;LAYER_CHANGE\nG1 Z0.25\n");
    put("EXCLUDE_OBJECT_START NAME=A\n;TYPE:Outer wall\nG1 X10.123 Y-0.04\nG1 X20.5 Y-0.04 E1\nG1 X20.5 Y7 E1\nEXCLUDE_OBJECT_END NAME=A\n");
    assert(parse(&r) == OBJECT_PREVIEW_OK);
    size_t length = 0;
    char *json = gcode_object_preview_json("internal", "a b.gcode", &r, 0, &length);
    assert(json && length == strlen(json));
    const char *expected = "{\"storage\":\"internal\",\"file\":\"a b.gcode\",\"object\":\"A\",\"layers\":1,\"step\":3,"
        "\"z_max\":0.25,\"bounds\":[10.12,-0.04,20.5,7],\"rings\":[{\"z\":0.25,\"paths\":[[10.12,-0.04,20.5,-0.04,20.5,7]]}],"
        "\"truncated\":false,\"objects\":[\"A\",\"Q\\\"x\"]}\n";
    if (strcmp(json, expected)) { fprintf(stderr, "%s\n%s", json, expected); abort(); }
    free(json);
    /* An object without outer wall has no height and no bounds. */
    json = gcode_object_preview_json("internal", "a.gcode", &r, 1, &length);
    assert(json && strstr(json, "\"layers\":0,\"step\":3,\"z_max\":null,\"bounds\":null,\"rings\":[]"));
    free(json);
    done(&r);

    /* ---- Files without object marks have no objects. ---- */
    used = 0;
    put("M83\n;LAYER_CHANGE\nG1 Z0.2\n;TYPE:Outer wall\n"); square(10, 10, 10);
    assert(parse(&r) == OBJECT_PREVIEW_OK && r.count == 0);
    done(&r);

    /* ---- Layers without layer marks: a new height is a new layer. ---- */
    used = 0;
    put("M83\nEXCLUDE_OBJECT_DEFINE NAME=Z\n");
    for (int l = 0; l < 9; l++) {
        put("G1 Z%.1f\nEXCLUDE_OBJECT_START NAME=Z\n;TYPE:WALL-OUTER\n", 0.3 * (l + 1)); square(5, 5, 5);
        put("G1 Z%.1f\n", 0.3 * (l + 1) + 0.4); /* a z-hop without extrusion is no layer */
        put("EXCLUDE_OBJECT_END NAME=Z\n");
    }
    assert(parse(&r) == OBJECT_PREVIEW_OK);
    assert(object(&r, "Z")->layers == 9 && object(&r, "Z")->ring_count == 3 && fabs(object(&r, "Z")->rings[1].z - 1.2) < 1e-5);
    done(&r);

    /* ---- Limits: 64 objects, names up to 127 bytes. ---- */
    char long_name[200];
    memset(long_name, 'n', sizeof(long_name)); long_name[127] = 0;
    used = 0;
    for (int i = 0; i < 63; i++) put("EXCLUDE_OBJECT_DEFINE NAME=O%d\n", i);
    put("EXCLUDE_OBJECT_DEFINE NAME=%s\n", long_name);
    assert(parse(&r) == OBJECT_PREVIEW_OK && r.count == 64 && !r.truncated && object_preview_find(&r, long_name) == 63);
    done(&r);
    long_name[127] = 'n'; long_name[128] = 0;
    put("EXCLUDE_OBJECT_DEFINE NAME=%s\nEXCLUDE_OBJECT_DEFINE NAME=O99\n", long_name);
    assert(parse(&r) == OBJECT_PREVIEW_OK && r.count == 64 && r.truncated);
    done(&r);

    /* ---- Time and size limits end the scan. ---- */
    tower(200);
    struct timespec past;
    clock_gettime(CLOCK_MONOTONIC, &past);
    past.tv_sec -= 1;
    assert(parse_with(&r, 50, OBJECT_PREVIEW_BYTES_MAX, &past) == OBJECT_PREVIEW_LIMIT);
    done(&r);
    assert(parse_with(&r, LAYER_PATH_TIME_MS, used - 1, NULL) == OBJECT_PREVIEW_TOO_LARGE);
    done(&r);
    assert(parse_with(&r, LAYER_PATH_TIME_MS, used, NULL) == OBJECT_PREVIEW_OK);
    done(&r);

    /* ---- Out of memory at any point: reported, and nothing stays allocated. ---- */
    used = 0;
    put("M83\n");
    for (int l = 0; l < 150; l++) {
        put(";LAYER_CHANGE\nG1 Z%.2f\n", 0.2 * (l + 1));
        for (int o = 0; o < 6; o++) {
            put("EXCLUDE_OBJECT_START NAME=M%d\n;TYPE:Outer wall\nG1 X%d Y10\n", o, o * 30);
            for (int k = 1; k <= 200; k++) put("G1 X%.2f Y%.2f E0.01\n", o * 30 + 10 * cos(k * 0.0314159), 10 + 10 * sin(k * 0.0314159));
            put("EXCLUDE_OBJECT_END NAME=M%d\n", o);
        }
    }
    assert(parse(&r) == OBJECT_PREVIEW_OK);
    size_t peak = r.peak, kept = r.bytes;
    assert(kept < peak && r.count == 6);
    done(&r);
    used = 0;
    put("M83\n");
    for (int l = 0; l < 30; l++) {
        put(";LAYER_CHANGE\nG1 Z%.2f\n", 0.2 * (l + 1));
        for (int o = 0; o < 3; o++) {
            put("EXCLUDE_OBJECT_START NAME=M%d\n;TYPE:Outer wall\nG1 X%d Y10\n", o, o * 30);
            for (int k = 1; k <= 300; k++) put("G1 X%.2f Y%.2f E0.01\n", o * 30 + 10 * cos(k * 0.0209), 10 + 10 * sin(k * 0.0209));
            put("EXCLUDE_OBJECT_END NAME=M%d\n", o);
        }
    }
    int failures = 0, succeeded = 0;
    for (long budget = 0; budget < 2000; budget++) {
        object_preview_alloc_budget = budget;
        int rc = parse(&r);
        object_preview_alloc_budget = -1;
        assert(rc == OBJECT_PREVIEW_OK || rc == OBJECT_PREVIEW_NO_MEMORY);
        if (rc == OBJECT_PREVIEW_NO_MEMORY) failures++;
        done(&r);
        if (rc == OBJECT_PREVIEW_OK) { succeeded = 1; break; }
    }
    assert(failures > 10 && succeeded);
    printf("PASS gcode object preview: objects, outer wall only, steps and rings, 128 points per ring, extrusion modes, limits, out of memory (peak %zu, kept %zu bytes)\n", peak, kept);
    rmdir(dir);
    return 0;
}
