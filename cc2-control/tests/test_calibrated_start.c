#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

int main(void) {
    char script[4096];
    int tools[] = {0, 2}, trays[] = {1, 3};
    for (int full = 0; full <= 1; ++full) {
        for (int side = 0; side <= 1; ++side) {
            assert(build_calibrated_start_script(script, sizeof(script), full,
                side ? 'B' : 'A', "local", "Benchy test.gcode", tools, trays, 2) == 0);
            assert(strstr(script, "SLICE_CFG_MODEL=1"));
            assert(!strstr(script, "SLICE_CFG_MODEL=0"));
            assert(strstr(script, side ? "PRINT_SURFACE_SET PLANE=1\n" : "PRINT_SURFACE_SET PLANE=0\n"));
            assert(strstr(script, "CANVAS_SET_COLOR_TABLE T=0 ID=0 CHANNEL=1\n"));
            assert(strstr(script, "CANVAS_SET_COLOR_TABLE T=2 ID=0 CHANNEL=3\n"));
            assert(strstr(script, "SDCARD_PRINT_FILE FILENAME=local/\"Benchy test.gcode\" SLICE_CFG_MODEL=1"));
            if (full) {
                assert(strstr(script, "EXECUTE_CALIBRATE_FROM_SLICER=0\n"));
                const char *calibration = strstr(script, "BED_MESH_CALIBRATE PROFILE=");
                assert(calibration && !strstr(calibration + 1, "BED_MESH_CALIBRATE PROFILE="));
                assert(strstr(script, side ? "PROFILE=default1 BED_TEMP=60\n" : "PROFILE=default BED_TEMP=60\n"));
                assert(calibration < strstr(script, "SDCARD_PRINT_FILE"));
            } else {
                assert(strstr(script, "EXECUTE_CALIBRATE_FROM_SLICER=1\n"));
                assert(!strstr(script, "BED_MESH_CALIBRATE PROFILE="));
            }
        }
    }
    assert(build_calibrated_start_script(script, 12, 0, 'A', "local", "test.gcode", tools, trays, 2) == -1);
    assert(build_calibrated_start_script(script, sizeof(script), 0, 'A', "local", "bad\".gcode", tools, trays, 2) == -1);
    assert(build_calibrated_start_script(script, sizeof(script), 0, 'A', "local", "test.gcode", NULL, NULL, 2) == -1);
    assert(build_calibrated_start_script(script, sizeof(script), 0, 'A', "local", "test.gcode", NULL, NULL, 0) == 0);
    puts("PASS calibrated start: adaptive skips preliminary stage, full runs once, plate and Canvas preserved");
    return 0;
}
