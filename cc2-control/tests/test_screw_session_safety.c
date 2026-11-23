#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

/* A screw-tramming session ends after 10 minutes without an action, and CC2 Control itself never
 * clears the X/Y homing its re-probes depend on: no automatic M84 while it runs (even with both
 * heater targets at 0 C) and no "Motors off" until it has ended. */
static int failures;
static void expect(int ok, const char *what) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}
static mqtt_client m;
static char script[4096], reason[256];
static void start_session(time_t t0) {
    memset(&m, 0, sizeof(m));
    m.connected = m.registered = 1; m.have_machine_status = 1; m.machine_status = 1;
    m.last_message = t0; m.have_position = 1; strcpy(m.homed_axes, "xyz");
    control_screws_heaters_off_sent();
    assert(control_build_script("screws:tram:start", &m, script, sizeof(script), reason, sizeof(reason)));
    control_screws_note_action("screws:tram:start"); control_screws_note_job(1);
    (void)control_screws_tick(&m, 1, 1, 1, 0, t0);
    assert(control_screws_session_active());
}

int main(void) {
    time_t t0 = time(NULL);

    /* --- 10 minutes without an action end the session. --- */
    start_session(t0);
    m.last_message = t0 + 600;
    expect(!control_screws_tick(&m, 0, 0, 0, 0, t0 + 600) && control_screws_session_active(),
           "600 s after the last action the session is still open");
    m.last_message = t0 + 601;
    expect(control_screws_tick(&m, 0, 0, 0, 0, t0 + 601) && !control_screws_session_active() &&
           control_screws_end_reason() == CONTROL_SCREWS_END_TIMEOUT, "after more than 600 s it ends as timeout");

    /* --- A session job whose end was never seen does not keep the session open for ever. --- */
    /* The console has moved on to another command (its generation is past the job's) while the job still counts
     * as running: the session ends like a failed job, with the heaters switched off. */
    memset(&m, 0, sizeof(m));
    m.connected = m.registered = 1; m.have_machine_status = 1; m.machine_status = 1;
    m.last_message = t0; m.have_position = 1; strcpy(m.homed_axes, "xyz");
    control_screws_heaters_off_sent();
    assert(control_build_script("screws:tram:start", &m, script, sizeof(script), reason, sizeof(reason)));
    control_screws_note_action("screws:tram:start"); control_screws_note_job(7);
    m.last_message = t0 + 5;
    expect(!control_screws_tick(&m, 0, 0, 7, 0, t0 + 5) && control_screws_session_active() && control_screws_measuring(),
           "while the console still runs the session's job the session stays open");
    m.last_message = t0 + 6;
    expect(control_screws_tick(&m, 0, 0, 8, 0, t0 + 6) && !control_screws_session_active() &&
           control_screws_end_reason() == CONTROL_SCREWS_END_CONSOLE && control_screws_heaters_off_pending() &&
           !control_screws_measuring(),
           "a job the console has left behind ends the session as a console error, heaters off pending");
    control_screws_heaters_off_sent();

    /* --- "Motors off" is refused while a session runs, allowed without one. --- */
    start_session(time(NULL));
    expect(!control_build_script("system:motors_off", &m, script, sizeof(script), reason, sizeof(reason)) &&
           strstr(reason, "screw tramming session") && control_screws_session_active(),
           "motors off is refused during a session, with its reason, and the session stays");
    assert(control_build_script("screws:tram:end", &m, script, sizeof(script), reason, sizeof(reason)));
    expect(control_build_script("system:motors_off", &m, script, sizeof(script), reason, sizeof(reason)) &&
           !strcmp(script, "M84"), "without a session motors off sends M84 as before");

    /* --- No automatic M84 during a session, even with both heater targets at 0 C. --- */
    control_idle_motors idle = {0};
    control_idle_sample s = {.valid = 1, .machine_status = 1, .print_state = "", .board_fan = 1.0,
                             .x = 52.5, .y = 264.0, .z = 50.0, .session = 1};
    int sent = 0;
    for (time_t t = 0; t <= 3 * CONTROL_IDLE_MOTORS_SECONDS; t += 30) sent += control_idle_motors_due(&idle, &s, t);
    expect(sent == 0, "a session keeps its steppers for half an hour of idle with heaters off");
    s.session = 0;
    time_t ended = 3 * CONTROL_IDLE_MOTORS_SECONDS + 30;
    expect(!control_idle_motors_due(&idle, &s, ended) && !control_idle_motors_due(&idle, &s, ended + CONTROL_IDLE_MOTORS_SECONDS - 1),
           "after the session the wait starts again from its end");
    expect(control_idle_motors_due(&idle, &s, ended + CONTROL_IDLE_MOTORS_SECONDS), "and M84 follows a full period later, as before");

    /* --- The main loop's sample carries the session. --- */
    console_state console; console_init(&console, "/tmp/unused");
    start_session(time(NULL));
    expect(idle_motors_sample(&m, &console).session == 1, "the idle sample of the main loop marks a running session");
    assert(control_build_script("screws:tram:end", &m, script, sizeof(script), reason, sizeof(reason)));
    expect(idle_motors_sample(&m, &console).session == 0, "and no session once it has ended");
    console_destroy(&console);

    /* --- The session ends after 10 minutes without an action. --- */
    expect(SCREWS_SESSION_TIMEOUT_S == 600, "the screw session ends after 10 minutes");

    if (failures) { printf("%d FAIL\n", failures); return 1; }
    puts("PASS screw session safety: 10-minute limit, no M84 by CC2 Control while a session needs its homing");
    return 0;
}
