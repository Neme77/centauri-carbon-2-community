#define main cc2_main_original
#define console_start mock_console_start
#include "../src/main.c"
#undef main
#undef console_start
#include <assert.h>

/* #329 review: an explicit screws:tram:end keeps the heater shutdown pending until its
 * M104 S0 / M140 S0 is confirmed. A rejected or failed end is retried by the main loop,
 * and a print or heater action that takes the heaters over in between is left alone. */
static int reject_console, reject_sends, sends;
static double nozzle_target, bed_target;
static char queued[700];

int mock_console_start(console_state *state, const char *script, char *reason, size_t cap) {
    if (reject_console) { snprintf(reason, cap, "Another command is running"); return -1; }
    snprintf(queued, sizeof(queued), "%s", script);
    pthread_mutex_lock(&state->lock);
    state->busy = 1; state->completed = 0; state->success = 0; state->generation++;
    pthread_mutex_unlock(&state->lock);
    return 0;
}

/* The printer finishes the queued script; a successful run applies its targets. */
static void finish(console_state *state, int success) {
    if (success && strstr(queued, "M104 S0")) nozzle_target = 0;
    if (success && strstr(queued, "M140 S0")) bed_target = 0;
    pthread_mutex_lock(&state->lock);
    state->busy = 0; state->completed = 1; state->success = success;
    pthread_mutex_unlock(&state->lock);
}

static int fake_send(const char *script) {
    sends++;
    if (reject_sends > 0) { reject_sends--; return -1; }
    if (!strcmp(script, "TURN_OFF_HEATERS")) nozzle_target = bed_target = 0;
    return 0;
}

/* One main-loop pass: the session tick, then the shutdown retry. */
static void step(console_state *state, mqtt_client *mqtt) {
    pthread_mutex_lock(&state->lock);
    int completed = state->completed, success = state->success, busy = state->busy;
    unsigned long generation = state->generation;
    pthread_mutex_unlock(&state->lock);
    (void)control_screws_tick(mqtt, completed, success, generation, 0, time(NULL));
    screws_heaters_off_tick(mqtt, busy, fake_send);
}

static int act(console_state *state, mqtt_client *mqtt, const char *action) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    control_response(pair[0], state, mqtt, action, strlen(action));
    char reply[512]; ssize_t n = read(pair[1], reply, sizeof(reply) - 1);
    close(pair[0]); close(pair[1]);
    assert(n > 0); reply[n] = 0;
    return atoi(strchr(reply, ' ') + 1);
}

/* Starts a session whose job completed, with the session's heaters on. */
static void session(console_state *state, mqtt_client *mqtt) {
    reject_console = 0; reject_sends = 0; sends = 0;
    assert(act(state, mqtt, "screws:tram:start") == 202);
    finish(state, 1);
    nozzle_target = 140; bed_target = 60;
    step(state, mqtt);
    assert(control_screws_session_active() && !control_screws_heaters_off_pending());
}

int main(void) {
    console_state state; console_init(&state, "/tmp/unused");
    mqtt_client mqtt; memset(&mqtt, 0, sizeof(mqtt));
    mqtt.connected = mqtt.registered = mqtt.have_machine_status = 1;
    mqtt.machine_status = 1; mqtt.last_message = time(NULL);

    /* The console rejects the end, then the first retry is rejected too. */
    session(&state, &mqtt);
    reject_console = 1; reject_sends = 1;
    assert(act(&state, &mqtt, "screws:tram:end") == 409);
    assert(!control_screws_session_active() && control_screws_heaters_off_pending());
    step(&state, &mqtt);
    assert(sends == 1 && control_screws_heaters_off_pending() && nozzle_target == 140 && bed_target == 60);
    step(&state, &mqtt);
    assert(sends == 2 && !control_screws_heaters_off_pending() && nozzle_target == 0 && bed_target == 0);
    step(&state, &mqtt);
    assert(sends == 2);

    /* The end is queued but its script fails: nothing is retried while the console is busy,
     * then the shutdown is sent once it is free. */
    session(&state, &mqtt);
    assert(act(&state, &mqtt, "screws:tram:end") == 202 && !strcmp(queued, "M104 S0\nM140 S0"));
    assert(!control_screws_session_active() && control_screws_heaters_off_pending());
    step(&state, &mqtt);
    assert(sends == 0 && control_screws_heaters_off_pending());
    finish(&state, 0);
    step(&state, &mqtt);
    assert(sends == 1 && !control_screws_heaters_off_pending() && nozzle_target == 0 && bed_target == 0);

    /* A successful end confirms the shutdown: no second command. */
    session(&state, &mqtt);
    assert(act(&state, &mqtt, "screws:tram:end") == 202);
    finish(&state, 1);
    step(&state, &mqtt);
    assert(sends == 0 && !control_screws_heaters_off_pending() && nozzle_target == 0 && bed_target == 0);

    /* A print that starts after a rejected end owns the heaters: the shutdown is dropped. */
    session(&state, &mqtt);
    reject_console = 1;
    assert(act(&state, &mqtt, "screws:tram:end") == 409 && control_screws_heaters_off_pending());
    mqtt.machine_status = 2; nozzle_target = 210; bed_target = 60;
    step(&state, &mqtt);
    assert(!control_screws_heaters_off_pending());
    mqtt.machine_status = 1; mqtt.last_message = time(NULL);
    step(&state, &mqtt);
    assert(sends == 0 && nozzle_target == 210 && bed_target == 60);

    /* A heater action queued after a failed end takes the heaters over as well. */
    session(&state, &mqtt);
    assert(act(&state, &mqtt, "screws:tram:end") == 202);
    finish(&state, 0);
    assert(act(&state, &mqtt, "heaters:set:0:70") == 202);
    finish(&state, 1);
    bed_target = 70;
    step(&state, &mqtt);
    assert(sends == 0 && !control_screws_heaters_off_pending() && bed_target == 70);

    console_destroy(&state);
    puts("PASS explicit screw tramming end: shutdown pending until confirmed, retried after rejection or failure, takeovers respected");
    return 0;
}
