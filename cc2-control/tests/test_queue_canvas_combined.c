/* The combined path of #352 and #390 for a queued start fed from the Canvas (button and automatic, USB files):
 * #352 asks for a fresh Canvas report and assigns the slots from it; #390 keeps exactly the trays of that report as
 * the comparison value (queue_start_trays -> usb_start_trays_note) and compares them with a second report after the
 * copy. K1-K6 follow both steps in order; K7 a queued start whose slots came without a report is refused, never
 * compared with a later one. The comparison value from an older report (the cache when the start was first asked
 * for) turns K1 and K2 red. */
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
static int start_trays[GCODE_TOOLS_MAX]; /* the Canvas tray of each mapped tool in the last start */
static size_t start_count;

int mock_mqtt_start_print(mqtt_client *c, const char *media, const char *filename, const int *tools, const int *trays,
                          size_t count, char layout, int bedlevel_force, int timelapse) {
    (void)c; (void)media; (void)filename; (void)tools; (void)layout; (void)bedlevel_force; (void)timelapse;
    starts_sent++;
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
/* Several trays with their labels: status 0 is an empty tray, which keeps the label of the filament it held. */
#define TRAY_NAMED(id, status, type, name, color) \
    "{\"tray_id\":" #id ",\"status\":" #status ",\"filament_type\":\"" type "\",\"filament_name\":\"" name "\",\"filament_color\":\"" color "\"}"
static void canvas_list(const char *trays) {
    machine.canvas_uds_ready = 0; machine.canvas_revision++; machine.canvas_discovery_complete = 1;
    machine.canvas_snapshot_len = (size_t)snprintf(machine.canvas_snapshot, sizeof(machine.canvas_snapshot),
        "{\"id\":2005,\"result\":{\"canvas_info\":{\"canvas_list\":[{\"canvas_id\":0,\"connected\":1,\"tray_list\":[%s]}]}}}", trays);
}

/* HTTP handlers through a socket pair: the status line of the reply, and its body. */
static int pair_[2];
static char reply_body[16384];
static int begin(void) { assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair_)); return pair_[0]; }
static int finish(void) {
    close(pair_[0]);
    size_t used = 0; ssize_t n;
    { struct timeval rcv_limit = {30, 0}; /* never wait forever: a missing answer fails the test */
      setsockopt(pair_[1], SOL_SOCKET, SO_RCVTIMEO, &rcv_limit, sizeof(rcv_limit)); }
    while (used + 1 < sizeof(reply_body) && (n = recv(pair_[1], reply_body + used, sizeof(reply_body) - 1 - used, 0)) > 0) used += (size_t)n;
    close(pair_[1]); reply_body[used] = 0;
    assert(used > 0);
    return atoi(strchr(reply_body, ' ') + 1);
}
/* A test that waits for something that never comes ends red with a message instead of hanging the suite. */
static void test_too_long(int signal_number) {
    (void)signal_number;
    static const char text[] = "FAIL: the test ran longer than 300 s (an answer that never came?)\n";
    if (write(2, text, sizeof(text) - 1) < 0) {}
    _exit(1);
}
static void idle(void) { printer_says(1, "complete", "", "u"); telemetry_says("complete", NULL); }
static int fails;
static void expect(int ok, const char *what) { printf("%s: %s\n", ok ? "PASS" : "FAIL", what); if (!ok) fails++; }
static void wait_worker(void) { for (int i = 0; i < 5000 && !atomic_load(&usb_start_done); ++i) nanosleep(&(struct timespec){0, 1000000}, NULL); }
/* the printer answers each report asked for with the trays as they are now (answer=1) or never (answer=0) */
static void ticks(int ms, int answer) {
    for (int i = 0; i < ms && usb_start_active(); ++i) {
        if (answer && machine.canvas_refresh_due) { machine.canvas_refresh_due = 0; machine.canvas_revision++; }
        usb_start_tick(&machine); nanosleep(&(struct timespec){0, 1000000}, NULL);
    }
}
#define OLD TRAY_NAMED(0, 1, "PLA", "PLA Matte", "#F72221")
#define MOVED TRAY_NAMED(0, 0, "PLA", "PLA Matte", "#F72221") "," TRAY_NAMED(3, 1, "PLA", "PLA Matte", "#F72221")
#define JOB "usb\npart.gcode\n\nA\nsaved\n0\n\n\n1\nend\n\n\n0\tPLA\tPLA Matte\t#F72221"
static void fresh_queue(void) {
    queue.count = 0; queue.active_id = 0; queue.complete_ms = 0; queue.check_id = 0;
    snprintf(queue.after, sizeof(queue.after), "wait");
    idle(); canvas_list(OLD);
    queue_add_response(begin(), &machine, JOB, strlen(JOB)); assert(finish() == 201);
}
static int auto_arm(void) { snprintf(queue.after, sizeof(queue.after), "auto"); queue.delay = 10; queue.complete_ms = monotonic_ms() - 11000; queue.complete_canvas = machine.canvas_revision; return 0; }
int main(void) {
    signal(SIGALRM, test_too_long); alarm(120);
    char dir[] = "/tmp/cc2-kombi-XXXXXX"; assert(mkdtemp(dir));
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
    queue_load(); assert(queue_available);
    int sent, status; char id[16];

    /* K1 manual (button), USB: cached report has the spool in tray 0; it is moved to tray 3 before #352's fresh
     * report. Mapping must take tray 3 AND the #390 baseline must be that same fresh report -> 202 on tray 3. */
    fresh_queue(); snprintf(id, sizeof(id), "%d", queue.items[0].id); sent = starts_sent;
    assert(queue_start_response(begin(), &machine, id, strlen(id)) == 0);
    status = finish(); assert(status == 409 && strstr(reply_body, "\"checking\":true"));
    canvas_list(MOVED); machine.canvas_refresh_due = 0;
    assert(queue_start_response(begin(), &machine, id, strlen(id)) == 1);
    expect(usb_start_trays_known && !memcmp(usb_start_trays, queue_start_trays, sizeof(usb_start_trays)) &&
           usb_start_trays[3].status == 1, "K1 button: the comparison value is the report the slots were assigned from");
    wait_worker(); ticks(3000, 1); status = finish();
    expect(status == 202 && starts_sent == sent + 1 && start_trays[0] == 3, "K1 button: moved before fresh report -> mapped to tray 3, baseline = fresh report, 202");

    /* K2 automatic, USB: spool moved during the delay (old cache tray 0, fresh report tray 3). */
    fresh_queue(); auto_arm(); sent = starts_sent;
    queue_tick(&machine); assert(queue.check_id && machine.canvas_refresh_due);
    canvas_list(MOVED); machine.canvas_refresh_due = 0; queue_tick(&machine);
    expect(usb_start_active() && usb_start_queue_auto, "K2 auto: worker started after the fresh report");
    wait_worker(); ticks(3000, 1);
    expect(starts_sent == sent + 1 && start_trays[0] == 3 && !strcmp(queue.items[0].state, "starting"), "K2 auto: moved during delay -> tray 3, 202");

    /* K3 automatic, USB: spool changed during the copy -> refused after the copy, auto_failed, pending. */
    fresh_queue(); auto_arm(); sent = starts_sent;
    queue_tick(&machine); machine.canvas_refresh_due = 0; machine.canvas_revision++; queue_tick(&machine);
    assert(usb_start_active());
    canvas_list(TRAY_NAMED(0, 1, "PLA", "PLA Matte", "#00AA00"));
    wait_worker(); ticks(3000, 1);
    expect(starts_sent == sent && !strcmp(queue.items[0].state, "pending") && !strcmp(queue.items[0].last_result, "auto_failed"), "K3 auto: changed during copy -> no start, auto_failed");

    /* K4 manual, USB: no Canvas answer after the copy -> 409 canvas_unreported, job pending, nothing sent. */
    usb_start_canvas_wait_ms = 50;
    fresh_queue(); snprintf(id, sizeof(id), "%d", queue.items[0].id); sent = starts_sent;
    assert(queue_start_response(begin(), &machine, id, strlen(id)) == 0); assert(finish() == 409);
    machine.canvas_refresh_due = 0; machine.canvas_revision++;
    assert(queue_start_response(begin(), &machine, id, strlen(id)) == 1);
    wait_worker(); ticks(500, 0); status = finish();
    expect(status == 409 && strstr(reply_body, "canvas_unreported") && starts_sent == sent && !strcmp(queue.items[0].state, "pending"), "K4 button: no reply after copy -> 409 canvas_unreported");

    /* K5 automatic, USB: no Canvas answer after the copy -> auto_failed. */
    fresh_queue(); auto_arm(); sent = starts_sent;
    queue_tick(&machine); machine.canvas_refresh_due = 0; machine.canvas_revision++; queue_tick(&machine);
    assert(usb_start_active());
    wait_worker(); ticks(500, 0);
    expect(!usb_start_active() && starts_sent == sent && !strcmp(queue.items[0].last_result, "auto_failed"), "K5 auto: no reply after copy -> auto_failed");
    usb_start_canvas_wait_ms = USB_START_CANVAS_WAIT_MS;

    /* K6 automatic, USB: no answer to #352's request before the start -> auto_material, worker never started. */
    fresh_queue(); auto_arm(); sent = starts_sent;
    queue_tick(&machine); assert(queue.check_id);
    queue.check_ms -= QUEUE_CHECK_MS + 1; queue.complete_ms -= QUEUE_CHECK_MS + 1; queue_tick(&machine);
    expect(!usb_start_active() && starts_sent == sent && !strcmp(queue.items[0].last_result, "auto_material"), "K6 auto: no reply before start -> auto_material, nothing copied");

    /* K7 a queued USB start with a mapping whose slots came without a fresh report: refused before the copy with
     * reason canvas_unreported, the worker never starts, nothing is sent. */
    idle(); canvas_list(OLD); sent = starts_sent;
    queue_start_trays_known = 0;
    {
        static const char body[] = "usb\npart.gcode\n0:0\nA\nsaved\n0";
        int refused = 0, taken = usb_start_begin_for(begin(), &machine, body, strlen(body), 99, 0, -1, &refused);
        status = finish();
        expect(!taken && refused == 409 && status == 409 && strstr(reply_body, "canvas_unreported") && !usb_start_active() &&
               starts_sent == sent, "K7 queue start without its report: refused, never compared with a later report");
    }

    printf(fails ? "%d FAIL\n" : "PASS combined queue path: fresh report, same comparison value, refusals\n", fails);
    return fails != 0;
}
