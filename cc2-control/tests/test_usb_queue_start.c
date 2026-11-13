#define main cc2_main_original
#define mqtt_start_print mock_mqtt_start_print
#define mqtt_prepare_timelapse mock_mqtt_prepare_timelapse
#include "../src/main.c"
#undef main
#undef mqtt_start_print
#undef mqtt_prepare_timelapse
#include <assert.h>

/* A queued USB job is copied on the worker like a print-dialog start: the main loop stays free, the
 * queue file keeps the job "pending" until the copy is done (a restart meanwhile leaves it to be
 * started again), and only then the job becomes "starting" and the start is sent. */
static int failures;
static void expect(int ok, const char *what) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}
static mqtt_client machine;
static int starts_sent;
int mock_mqtt_start_print(mqtt_client *c, const char *media, const char *filename, const int *tools, const int *trays,
                          size_t count, char layout, int bedlevel_force, int timelapse) {
    (void)c; (void)media; (void)filename; (void)tools; (void)trays; (void)count; (void)layout; (void)bedlevel_force; (void)timelapse;
    starts_sent++; return 0;
}
int mock_mqtt_prepare_timelapse(mqtt_client *c, int enabled) { (void)c; (void)enabled; return -1; }
static int fake_uds(const char *q, char **r, size_t *l) { (void)q; *r = strdup("{\"id\":1001,\"result\":{}}"); *l = strlen(*r); return 0; }

static int gate[2];
static void hold(void) { char byte; assert(read(gate[0], &byte, 1) == 1); }
static void release(void) { assert(write(gate[1], "x", 1) == 1); }
static void idle(void) {
    machine.connected = machine.registered = machine.have_machine_status = 1;
    machine.machine_status = 1; machine.print_state[0] = 0; machine.last_message = time(NULL);
}
static queue_item *usb_job(int id) {
    queue_item *it = &queue.items[queue.count++];
    memset(it, 0, sizeof(*it));
    it->id = id; snprintf(it->storage, sizeof(it->storage), "usb");
    snprintf(it->path, sizeof(it->path), "part.gcode"); it->side = 'A';
    snprintf(it->leveling, sizeof(it->leveling), "saved");
    it->copies = 2; it->estimated_seconds = -1; it->filament_grams = -1; it->duration_baseline = -1;
    snprintf(it->state, sizeof(it->state), "pending");
    if (queue.next_id <= id) queue.next_id = id + 1;
    return it;
}
static int pair_[2];
static int request_start(const char *id) {
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair_));
    return queue_start_response(pair_[0], &machine, id, strlen(id));
}
static int reply(char *body, size_t cap) {
    size_t used = 0; ssize_t n;
    while (used + 1 < cap && (n = recv(pair_[1], body + used, cap - 1 - used, 0)) > 0) used += (size_t)n;
    body[used] = 0; close(pair_[1]);
    return used ? atoi(strchr(body, ' ') + 1) : -1;
}
/* Open file descriptors of this process (Linux), to see that a refused automatic start closes its pair. */
static int open_fds(void) {
    DIR *dir = opendir("/proc/self/fd"); assert(dir);
    int count = 0; struct dirent *entry;
    while ((entry = readdir(dir))) if (entry->d_name[0] != '.') count++;
    closedir(dir); return count;
}
static void wait_copy(void) {
    for (int i = 0; i < 5000 && !atomic_load(&usb_start_done); ++i) nanosleep(&(struct timespec){0, 1000000}, NULL);
    assert(atomic_load(&usb_start_done));
}
static const char *saved_state(int id) {
    static char text[64];
    queue_state live = queue; queue_load();
    queue_item *it = queue_find(id);
    snprintf(text, sizeof(text), "%s", it ? it->state : "missing");
    queue = live;
    return text;
}
static int copies(void) {
    DIR *dir = opendir(gcode_internal_root); assert(dir);
    int count = 0; struct dirent *entry;
    while ((entry = readdir(dir))) if (!strncmp(entry->d_name, GCODE_USB_IMPORT_PREFIX, 8)) count++;
    closedir(dir); return count;
}

int main(void) {
    char dir[] = "/tmp/cc2-queue-usb-XXXXXX";
    assert(mkdtemp(dir));
    static char file[640], internal[640], usb[640], gcode[700], autosave[640];
    snprintf(file, sizeof(file), "%s/print-queue.json", dir); queue_path = file;
    snprintf(internal, sizeof(internal), "%s/internal", dir); assert(!mkdir(internal, 0755)); gcode_internal_root = internal;
    snprintf(usb, sizeof(usb), "%s/usb", dir); assert(!mkdir(usb, 0755)); gcode_usb_root = usb;
    snprintf(gcode, sizeof(gcode), "%s/part.gcode", usb);
    FILE *out = fopen(gcode, "w"); assert(out && fputs("G28\nG1 X10 Y10 E1\n", out) >= 0 && !fclose(out));
    snprintf(autosave, sizeof(autosave), "%s/autosave.cfg", dir); printer_autosave_path = autosave;
    out = fopen(autosave, "w"); assert(out && fputs("[bed_mesh default]\nx_count = 11\ny_count = 11\npoints =\n  0, 0\n", out) >= 0 && !fclose(out));
    canvas_color_table_uds = fake_uds;
    memset(&machine, 0, sizeof(machine)); uds_init(&telemetry); idle();
    queue_load(); assert(queue_available);
    assert(!pipe(gate));
    char body[1024];

    /* The Start button: the copy runs on the worker; the queue file still says "pending". */
    usb_job(1); assert(!queue_save());
    usb_start_copy_hook = hold;
    expect(request_start("1") == 1 && usb_start_active(), "a queued USB start hands its socket to the copy worker");
    expect(!strcmp(queue.items[0].state, "pending") && queue.active_id == 0 && !strcmp(saved_state(1), "pending"),
           "during the copy the job stays pending, also in the queue file (a restart leaves it to start again)");
    expect(printer_change_blocked() != NULL, "another start is blocked while the copy runs");
    char other[256]; int other_pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, other_pair));
    expect(queue_start_response(other_pair[0], &machine, "1", 1) == 0, "a second queue start is answered at once");
    close(other_pair[0]); ssize_t got = recv(other_pair[1], other, sizeof(other) - 1, 0); close(other_pair[1]);
    other[got > 0 ? got : 0] = 0;
    expect(strstr(other, " 409 ") != NULL, "and refused with 409");
    release(); wait_copy(); usb_start_tick(&machine);
    expect(reply(body, sizeof(body)) == 202 && strstr(body, "\"imported_from_usb\":true") && starts_sent == 1,
           "after the copy the start is sent and answered 202");
    expect(!strcmp(queue.items[0].state, "starting") && queue.active_id == 1 &&
           !strncmp(queue.items[0].print_name, GCODE_USB_IMPORT_PREFIX, 8) && !strcmp(saved_state(1), "starting"),
           "the job is starting with the copy as its print name, and saved so");

    /* The browser leaves during the copy: nothing is sent, the job stays pending, the copy is removed. */
    queue.count = 0; queue.active_id = 0; usb_job(2); assert(!queue_save()); starts_sent = 0;
    char name[PATH_MAX_LOCAL], path[2 * PATH_MAX_LOCAL + 700]; assert(!gcode_usb_import_name("part.gcode", name));
    snprintf(path, sizeof(path), "%s/%s", internal, name); unlink(path);
    expect(request_start("2") == 1, "second job: the copy starts");
    close(pair_[1]); usb_start_tick(&machine);
    release(); wait_copy(); usb_start_tick(&machine);
    expect(starts_sent == 0 && !strcmp(queue.items[0].state, "pending") && copies() == 0 && !usb_start_active(),
           "a browser gone during the copy sends nothing; the job stays pending; no copy is left");

    /* The job is removed from the queue while its file is copied: the start is refused, the copy removed. */
    expect(request_start("2") == 1, "the copy starts again");
    queue.count = 0; assert(!queue_save());
    release(); wait_copy(); usb_start_tick(&machine);
    expect(reply(body, sizeof(body)) == 409 && strstr(body, "changed while its USB file was copied") && starts_sent == 0 &&
           copies() == 0, "a job removed during the copy is not started");

    /* Another queued copy became the active one during the copy: this start is not sent. */
    usb_job(5); assert(!queue_save());
    expect(request_start("5") == 1, "a further copy starts");
    queue.active_id = 99;
    release(); wait_copy(); usb_start_tick(&machine);
    expect(reply(body, sizeof(body)) == 409 && starts_sent == 0 && !strcmp(queue_find(5)->state, "pending") && copies() == 0,
           "a start is not sent when another queued copy became active meanwhile");
    queue.active_id = 0; queue.count = 0; assert(!queue_save());

    /* The automatic policy: the copy runs on the worker; a refusal after it marks the job auto_failed. */
    usb_job(3); snprintf(queue.after, sizeof(queue.after), "auto"); queue.delay = 10;
    queue.complete_ms = monotonic_ms() - 11000; assert(!queue_save());
    queue_tick(&machine);
    expect(usb_start_active() && !strcmp(queue.items[0].state, "pending"), "the automatic start copies on the worker");
    machine.machine_status = 2; /* the printer started something meanwhile */
    release(); wait_copy(); usb_start_tick(&machine);
    expect(starts_sent == 0 && !strcmp(queue.items[0].last_result, "auto_failed") && copies() == 0,
           "an automatic start refused after the copy is marked auto_failed and leaves no copy");
    idle();
    queue.items[0].last_result[0] = 0; queue.complete_ms = monotonic_ms() - 11000; assert(!queue_save());
    queue_tick(&machine);
    usb_start_cancel(); /* emergency stop during the automatic copy */
    release(); wait_copy(); usb_start_tick(&machine);
    expect(starts_sent == 0 && !strcmp(queue.items[0].last_result, "auto_failed") && !strcmp(queue.items[0].state, "pending"),
           "an emergency stop during the automatic copy sends nothing and marks it auto_failed");
    queue.items[0].last_result[0] = 0; queue.complete_ms = monotonic_ms() - 11000; assert(!queue_save());
    queue_tick(&machine);
    release(); wait_copy(); usb_start_tick(&machine);
    expect(starts_sent == 1 && !strcmp(queue.items[0].state, "starting") && queue.active_id == 3,
           "an automatic start whose copy completes is sent");
    usb_start_copy_hook = NULL;

    /* An automatic start refused before any copy (here an invalid leveling mode) closes both ends of its pair. */
    queue.count = 0; queue.active_id = 0; idle();
    queue_item *bad = usb_job(6); snprintf(bad->leveling, sizeof(bad->leveling), "bogus");
    queue.complete_ms = monotonic_ms() - 11000; assert(!queue_save());
    int fds_before = open_fds();
    queue_tick(&machine);
    expect(!usb_start_active() && !strcmp(bad->last_result, "auto_failed") && open_fds() == fds_before,
           "a refused automatic start is marked auto_failed and leaves no descriptor open");

    /* An internal job is still started at once. */
    queue.count = 0; queue.active_id = 0; snprintf(queue.after, sizeof(queue.after), "wait");
    queue_item *it = usb_job(4); snprintf(it->storage, sizeof(it->storage), "internal");
    out = fopen(path, "w"); fclose(out); snprintf(it->path, sizeof(it->path), "%s", name); assert(!queue_save());
    /* An internal job is checked on the same worker (no copy) and started from the main loop. */
    expect(request_start("4") == 1 && !strcmp(queue_find(4)->state, "pending"), "an internal queued job is checked on the worker too");
    wait_copy(); usb_start_tick(&machine);
    expect(reply(body, sizeof(body)) == 202 && !strcmp(queue_find(4)->state, "starting"), "and started after the check");

    close(gate[0]); close(gate[1]);
    if (failures) { printf("%d FAIL\n", failures); return 1; }
    puts("PASS USB queue start: copied on the worker, pending until copied, started from the main loop");
    return 0;
}
