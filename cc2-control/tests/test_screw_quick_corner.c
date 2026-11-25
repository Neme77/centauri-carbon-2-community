#include "../src/screw_history.h"
#include "../src/console.h"
#include <assert.h>

/* "Quick measurement of this screw" (screws:measure:corner:<ID>[:<C>]): the quick measurement for one corner.
 * Bed heating, the firmware's G180 S3 (homing and nozzle cleaning), M190, then front left as the reference and
 * the chosen corner, both heaters off, no 30 s wait. Same guards as the quick measurement, and not in a session. */
static int failures;
static void expect(int ok, const char *what) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}
static int count(const char *text, const char *needle) {
    int n = 0;
    for (const char *p = text; (p = strstr(p, needle)); p += strlen(needle)) n++;
    return n;
}
static void probe(char *out, size_t cap, double x, double y, double z) {
    size_t n = strlen(out);
    snprintf(out + n, cap - n, "// probe at %.3f,%.3f is z=%.6f\n// Result is z=%.6f\n", x, y, z, z);
}

int main(void) {
    char dir[] = "screw-qc-XXXXXX";
    assert(mkdtemp(dir) && !chdir(dir));
    char script[CONSOLE_COMMAND_MAX], reason[256];
    mqtt_client mqtt = {0};
    mqtt.connected = mqtt.registered = 1; mqtt.have_machine_status = 1; mqtt.machine_status = 1;
    mqtt.last_message = time(NULL);

    expect(control_build_script("screws:measure:corner:FR", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
               strstr(script, "SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM140 S60\nG180 S3\nM190 S60\nG90\nG1 Z10 F600\n") &&
               strstr(script, "G1 X27.9 Y25.95 F12000\nPROBE SAMPLES=3\n") && strstr(script, "G1 X227.15 Y25.95 F12000\nPROBE SAMPLES=3\n") &&
               strstr(script, "RESTORE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM104 S0\nM140 S0") && count(script, "PROBE") == 2,
           "a corner measurement heats, runs G180 S3, waits for the bed, probes front left and the corner, switches both heaters off");
    expect(strstr(script, "X27.9 Y25.95") < strstr(script, "X227.15 Y25.95") && !strstr(script, "G4") && !strstr(script, "G28"),
           "front left comes first, there is no wait and no own G28");
    expect(control_build_script("screws:measure:corner:RL:70", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
               strstr(script, "M140 S70\nG180 S3\nM190 S70\n") && strstr(script, "X27.9 Y225.5") && !strstr(script, "S60"),
           "a chosen bed target is used for heating and waiting");
    expect(control_build_script("screws:measure:corner:RR", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
               strstr(script, "X227.15 Y225.5") && count(script, "PROBE") == 2, "back right");
    expect(control_build_script("screws:measure:corner:FL", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
               count(script, "PROBE") == 1, "front left alone is measured once");
    static const char *const refused[] = {"screws:measure:corner:", "screws:measure:corner:XX", "screws:measure:corner:fr",
                                          "screws:measure:corner:FR:", "screws:measure:corner:FR:111", "screws:measure:corner:FR:abc",
                                          "screws:measure:corner:FR:70:3", "screws:measure:corner:FR:70:22", "screws:measure:corner:FR:70:", "screws:measure:corner:FR::2", "screws:measure:60:3", "screws:measure:60:", "screws:measure:60:02x", "screws:measure:corner:FR:1a", "screws:measure:corner:FR: 7", "screws:measure:corner:FR:+7", "screws:measure:corner:FRX", "screws:measure:corner"};
    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); i++) {
        char what[120]; snprintf(what, sizeof(what), "%s is refused", refused[i]);
        expect(!control_build_script(refused[i], &mqtt, script, sizeof(script), reason, sizeof(reason)), what);
    }
    expect(control_screws_is_quick("screws:measure:corner:FR") && control_screws_is_quick("screws:measure:corner:RL:70") &&
               !control_screws_is_quick("screws:measure:corner:XX") && control_screws_is_quick("screws:measure:100") &&
               !control_screws_is_quick("screws:measure:corner"),
           "the corner action is recognised as a quick measurement, the quick one still is");


    /* The bed is hotter than the target: it cools down first (the nozzle stays off), then the nozzle is cleaned. The printer's
     * M190 releases when the bed is within 5 K of the target and no longer changing fast (heaters.cpp, PID_SETTLE_DELTA),
     * and up to 2.5 K above the target is accepted. Above it the wait is for a target 2.5 K lower, which releases at the target + 2.5 K; then the real target is set again.
     * A bed at or below the target + 2.5 K, or unknown, keeps the old order (heat while cleaning). */
    mqtt.have_bed_temp = 1; mqtt.bed_temp = 100.0;
    expect(control_build_script("screws:measure:60", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
               strstr(script, "SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM190 S57.5\nM140 S60\nG180 S3\nM190 S60\nG4 P15000\nG90\nG1 Z10 F600\n"),
           "four corners with a hot bed: cool to the target first, then G180 S3, then a short hold");
    expect(strstr(script, "M190 S57.5") < strstr(script, "G180 S3"), "the cooling wait comes before the nozzle cleaning");
    expect(control_build_script("screws:measure:corner:RR:70", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
               strstr(script, "NAME=CC2_SCREW_MEASURE\nM190 S67.5\nM140 S70\nG180 S3\nM190 S70\nG4 P15000\n"), "one corner, full preparation: the same order, at 70 C");
    mqtt.bed_temp = 62.5;
    expect(control_build_script("screws:measure:60", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
               strstr(script, "NAME=CC2_SCREW_MEASURE\nM140 S60\nG180 S3\nM190 S60\nG90\n") && !strstr(script, "S57.5"), "within 2.5 K above the target: no cooling wait");
    mqtt.bed_temp = 62.6;
    expect(control_build_script("screws:measure:60", &mqtt, script, sizeof(script), reason, sizeof(reason)) && strstr(script, "M190 S57.5\n"), "more than 2.5 K above: cooling wait");
    mqtt.bed_temp = 20.0;
    expect(control_build_script("screws:measure:60", &mqtt, script, sizeof(script), reason, sizeof(reason)) && !strstr(script, "S57.5") && strstr(script, "G180 S3\nM190 S60"), "a cold bed heats while the nozzle is cleaned, as before");
    mqtt.have_bed_temp = 0; mqtt.bed_temp = 100.0;
    expect(control_build_script("screws:measure:60", &mqtt, script, sizeof(script), reason, sizeof(reason)) && !strstr(script, "S57.5"), "an unknown bed temperature keeps the old order");
    mqtt.have_bed_temp = 1; mqtt.bed_temp = 100.0;
    expect(control_build_script("screws:measure:4", &mqtt, script, sizeof(script), reason, sizeof(reason)) && !strstr(script, "S-"), "no negative cooling target for a tiny bed target");
    mqtt.have_position = 1; strcpy(mqtt.homed_axes, "xyz");
    expect(control_build_script("screws:measure:corner:FR:60", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
               strstr(script, "NAME=CC2_SCREW_MEASURE\nM190 S57.5\nM140 S60\nM190 S60\nG28 Z\nG4 P30000\n"), "one corner again, X and Y homed: cool first, then G28 Z");
    mqtt.have_position = 0; mqtt.homed_axes[0] = 0;
    expect(control_build_script("screws:tram:start:60", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
               strstr(script, "NAME=CC2_SCREW_TRAM\nM190 S57.5\nM140 S60\nG28\nG180 S3\nM190 S60\nG4 P30000\n"), "a session start with a hot bed: cool first, then G28 and G180 S3");
    control_screws_reset();
    mqtt.bed_temp = 40.0;
    expect(control_build_script("screws:tram:start:60", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
               strstr(script, "NAME=CC2_SCREW_TRAM\nM140 S60\nG28\nG180 S3\nM190 S60\nG4 P30000\n") && !strstr(script, "S57.5"), "a colder bed: the old order");
    control_screws_reset();
    mqtt.have_bed_temp = 0;

    /* The session start takes "2 x 3 probes" too (":2" after an explicit bed target): every corner is probed twice, the finished job
     * says so, and the history keeps both results of every corner (the page shows their average). */
    expect(control_build_script("screws:tram:start:60:2", &mqtt, script, sizeof(script), reason, sizeof(reason)) && count(script, "PROBE") == 8 &&
               count(script, "PROBE SAMPLES=3\nG1 Z10 F600\nPROBE SAMPLES=3\nG1 Z10\n") == 4 && strstr(script, "M140 S60\nG28\nG180 S3\nM190 S60\nG4 P30000\n"),
           "the session start with 2x3 probes: eight probes, two per corner");
    control_screws_note_action("screws:tram:start:60:2"); control_screws_note_job(31);
    (void)control_screws_tick(&mqtt, 1, 1, 31, 0, time(NULL));
    {
        control_screws_job sj; char sout[4096] = ""; screw_history_store sh; memset(&sh, 0, sizeof(sh));
        expect(control_screws_take_finished(&sj) && sj.kind == CONTROL_SCREWS_JOB_START && sj.twice == 1 && sj.bed == 60, "the finished start knows it measured twice");
        static const double xs[4] = {27.9, 227.15, 227.15, 27.9}, ys[4] = {25.95, 25.95, 225.5, 225.5};
        for (int i = 0; i < 4; i++) { probe(sout, sizeof(sout), xs[i], ys[i], -0.70 + i * 0.01); probe(sout, sizeof(sout), xs[i], ys[i], -0.70 + i * 0.01 + 0.004); }
        expect(screw_history_record(&sh, &sj, sout, strlen(sout), 1762800000) && sh.s[0].m[0].twice == 1 && sh.s[0].m[0].have == 15 && sh.s[0].m[0].have2 == 15,
               "the history keeps both results of every corner of a start");
    }
    control_screws_reset();
    expect(control_screws_is_start("screws:tram:start:60:2") && control_screws_is_start("screws:tram:start:2") && !control_screws_is_start("screws:tram:start:60:3") && !control_screws_is_start("screws:tram:start:60:"),
           "only :2 after an explicit target is the twice suffix; a lone :2 is a bed target");
    expect(!control_build_script("screws:tram:start:60:3", &mqtt, script, sizeof(script), reason, sizeof(reason)) && !control_build_script("screws:tram:start:60:2:2", &mqtt, script, sizeof(script), reason, sizeof(reason)),
           "other suffixes are refused");
    expect(control_build_script("screws:tram:start:2", &mqtt, script, sizeof(script), reason, sizeof(reason)) && count(script, "PROBE") == 4 && strstr(script, "M140 S2\n"),
           "a lone :2 is the bed target 2 C, once");
    control_screws_reset();

    /* "2 x 3 probes": a trailing ":2" after an explicit bed target, as the session's check:2 and probe:<ID>:2. Every
     * probe of the measurement, front left included, runs twice. A lone ":2" is still the bed target 2 C. */
    expect(control_build_script("screws:measure:60:2", &mqtt, script, sizeof(script), reason, sizeof(reason)) && count(script, "PROBE") == 8 &&
               count(script, "PROBE SAMPLES=3\nG1 Z10 F600\nPROBE SAMPLES=3\nG1 Z10\n") == 4 && strstr(script, "M140 S60\nG180 S3\nM190 S60\n"),
           "four corners, twice: every corner is probed twice, at the chosen bed target");
    control_screws_note_quick(21);
    (void)control_screws_tick(&mqtt, 1, 1, 21, 0, time(NULL));
    control_screws_job tj;
    expect(control_screws_take_finished(&tj) && tj.kind == CONTROL_SCREWS_JOB_QUICK && tj.twice == 1 && tj.corner == -1 && tj.bed == 60,
           "the finished four-corner job knows it measured twice");
    expect(control_build_script("screws:measure:2", &mqtt, script, sizeof(script), reason, sizeof(reason)) && count(script, "PROBE") == 4 &&
               strstr(script, "M140 S2\n"), "a lone :2 is the bed target 2 C, one probe per corner");
    expect(control_build_script("screws:measure:corner:RL:70:2", &mqtt, script, sizeof(script), reason, sizeof(reason)) && count(script, "PROBE") == 4 &&
               strstr(script, "X27.9 Y25.95 F12000\nPROBE SAMPLES=3\nG1 Z10 F600\nPROBE SAMPLES=3\n") &&
               strstr(script, "X27.9 Y225.5 F12000\nPROBE SAMPLES=3\nG1 Z10 F600\nPROBE SAMPLES=3\n") && strstr(script, "M140 S70\nG180 S3\nM190 S70\n"),
           "one corner, twice: front left and the corner, each twice");
    control_screws_note_quick(22);
    (void)control_screws_tick(&mqtt, 1, 1, 22, 0, time(NULL));
    expect(control_screws_take_finished(&tj) && tj.twice == 1 && tj.corner == 3 && tj.bed == 70, "the finished corner job knows it measured twice");
    expect(control_build_script("screws:measure:corner:FL:70:2", &mqtt, script, sizeof(script), reason, sizeof(reason)) && count(script, "PROBE") == 2,
           "front left alone, twice");
    expect(control_build_script("screws:measure:corner:FR:2", &mqtt, script, sizeof(script), reason, sizeof(reason)) && count(script, "PROBE") == 2 &&
               strstr(script, "M140 S2\n"), "a corner with a lone :2 is the bed target 2 C, once");
    expect(control_screws_is_quick("screws:measure:60:2") && control_screws_is_quick("screws:measure:corner:FR:60:2") &&
               !control_screws_is_quick("screws:measure:60:3"), "the twice forms are quick measurements, other suffixes are not");
    control_screws_note_quick(23);
    (void)control_screws_tick(&mqtt, 1, 1, 23, 0, time(NULL));
    expect(control_build_script("screws:measure:60", &mqtt, script, sizeof(script), reason, sizeof(reason)) && count(script, "PROBE") == 4, "once is still once");
    control_screws_note_quick(24);
    (void)control_screws_tick(&mqtt, 1, 1, 24, 0, time(NULL));
    expect(control_screws_take_finished(&tj) && tj.twice == 0, "a once-job does not remember the earlier twice");

    /* "Measure this corner again": X and Y still referenced, so only Z is homed (G28 Z), the bed waits for its target
     * (heated before the Z homing, as in the session), then the short wait of the session, front left and the corner. */
    mqtt.have_position = 1; strcpy(mqtt.homed_axes, "xyz");
    expect(control_build_script("screws:measure:corner:FR:70", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
               strstr(script, "SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM140 S70\nM190 S70\nG28 Z\nG4 P30000\nG90\nG1 Z10 F600\n") &&
               !strstr(script, "G180") && !strstr(script, "G28\n") && count(script, "PROBE") == 2 && strstr(script, "X27.9 Y25.95") < strstr(script, "X227.15 Y25.95") &&
               strstr(script, "RESTORE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM104 S0\nM140 S0"),
           "X and Y referenced: G28 Z only, wait for the bed, 30 s wait, front left and the corner, both heaters off");
    expect(control_build_script("screws:measure:corner:FR:70:2", &mqtt, script, sizeof(script), reason, sizeof(reason)) && count(script, "PROBE") == 4 &&
               strstr(script, "G28 Z\nG4 P30000") && !strstr(script, "G180"), "the short way works with twice");
    expect(control_build_script("screws:measure:60", &mqtt, script, sizeof(script), reason, sizeof(reason)) && strstr(script, "G180 S3") && !strstr(script, "G4 P"),
           "the four-corner measurement keeps the full preparation");
    strcpy(mqtt.homed_axes, "x");
    expect(control_build_script("screws:measure:corner:FR", &mqtt, script, sizeof(script), reason, sizeof(reason)) && strstr(script, "G180 S3") && !strstr(script, "G28 Z"),
           "only X referenced: the full preparation as before");
    strcpy(mqtt.homed_axes, "xyz"); mqtt.have_position = 0;
    expect(control_build_script("screws:measure:corner:FR", &mqtt, script, sizeof(script), reason, sizeof(reason)) && strstr(script, "G180 S3") && !strstr(script, "G28 Z"),
           "no position report: the full preparation as before");
    mqtt.have_position = 0; mqtt.homed_axes[0] = 0;
    mqtt.machine_status = 2;
    expect(!control_build_script("screws:measure:corner:FR", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
               strstr(reason, "printing"), "refused while printing");
    mqtt.machine_status = 10;
    expect(!control_build_script("screws:measure:corner:FR", &mqtt, script, sizeof(script), reason, sizeof(reason)),
           "refused when the printer is not idle");
    mqtt.machine_status = 1; mqtt.have_machine_status = 0;
    expect(!control_build_script("screws:measure:corner:FR", &mqtt, script, sizeof(script), reason, sizeof(reason)),
           "refused without a machine status");
    mqtt.have_machine_status = 1;
    expect(control_build_script("screws:tram:start", &mqtt, script, sizeof(script), reason, sizeof(reason)) && control_screws_session_active(),
           "a session is started for the next check");
    expect(!control_build_script("screws:measure:corner:FR", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
               strstr(reason, "session") && control_screws_session_active(),
           "refused during a session (re-probe there), and the session stays");
    /* No quick measurement in a session, none of them (it used to end the session), and no second start. */
    expect(!control_build_script("screws:measure", &mqtt, script, sizeof(script), reason, sizeof(reason)) && strstr(reason, "session") &&
               control_screws_session_active(), "the four-corner quick measurement is refused during a session, which stays");
    expect(!control_build_script("screws:measure:70:2", &mqtt, script, sizeof(script), reason, sizeof(reason)) && control_screws_session_active(),
           "also with a bed target and twice");
    expect(!control_build_script("screws:measure:corner:RL:70:2", &mqtt, script, sizeof(script), reason, sizeof(reason)) && control_screws_session_active(),
           "and for a corner");
    int bed_before = control_screws_bed();
    expect(!control_build_script("screws:tram:start:90", &mqtt, script, sizeof(script), reason, sizeof(reason)) && strstr(reason, "session") &&
               control_screws_session_active() && control_screws_bed() == bed_before && control_screws_end_reason() == CONTROL_SCREWS_END_NONE,
           "a second start is refused during a session: it keeps its bed target and is not ended");
    expect(!control_build_script("screws:tram:start", &mqtt, script, sizeof(script), reason, sizeof(reason)) && control_screws_session_active(),
           "also the plain start");
    control_screws_reset();

    /* The job and the history entry. */
    expect(control_build_script("screws:measure:corner:RR:80", &mqtt, script, sizeof(script), reason, sizeof(reason)), "builds");
    control_screws_note_quick(7);
    expect(!control_screws_tick(&mqtt, 1, 1, 7, 0, time(NULL)) || 1, "tick runs");
    control_screws_job job;
    expect(control_screws_take_finished(&job) && job.kind == CONTROL_SCREWS_JOB_QUICK && job.corner == 2 && job.bed_set && job.bed == 80,
           "the finished corner job carries its corner and bed target");
    char out[4096] = "";
    probe(out, sizeof(out), 27.9, 25.95, -0.70);
    probe(out, sizeof(out), 227.15, 225.5, -0.80);
    screw_history_store h; memset(&h, 0, sizeof(h));
    time_t t0 = 1762800000;
    screw_ist ist = {7, {80.1, 140.0, 24.0}};
    expect(screw_history_record_ist(&h, &job, out, strlen(out), t0, &ist) && h.count == 1 && h.s[0].quick &&
               h.s[0].m[0].corner == 2 && h.s[0].m[0].have == 5 && h.s[0].m[0].have_ist == 7,
           "the entry is a quick entry with its corner, front left and that corner, and the temperatures");
    char *json = screw_history_json(&h, NULL);
    expect(strstr(json, "\"kind\":\"quick\"") && strstr(json, "\"corner\":\"RR\"") && strstr(json, "\"FL\":-0.700000") &&
               strstr(json, "\"RR\":-0.800000") && !strstr(json, "\"FR\""), "the history JSON names the corner and has only the two values");
    free(json);
    char only_fl[1024] = "";
    probe(only_fl, sizeof(only_fl), 27.9, 25.95, -0.70);
    memset(&h, 0, sizeof(h));
    expect(!screw_history_record(&h, &job, only_fl, strlen(only_fl), t0) && h.count == 0,
           "a corner job without a result for its corner records nothing");
    /* A plain quick measurement after it has no corner again. */
    expect(control_build_script("screws:measure", &mqtt, script, sizeof(script), reason, sizeof(reason)), "plain quick builds");
    control_screws_note_quick(8);
    (void)control_screws_tick(&mqtt, 1, 1, 8, 0, time(NULL));
    expect(control_screws_take_finished(&job) && job.corner == -1, "a plain quick measurement has no corner");

    /* The hold-time prefix: only the measurements that prepare the bed, minutes 0..60; the measurement itself is checked
     * as without it. */
    int minutes = -1; char inner[96];
    expect(control_screws_soak_parse("screws:soak:60:screws:measure:corner:FR:70:2", &minutes, inner, sizeof(inner), reason, sizeof(reason)) == 1 &&
               minutes == 60 && !strcmp(inner, "screws:measure:corner:FR:70:2"), "screws:soak:60 before a corner measurement");
    expect(control_screws_soak_parse("screws:soak:0:screws:tram:start", &minutes, inner, sizeof(inner), reason, sizeof(reason)) == 1 &&
               minutes == 0 && !strcmp(inner, "screws:tram:start"), "0 minutes before a session start");
    expect(control_screws_soak_parse("screws:soak:5:screws:measure:111", &minutes, inner, sizeof(inner), reason, sizeof(reason)) == 1,
           "a malformed measurement is left to the measurement's own check");
    expect(control_screws_soak_parse("screws:measure", &minutes, inner, sizeof(inner), reason, sizeof(reason)) == 0 &&
               control_screws_soak_parse("screws:soakx", &minutes, inner, sizeof(inner), reason, sizeof(reason)) == 0, "no prefix: 0");
    expect(control_screws_soak_parse("screws:soak:61:screws:measure", &minutes, inner, sizeof(inner), reason, sizeof(reason)) == -1 &&
               strstr(reason, "0 to 60") &&
               control_screws_soak_parse("screws:soak:5:screws:tram:check", &minutes, inner, sizeof(inner), reason, sizeof(reason)) == -1 &&
               strstr(reason, "only available") &&
               control_screws_soak_parse("screws:soak:5:screws:measure", &minutes, inner, 10, reason, sizeof(reason)) == -1,
           "too many minutes, a re-probe or no room: -1");
    expect(!control_screws_is_quick("screws:soak:5:screws:measure") && !control_screws_is_start("screws:soak:5:screws:tram:start") &&
               !control_screws_cancel_tail("screws:soak:5:screws:measure"),
           "the prefixed action itself is no measurement job");
    expect(control_screws_action_bed("screws:measure") == 60 && control_screws_action_bed("screws:measure:corner:RL:70:2") == 70 &&
               control_screws_action_bed("screws:tram:start:5") == 5 && control_screws_action_bed("screws:tram:check") == -1 &&
               control_screws_action_bed("screws:measure:111") == -1,
           "the bed target of a measurement");
    expect(!strcmp(control_screws_action_kind("screws:measure:70:2"), "quick") && !strcmp(control_screws_action_kind("screws:measure:corner:FR"), "corner") &&
               !strcmp(control_screws_action_kind("screws:tram:start:60"), "session") && !control_screws_action_kind("screws:tram:probe:FL"),
           "the kind of a measurement");
    /* A dry check builds the same script and starts nothing. */
    control_screws_reset();
    char dry[CONSOLE_COMMAND_MAX];
    expect(control_check_script("screws:tram:start:70", &mqtt, dry, sizeof(dry), reason, sizeof(reason)) && !control_screws_session_active() &&
               control_build_script("screws:tram:start:70", &mqtt, script, sizeof(script), reason, sizeof(reason)) && !strcmp(dry, script) &&
               control_screws_session_active(),
           "the dry check of a session start builds its script and starts no session");
    expect(!control_check_script("screws:tram:start:70", &mqtt, dry, sizeof(dry), reason, sizeof(reason)) && strstr(reason, "already running") &&
               control_screws_session_active(), "and refuses a second start like the start itself, the session stays");
    control_screws_reset();
    /* Right after a bed mesh at the same target, with X, Y and Z homed: no G180 S3 for the four corners. */
    mqtt.have_position = 1; strcpy(mqtt.homed_axes, "xyz");
    control_screws_after_mesh(70);
    expect(control_build_script("screws:measure:70", &mqtt, script, sizeof(script), reason, sizeof(reason)) && control_screws_built_direct() &&
               strstr(script, "SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM140 S70\nM190 S70\nG28 Z\nG4 P30000\nG90\nG1 Z10 F600\nG1 X27.9 Y25.95") &&
               !strstr(script, "G180") && count(script, "PROBE") == 4 && strstr(script, "M104 S0\nM140 S0"),
           "after a mesh at 70 C the four corners home only Z and wait 30 s");
    expect(control_build_script("screws:measure:60", &mqtt, script, sizeof(script), reason, sizeof(reason)) && !control_screws_built_direct() &&
               strstr(script, "G180 S3"), "another target: the full preparation");
    expect(control_build_script("screws:measure:corner:FR:70", &mqtt, script, sizeof(script), reason, sizeof(reason)) && !control_screws_built_direct(),
           "the one-corner form is not the direct variant");
    strcpy(mqtt.homed_axes, "xy");
    expect(control_build_script("screws:measure:70", &mqtt, script, sizeof(script), reason, sizeof(reason)) && !control_screws_built_direct() &&
               strstr(script, "G180 S3"), "Z not homed: the full preparation");
    control_screws_after_mesh(-1);
    return failures ? 1 : 0;
}
