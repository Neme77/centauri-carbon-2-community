#define main cc2_main_original
#define console_start pending_fixture_console_start
#define console_emergency_stop pending_fixture_emergency_stop
#include "../src/main.c"
#undef main
#undef console_start
#undef console_emergency_stop
#include <assert.h>

/* No printer process, heater, command socket or firmware executable is used. */
static int commands, emergency_calls;
int pending_fixture_emergency_stop(console_state *c, char *reason, size_t cap) {
    (void)c; (void)reason; (void)cap; emergency_calls++; return 0;
}
int pending_fixture_console_start(console_state *c, const char *script, char *reason, size_t cap) {
    (void)c; (void)script; (void)reason; (void)cap; commands++; return 0;
}
static const char boot_autosave[] =
    "#*# <---------------------- SAVE_CONFIG ---------------------->\n"
    "#*# DO NOT EDIT THIS BLOCK OR BELOW. The contents are auto-generated.\n#*#\n"
    "#*# [canvas_dev]\n#*# unload_fila_dist = 200\n#*#\n"
    "#*# [stepper_z]\n#*# position_endstop = 5.000000\n";
/* SAVE_CONFIG rebuilds the block from its boot-time object plus its own edits,
 * not the block currently on disk: elegoo/configfile.cpp cmd_SAVE_CONFIG. */
static const char native_saved[] =
    "#*# <---------------------- SAVE_CONFIG ---------------------->\n"
    "#*# DO NOT EDIT THIS BLOCK OR BELOW. The contents are auto-generated.\n#*#\n"
    "#*# [canvas_dev]\n#*# unload_fila_dist = 200\n#*#\n"
    "#*# [stepper_z]\n#*# position_endstop = 7.125000\n";
static char reply[4096];
static void put(const char *path, const char *text) {
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fwrite(text, 1, strlen(text), f) == strlen(text)); assert(!fclose(f));
}
static void get_pair(int pair[2]) { assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair)); }
static int finish(int pair[2]) {
    close(pair[0]); size_t used = 0; ssize_t n;
    while ((n = read(pair[1], reply + used, sizeof(reply) - 1 - used)) > 0) used += (size_t)n;
    reply[used] = 0; close(pair[1]); int status = 0; sscanf(reply, "HTTP/1.1 %d", &status); return status;
}
static int canvas(int op, mqtt_client *m, const char *body) {
    int pair[2]; get_pair(pair);
    if (op == 0) canvas_unload_response(pair[0]);
    else if (op == 1) canvas_unload_save_response(pair[0], m, body, strlen(body));
    else canvas_unload_restart_response(pair[0], m, "RESTART", 7);
    return finish(pair);
}

#ifndef CC2_REPRODUCE_267
static void blocked_writes(mqtt_client *m) {
    console_state c; console_init(&c, "/tmp/cc2-autosave-fixture-no-printer");
    const char *actions[] = {"pid:save", "pid:extruder:200", "pid:heater_bed:60"};
    for (size_t i = 0; i < sizeof(actions) / sizeof(actions[0]); i++) {
        int pair[2]; get_pair(pair); control_response(pair[0], &c, m, actions[i], strlen(actions[i]));
        assert(finish(pair) == 409 && strstr(reply, "needs a restart") && !commands);
    }
    int pair[2]; get_pair(pair);
    console_command_response(pair[0], &c, m, "save_config", 11);
    assert(finish(pair) == 409 && strstr(reply, "needs a restart") && !commands);
    get_pair(pair); plates_calibrate_response(pair[0], m, &c, "A\n60\n0", 6);
    assert(finish(pair) == 409 && strstr(reply, "needs a restart") && !commands && !plates_cal.stage);
    emergency_calls = 0;
    get_pair(pair); control_response(pair[0], &c, m, "system:emergency_stop", 21);
    assert(finish(pair) == 202);
    get_pair(pair); console_command_response(pair[0], &c, m, "M112", 4);
    assert(finish(pair) == 202 && emergency_calls == 2 && !commands);
    pthread_mutex_destroy(&c.lock);
}
static void check_file_kept(void) {
    size_t size = 0; char *text = plates_read_file(printer_autosave_path, AUTOSAVE_FILE_MAX, &size);
    assert(text && !strcmp(text, native_saved)); free(text);
}
static void plate_conflict(mqtt_client *m) {
    unlink(canvas_unload_pending_path);
    memset(&plates, 0, sizeof(plates)); plates.count = plates.measure_count = 1;
    plate_entry *p = &plates.plates[0]; plate_measure *measure = &plates.measures[0];
    strcpy(p->id, "1111111111111111"); strcpy(p->measure, "2222222222222222"); p->side = 'A';
    strcpy(measure->id, p->measure); strcpy(measure->plate, p->id); measure->temp = 60;
    strcpy(plates.pending, p->id);
    plate_mesh *mesh = &measure->mesh;
    mesh->x_count = mesh->y_count = 3; mesh->min_x = mesh->min_y = 6;
    mesh->max_x = mesh->max_y = 246; mesh->tension = 0.2; strcpy(mesh->algo, "lagrange");
    for (int i = 0; i < 9; i++) mesh->points[i] = 0.1;
    size_t length = 0; char *staged = autosave_with_mesh(native_saved, strlen(native_saved), "default", mesh, &length);
    assert(staged); put(printer_autosave_path, staged); free(staged);
    assert(plates_pending_intact()); blocked_writes(m);
    assert(canvas(1, m, "300") == 409 && strstr(reply, "plate mesh change"));
    put(printer_autosave_path, native_saved);
    assert(!plates_pending_intact() && !plates_reboot_guard());
    int pair[2]; get_pair(pair); plates_get_response(pair[0]);
    assert(finish(pair) == 200 && strstr(reply, "\"pending_conflict\":true"));
    check_file_kept(); memset(&plates, 0, sizeof(plates));
}
#endif

int main(void) {
    char directory[] = "/tmp/cc2-pending-autosave-XXXXXX"; assert(mkdtemp(directory)); assert(!chdir(directory));
    printer_config_path = "printer.cfg"; printer_autosave_path = "autosave.cfg";
    boot_id_path = "boot-id"; canvas_unload_pending_path = "pending";
    canvas_unload_program = "missing-fixture-printer";
    put(boot_id_path, "fixture-boot-one\n"); put(printer_config_path, "[canvas_dev]\nunload_fila_dist = 80\n");
    put(printer_autosave_path, boot_autosave);
    mqtt_client m = {0}; m.connected = m.registered = m.have_machine_status = 1;
    m.machine_status = 1; m.last_message = time(NULL);
    telemetry.fd = 0; telemetry.ready = 1; clock_gettime(CLOCK_MONOTONIC, &telemetry.last_rx);
    assert(canvas(1, &m, "300") == 200 && strstr(reply, "\"pending_restart\":true"));
    put(printer_autosave_path, native_saved);
    assert(canvas(0, &m, NULL) == 200 && strstr(reply, "\"override\":200"));
#ifdef CC2_REPRODUCE_267
    assert(strstr(reply, "\"pending_restart\":true"));
    puts("REPRODUCED #267: native SAVE_CONFIG discards the staged 300 mm edit while the original API still claims it is pending");
#else
    assert(strstr(reply, "\"pending_restart\":false") && strstr(reply, "\"pending_conflict\":true") &&
           strstr(reply, "\"pending_value\":300"));
    assert(canvas(2, &m, NULL) == 409 && !reboot_pending); check_file_kept(); blocked_writes(&m);
    /* Explicit re-save preserves the native Z calibration and repairs the pending edit. */
    assert(canvas(1, &m, "300") == 200 && strstr(reply, "\"pending_conflict\":false"));
    assert(canvas_unload_pending_intact());
    size_t length = 0; char *text = plates_read_file(printer_autosave_path, AUTOSAVE_FILE_MAX, &length);
    assert(text && strstr(text, "position_endstop = 7.125000") && strstr(text, "unload_fila_dist = 300")); free(text);
    assert(canvas(2, &m, NULL) == 202 && reboot_pending);
    /* A native save during the two-second reboot delay must not apply the wrong setting. */
    put(printer_autosave_path, native_saved); assert(!plates_reboot_guard());
    reboot_pending = 0; reboot_guard = NULL; canvas_unload_restart_requested = 0;
    /* Restoring the default is also an owned edit, and must detect reintroduced overrides. */
    assert(canvas(1, &m, "default") == 200 && canvas_unload_pending_intact());
    put(printer_autosave_path, native_saved);
    assert(canvas(0, &m, NULL) == 200 && strstr(reply, "\"pending_conflict\":true") && strstr(reply, "\"pending_value\":0"));
    check_file_kept();
    assert(canvas(1, &m, "default") == 200 && canvas_unload_pending_intact());
    /* A new CC2 process can recover the expectation; legacy tracking fails closed. */
    put(canvas_unload_pending_path, "fixture-boot-one");
    assert(canvas(0, &m, NULL) == 200 && strstr(reply, "\"pending_conflict\":true") && strstr(reply, "\"pending_value\":null"));
    assert(canvas(2, &m, NULL) == 409 && !reboot_pending);
    assert(canvas(1, &m, "300") == 200);
    assert(!mkdir("autosave.cfg.cc2-new", 0700));
    assert(canvas(1, &m, "250") == 500); assert(canvas_unload_pending_intact());
    assert(canvas(0, &m, NULL) == 200 && strstr(reply, "\"pending_value\":300"));
    assert(!rmdir("autosave.cfg.cc2-new"));
    put(boot_id_path, "fixture-boot-two\n"); assert(!canvas_unload_pending());
    plate_conflict(&m);
    puts("PASS #267: owned pull-back/default and mesh conflict detection, retained native Z save, guarded PID/mesh/console, explicit re-save, reboot-window refusal, legacy marker and failed-write recovery");
#endif
    return 0;
}
