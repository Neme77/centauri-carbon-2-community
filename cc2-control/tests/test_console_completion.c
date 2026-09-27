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

int main(void) {
    test_split_result_marker();
    test_result_marker_survives_large_stream();
    test_output_saturation_is_reported();
    puts("console completion tests: PASS");
    return 0;
}
