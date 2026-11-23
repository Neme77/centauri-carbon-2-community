/* The maintenance lock of this test, not the printer's. */
static char soak_test_lock[64];
#define CC2_MAINTENANCE_LOCK soak_test_lock
#define main cc2_main_original
#define console_start mock_console_start
#include "../src/main.c"
#undef main
#undef console_start
#include <assert.h>

/* A hold time before a screw measurement (screws:soak:<minutes>:<measurement>) and the measurement that follows a bed
 * mesh directly. The hold time sets the bed through the console (M140), waits until telemetry reads the bed at its
 * target, holds it with the console free and then starts the measurement exactly as /api/control starts it; every check
 * of the measurement runs when it is accepted and again when it starts. Right after a bed mesh of this service, at the
 * same bed target and with every axis homed, the four-corner measurement and the session start leave out the nozzle
 * cleaning (G180 S3) and the full G28, and a hold time is skipped. Time and telemetry are set by the test. */
static int failures;
#define expect(ok, what) expect_((ok) ? 1 : 0, (what))
static void expect_(int ok, const char *what) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}
static console_state state;
static mqtt_client mqtt;
static int reject_console;
static char queued[700], reply[16384];
static int sends;
static char sent[16][32];

int mock_console_start(console_state *s, const char *script, char *reason, size_t cap) {
    pthread_mutex_lock(&s->lock);
    if (reject_console || s->busy) {
        s->staged_next = 0; pthread_mutex_unlock(&s->lock);
        snprintf(reason, cap, "Another command is running"); return -1;
    }
    snprintf(queued, sizeof(queued), "%s", script);
    snprintf(s->command, sizeof(s->command), "%s", script);
    s->busy = 1; s->completed = 0; s->success = 0; s->generation++;
    s->staged = s->staged_next; s->staged_next = 0;
    snprintf(s->cancel_tail, sizeof(s->cancel_tail), "%s", s->cancel_tail_next);
    pthread_mutex_unlock(&s->lock);
    return 0;
}
static void finish(int success) {
    pthread_mutex_lock(&state.lock);
    state.busy = 0; state.completed = 1; state.success = success;
    pthread_mutex_unlock(&state.lock);
}
static int fake_send(const char *script) {
    if (sends < 16) snprintf(sent[sends], sizeof(sent[0]), "%s", script);
    sends++;
    return 0;
}
static int failing_send(const char *script) { (void)script; sends++; return -1; }
static int starts(const char *text, const char *prefix) { return !strncmp(text, prefix, strlen(prefix)); }
static int sent_count(const char *script) {
    int n = 0;
    for (int i = 0; i < sends && i < 16; i++) if (!strcmp(sent[i], script)) n++;
    return n;
}
static void fresh(void) {
    mqtt.connected = mqtt.registered = 1; mqtt.have_machine_status = 1; mqtt.machine_status = 1;
    mqtt.last_message = time(NULL);
    telemetry.fd = 0; telemetry.ready = 1; clock_gettime(CLOCK_MONOTONIC, &telemetry.last_rx);
}
static void bed(double now, double target) {
    telemetry.values[U_BT] = now; telemetry.values[U_BG] = target;
    telemetry.present |= (UINT32_C(1) << U_BT) | (UINT32_C(1) << U_BG);
    clock_gettime(CLOCK_MONOTONIC, &telemetry.last_rx);
}
static int read_reply(int pair[2]) {
    close(pair[0]);
    size_t used = 0; ssize_t got;
    while (used + 1 < sizeof(reply) && (got = recv(pair[1], reply + used, sizeof(reply) - 1 - used, 0)) > 0) used += (size_t)got;
    reply[used] = 0; close(pair[1]);
    return atoi(strchr(reply, ' ') + 1);
}
static int act(const char *action) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    queued[0] = 0;
    control_response(pair[0], &state, &mqtt, action, strlen(action));
    return read_reply(pair);
}
static int calibrate(const char *body) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    plates_calibrate_response(pair[0], &mqtt, &state, body, strlen(body));
    return read_reply(pair);
}
static const char *printer(void) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    printer_response(pair[0], &mqtt);
    assert(read_reply(pair) == 200);
    return reply;
}
static void tick(void) { screws_soak_tick(&mqtt, &state, 0); }
/* The completed console job also ends the quick measurement or the session job it was. */
static void done(int success) {
    finish(success);
    (void)control_screws_tick(&mqtt, 1, success, state.generation, 0, time(NULL));
}
/* The script `action` builds now, without any effect and without the direct variant. */
static const char *expected(const char *action) {
    static char script[700]; char why[256];
    control_screws_after_mesh(-1);
    assert(control_check_script(action, &mqtt, script, sizeof(script), why, sizeof(why)));
    return script;
}
/* A soak that has just been accepted, whose M140 has completed and whose bed telemetry shows the target. */
static void accepted_and_heating(const char *action, int target) {
    char what[160]; snprintf(what, sizeof(what), "%s is accepted and sets the bed through the console", action);
    char m140[24]; snprintf(m140, sizeof(m140), "M140 S%d", target);
    expect(act(action) == 202 && !strcmp(queued, m140) && screws_soak.stage == SOAK_HEATING, what);
    finish(1); bed(30, target); tick();
}
static void end_session(void) {
    if (!control_screws_session_active()) return;
    if (state.busy) done(1);
    assert(act("screws:tram:end") == 202); done(1);
    control_screws_heaters_off_sent();
}
/* A request through the HTTP dispatcher (handle_client), with the refusals that pause printer changes. */
static int http_post(const char *path, const char *body) {
    int listener = socket(AF_INET, SOCK_STREAM, 0); assert(listener >= 0);
    struct sockaddr_in address; memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(!bind(listener, (struct sockaddr *)&address, sizeof(address)) && !listen(listener, 1));
    socklen_t size = sizeof(address); assert(!getsockname(listener, (struct sockaddr *)&address, &size));
    service_http_port = ntohs(address.sin_port);
    int client = socket(AF_INET, SOCK_STREAM, 0); assert(client >= 0 && !connect(client, (struct sockaddr *)&address, sizeof(address)));
    int server = accept(listener, NULL, NULL); assert(server >= 0); close(listener);
    char message[1024];
    int length = snprintf(message, sizeof(message),
                          "POST %s HTTP/1.1\r\nHost: localhost:%d\r\nOrigin: http://localhost:%d\r\nX-CC2-Request: 1\r\n"
                          "Content-Length: %zu\r\n\r\n%s", path, service_http_port, service_http_port, strlen(body), body);
    assert(length > 0 && (size_t)length < sizeof(message));
    queued[0] = 0;
    handle_client(server, message, (size_t)length, "none", &mqtt, &state); close(server);
    size_t used = 0; ssize_t got;
    while (used + 1 < sizeof(reply) && (got = read(client, reply + used, sizeof(reply) - 1 - used)) > 0) used += (size_t)got;
    reply[used] = 0; close(client);
    int status = 0; assert(sscanf(reply, "HTTP/1.1 %d", &status) == 1);
    return status;
}
/* The sessions during which the dispatcher refuses every printer change, each with its 409. */
enum { LOCK_NOZZLE, LOCK_DRYING, LOCK_CELL, LOCK_TOOLS, LOCK_UPDATE, LOCK_MAINTENANCE, LOCK_RFID_LIFECYCLE, LOCK_RFID_SCAN, LOCKS };
static const char *const lock_reason[LOCKS] = {
    "A nozzle expansion calibration is running; use the emergency stop to interrupt it",
    "Stop bed heating and wait for confirmation before another operation",
    "Finish the load-cell calibration session first",
    "A native calibration tool is still running",
    "Application update is running; printer changes are paused",
    "Installation or native maintenance is running; printer changes are paused",
    "RFID activation or recovery is incomplete; finish the lifecycle before changing the printer",
    "Cancel the RFID scan before changing printer settings",
};
static void lock(int which, int on) {
    switch (which) {
    case LOCK_NOZZLE: nx.stage = on ? NX_COOLING : NX_IDLE; break;
    case LOCK_DRYING: drying.stage = on ? DRY_CHECK : DRY_OFF; break;
    case LOCK_CELL: cell.loaded = 1; cell.state = on ? CELL_UNKNOWN : CELL_OFF; break;
    case LOCK_TOOLS: tools.busy = on; break;
    case LOCK_UPDATE: atomic_store(&app_update_busy, on); break;
    case LOCK_MAINTENANCE: if (on) assert(!mkdir(soak_test_lock, 0700)); else assert(!rmdir(soak_test_lock)); break;
    case LOCK_RFID_LIFECYCLE: rfid_native_job.child = on ? 1 : 0; break;
    case LOCK_RFID_SCAN: rfid.child = on ? 1 : 0; break;
    }
}
/* What a bed mesh of this service leaves behind when its probing succeeded at `temp`. */
static void mesh_done(int temp) {
    plates_cal.stage = CAL_PROBING; plates_cal.temp = temp;
    pthread_mutex_lock(&state.lock); plates_cal.generation = state.generation; pthread_mutex_unlock(&state.lock);
    plates_cal_end(&mqtt, "saved", "", "", 0);
}

int main(void) {
    char dir[] = "/tmp/screw-soak-XXXXXX"; assert(mkdtemp(dir));
    static char marker_path[64]; snprintf(marker_path, sizeof(marker_path), "%s/session", dir);
    screws_marker_path = marker_path;
    snprintf(soak_test_lock, sizeof(soak_test_lock), "%s/install.lock", dir);
    console_init(&state, "/tmp/unused");
    memset(&mqtt, 0, sizeof(mqtt)); fresh();
    screws_soak_send = fake_send;

    /* ---- Forms: the minutes 0..60 and only the measurements that prepare the bed themselves. ---- */
    static const char *const bad[] = {"screws:soak", "screws:soak:", "screws:soak:5", "screws:soak:5:", "screws:soak::screws:measure",
        "screws:soak:61:screws:measure", "screws:soak:123:screws:measure", "screws:soak:-1:screws:measure", "screws:soak:5x:screws:measure",
        "screws:soak:5:heaters:off", "screws:soak:5:screws:soak:5:screws:measure", "screws:soak:5:screws:tram:end",
        "screws:soak:5:screws:measurement"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        char what[120]; snprintf(what, sizeof(what), "%s is refused with 400", bad[i]);
        expect(act(bad[i]) == 400 && !queued[0] && !screws_soak.stage, what);
    }
    expect(act("screws:soak:5:screws:tram:probe:FL") == 400 && strstr(reply, "only available for screws:measure and screws:tram:start") &&
               act("screws:soak:5:screws:tram:check") == 400 && act("screws:soak:5:screws:tram:check:2") == 400,
           "re-probes of a session have no hold time (400)");
    expect(act("screws:soak:61:screws:measure") == 400 && strstr(reply, "from 0 to 60"), "the 400 names the range");
    /* The measurement after the prefix holds at most 95 characters (the job keeps it): 96 answer 400 Invalid action. */
    {
        char inner[200], longer[220];
        snprintf(inner, sizeof(inner), "screws:measure:%081d", 0);
        assert(strlen(inner) == 96);
        snprintf(longer, sizeof(longer), "screws:soak:5:%s", inner);
        expect(act(longer) == 400 && strstr(reply, "\"error\":\"Invalid action\"") && !queued[0] && !screws_soak.stage,
               "a measurement of 96 characters after the prefix answers 400 Invalid action");
        inner[95] = 0; snprintf(longer, sizeof(longer), "screws:soak:5:%s", inner);
        expect(act(longer) == 409 && strstr(reply, "bed target must be a whole number") && !queued[0] && !screws_soak.stage,
               "95 characters reach the measurement's own check (409)");
    }
    /* After an emergency stop (the console's latch stays set until CC2 Control restarts) a hold time would set the bed
     * and end at the next tick without switching it off again: refused before anything is sent. */
    atomic_store(&state.emergency_sent, 1);
    expect(act("screws:soak:5:screws:measure") == 409 && strstr(reply, "An emergency stop was sent") && !queued[0] && !screws_soak.stage,
           "a hold time after an emergency stop answers 409 and sends nothing");
    atomic_store(&state.emergency_sent, 0);
    if (state.busy) finish(1);
    if (screws_soak.stage) (void)act("screws:cancel");
    expect(act("screws:soakx:5:screws:measure") == 409 && strstr(reply, "Unknown or unsupported control action") && !queued[0],
           "another word after screws: is no hold time");
    /* The measurement itself is checked as without a hold time: the same 409 and nothing is sent. */
    expect(act("screws:soak:5:screws:measure:111") == 409 && strstr(reply, "bed target must be a whole number") && !queued[0] && !screws_soak.stage,
           "a malformed measurement answers the measurement's own 409");
    mqtt.machine_status = 2;
    expect(act("screws:soak:5:screws:measure") == 409 && strstr(reply, "blocked while printing") && !queued[0], "refused while printing");
    mqtt.machine_status = 1;
    reject_console = 1;
    expect(act("screws:soak:5:screws:measure") == 409 && strstr(reply, "Another command is running") && !screws_soak.stage,
           "refused while the console is busy");
    reject_console = 0;
    mqtt.have_position = 1; strcpy(mqtt.homed_axes, "xyz");
    assert(act("screws:tram:start") == 202); done(1);
    expect(act("screws:soak:5:screws:tram:start") == 409 && strstr(reply, "already running") && !screws_soak.stage,
           "a session start with a hold time is refused while a session runs");
    expect(act("screws:soak:5:screws:measure") == 409 && strstr(reply, "A screw session is running") && control_screws_session_active(),
           "a quick measurement with a hold time is refused during a session, which stays");
    end_session();

    /* ---- 0 minutes: exactly as without the prefix. ---- */
    static const char *const same[] = {"screws:measure:70", "screws:measure", "screws:measure:corner:FR:60:2", "screws:tram:start:65"};
    for (size_t i = 0; i < sizeof(same) / sizeof(same[0]); i++) {
        char plain[700], with[700], prefixed[96], what[160];
        snprintf(prefixed, sizeof(prefixed), "screws:soak:0:%s", same[i]);
        int a = act(same[i]); snprintf(plain, sizeof(plain), "%s", queued); done(1); end_session();
        int b = act(prefixed); snprintf(with, sizeof(with), "%s", queued);
        snprintf(what, sizeof(what), "%s builds the byte-identical script and answer of %s", prefixed, same[i]);
        expect(a == 202 && b == 202 && plain[0] && !strcmp(plain, with) && strstr(reply, "{\"accepted\":true}\n") && !strstr(reply, "after_mesh") &&
                   !screws_soak.stage, what);
        done(1); end_session();
    }
    expect(act("screws:soak:00:screws:measure") == 202 && !strcmp(queued, expected("screws:measure")) && !screws_soak.stage,
           "00 minutes is 0 minutes");
    done(1);

    /* ---- Heating, holding, then the measurement unchanged. ---- */
    sends = 0;
    accepted_and_heating("screws:soak:10:screws:measure:70", 70);
    expect(screws_soak.stage == SOAK_HEATING && screws_soak.seconds == 600 && !strcmp(screws_soak.action, "screws:measure:70"),
           "the job heats: a bed at 30 C is not at 70 C");
    expect(strstr(printer(), "\"screws_soak\":{\"active\":true,\"stage\":\"heating\",\"bed\":70,\"remaining\":600,\"total\":600,\"action\":\"quick\"}") &&
               strstr(reply, "\"screws_after_mesh\":false}"),
           "GET /api/printer reports the heating hold time");
    /* Meanwhile every other screw measurement and a mesh calibration are refused. */
    static const char *const blocked[] = {"screws:measure", "screws:measure:60:2", "screws:measure:corner:FR", "screws:tram:start",
                                          "screws:tram:start:60:2", "screws:soak:5:screws:measure", "screws:soak:0:screws:measure"};
    for (size_t i = 0; i < sizeof(blocked) / sizeof(blocked[0]); i++) {
        char what[120]; snprintf(what, sizeof(what), "%s is refused during the hold time", blocked[i]);
        expect(act(blocked[i]) == 409 && strstr(reply, "waiting for its bed hold time: cancel it first") && !queued[0] && screws_soak.stage, what);
    }
    expect(calibrate("A\n70\n0") == 409 && strstr(reply, "A screw measurement is waiting for its bed hold time") && !plates_cal.stage && !queued[0],
           "POST /api/plates/calibrate is refused during the hold time");
    bed(68.9, 70); tick(); expect(screws_soak.stage == SOAK_HEATING, "1.1 K below the target is not there yet");
    bed(69.2, 70); tick();
    expect(screws_soak.stage == SOAK_SOAKING && screws_soak.soak_until > monotonic_ms() + 595000, "at the target the hold time starts");
    expect(strstr(printer(), "\"screws_soak\":{\"active\":true,\"stage\":\"soaking\",\"bed\":70,\"remaining\":") &&
               strstr(reply, ",\"total\":600,\"action\":\"quick\"}"),
           "GET /api/printer reports the hold time counting down");
    tick(); expect(screws_soak.stage == SOAK_SOAKING && !state.busy && sends == 0, "while it holds, the console stays free and nothing is sent");
    /* The firmware's idle bed shutdown: the target is repeated every ten minutes while the job owns the bed. */
    expect(screws_soak.renew_at > monotonic_ms() + PLATE_BED_RENEW_MS - 5000, "the first repeat is due ten minutes after the M140");
    screws_soak.renew_at = monotonic_ms() - 1; tick();
    expect(sends == 1 && sent_count("M140 S70") == 1 && screws_soak.renew_at > monotonic_ms() + PLATE_BED_RENEW_MS - 5000,
           "the target is repeated, the next repeat in ten minutes");
    tick(); expect(sends == 1, "once per period");
    screws_soak_send = failing_send; screws_soak.renew_at = monotonic_ms() - 1; tick();
    expect(sends == 2 && screws_soak.renew_at <= monotonic_ms() + 30000, "a failed send is retried after 30 s");
    screws_soak_send = fake_send;
    state.busy = 1; screws_soak.renew_at = monotonic_ms() - 1; tick(); expect(sends == 2, "not while a console command runs");
    state.busy = 0;
    mqtt.last_message -= 20; tick(); expect(sends == 2 && screws_soak.stage == SOAK_SOAKING, "not with stale MQTT");
    fresh(); tick(); expect(sends == 3 && sent_count("M140 S70") == 2, "repeated once MQTT is fresh again");
    /* The end of the hold time: a command typed meanwhile finishes first, then the measurement starts. */
    const char *want = expected("screws:measure:70");
    char want_copy[700]; snprintf(want_copy, sizeof(want_copy), "%s", want);
    screws_soak.soak_until = monotonic_ms() - 1;
    state.busy = 1; tick(); expect(screws_soak.stage == SOAK_SOAKING, "a running console command is waited for");
    state.busy = 0; tick();
    expect(!screws_soak.stage && !strcmp(screws_soak.result, "started") && !strcmp(queued, want_copy) && control_screws_measuring() &&
               state.staged && strstr(state.cancel_tail, "M140 S0"),
           "then the measurement starts with its own unchanged, cancellable script");
    expect(strstr(printer(), "\"screws_soak\":{\"active\":false,\"result\":\"started\",\"reason\":\"\"}"), "GET /api/printer: started");
    done(1);
    expect(!control_screws_measuring(), "the measurement completes as any other");

    /* The one-corner measurement and the session start with a hold time. */
    accepted_and_heating("screws:soak:1:screws:measure:corner:FR:60", 60);
    expect(strstr(printer(), "\"action\":\"corner\"}"), "a corner measurement is reported as corner");
    bed(60.4, 60); tick(); screws_soak.soak_until = monotonic_ms() - 1;
    want = expected("screws:measure:corner:FR:60"); snprintf(want_copy, sizeof(want_copy), "%s", want);
    tick();
    expect(!screws_soak.stage && !strcmp(queued, want_copy) && control_screws_measuring(), "the corner measurement starts unchanged");
    done(1);
    accepted_and_heating("screws:soak:2:screws:tram:start:60:2", 60);
    expect(strstr(printer(), "\"total\":120,\"action\":\"session\"}") && !control_screws_session_active(),
           "a session start is reported as session; no session runs during the hold time");
    bed(60, 60); tick(); screws_soak.soak_until = monotonic_ms() - 1;
    want = expected("screws:tram:start:60:2"); snprintf(want_copy, sizeof(want_copy), "%s", want);
    tick();
    expect(!screws_soak.stage && !strcmp(queued, want_copy) && control_screws_session_active() && control_screws_measuring() &&
               control_screws_bed() == 60,
           "then the session starts as screws:tram:start:60:2 does");
    end_session();

    /* A hotter bed counts as at the target only within 2.5 K above it. */
    accepted_and_heating("screws:soak:1:screws:measure:60", 60);
    bed(63, 60); tick(); expect(screws_soak.stage == SOAK_HEATING, "3 K above the target: still cooling");
    bed(62.4, 60); tick(); expect(screws_soak.stage == SOAK_SOAKING, "2.4 K above it: the hold time starts");
    sends = 0; assert(act("screws:cancel") == 202);

    /* ---- Cancel in every phase: the bed goes off when the job owns it. ---- */
    sends = 0;
    accepted_and_heating("screws:soak:5:screws:measure", 60);
    bed(30, 60);
    expect(act("screws:cancel") == 202 && !screws_soak.stage && !strcmp(screws_soak.result, "cancelled") && sent_count("M140 S0") == 1,
           "cancelled while heating: 202, the bed goes off");
    expect(strstr(printer(), "\"screws_soak\":{\"active\":false,\"result\":\"cancelled\",\"reason\":\"\"}"), "GET /api/printer: cancelled");
    expect(act("screws:cancel") == 409 && strstr(reply, "No screw measurement is running"), "without a hold time or a job the cancel is 409 as before");
    sends = 0;
    accepted_and_heating("screws:soak:5:screws:measure", 60);
    bed(60, 60); tick(); expect(screws_soak.stage == SOAK_SOAKING, "holding");
    expect(act("screws:cancel") == 202 && !screws_soak.stage && sent_count("M140 S0") == 1, "cancelled while holding: the bed goes off");
    /* Cancelled before the job's own M140 reached telemetry: switched off once it shows, within a few seconds. */
    sends = 0; bed(25, 0);
    expect(act("screws:soak:5:screws:measure") == 202 && state.busy, "accepted, its M140 still running");
    expect(act("screws:cancel") == 202 && !screws_soak.stage && sends == 0 && screws_soak_off.until, "cancelled at once: nothing to switch off yet");
    tick(); expect(sends == 0, "not while its M140 runs");
    finish(1); bed(25, 60); tick();
    expect(sent_count("M140 S0") == 1 && !screws_soak_off.until, "once telemetry shows the job's target, it goes off");
    sends = 0; bed(25, 0);
    assert(act("screws:soak:5:screws:measure") == 202 && act("screws:cancel") == 202);
    finish(1); screws_soak_off.until = monotonic_ms() - 1; bed(25, 60); tick();
    expect(sends == 0 && !screws_soak_off.until, "after those seconds a target seen is not the job's any more");
    bed(25, 0);

    /* ---- What else ends the job; it switches the bed off only where it failed itself. ---- */
    for (int c = 0; c < 8; ++c) {
        fresh(); sends = 0;
        accepted_and_heating("screws:soak:5:screws:measure", 60);
        bed(60, 60); tick(); assert(screws_soak.stage == SOAK_SOAKING);
        const char *why = NULL, *how = "ended";
        if (c == 0) { mqtt.machine_status = 2; tick(); why = "A print started"; }
        if (c == 1) { assert(act("heaters:off") == 202 && !strcmp(queued, "TURN_OFF_HEATERS")); finish(1); why = "The heaters were switched off"; }
        if (c == 2) { screws_soak_tick(&mqtt, &state, 1); why = "Emergency stop"; }
        if (c == 3) { plates_cal.stage = CAL_HEATING; tick(); plates_cal.stage = CAL_IDLE; why = "A bed mesh calibration was started"; }
        if (c == 4) { assert(act("screws:tram:end") == 202); finish(1); control_screws_heaters_off_sent(); why = "Another screw action was started"; }
        if (c == 5) { bed(60, 50); tick(); why = "The bed target was changed"; }
        if (c == 6) { mqtt.machine_status = 10; tick(); why = "The printer is not idle"; }
        if (c == 7) { (void)act("system:emergency_stop"); why = "Emergency stop"; }
        char what[160]; snprintf(what, sizeof(what), "%s ends the job without touching the bed", why);
        expect(!screws_soak.stage && !strcmp(screws_soak.result, how) && !strcmp(screws_soak.reason, why) && !sent_count("M140 S0"), what);
        if (state.busy) finish(1);
        mqtt.machine_status = 1;
    }
    fresh();
    /* Failures of the job itself switch its bed off. */
    sends = 0;
    accepted_and_heating("screws:soak:5:screws:measure", 60);
    bed(40, 60); screws_soak.since -= SCREWS_SOAK_HEAT_MS + 1000; tick();
    expect(!screws_soak.stage && !strcmp(screws_soak.result, "failed") && !strcmp(screws_soak.reason, "The bed did not reach the temperature") &&
               sent_count("M140 S0") == 1,
           "a bed that does not reach the target in 30 minutes: failed, bed off");
    sends = 0; bed(25, 60);
    expect(act("screws:soak:5:screws:measure") == 202, "accepted");
    finish(0); tick();
    expect(!screws_soak.stage && !strcmp(screws_soak.reason, "The bed target could not be set") && sent_count("M140 S0") == 1,
           "its M140 failed: failed, bed off");
    /* At the end of the hold time the start waits up to a minute for fresh evidence. */
    sends = 0;
    accepted_and_heating("screws:soak:1:screws:measure", 60);
    bed(60, 60); tick(); screws_soak.soak_until = monotonic_ms() - 1;
    telemetry.present &= ~((UINT32_C(1) << U_BT) | (UINT32_C(1) << U_BG));
    tick(); expect(screws_soak.stage == SOAK_SOAKING && screws_soak.held && !state.busy, "no bed telemetry: it waits");
    bed(60, 60); mqtt.last_message -= 20; tick(); expect(screws_soak.stage == SOAK_SOAKING && !state.busy, "stale MQTT: it waits");
    screws_soak.held -= 61000; tick();
    expect(!screws_soak.stage && !strcmp(screws_soak.result, "failed") && !strcmp(screws_soak.reason, "Printer status is stale") &&
               !sent_count("M140 S0") && !state.busy,
           "after a minute it stops; a bed it cannot see as its own stays as it is");
    fresh();
    /* The second check, when the hold time ends: a reason found then ends the job with that reason and the bed off. */
    sends = 0;
    accepted_and_heating("screws:soak:1:screws:measure", 60);
    bed(60, 60); tick(); screws_soak.soak_until = monotonic_ms() - 1;
    plates_background.fd = 99; tick(); plates_background.fd = -1;
    expect(!screws_soak.stage && !strcmp(screws_soak.result, "failed") && !strcmp(screws_soak.reason, "Plate verification is still running") &&
               sent_count("M140 S0") == 1 && !state.busy,
           "the checks run again at the start: refused then, the job ends with the reason and the bed off");
    sends = 0;
    accepted_and_heating("screws:soak:1:screws:measure", 60);
    bed(60, 60); tick(); screws_soak.soak_until = monotonic_ms() - 1;
    reject_console = 1; tick();
    expect(screws_soak.stage == SOAK_SOAKING && !control_screws_measuring(), "a console that refuses the start is tried again");
    screws_soak.soak_until = monotonic_ms() - 61000; tick(); reject_console = 0;
    expect(!screws_soak.stage && !strcmp(screws_soak.reason, "Another command is running") && sent_count("M140 S0") == 1,
           "for up to a minute, then the job fails with the bed off");
    expect(strstr(printer(), "\"screws_soak\":{\"active\":false,\"result\":\"failed\",\"reason\":\"Another command is running\"}"),
           "GET /api/printer names the reason");
    /* A session start that the console refuses after the hold time: no session was built and none ended, so the end
     * reason of the last real session stays. */
    mqtt.have_position = 1; strcpy(mqtt.homed_axes, "xyz");
    assert(act("screws:tram:start:60") == 202); done(1); end_session();
    int reason_before = control_screws_end_reason();
    assert(reason_before == CONTROL_SCREWS_END_USER);
    sends = 0;
    accepted_and_heating("screws:soak:1:screws:tram:start:60", 60);
    bed(60, 60); tick(); screws_soak.soak_until = monotonic_ms() - 1;
    reject_console = 1; tick(); tick();
    expect(screws_soak.stage == SOAK_SOAKING && !control_screws_session_active() && control_screws_end_reason() == reason_before,
           "a session start the console refuses: no session, the last end reason stays (not console_error)");
    screws_soak.soak_until = monotonic_ms() - 61000; tick(); reject_console = 0;
    expect(!screws_soak.stage && !strcmp(screws_soak.reason, "Another command is running") && !control_screws_session_active() &&
               control_screws_end_reason() == reason_before && sent_count("M140 S0") == 1,
           "after a minute the job fails with the bed off, still without an end reason of its own");
    accepted_and_heating("screws:soak:1:screws:tram:start:60", 60);
    bed(60, 60); tick(); screws_soak.soak_until = monotonic_ms() - 1;
    want = expected("screws:tram:start:60"); snprintf(want_copy, sizeof(want_copy), "%s", want);
    tick();
    expect(!screws_soak.stage && !strcmp(queued, want_copy) && control_screws_session_active() && control_screws_measuring() &&
               state.staged && strstr(state.cancel_tail, "M140 S0"),
           "a console that is free starts the session with its unchanged, cancellable script");
    end_session();
    /* No MQTT connection while it heats and holds: the target is not repeated and the bed is not touched. */
    sends = 0;
    accepted_and_heating("screws:soak:1:screws:measure", 60);
    mqtt.connected = 0; screws_soak.renew_at = monotonic_ms() - 1; tick();
    expect(screws_soak.stage == SOAK_HEATING && sends == 0, "disconnected while heating: it keeps waiting, nothing is sent");
    bed(60, 60); tick(); expect(screws_soak.stage == SOAK_SOAKING && sends == 0, "the hold time starts from the telemetry alone");
    screws_soak.renew_at = monotonic_ms() - 1; tick(); expect(sends == 0, "disconnected while holding: the target is not repeated");
    screws_soak.soak_until = monotonic_ms() - 1; tick();
    expect(screws_soak.stage == SOAK_SOAKING && screws_soak.held && !state.busy, "at the end it waits for the connection");
    fresh(); tick();
    expect(!screws_soak.stage && !strcmp(screws_soak.result, "started") && control_screws_measuring(), "connected again: it starts");
    done(1);
    accepted_and_heating("screws:soak:1:screws:measure", 60);
    bed(60, 60); tick(); screws_soak.soak_until = monotonic_ms() - 1;
    mqtt.connected = 0; tick(); screws_soak.held -= 61000; tick();
    expect(!screws_soak.stage && !strcmp(screws_soak.reason, "Printer status is stale") && !sent_count("M140 S0") && !state.busy,
           "still disconnected after a minute: failed, the bed it cannot see as its own stays");
    fresh();
    /* The refusals that pause printer changes count among the checks at the end of the hold time: each ends the job with
     * its own 409 text and the bed off, and nothing starts. */
    for (int which = 0; which < LOCKS; which++) {
        sends = 0;
        accepted_and_heating("screws:soak:1:screws:measure", 60);
        bed(60, 60); tick(); screws_soak.soak_until = monotonic_ms() - 1;
        lock(which, 1); tick(); lock(which, 0);
        char what[200]; snprintf(what, sizeof(what), "at the end of the hold time: %s", lock_reason[which]);
        expect(!screws_soak.stage && !strcmp(screws_soak.result, "failed") && !strcmp(screws_soak.reason, lock_reason[which]) &&
                   !state.busy && !control_screws_measuring() && sent_count("M140 S0") == 1,
               what);
        /* The same session refuses the hold time itself, as without it. */
        lock(which, 1);
        int status = http_post("/api/control", "screws:soak:5:screws:measure");
        lock(which, 0);
        snprintf(what, sizeof(what), "a new hold time is refused while: %s", lock_reason[which]);
        expect(status == 409 && strstr(reply, lock_reason[which]) && !queued[0] && !screws_soak.stage, what);
    }
    /* While a hold time runs, those sessions do not start: 409 through the dispatcher, the hold time stays. */
    static const char *const starts_blocked[][2] = {
        {"/api/nozzle-expansion/start", "150:200:10"}, {"/api/bed-drying/start", "{}"}, {"/api/bed-drying/preheat", "{}"},
        {"/api/calibration-tools/action", "cell:start"}, {"/api/calibration-tools/action", "rotation:read"},
        {"/api/app-update/upload", ""}, {"/api/app-update/download", "{}"}, {"/api/app-update/install", "{}"},
        {"/api/network/action", "{}"}, {"/api/network/scan", "{}"}, {"/api/rfid/action", "scan"},
        {"/api/rfid/lifecycle/action", "activate"}};
    accepted_and_heating("screws:soak:5:screws:measure", 60);
    for (int heating = 1; heating >= 0; heating--) {
        if (!heating) { bed(60, 60); tick(); assert(screws_soak.stage == SOAK_SOAKING); }
        for (size_t i = 0; i < sizeof(starts_blocked) / sizeof(starts_blocked[0]); i++) {
            char what[200];
            snprintf(what, sizeof(what), "POST %s %s is refused while the hold time %s", starts_blocked[i][0], starts_blocked[i][1],
                     heating ? "heats" : "holds");
            expect(http_post(starts_blocked[i][0], starts_blocked[i][1]) == 409 &&
                       strstr(reply, "A screw measurement is waiting for its bed hold time: cancel it first") && screws_soak.stage &&
                       nx.stage == NX_IDLE && drying.stage == DRY_OFF && !tools.busy && !rfid.child && !state.busy,
                   what);
        }
    }
    expect(http_post("/api/control", "screws:cancel") == 202 && !screws_soak.stage, "screws:cancel still passes the dispatcher");
    /* A shutdown left pending by an ended session must not undo the job's bed. */
    control_screws_restore_pending();
    assert(act("screws:soak:1:screws:measure") == 202);
    expect(!control_screws_heaters_off_pending(), "the hold time takes the heaters over");
    finish(1); assert(act("screws:cancel") == 202);

    /* ---- Mesh calibration running: no screw measurement starts beside it. ---- */
    for (int stage = CAL_HOMING; stage <= CAL_SAVING; stage++) {
        plates_cal.stage = stage;
        static const char *const measures[] = {"screws:measure", "screws:measure:corner:FR:60", "screws:tram:start:60:2", "screws:soak:5:screws:measure"};
        for (size_t i = 0; i < sizeof(measures) / sizeof(measures[0]); i++) {
            char what[120]; snprintf(what, sizeof(what), "%s is refused while a calibration runs (stage %d)", measures[i], stage);
            expect(act(measures[i]) == 409 && strstr(reply, "A bed mesh calibration is running") && !queued[0] && !screws_soak.stage, what);
        }
    }
    plates_cal.stage = CAL_IDLE;
    expect(act("fan:part:0") == 202, "other actions are not affected");
    finish(1);

    /* ---- The measurement right after a bed mesh. ---- */
    strcpy(mqtt.homed_axes, "xyz"); mqtt.have_position = 1;
    plates_cal.stage = CAL_PROBING; plates_cal_end(&mqtt, "failed", "probing", "", 0);
    expect(!plates_cal.ended_at, "a failed calibration leaves nothing to follow");
    plates_cal.stage = CAL_PROBING; plates_cal_end(&mqtt, "cancelled", "", "", 0);
    expect(!plates_cal.ended_at, "nor does a cancelled one");
    static const char *const results[] = {"saved", "work", "done"};
    for (int r = 0; r < 3; r++) {
        plates_cal.stage = CAL_PROBING; plates_cal.temp = 60;
        pthread_mutex_lock(&state.lock); plates_cal.generation = state.generation; pthread_mutex_unlock(&state.lock);
        plates_cal_end(&mqtt, results[r], "", "", 0);
        char what[120]; snprintf(what, sizeof(what), "after a calibration with result %s the four corners follow directly", results[r]);
        expect(act("screws:measure") == 202 && strstr(reply, "{\"accepted\":true,\"after_mesh\":true}") &&
                   starts(queued, "SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM140 S60\nM190 S60\nG28 Z\nG4 P30000\nG90\nG1 Z10 F600\n"
                                  "G1 X27.9 Y25.95 F12000\nPROBE SAMPLES=3\n") &&
                   !strstr(queued, "G180") && !strstr(queued, "G28\n") && strstr(queued, "RESTORE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM104 S0\nM140 S0"),
               what);
        if (r == 0) {
            expect(strstr(printer(), "\"screws_after_mesh\":true}"), "GET /api/printer: the running measurement is the direct one");
            done(1);
            expect(strstr(printer(), "\"screws_after_mesh\":false}"), "and no longer once it completed");
        } else done(1);
    }
    /* Everything else keeps the full preparation, byte for byte. */
    struct { const char *what; int temp, age_ms, foreign, homed; const char *action; } cases[] = {
        {"181 s after the calibration", 60, 181000, 0, 1, "screws:measure"},
        {"another console job since the calibration", 60, 0, 1, 1, "screws:measure"},
        {"another bed target than the calibration's", 70, 0, 0, 1, "screws:measure:60"},
        {"Z not homed", 60, 0, 0, 0, "screws:measure"},
        {"a session start 181 s after", 60, 181000, 0, 1, "screws:tram:start"},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        mesh_done(cases[i].temp);
        plates_cal.ended_at -= cases[i].age_ms;
        if (cases[i].foreign) { assert(act("fan:part:0") == 202); finish(1); }
        strcpy(mqtt.homed_axes, cases[i].homed ? "xyz" : "xy");
        want = expected(cases[i].action); snprintf(want_copy, sizeof(want_copy), "%s", want);
        char what[160]; snprintf(what, sizeof(what), "%s: the full preparation, byte-identical", cases[i].what);
        expect(act(cases[i].action) == 202 && !strstr(reply, "after_mesh") && !strcmp(queued, want_copy) && strstr(queued, "G180 S3"), what);
        done(1); end_session();
    }
    strcpy(mqtt.homed_axes, "xyz");
    mesh_done(60); plates_cal.ended_at -= 179000;
    expect(act("screws:measure") == 202 && strstr(reply, "after_mesh") && !strstr(queued, "G180"), "179 s after it still follows directly");
    done(1);
    mesh_done(60); mqtt.have_position = 0;
    expect(act("screws:measure") == 202 && !strstr(reply, "after_mesh") && strstr(queued, "G180 S3"), "without a known position: the full preparation");
    done(1); mqtt.have_position = 1;
    /* The session start directly after the mesh: no full G28 and no nozzle cleaning. */
    mesh_done(60);
    expect(act("screws:tram:start:60:2") == 202 && strstr(reply, "after_mesh") &&
               starts(queued, "SAVE_GCODE_STATE NAME=CC2_SCREW_TRAM\nM140 S60\nM190 S60\nG28 Z\nG4 P30000\nG90\nG1 Z10 F600\n") &&
               !strstr(queued, "G180") && !strstr(queued, "G28\n") && strstr(queued, "M104 S0\nM140 S60\n") &&
               control_screws_session_active() && control_screws_after_mesh_running(),
           "a session start right after the mesh homes only Z");
    end_session();
    /* The one-corner form keeps its own short way; the hold time is skipped right after the mesh. */
    mesh_done(60);
    expect(act("screws:measure:corner:FR:60") == 202 && !strstr(reply, "after_mesh"), "the one-corner form is not the direct variant");
    done(1);
    mesh_done(60); sends = 0;
    expect(act("screws:soak:10:screws:measure:60") == 202 && strstr(reply, "after_mesh") && !screws_soak.stage &&
               !strstr(queued, "G180") && strstr(queued, "G28 Z\nG4 P30000"),
           "a hold time right after the mesh is skipped: the direct measurement starts at once");
    done(1);
    mesh_done(70);
    expect(act("screws:soak:10:screws:measure:60") == 202 && screws_soak.stage == SOAK_HEATING && !strcmp(queued, "M140 S60"),
           "at another bed target the hold time runs");
    finish(1); assert(act("screws:cancel") == 202);
    /* Script lengths (CONSOLE_COMMAND_MAX, 512 with the terminating zero) at the longest forms that fit: the quick
     * measurement at a three-digit target, twice, with a bed that cools first and without, and the session start twice at
     * 110 C with a cooling bed. Both variants fit everywhere. The direct one is not always the shorter: in place of the full one's G180 S3 and,
     * only with a cooling bed, its 15 s hold (G4 P15000) it always has G28 Z and the 30 s wait (G4 P30000), so without
     * cooling the direct quick measurement is 8 characters longer (450 against 442). */
    struct { const char *action; int target; double bed_now; const char *head; int direct_longer; } longest[] = {
        {"screws:measure:110:2", 110, 115, "M190 S107.5\nM140 S110\nM190 S110\nG28 Z\nG4 P30000\n", 0},
        {"screws:measure:110:2", 110, 110, "MEASURE\nM140 S110\nM190 S110\nG28 Z\nG4 P30000\n", 1},
        {"screws:tram:start:110:2", 110, 115, "M190 S107.5\nM140 S110\nM190 S110\nG28 Z\nG4 P30000\n", 0},
    };
    mqtt.have_bed_temp = 1;
    for (int i = 0; i < 3; i++) {
        char script[700], why[256]; size_t full, direct;
        mqtt.bed_temp = longest[i].bed_now;
        control_screws_after_mesh(-1);
        int full_ok = control_check_script(longest[i].action, &mqtt, script, sizeof(script), why, sizeof(why)) && !control_screws_built_direct();
        full = strlen(script);
        control_screws_after_mesh(longest[i].target);
        int direct_ok = control_check_script(longest[i].action, &mqtt, script, sizeof(script), why, sizeof(why)) && control_screws_built_direct();
        direct = strlen(script);
        control_screws_after_mesh(-1);
        char what[256];
        snprintf(what, sizeof(what), "%s with the bed at %d C: direct %zu, full %zu characters, both below %d", longest[i].action,
                 (int)longest[i].bed_now, direct, full, CONSOLE_COMMAND_MAX);
        expect(full_ok && direct_ok && (longest[i].direct_longer ? direct > full : direct <= full) && full < CONSOLE_COMMAND_MAX &&
                   direct < CONSOLE_COMMAND_MAX && strstr(script, longest[i].head),
               what);
    }
    /* Every bed target 0..110 C, once and twice, with the bed at the target and more than 2.5 K above it (the cooling
     * line), full and directly after the mesh: the session start, the four-corner and the one-corner measurement are
     * accepted and fit the console line (at most CONSOLE_COMMAND_MAX - 1 = 511 characters). */
    {
        static const char *const kinds[] = {"screws:tram:start", "screws:measure", "screws:measure:corner:FR"};
        size_t longest_of[3] = {0, 0, 0}; char longest_at[3][128] = {"", "", ""};
        int bad = 0; char first_bad[512] = "";
        for (int k = 0; k < 3; k++)
            for (int target = 0; target <= CONTROL_SCREWS_BED_MAX; target++)
                for (int twice = 0; twice < 2; twice++)
                    for (int hot = 0; hot < 2; hot++)
                        for (int direct = 0; direct < (k == 2 ? 1 : 2); direct++) {
                            char action[64], script[700], why[256];
                            snprintf(action, sizeof(action), "%s:%d%s", kinds[k], target, twice ? ":2" : "");
                            mqtt.bed_temp = target + (hot ? 3.0 : 0.0);
                            control_screws_after_mesh(direct ? target : -1);
                            int ok = control_check_script(action, &mqtt, script, sizeof(script), why, sizeof(why)) &&
                                     control_screws_built_direct() == direct && strlen(script) < CONSOLE_COMMAND_MAX;
                            control_screws_after_mesh(-1);
                            if (!ok && !bad++)
                                snprintf(first_bad, sizeof(first_bad), "%s (bed %d C, %s): %zu characters, %s", action, (int)mqtt.bed_temp,
                                         direct ? "direct" : "full", strlen(script), why);
                            if (ok && strlen(script) > longest_of[k]) {
                                longest_of[k] = strlen(script);
                                snprintf(longest_at[k], sizeof(longest_at[k]), "%s at %d C %s", action, (int)mqtt.bed_temp, direct ? "direct" : "full");
                            }
                        }
        char what[700];
        snprintf(what, sizeof(what), "every target, once and twice, cooling or not, full or direct fits (%d refused%s%s)", bad,
                 bad ? ", first: " : "", first_bad);
        expect(!bad, what);
        for (int k = 0; k < 3; k++) {
            snprintf(what, sizeof(what), "%s: longest %zu characters (%s)", kinds[k], longest_of[k], longest_at[k]);
            expect(longest_of[k] > 0 && longest_of[k] < CONSOLE_COMMAND_MAX, what);
        }
    }
    /* The corners as the table has them, without trailing zeros, and the second lift after a repeated probe without the
     * feed rate: the same moves at the printer's last feed rate (F600). */
    {
        char script[700], why[256];
        mqtt.bed_temp = 60; control_screws_after_mesh(-1);
        static const char *const moves[] = {"G1 X27.9 Y25.95 F12000\n", "G1 X227.15 Y25.95 F12000\n", "G1 X227.15 Y225.5 F12000\n",
                                            "G1 X27.9 Y225.5 F12000\n"};
        static const char *const forms[] = {"screws:measure:60:2", "screws:tram:start:60:2", "screws:measure:60", "screws:tram:start:60"};
        for (int f = 0; f < 4; f++) {
            int twice = f < 2;
            int ok = control_check_script(forms[f], &mqtt, script, sizeof(script), why, sizeof(why));
            for (int i = 0; ok && i < 4; i++) {
                char block[160];
                snprintf(block, sizeof(block), "%sPROBE SAMPLES=3\nG1 Z10 F600\n%s", moves[i], twice ? "PROBE SAMPLES=3\nG1 Z10\n" : "");
                ok = strstr(script, block) != NULL;
                if (!twice && ok) ok = !strstr(script, "PROBE SAMPLES=3\nG1 Z10 F600\nPROBE");
            }
            ok = ok && !strstr(script, "27.90") && !strstr(script, "225.50") && !strstr(script, "e+") &&
                 (twice ? !strstr(script, "PROBE SAMPLES=3\nG1 Z10 F600\nPROBE SAMPLES=3\nG1 Z10 F600") : 1);
            char what[160]; snprintf(what, sizeof(what), "%s: the four corners word for word", forms[f]);
            expect(ok, what);
        }
    }
    mqtt.have_bed_temp = 0;
    expect(!control_screws_session_active(), "a dry check of a session start starts no session");

    printf("%s\n", failures ? "FAILED" : "ALL PASS");
    return failures ? 1 : 0;
}
