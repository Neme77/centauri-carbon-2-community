#include "../src/control.h"
#include "../src/screw_history.h"
#include <assert.h>

/* The screw history keeps why each tramming session ended, so it can be seen whether sessions
 * really break off (telemetry, timeout, homing) before anything is changed about them. */
static int failures;
static void expect(int ok, const char *what) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}
static char *json_of_file(const char *text) {
    FILE *file = fopen("screw-history.txt", "w"); assert(file); fputs(text, file); fclose(file);
    static screw_history_store h; /* big: not on the stack (the single probes made the store larger) */
    assert(screw_history_load(&h, "screw-history.txt"));
    return screw_history_json(&h, NULL);
}
/* The session object that starts at `started` carries `reason` ("\"timeout\"" or "null"). */
static int session_reason(const char *json, long long started, const char *reason) {
    char key[64], want[64];
    snprintf(key, sizeof(key), "\"started\":%lld,", started);
    snprintf(want, sizeof(want), "\"end_reason\":%s,\"points\":", reason);
    const char *at = json ? strstr(json, key) : NULL;
    if (!at) return 0;
    const char *next = strstr(at, "{\"kind\":");
    const char *found = strstr(at, want);
    return found && (!next || found < next);
}
static screw_history_store back_store; /* the history read back from the file, big: not on the stack */
static const char output[] = "// probe at 27.900,25.950 is z=1\n// Result is z=-0.100000\n";

int main(void) {
    char dir[] = "screw-end-XXXXXX"; /* the harnesses share one working directory */
    assert(mkdtemp(dir) && !chdir(dir));

    /* --- The stored reason is read back and reported; older files have none. --- */
    char *json = json_of_file("cc2-screw-history 1\n"
                              "S 1762800000 1762800100 0 timeout\nM 1762800050 0 -1 0 1 0 -0.1 0 0 0 0 0 0 0\n"
                              "S 1762800200 1762800300 0\nM 1762800250 0 -1 0 1 0 -0.1 0 0 0 0 0 0 0\n"
                              "S 1762800400 1762800500 1 user\nM 1762800500 3 -1 0 1 0 -0.1 0 0 0 0 0 0 0\n"
                              "S 1762800600 1762800700 0 no_such_reason\nM 1762800650 0 -1 0 1 0 -0.1 0 0 0 0 0 0 0\n");
    expect(session_reason(json, 1762800000, "\"timeout\""),
           "a stored timeout is reported as end_reason");
    expect(session_reason(json, 1762800200, "null"),
           "a session from a file without reasons reports null");
    expect(session_reason(json, 1762800600, "null") && session_reason(json, 1762800400, "\"user\""),
           "an unknown stored reason is reported as null, the entry is kept; a quick entry keeps its reason field");
    free(json);
    json = json_of_file("cc2-screw-history 1\nS 1762800000 0 0\nM 1762800050 0 -1 0 1 0 -0.1 0 0 0 0 0 0 0\n");
    expect(json && strstr(json, "\"ended\":1762800050,") && session_reason(json, 1762800000, "\"restart\""),
           "a session left open by a restart is closed with the reason restart");
    free(json);

    /* --- Every way a session ends is recorded with its own reason. --- */
    struct { const char *name; int reason; } cases[] = {
        {"user", CONTROL_SCREWS_END_USER}, {"emergency_stop", CONTROL_SCREWS_END_EMERGENCY},
        {"disconnected", CONTROL_SCREWS_END_DISCONNECT}, {"print_started", CONTROL_SCREWS_END_PRINT},
        {"telemetry_stale", CONTROL_SCREWS_END_TELEMETRY}, {"console_error", CONTROL_SCREWS_END_CONSOLE},
        {"machine_state", CONTROL_SCREWS_END_STATE}, {"timeout", CONTROL_SCREWS_END_TIMEOUT},
        {"xy_homing_lost", CONTROL_SCREWS_END_XY}, {"quick_measurement", CONTROL_SCREWS_END_MEASURE},
        {"heaters_off", CONTROL_SCREWS_END_HEATERS_OFF}, {"new_session", CONTROL_SCREWS_END_NEW_SESSION},
        {"restart", CONTROL_SCREWS_END_RESTART}, {"mesh_calibration", CONTROL_SCREWS_END_MESH}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        char what[96]; snprintf(what, sizeof(what), "reason %s has its stored name", cases[i].name);
        expect(control_screws_end_name(cases[i].reason) && !strcmp(control_screws_end_name(cases[i].reason), cases[i].name) &&
               control_screws_end_from_name(cases[i].name) == cases[i].reason, what);
    }
    expect(!control_screws_end_name(CONTROL_SCREWS_END_NONE) && !control_screws_end_name(CONTROL_SCREWS_END_COUNT) &&
           control_screws_end_from_name("bogus") == CONTROL_SCREWS_END_NONE, "no reason and unknown names map to none");

    char script[4096], reason[256];
    time_t t0 = time(NULL);
    long long session_started = 0; /* the start the session itself recorded (control.c reads its own clock; never assume it equals t0) */
    mqtt_client m = {0};
    /* Starts a session whose start job completed, so it is open in `h`. */
#define START(h) do { \
        memset(&m, 0, sizeof(m)); m.connected = m.registered = 1; m.have_machine_status = 1; m.machine_status = 1; \
        m.last_message = t0; m.have_position = 1; strcpy(m.homed_axes, "xyz"); \
        control_screws_heaters_off_sent(); \
        assert(control_build_script("screws:tram:start", &m, script, sizeof(script), reason, sizeof(reason))); \
        control_screws_note_action("screws:tram:start"); control_screws_note_job(++generation); \
        (void)control_screws_tick(&m, 1, 1, generation, 0, t0); \
        control_screws_job job; assert(control_screws_take_finished(&job)); session_started = (long long)job.started; \
        memset(&(h), 0, sizeof(h)); assert(screw_history_record(&(h), &job, output, strlen(output), t0)); \
        assert(control_screws_session_active() && control_screws_end_reason() == CONTROL_SCREWS_END_NONE); \
    } while (0)
    /* Closes the history entry the way the main loop does and checks the stored reason. */
#define ENDED_WITH(h, name, what) do { \
        assert(!control_screws_session_active()); \
        assert(screw_history_tick(&(h), control_screws_session_active(), t0 + 5, control_screws_end_reason())); \
        assert(screw_history_save(&(h), "screw-history.txt")); \
        assert(screw_history_load(&back_store, "screw-history.txt")); \
        char *text = screw_history_json(&back_store, NULL); \
        expect(text && strstr(text, "\"end_reason\":\"" name "\""), what); free(text); \
    } while (0)
    unsigned long generation = 0;
    static screw_history_store h;

    START(h);
    assert(control_build_script("screws:tram:end", &m, script, sizeof(script), reason, sizeof(reason)));
    ENDED_WITH(h, "user", "screws:tram:end is stored as user");
    /* The idle limit does not run while a session job (waiting for the bed, probing) is running: it counts from the end of
     * the job. A job longer than the limit must not end its own session. */
    START(h);
    assert(control_build_script("screws:tram:check", &m, script, sizeof(script), reason, sizeof(reason)));
    control_screws_note_action("screws:tram:check"); control_screws_note_job(++generation);
    {
        time_t late = t0 + SCREWS_SESSION_TIMEOUT_S + 120;
        m.last_message = late; m.machine_status = 10; /* the printer reports Manual homing during its own job */
        (void)control_screws_tick(&m, 0, 0, 0, 0, late);
        expect(control_screws_session_active(), "a running job longer than the idle limit does not end the session");
        (void)control_screws_tick(&m, 1, 1, generation, 0, late);
        m.machine_status = 1;
        time_t before = late + SCREWS_SESSION_TIMEOUT_S, after = late + SCREWS_SESSION_TIMEOUT_S + 2;
        m.last_message = before; (void)control_screws_tick(&m, 0, 0, 0, 0, before);
        expect(control_screws_session_active(), "the limit counts from the end of the job: not yet ended exactly at it");
        m.last_message = after; (void)control_screws_tick(&m, 0, 0, 0, 0, after);
        expect(!control_screws_session_active() && control_screws_end_reason() == CONTROL_SCREWS_END_TIMEOUT, "and ended just after it");
        control_screws_job done; (void)control_screws_take_finished(&done);
    }
    START(h);
    (void)control_screws_tick(&m, 0, 0, 0, 1, t0 + 1);
    ENDED_WITH(h, "emergency_stop", "the emergency stop is stored as emergency_stop");
    START(h);
    assert(control_build_script("system:emergency_stop", &m, script, sizeof(script), reason, sizeof(reason)));
    ENDED_WITH(h, "emergency_stop", "the queued emergency stop action is stored as emergency_stop");
    START(h);
    m.connected = 0; (void)control_screws_tick(&m, 0, 0, 0, 0, t0 + 1);
    ENDED_WITH(h, "disconnected", "a lost printer connection is stored as disconnected");
    START(h);
    m.machine_status = 2; strcpy(m.print_state, "printing"); (void)control_screws_tick(&m, 0, 0, 0, 0, t0 + 1);
    ENDED_WITH(h, "print_started", "a print start is stored as print_started");
    START(h);
    (void)control_screws_tick(&m, 0, 0, 0, 0, t0 + 16);
    ENDED_WITH(h, "telemetry_stale", "telemetry older than 15 s is stored as telemetry_stale");
    START(h);
    (void)control_screws_tick(&m, 0, 0, 0, 0, t0 + 16);
    m.last_message = time(NULL); m.machine_status = 1;
    assert(control_build_script("heaters:off", &m, script, sizeof(script), reason, sizeof(reason)));
    ENDED_WITH(h, "telemetry_stale", "a later action without a session does not overwrite the reason");
    START(h);
    assert(control_build_script("screws:tram:probe:FR", &m, script, sizeof(script), reason, sizeof(reason)));
    control_screws_note_action("screws:tram:probe:FR"); control_screws_note_job(++generation);
    (void)control_screws_tick(&m, 1, 0, generation, 0, t0 + 1);
    ENDED_WITH(h, "console_error", "a failed session job is stored as console_error");
    /* R11/R23: a measurement the user cancelled leaves the session running, without a result; only a cancelled start
     * ends the session, and then by the user. The note does not outlive its job. */
    START(h);
    assert(control_build_script("screws:tram:probe:FR", &m, script, sizeof(script), reason, sizeof(reason)));
    {
        const char *kept = control_screws_cancel_tail("screws:tram:probe:FR");
        expect(kept && strstr(kept, "G1 Z10 F600\nRESTORE_GCODE_STATE NAME=CC2_SCREW_TRAM\nM104 S0\nM140 S60") && !strstr(kept, "M140 S0"),
               "a cancelled measurement of a session lifts the nozzle and keeps the session's bed target");
    }
    control_screws_note_action("screws:tram:probe:FR"); control_screws_note_job(++generation);
    control_screws_note_cancel();
    (void)control_screws_tick(&m, 1, 0, generation, 0, t0 + 1);
    expect(control_screws_session_active() && !control_screws_measuring() && !control_screws_heaters_off_pending(),
           "a cancelled measurement leaves the session running");
    expect(!control_screws_take_finished(NULL), "a cancelled measurement is not recorded");
    assert(control_build_script("screws:tram:probe:FR", &m, script, sizeof(script), reason, sizeof(reason)));
    control_screws_note_action("screws:tram:probe:FR"); control_screws_note_job(++generation);
    (void)control_screws_tick(&m, 1, 0, generation, 0, t0 + 2);
    ENDED_WITH(h, "console_error", "the cancel note does not outlive its job: the next failure is a console error");
    memset(&m, 0, sizeof(m)); m.connected = m.registered = 1; m.have_machine_status = 1; m.machine_status = 1;
    m.last_message = t0; m.have_position = 1; strcpy(m.homed_axes, "xyz");
    control_screws_heaters_off_sent();
    assert(control_build_script("screws:tram:start", &m, script, sizeof(script), reason, sizeof(reason)));
    {
        const char *start = control_screws_cancel_tail("screws:tram:start");
        expect(start && strstr(start, "RESTORE_GCODE_STATE NAME=CC2_SCREW_TRAM\nM104 S0\nM140 S0"),
               "a cancelled start of a session switches both heaters off");
    }
    control_screws_note_action("screws:tram:start"); control_screws_note_job(++generation);
    control_screws_note_cancel();
    (void)control_screws_tick(&m, 1, 0, generation, 0, t0);
    expect(!control_screws_session_active() && control_screws_end_reason() == CONTROL_SCREWS_END_USER &&
           control_screws_heaters_off_pending() && !control_screws_take_finished(NULL),
           "a cancelled start ends the session by the user, heaters off, nothing recorded");
    {
        const char *quick = control_screws_cancel_tail("screws:measure:60:2"), *corner = control_screws_cancel_tail("screws:measure:corner:FR");
        expect(quick && corner && strstr(quick, "RESTORE_GCODE_STATE NAME=CC2_SCREW_MEASURE") && !strcmp(quick, corner),
               "a quick measurement is cancelled with the restore of its own saved state");
        expect(quick && strstr(quick, "G1 Z10 F600\n") && strstr(quick, "M104 S0\nM140 S0"),
               "a cancelled quick measurement lifts the nozzle and switches both heaters off");
        expect(!control_screws_cancel_tail("screws:tram:end") && !control_screws_cancel_tail("heaters:off") && !control_screws_cancel_tail(NULL),
               "the end of a session and other actions are not staged");
    }
    START(h);
    control_screws_invalidate_for(CONTROL_SCREWS_END_CONSOLE); /* the start that could not be queued, main.c */
    ENDED_WITH(h, "console_error", "a start that cannot be queued is stored as console_error");
    START(h);
    m.machine_status = 9; (void)control_screws_tick(&m, 0, 0, 0, 0, t0 + 1);
    ENDED_WITH(h, "machine_state", "another machine state is stored as machine_state");
    START(h);
    m.have_machine_status = 0; (void)control_screws_tick(&m, 0, 0, 0, 0, t0 + 1);
    ENDED_WITH(h, "machine_state", "a missing machine state is stored as machine_state");
    START(h);
    m.last_message = t0 + SCREWS_SESSION_TIMEOUT_S + 1;
    (void)control_screws_tick(&m, 0, 0, 0, 0, t0 + SCREWS_SESSION_TIMEOUT_S + 1);
    ENDED_WITH(h, "timeout", "20 minutes without an action are stored as timeout");
    START(h);
    m.last_message = t0 + 20; (void)control_screws_tick(&m, 0, 0, 0, 0, t0 + 20); /* past the own-job grace period */
    expect(control_screws_session_active(), "the session is still open after its own job's grace period");
    strcpy(m.homed_axes, "z"); (void)control_screws_tick(&m, 0, 0, 0, 0, t0 + 20);
    ENDED_WITH(h, "xy_homing_lost", "lost X/Y homing is stored as xy_homing_lost");
    START(h);
    /* The service no longer lets a quick measurement end a session (it refuses it); the stored name stays valid for
     * history written by earlier versions. */
    expect(!control_build_script("screws:measure", &m, script, sizeof(script), reason, sizeof(reason)) && control_screws_session_active(),
           "a quick measurement is refused during a session and does not end it");
    expect(!control_build_script("screws:tram:start", &m, script, sizeof(script), reason, sizeof(reason)) && control_screws_session_active(),
           "a second start is refused during a session and does not end it");
    control_screws_invalidate_for(CONTROL_SCREWS_END_MEASURE);
    ENDED_WITH(h, "quick_measurement", "an old quick_measurement end is still stored by name");
    START(h);
    assert(control_build_script("heaters:off", &m, script, sizeof(script), reason, sizeof(reason)));
    ENDED_WITH(h, "heaters_off", "heaters off is stored as heaters_off");

    /* A new start replaces the open session; the history closes it as new_session. */
    START(h);
    control_screws_job job = {CONTROL_SCREWS_JOB_START, -1, 0, t0 + 50, 0, 0};
    assert(screw_history_record(&h, &job, output, strlen(output), t0 + 50));
    char *text = screw_history_json(&h, NULL);
    expect(session_reason(text, session_started, "\"new_session\"") && session_reason(text, (long long)t0 + 50, "null"),
           "a new start closes the earlier session as new_session; the new one is open without a reason");
    free(text);
    /* A one-shot measurement recorded while a session is open closes it as quick_measurement. */
    job = (control_screws_job){CONTROL_SCREWS_JOB_QUICK, -1, 0, t0 + 60, 0, 0};
    assert(screw_history_record(&h, &job, output, strlen(output), t0 + 60));
    text = screw_history_json(&h, NULL);
    expect(text && strstr(text, "\"started\":1") && strstr(text, "\"kind\":\"quick\"") &&
           strstr(text, "\"end_reason\":\"quick_measurement\""), "a recorded quick entry closes the open session as quick_measurement");
    free(text);

    /* Earlier versions read the first three fields of an S line and ignore the rest. */
    FILE *file = fopen("screw-history.txt", "r"); char line[256] = ""; int found = 0;
    long long a, b; int q;
    while (file && fgets(line, sizeof(line), file))
        if (line[0] == 'S' && strstr(line, " new_session") && sscanf(line, "S %lld %lld %d", &a, &b, &q) == 3) found = 1;
    if (file) fclose(file);
    assert(screw_history_save(&h, "screw-history.txt"));
    file = fopen("screw-history.txt", "r"); found = 0;
    while (file && fgets(line, sizeof(line), file))
        if (line[0] == 'S' && strstr(line, " new_session c\n") && sscanf(line, "S %lld %lld %d", &a, &b, &q) == 3) found = 1;
    if (file) fclose(file);
    expect(found, "the reason is a fourth field after the three an earlier version reads; the marker of the measuring points follows it");

    /* Where the corners were probed. A session recorded now says "screws" (the screw centres) and keeps saying so after
     * a save and a reload; a session of an older file, which has no fifth field, says "old" and is never upgraded. A
     * session without a reason writes "-" as its fourth field so that the marker can follow. */
    {
        screw_history_store again; memset(&again, 0, sizeof(again));
        expect(screw_history_load(&again, "screw-history.txt") && again.count == h.count, "the saved history loads again");
        char *now = screw_history_json(&again, NULL);
        expect(now && strstr(now, "\"points\":\"screws\"") && !strstr(now, "\"points\":\"old\""), "sessions recorded now are marked as probed at the screw centres, also after a reload");
        free(now);
        char *old = json_of_file("cc2-screw-history 1\nS 1762800000 1762800100 0 timeout\nM 1762800050 0 -1 0 1 0 -0.1 0 0 0 0 0 0 0\n"
                                 "S 1762800200 1762800300 0\nM 1762800250 0 -1 0 1 0 -0.1 0 0 0 0 0 0 0\n"
                                 "S 1762800400 1762800500 0 - c\nM 1762800450 0 -1 0 1 0 -0.1 0 0 0 0 0 0 0\n"
                                 "S 1762800600 1762800700 0 user c\nM 1762800650 0 -1 0 1 0 -0.1 0 0 0 0 0 0 0\n");
        const char *first = old ? strstr(old, "\"started\":1762800000,") : NULL, *second = old ? strstr(old, "\"started\":1762800200,") : NULL;
        const char *third = old ? strstr(old, "\"started\":1762800400,") : NULL, *fourth = old ? strstr(old, "\"started\":1762800600,") : NULL;
        expect(first && second && third && fourth, "all four stored sessions are reported");
        if (first && second && third && fourth) {
            const char *order[5] = {first, second, third, fourth, old + strlen(old)};
            const char *want[4] = {"\"points\":\"old\"", "\"points\":\"old\"", "\"end_reason\":null,\"points\":\"screws\"", "\"end_reason\":\"user\",\"points\":\"screws\""};
            /* The sessions are listed newest first or oldest first; each one's text ends where the next in the text begins. */
            for (int i = 0; i < 4; i++) {
                const char *end = old + strlen(old);
                for (int k = 0; k < 4; k++) if (order[k] > order[i] && order[k] < end) end = order[k];
                const char *hit = strstr(order[i], want[i]);
                expect(hit && hit < end, i < 2 ? "a session of an older file keeps the earlier measuring points" : "a stored marker is read back, with and without a reason");
            }
        }
        free(old);
    }

    if (failures) { printf("%d FAIL\n", failures); return 1; }
    puts("PASS screw end reason: every session end stored, read back and reported");
    return 0;
}
