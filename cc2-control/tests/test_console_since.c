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

/* The query a client sends: the generation and the stored length it holds, and the instance the last reply named. */
static void held(const console_state *state, unsigned long generation, size_t length, char *query, size_t capacity) {
    snprintf(query, capacity, "since=%lu:%zu:%s", generation, length, state->instance);
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
    char query[96], expected[256];
    held(&state, 7, 8, query, sizeof(query));
    reply(&state, query, body);
    snprintf(expected, sizeof(expected), "{\"busy\":false,\"completed\":true,\"success\":true,\"generation\":7,\"command\":\"M105\",\"unchanged\":true,\"output_len\":8,\"instance\":\"%s\"}\n", state.instance);
    assert(!strcmp(body, expected));
    assert(!strstr(body, "\"output\":"));
    console_destroy(&state);
    free(body);
}

static void test_new_output_sends_everything(void) {
    console_state state;
    char *body = malloc(BODY_MAX);
    setup(&state, 0, 7, "M105", "ok T:21\nok T:22\n");
    char query[96], expected[256];
    held(&state, 7, 8, query, sizeof(query));
    reply(&state, query, body);
    assert(!strstr(body, "unchanged"));
    snprintf(expected, sizeof(expected), "{\"busy\":false,\"completed\":true,\"success\":true,\"generation\":7,\"command\":\"M105\",\"output\":\"ok T:21\\nok T:22\\n\",\"output_len\":16,\"instance\":\"%s\"}\n", state.instance);
    assert(!strcmp(body, expected));
    console_destroy(&state);
    free(body);
}

static void test_running_command_always_sends_output(void) {
    console_state state;
    char *body = malloc(BODY_MAX);
    setup(&state, 1, 7, "G28", "homing\n");
    char query[96];
    held(&state, 7, 7, query, sizeof(query));
    reply(&state, query, body);
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
    char query[96];
    held(&state, 7, 8, query, sizeof(query));
    reply(&state, query, body);
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
    char query[96];
    held(&state, 7, 8, query, sizeof(query));
    reply(&state, query, body);
    assert(!strstr(body, "unchanged"));
    assert(strstr(body, "\"output\":\"\""));
    held(&state, 8, 0, query, sizeof(query));
    reply(&state, query, body);
    assert(strstr(body, "\"unchanged\":true"));
    console_destroy(&state);
    free(body);
}

static void test_instance_names_a_start_of_the_service(void) {
    console_state a, b;
    console_init(&a, "/tmp/not-used");
    console_init(&b, "/tmp/not-used");
    assert(strlen(a.instance) == 16 && strlen(b.instance) == 16);
    for (int i = 0; i < 16; i++) {
        assert((a.instance[i] >= '0' && a.instance[i] <= '9') || (a.instance[i] >= 'a' && a.instance[i] <= 'f'));
    }
    assert(strcmp(a.instance, b.instance) != 0); /* two starts of the service differ */
    console_destroy(&a);
    console_destroy(&b);
}

static void test_missing_or_other_instance_gets_the_full_reply(void) {
    /* A client that holds generation 7 and length 8 of one instance asks again after the service restarted: the new
     * instance has reached the same two numbers with other text, and must answer in full. */
    console_state before, after;
    char *body = malloc(BODY_MAX);
    char query[96];
    setup(&before, 0, 7, "M105", "ok T:21\n");
    setup(&after, 0, 7, "M105", "ok T:99\n");
    assert(before.output_len == after.output_len && strcmp(before.instance, after.instance) != 0);
    held(&before, 7, before.output_len, query, sizeof(query));
    reply(&before, query, body);
    assert(strstr(body, "\"unchanged\":true"));
    reply(&after, query, body);
    assert(!strstr(body, "unchanged"));
    assert(strstr(body, "\"output\":\"ok T:99\\n\"") && strstr(body, "\"output_len\":8"));
    char named[64];
    snprintf(named, sizeof(named), "\"instance\":\"%s\"", after.instance);
    assert(strstr(body, named)); /* the client learns the new instance from the full reply */
    /* A client that names no instance (an older web interface) gets the full reply as well, with the instance in it. */
    reply(&after, "since=7:8", body);
    assert(!strstr(body, "unchanged") && strstr(body, "\"output\":\"ok T:99\\n\"") && strstr(body, named));
    /* A request without since carries neither output_len nor instance, as before. */
    reply(&after, NULL, body);
    assert(!strstr(body, "instance") && !strstr(body, "output_len"));
    /* The same instance and the same numbers: still unchanged. */
    held(&after, 7, after.output_len, query, sizeof(query));
    reply(&after, query, body);
    assert(strstr(body, "\"unchanged\":true") && strstr(body, named));
    console_destroy(&before);
    console_destroy(&after);
    free(body);
}

static void test_broken_parameter_behaves_like_none(void) {
    static const char *broken[] = {
        "since", "since=", "since=7", "since=7:", "since=:8", "since=7:8:9", "since=-7:8", "since=7:-8",
        "since=7:8x", "since=x7:8", "since=7 :8", "since=+7:8", "since=0x7:8", "since=7;8", "since=7,8",
        "since=99999999999999999999999999:8", "since=7:99999999999999999999999999",
        "since=&since=7:8", "nosince=7:8", "Since=7:8", "other=1", "&", "=", "since%3D7:8",
        /* the instance must be exactly 16 lowercase hex digits and the last part */
        "since=7:8:", "since=7:8:0123456789abcde", "since=7:8:0123456789abcdef0", "since=7:8:0123456789ABCDEF",
        "since=7:8:0123456789abcdeg", "since=7:8:0123456789abcdef:1", "since=7:8:0123456789abcdef ",
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
    char held_query[96], query[160];
    held(&state, 7, 8, held_query, sizeof(held_query));
    snprintf(query, sizeof(query), "x=1&%s", held_query);
    reply(&state, query, body);
    assert(strstr(body, "\"unchanged\":true"));
    snprintf(query, sizeof(query), "%s&x=1", held_query);
    reply(&state, query, body);
    assert(strstr(body, "\"unchanged\":true"));
    snprintf(query, sizeof(query), "x=1&%s&y=2", held_query);
    reply(&state, query, body);
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
    char query[96];
    held(&state, 3, 11, query, sizeof(query));
    reply(&state, query, body);
    assert(strstr(body, "\"unchanged\":true"));
    assert(strstr(body, "\"output_len\":11"));
    held(&state, 3, 10, query, sizeof(query));
    reply(&state, query, body);
    assert(strstr(body, "\"output\":"));
    assert(strstr(body, "\"output_len\":11"));
    console_destroy(&state);
    free(body);
}

static void test_command_is_escaped_in_both_replies(void) {
    console_state state;
    char *body = malloc(BODY_MAX);
    setup(&state, 0, 2, "M118 \"hi\"\\", "x");
    char query[96];
    held(&state, 2, 1, query, sizeof(query));
    reply(&state, query, body);
    assert(strstr(body, "\"command\":\"M118 \\\"hi\\\"\\\\\""));
    assert(strstr(body, "\"unchanged\":true"));
    held(&state, 2, 0, query, sizeof(query));
    reply(&state, query, body);
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
        char query[96];
        held(&state, 9, 262143, query, sizeof(query));
        reply(&state, query, body);
        assert(strlen(body) < 200 && strstr(body, "\"unchanged\":true") && strstr(body, "\"output_len\":262143"));
        held(&state, 9, 262142, query, sizeof(query));
        reply(&state, query, body);
        assert(strlen(body) > CONSOLE_OUTPUT_MAX && strstr(body, "\"output_len\":262143"));
        assert(strlen(body) < BODY_MAX);
        held(&state, 9, 262144, query, sizeof(query));
        reply(&state, query, body);
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
        char query[96];
        held(&state, 4, last_len, query, sizeof(query));
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
    char query[96];
    held(&state, 4, state.output_len, query, sizeof(query));
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
    test_instance_names_a_start_of_the_service();
    test_missing_or_other_instance_gets_the_full_reply();
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
