#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/console.c"

#define BODY_MAX (CONSOLE_OUTPUT_MAX * 2 + 1024)

static void reply(console_state *state, const char *query, char *body) {
    console_json_query(state, query, body, BODY_MAX);
}

static void setup(console_state *state, int busy, unsigned long generation, const char *command, const char *output) {
    console_init(state, "/tmp/not-used");
    state->busy = busy;
    state->completed = !busy;
    state->success = !busy;
    state->generation = generation;
    snprintf(state->command, sizeof(state->command), "%s", command);
    snprintf(state->output, sizeof(state->output), "%s", output);
    state->output_len = strlen(output);
}

static void test_without_parameter_is_unchanged_from_before(void) {
    console_state state;
    char *body = malloc(BODY_MAX), *old = malloc(BODY_MAX);
    setup(&state, 0, 7, "M105", "ok T:21\n");
    console_json(&state, old, BODY_MAX);
    assert(!strcmp(old, "{\"busy\":false,\"completed\":true,\"success\":true,\"generation\":7,\"command\":\"M105\",\"output\":\"ok T:21\\n\"}\n"));
    reply(&state, NULL, body);
    assert(!strcmp(body, old));
    reply(&state, "", body);
    assert(!strcmp(body, old));
    console_destroy(&state);
    free(body); free(old);
}

static void test_same_state_answers_without_output(void) {
    console_state state;
    char *body = malloc(BODY_MAX);
    setup(&state, 0, 7, "M105", "ok T:21\n");
    reply(&state, "since=7:8", body);
    assert(!strcmp(body, "{\"busy\":false,\"completed\":true,\"success\":true,\"generation\":7,\"command\":\"M105\",\"unchanged\":true,\"output_len\":8}\n"));
    assert(!strstr(body, "\"output\":"));
    console_destroy(&state);
    free(body);
}

static void test_new_output_sends_everything(void) {
    console_state state;
    char *body = malloc(BODY_MAX);
    setup(&state, 0, 7, "M105", "ok T:21\nok T:22\n");
    reply(&state, "since=7:8", body);
    assert(!strstr(body, "unchanged"));
    assert(!strcmp(body, "{\"busy\":false,\"completed\":true,\"success\":true,\"generation\":7,\"command\":\"M105\",\"output\":\"ok T:21\\nok T:22\\n\",\"output_len\":16}\n"));
    console_destroy(&state);
    free(body);
}

static void test_running_command_always_sends_output(void) {
    console_state state;
    char *body = malloc(BODY_MAX);
    setup(&state, 1, 7, "G28", "homing\n");
    reply(&state, "since=7:7", body);
    assert(strstr(body, "\"busy\":true"));
    assert(strstr(body, "\"output\":\"homing\\n\""));
    assert(!strstr(body, "unchanged"));
    console_destroy(&state);
    free(body);
}

static void test_new_generation_with_same_length_sends_output(void) {
    console_state state;
    char *body = malloc(BODY_MAX);
    setup(&state, 0, 8, "M114", "abcdefgh");
    reply(&state, "since=7:8", body);
    assert(!strstr(body, "unchanged"));
    assert(strstr(body, "\"output\":\"abcdefgh\""));
    assert(strstr(body, "\"generation\":8"));
    console_destroy(&state);
    free(body);
}

static void test_clear_is_a_new_generation(void) {
    console_state state;
    char *body = malloc(BODY_MAX);
    setup(&state, 0, 7, "M105", "ok T:21\n");
    assert(console_clear(&state) == 0);
    reply(&state, "since=7:8", body);
    assert(!strstr(body, "unchanged"));
    assert(strstr(body, "\"output\":\"\""));
    reply(&state, "since=8:0", body);
    assert(strstr(body, "\"unchanged\":true"));
    console_destroy(&state);
    free(body);
}

static void test_broken_parameter_behaves_like_none(void) {
    static const char *broken[] = {
        "since", "since=", "since=7", "since=7:", "since=:8", "since=7:8:9", "since=-7:8", "since=7:-8",
        "since=7:8x", "since=x7:8", "since=7 :8", "since=+7:8", "since=0x7:8", "since=7;8", "since=7,8",
        "since=99999999999999999999999999:8", "since=7:99999999999999999999999999",
        "since=&since=7:8", "nosince=7:8", "Since=7:8", "other=1", "&", "=", "since%3D7:8",
    };
    console_state state;
    char *body = malloc(BODY_MAX), *old = malloc(BODY_MAX);
    setup(&state, 0, 7, "M105", "ok T:21\n");
    console_json(&state, old, BODY_MAX);
    for (size_t i = 0; i < sizeof(broken) / sizeof(broken[0]); i++) {
        reply(&state, broken[i], body);
        if (strcmp(body, old)) {
            fprintf(stderr, "query '%s' changed the reply: %s", broken[i], body);
            abort();
        }
    }
    console_destroy(&state);
    free(body); free(old);
}

static void test_parameter_among_others(void) {
    console_state state;
    char *body = malloc(BODY_MAX);
    setup(&state, 0, 7, "M105", "ok T:21\n");
    reply(&state, "x=1&since=7:8", body);
    assert(strstr(body, "\"unchanged\":true"));
    reply(&state, "since=7:8&x=1", body);
    assert(strstr(body, "\"unchanged\":true"));
    reply(&state, "x=1&since=7:8&y=2", body);
    assert(strstr(body, "\"unchanged\":true"));
    console_destroy(&state);
    free(body);
}

static void test_length_counts_stored_bytes_not_json(void) {
    console_state state;
    char *body = malloc(BODY_MAX);
    /* Control characters are dropped from the JSON text but stay in the stored output. */
    setup(&state, 0, 3, "M105", "a\x1b[0mb\n\"q\"\\");
    size_t stored = state.output_len;
    assert(stored == 11);
    reply(&state, "since=3:11", body);
    assert(strstr(body, "\"unchanged\":true"));
    assert(strstr(body, "\"output_len\":11"));
    reply(&state, "since=3:10", body);
    assert(strstr(body, "\"output\":"));
    assert(strstr(body, "\"output_len\":11"));
    console_destroy(&state);
    free(body);
}

static void test_command_is_escaped_in_both_replies(void) {
    console_state state;
    char *body = malloc(BODY_MAX);
    setup(&state, 0, 2, "M118 \"hi\"\\", "x");
    reply(&state, "since=2:1", body);
    assert(strstr(body, "\"command\":\"M118 \\\"hi\\\"\\\\\""));
    assert(strstr(body, "\"unchanged\":true"));
    reply(&state, "since=2:0", body);
    assert(strstr(body, "\"command\":\"M118 \\\"hi\\\"\\\\\""));
    console_destroy(&state);
    free(body);
}

static void test_output_limit(void) {
    console_state state;
    char *body = malloc(BODY_MAX);
    char *full = malloc(CONSOLE_OUTPUT_MAX);
    for (int worst = 0; worst < 2; worst++) {
        memset(full, worst ? '\n' : 'a', CONSOLE_OUTPUT_MAX - 1);
        full[CONSOLE_OUTPUT_MAX - 1] = '\0';
        setup(&state, 0, 9, "M105", full);
        assert(state.output_len == CONSOLE_OUTPUT_MAX - 1);
        console_json(&state, body, BODY_MAX);
        size_t before = strlen(body);
        assert(before < BODY_MAX);
        reply(&state, NULL, body);
        assert(strlen(body) == before);
        reply(&state, "since=9:262143", body);
        assert(strlen(body) < 200 && strstr(body, "\"unchanged\":true") && strstr(body, "\"output_len\":262143"));
        reply(&state, "since=9:262142", body);
        assert(strlen(body) > CONSOLE_OUTPUT_MAX && strstr(body, "\"output_len\":262143"));
        assert(strlen(body) < BODY_MAX);
        reply(&state, "since=9:262144", body);
        assert(!strstr(body, "unchanged"));
        console_destroy(&state);
    }
    free(full);
    free(body);
}

typedef struct { console_state *state; int rounds; } writer_args;

static void *writer(void *opaque) {
    writer_args *args = opaque;
    for (int i = 0; i < args->rounds; i++) append_output(args->state, "x\x1b[1my", 6);
    return NULL;
}

static void test_reply_is_a_consistent_snapshot_while_output_grows(void) {
    console_state state;
    setup(&state, 1, 4, "G28", "");
    writer_args args = { &state, 4000 };
    pthread_t thread;
    assert(pthread_create(&thread, NULL, writer, &args) == 0);
    char *body = malloc(BODY_MAX);
    unsigned long last_len = 0;
    for (int i = 0; i < 400; i++) {
        char query[64];
        snprintf(query, sizeof(query), "since=4:%lu", last_len);
        reply(&state, query, body);
        const char *field = strstr(body, "\"output_len\":");
        assert(field);
        unsigned long len = strtoul(field + strlen("\"output_len\":"), NULL, 10);
        assert(len >= last_len && len % 6 == 0);
        const char *out = strstr(body, "\"output\":\"");
        assert(out);                       /* a running command always sends its output */
        out += strlen("\"output\":\"");
        const char *end = strstr(out, "\",\"output_len\"");
        assert(end);
        assert((unsigned long)(end - out) == len / 6 * 5); /* the escape byte is not shown */
        last_len = len;
    }
    pthread_join(thread, NULL);
    state.busy = 0;
    state.completed = 1;
    state.success = 1;
    char query[64];
    snprintf(query, sizeof(query), "since=4:%zu", state.output_len);
    reply(&state, query, body);
    assert(strstr(body, "\"unchanged\":true"));
    console_destroy(&state);
    free(body);
}

int main(void) {
    test_without_parameter_is_unchanged_from_before();
    test_same_state_answers_without_output();
    test_new_output_sends_everything();
    test_running_command_always_sends_output();
    test_new_generation_with_same_length_sends_output();
    test_clear_is_a_new_generation();
    test_broken_parameter_behaves_like_none();
    test_parameter_among_others();
    test_length_counts_stored_bytes_not_json();
    test_command_is_escaped_in_both_replies();
    test_output_limit();
    test_reply_is_a_consistent_snapshot_while_output_grows();
    puts("PASS console since parameter");
    return 0;
}
