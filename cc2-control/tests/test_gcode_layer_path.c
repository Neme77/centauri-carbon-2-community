#include "../src/gcode_layer_path.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static FILE *write_gcode(const char *dir, const char *name, const char *text) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w+");
    assert(f);
    assert(fputs(text, f) >= 0);
    assert(!fflush(f));
    rewind(f);
    return f;
}

static int parse_file(FILE *file, int layer, layer_path_info *info, layer_point *pts, int *starts) {
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    return gcode_layer_path_parse_at(file, layer, pts, LAYER_PATH_COLLECT_MAX, starts, LAYER_PATH_PATHS_MAX,
                                     info, &start, LAYER_PATH_TIME_MS, LAYER_PATH_BYTES_MAX, NULL, NULL, NULL, 0);
}

static void assert_json_matches_index(FILE *file, const char *storage, const char *name, int layer,
                                      layer_path_mark *marks, int *mark_count,
                                      char *cold, size_t cold_cap, char *indexed, size_t indexed_cap) {
    rewind(file);
    layer_path_info info;
    layer_point pts[LAYER_PATH_COLLECT_MAX];
    int starts[LAYER_PATH_PATHS_MAX];
    assert(parse_file(file, layer, &info, pts, starts) == LAYER_PATH_OK);
    int n = gcode_layer_path_json(cold, cold_cap, storage, name, &info, pts, starts);
    assert(n > 0);
    rewind(file);
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    layer_path_mark resume;
    int have_resume = layer_path_mark_pick(marks, *mark_count, layer - 1, &resume);
    assert(gcode_layer_path_parse_at(file, layer, pts, LAYER_PATH_COLLECT_MAX, starts, LAYER_PATH_PATHS_MAX,
                                     &info, &start, LAYER_PATH_TIME_MS, LAYER_PATH_BYTES_MAX,
                                     have_resume ? &resume : NULL, marks, mark_count,
                                     LAYER_PATH_INDEX_MAX) == LAYER_PATH_OK);
    int m = gcode_layer_path_json(indexed, indexed_cap, storage, name, &info, pts, starts);
    assert(m == n && memcmp(cold, indexed, (size_t)n) == 0);
}

static int scan_build_marks(FILE *file, int layer, layer_path_mark *marks, int *mark_count) {
    rewind(file);
    layer_path_info info;
    layer_point pts[LAYER_PATH_COLLECT_MAX];
    int starts[LAYER_PATH_PATHS_MAX];
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    *mark_count = 0;
    return gcode_layer_path_parse_at(file, layer, pts, LAYER_PATH_COLLECT_MAX, starts, LAYER_PATH_PATHS_MAX,
                                     &info, &start, LAYER_PATH_TIME_MS, LAYER_PATH_BYTES_MAX,
                                     NULL, marks, mark_count, LAYER_PATH_INDEX_MAX);
}

static void expect_ok(int rc, const layer_path_info *info, int min_points) {
    assert(rc == LAYER_PATH_OK);
    assert(info->point_count >= min_points);
    assert(info->point_count <= LAYER_PATH_POINTS_MAX);
}

int main(void) {
    (void)gcode_layer_path_parse; /* used by test_gcode_layer_index.c; kept in the header for the index */
    char dir[] = "/tmp/cc2-layer-XXXXXX";
    assert(mkdtemp(dir));
    layer_point pts[LAYER_PATH_COLLECT_MAX];
    int starts[LAYER_PATH_PATHS_MAX];
    layer_path_info info;
    char storage[16], file[LAYER_PATH_NAME_MAX];
    int layer = 0;

    assert(gcode_layer_path_query("file=cube.gcode&layer=1", storage, file, &layer) == LAYER_PATH_OK);
    assert(!strcmp(storage, "internal") && !strcmp(file, "cube.gcode") && layer == 1);
    assert(gcode_layer_path_query("storage=usb&file=folder%2Fpart.gcode&layer=12", storage, file, &layer) == LAYER_PATH_OK);
    assert(!strcmp(storage, "usb") && !strcmp(file, "folder/part.gcode") && layer == 12);
    assert(gcode_layer_path_query("file=../cube.gcode&layer=1", storage, file, &layer) == LAYER_PATH_BAD_QUERY);
    assert(gcode_layer_path_query("file=/etc/passwd.gcode&layer=1", storage, file, &layer) == LAYER_PATH_BAD_QUERY);
    assert(gcode_layer_path_query("file=cube.txt&layer=1", storage, file, &layer) == LAYER_PATH_BAD_QUERY);
    assert(gcode_layer_path_query("storage=other&file=cube.gcode&layer=1", storage, file, &layer) == LAYER_PATH_BAD_QUERY);
    assert(gcode_layer_path_query("file=cube.gcode&layer=0", storage, file, &layer) == LAYER_PATH_BAD_LAYER);
    assert(gcode_layer_path_query("file=cube.gcode&layer=-1", storage, file, &layer) == LAYER_PATH_BAD_LAYER);
    assert(gcode_layer_path_query("file=cube.gcode&layer=1.5", storage, file, &layer) == LAYER_PATH_BAD_LAYER);
    assert(gcode_layer_path_query("file=cube.gcode", storage, file, &layer) == LAYER_PATH_BAD_LAYER);
    assert(gcode_layer_path_query("layer=1", storage, file, &layer) == LAYER_PATH_BAD_QUERY);
    assert(gcode_layer_path_query("file=cube.gcode&layer=1&extra=1", storage, file, &layer) == LAYER_PATH_BAD_QUERY);
    assert(gcode_layer_path_query("file=..%2Fcube.gcode&layer=1", storage, file, &layer) == LAYER_PATH_BAD_QUERY);
    assert(gcode_layer_path_query(NULL, storage, file, &layer) == LAYER_PATH_BAD_QUERY);

    FILE *f = write_gcode(dir, "layers.gcode",
        ";LAYER:0\nG90\nM82\nG1 X10 Y10 E1\nG1 X20 Y10 E2\n"
        ";LAYER:1\nG0 X0 Y0\n"
        ";LAYER:2\nG0 X30 Y30\nG1 X40 Y30 E4\n");
    assert(parse_file(f, 1, &info, pts, starts) == LAYER_PATH_OK);
    expect_ok(LAYER_PATH_OK, &info, 2);
    assert(info.layer == 1 && info.total_layers == 3 && info.path_count >= 1);
    assert(fabs(pts[0].x - 10) < 0.05 && fabs(pts[0].y - 10) < 0.05);
    rewind(f);
    assert(parse_file(f, 2, &info, pts, starts) == LAYER_PATH_OK);
    assert(info.point_count == 0 && info.path_count == 0 && info.total_layers == 3);
    rewind(f);
    assert(parse_file(f, 3, &info, pts, starts) == LAYER_PATH_OK);
    assert(info.path_count >= 1 && info.point_count >= 2);
    assert(fabs(pts[0].x - 30) < 0.05 && fabs(pts[0].y - 30) < 0.05);
    rewind(f);
    assert(parse_file(f, 4, &info, pts, starts) == LAYER_PATH_OUTSIDE);
    rewind(f);
    assert(parse_file(f, 9, &info, pts, starts) == LAYER_PATH_OUTSIDE);
    fclose(f);

    f = write_gcode(dir, "orca.gcode",
        ";LAYER_CHANGE\n;Z:0.2\nG1 Z0.2 F300\nG1 X5 Y5 E0.2\nG1 X15 Y5 E0.4\n"
        ";BEFORE_LAYER_CHANGE\nG1 X15 Y15 E0.4\n"
        ";LAYER_CHANGE\n;Z:0.4\nG1 Z0.4\nG0 X25 Y25\nG1 X35 Y25 E1.0\n"
        ";AFTER_LAYER_CHANGE\n");
    assert(parse_file(f, 1, &info, pts, starts) == LAYER_PATH_OK);
    assert(info.total_layers == 2);
    assert(info.point_count >= 2);
    assert(fabs(pts[0].x - 5) < 0.05);
    rewind(f);
    assert(parse_file(f, 2, &info, pts, starts) == LAYER_PATH_OK);
    assert(info.point_count >= 2);
    assert(fabs(pts[0].x - 25) < 0.05);
    fclose(f);

    /* ElegooSlicer writes ;LAYER_CHANGE together with Cura-style ;LAYER:N, puts
     * ;LAYER:0 into the start block and numbers the printed layers ;LAYER:1..N.
     * ;LAYER_CHANGE alone must number the layers, and ;Z: must win over a later
     * z-hop or the end G-code lift. */
    f = write_gcode(dir, "elegoo.gcode",
        ";LAYER_COUNT:3\nG90\nM82\n"
        ";LAYER:0\nG1 Z0.8 F600\nG0 X1 Y1\nG1 X60 Y1 E2\n"
        ";LAYER_CHANGE\n;Z:0.2\n;LAYER:1\nG1 Z0.2 F600\nG0 X5 Y5\nG1 X15 Y5 E3\nG1 X15 Y15 E4\n"
        ";LAYER_CHANGE\n;Z:0.4\n;LAYER:2\nG1 Z0.4 F600\nG0 X25 Y25\nG1 X35 Y25 E5\nG1 X35 Y35 E6\nG1 Z0.8\n"
        ";LAYER_CHANGE\n;Z:0.6\n;LAYER:3\nG1 Z0.6 F600\nG0 X45 Y45\nG1 X55 Y45 E7\nG1 X55 Y55 E8\n"
        "G1 Z5.6 F600\nG0 X0 Y200\nM104 S0\n");
    assert(parse_file(f, 1, &info, pts, starts) == LAYER_PATH_OK);
    assert(info.layer == 1 && info.total_layers == 3);
    assert(info.path_count >= 1 && info.point_count >= 2);
    assert(fabs(pts[0].x - 5) < 0.05 && fabs(pts[0].y - 5) < 0.05);
    for (int i = 0; i < info.point_count; i++) assert(pts[i].x > 4 && pts[i].x < 16 && pts[i].y > 4 && pts[i].y < 16);
    assert(fabs(info.z - 0.2) < 0.001);
    rewind(f);
    assert(parse_file(f, 2, &info, pts, starts) == LAYER_PATH_OK);
    assert(info.layer == 2 && info.total_layers == 3);
    assert(info.path_count >= 1 && info.point_count >= 2);
    assert(fabs(pts[0].x - 25) < 0.05 && fabs(pts[0].y - 25) < 0.05);
    for (int i = 0; i < info.point_count; i++) assert(pts[i].x > 24 && pts[i].x < 36 && pts[i].y > 24 && pts[i].y < 36);
    assert(fabs(info.z - 0.4) < 0.001);
    rewind(f);
    assert(parse_file(f, 3, &info, pts, starts) == LAYER_PATH_OK);
    assert(info.layer == 3 && info.total_layers == 3);
    assert(info.path_count >= 1 && info.point_count >= 2);
    assert(fabs(pts[0].x - 45) < 0.05 && fabs(pts[0].y - 45) < 0.05);
    for (int i = 0; i < info.point_count; i++) assert(pts[i].x > 44 && pts[i].x < 56 && pts[i].y > 44 && pts[i].y < 56);
    assert(fabs(info.z - 0.6) < 0.001);
    rewind(f);
    assert(parse_file(f, 4, &info, pts, starts) == LAYER_PATH_OUTSIDE);
    fclose(f);

    f = write_gcode(dir, "travel.gcode",
        ";LAYER:0\nG90\nM82\nG1 X0 Y0 E1\nG1 X10 Y0 E2\nG0 X10 Y10\nG1 X20 Y10 E3\nG1 X20 Y20 E4\n");
    assert(parse_file(f, 1, &info, pts, starts) == LAYER_PATH_OK);
    assert(info.path_count == 2);
    assert(info.point_count >= 4);
    fclose(f);

    f = write_gcode(dir, "relative.gcode",
        ";LAYER:0\nG91\nM83\nG92 X0 Y0 E0\nG1 X10 Y0 E0.5\nG1 X10 Y0 E0.5\nG1 X0 Y10 E-0.2\nG1 X10 Y0 E0.4\n");
    assert(parse_file(f, 1, &info, pts, starts) == LAYER_PATH_OK);
    assert(info.path_count == 2);
    assert(fabs(pts[0].x) < 0.05 && fabs(pts[0].y) < 0.05);
    assert(fabs(pts[1].x - 10) < 0.05);
    fclose(f);

    f = write_gcode(dir, "abs.gcode",
        ";LAYER:0\nG90\nM82\nG1 X10 Y10 E1.0\nG1 X20 Y10 E1.5\nG1 X20 Y20 E1.2\nG1 X30 Y20 E2.0\n");
    assert(parse_file(f, 1, &info, pts, starts) == LAYER_PATH_OK);
    assert(info.path_count == 2);
    fclose(f);

    f = write_gcode(dir, "arc.gcode",
        ";LAYER:0\nG90\nM83\nG1 X10 Y10 E0.1\nG3 X20 Y20 I0 J10 E1.0\n");
    assert(parse_file(f, 1, &info, pts, starts) == LAYER_PATH_OK);
    assert(info.point_count >= 4);
    double cx = 10, cy = 20;
    for (int i = 0; i < info.point_count; i++) {
        double r = hypot(pts[i].x - cx, pts[i].y - cy);
        assert(fabs(r - 10) < 0.6);
    }
    fclose(f);

    f = write_gcode(dir, "g2.gcode",
        ";LAYER:0\nG90\nM83\nG1 X20 Y10 E0.1\nG2 X10 Y20 I0 J10 E0.8\n");
    assert(parse_file(f, 1, &info, pts, starts) == LAYER_PATH_OK);
    assert(info.point_count >= 4);
    fclose(f);

    FILE *huge = write_gcode(dir, "huge.gcode", ";LAYER:0\nG90\nM83\nG1 X0 Y0 E0.01\n");
    assert(!fseek(huge, 0, SEEK_END));
    for (int i = 1; i <= 6000; i++) {
        char line[80];
        snprintf(line, sizeof(line), "G1 X%d Y%d E0.01\n", i % 2, i);
        assert(fputs(line, huge) >= 0);
    }
    rewind(huge);
    assert(parse_file(huge, 1, &info, pts, starts) == LAYER_PATH_OK);
    assert(info.point_count <= LAYER_PATH_POINTS_MAX);
    assert(info.truncated);
    fclose(huge);

    f = write_gcode(dir, "implicit.gcode", "G90\nM83\nG1 X1 Y1 E0.2\nG1 X8 Y1 E0.2\n");
    assert(parse_file(f, 1, &info, pts, starts) == LAYER_PATH_OK);
    assert(info.total_layers == 1 && info.point_count >= 2);
    rewind(f);
    assert(parse_file(f, 2, &info, pts, starts) == LAYER_PATH_OUTSIDE);
    fclose(f);

    char json[4096];
    f = write_gcode(dir, "json.gcode", "; total layer number: 4\n;LAYER:0\nG1 X10 Y10 E1\nG1 X20 Y10 E2\n");
    assert(parse_file(f, 1, &info, pts, starts) == LAYER_PATH_OK);
    int n = gcode_layer_path_json(json, sizeof(json), "internal", "json.gcode", &info, pts, starts);
    assert(n > 0);
    assert(strstr(json, "\"layer\":1"));
    assert(strstr(json, "\"total_layers\":4"));
    assert(strstr(json, "\"paths\":["));
    fclose(f);

    char path[512];
    {
        /* An ElegooSlicer file with the layer count in its header is read only up to the
         * end of the requested layer; the answer is the same as a full read. */
        char text[65536];
        size_t used = (size_t)snprintf(text, sizeof(text), "; HEADER_BLOCK_START\n; total layer number: 40\n; HEADER_BLOCK_END\n");
        for (int l = 0; l < 40; l++)
            used += (size_t)snprintf(text + used, sizeof(text) - used,
                ";LAYER_CHANGE\n;Z:%.2f\nG1 Z%.2f\nG1 X10 Y%d\nG1 X60 Y%d E%d\nG1 X60 Y%d E%d\n",
                0.2 * (l + 1), 0.2 * (l + 1), l, l, 2 * l + 1, l + 30, 2 * l + 2);
        f = write_gcode(dir, "early.gcode", text);
        expect_ok(parse_file(f, 3, &info, pts, starts), &info, 2);
        long stopped = ftell(f);
        assert(info.layer == 3 && info.total_layers == 40 && info.z > 0.59 && info.z < 0.61);
        assert(stopped > 0 && (size_t)stopped < used / 4);
        layer_path_info late;
        rewind(f);
        expect_ok(parse_file(f, 40, &late, pts, starts), &late, 2);
        assert(late.layer == 40 && late.total_layers == 40 && late.z > 7.99 && late.z < 8.01);
        rewind(f);
        assert(parse_file(f, 41, &late, pts, starts) == LAYER_PATH_OUTSIDE);
        fclose(f);
        /* Without the header count the file is read to the end to count its layers. */
        char *body = strstr(text, "; HEADER_BLOCK_END\n") + strlen("; HEADER_BLOCK_END\n");
        f = write_gcode(dir, "nocount.gcode", body);
        expect_ok(parse_file(f, 3, &info, pts, starts), &info, 2);
        assert(info.total_layers == 40 && (size_t)ftell(f) == strlen(body));
        fclose(f);
        snprintf(path, sizeof(path), "%s/early.gcode", dir); unlink(path);
        snprintf(path, sizeof(path), "%s/nocount.gcode", dir); unlink(path);
    }
    /* Layer index: a follow-up seek must match a from-scratch parse byte for byte,
     * and must not reread the whole file. Tiny file, then a normal multi-layer file. */
    {
        char cold[8192], indexed[8192];
        layer_path_mark marks[LAYER_PATH_INDEX_MAX];
        int mark_count = 0;
        f = write_gcode(dir, "index-tiny.gcode",
            "; total layer number: 2\nG90\nM83\nG92 E0\n"
            ";LAYER_CHANGE\n;Z:0.2\nG1 Z0.2\nG1 X10 Y10 E0.5\nG1 X30 Y10 E0.5\n"
            ";LAYER_CHANGE\n;Z:0.4\nG1 Z0.4\nG1 X30 Y20 E0.4\nG1 X10 Y20 E0.4\n");
        for (int layer = 1; layer <= 2; layer++) {
            rewind(f);
            assert(parse_file(f, layer, &info, pts, starts) == LAYER_PATH_OK);
            int n = gcode_layer_path_json(cold, sizeof(cold), "internal", "index-tiny.gcode", &info, pts, starts);
            assert(n > 0);
            rewind(f);
            struct timespec start;
            clock_gettime(CLOCK_MONOTONIC, &start);
            mark_count = 0;
            assert(gcode_layer_path_parse_at(f, 1, pts, LAYER_PATH_COLLECT_MAX, starts, LAYER_PATH_PATHS_MAX,
                                             &info, &start, LAYER_PATH_TIME_MS, LAYER_PATH_BYTES_MAX,
                                             NULL, marks, &mark_count, LAYER_PATH_INDEX_MAX) == LAYER_PATH_OK);
            layer_path_mark resume;
            assert(layer_path_mark_pick(marks, mark_count, layer - 1, &resume));
            rewind(f);
            clock_gettime(CLOCK_MONOTONIC, &start);
            long before = resume.offset;
            assert(gcode_layer_path_parse_at(f, layer, pts, LAYER_PATH_COLLECT_MAX, starts, LAYER_PATH_PATHS_MAX,
                                             &info, &start, LAYER_PATH_TIME_MS, LAYER_PATH_BYTES_MAX,
                                             &resume, marks, &mark_count, LAYER_PATH_INDEX_MAX) == LAYER_PATH_OK);
            int m = gcode_layer_path_json(indexed, sizeof(indexed), "internal", "index-tiny.gcode", &info, pts, starts);
            assert(m == n && memcmp(cold, indexed, (size_t)n) == 0);
            if (layer == 2) assert(before > 0);
        }
        fclose(f);

        char text[96 * 1024];
        size_t used = (size_t)snprintf(text, sizeof(text), "; total layer number: 40\nG90\nM82\n");
        for (int l = 0; l < 40; l++)
            used += (size_t)snprintf(text + used, sizeof(text) - used,
                ";LAYER_CHANGE\n;Z:%.2f\nG1 Z%.2f\nG1 X10 Y%d\nG1 X%d Y%d E%d\n",
                0.2 * (l + 1), 0.2 * (l + 1), l, 20 + l, l, l + 1);
        f = write_gcode(dir, "index-normal.gcode", text);
        char *cold_json = malloc(LAYER_PATH_JSON_MAX);
        char *index_json = malloc(LAYER_PATH_JSON_MAX);
        assert(cold_json && index_json);
        for (int layer = 1; layer <= 40; layer += 19) {
            rewind(f);
            assert(parse_file(f, layer, &info, pts, starts) == LAYER_PATH_OK);
            long cold_end = ftell(f);
            int n = gcode_layer_path_json(cold_json, LAYER_PATH_JSON_MAX, "internal", "index-normal.gcode", &info, pts, starts);
            assert(n > 0 && cold_end > 0);
            rewind(f);
            struct timespec start;
            clock_gettime(CLOCK_MONOTONIC, &start);
            mark_count = 0;
            assert(gcode_layer_path_parse_at(f, layer, pts, LAYER_PATH_COLLECT_MAX, starts, LAYER_PATH_PATHS_MAX,
                                             &info, &start, LAYER_PATH_TIME_MS, LAYER_PATH_BYTES_MAX,
                                             NULL, marks, &mark_count, LAYER_PATH_INDEX_MAX) == LAYER_PATH_OK);
            int built = gcode_layer_path_json(index_json, LAYER_PATH_JSON_MAX, "internal", "index-normal.gcode", &info, pts, starts);
            assert(built == n && memcmp(cold_json, index_json, (size_t)n) == 0);
            layer_path_mark resume;
            assert(layer_path_mark_pick(marks, mark_count, layer - 1, &resume));
            rewind(f);
            clock_gettime(CLOCK_MONOTONIC, &start);
            assert(gcode_layer_path_parse_at(f, layer, pts, LAYER_PATH_COLLECT_MAX, starts, LAYER_PATH_PATHS_MAX,
                                             &info, &start, LAYER_PATH_TIME_MS, LAYER_PATH_BYTES_MAX,
                                             &resume, marks, &mark_count, LAYER_PATH_INDEX_MAX) == LAYER_PATH_OK);
            int m = gcode_layer_path_json(index_json, LAYER_PATH_JSON_MAX, "internal", "index-normal.gcode", &info, pts, starts);
            assert(m == n && memcmp(cold_json, index_json, (size_t)n) == 0);
            long followed = ftell(f) - (long)resume.offset;
            if (layer > 1) assert(resume.offset > 0 && followed > 0 && followed < cold_end / 4);
        }
        /* The next layer after a scan starts at its own marker, not at byte 0. */
        layer_path_mark next;
        assert(layer_path_mark_pick(marks, mark_count, 20, &next));
        rewind(f);
        assert(parse_file(f, 21, &info, pts, starts) == LAYER_PATH_OK);
        int n = gcode_layer_path_json(cold_json, LAYER_PATH_JSON_MAX, "internal", "index-normal.gcode", &info, pts, starts);
        rewind(f);
        struct timespec start;
        clock_gettime(CLOCK_MONOTONIC, &start);
        assert(gcode_layer_path_parse_at(f, 21, pts, LAYER_PATH_COLLECT_MAX, starts, LAYER_PATH_PATHS_MAX,
                                         &info, &start, LAYER_PATH_TIME_MS, LAYER_PATH_BYTES_MAX,
                                         &next, marks, &mark_count, LAYER_PATH_INDEX_MAX) == LAYER_PATH_OK);
        int m = gcode_layer_path_json(index_json, LAYER_PATH_JSON_MAX, "internal", "index-normal.gcode", &info, pts, starts);
        assert(m == n && memcmp(cold_json, index_json, (size_t)n) == 0);
        assert(next.offset > 0 && ftell(f) - (long)next.offset < (long)used / 4);
        /* A remembered layer that cannot be used falls back to reading from the start:
         * a stream that cannot seek (fseeko fails) and a mark with an invalid offset both
         * give the same bytes as the scan from the start. */
        {
            int pipe_fd[2];
            assert(used < 4096 && !pipe(pipe_fd));
            assert(write(pipe_fd[1], text, used) == (ssize_t)used);
            close(pipe_fd[1]);
            FILE *stream = fdopen(pipe_fd[0], "r");
            assert(stream);
            clock_gettime(CLOCK_MONOTONIC, &start);
            int pipe_count = 0;
            assert(gcode_layer_path_parse_at(stream, 21, pts, LAYER_PATH_COLLECT_MAX, starts, LAYER_PATH_PATHS_MAX,
                                             &info, &start, LAYER_PATH_TIME_MS, LAYER_PATH_BYTES_MAX,
                                             &next, marks, &pipe_count, LAYER_PATH_INDEX_MAX) == LAYER_PATH_OK);
            m = gcode_layer_path_json(index_json, LAYER_PATH_JSON_MAX, "internal", "index-normal.gcode", &info, pts, starts);
            assert(m == n && memcmp(cold_json, index_json, (size_t)n) == 0);
            fclose(stream);

            layer_path_mark bad = next;
            bad.offset = -1;
            rewind(f);
            clock_gettime(CLOCK_MONOTONIC, &start);
            assert(gcode_layer_path_parse_at(f, 21, pts, LAYER_PATH_COLLECT_MAX, starts, LAYER_PATH_PATHS_MAX,
                                             &info, &start, LAYER_PATH_TIME_MS, LAYER_PATH_BYTES_MAX,
                                             &bad, NULL, NULL, 0) == LAYER_PATH_OK);
            m = gcode_layer_path_json(index_json, LAYER_PATH_JSON_MAX, "internal", "index-normal.gcode", &info, pts, starts);
            assert(m == n && memcmp(cold_json, index_json, (size_t)n) == 0);
        }
        free(cold_json);
        free(index_json);
        fclose(f);
        snprintf(path, sizeof(path), "%s/index-tiny.gcode", dir); unlink(path);
        snprintf(path, sizeof(path), "%s/index-normal.gcode", dir); unlink(path);
    }

    /* Old vs indexed path bytes: slicer variants, full mark table, on-mark seeks, backward layer. */
    {
        char *cold = malloc(LAYER_PATH_JSON_MAX);
        char *indexed = malloc(LAYER_PATH_JSON_MAX);
        layer_path_mark marks[LAYER_PATH_INDEX_MAX];
        int mark_count = 0;
        assert(cold && indexed);

        /* a) ElegooSlicer: ;LAYER:0 in the start block plus ;LAYER_CHANGE / ;LAYER:N — layer 0 mark is the start block (first offset wins). */
        f = write_gcode(dir, "idx-elegoo.gcode",
            ";LAYER_COUNT:2\nG90\nM82\n"
            ";LAYER:0\nG1 Z0.8\nG0 X1 Y1\n"
            ";LAYER_CHANGE\n;Z:0.2\n;LAYER:1\nG1 Z0.2\nG0 X5 Y5\nG1 X15 Y5 E1\n"
            ";LAYER_CHANGE\n;Z:0.4\n;LAYER:2\nG1 Z0.4\nG0 X25 Y25\nG1 X35 Y25 E2\n");
        assert(scan_build_marks(f, 2, marks, &mark_count) == LAYER_PATH_OK);
        assert(mark_count >= 2);
        long layer0_off = -1, change0_off = -1;
        for (int i = 0; i < mark_count; i++) {
            if (marks[i].layer == 0) layer0_off = (long)marks[i].offset;
            else if (marks[i].layer == 1 && change0_off < 0) change0_off = (long)marks[i].offset;
        }
        assert(layer0_off >= 0 && change0_off >= 0 && layer0_off < change0_off);
        for (int layer = 1; layer <= 2; layer++)
            assert_json_matches_index(f, "internal", "idx-elegoo.gcode", layer, marks, &mark_count, cold, LAYER_PATH_JSON_MAX, indexed, LAYER_PATH_JSON_MAX);
        fclose(f);

        /* b) Pure Cura: 0-based ;LAYER:N only, no ;LAYER_CHANGE. */
        f = write_gcode(dir, "idx-cura.gcode",
            ";LAYER:0\nG90\nM82\nG1 X0 Y0 E0.1\nG1 X10 Y0 E0.2\n"
            ";LAYER:1\nG0 X0 Y0\nG1 X10 Y10 E0.3\n"
            ";LAYER:2\nG1 X20 Y10 E0.4\nG1 X20 Y20 E0.5\n");
        mark_count = 0;
        assert(scan_build_marks(f, 3, marks, &mark_count) == LAYER_PATH_OK);
        assert(mark_count == 3);
        for (int layer = 1; layer <= 3; layer++)
            assert_json_matches_index(f, "internal", "idx-cura.gcode", layer, marks, &mark_count, cold, LAYER_PATH_JSON_MAX, indexed, LAYER_PATH_JSON_MAX);
        fclose(f);

        /* c) More than LAYER_PATH_INDEX_MAX layers: table stays full; last slot tracks the highest new mark. */
        {
            char *text = malloc(64 * 1024);
            assert(text);
            size_t used = 0;
            const int layers = LAYER_PATH_INDEX_MAX + 8;
            for (int l = 0; l < layers; l++)
                used += (size_t)snprintf(text + used, 64 * 1024 - used, ";LAYER:%d\nG1 X%d Y%d E0.01\n", l, l % 50, l);
            f = write_gcode(dir, "idx-full.gcode", text);
            free(text);
            mark_count = 0;
            assert(scan_build_marks(f, layers, marks, &mark_count) == LAYER_PATH_OK);
            assert(mark_count == LAYER_PATH_INDEX_MAX);
            assert(marks[mark_count - 1].layer == layers - 1);
            assert_json_matches_index(f, "internal", "idx-full.gcode", layers, marks, &mark_count, cold, LAYER_PATH_JSON_MAX, indexed, LAYER_PATH_JSON_MAX);
            assert_json_matches_index(f, "internal", "idx-full.gcode", 1, marks, &mark_count, cold, LAYER_PATH_JSON_MAX, indexed, LAYER_PATH_JSON_MAX);
            fclose(f);
        }

        /* d) Target layer exactly on a saved mark and on the next layer after it. */
        {
            char text[8192];
            size_t used = (size_t)snprintf(text, sizeof(text), "G90\nM83\n");
            for (int l = 0; l < 12; l++)
                used += (size_t)snprintf(text + used, sizeof(text) - used,
                    ";LAYER_CHANGE\n;Z:%.2f\nG1 Z%.2f\nG1 X%d Y%d E%.1f\nG1 X%d Y%d E%.1f\n",
                    0.2 * (l + 1), 0.2 * (l + 1), 10 + l, 10, (double)l, 30 + l, 10, (double)l + 0.5);
            f = write_gcode(dir, "idx-onmark.gcode", text);
            mark_count = 0;
            assert(scan_build_marks(f, 12, marks, &mark_count) == LAYER_PATH_OK);
            for (int li = 0; li < 2; li++) {
                int layer = li == 0 ? 5 : 6;
                layer_path_mark at, next;
                assert(layer_path_mark_pick(marks, mark_count, layer - 1, &at));
                assert(at.layer == layer - 1);
                rewind(f);
                assert_json_matches_index(f, "internal", "idx-onmark.gcode", layer, marks, &mark_count, cold, LAYER_PATH_JSON_MAX, indexed, LAYER_PATH_JSON_MAX);
                if (layer < 12) {
                    assert(layer_path_mark_pick(marks, mark_count, layer, &next));
                    assert(next.layer == layer);
                }
            }
            fclose(f);
        }

        /* e) Backward query: deep scan first, then an earlier layer still matches cold bytes. */
        {
            char *text = malloc(96 * 1024);
            assert(text);
            size_t used = (size_t)snprintf(text, 96 * 1024, "; total layer number: 160\nG90\nM82\n");
            for (int l = 0; l < 160; l++)
                used += (size_t)snprintf(text + used, 96 * 1024 - used,
                    ";LAYER_CHANGE\n;Z:%.2f\nG1 Z%.2f\nG1 X%d Y%d E%d\n", 0.2 * (l + 1), 0.2 * (l + 1), l, l, l + 1);
            f = write_gcode(dir, "idx-back.gcode", text);
            free(text);
            mark_count = 0;
            assert(scan_build_marks(f, 150, marks, &mark_count) == LAYER_PATH_OK);
            assert_json_matches_index(f, "internal", "idx-back.gcode", 150, marks, &mark_count, cold, LAYER_PATH_JSON_MAX, indexed, LAYER_PATH_JSON_MAX);
            assert_json_matches_index(f, "internal", "idx-back.gcode", 20, marks, &mark_count, cold, LAYER_PATH_JSON_MAX, indexed, LAYER_PATH_JSON_MAX);
            fclose(f);
        }

        free(cold);
        free(indexed);
        snprintf(path, sizeof(path), "%s/idx-elegoo.gcode", dir); unlink(path);
        snprintf(path, sizeof(path), "%s/idx-cura.gcode", dir); unlink(path);
        snprintf(path, sizeof(path), "%s/idx-full.gcode", dir); unlink(path);
        snprintf(path, sizeof(path), "%s/idx-onmark.gcode", dir); unlink(path);
        snprintf(path, sizeof(path), "%s/idx-back.gcode", dir); unlink(path);
    }
    snprintf(path, sizeof(path), "%s/layers.gcode", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/orca.gcode", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/elegoo.gcode", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/travel.gcode", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/relative.gcode", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/abs.gcode", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/arc.gcode", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/g2.gcode", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/huge.gcode", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/implicit.gcode", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/json.gcode", dir); unlink(path);
    rmdir(dir);
    puts("PASS: layer-path query validation, layer markers, ElegooSlicer ;LAYER_CHANGE numbering and ;Z: height, empty layer, travel, relative/absolute extrusion, arcs, point cap and early stop after the requested layer");
    return 0;
}
