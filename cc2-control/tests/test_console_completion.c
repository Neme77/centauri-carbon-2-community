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

int main(void) {
    test_canvas_nonmotion_before_homing();
    test_split_result_marker();
    test_result_marker_survives_large_stream();
    test_output_saturation_is_reported();
    puts("console completion tests: PASS");
    return 0;
}
