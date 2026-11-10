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
    return gcode_layer_path_parse(file, layer, pts, LAYER_PATH_COLLECT_MAX, starts, LAYER_PATH_PATHS_MAX,
                                  info, &start, LAYER_PATH_TIME_SECONDS, LAYER_PATH_BYTES_MAX);
}

static void expect_ok(int rc, const layer_path_info *info, int min_points) {
    assert(rc == LAYER_PATH_OK);
    assert(info->point_count >= min_points);
    assert(info->point_count <= LAYER_PATH_POINTS_MAX);
}

int main(void) {
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
    snprintf(path, sizeof(path), "%s/layers.gcode", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/orca.gcode", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/travel.gcode", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/relative.gcode", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/abs.gcode", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/arc.gcode", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/g2.gcode", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/huge.gcode", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/implicit.gcode", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/json.gcode", dir); unlink(path);
    rmdir(dir);
    puts("PASS: layer-path query validation, layer markers, empty layer, travel, relative/absolute extrusion, arcs and point cap");
    return 0;
}
