#define main cc2_main_original
#define mqtt_start_print mock_mqtt_start_print
#define mqtt_prepare_timelapse mock_mqtt_prepare_timelapse
#include "../src/main.c"
#undef main
#undef mqtt_start_print
#undef mqtt_prepare_timelapse
#include <assert.h>

/* How the print queue follows a copy from the start request to its end, from the MQTT cache and
 * the UDS telemetry alone; what it decides on stale or foreign evidence; the guards of its HTTP
 * handlers while a copy runs; and the automatic policy, with the MQTT start mocked away. */
static mqtt_client machine;
static int starts_sent;
static char start_file[PATH_MAX_LOCAL];

int mock_mqtt_start_print(mqtt_client *c, const char *media, const char *filename, const int *tools, const int *trays,
                          size_t count, char layout, int bedlevel_force, int timelapse) {
    (void)c; (void)media; (void)tools; (void)trays; (void)count; (void)layout; (void)bedlevel_force; (void)timelapse;
    starts_sent++;
    snprintf(start_file, sizeof(start_file), "%s", filename);
    return 0;
}
int mock_mqtt_prepare_timelapse(mqtt_client *c, int enabled) { (void)c; (void)enabled; return -1; }
static int fake_uds(const char *request, char **reply, size_t *length) {
    (void)request;
    *reply = strdup("{\"id\":1001,\"result\":{}}"); *length = strlen(*reply);
    return *reply ? 0 : -1;
}

static void printer_says(int status, const char *mqtt_state, const char *file, const char *uuid) {
    machine.connected = machine.registered = machine.have_machine_status = 1;
    machine.machine_status = status;
    snprintf(machine.print_state, sizeof(machine.print_state), "%s", mqtt_state);
    snprintf(machine.filename, sizeof(machine.filename), "%s", file);
    snprintf(machine.uuid, sizeof(machine.uuid), "%s", uuid);
    machine.last_message = time(NULL);
}

static void telemetry_says(const char *state, const char *file) {
    telemetry.fd = 99; telemetry.ready = 1; telemetry.have_print_state = 1;
    snprintf(telemetry.print_state, sizeof(telemetry.print_state), "%s", state);
    telemetry.have_filename = file != NULL;
    if (file) snprintf(telemetry.filename, sizeof(telemetry.filename), "%s", file);
    telemetry.present = 0;
    clock_gettime(CLOCK_MONOTONIC, &telemetry.last_rx);
}
static void telemetry_stale(void) { telemetry.last_rx.tv_sec -= 30; }
static void telemetry_heating(double bed_target) { telemetry.present |= 1u << U_BG; telemetry.values[U_BG] = bed_target; }

static queue_item *job(int id, int copies, int done) {
    queue_item *it = &queue.items[queue.count++];
    memset(it, 0, sizeof(*it));
    it->id = id; snprintf(it->storage, sizeof(it->storage), "internal");
    snprintf(it->path, sizeof(it->path), "part.gcode"); it->side = 'A';
    snprintf(it->leveling, sizeof(it->leveling), "saved");
    it->copies = copies; it->done = done; it->estimated_seconds = -1; it->filament_grams = -1; it->duration_baseline = -1;
    snprintf(it->state, sizeof(it->state), "pending");
    if (queue.next_id <= id) queue.next_id = id + 1;
    return it;
}

static void requested(queue_item *it) {
    snprintf(it->state, sizeof(it->state), "starting");
    it->started_ms = it->requested_ms = monotonic_ms(); it->job_uuid[0] = 0; it->native_seen = 0;
    queue.active_id = it->id;
}

/* The printer runs the copy: MQTT and the printer service both show the file printing. */
static void copy_starts(queue_item *it, const char *uuid) {
    requested(it);
    printer_says(2, "printing", "part.gcode", uuid); telemetry_says("printing", "local/part.gcode");
    queue_tick(&machine);
    assert(!strcmp(it->state, "printing") && !strcmp(it->job_uuid, uuid) && it->native_seen);
}

/* HTTP handlers through a socket pair: the status line of the reply, and its body. */
static int pair_[2];
static char reply_body[16384];
static int begin(void) { assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair_)); return pair_[0]; }
static int finish(void) {
    close(pair_[0]);
    size_t used = 0; ssize_t n;
    while (used + 1 < sizeof(reply_body) && (n = recv(pair_[1], reply_body + used, sizeof(reply_body) - 1 - used, 0)) > 0) used += (size_t)n;
    close(pair_[1]); reply_body[used] = 0;
    assert(used > 0);
    return atoi(strchr(reply_body, ' ') + 1);
}

int main(void) {
    char dir[] = "/tmp/cc2-queue-XXXXXX";
    assert(mkdtemp(dir));
    static char file[640], internal[640], gcode[700], autosave[640];
    snprintf(file, sizeof(file), "%s/print-queue.json", dir); queue_path = file;
    snprintf(internal, sizeof(internal), "%s/internal", dir); assert(mkdir(internal, 0755) == 0); gcode_internal_root = internal;
    snprintf(gcode, sizeof(gcode), "%s/part.gcode", internal);
    FILE *out = fopen(gcode, "w"); assert(out && fputs("G28\nG1 X10 Y10 E1\n", out) >= 0 && fclose(out) == 0);
    snprintf(autosave, sizeof(autosave), "%s/autosave.cfg", dir); printer_autosave_path = autosave;
    out = fopen(autosave, "w"); assert(out && fputs("[bed_mesh default]\nx_count = 11\ny_count = 11\npoints =\n  0, 0\n", out) >= 0 && fclose(out) == 0);
    canvas_color_table_uds = fake_uds;
    memset(&machine, 0, sizeof(machine)); uds_init(&telemetry);
    queue_load();
    assert(queue_available && queue.count == 0 && !strcmp(queue.after, "wait") && queue.remove_done);
    queue.remove_done = 0; /* finished jobs stay listed until the section that tests removal */

    /* A copy: start request, the printer runs the file, the printer finishes it. */
    queue_item *it = job(1, 2, 0);
    assert(queue_save() == 0);
    copy_starts(it, "u1");
    printer_says(1, "complete", "", "u1"); telemetry_says("complete", "local/part.gcode");
    queue_tick(&machine);
    assert(it->done == 1 && !strcmp(it->state, "pending") && queue.active_id == 0 && !it->last_result[0]);
    assert(queue.complete_ms == 0); /* counted under "wait": no automatic start pending */
    queue_load(); it = &queue.items[0]; /* persisted as counted, with its unknown estimates */
    assert(queue_available && queue.count == 1 && it->done == 1 && !strcmp(it->state, "pending") && queue.active_id == 0 && !queue.remove_done);
    assert(it->estimated_seconds == -1 && it->filament_grams < 0);

    /* A cancelled copy is not counted; the job stays first with a note. */
    copy_starts(it, "u2");
    printer_says(1, "cancelled", "", "u2"); telemetry_says("cancelled", "local/part.gcode");
    queue_tick(&machine);
    assert(it->done == 1 && !strcmp(it->state, "pending") && !strcmp(it->last_result, "stopped") && queue.active_id == 0);

    /* A start the printer never reported running: idle for the grace period. */
    requested(it); it->started_ms = monotonic_ms() - QUEUE_START_GRACE_MS - 1;
    printer_says(1, "standby", "", ""); telemetry_says("standby", NULL);
    queue_tick(&machine);
    assert(!strcmp(it->state, "pending") && !strcmp(it->last_result, "not_started") && queue.active_id == 0);

    /* A calibrated start keeps the printer busy for minutes before the file prints: the wait goes on
     * while the printer is not idle or heats, and only idle time counts towards the grace. */
    requested(it); it->started_ms = monotonic_ms() - QUEUE_START_GRACE_MS - 1;
    printer_says(5, "standby", "", ""); telemetry_says("standby", NULL);
    queue_tick(&machine);
    assert(!strcmp(it->state, "starting") && monotonic_ms() - it->started_ms < 1000);
    it->started_ms = monotonic_ms() - QUEUE_START_GRACE_MS - 1;
    printer_says(1, "standby", "", ""); telemetry_says("standby", NULL); telemetry_heating(60);
    queue_tick(&machine);
    assert(!strcmp(it->state, "starting") && monotonic_ms() - it->started_ms < 1000);
    printer_says(2, "printing", "part.gcode", "u3"); telemetry_says("printing", "local/part.gcode");
    queue_tick(&machine);
    assert(!strcmp(it->state, "printing") && it->native_seen);
    printer_says(1, "complete", "", "u3"); telemetry_says("complete", "local/part.gcode");
    queue_tick(&machine);
    assert(it->done == 2 && !strcmp(it->state, "done") && queue.count == 1 && queue.active_id == 0);
    it->copies = 4; snprintf(it->state, sizeof(it->state), "pending");
    /* ...but a start request this old is given up whatever the printer shows. */
    requested(it); it->requested_ms = monotonic_ms() - QUEUE_START_CAP_MS - 1;
    printer_says(5, "standby", "", ""); telemetry_says("standby", NULL);
    queue_tick(&machine);
    assert(!strcmp(it->state, "pending") && !strcmp(it->last_result, "not_started") && queue.active_id == 0);
    /* The printer running another file means our start did not happen. */
    requested(it);
    printer_says(2, "printing", "other.gcode", "u4"); telemetry_says("printing", "local/other.gcode");
    queue_tick(&machine);
    assert(!strcmp(it->state, "pending") && !strcmp(it->last_result, "not_started") && queue.active_id == 0);

    /* Another file completing is not this job's copy; it ends uncounted once the printer stays idle. */
    copy_starts(it, "u5"); it->started_ms = monotonic_ms() - QUEUE_IDLE_GRACE_MS - 1;
    printer_says(1, "complete", "other.gcode", "u5"); telemetry_says("complete", "local/other.gcode");
    queue_tick(&machine);
    assert(it->done == 2 && !strcmp(it->last_result, "stopped") && queue.active_id == 0);

    /* Idle again without "complete" for a minute: the copy ended otherwise. */
    copy_starts(it, "u6"); it->started_ms = monotonic_ms() - QUEUE_IDLE_GRACE_MS - 1;
    printer_says(1, "standby", "", ""); telemetry_says("standby", NULL);
    queue_tick(&machine);
    assert(!strcmp(it->state, "pending") && !strcmp(it->last_result, "stopped") && it->done == 2);

    /* A "complete" left over from the previous copy of the same file: this copy's print_stats were never
     * seen running (the printer service was silent), so it is not counted. */
    requested(it);
    printer_says(2, "printing", "part.gcode", "u7"); telemetry_says("complete", "local/part.gcode"); telemetry_stale();
    queue_tick(&machine);
    assert(!strcmp(it->state, "printing") && !it->native_seen);
    printer_says(1, "complete", "", "u7"); telemetry_stale();
    queue_tick(&machine);
    assert(!strcmp(it->state, "printing") && it->done == 2);
    it->started_ms = monotonic_ms() - QUEUE_IDLE_GRACE_MS - 1;
    queue_tick(&machine);
    assert(!strcmp(it->state, "pending") && !strcmp(it->last_result, "stopped") && it->done == 2);
    /* The same evidence with the printer service showing the file running counts. */
    copy_starts(it, "u8");
    printer_says(1, "complete", "", "u8"); telemetry_says("complete", "local/part.gcode");
    queue_tick(&machine);
    assert(it->done == 3 && !strcmp(it->state, "pending") && !it->last_result[0]);

    /* Stale MQTT decides nothing, in either direction. */
    copy_starts(it, "u9");
    printer_says(1, "complete", "", "u9"); machine.last_message = time(NULL) - 60; telemetry_says("complete", "local/part.gcode");
    queue_tick(&machine);
    assert(!strcmp(it->state, "printing") && queue.active_id == 1 && it->done == 3);
    machine.last_message = time(NULL);
    queue_tick(&machine);
    assert(it->done == 4 && !strcmp(it->state, "done") && queue.count == 1 && queue.active_id == 0);

    /* The last copy with remove_done drops the job; so does lowering the copies to the printed count. */
    queue.count = 0; it = job(2, 1, 0); queue.remove_done = 1;
    copy_starts(it, "u10");
    printer_says(1, "complete", "part.gcode", "u10"); telemetry_says("complete", NULL);
    queue_tick(&machine);
    assert(queue.count == 0 && queue.active_id == 0);
    it = job(3, 3, 1); queue_available = 1;
    queue_copies_response(begin(), "3\n1", 3); assert(finish() == 200);
    assert(queue.count == 0);

    /* A USB job prints as the copy gcode_import_usb() made in internal memory; the start records that name. */
    it = job(4, 1, 0); snprintf(it->storage, sizeof(it->storage), "usb"); snprintf(it->path, sizeof(it->path), "stick/part.gcode");
    requested(it); snprintf(it->print_name, sizeof(it->print_name), "CC2_USB_0badf00d_part.gcode");
    printer_says(2, "printing", "CC2_USB_0badf00d_part.gcode", "u11"); telemetry_says("printing", "local/CC2_USB_0badf00d_part.gcode");
    queue_tick(&machine);
    assert(!strcmp(it->state, "printing") && it->native_seen);
    printer_says(1, "complete", "", "u11"); telemetry_says("complete", "local/CC2_USB_0badf00d_part.gcode");
    queue_tick(&machine);
    assert(queue.count == 0 && queue.active_id == 0); /* counted: the last copy, removed */
    it = job(5, 1, 0); requested(it); it->started_ms = monotonic_ms() - QUEUE_IDLE_GRACE_MS - 1;
    printer_says(2, "printing", "CC2_USB_0badf00d_part.gcode", "u12"); telemetry_says("printing", "local/CC2_USB_0badf00d_part.gcode");
    queue_tick(&machine);
    assert(!strcmp(it->state, "pending") && !strcmp(it->last_result, "not_started")); /* an internal job is not the import */
    queue.count = 0;

    /* Changing the policy never starts anything: a copy counted under "wait" leaves no pending delay,
     * and switching to "auto" afterwards does not create one. */
    it = job(6, 3, 0); queue.remove_done = 0;
    copy_starts(it, "u13");
    printer_says(1, "complete", "", "u13"); telemetry_says("complete", "local/part.gcode");
    queue_tick(&machine);
    assert(it->done == 1 && queue.complete_ms == 0);
    queue_policy_response(begin(), "auto\n10\n0", 10); assert(finish() == 200);
    assert(!strcmp(queue.after, "auto") && queue.complete_ms == 0);
    queue_tick(&machine);
    assert(queue.active_id == 0 && starts_sent == 0 && !it->last_result[0]);
    /* Under "auto" a counted copy arms the delay; a print the operator starts by hand disarms it. */
    copy_starts(it, "u14");
    printer_says(1, "complete", "", "u14"); telemetry_says("complete", "local/part.gcode");
    queue_tick(&machine);
    assert(it->done == 2 && queue.complete_ms > 0);
    printer_says(2, "printing", "manual.gcode", "m1"); telemetry_says("printing", "local/manual.gcode");
    queue_tick(&machine);
    assert(queue.complete_ms == 0 && queue.active_id == 0 && !strcmp(it->state, "pending"));
    /* While the delay runs, the queue reports the seconds left and a hold cancels the start. */
    queue.complete_ms = monotonic_ms();
    queue_get_response(begin(), &machine); assert(finish() == 200 && strstr(reply_body, "\"auto_in\":10"));
    queue_hold_response(begin()); assert(finish() == 200 && queue.complete_ms == 0);
    queue_get_response(begin(), &machine); assert(finish() == 200 && strstr(reply_body, "\"auto_in\":null"));
    /* A changed delay under "auto" also disarms it. */
    queue.complete_ms = monotonic_ms();
    queue_policy_response(begin(), "auto\n20\n0", 10); assert(finish() == 200);
    assert(queue.complete_ms == 0);
    queue_policy_response(begin(), "auto\n20\n1", 10); assert(finish() == 200); /* only remove_done: untouched */
    queue.remove_done = 0;

    /* The automatic start goes through the protected print path after the delay, once; the running
     * copy is then followed like any other. */
    queue.delay = 10; queue.complete_ms = monotonic_ms() - 5000;
    printer_says(1, "complete", "", ""); telemetry_says("complete", NULL);
    queue_tick(&machine);
    assert(queue.complete_ms != 0 && starts_sent == 0); /* not before the delay */
    queue.complete_ms = monotonic_ms() - 11000;
    queue_tick(&machine);
    assert(starts_sent == 1 && !strcmp(start_file, "part.gcode") && !strcmp(it->state, "starting") && queue.active_id == 6);
    assert(!strcmp(it->print_name, "part.gcode") && queue.complete_ms == 0 && !it->last_result[0]);
    queue_tick(&machine);
    assert(starts_sent == 1 && !strcmp(it->state, "starting")); /* no second start while it waits */
    printer_says(2, "printing", "part.gcode", "u15"); telemetry_says("printing", "local/part.gcode");
    queue_tick(&machine);
    assert(!strcmp(it->state, "printing"));
    printer_says(1, "complete", "", "u15"); telemetry_says("complete", "local/part.gcode");
    queue_tick(&machine);
    assert(it->done == 3 && !strcmp(it->state, "done") && queue.active_id == 0 && queue.count == 1);
    /* Without a printer link the print path refuses; the refusal is recorded, not retried. */
    queue.count = 0; it = job(7, 2, 0);
    queue.complete_ms = monotonic_ms() - 11000;
    printer_says(1, "complete", "", ""); machine.registered = 0; telemetry_says("complete", NULL);
    queue_tick(&machine);
    assert(queue.complete_ms == 0 && !strcmp(it->last_result, "auto_failed") && queue.active_id == 0 && starts_sent == 1);
    machine.registered = 1;
    /* The "wait" policy never starts. */
    snprintf(queue.after, sizeof(queue.after), "wait"); queue.complete_ms = monotonic_ms() - 11000; it->last_result[0] = 0;
    queue_tick(&machine);
    assert(!it->last_result[0] && queue.active_id == 0 && starts_sent == 1);

    /* The HTTP handlers while a copy runs: nothing else starts, the running job stays, its copies keep one
     * for the running print, and "next" queues behind it. */
    copy_starts(it, "u16"); /* job 7, copies 2, done 0, printing */
    assert(queue.active_id == 7);
    queue_start_response(begin(), &machine, "", 0); assert(finish() == 409 && strstr(reply_body, "already running"));
    queue_start_response(begin(), &machine, "7", 1); assert(finish() == 409);
    queue_remove_response(begin(), "7", 1); assert(finish() == 409 && queue.count == 1);
    queue_copies_response(begin(), "7\n0", 3); assert(finish() == 400);
    it->done = 1;
    queue_copies_response(begin(), "7\n1", 3); assert(finish() == 409 && strstr(reply_body, "keep one copy more"));
    queue_copies_response(begin(), "7\n2", 3); assert(finish() == 200 && !strcmp(it->state, "printing") && it->copies == 2);
    static const char add_next[] = "internal\npart.gcode\n\nA\nsaved\n0\n\n\n1\nnext";
    queue_add_response(begin(), add_next, strlen(add_next)); assert(finish() == 201);
    assert(queue.count == 2 && queue.items[0].id == 7 && queue.items[1].id == 8 && !strcmp(queue.items[1].state, "pending"));
    static const char add_bad[] = "internal\npart.gcode\n\nA\nsaved\n0\n\nx\"y\n1";
    queue_add_response(begin(), add_bad, strlen(add_bad)); assert(finish() == 400); /* not a nozzle id */
    static const char add_measure[] = "internal\npart.gcode\n\nA\nsaved\n0\n0123456789abcdef\n\n1";
    queue_add_response(begin(), add_measure, strlen(add_measure)); assert(finish() == 201);
    queue_get_response(begin(), &machine); assert(finish() == 200);
    /* `next` names the first job that could start; the browser hides it behind `active`. */
    assert(strstr(reply_body, "\"active\":7") && strstr(reply_body, "\"measure\":\"0123456789abcdef\"") && strstr(reply_body, "\"next\":8"));
    assert(queue_save() == 0);

    /* A loaded file with a running copy keeps following it; two running copies are refused. */
    queue_load();
    assert(queue_available && queue.count == 3 && queue.active_id == 7 && !strcmp(queue.items[0].state, "printing"));
    assert(!strcmp(queue.items[2].measure, "0123456789abcdef"));
    snprintf(queue.items[1].state, sizeof(queue.items[1].state), "printing"); assert(queue_save() == 0);
    queue_load();
    assert(!queue_available && queue.count == 0);

    unlink(gcode); unlink(file); unlink(autosave); rmdir(internal); rmdir(dir);
    puts("PASS copies counted only from this copy's own completion seen fresh; stops, foreign jobs, long starts, "
         "USB imports, policy changes, the automatic start and the running-copy guards handled");
    return 0;
}
