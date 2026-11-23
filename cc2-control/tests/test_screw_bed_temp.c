#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

/* Screw tramming and the one-shot screw measurement heat the bed to a chosen target (0..110 C, the touchscreen's bed limit,
 * 60 C when none is given). Every place that set 60 C follows it: the start, the closing M140 of
 * each job, the ten-minute renewal, the restart marker and its shutdown, and the history. */
static int failures;
static void expect(int ok, const char *what) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}
static char sent[256];
static int sends;
static int capture(const char *script) { snprintf(sent, sizeof(sent), "%s", script); sends++; return 0; }
static char *json_of_history_file(const char *text) {
    FILE *file = fopen("screw-history.txt", "w"); assert(file); fputs(text, file); fclose(file);
    screw_history_store h; assert(screw_history_load(&h, "screw-history.txt"));
    return screw_history_json(&h, NULL);
}

int main(void) {
    char dir[] = "screw-bed-XXXXXX"; /* the harnesses share one working directory */
    assert(mkdtemp(dir) && !chdir(dir));
    char script[CONSOLE_COMMAND_MAX], reason[256];
    mqtt_client mqtt = {0};
    mqtt.connected = mqtt.registered = 1; mqtt.have_machine_status = 1; mqtt.machine_status = 1;
    mqtt.last_message = time(NULL);

    /* --- The chosen target in the scripts. --- */
    expect(control_build_script("screws:tram:start:70", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
           strstr(script, "M140 S70\nG28\nG180 S3\nM190 S70\nG4 P30000\nG90\n") &&
           strstr(script, "RESTORE_GCODE_STATE NAME=CC2_SCREW_TRAM\nM104 S0\nM140 S70\n") && !strstr(script, "S60"),
           "a session started at 70 C heats, waits for and keeps the bed at 70 C");
    mqtt.have_position = 1; strcpy(mqtt.homed_axes, "xyz");
    expect(control_build_script("screws:tram:probe:FR", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
           strstr(script, "M104 S0\nM140 S70\n") && !strstr(script, "S60"), "a re-probe keeps the session's 70 C");
    expect(control_build_script("screws:tram:check:2", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
           strstr(script, "M104 S0\nM140 S70\n") && !strstr(script, "S60"), "a check keeps the session's 70 C");
    control_screws_reset(); /* a running session refuses a second start and any quick measurement */
    expect(control_build_script("screws:tram:start", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
           strstr(script, "M140 S60\nG28\nG180 S3\nM190 S60\n") && strstr(script, "M104 S0\nM140 S60\n"),
           "a start without a target is the 60 C session as before");
    control_screws_reset(); /* a running session refuses a second start and any quick measurement */
    expect(control_build_script("screws:tram:start:0", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
           strstr(script, "M140 S0\nG28\n") && strstr(script, "M190 S0\n"), "0 C (bed not heated) is accepted");
    control_screws_reset(); /* a running session refuses a second start and any quick measurement */
    expect(control_build_script("screws:tram:start:110", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
           strstr(script, "M190 S110\n"), "110 C, the touchscreen's bed limit, is the highest target");
    static const char *const refused[] = {"screws:tram:start:111", "screws:tram:start:120", "screws:tram:start:121", "screws:tram:start:-5", "screws:tram:start:",
                                          "screws:tram:start:6a", "screws:tram:start:1000", "screws:tram:start:70:2",
                                          "screws:tram:start: 70", "screws:tram:start:70.5", "screws:measure:111", "screws:measure:121",
                                          "screws:measure:", "screws:measure:abc"};
    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); i++) {
        char what[96]; snprintf(what, sizeof(what), "%s is refused", refused[i]);
        expect(!control_build_script(refused[i], &mqtt, script, sizeof(script), reason, sizeof(reason)), what);
    }
    expect(!control_build_script("screws:tram:startx", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
           strstr(reason, "Unknown screw tramming action"), "another action name stays an unknown action");
    control_screws_reset(); /* a running session refuses a second start and any quick measurement */
    expect(control_build_script("screws:measure:100", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
           strstr(script, "M140 S100\nG180 S3\nM190 S100\n") && strstr(script, "M104 S0\nM140 S0"),
           "a one-shot measurement at 100 C heats and waits for 100 C, then switches both heaters off");
    expect(control_build_script("screws:measure", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
           strstr(script, "M140 S60\nG180 S3\nM190 S60\n"), "a one-shot measurement without a target stays at 60 C");

    /* --- A shutdown inherited from an earlier process follows that process's target. --- */
    control_screws_invalidate(); control_screws_heaters_off_sent();
    FILE *marker = fopen("marker", "w"); assert(marker); fputs("70\n", marker); fclose(marker);
    screws_marker_path = "marker"; screws_marker_restore(0);
    sends = 0; screws_heaters_off_tick(&mqtt, 0, capture, 1, 70.0);
    expect(sends == 1 && !strcmp(sent, "TURN_OFF_HEATERS"), "a bed still at the marker's 70 C is switched off after a restart");
    marker = fopen("marker", "w"); assert(marker); fputs("70\n", marker); fclose(marker);
    screws_marker_restore(0);
    sends = 0; screws_heaters_off_tick(&mqtt, 0, capture, 1, 60.0);
    expect(sends == 0 && !control_screws_heaters_off_pending(), "a bed someone set to 60 C since is left alone");
    marker = fopen("marker", "w"); assert(marker); fputs("20\n", marker); fclose(marker);
    screws_marker_restore(0);
    sends = 0; screws_heaters_off_tick(&mqtt, 0, capture, 1, 20.0);
    expect(sends == 1, "a session at 20 C (below the 30 C the alignment session uses) is switched off after a restart");
    marker = fopen("marker", "w"); assert(marker); fclose(marker); /* the empty marker of earlier versions */
    screws_marker_restore(0);
    sends = 0; screws_heaters_off_tick(&mqtt, 0, capture, 1, 60.0);
    expect(sends == 1, "an empty marker from an earlier version still stands for 60 C");

    /* --- The history keeps the target of each measurement; older lines have none. --- */
    char *json = json_of_history_file("cc2-screw-history 1\nS 1762800000 1762800100 0\n"
                                      "M 1762800050 0 -1 0 15 0 -0.1 -0.2 -0.3 -0.4 0 0 0 0 70\n"
                                      "M 1762800090 2 -1 0 15 0 -0.1 -0.2 -0.3 -0.4 0 0 0 0\n");
    expect(json && strstr(json, "\"kind\":\"start\"") && strstr(json, "\"bed\":70}") &&
           strstr(json, "\"z\":{\"FL\":-0.100000,\"FR\":-0.200000,\"RR\":-0.300000,\"RL\":-0.400000}}") &&
           !strstr(json, "\"bed\":null"),
           "the history reports 70 C for a measurement that recorded it and no bed field for one that did not");
    free(json);

    /* --- The target travels with the jobs into the history, and is renewed. --- */
    time_t t0 = time(NULL); mqtt.last_message = t0;
    control_screws_reset(); /* a running session refuses a second start and any quick measurement */
    expect(control_build_script("screws:tram:start:70", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
           control_screws_bed() == 70, "the running session reports its 70 C target");
    control_screws_note_action("screws:tram:start:70"); control_screws_note_job(41);
    (void)control_screws_take_finished(NULL);
    (void)control_screws_tick(&mqtt, 1, 1, 41, 0, t0);
    control_screws_job job = {0};
    expect(control_screws_take_finished(&job) && job.kind == CONTROL_SCREWS_JOB_START && job.bed_set && job.bed == 70,
           "the completed start is reported with its 70 C target");
    screw_history_store h; memset(&h, 0, sizeof(h));
    const char *output = "probe at 27.900,25.950 is z=1\n// Result is z=-0.100000\n"
                         "probe at 227.150,25.950 is z=1\n// Result is z=-0.200000\n"
                         "probe at 227.150,225.500 is z=1\n// Result is z=-0.300000\n"
                         "probe at 27.900,225.500 is z=1\n// Result is z=-0.400000\n";
    expect(screw_history_record(&h, &job, output, strlen(output), t0) && h.s[0].m[0].bed == 70, "the history records 70 C");
    expect(screw_history_save(&h, "screw-history.txt"), "the history is saved");
    FILE *file = fopen("screw-history.txt", "r"); char text[1024] = ""; size_t n = fread(text, 1, sizeof(text) - 1, file); fclose(file);
    text[n] = 0;
    expect(strstr(text, " 0.000000 0.000000 0.000000 0.000000 70\n") != NULL, "the saved line ends with the 70 C target");
    screw_history_store loaded; assert(screw_history_load(&loaded, "screw-history.txt"));
    expect(loaded.count == 1 && loaded.s[0].m[0].bed == 70, "70 C is read back from the file");
    mqtt.last_message = t0 + SCREWS_BED_RENEW_S;
    sends = 0; screws_bed_renew_tick(&mqtt, 0, t0 + SCREWS_BED_RENEW_S, capture, 1, 70.0);
    expect(sends == 1 && !strcmp(sent, "M140 S70"), "the ten-minute renewal sends M140 S70");
    sends = 0; screws_bed_renew_tick(&mqtt, 0, t0 + 2 * SCREWS_BED_RENEW_S, capture, 1, 60.0);
    expect(sends == 0, "the renewal leaves a bed alone that someone has set to 60 C");
    screws_resumed = 0; screws_marker_present = 0; screws_marker_sync();
    file = fopen("marker", "r"); n = file ? fread(text, 1, sizeof(text) - 1, file) : 0; if (file) fclose(file); text[n] = 0;
    expect(!strcmp(text, "70\n"), "the restart marker holds the session's 70 C");
    mqtt.last_message = time(NULL);
    expect(control_build_script("screws:tram:end", &mqtt, script, sizeof(script), reason, sizeof(reason)) &&
           control_screws_heaters_off_pending(), "ending the session leaves the shutdown pending");
    screws_marker_sync();
    file = fopen("marker", "r"); n = file ? fread(text, 1, sizeof(text) - 1, file) : 0; if (file) fclose(file); text[n] = 0;
    expect(!strcmp(text, "70\n"), "the pending shutdown keeps the 70 C marker");
    control_screws_heaters_off_sent(); screws_marker_sync();
    expect(access("marker", F_OK) != 0, "the marker is removed once the heaters are off");

    mqtt.last_message = time(NULL); mqtt.machine_status = 1;
    expect(control_build_script("screws:measure:100", &mqtt, script, sizeof(script), reason, sizeof(reason)), "quick measurement at 100 C builds");
    control_screws_note_quick(51);
    (void)control_screws_tick(&mqtt, 1, 1, 51, 0, time(NULL));
    memset(&job, 0, sizeof(job));
    expect(control_screws_take_finished(&job) && job.kind == CONTROL_SCREWS_JOB_QUICK && job.bed_set && job.bed == 100,
           "the one-shot measurement is reported with its 100 C target");
    expect(control_screws_is_start("screws:tram:start:70") && control_screws_is_start("screws:tram:start") &&
           !control_screws_is_start("screws:tram:start:abc") && control_screws_is_quick("screws:measure:100") &&
           !control_screws_is_quick("screws:measurex"), "start and quick actions are recognised with and without a target");

    if (failures) { printf("%d FAIL\n", failures); return 1; }
    puts("PASS screw bed target: chosen target in every script, renewal, restart marker and history");
    return 0;
}
