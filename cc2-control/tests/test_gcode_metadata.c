#include <assert.h>
#include <ctype.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "../src/gcode_metadata.h"

int main(void) {
    int wait = 0;
    assert(gcode_metadata_bed_command("; start_gcode = M190 S190\n", &wait) < 0);
    assert(gcode_metadata_bed_command("M140 S0\n", &wait) < 0);
    assert(gcode_metadata_bed_command("M140 S30\n", &wait) == 30 && !wait);
    assert(gcode_metadata_bed_command("M190 S60 ; bed\n", &wait) == 60 && wait);
    assert(gcode_metadata_bed_command(" m190 R65\n", &wait) == 65 && wait);
    assert(gcode_metadata_bed_command("M190 S{bed_temperature}\n", &wait) < 0);
    assert(gcode_metadata_bed_command("M1900 S190\n", &wait) < 0);
    assert(gcode_metadata_number("; bed_temperature_gcode = M190 S60\n", "bed_temperature", 200) < 0);
    assert(gcode_metadata_number("; bed_temperature_formula = M190 S60\n", "bed_temperature", 200) < 0);
    assert(gcode_metadata_number("; first_layer_bed_temperature = 60\n", "bed_temperature", 200) < 0);
    assert(gcode_metadata_number("; bed_temperature = M190 S60\n", "bed_temperature", 200) < 0);
    assert(gcode_metadata_number("; bed_temperature = 60\n", "bed_temperature", 200) == 60);
    assert(gcode_metadata_number(" ; BED_TEMPERATURE: 65,70\r\n", "bed_temperature", 200) == 65);
    assert(gcode_metadata_number("; bed_temperature = nan\n", "bed_temperature", 200) < 0);
    assert(gcode_metadata_number("; bed_temperature = 201\n", "bed_temperature", 200) < 0);
    assert(gcode_metadata_number("; bed_temperature = -60\n", "bed_temperature", 200) < 0);
    assert(gcode_metadata_number("; nozzle_temperature = 220\n", "nozzle_temperature", 500) == 220);
    assert(gcode_metadata_number("; nozzle_temperature_initial_layer = 225\n", "nozzle_temperature_initial_layer", 500) == 225);
    assert(fabs(gcode_metadata_grams("; filament used [g] = 0, 21.2, 3.1, 0\n") - 24.3) < 1e-8);
    assert(gcode_metadata_grams("; filament weight = 31.3g\n") == 31.3);
    assert(gcode_metadata_grams("; filament weight [g] = 0; 8.5\n") == 8.5);
    assert(gcode_metadata_grams("; filament weight (g) = 4.5\n") == 4.5);
    assert(gcode_metadata_grams("; total filament weight [g]: 5\n") == 5);
    assert(gcode_metadata_grams("; filament used [g] = 0.00, 21.24\n") == 21.24);
    assert(gcode_metadata_grams("; total filament used [g] = 21.24\n") == 21.24);
    assert(gcode_metadata_number("; machine_start_gcode = M140 S[bed_temperature_initial_layer_single]\\nM190 S[bed_temperature_initial_layer_single] A\n", "bed_temperature", 200) < 0);
    assert(gcode_metadata_grams("; filament_used_g = 0\n") == 0);
    assert(gcode_metadata_grams("; filament used [mm] = 1000\n") < 0);
    assert(gcode_metadata_grams("; filament weight_formula = 31.3\n") < 0);
    assert(gcode_metadata_grams("; filament weight = 0, nan\n") < 0);
    assert(gcode_metadata_grams("; filament weight = 0, -3\n") < 0);
    assert(gcode_metadata_grams("; filament weight = 0,\n") < 0);
    assert(gcode_metadata_grams("; filament weight = 0, unknown\n") < 0);
    puts("PASS: exact slicer keys, bounded temperatures and complete filament mass totals");
    return 0;
}
