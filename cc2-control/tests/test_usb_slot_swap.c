#pragma GCC diagnostic ignored "-Wunused-function"
#define main cc2_main_original
#define mqtt_start_print mock_mqtt_start_print
#define mqtt_prepare_timelapse mock_mqtt_prepare_timelapse
#include "../src/main.c"
#undef main
#undef mqtt_start_print
#undef mqtt_prepare_timelapse
#include <assert.h>

static mqtt_client machine;
static int starts_sent;
static char start_file[PATH_MAX_LOCAL];
static int start_trays[GCODE_TOOLS_MAX]; /* the Canvas tray of each mapped tool in the last start */
static size_t start_count;

int mock_mqtt_start_print(mqtt_client *c, const char *media, const char *filename, const int *tools, const int *trays,
                          size_t count, char layout, int bedlevel_force, int timelapse) {
    (void)c; (void)media; (void)tools; (void)layout; (void)bedlevel_force; (void)timelapse;
    starts_sent++;
    snprintf(start_file, sizeof(start_file), "%s", filename);
    start_count = count;
    for (size_t i = 0; i < count && i < GCODE_TOOLS_MAX; ++i) start_trays[i] = trays[i];
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
/* The Canvas telemetry in the MQTT cache: one tray, as the printer's method 2005 reports it. Each call is a
 * new report, as mqtt.c counts them. */
static void canvas_says(int tray, const char *type, int status) {
    machine.canvas_uds_ready = 0; machine.canvas_revision++; machine.canvas_discovery_complete = 1;
    machine.canvas_snapshot_len = (size_t)snprintf(machine.canvas_snapshot, sizeof(machine.canvas_snapshot),
        "{\"id\":2005,\"result\":{\"canvas_info\":{\"canvas_list\":[{\"canvas_id\":0,\"connected\":1,\"tray_list\":["
        "{\"tray_id\":%d,\"status\":%d,\"filament_type\":\"%s\"}]}]}}}", tray, status, type);
}
/* Several trays with their labels: status 0 is an empty tray, which keeps the label of the filament it held. */
#define TRAY_NAMED(id, status, type, name, color) \
    "{\"tray_id\":" #id ",\"status\":" #status ",\"filament_type\":\"" type "\",\"filament_name\":\"" name "\",\"filament_color\":\"" color "\"}"
#define TRAY(id, status, type, color) TRAY_NAMED(id, status, type, type, color)
static void canvas_list(const char *trays) {
    machine.canvas_uds_ready = 0; machine.canvas_revision++; machine.canvas_discovery_complete = 1;
    machine.canvas_snapshot_len = (size_t)snprintf(machine.canvas_snapshot, sizeof(machine.canvas_snapshot),
        "{\"id\":2005,\"result\":{\"canvas_info\":{\"canvas_list\":[{\"canvas_id\":0,\"connected\":1,\"tray_list\":[%s]}]}}}", trays);
}

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

/* What queue_start_item records when the printer accepts a start. */
static void requested(queue_item *it) {
    double value;
    snprintf(it->state, sizeof(it->state), "starting");
    snprintf(it->print_name, sizeof(it->print_name), "%s", it->path);
    snprintf(it->request_uuid, sizeof(it->request_uuid), "%s", machine.uuid);
    it->started_ms = it->requested_ms = monotonic_ms(); it->mismatch_ms = 0;
    it->job_uuid[0] = 0; it->native_seen = 0;
    it->duration_baseline = uds_value(&telemetry, U_TOTAL_DURATION, &value) ? value : -1;
    it->bed_baseline = uds_value(&telemetry, U_BG, &value) ? value : 0;
    it->nozzle_baseline = uds_value(&telemetry, U_EG, &value) ? value : 0;
    queue.active_id = it->id;
}

/* The printer runs the copy: MQTT and the printer service both show the file printing, as a new job. */
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

static int add(const char *body) {
    queue_add_response(begin(), body, strlen(body));
    return finish();
}

/* B-5: a print dialog start (POST /api/gcode-files/print -> usb_start_begin) takes the mapping the page chose
 * and copies the file on the start worker. A spool changed in a mapped tray while the copy runs must not be
 * printed with the old mapping: the start is refused with 409 and nothing is sent. Starts without a change
 * and changes in trays the mapping does not name go out as before. */
static int checks_failed;
static void settle_start(void) {
    for (int i = 0; i < 5000 && !atomic_load(&usb_start_done); ++i) nanosleep(&(struct timespec){0, 1000000}, NULL);
    assert(atomic_load(&usb_start_done));
    usb_start_tick(&machine);
    /* A USB start with a mapping asks for a fresh Canvas report after the copy: the printer answers with the
     * trays as they are now (the same snapshot, counted as a new report). */
    if (usb_start_active() && machine.canvas_refresh_due) {
        machine.canvas_refresh_due = 0; machine.canvas_revision++;
        usb_start_tick(&machine);
    }
}
#define RED_PLA(id, status) TRAY_NAMED(id, status, "PLA", "PLA Matte", "#F72221")
static int dialog_start(const char *body) { return usb_start_begin(begin(), &machine, body, strlen(body)); }
static void check(const char *what, int want_start, int status, int sent_before, const char *message) {
    int sent = starts_sent - sent_before;
    int ok = want_start ? (status == 202 && sent == 1 && start_trays[0] == 0)
                        : (status == 409 && sent == 0 && message && strstr(reply_body, message));
    fprintf(stderr, "%s: reply %d, starts sent %d, tray used %d -> %s\n", what, status, sent, sent ? start_trays[0] : -1,
            ok ? "ok" : "FAIL");
    if (!ok) checks_failed++;
}
static void idle(void) { printer_says(1, "complete", "", "u"); telemetry_says("complete", NULL); }
#define DIALOG "internal\npart.gcode\n0:0\nA\nsaved\n0\n\n"
/* The USB copies in the internal memory (and their removal). */
static char internal_dir[640];
static int imports(int remove_them) {
    DIR *dir = opendir(internal_dir); struct dirent *entry; int count = 0;
    assert(dir);
    while ((entry = readdir(dir))) if (!strncmp(entry->d_name, GCODE_USB_IMPORT_PREFIX, strlen(GCODE_USB_IMPORT_PREFIX))) {
        count++;
        if (remove_them) { char path[1400]; snprintf(path, sizeof(path), "%s/%s", internal_dir, entry->d_name); unlink(path); }
    }
    closedir(dir); return count;
}

int main(void) {
    char dir[] = "/tmp/cc2-dialogswap-XXXXXX";
    assert(mkdtemp(dir));
    static char file[640], internal[640], gcode[700], autosave[640];
    snprintf(file, sizeof(file), "%s/print-queue.json", dir); queue_path = file;
    snprintf(internal, sizeof(internal), "%s/internal", dir); assert(mkdir(internal, 0755) == 0); gcode_internal_root = internal;
    snprintf(internal_dir, sizeof(internal_dir), "%s", internal);
    static char usb[640], usbfile[700], duo[700];
    snprintf(usb, sizeof(usb), "%s/usb", dir); assert(mkdir(usb, 0755) == 0); gcode_usb_root = usb;
    snprintf(usbfile, sizeof(usbfile), "%s/part.gcode", usb);
    FILE *extra = fopen(usbfile, "w"); assert(extra && fputs("; filament_type = PLA\nG28\nG1 X10 Y10 E1\n", extra) >= 0 && fclose(extra) == 0);
    snprintf(duo, sizeof(duo), "%s/duo.gcode", internal);
    extra = fopen(duo, "w"); assert(extra && fputs("; filament_type = PLA;PLA\nG28\nT0\nG1 X10 Y10 E1\nT1\nG1 X20 Y20 E1\n", extra) >= 0 && fclose(extra) == 0);
    snprintf(gcode, sizeof(gcode), "%s/part.gcode", internal);
    FILE *out = fopen(gcode, "w"); assert(out && fputs("; filament_type = PLA\nG28\nG1 X10 Y10 E1\n", out) >= 0 && fclose(out) == 0);
    snprintf(autosave, sizeof(autosave), "%s/autosave.cfg", dir); printer_autosave_path = autosave;
    out = fopen(autosave, "w"); assert(out && fputs("[bed_mesh default]\nx_count = 11\ny_count = 11\npoints =\n  0, 0\n", out) >= 0 && fclose(out) == 0);
    canvas_color_table_uds = fake_uds;
    memset(&machine, 0, sizeof(machine)); uds_init(&telemetry);
    queue_load();
    assert(queue_available && queue.count == 0);
    int sent, status;

    /* Control: nothing changes. */
    idle(); canvas_list(RED_PLA(0, 1)); sent = starts_sent;
    assert(dialog_start(DIALOG) == 1);
    settle_start(); status = finish();
    check("C no change during the copy", 1, status, sent, NULL);

    /* Control: same trays reported again. */
    idle(); canvas_list(RED_PLA(0, 1)); sent = starts_sent;
    assert(dialog_start(DIALOG) == 1);
    canvas_list(RED_PLA(0, 1));
    settle_start(); status = finish();
    check("C2 same trays reported again", 1, status, sent, NULL);

    /* A: the spool moves from tray 0 to tray 3 during the copy; tray 0 is empty afterwards. */
    idle(); canvas_list(RED_PLA(0, 1) "," RED_PLA(3, 0)); sent = starts_sent;
    assert(dialog_start(DIALOG) == 1);
    canvas_list(RED_PLA(0, 0) "," RED_PLA(3, 1));
    settle_start(); status = finish();
    check("A spool moved to another tray", 0, status, sent, "Canvas trays changed while the file was copied");

    /* B: another colour in the same tray. */
    idle(); canvas_list(RED_PLA(0, 1)); sent = starts_sent;
    assert(dialog_start(DIALOG) == 1);
    canvas_list(TRAY_NAMED(0, 1, "PLA", "PLA Matte", "#00AA00"));
    settle_start(); status = finish();
    check("B other colour in the same tray", 0, status, sent, "Canvas trays changed while the file was copied");

    /* B2: another material in the same tray. */
    idle(); canvas_list(RED_PLA(0, 1)); sent = starts_sent;
    assert(dialog_start(DIALOG) == 1);
    canvas_list(TRAY_NAMED(0, 1, "PETG", "PETG Basic", "#F72221"));
    settle_start(); status = finish();
    check("B2 other material in the same tray", 0, status, sent, "Canvas trays changed while the file was copied");

    /* B3, B4: only the material type, or only the name, changes in the tray. */
    idle(); canvas_list(RED_PLA(0, 1)); sent = starts_sent;
    assert(dialog_start(DIALOG) == 1);
    canvas_list(TRAY_NAMED(0, 1, "PETG", "PLA Matte", "#F72221"));
    settle_start(); status = finish();
    check("B3 only the material type changed", 0, status, sent, "Canvas trays changed while the file was copied");
    idle(); canvas_list(RED_PLA(0, 1)); sent = starts_sent;
    assert(dialog_start(DIALOG) == 1);
    canvas_list(TRAY_NAMED(0, 1, "PLA", "PLA Silk", "#F72221"));
    settle_start(); status = finish();
    check("B4 only the name changed", 0, status, sent, "Canvas trays changed while the file was copied");

    /* P: the mapping names tray 2 (not 0); tray 2 changes colour. */
    idle(); canvas_list(RED_PLA(0, 1) "," RED_PLA(2, 1)); sent = starts_sent;
    assert(dialog_start("internal\npart.gcode\n0:2\nA\nsaved\n0\n\n") == 1);
    canvas_list(RED_PLA(0, 1) "," TRAY_NAMED(2, 1, "PLA", "PLA Matte", "#00AA00"));
    settle_start(); status = finish();
    check("P mapped tray 2 changed colour", 0, status, sent, "Canvas trays changed while the file was copied");

    /* Q: two tools, 0:0 and 1:2; the second mapped tray changes. Q2: nothing changes, the start goes out. */
    idle(); canvas_list(RED_PLA(0, 1) "," RED_PLA(2, 1)); sent = starts_sent;
    assert(dialog_start("internal\nduo.gcode\n0:0,1:2\nA\nsaved\n0\n\n") == 1);
    canvas_list(RED_PLA(0, 1) "," TRAY_NAMED(2, 1, "PLA", "PLA Matte", "#00AA00"));
    settle_start(); status = finish();
    check("Q second mapped tray changed", 0, status, sent, "Canvas trays changed while the file was copied");
    idle(); canvas_list(RED_PLA(0, 1) "," RED_PLA(2, 1)); sent = starts_sent;
    assert(dialog_start("internal\nduo.gcode\n0:0,1:2\nA\nsaved\n0\n\n") == 1);
    settle_start(); status = finish();
    check("Q2 two tools, no change", 1, status, sent, NULL);

    /* M: a USB file. The copy is removed when the start is refused; without a change it is kept (the start). */
    idle(); canvas_list(RED_PLA(0, 1)); sent = starts_sent; assert(imports(1) == 0);
    assert(dialog_start("usb\npart.gcode\n0:0\nA\nsaved\n0\n\n") == 1);
    canvas_list(TRAY_NAMED(0, 1, "PLA", "PLA Matte", "#00AA00"));
    settle_start(); status = finish();
    check("M USB start, colour changed", 0, status, sent, "Canvas trays changed while the file was copied");
    if (imports(0) != 0) { fprintf(stderr, "M the USB copy was not removed after the refusal -> FAIL\n"); checks_failed++; }
    idle(); canvas_list(RED_PLA(0, 1)); sent = starts_sent;
    assert(dialog_start("usb\npart.gcode\n0:0\nA\nsaved\n0\n\n") == 1);
    settle_start(); status = finish();
    check("M2 USB start, no change", 1, status, sent, NULL);
    imports(1);


    /* The queue on this stack takes the mapping the job stores (no Canvas lookup): the same rule holds for it.
     * Button start, then the automatic start. */
    for (int round = 0; round < 2; ++round) {
        int swap = round == 0;
        queue.count = 0; queue.active_id = 0; idle(); canvas_list(RED_PLA(0, 1)); sent = starts_sent;
        const char *job = "internal\npart.gcode\n0:0\nA\nsaved\n0\n\n\n1";
        queue_add_response(begin(), job, strlen(job)); assert(finish() == 201);
        char id[16]; snprintf(id, sizeof(id), "%d", queue.items[0].id);
        assert(queue_start_response(begin(), &machine, id, strlen(id)) == 1);
        if (swap) canvas_list(TRAY_NAMED(0, 1, "PLA", "PLA Matte", "#00AA00"));
        settle_start(); status = finish();
        check(swap ? "QB queued button start, colour changed" : "QB2 queued button start, no change", !swap, status, sent,
              "Canvas trays changed while the file was copied");
        if (swap && strcmp(queue.items[0].state, "pending")) { fprintf(stderr, "QB job not pending -> FAIL\n"); checks_failed++; }
        queue.count = 0; queue.active_id = 0; idle(); canvas_list(RED_PLA(0, 1)); sent = starts_sent;
        queue_add_response(begin(), job, strlen(job)); assert(finish() == 201);
        snprintf(queue.after, sizeof(queue.after), "auto"); queue.delay = 10; queue.complete_ms = monotonic_ms() - 11000;
        queue_tick(&machine);
        assert(usb_start_active() && usb_start_queue_auto);
        if (swap) canvas_list(TRAY_NAMED(0, 1, "PLA", "PLA Matte", "#00AA00"));
        settle_start();
        int ok = swap ? starts_sent == sent && !strcmp(queue.items[0].last_result, "auto_failed") && !strcmp(queue.items[0].state, "pending")
                      : starts_sent == sent + 1 && !queue.items[0].last_result[0];
        fprintf(stderr, "%s: starts sent %d, state %s/%s -> %s\n", swap ? "QA queued automatic start, colour changed" : "QA2 queued automatic start, no change",
                starts_sent - sent, queue.items[0].state, queue.items[0].last_result, ok ? "ok" : "FAIL");
        if (!ok) checks_failed++;
        snprintf(queue.after, sizeof(queue.after), "wait");
    }

    /* J: a tray the mapping does not name changes: the start goes out. */
    idle(); canvas_list(RED_PLA(0, 1) "," RED_PLA(2, 1)); sent = starts_sent;
    assert(dialog_start(DIALOG) == 1);
    canvas_list(RED_PLA(0, 1) "," TRAY_NAMED(2, 1, "PETG", "PETG Basic", "#123456"));
    settle_start(); status = finish();
    check("J unmapped tray changed", 1, status, sent, NULL);

    /* E (limit, as for the queue): no report after the copy: judged as before, the start goes out. */
    idle(); canvas_list(RED_PLA(0, 1)); sent = starts_sent;
    assert(dialog_start(DIALOG) == 1);
    machine.canvas_snapshot_len = 0;
    settle_start(); status = finish();
    check("E no Canvas report after the copy", 1, status, sent, NULL);

    /* K: no report when the start began: nothing to compare with, the start goes out. */
    idle(); machine.canvas_snapshot_len = 0; sent = starts_sent;
    assert(dialog_start(DIALOG) == 1);
    canvas_list(TRAY_NAMED(0, 1, "PLA", "PLA Matte", "#00AA00"));
    settle_start(); status = finish();
    check("K no report at the beginning", 1, status, sent, NULL);

    unlink(gcode); unlink(usbfile); unlink(duo); rmdir(usb); unlink(file); unlink(autosave); rmdir(internal); rmdir(dir);
    if (checks_failed) { fprintf(stderr, "FAIL: %d checks\n", checks_failed); return 1; }
    puts("PASS no dialog start with a changed tray after the copy; unchanged starts go out");
    return 0;
}
