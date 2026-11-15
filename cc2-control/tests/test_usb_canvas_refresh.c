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


static int pair_[2];
static char reply_body[16384];
static int begin(void) { assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair_)); return pair_[0]; }
static int finish(void) {
    close(pair_[0]);
    size_t used = 0; ssize_t n;
    while (used + 1 < sizeof(reply_body) && (n = recv(pair_[1], reply_body + used, sizeof(reply_body) - 1 - used, 0)) > 0) used += (size_t)n;
    close(pair_[1]); reply_body[used] = 0;
    return used ? atoi(strchr(reply_body, ' ') + 1) : -1;
}
/* Whether the start was answered yet (the worker's socket is closed once it is). */
static int answered(void) {
    char byte; ssize_t n = recv(pair_[1], &byte, 1, MSG_PEEK | MSG_DONTWAIT);
    return n >= 0;
}
static int failures;
static void expect(int ok, const char *what) { printf("%s: %s\n", ok ? "PASS" : "FAIL", what); if (!ok) failures++; }
static void copied(void) {
    for (int i = 0; i < 5000 && !atomic_load(&usb_start_done); ++i) nanosleep(&(struct timespec){0, 1000000}, NULL);
    assert(atomic_load(&usb_start_done));
}
static void idle(void) { printer_says(1, "complete", "", "u"); telemetry_says("complete", NULL); }
#define RED_PLA(id, status) TRAY_NAMED(id, status, "PLA", "PLA Matte", "#F72221")
#define GREEN_PLA(id, status) TRAY_NAMED(id, status, "PLA", "PLA Matte", "#00AA00")
#define USB_MAPPED "usb\npart.gcode\n0:0\nA\nsaved\n0\n\n"

/* Fund 3: after the copy of a USB file with a slot mapping the Canvas trays are compared with a fresh report,
 * asked for after the copy, not with the one stored before. */
int main(void) {
    char dir[] = "/tmp/cc2-canvasrefresh-XXXXXX";
    assert(mkdtemp(dir));
    static char file[640], internal[640], usb[640], usbfile[700], autosave[640];
    snprintf(file, sizeof(file), "%s/print-queue.json", dir); queue_path = file;
    snprintf(internal, sizeof(internal), "%s/internal", dir); assert(mkdir(internal, 0755) == 0); gcode_internal_root = internal;
    snprintf(usb, sizeof(usb), "%s/usb", dir); assert(mkdir(usb, 0755) == 0); gcode_usb_root = usb;
    snprintf(usbfile, sizeof(usbfile), "%s/part.gcode", usb);
    FILE *out = fopen(usbfile, "w"); assert(out && fputs("; filament_type = PLA\nG28\nG1 X10 Y10 E1\n", out) >= 0 && fclose(out) == 0);
    snprintf(autosave, sizeof(autosave), "%s/autosave.cfg", dir); printer_autosave_path = autosave;
    out = fopen(autosave, "w"); assert(out && fputs("[bed_mesh default]\nx_count = 11\ny_count = 11\npoints =\n  0, 0\n", out) >= 0 && fclose(out) == 0);
    canvas_color_table_uds = fake_uds;
    memset(&machine, 0, sizeof(machine)); uds_init(&telemetry);
    queue_load();
    int sent;

    /* R: the spool in tray 0 is swapped during the copy, but the printer has not sent a report on its own.
     * Only the report asked for after the copy shows it: the start must be refused, not sent. */
    idle(); canvas_list(RED_PLA(0, 1)); sent = starts_sent;
    assert(usb_start_begin(begin(), &machine, USB_MAPPED, strlen(USB_MAPPED)) == 1);
    copied(); usb_start_tick(&machine);
    expect(machine.canvas_refresh_due == 1, "R after the copy a fresh Canvas report is asked for");
    expect(!answered() && starts_sent == sent, "R and the start waits for it");
    machine.canvas_refresh_due = 0; canvas_list(GREEN_PLA(0, 1)); usb_start_tick(&machine);
    int status = finish();
    expect(status == 409 && starts_sent == sent && strstr(reply_body, "Canvas trays changed while the file was copied"),
           "R a swap shown only by the fresh report refuses the start");

    /* G: the report comes and the trays are the same: the start goes out. */
    idle(); canvas_list(RED_PLA(0, 1)); sent = starts_sent;
    assert(usb_start_begin(begin(), &machine, USB_MAPPED, strlen(USB_MAPPED)) == 1);
    copied(); usb_start_tick(&machine);
    machine.canvas_refresh_due = 0; canvas_list(RED_PLA(0, 1)); usb_start_tick(&machine);
    status = finish();
    expect(status == 202 && starts_sent == sent + 1, "G fresh report with the same trays: the start goes out");

    /* S: no report within the wait: refused with its own message. */
    usb_start_canvas_wait_ms = 50;
    idle(); canvas_list(RED_PLA(0, 1)); sent = starts_sent;
    assert(usb_start_begin(begin(), &machine, USB_MAPPED, strlen(USB_MAPPED)) == 1);
    copied(); usb_start_tick(&machine);
    expect(!answered(), "S waits while the time is not over");
    nanosleep(&(struct timespec){0, 80000000}, NULL);
    idle(); usb_start_tick(&machine);
    status = finish();
    expect(status == 409 && starts_sent == sent && strstr(reply_body, "did not report its trays after the copy"),
           "S no fresh report in time: the start is refused");
    expect(!usb_start_active(), "S and the worker is free again");
    usb_start_canvas_wait_ms = USB_START_CANVAS_WAIT_MS;

    /* N: no slot mapping: judged at once, as before. */
    idle(); canvas_list(RED_PLA(0, 1)); sent = starts_sent; machine.canvas_refresh_due = 0;
    assert(usb_start_begin(begin(), &machine, "usb\npart.gcode\n\nA\nsaved\n0\n\n", 27) == 1);
    copied(); usb_start_tick(&machine);
    status = finish();
    expect(status == 202 && starts_sent == sent + 1 && !machine.canvas_refresh_due, "N without a mapping: no wait, the start goes out");

    /* K: the Canvas had not reported when the start began: nothing to compare, no wait, as before. */
    idle(); machine.canvas_snapshot_len = 0; sent = starts_sent; machine.canvas_refresh_due = 0;
    assert(usb_start_begin(begin(), &machine, USB_MAPPED, strlen(USB_MAPPED)) == 1);
    copied(); usb_start_tick(&machine);
    status = finish();
    expect(status == 202 && starts_sent == sent + 1 && !machine.canvas_refresh_due, "K no Canvas report at the start: no wait, the start goes out");

    /* X: emergency stop while waiting for the report: refused at once, nothing sent. */
    idle(); canvas_list(RED_PLA(0, 1)); sent = starts_sent;
    assert(usb_start_begin(begin(), &machine, USB_MAPPED, strlen(USB_MAPPED)) == 1);
    copied(); usb_start_tick(&machine);
    expect(!answered(), "X waits for the report");
    usb_start_cancel(); usb_start_tick(&machine);
    status = finish();
    expect(status == 409 && starts_sent == sent && strstr(reply_body, "emergency stop"), "X emergency stop during the wait refuses the start");

    /* Q: a queued USB job with a mapping waits the same way; a swap in the fresh report keeps it pending. */
    queue.count = 0; queue.active_id = 0; idle(); canvas_list(RED_PLA(0, 1)); sent = starts_sent;
    const char *job = "usb\npart.gcode\n0:0\nA\nsaved\n0\n\n\n1";
    queue_add_response(begin(), job, strlen(job)); assert(finish() == 201);
    char id[16]; snprintf(id, sizeof(id), "%d", queue.items[0].id);
    assert(queue_start_response(begin(), &machine, id, strlen(id)) == 1);
    copied(); usb_start_tick(&machine);
    expect(machine.canvas_refresh_due == 1 && !answered(), "Q the queued job asks for a fresh report and waits");
    machine.canvas_refresh_due = 0; canvas_list(GREEN_PLA(0, 1)); usb_start_tick(&machine);
    status = finish();
    expect(status == 409 && starts_sent == sent && !strcmp(queue.items[0].state, "pending"),
           "Q a swap in the fresh report refuses the queued start and keeps the job pending");

    if (failures) { printf("%d FAIL\n", failures); return 1; }
    puts("PASS USB start compares the Canvas trays with a fresh report after the copy");
    return 0;
}
