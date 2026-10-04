#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

static void fixture(const char *text) {
    FILE *f = fopen("filaments.gcode", "w"); assert(f);
    fputs(text, f); fclose(f);
}
int main(void) {
    char root[PATH_MAX_LOCAL]; assert(getcwd(root, sizeof(root)));
    gcode_filament_info info[GCODE_TOOLS_MAX];
    fixture("; filament_colour = #112233;;#ABCDEF80\n; filament_type = PLA;;PETG\nT0\nT2\n");
    assert(gcode_read_filaments(root, "filaments.gcode", info) == 0);
    assert(strcmp(info[0].color, "#112233") == 0);
    assert(!info[1].color[0] && !info[1].material[0]);
    assert(strcmp(info[2].color, "#ABCDEF80") == 0);
    assert(strcmp(info[2].material, "PETG") == 0);
    fixture("; filament_color = #zz0000,#123456\n; filament_type = \"PLA\", \"PETG\"\n");
    assert(gcode_read_filaments(root, "filaments.gcode", info) == 0);
    assert(!info[0].color[0]); assert(strcmp(info[1].color, "#123456") == 0);
    assert(strcmp(info[1].material, "PETG") == 0);
    fixture("; filament_colour = #12345\nG28\n");
    assert(gcode_read_filaments(root, "filaments.gcode", info) == 0);
    assert(!info[0].color[0] && !info[0].material[0]);
    fixture("; filament_type = PLA\\\"test\n");
    assert(gcode_read_filaments(root, "filaments.gcode", info) == 0);
    char escaped[130]; json_escape(escaped, sizeof(escaped), info[0].material);
    assert(strcmp(escaped, "PLA\\\\\\\"test") == 0);
    remove("filaments.gcode");
    puts("PASS: sparse tool indices, semicolon/comma lists, quoted materials, invalid/missing colors and JSON escaping");
    return 0;
}
