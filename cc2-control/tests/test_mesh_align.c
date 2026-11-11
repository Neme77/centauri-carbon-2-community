#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

/* Mesh-guided screw alignment session: nothing is sent before CONFIRM, a rejected or failed
 * command is not repeated behind the caller's back, and however the session ends the heaters
 * are switched off through the deferred shutdown unless a print or another bed target owns
 * them. No sleeps: the test moves the session's and the job's clocks itself. */

/* The printer service as plates.h sees it: scripts it ran, and the side slot in memory. */
static plate_mesh memory_a;
static char scripts[1024];
static int uds_down;
static int fake_uds(const char *query, char **reply, size_t *length) {
    if (uds_down) return -1;
    json_builder b = {malloc(16384), 0, 16384, 0};
    assert(b.data);
    if (strstr(query, "gcode/script")) {
        const char *script = strstr(query, "\"script\":\"") + 10;
        size_t used = strlen(scripts);
        snprintf(scripts + used, sizeof(scripts) - used, "%s%.*s", used ? "|" : "", (int)(strchr(script, '"') - script), script);
        /* SAVE copies the slot to a profile and writes autosave.cfg at once. */
        const char *save = strstr(script, "SAVE=");
        if (save && save < strchr(script, '"')) {
            char name[40]; snprintf(name, sizeof(name), "%.*s", (int)(strchr(save, '"') - save - 5), save + 5);
            size_t file_length, new_length;
            char *file = plates_read_file(printer_autosave_path, AUTOSAVE_FILE_MAX, &file_length); assert(file);
            char *updated = autosave_with_mesh(file, file_length, name, &memory_a, &new_length); assert(updated);
            FILE *f = fopen(printer_autosave_path, "wb"); assert(f);
            assert(fwrite(updated, 1, new_length, f) == new_length); fclose(f);
            free(file); free(updated);
        }
        json_builder_printf(&b, "{\"id\":204,\"result\":{}}");
    } else {
        assert(strstr(query, "\"bed_mesh\":[\"profiles\"]"));
        const plate_mesh *m = &memory_a;
        json_builder_printf(&b, "{\"id\":7,\"result\":{\"status\":{\"bed_mesh\":{\"profiles\":{\"default\":{\"points\":[");
        for (int r = 0; r < m->y_count; ++r) {
            json_builder_printf(&b, "%s[", r ? "," : "");
            for (int c = 0; c < m->x_count; ++c) json_builder_printf(&b, "%s%.17g", c ? "," : "", m->points[r * m->x_count + c]);
            json_builder_printf(&b, "]");
        }
        json_builder_printf(&b, "],\"mesh_params\":{\"min_x\":%.1f,\"max_x\":%.1f,\"min_y\":%.1f,\"max_y\":%.1f,"
            "\"x_count\":%d,\"y_count\":%d,\"mesh_x_pps\":%d,\"mesh_y_pps\":%d,\"algo\":\"%s\",\"tension\":%.1f}}}}}}}",
            m->min_x, m->max_x, m->min_y, m->max_y, m->x_count, m->y_count, m->x_pps, m->y_pps, m->algo, m->tension);
    }
    assert(!b.failed);
    *reply = b.data; *length = b.length;
    return 0;
}

/* A synthetic 11 x 11 mesh: a plane with a slope, no measured values. */
static plate_mesh synthetic(double tilt) {
    plate_mesh m; memset(&m, 0, sizeof(m));
    m.x_count = m.y_count = 11; m.x_pps = m.y_pps = 3;
    m.min_x = m.min_y = 6; m.max_x = m.max_y = 246; m.tension = 0.2;
    snprintf(m.algo, sizeof(m.algo), "bicubic");
    for (int i = 0; i < 121; ++i) m.points[i] = round((0.1 + tilt * (i % 11) / 10.0 - tilt * (i / 11) / 20.0) * 1e6) / 1e6;
    return m;
}
/* What the firmware's calibration leaves: the slot in the file and in memory. */
static void printer_measured(const plate_mesh *m) {
    char section[4096], text[8192];
    assert(autosave_format(section, sizeof(section), "default", m) > 0);
    snprintf(text, sizeof(text), "\n\n" AUTOSAVE_MARKER "\n#*# DO NOT EDIT THIS BLOCK OR BELOW. The contents are auto-generated.\n#*#\n%s", section);
    FILE *f = fopen(printer_autosave_path, "wb"); assert(f);
    assert(fwrite(text, 1, strlen(text), f) == strlen(text)); fclose(f);
    memory_a = *m;
}

/* The console as the calibration job sees it: a command finishes at once, unless the console refuses it. */
static console_state console;
static char console_log[512];
static int console_ok = 1, console_refuses;
static int fake_console(console_state *c, const char *command) {
    if (console_refuses) return -1;
    size_t used = strlen(console_log);
    snprintf(console_log + used, sizeof(console_log) - used, "%s%s", used ? "|" : "", command);
    pthread_mutex_lock(&c->lock);
    c->generation++; c->busy = 0; c->completed = 1; c->success = console_ok;
    snprintf(c->command, sizeof(c->command), "%s", command);
    c->output_len = (size_t)snprintf(c->output, sizeof(c->output), "%s",
        !strncmp(command, "BED_MESH_CALIBRATE ", 19) ?
        "{\"report\":{\"message\":\"// Mesh Bed Leveling Complete\"}}\n{\"id\":101,\"result\":{}}\n" : "{\"id\":101,\"result\":{}}\n");
    pthread_mutex_unlock(&c->lock);
    return 0;
}
static int beds_off;
static int fake_bed_off(void) { beds_off++; return 0; }
static int fake_bed_target(int temp) { (void)temp; return 0; }

/* The deferred shutdown as the main loop delivers it. */
static int shutdowns, shutdown_refused;
static int fake_send(const char *script) {
    assert(!strcmp(script, "TURN_OFF_HEATERS"));
    if (shutdown_refused > 0) { shutdown_refused--; return -1; }
    shutdowns++;
    return 0;
}

static mqtt_client mqtt;
static void fresh(void) {
    mqtt.connected = mqtt.registered = 1; mqtt.have_machine_status = 1; mqtt.machine_status = 1;
    mqtt.last_message = time(NULL);
    telemetry.fd = 0; telemetry.ready = 1; clock_gettime(CLOCK_MONOTONIC, &telemetry.last_rx);
}
static void bed(double now, double target) {
    telemetry.values[U_BT] = now; telemetry.values[U_BG] = target;
    telemetry.present |= (UINT32_C(1) << U_BT) | (UINT32_C(1) << U_BG);
}
/* One main-loop pass: the calibration job, the tram session, the alignment session, the shutdown retry. */
static void loop(int emergency) {
    plates_calibration_tick(&mqtt, &console);
    pthread_mutex_lock(&console.lock);
    int completed = console.completed, success = console.success, busy = console.busy;
    unsigned long generation = console.generation;
    pthread_mutex_unlock(&console.lock);
    time_t now = time(NULL);
    (void)control_screws_tick(&mqtt, completed, success, generation, emergency, now);
    align_tick(&mqtt, emergency, now);
    screws_heaters_off_tick(&mqtt, busy, fake_send);
}

static char response[4096];
enum { GET, MEASURE, SAVE, END, CALIBRATE, CONTROL, CREATE };
static int call(int handler, const char *body) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    size_t length = body ? strlen(body) : 0;
    switch (handler) {
        case GET: align_get_response(pair[0]); break;
        case MEASURE: align_measure_response(pair[0], &mqtt, &console, body, length); break;
        case SAVE: align_save_response(pair[0], &mqtt, body, length); break;
        case END: align_end_response(pair[0], &mqtt, body, length); break;
        case CALIBRATE: align_plates_calibrate_response(pair[0], &mqtt, &console, body, length); break;
        case CONTROL: control_response(pair[0], &console, &mqtt, body, length); break;
        case CREATE: plates_create_response(pair[0], &mqtt, body, length); break;
    }
    close(pair[0]);
    size_t used = 0; ssize_t n;
    while ((n = recv(pair[1], response + used, sizeof(response) - 1 - used, 0)) > 0) used += (size_t)n;
    response[used] = 0; close(pair[1]);
    return atoi(strchr(response, ' ') + 1);
}

#define REQUEST "A\n60\n2\n\n4"
#define CONFIRMED REQUEST "\nCONFIRM"

/* Everything a new test starts from: an idle, homed, cold printer and no session. */
static void reset(void) {
    align_finish("ended", 0);
    plates_cal_end(&mqtt, "", "", "", 0);
    control_screws_heaters_taken();
    control_screws_reset();
    memset(&mqtt, 0, sizeof(mqtt)); fresh();
    snprintf(mqtt.homed_axes, sizeof(mqtt.homed_axes), "xyz");
    atomic_store(&console.emergency_sent, 0);
    pthread_mutex_lock(&console.lock); console.busy = 0; pthread_mutex_unlock(&console.lock);
    console_ok = 1; console_refuses = 0; uds_down = 0; shutdown_refused = 0;
    console_log[0] = scripts[0] = 0; beds_off = shutdowns = 0;
    bed(25, 0);
}
/* Runs the confirmed measurement up to the given stage of its calibration job. */
static void measure_until(int stage) {
    assert(call(MEASURE, CONFIRMED) == 202);
    assert(align.active && align.measuring && plates_cal.stage == CAL_HEATING);
    if (stage == CAL_HEATING) return;
    bed(60, 60); loop(0); assert(plates_cal.stage == CAL_SOAKING);
    if (stage == CAL_SOAKING) return;
    plates_cal.soak_until = monotonic_ms() - 1;
    plates_calibration_tick(&mqtt, &console); assert(plates_cal.stage == CAL_PROBING);
}
/* One complete round: the firmware saved `mesh` in the slot. */
static void measure(const plate_mesh *mesh) {
    measure_until(CAL_PROBING);
    printer_measured(mesh);
    loop(0);
    assert(!plates_cal.stage && !strcmp(plates_cal.result, "done") && align.active && !align.measuring && align.measured);
}

int main(void) {
    char directory[] = "/tmp/cc2-mesh-align-XXXXXX";
    assert(mkdtemp(directory));
    static char autosave[128], library[128], service[128];
    snprintf(autosave, sizeof(autosave), "%s/autosave.cfg", directory);
    snprintf(library, sizeof(library), "%s/bed-plates.json", directory);
    snprintf(service, sizeof(service), "%s/elegoo_uds", directory);
    FILE *socket_file = fopen(service, "wb"); assert(socket_file); fclose(socket_file);
    printer_autosave_path = autosave; plates_path = library; object_query_path = service;
    plates_uds = fake_uds; plates_console = fake_console; plates_bed_off = fake_bed_off; plates_bed_target = fake_bed_target;
    uds_init(&telemetry);
    console_init(&console, "/tmp/unused");
    recovery_console = &console;
    plate_mesh first = synthetic(0.4), second = synthetic(0.1), third = synthetic(0.02);
    printer_measured(&first);
    plates_load(); assert(plates_available);
    reset();
    assert(call(CREATE, "A\nTextured\n0.000") == 201);
    char plate[PLATE_ID_LEN + 1]; snprintf(plate, sizeof(plate), "%s", plates.plates[0].id);
    assert(call(CREATE, "B\nSmooth\n0.000") == 201);
    char other[PLATE_ID_LEN + 1]; snprintf(other, sizeof(other), "%s", plates.plates[1].id);
    char body[128];

    /* Without CONFIRM a request only lists its commands: nothing reaches the console or the printer
     * service, and no session exists. An unhomed printer is homed first. */
    reset(); mqtt.homed_axes[0] = 0;
    assert(call(MEASURE, REQUEST) == 200);
    assert(strstr(response, "{\"confirm_required\":true,\"commands\":[\"G28\",\"M140 S60\","
                            "\"BED_MESH_CALIBRATE PROFILE=default BED_TEMP=60\"],\"round\":1,\"rounds\":4,\"soak\":2}"));
    assert(!align.active && !plates_cal.stage && !console_log[0] && !scripts[0] && !control_screws_heaters_off_pending());
    assert(call(GET, NULL) == 200 && strstr(response, "{\"active\":false,\"round\":0,") && strstr(response, "\"ended\":\"\",\"heaters_off_pending\":false"));
    assert(call(SAVE, plate) == 409 && call(END, "") == 200 && !console_log[0] && !scripts[0]);
    snprintf(mqtt.homed_axes, sizeof(mqtt.homed_axes), "xyz");
    assert(call(MEASURE, REQUEST) == 200 && strstr(response, "\"commands\":[\"M140 S60\",\"BED_MESH_CALIBRATE"));
    /* Malformed requests, and anything but CONFIRM as the last line, are refused. */
    assert(call(MEASURE, "A\n60\n2\n\n0") == 400 && call(MEASURE, "A\n60\n2\n\n11") == 400 && call(MEASURE, "C\n60\n2\n\n4") == 400);
    assert(call(MEASURE, "A\n20\n2\n\n4") == 400 && call(MEASURE, "A\n60\n61\n\n4") == 400 && call(MEASURE, "A\n60\n2") == 400);
    assert(call(MEASURE, REQUEST "\nYES") == 400 && call(MEASURE, "A\n60\n2\nnot-a-nozzle\n4") == 400);
    assert(call(SAVE, "x") == 400 && call(END, "NOW") == 400 && !align.active && !console_log[0]);

    /* A confirmed measurement is the calibration job; its start takes the heaters from a shutdown that
     * an earlier session left pending. Round by round up to the saved mesh. */
    reset(); control_screws_request_heaters_off();
    measure(&second);
    assert(!strcmp(console_log, "M140 S60|BED_MESH_CALIBRATE PROFILE=default BED_TEMP=60"));
    assert(align.round == 1 && !control_screws_heaters_off_pending() && !shutdowns);
    assert(call(GET, NULL) == 200 && strstr(response, "{\"active\":true,\"round\":1,\"rounds\":4,\"measuring\":false,\"measured\":true,\"side\":\"A\",\"temp\":60,\"soak\":2,"));
    assert(strstr(response, "\"ended\":\"\",\"heaters_off_pending\":false,\"calibration\":{\"stage\":\"off\"") && strstr(response, "\"result\":\"done\""));
    /* The next round must repeat the session's settings, and other jobs stay out while it is active. */
    assert(call(MEASURE, "A\n70\n2\n\n4\nCONFIRM") == 409 && strstr(response, "other settings"));
    assert(call(MEASURE, "A\n60\n5\n\n4\nCONFIRM") == 409 && call(CALIBRATE, "A\n60\n0") == 409);
    assert(call(CONTROL, "screws:tram:start") == 409 && strstr(response, "mesh-guided alignment"));
    assert(call(CONTROL, "screws:measure") == 409 && align.round == 1 && !plates_cal.stage);
    console_log[0] = 0;
    assert(call(MEASURE, REQUEST) == 200 && strstr(response, "\"round\":2,\"rounds\":4") && !console_log[0]);
    measure(&third);
    assert(align.round == 2);
    /* The save is listed first, then made through the plate library; the session ends and asks for
     * the heaters to go off. */
    snprintf(body, sizeof(body), "%s", other);
    assert(call(SAVE, body) == 409 && strstr(response, "other side"));
    assert(call(SAVE, "0123456789abcdef") == 404);
    scripts[0] = 0;
    assert(call(SAVE, plate) == 200);
    assert(strstr(response, "\"commands\":[\"BED_MESH_PROFILE LOAD=default\",\"BED_MESH_PROFILE SAVE=cc2_<id>\","
                            "\"SET_GCODE_OFFSET Z=0.000\",\"TURN_OFF_HEATERS\"],\"replaces\":false,\"temp\":60}"));
    assert(!scripts[0] && align.active && !plate_measure_count(plate) && !control_screws_heaters_off_pending());
    /* A save that fails keeps the session, so it can be repeated; nothing is switched off yet. */
    snprintf(body, sizeof(body), "%s\nCONFIRM", plate);
    uds_down = 1;
    assert(call(SAVE, body) == 503 && align.active && align.measured && !plate_measure_count(plate));
    assert(!control_screws_heaters_off_pending());
    uds_down = 0;
    assert(call(SAVE, body) == 200 && strstr(response, "\"saved\":true"));
    assert(plate_measure_count(plate) == 1 && plate_mesh_equal(&plate_base(plate_find(plate))->mesh, &third, 1));
    assert(plate_base(plate_find(plate))->temp == 60 && !strcmp(plates.current, plate));
    assert(strstr(scripts, "BED_MESH_PROFILE LOAD=default\\nBED_MESH_PROFILE SAVE=cc2_") && strstr(scripts, "SET_GCODE_OFFSET Z=0.000"));
    assert(!align.active && !strcmp(align.ended, "saved") && control_screws_heaters_off_pending());
    bed(60, 60); loop(0);
    assert(shutdowns == 1 && !control_screws_heaters_off_pending());
    loop(0); assert(shutdowns == 1);
    assert(call(GET, NULL) == 200 && strstr(response, "\"active\":false") && strstr(response, "\"ended\":\"saved\""));
    /* A second save of the same temperature replaces that measurement and names its profile. */
    reset(); measure(&second);
    assert(call(SAVE, plate) == 200 && strstr(response, "\"replaces\":true") && !strstr(response, "<id>"));
    assert(call(END, "CONFIRM") == 200);

    /* The console refuses the first command: no session, nothing queued. The repeated request sends
     * it once. In a running session the refused round leaves the session waiting as before. */
    reset(); console_refuses = 1;
    assert(call(MEASURE, CONFIRMED) == 409 && strstr(response, "Another printer command"));
    assert(!align.active && !plates_cal.stage && !console_log[0] && !control_screws_heaters_off_pending());
    loop(0); assert(!shutdowns);
    console_refuses = 0;
    measure(&second);
    assert(!strcmp(console_log, "M140 S60|BED_MESH_CALIBRATE PROFILE=default BED_TEMP=60"));
    console_refuses = 1; console_log[0] = 0;
    assert(call(MEASURE, CONFIRMED) == 409 && align.active && !align.measuring && align.measured && align.round == 1);
    loop(0); assert(align.active && !console_log[0] && !shutdowns);
    console_refuses = 0;
    /* A printer that is not idle, a tram session or a latched emergency stop refuses a round as well. */
    mqtt.machine_status = 3;
    assert(call(MEASURE, CONFIRMED) == 409 && strstr(response, "must be idle") && align.active && !console_log[0]);
    fresh();
    atomic_store(&console.emergency_sent, 1);
    assert(call(MEASURE, CONFIRMED) == 409 && strstr(response, "emergency stop") && !console_log[0]);
    atomic_store(&console.emergency_sent, 0);
    measure(&third);
    assert(align.round == 2 && !strcmp(console_log, "M140 S60|BED_MESH_CALIBRATE PROFILE=default BED_TEMP=60"));
    assert(call(END, "CONFIRM") == 200);

    /* A measurement that fails ends the session; the shutdown stays pending while the printer is busy
     * or the send is refused, and is delivered once. */
    reset();
    measure_until(CAL_SOAKING);
    plates_cal.soak_until = monotonic_ms() - 1; console_ok = 0;
    loop(0);                                    /* BED_MESH_CALIBRATE is sent and fails */
    loop(0);
    assert(!plates_cal.stage && !strcmp(plates_cal.result, "failed") && !strcmp(plates_cal.error, "probing"));
    assert(!align.active && !strcmp(align.ended, "failed"));
    assert(call(GET, NULL) == 200 && strstr(response, "\"ended\":\"failed\"") && strstr(response, "\"error\":\"probing\""));
    assert(call(SAVE, plate) == 409 && strstr(response, "No alignment session"));
    reset(); measure_until(CAL_SOAKING);
    plates_cal.soak_until = monotonic_ms() - 1; console_ok = 0;
    plates_calibration_tick(&mqtt, &console); plates_calibration_tick(&mqtt, &console);
    assert(!plates_cal.stage);
    mqtt.machine_status = 5; shutdown_refused = 1;  /* the printer still reports its levelling state */
    loop(0); assert(!align.active && control_screws_heaters_off_pending() && !shutdowns);
    fresh(); loop(0); assert(control_screws_heaters_off_pending() && !shutdowns && shutdown_refused == 0);
    pthread_mutex_lock(&console.lock); console.busy = 1; pthread_mutex_unlock(&console.lock);
    loop(0); assert(control_screws_heaters_off_pending() && !shutdowns);
    pthread_mutex_lock(&console.lock); console.busy = 0; pthread_mutex_unlock(&console.lock);
    loop(0); assert(shutdowns == 1 && !control_screws_heaters_off_pending());
    loop(0); assert(shutdowns == 1);
    /* Homing that fails before anything was heated ends the session the same way. */
    reset(); mqtt.homed_axes[0] = 0; console_ok = 0;
    assert(call(MEASURE, CONFIRMED) == 202 && !strcmp(console_log, "G28"));
    loop(0);
    assert(!align.active && !strcmp(align.ended, "failed") && !strcmp(plates_cal.error, "homing") && shutdowns == 1);

    /* The end is listed first. Confirmed while the bed heats or soaks it stops the job, which switches
     * off the bed it owns, and the deferred shutdown follows. Once probing has begun it is refused. */
    reset(); measure_until(CAL_SOAKING);
    assert(call(END, "") == 200 && strstr(response, "{\"confirm_required\":true,\"commands\":[\"M140 S0\",\"TURN_OFF_HEATERS\"],\"measuring\":true}"));
    assert(align.active && plates_cal.stage == CAL_SOAKING && !beds_off && !control_screws_heaters_off_pending());
    assert(call(END, "CONFIRM") == 200 && strstr(response, "\"ended\":true"));
    assert(!align.active && !strcmp(align.ended, "ended") && !plates_cal.stage && !strcmp(plates_cal.result, "cancelled") && beds_off == 1);
    assert(control_screws_heaters_off_pending());
    bed(60, 0); loop(0);
    assert(shutdowns == 1 && !control_screws_heaters_off_pending() && !strstr(console_log, "BED_MESH_CALIBRATE"));
    reset(); measure_until(CAL_PROBING);
    assert(call(END, "CONFIRM") == 409 && strstr(response, "emergency stop") && align.active && align.measuring);
    printer_measured(&second); loop(0);
    assert(align.active && align.round == 1);
    /* Between rounds only the shutdown is listed, and an end without a session sends nothing. */
    assert(call(END, "") == 200 && strstr(response, "\"commands\":[\"TURN_OFF_HEATERS\"],\"measuring\":false"));
    assert(call(END, "CONFIRM") == 200 && control_screws_heaters_off_pending());
    loop(0); assert(shutdowns == 1);
    assert(call(END, "CONFIRM") == 200 && !control_screws_heaters_off_pending());
    loop(0); assert(shutdowns == 1);
    /* The job cancelled from the Mesh tab ends the session too. */
    reset(); measure_until(CAL_SOAKING);
    plates_cal_end(&mqtt, "cancelled", "", "", 1);
    loop(0);
    assert(!align.active && !strcmp(align.ended, "cancelled") && beds_off == 1 && shutdowns == 1);
    loop(0); assert(shutdowns == 1);

    /* The page went away: a session nobody continues ends after the timeout and switches the heaters
     * off. A measurement that still runs is not cut short by it. */
    reset(); measure(&second);
    bed(60, 60);
    align.last -= SCREWS_SESSION_TIMEOUT_S - 5; loop(0);
    assert(align.active && !shutdowns);
    assert(call(GET, NULL) == 200 && strstr(response, "\"idle_left\":") && !strstr(response, "\"idle_left\":0,"));
    align.last -= 10; loop(0);
    assert(!align.active && !strcmp(align.ended, "timeout") && shutdowns == 1 && !control_screws_heaters_off_pending());
    reset(); measure_until(CAL_SOAKING);
    align.last -= 2 * SCREWS_SESSION_TIMEOUT_S; loop(0);
    assert(align.active && align.measuring && plates_cal.stage == CAL_SOAKING && !shutdowns);
    plates_cal.soak_until = monotonic_ms() - 1; plates_calibration_tick(&mqtt, &console);
    printer_measured(&second); loop(0);
    assert(align.active && align.round == 1 && time(NULL) - align.last < 5);
    loop(0); assert(align.active && !shutdowns);
    assert(call(END, "CONFIRM") == 200);

    /* A print that starts between two rounds owns the heaters: the session ends without a shutdown,
     * and a shutdown already pending is dropped as well. */
    reset(); measure(&second);
    mqtt.machine_status = 2; bed(60, 60);
    loop(0);
    assert(!align.active && !strcmp(align.ended, "print") && !control_screws_heaters_off_pending() && !shutdowns);
    fresh(); loop(0); assert(!shutdowns);
    reset(); measure(&second);
    mqtt.machine_status = 3;                    /* not idle yet: the shutdown waits */
    assert(call(END, "CONFIRM") == 200 && control_screws_heaters_off_pending());
    loop(0); assert(!shutdowns);
    mqtt.machine_status = 2; loop(0);
    assert(!control_screws_heaters_off_pending());
    fresh(); loop(0); assert(!shutdowns);
    /* A print that starts while the bed soaks ends the job and the session; nothing is switched off. */
    reset(); measure_until(CAL_SOAKING);
    mqtt.machine_status = 2;
    loop(0); loop(0);
    assert(!align.active && !plates_cal.stage && !strcmp(plates_cal.error, "busy") && !beds_off && !shutdowns);
    assert(!control_screws_heaters_off_pending() && !strstr(console_log, "BED_MESH_CALIBRATE"));

    /* A bed target someone else set meanwhile is theirs: the session ends without touching it. */
    reset(); measure(&second);
    bed(72, 80);
    assert(call(END, "CONFIRM") == 200 && !control_screws_heaters_off_pending());
    loop(0); assert(!shutdowns);
    reset(); measure(&second);
    bed(72, 80); align.last -= SCREWS_SESSION_TIMEOUT_S + 5; loop(0);
    assert(!align.active && !strcmp(align.ended, "timeout") && !shutdowns);

    /* An emergency stop ends the session; the shutdown is delivered once the printer is idle again. */
    reset(); measure(&second);
    bed(60, 60); mqtt.machine_status = 14;
    loop(1);
    assert(!align.active && !strcmp(align.ended, "estop") && control_screws_heaters_off_pending() && !shutdowns);
    fresh(); loop(0); assert(shutdowns == 1);

    /* The session may run only as many measurements as it was started with. */
    reset();
    assert(call(MEASURE, "A\n60\n0\n\n2\nCONFIRM") == 202);
    bed(60, 60); loop(0); loop(0); printer_measured(&second); loop(0);
    assert(align.round == 1 && align.rounds == 2);
    assert(call(MEASURE, "A\n60\n0\n\n2\nCONFIRM") == 202);
    loop(0); loop(0); printer_measured(&third); loop(0);
    assert(align.round == 2 && align.active && align.measured);
    console_log[0] = 0;
    assert(call(MEASURE, "A\n60\n0\n\n2\nCONFIRM") == 409 && strstr(response, "all its measurements") && !console_log[0]);
    assert(call(MEASURE, "A\n60\n0\n\n2") == 409 && align.active);
    snprintf(body, sizeof(body), "%s\nCONFIRM", plate);
    assert(call(SAVE, body) == 200 && !align.active);

    /* A tram session keeps the alignment out, and a calibration without a session still works. */
    reset();
    control_screws_reset();
    assert(call(CALIBRATE, "A\n60\n0") == 202 && plates_cal.stage == CAL_HEATING && !align.active);
    plates_cal_end(&mqtt, "cancelled", "", "", 0);

    console_destroy(&console);
    puts("PASS mesh-guided alignment session: listed before sent, no repeat after a refusal, failure, end, timeout, print, foreign bed target and emergency stop");
    return 0;
}
