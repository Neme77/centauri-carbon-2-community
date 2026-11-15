#define main cc2_main_original
#define mqtt_start_print mock_mqtt_start_print
#define console_emergency_stop mock_console_emergency_stop
#include "../src/main.c"
#undef main
#undef mqtt_start_print
#undef console_emergency_stop
#include <assert.h>

/* A USB import is only printed when the copy is complete and verified, it never fills the internal
 * storage past the upload reserve, and a cancelled start stops copying at once. */
int mock_console_emergency_stop(console_state *state, char *reason, size_t cap) {
    (void)state; (void)reason; (void)cap; return 0;
}
static int starts;
int mock_mqtt_start_print(mqtt_client *c, const char *media, const char *filename,
                          const int *tools, const int *trays, size_t count,
                          char layout, int bedlevel_force, int timelapse) {
    (void)c; (void)media; (void)filename; (void)tools; (void)trays; (void)count;
    (void)layout; (void)bedlevel_force; (void)timelapse;
    starts++;
    return 0;
}
static int fake_uds(const char *request, char **reply, size_t *length) {
    (void)request;
    static const char acknowledged[] = "{\"id\":1001,\"result\":{}}";
    *reply = strdup(acknowledged); *length = strlen(acknowledged);
    return *reply ? 0 : -1;
}

#define SOURCE_SIZE (8 * 65536 + 777)
static char content[SOURCE_SIZE];

static void write_source(void) {
    FILE *file = fopen("usb/part.gcode", "wb"); assert(file);
    assert(fwrite(content, 1, sizeof(content), file) == sizeof(content));
    assert(!fclose(file));
}
/* Internal copies and leftovers of this file: final imports and .part files. */
static int leftovers(void) {
    DIR *dir = opendir("gcode"); assert(dir);
    int count = 0; struct dirent *entry;
    while ((entry = readdir(dir))) if (!strncmp(entry->d_name, GCODE_USB_IMPORT_PREFIX, 8)) count++;
    closedir(dir);
    return count;
}
static int answer(int peer, char *response, size_t cap) {
    size_t used = 0; ssize_t n;
    { struct timeval rcv_limit = {30, 0}; /* never wait forever: a missing answer fails the test */
      setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &rcv_limit, sizeof(rcv_limit)); }
    while (used + 1 < cap && (n = recv(peer, response + used, cap - 1 - used, 0)) > 0) used += (size_t)n;
    response[used] = 0; close(peer);
    assert(used > 0);
    return atoi(strchr(response, ' ') + 1);
}
static void wait_worker(void) {
    for (int i = 0; i < 5000 && !atomic_load(&usb_start_done); ++i) nanosleep(&(struct timespec){0, 1000000}, NULL);
    assert(atomic_load(&usb_start_done));
}
static int begin(mqtt_client *mqtt) {
    static const char body[] = "usb\npart.gcode\n\nA\nsaved\n0";
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    assert(usb_start_begin(pair[0], mqtt, body, strlen(body)));
    return pair[1];
}

/* Free space as the printer's statvfs would report it. */
static unsigned long long fake_free;
static int small_statvfs(const char *path, struct statvfs *st) {
    (void)path; memset(st, 0, sizeof(*st));
    st->f_frsize = st->f_bsize = 4096; st->f_bavail = st->f_bfree = fake_free / 4096;
    return 0;
}

static int gate[2];
static void hold(void) { char byte; assert(read(gate[0], &byte, 1) == 1); }
static void truncate_source(void) { assert(!truncate("usb/part.gcode", SOURCE_SIZE / 2)); }
/* The worker keeps the USB file open from before the scan until the copy, so a FAT stick cannot hand
 * the same file a new inode number in between. Checked through /proc where the system has it. */
static int pinned_seen = -1;
static void check_pinned(void) {
    char wanted[PATH_MAX], link[300], target[PATH_MAX];
    DIR *fds = opendir("/proc/self/fd");
    if (!fds || !realpath("usb/part.gcode", wanted)) { if (fds) closedir(fds); return; }
    pinned_seen = 0; struct dirent *entry;
    while ((entry = readdir(fds))) {
        snprintf(link, sizeof(link), "/proc/self/fd/%s", entry->d_name);
        ssize_t n = readlink(link, target, sizeof(target) - 1);
        if (n > 0) { target[n] = 0; if (!strcmp(target, wanted)) pinned_seen = 1; }
    }
    closedir(fds);
}

/* Seams of the copy itself: after each block written, and after the copy was synced. */
static int block_action, reached[2];
/* Action 1 pauses the copy after two blocks until the main thread, which owns the start like the
 * main loop, has pressed the emergency stop. Action 2 shortens the USB file at that point. */
static void on_block(size_t copied) {
    if (copied < 2 * 65536 || !block_action) return;
    int action = block_action; block_action = 0;
    if (action == 1) { assert(write(reached[1], "x", 1) == 1); hold(); }
    if (action == 2) assert(!truncate("usb/part.gcode", SOURCE_SIZE / 2));
}
static void corrupt_part(const char *path) {
    FILE *file = fopen(path, "r+b"); assert(file);
    assert(!fseek(file, SOURCE_SIZE / 2, SEEK_SET)); assert(fputc('#', file) == '#'); assert(!fclose(file));
}

/* A test that waits for something that never comes ends red with a message instead of hanging the suite. */
static void test_too_long(int signal_number) {
    (void)signal_number;
    static const char text[] = "FAIL: the test ran longer than 300 s (an answer that never came?)\n";
    if (write(2, text, sizeof(text) - 1) < 0) {}
    _exit(1);
}
int main(void) {
    signal(SIGALRM, test_too_long); alarm(300);
    char response[1024];
    char dir[] = "usb-copy-XXXXXX"; /* the harnesses share one working directory */
    assert(mkdtemp(dir) && !chdir(dir));
    assert(!mkdir("gcode", 0700) && !mkdir("usb", 0700));
    for (size_t i = 0; i < sizeof(content); ++i) content[i] = (i % 61 == 60) ? '\n' : (char)('A' + i % 26);
    content[0] = ';';
    gcode_internal_root = "gcode"; gcode_usb_root = "usb";
    FILE *autosave = fopen("autosave.cfg", "w"); assert(autosave);
    fputs("[bed_mesh default]\nx_count = 11\ny_count = 11\npoints =\n  0, 0\n", autosave); fclose(autosave);
    printer_autosave_path = "autosave.cfg";
    canvas_color_table_uds = fake_uds;
    mqtt_client mqtt = {0};
    mqtt.connected = mqtt.registered = 1; mqtt.have_machine_status = 1; mqtt.machine_status = 1;
    assert(!pipe(gate));
    int peer, pair[2];
    static const char body[] = "usb\npart.gcode\n\nA\nsaved\n0";
    char copy[PATH_MAX_LOCAL * 2], name[PATH_MAX_LOCAL];
    assert(!gcode_usb_import_name("part.gcode", name));
    snprintf(copy, sizeof(copy), "gcode/%s", name);
    write_source();

    /* U9: an emergency stop before the copy began: nothing is copied at all. */
    usb_start_copy_hook = hold;
    peer = begin(&mqtt);
    usb_start_cancel();
    assert(write(gate[1], "x", 1) == 1); wait_worker();
    assert(leftovers() == 0 && !usb_start.imported_ok && usb_start.import_copied == 0);
    usb_start_tick(&mqtt);
    assert(answer(peer, response, sizeof(response)) == 409 && strstr(response, "emergency stop"));
    assert(starts == 0 && leftovers() == 0);

    /* U9: the browser left before the copy began: nothing is copied either. */
    peer = begin(&mqtt);
    close(peer); usb_start_tick(&mqtt);
    assert(usb_start_cancelled == 1);
    assert(write(gate[1], "x", 1) == 1); wait_worker();
    assert(leftovers() == 0 && !usb_start.imported_ok);
    usb_start_tick(&mqtt);
    assert(starts == 0 && leftovers() == 0 && !usb_start_active());
    usb_start_copy_hook = NULL;

    /* U8: the copy would leave less than the upload reserve: refused before anything is written. */
    upload_statvfs = small_statvfs;
    fake_free = UPLOAD_STORAGE_MARGIN + SOURCE_SIZE - 4096;
    peer = begin(&mqtt); wait_worker();
    assert(leftovers() == 0 && !usb_start.imported_ok);
    usb_start_tick(&mqtt);
    assert(answer(peer, response, sizeof(response)) == 507 && strstr(response, "free space"));
    assert(starts == 0 && leftovers() == 0);
    /* The same holds for a start without the worker (the program no longer starts USB files this way). */
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    assert(gcode_start_response(pair[0], &mqtt, body, strlen(body), NULL) == 507);
    close(pair[0]);
    assert(answer(pair[1], response, sizeof(response)) == 507 && strstr(response, "free space"));
    assert(starts == 0 && leftovers() == 0);
    /* Enough room for the copy and the reserve: accepted. */
    fake_free = UPLOAD_STORAGE_MARGIN + SOURCE_SIZE + 4096;
    peer = begin(&mqtt); wait_worker(); usb_start_tick(&mqtt);
    assert(answer(peer, response, sizeof(response)) == 202 && starts == 1 && leftovers() == 1);
    /* Starting the same file again: its earlier copy, replaced by this one, counts towards the
     * reserve. Here it stands in at 20 MiB, so the reserve is covered once the copy is in place. */
    assert(!truncate(copy, 20 * 1024 * 1024));
    fake_free = SOURCE_SIZE + 4096;
    starts = 0;
    peer = begin(&mqtt); wait_worker(); usb_start_tick(&mqtt);
    assert(answer(peer, response, sizeof(response)) == 202 && starts == 1 && leftovers() == 1);
    /* The copy itself must still fit while the earlier one is in place. */
    assert(!truncate(copy, 20 * 1024 * 1024));
    fake_free = SOURCE_SIZE - 4096;
    starts = 0;
    peer = begin(&mqtt); wait_worker(); usb_start_tick(&mqtt);
    assert(answer(peer, response, sizeof(response)) == 507 && strstr(response, "free space") && starts == 0);
    struct stat kept; assert(!stat(copy, &kept) && kept.st_size == 20 * 1024 * 1024);
    upload_statvfs = statvfs;
    assert(!unlink(copy));

    /* U6: the USB file changed between the scan and the copy (here: shortened to half). */
    starts = 0;
    usb_start_copy_hook = truncate_source;
    peer = begin(&mqtt); wait_worker(); usb_start_tick(&mqtt);
    assert(answer(peer, response, sizeof(response)) == 507 && strstr(response, "changed"));
    assert(starts == 0 && leftovers() == 0);
    usb_start_copy_hook = NULL; write_source();

    /* U9: an emergency stop in the middle of the copy stops it after the current block. */
    assert(!pipe(reached));
    usb_copy_block_hook = on_block; block_action = 1;
    peer = begin(&mqtt);
    char byte; assert(read(reached[0], &byte, 1) == 1);
    usb_start_cancel();
    assert(write(gate[1], "x", 1) == 1); wait_worker();
    assert(leftovers() == 0 && !usb_start.imported_ok && usb_start.import_copied < SOURCE_SIZE);
    usb_start_tick(&mqtt);
    assert(answer(peer, response, sizeof(response)) == 409 && starts == 0);

    /* U6: the USB file is shortened while it is copied. */
    block_action = 2;
    peer = begin(&mqtt); wait_worker(); usb_start_tick(&mqtt);
    assert(answer(peer, response, sizeof(response)) == 507 && strstr(response, "changed"));
    assert(starts == 0 && leftovers() == 0);
    usb_copy_block_hook = NULL; write_source();

    /* U6: the synced copy reads back different from what was read from the stick. */
    usb_copy_synced_hook = corrupt_part;
    peer = begin(&mqtt); wait_worker(); usb_start_tick(&mqtt);
    assert(answer(peer, response, sizeof(response)) == 507 && strstr(response, "verif"));
    assert(starts == 0 && leftovers() == 0);
    usb_copy_synced_hook = NULL;

    /* A clean copy is still started, byte for byte the file on the stick. */
    starts = 0; usb_start_copy_hook = check_pinned;
    peer = begin(&mqtt); wait_worker(); usb_start_tick(&mqtt);
    assert(answer(peer, response, sizeof(response)) == 202 && starts == 1);
    usb_start_copy_hook = NULL;
    if (pinned_seen < 0) puts("SKIP pinned USB file check: no /proc/self/fd");
    else assert(pinned_seen == 1);
    FILE *file = fopen(copy, "rb"); assert(file);
    static char back[SOURCE_SIZE + 1]; size_t got = fread(back, 1, sizeof(back), file); fclose(file);
    assert(got == SOURCE_SIZE && !memcmp(back, content, got));
    assert(!unlink(copy) && leftovers() == 0);
    close(gate[0]); close(gate[1]); close(reached[0]); close(reached[1]);

    puts("PASS USB copy: verified against the stick, storage reserve kept, cancel stops the copy");
    return 0;
}
