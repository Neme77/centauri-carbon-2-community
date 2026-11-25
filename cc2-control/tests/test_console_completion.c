#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/console.c"

static void test_split_result_marker(void) {
    char window[512] = {0};
    size_t used = 0;
    const char *first = "{\"report\":{}}\003{\"id\"";
    const char *second = ":101,\"result\":{}}";
    assert(!protocol_window_contains_result(window, &used, first, strlen(first)));
    assert(protocol_window_contains_result(window, &used, second, strlen(second)));
}

static void test_result_marker_survives_large_stream(void) {
    char window[512] = {0};
    size_t used = 0;
    char noise[4096];
    memset(noise, 'x', sizeof(noise));
    assert(!protocol_window_contains_result(window, &used, noise, sizeof(noise)));
    const char *result = "{\"id\":101,\"result\":{}}";
    assert(protocol_window_contains_result(window, &used, result, strlen(result)));
}

static void test_output_saturation_is_reported(void) {
    console_state state;
    console_init(&state, "/tmp/not-used");
    char block[4096];
    memset(block, 'a', sizeof(block));
    int saturated = 0;
    while (!saturated) saturated = append_output(&state, block, sizeof(block));
    assert(state.output_len == CONSOLE_OUTPUT_MAX - 1);
    assert(state.output[state.output_len] == '\0');
    console_destroy(&state);
}

static void test_canvas_nonmotion_before_homing(void) {
    console_state state;
    console_init(&state, "/tmp/not-used");
    mqtt_client mqtt = {0};
    mqtt.connected = mqtt.registered = mqtt.have_machine_status = 1;
    mqtt.machine_status = 1;
    mqtt.last_message = time(NULL);
    char reason[256];
    const char *safe[] = {
        "USED_CANVAS_DEV USED_CANVAS=0",
        "used_canvas_dev USED_CANVAS=1",
        "CANVAS_SET_FILAMENT_INFO EXTERN_FILA=1 TYPE=PETG NOZZLE_MAX_TEMP=220",
        "CANVAS_SET_FILAMENT_INFO CHANNEL=2 TYPE=PLA",
        "M105", "HELP", "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=0"
    };
    const char *motion[] = {
        "G1 X10", "G1 E5", "M600 S1", "M600 S2", "G180 S800",
        "CANVAS_LOAD_FILAMENT CHANNEL=0", "CANVAS_UNLOAD_FILAMENT CHANNEL=0",
        "CANVAS_SWITCH_FILAMENT CHANNEL=0", "BED_MESH_CALIBRATE", "UNKNOWN_MACRO"
    };
    for (int xy = 0; xy < 2; xy++) {
        strcpy(mqtt.homed_axes, xy ? "xy" : "");
        for (size_t i = 0; i < sizeof(safe)/sizeof(safe[0]); i++)
            assert(console_command_allowed(&state, safe[i], &mqtt, reason, sizeof(reason)));
        for (size_t i = 0; i < sizeof(motion)/sizeof(motion[0]); i++)
            assert(!console_command_allowed(&state, motion[i], &mqtt, reason, sizeof(reason)));
    }
    assert(!console_command_allowed(&state, "USED_CANVAS_DEV USED_CANVAS=0\nG1 X10", &mqtt, reason, sizeof(reason)));
    assert(!console_command_allowed(&state, "USED_CANVAS_DEV_EXTRA", &mqtt, reason, sizeof(reason)));
    mqtt.machine_status = 2;
    assert(!console_command_allowed(&state, safe[0], &mqtt, reason, sizeof(reason)));
    mqtt.machine_status = 3;
    assert(!console_command_allowed(&state, safe[2], &mqtt, reason, sizeof(reason)));
    mqtt.machine_status = 1;
    mqtt.last_message = time(NULL) - 30;
    assert(!console_command_allowed(&state, safe[0], &mqtt, reason, sizeof(reason)));
    mqtt.last_message = time(NULL);
    mqtt.connected = 0;
    assert(!console_command_allowed(&state, safe[2], &mqtt, reason, sizeof(reason)));
    console_destroy(&state);
}

static void test_non_finite_axis_targets_refused(void) {
    console_state state;
    console_init(&state, "/tmp/not-used");
    mqtt_client mqtt = {0};
    mqtt.connected = mqtt.registered = mqtt.have_machine_status = mqtt.have_position = 1;
    mqtt.machine_status = 1;
    mqtt.last_message = time(NULL);
    mqtt.x = mqtt.y = mqtt.z = 10.0;
    char reason[256];
    const char *refused[] = {
        "G1 Xnan", "G1 X-nan", "G1 Ynan", "G1 Znan", "G0 Xnan F3000",
        "G1 Xinf", "G1 X-inf", "G1 X10 Ynan"
    };
    const char *allowed[] = { "G1 X10", "G1 X10 Y20", "G1 Z5", "G1 E5" };
    const char *homed[] = { "", "xy", "xyz" };
    for (size_t h = 0; h < sizeof(homed)/sizeof(homed[0]); h++) {
        strcpy(mqtt.homed_axes, homed[h]);
        for (size_t i = 0; i < sizeof(refused)/sizeof(refused[0]); i++) {
            if (console_command_allowed(&state, refused[i], &mqtt, reason, sizeof(reason))) {
                fprintf(stderr, "FAIL: \"%s\" (homed_axes=\"%s\") must be refused\n", refused[i], homed[h]);
                assert(0);
            }
            if (!strcmp(homed[h], "xyz") && !strstr(reason, "Invalid ")) {
                fprintf(stderr, "FAIL: \"%s\" refused with \"%s\", expected \"Invalid ... coordinate\"\n", refused[i], reason);
                assert(0);
            }
        }
    }
    strcpy(mqtt.homed_axes, "xyz");
    for (size_t i = 0; i < sizeof(allowed)/sizeof(allowed[0]); i++) {
        if (!console_command_allowed(&state, allowed[i], &mqtt, reason, sizeof(reason))) {
            fprintf(stderr, "FAIL: \"%s\" must stay allowed, refused with \"%s\"\n", allowed[i], reason);
            assert(0);
        }
    }
    console_destroy(&state);
}

static void test_non_finite_axis_targets_refused_relative(void) {
    console_state state;
    console_init(&state, "/tmp/not-used");
    mqtt_client mqtt = {0};
    mqtt.connected = mqtt.registered = mqtt.have_machine_status = mqtt.have_position = 1;
    mqtt.machine_status = 1;
    mqtt.last_message = time(NULL);
    mqtt.x = mqtt.y = mqtt.z = 10.0;
    strcpy(mqtt.homed_axes, "xyz");
    char reason[256];
    /* G91 switches the console to relative moves, where the target is the
       current position plus the requested value. */
    if (!console_command_allowed(&state, "G91", &mqtt, reason, sizeof(reason))) {
        fprintf(stderr, "FAIL: \"G91\" must be allowed, refused with \"%s\"\n", reason);
        assert(0);
    }
    const char *refused[] = {
        "G1 Xnan", "G1 X-nan", "G1 Ynan", "G1 Znan", "G0 Xnan F3000",
        "G1 Xinf", "G1 X-inf", "G1 X10 Ynan"
    };
    const char *allowed[] = { "G1 X10", "G1 X-5 Y5", "G1 Z1", "G1 E5" };
    for (size_t i = 0; i < sizeof(refused)/sizeof(refused[0]); i++) {
        if (console_command_allowed(&state, refused[i], &mqtt, reason, sizeof(reason))) {
            fprintf(stderr, "FAIL: \"%s\" (G91) must be refused\n", refused[i]);
            assert(0);
        }
        if (!strstr(reason, "Invalid ")) {
            fprintf(stderr, "FAIL: \"%s\" (G91) refused with \"%s\", expected \"Invalid ... coordinate\"\n", refused[i], reason);
            assert(0);
        }
    }
    for (size_t i = 0; i < sizeof(allowed)/sizeof(allowed[0]); i++) {
        if (!console_command_allowed(&state, allowed[i], &mqtt, reason, sizeof(reason))) {
            fprintf(stderr, "FAIL: \"%s\" (G91) must stay allowed, refused with \"%s\"\n", allowed[i], reason);
            assert(0);
        }
    }
    console_destroy(&state);
}

/* Other spellings of the same refusal: lower case, the G00/G01 aliases, number
 * forms that strtod accepts (overflow, nan with payload, Infinity), a repeated
 * axis word (only one of them may reach Klipper unchecked) and a word that
 * only appears after a comment. Runs in absolute and in relative mode. */
static void test_non_finite_axis_forms_and_repeats(void) {
    console_state state;
    console_init(&state, "/tmp/not-used");
    mqtt_client mqtt = {0};
    mqtt.connected = mqtt.registered = mqtt.have_machine_status = mqtt.have_position = 1;
    mqtt.machine_status = 1;
    mqtt.last_message = time(NULL);
    mqtt.x = mqtt.y = mqtt.z = 10.0;
    strcpy(mqtt.homed_axes, "xyz");
    char reason[256];
    const char *refused[] = {
        "g1 xnan", "g1 x-nan y5", "G1 xNaN", "G00 Xnan", "G01 Ynan", "g01 znan",
        "G1 X1e999", "G1 X-1e999", "G1 Y1e999", "G1 Z-1e999",
        "G1 Xnan(abc)", "G1 XInfinity", "G1 X-Infinity", "G1 Yinfinity",
        "G1 X10 Xnan", "G1 Xnan X10", "G1 X10 Xinf", "G1 X10 X1000", "G1 X1000 X10",
        "G1 Y5 Y1000", "g1 z1 z300", "G0 X5 Y5 X6", "G1 X5 Y5 Y6", "G1 X5 X5"
    };
    const char *allowed[] = {
        "G1 X10 ;Ynan", "G1 X10 ; Xnan", "G1 X10 ;X1000", "g1 x10 y20", "G01 X10", "G00 Y5",
        "G1 X10 Y20 Z5 E1 F3000"
    };
    for (int mode = 0; mode < 2; mode++) {
        if (mode && !console_command_allowed(&state, "G91", &mqtt, reason, sizeof(reason))) {
            fprintf(stderr, "FAIL: \"G91\" must be allowed, refused with \"%s\"\n", reason);
            assert(0);
        }
        for (size_t i = 0; i < sizeof(refused)/sizeof(refused[0]); i++) {
            /* relative mode: 10 + 1000 and 10 + 1e999 are out of range as well, but "X5 X5" etc. are in range */
            if (console_command_allowed(&state, refused[i], &mqtt, reason, sizeof(reason))) {
                fprintf(stderr, "FAIL: \"%s\" (%s) must be refused\n", refused[i], mode ? "G91" : "G90");
                assert(0);
            }
            if (!strstr(reason, "coordinate")) {
                fprintf(stderr, "FAIL: \"%s\" (%s) refused with \"%s\", expected a coordinate message\n",
                        refused[i], mode ? "G91" : "G90", reason);
                assert(0);
            }
        }
        for (size_t i = 0; i < sizeof(allowed)/sizeof(allowed[0]); i++) {
            const char *command = allowed[i];
            /* relative mode: 10 and 20 are steps from 10, still inside the range */
            if (!console_command_allowed(&state, command, &mqtt, reason, sizeof(reason))) {
                fprintf(stderr, "FAIL: \"%s\" (%s) must stay allowed, refused with \"%s\"\n",
                        command, mode ? "G91" : "G90", reason);
                assert(0);
            }
        }
    }
    console_destroy(&state);
}

int main(void) {
    test_canvas_nonmotion_before_homing();
    test_non_finite_axis_targets_refused();
    test_non_finite_axis_targets_refused_relative();
    test_non_finite_axis_forms_and_repeats();
    test_split_result_marker();
    test_result_marker_survives_large_stream();
    test_output_saturation_is_reported();
    puts("console completion tests: PASS");
    return 0;
}
