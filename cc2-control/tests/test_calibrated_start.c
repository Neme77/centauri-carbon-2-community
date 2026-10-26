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
            /* Each command replaces the printer's whole table: the map goes in one. */
            const char *map = strstr(script, "CANVAS_SET_COLOR_TABLE T=0,2 ID=0,0 CHANNEL=1,3\n");
            assert(map && !strstr(map + 1, "CANVAS_SET_COLOR_TABLE"));
            assert(map < strstr(script, "SDCARD_PRINT_FILE"));
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
    assert(!strstr(script, "CANVAS_SET_COLOR_TABLE")); /* the external feed clears it beforehand */
    int one_tool[] = {1}, one_tray[] = {2};
    assert(build_calibrated_start_script(script, sizeof(script), 0, 'A', "local", "test.gcode", one_tool, one_tray, 1) == 0);
    assert(strstr(script, "\nCANVAS_SET_COLOR_TABLE T=1 ID=0 CHANNEL=2\nSDCARD_PRINT_FILE"));
    int all_tools[16], all_trays[16];
    for (int tool = 0; tool < 16; ++tool) { all_tools[tool] = 15 - tool; all_trays[tool] = tool % 4; }
    assert(build_calibrated_start_script(script, sizeof(script), 1, 'B', "local", "test.gcode", all_tools, all_trays, 16) == 0);
    assert(strstr(script, "CANVAS_SET_COLOR_TABLE T=15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0 "
                          "ID=0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0 CHANNEL=0,1,2,3,0,1,2,3,0,1,2,3,0,1,2,3\n"));
    /* A map cut short by the buffer fails the whole script instead of starting with part of it. */
    size_t map_at = (size_t)(strstr(script, "CANVAS_SET_COLOR_TABLE") - script), full = strlen(script);
    for (size_t cap = map_at + 1; cap <= full; cap += 7)
        assert(build_calibrated_start_script(script, cap, 1, 'B', "local", "test.gcode", all_tools, all_trays, 16) == -1);
    assert(build_calibrated_start_script(script, full + 1, 1, 'B', "local", "test.gcode", all_tools, all_trays, 16) == 0);
    puts("PASS calibrated start: adaptive skips preliminary stage, full runs once, plate and Canvas map in one command");
    return 0;
}
