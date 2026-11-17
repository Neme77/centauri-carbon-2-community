#define main cc2_main_original
#define mqtt_start_print mock_mqtt_start_print
#include "../src/main.c"
#undef main
#undef mqtt_start_print
#include <assert.h>

/* Three weak points of the USB start:
 * 1. Old CC2_USB_ copies fill the internal memory: a new start must make room by removing old copies,
 *    not be refused for good.
 * 2. An emergency stop or a browser that leaves must also stop the scan before the copy and the read back.
 * 3. A .part file left by CC2 Control ending during a copy is removed when CC2 Control starts. */
static int failures;
static void expect(int ok, const char *what) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}
int mock_mqtt_start_print(mqtt_client *c, const char *media, const char *filename, const int *tools,
                          const int *trays, size_t count, char layout, int force, int timelapse) {
    (void)c; (void)media; (void)filename; (void)tools; (void)trays; (void)count; (void)layout; (void)force; (void)timelapse;
    return 0;
}
static int fake_uds(const char *q, char **r, size_t *l) {
    (void)q; static const char ok[] = "{\"id\":1001,\"result\":{}}"; *r = strdup(ok); *l = strlen(ok); return 0;
}
#define MIB (1024ULL * 1024)
/* Internal memory of a fixed size: free is the size minus all files in gcode/. */
static unsigned long long capacity;
static int fake_statvfs(const char *path, struct statvfs *st) {
    (void)path; memset(st, 0, sizeof(*st));
    unsigned long long used = 0; DIR *dir = opendir("gcode"); assert(dir); struct dirent *entry;
    while ((entry = readdir(dir))) {
        char name[PATH_MAX_LOCAL + 8]; struct stat info;
        snprintf(name, sizeof(name), "gcode/%s", entry->d_name);
        if (!lstat(name, &info) && S_ISREG(info.st_mode)) used += (unsigned long long)info.st_size;
    }
    closedir(dir);
    st->f_frsize = st->f_bsize = 1; st->f_bavail = st->f_bfree = used > capacity ? 0 : capacity - used;
    return 0;
}
static void write_size(const char *path, size_t size) {
    FILE *file = fopen(path, "wb"); assert(file);
    char line[64]; memset(line, ';', sizeof(line)); line[63] = '\n';
    for (size_t done = 0; done < size; done += sizeof(line)) assert(fwrite(line, 1, sizeof(line), file) == sizeof(line));
    assert(!fclose(file));
}
static void age(const char *path, time_t when) {
    struct timespec times[2] = {{when, 0}, {when, 0}};
    assert(!utimensat(AT_FDCWD, path, times, 0));
}
static int exists(const char *path) { struct stat info; return lstat(path, &info) == 0; }
static int answer(int peer) {
    char response[1024]; size_t used = 0; ssize_t n;
    { struct timeval rcv_limit = {30, 0}; /* never wait forever: a missing answer fails the test */
      setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &rcv_limit, sizeof(rcv_limit)); }
    while (used + 1 < sizeof(response) && (n = recv(peer, response + used, sizeof(response) - 1 - used, 0)) > 0) used += (size_t)n;
    response[used] = 0; close(peer);
    return used ? atoi(strchr(response, ' ') + 1) : -1;
}
/* Waits for the worker and lets the main loop serve it meanwhile (room for the copy is made there). */
/* The printer's status arrives every few seconds on a live link; `status_stale` plays a link that went quiet,
 * under which no old copy may be removed (gcode_delete_guard). */
static int status_stale;
/* The printer has reported its Canvas trays (a start with a mapping is judged with them) and answers each fresh
 * report the start asks for. serve() ticks until the start is answered, at most 20 s, so no case waits in recv(). */
static void canvas_reports(mqtt_client *mqtt) {
    mqtt->canvas_revision++; mqtt->canvas_discovery_complete = 1;
    mqtt->canvas_snapshot_len = (size_t)snprintf(mqtt->canvas_snapshot, sizeof(mqtt->canvas_snapshot),
        "{\"id\":2005,\"result\":{\"canvas_info\":{\"canvas_list\":[{\"canvas_id\":0,\"connected\":1,\"tray_list\":["
        "{\"tray_id\":0,\"status\":1,\"filament_type\":\"PLA\"},{\"tray_id\":1,\"status\":1,\"filament_type\":\"PLA\"},"
        "{\"tray_id\":2,\"status\":1,\"filament_type\":\"PLA\"}]}]}}}");
}
static void serve(mqtt_client *mqtt) {
    for (int i = 0; i < 20000 && (!atomic_load(&usb_start_done) || usb_start_active()); ++i) {
        mqtt->last_message = status_stale ? time(NULL) - 60 : time(NULL);
        if (mqtt->canvas_refresh_due) { mqtt->canvas_refresh_due = 0; canvas_reports(mqtt); }
        usb_start_tick(mqtt);
        if (usb_start_active()) nanosleep(&(struct timespec){0, 1000000}, NULL);
    }
    usb_start_tick(mqtt);
}
static int copy_started;
static void note_copy(void) { copy_started = 1; }
/* A stick that stops answering: the worker stands still (no heartbeat) for 1.5 s, as in a fread() the kernel holds. */
/* A slow but healthy stick: every block takes 30 ms. */
static void slow_block(size_t copied) { (void)copied; nanosleep(&(struct timespec){0, 30000000}, NULL); }
static void stick_hangs(void) { nanosleep(&(struct timespec){1, 500000000}, NULL); }
static void cancel_now(const char *path) { (void)path; usb_start_cancelled = 2; }
/* Runs the cleanup while a copy is between its write and its read back: the copy still holds its .part. */
static int removed_during_copy = -1, part_there_during_copy;
static void cleanup_during_copy(const char *path) {
    removed_during_copy = gcode_usb_part_cleanup();
    struct stat info; part_there_during_copy = lstat(path, &info) == 0;
}
/* Emergency stop after the second block of the phase `stop_phase` (0 scan in one pass, 1 read back). */
static int stop_phase = -1, stop_blocks, blocks_after_stop;
static void stop_in_block(int phase) {
    if (phase != stop_phase) return;
    if (atomic_load(&usb_start_cancelled)) { blocks_after_stop++; return; }
    if (++stop_blocks == 2) usb_start_cancelled = 2;
}
/* Runs one start whose emergency stop comes in the middle of `phase`; returns the answer. */
static int stop_midway(mqtt_client *mqtt, const char *body, int phase) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    stop_phase = phase; stop_blocks = blocks_after_stop = 0; usb_read_block_hook = stop_in_block;
    assert(usb_start_begin(pair[0], mqtt, body, strlen(body)));
    for (int i = 0; i < 5000 && !atomic_load(&usb_start_done); ++i) nanosleep(&(struct timespec){0, 1000000}, NULL);
    usb_start_tick(mqtt);
    usb_read_block_hook = NULL; stop_phase = -1;
    return answer(pair[1]);
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
    char dir[] = "usb-fixes-XXXXXX"; /* the harnesses share one working directory */
    assert(mkdtemp(dir) && !chdir(dir));
    assert(!mkdir("gcode", 0700) && !mkdir("usb", 0700));
    FILE *file = fopen("autosave.cfg", "w"); assert(file);
    fputs("[bed_mesh default]\nx_count = 11\ny_count = 11\npoints =\n  0, 0\n", file); fclose(file);
    gcode_internal_root = "gcode"; gcode_usb_root = "usb"; printer_autosave_path = "autosave.cfg";
    canvas_color_table_uds = fake_uds;
    mqtt_client mqtt = {0};
    mqtt.connected = mqtt.registered = 1; mqtt.have_machine_status = 1; mqtt.machine_status = 1;
    mqtt.last_message = time(NULL); canvas_reports(&mqtt);
    char body[256]; int pair[2];

    /* 1. Memory full of old copies: three old 1 MiB copies, room for the new 1 MiB file only once one goes. */
    write_size("gcode/CC2_USB_00000001_a.gcode", MIB); age("gcode/CC2_USB_00000001_a.gcode", 1000);
    write_size("gcode/CC2_USB_00000002_b.gcode", MIB); age("gcode/CC2_USB_00000002_b.gcode", 2000);
    write_size("gcode/CC2_USB_00000003_c.gcode", MIB); age("gcode/CC2_USB_00000003_c.gcode", 3000);
    write_size("usb/new.gcode", MIB);
    capacity = UPLOAD_STORAGE_MARGIN + 3 * MIB + MIB / 2; /* 16.5 MiB free, the start needs 17 MiB */
    upload_statvfs = fake_statvfs;
    snprintf(body, sizeof(body), "usb\nnew.gcode\n\nA\nsaved\n0");
    /* Variant: without a fresh printer status no old copy goes, and the start is refused for lack of room. */
    status_stale = 1; mqtt.last_message = time(NULL);
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    if (usb_start_begin(pair[0], &mqtt, body, strlen(body))) serve(&mqtt);
    int status = answer(pair[1]);
    expect(status != 202 && exists("gcode/CC2_USB_00000001_a.gcode"),
           "variant: a printer status that went quiet removes no old copy and refuses the start");
    status_stale = 0; mqtt.last_message = time(NULL);
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    if (usb_start_begin(pair[0], &mqtt, body, strlen(body))) serve(&mqtt);
    status = answer(pair[1]);
    expect(status == 202, "a start that only fits without the oldest copy makes room and is started");
    expect(!exists("gcode/CC2_USB_00000001_a.gcode"), "the oldest copy made the room");
    expect(exists("gcode/CC2_USB_00000002_b.gcode") && exists("gcode/CC2_USB_00000003_c.gcode"),
           "the newer copies stay: only as much is removed as the start needs");
    /* No room even with every old copy gone: refused, and the old copies are left alone. */
    write_size("usb/huge.gcode", 8 * MIB);
    snprintf(body, sizeof(body), "usb\nhuge.gcode\n\nA\nsaved\n0");
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    if (usb_start_begin(pair[0], &mqtt, body, strlen(body))) serve(&mqtt);
    status = answer(pair[1]);
    expect(status == 507 && exists("gcode/CC2_USB_00000002_b.gcode") && exists("gcode/CC2_USB_00000003_c.gcode"),
           "a file that cannot fit anyway is refused without removing old copies");
    /* 1b. A start the check before the copy refuses (adaptive leveling, no adaptive mesh in the file) must not
     * cost old copies: room is made only right before the copy. */
    write_size("usb/flat.gcode", MIB);
    snprintf(body, sizeof(body), "usb\nflat.gcode\n\nA\nadaptive\n0");
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    if (usb_start_begin(pair[0], &mqtt, body, strlen(body))) serve(&mqtt);
    status = answer(pair[1]);
    expect(status == 409 && exists("gcode/CC2_USB_00000002_b.gcode") && exists("gcode/CC2_USB_00000003_c.gcode"),
           "a start refused by the check before the copy removes no old copies");
    /* 1c. The main loop is busy longer than the worker waits for room (USB_START_ROOM_WAIT_MS): the start is
     * refused for lack of room, and no old copy may be removed for it afterwards. */
    write_size("gcode/CC2_USB_00000001_a.gcode", MIB); age("gcode/CC2_USB_00000001_a.gcode", 1000);
    write_size("usb/late.gcode", MIB);
    snprintf(body, sizeof(body), "usb\nlate.gcode\n\nA\nsaved\n0");
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    if (usb_start_begin(pair[0], &mqtt, body, strlen(body))) {
        nanosleep(&(struct timespec){USB_START_ROOM_WAIT_MS / 1000 + 1, 0}, NULL); /* main loop held up */
        serve(&mqtt);
    }
    status = answer(pair[1]);
    expect(status == 507 && exists("gcode/CC2_USB_00000001_a.gcode"),
           "a main loop too late for the room refuses the start and removes no old copy afterwards");
    unlink("gcode/CC2_USB_00000001_a.gcode");
    /* 1d. An upload into a memory full of old USB copies: the oldest copies make room as for a USB start (before
     * the upload begins, on the main loop), only as many as needed; without a fresh printer status nothing goes
     * and the upload is refused with 507 as before. */
    write_size("gcode/CC2_USB_00000001_a.gcode", MIB); age("gcode/CC2_USB_00000001_a.gcode", 1000);
    {
        struct statvfs room; assert(!fake_statvfs("gcode", &room));
        unsigned long long missing = (unsigned long long)MIB + UPLOAD_STORAGE_MARGIN; /* what a 1 MiB upload needs */
        expect(room.f_bavail < missing, "1d the upload does not fit without removing a copy");
        mqtt.last_message = time(NULL) - 60;
        assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
        expect(!upload_space_or_room(pair[0], &mqtt, gcode_internal_root, MIB, 1) && exists("gcode/CC2_USB_00000001_a.gcode"),
               "1d without a fresh printer status no copy goes and the upload is refused");
        close(pair[0]); close(pair[1]);
        mqtt.last_message = time(NULL);
        assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
        expect(upload_space_or_room(pair[0], &mqtt, gcode_internal_root, MIB, 1) && !exists("gcode/CC2_USB_00000001_a.gcode"),
               "1d an upload into a full memory makes room from the oldest USB copy");
        close(pair[0]); close(pair[1]);
    }
    upload_statvfs = statvfs;

    /* 2a. Emergency stop during the scan before the copy: the scan ends, nothing is copied. */
    write_size("usb/big.gcode", 64 * MIB);
    usb_start_copy_hook = note_copy; copy_started = 0;
    snprintf(body, sizeof(body), "usb\nbig.gcode\n\nA\nsaved\n0");
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    assert(usb_start_begin(pair[0], &mqtt, body, strlen(body)));
    usb_start_cancel();
    for (int i = 0; i < 60000 && !atomic_load(&usb_start_done); ++i) nanosleep(&(struct timespec){0, 1000000}, NULL);
    int stopped_before_copy = !copy_started;
    usb_start_tick(&mqtt);
    expect(answer(pair[1]) == 409, "the start is refused after the emergency stop");
    expect(stopped_before_copy, "an emergency stop during the scan stops it before the copy begins");
    usb_start_copy_hook = NULL;

    /* 2b. Emergency stop during the read back: the check ends as cancelled. */
    write_size("usb/small.gcode", 256 * 1024);
    usb_copy_synced_hook = cancel_now;
    snprintf(body, sizeof(body), "usb\nsmall.gcode\n\nA\nsaved\n0");
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    assert(usb_start_begin(pair[0], &mqtt, body, strlen(body)));
    for (int i = 0; i < 5000 && !atomic_load(&usb_start_done); ++i) nanosleep(&(struct timespec){0, 1000000}, NULL);
    int import_error = usb_start.import_error;
    usb_start_tick(&mqtt);
    expect(answer(pair[1]) == 409, "the start is refused after the emergency stop");
    expect(import_error == GCODE_IMPORT_CANCELLED, "an emergency stop before the read back cancels it instead of reading on");
    char small[PATH_MAX_LOCAL], path[PATH_MAX_LOCAL + 16];
    assert(!gcode_usb_import_name("small.gcode", small));
    snprintf(path, sizeof(path), "gcode/%s", small);
    expect(!exists(path), "and no copy is left");
    usb_copy_synced_hook = NULL;

    /* 2c. Emergency stop in the middle of the one-pass scan (USB, with a mapping): it ends after that block,
     * and no copy begins. 2d. The same on an internal start. 2e. In the middle of the read back. */
    file = fopen("usb/long.gcode", "w"); assert(file);
    fputs("; filament_type = PLA;PLA\nT0\n", file);
    for (int i = 0; i < 40000; ++i) fputs("G1 X10 Y10 E1 ; a line of moves\n", file);
    fputs("T1\n", file); fclose(file);
    assert(!link("usb/long.gcode", "gcode/long.gcode"));
    usb_start_copy_hook = note_copy; copy_started = 0;
    status = stop_midway(&mqtt, "usb\nlong.gcode\n0:1,1:2\nA\nsaved\n0", 0);
    expect(status == 409 && !copy_started && blocks_after_stop == 0,
           "an emergency stop in the middle of the one-pass scan ends it after that block, before any copy");
    status = stop_midway(&mqtt, "internal\nlong.gcode\n0:1,1:2\nA\nsaved\n0", 0);
    expect(status == 409 && blocks_after_stop == 0, "the same on an internal start");
    usb_start_copy_hook = NULL;
    status = stop_midway(&mqtt, "usb\nlong.gcode\n\nA\nsaved\n0", 1);
    assert(!gcode_usb_import_name("long.gcode", small));
    snprintf(path, sizeof(path), "gcode/%s", small);
    expect(status == 409 && blocks_after_stop == 0 && !exists(path),
           "an emergency stop in the middle of the read back ends it after that block and leaves no copy");
    unlink("gcode/long.gcode");

    /* 4. With a slot mapping and no cached inspection the checks read the file once, not once per check. */
    file = fopen("usb/two.gcode", "w"); assert(file);
    fputs("; filament_type = PLA;PLA\nG28\nT0\nG1 X10 Y10 E1\nT1\nG1 X20 Y20 E1\n", file); fclose(file);
    atomic_store(&gcode_scan_passes, 0);
    snprintf(body, sizeof(body), "usb\ntwo.gcode\n0:0,1:1\nA\nsaved\n0");
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    if (usb_start_begin(pair[0], &mqtt, body, strlen(body))) serve(&mqtt);
    status = answer(pair[1]);
    expect(status == 202 && atomic_load(&gcode_scan_passes) == 1, "mesh and tools are checked in one pass over the file");

    /* 4b. Variant: without a slot mapping only the mesh check runs, one pass on any version. */
    atomic_store(&gcode_scan_passes, 0);
    snprintf(body, sizeof(body), "usb\ntwo.gcode\n\nA\nsaved\n0");
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    if (usb_start_begin(pair[0], &mqtt, body, strlen(body))) serve(&mqtt);
    status = answer(pair[1]);
    expect(status == 202 && atomic_load(&gcode_scan_passes) == 1, "variant: without a mapping the file is read once as well");

    /* 4c. The print dialog's analysis reads the same file right now: the start waits for it and does not read the
     * stick a second time in parallel (two threads on one USB 2.0 stick); afterwards it scans as usual. */
    analysis_reading_set("usb", "two.gcode"); atomic_store(&gcode_scan_passes, 0);
    snprintf(body, sizeof(body), "usb\ntwo.gcode\n0:0,1:1\nA\nsaved\n0");
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    assert(usb_start_begin(pair[0], &mqtt, body, strlen(body)));
    nanosleep(&(struct timespec){0, 200000000}, NULL);
    expect(atomic_load(&gcode_scan_passes) == 0 && !atomic_load(&usb_start_done),
           "a start waits while the dialog's analysis reads the same file");
    analysis_reading_set(NULL, NULL);
    serve(&mqtt);
    status = answer(pair[1]);
    expect(status == 202 && atomic_load(&gcode_scan_passes) == 1, "and checks the file once the analysis is done");

    /* 4d. Fund 12: an earlier copy of the same USB file exists, and the touchscreen starts a print while the new copy
     * is made. The new copy must not replace the earlier one under that print: it is published (renamed over the
     * earlier copy) only once the printer is confirmed idle right before the start is sent; otherwise the earlier
     * copy stays untouched and the new one is removed. */
    {
        char name12[PATH_MAX_LOCAL], path12[PATH_MAX_LOCAL + 16];
        assert(!gcode_usb_import_name("same.gcode", name12));
        snprintf(path12, sizeof(path12), "gcode/%s", name12);
        file = fopen(path12, "w"); assert(file); fputs("; earlier copy\nG1 X1\n", file); fclose(file);
        file = fopen("usb/same.gcode", "w"); assert(file); fputs("; new USB file\nG1 X2\nG1 X3\n", file); fclose(file);
        snprintf(body, sizeof(body), "usb\nsame.gcode\n\nA\nsaved\n0");
        assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
        assert(usb_start_begin(pair[0], &mqtt, body, strlen(body)));
        for (int i = 0; i < 5000 && !atomic_load(&usb_start_done); ++i) nanosleep(&(struct timespec){0, 1000000}, NULL);
        mqtt.machine_status = 2; /* the touchscreen started the earlier copy meanwhile */
        serve(&mqtt);
        status = answer(pair[1]);
        char held[64] = ""; file = fopen(path12, "r"); assert(file); if (!fgets(held, sizeof(held), file)) held[0] = 0; fclose(file);
        char part12[PATH_MAX_LOCAL + 24]; snprintf(part12, sizeof(part12), "%s.part", path12);
        expect(status == 409 && !strcmp(held, "; earlier copy\n") && !exists(part12),
               "a print started meanwhile keeps the earlier copy untouched; the new copy is not published");
        mqtt.machine_status = 1;
        assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
        assert(usb_start_begin(pair[0], &mqtt, body, strlen(body)));
        serve(&mqtt);
        status = answer(pair[1]);
        file = fopen(path12, "r"); assert(file); if (!fgets(held, sizeof(held), file)) held[0] = 0; fclose(file);
        expect(status == 202 && !strcmp(held, "; new USB file\n") && !exists(part12),
               "on an idle printer the new copy replaces the earlier one when the start is sent");
        unlink(path12);
    }

    /* 4e. Fund 4: the stick stops answering during the start. After usb_start_stall_ms without progress the browser
     * gets an answer at once ("does not respond", reason usb_unresponsive); the start stays busy until the worker
     * comes back (a second start is refused meanwhile), then it is cleaned up without a second answer and nothing
     * is printed. */
    usb_start_stall_ms = 200; usb_start_copy_hook = stick_hangs;
    snprintf(body, sizeof(body), "usb\nsmall.gcode\n\nA\nsaved\n0");
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    assert(usb_start_begin(pair[0], &mqtt, body, strlen(body)));
    char hang_reply[1024] = ""; ssize_t hang_got = -1;
    for (int i = 0; i < 1000 && hang_got <= 0; ++i) {
        mqtt.last_message = time(NULL); usb_start_tick(&mqtt);
        hang_got = recv(pair[1], hang_reply, sizeof(hang_reply) - 1, MSG_DONTWAIT);
        if (hang_got <= 0) nanosleep(&(struct timespec){0, 1000000}, NULL);
    }
    if (hang_got > 0) hang_reply[hang_got] = 0;
    expect(hang_got > 0 && strstr(hang_reply, " 409 ") && strstr(hang_reply, "\"reason\":\"usb_unresponsive\"") &&
           !atomic_load(&usb_start_done), "a stick that stops answering gets an answer after the limit, while the worker still hangs");
    {
        int other[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, other));
        int taken = usb_start_begin(other[0], &mqtt, body, strlen(body));
        if (!taken) close(other[0]);
        expect(!taken && answer(other[1]) == 409, "a second start is refused while the hung worker is still busy");
    }
    serve(&mqtt);
    close(pair[1]);
    snprintf(path, sizeof(path), "gcode/%s", small);
    expect(!usb_start_active() && !exists(path), "when the worker comes back the start is cleaned up and no copy is left");
    usb_start_copy_hook = NULL; usb_start_stall_ms = USB_START_STALL_MS;

    /* 4f. A slow but healthy stick takes longer than the limit in total, but every block comes: no false alarm. */
    usb_start_stall_ms = 200; usb_copy_block_hook = slow_block;
    write_size("usb/slow.gcode", MIB); /* 16 blocks of 64 KiB, 30 ms each: about 0.5 s */
    snprintf(body, sizeof(body), "usb\nslow.gcode\n\nA\nsaved\n0");
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    if (usb_start_begin(pair[0], &mqtt, body, strlen(body))) serve(&mqtt);
    status = answer(pair[1]);
    expect(status == 202, "a slow stick with progress on every block is not taken for a hung one");
    usb_copy_block_hook = NULL; usb_start_stall_ms = USB_START_STALL_MS;

    /* 4g. e3 the other way round: a start reads the file, then the dialog's analysis of the SAME file arrives (second
     * browser, the queue, a slow network). It does not read in parallel: it is answered at once with 409, reason
     * start_reading; another file is analysed as usual; once the start has read, the analysis is accepted again. */
    usb_start_copy_hook = stick_hangs; /* holds the worker 1.5 s while it reads */
    snprintf(body, sizeof(body), "usb\nsmall.gcode\n\nA\nsaved\n0");
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    assert(usb_start_begin(pair[0], &mqtt, body, strlen(body)));
    {
        int a[2]; char reply[512] = "";
        assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, a));
        int queued = analysis_start(a[0], 0, "usb\nsmall.gcode", strlen("usb\nsmall.gcode"));
        if (!queued) close(a[0]);
        ssize_t got = queued ? -1 : recv(a[1], reply, sizeof(reply) - 1, 0); if (got > 0) reply[got] = 0;
        close(a[1]);
        expect(!queued && strstr(reply, " 409 ") && strstr(reply, "\"reason\":\"start_reading\""),
               "an analysis of the file a start is reading is refused at once, not read in parallel");
        int b[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, b));
        int other = analysis_start(b[0], 0, "usb\ntwo.gcode", strlen("usb\ntwo.gcode"));
        expect(other, "an analysis of another file is taken as usual");
        if (!other) close(b[0]);
        close(b[1]);
    }
    serve(&mqtt);
    status = answer(pair[1]);
    usb_start_copy_hook = NULL;
    {
        int c[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, c));
        int again = analysis_start(c[0], 0, "usb\nsmall.gcode", strlen("usb\nsmall.gcode"));
        expect(status == 202 && again, "once the start has read the file, its analysis is taken again");
        if (!again) close(c[0]);
        close(c[1]);
    }
    for (int i = 0; i < 200; ++i) { /* let the analysis worker finish before the next case */
        pthread_mutex_lock(&analysis_mutex); int busy = analysis_running; pthread_mutex_unlock(&analysis_mutex);
        if (!busy) break;
        nanosleep(&(struct timespec){0, 10000000}, NULL);
    }

    /* 5. After the read back the copy's pages are dropped from the cache too. */
    usb_copy_readback_dropped = 0;
    snprintf(body, sizeof(body), "usb\nsmall.gcode\n\nA\nsaved\n0");
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    if (usb_start_begin(pair[0], &mqtt, body, strlen(body))) serve(&mqtt);
    status = answer(pair[1]);
    expect(status == 202 && usb_copy_readback_dropped == 1, "the read back drops the copy from the cache");
    /* 5b. A copy over 8 MiB: its pages are dropped while it is read back, every 8 MiB, not only at the end. */
    write_size("usb/big.gcode", 17 * MIB);
    usb_copy_readback_dropped = 0;
    snprintf(body, sizeof(body), "usb\nbig.gcode\n\nA\nsaved\n0");
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    if (usb_start_begin(pair[0], &mqtt, body, strlen(body))) serve(&mqtt);
    status = answer(pair[1]);
    expect(status == 202 && usb_copy_readback_dropped == 3,
           "a 17 MiB read back drops the pages after 8 and 16 MiB and at the end");

    /* 6. An internal start a linked printer cannot take (busy) is refused before the file is read. Without a
     * printer link it is answered as develop answers it, at once: the request and the file first, then the printer. */
    write_size("gcode/inside.gcode", 4 * MIB);
    file = fopen("gcode/two.gcode", "w"); assert(file); fputs("T0\nG1 X1\nT1\nG1 X2\n", file); fclose(file);
    mqtt.machine_status = 2; atomic_store(&gcode_scan_passes, 0);
    snprintf(body, sizeof(body), "internal\ninside.gcode\n\nA\nsaved\n0");
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    int taken = usb_start_begin(pair[0], &mqtt, body, strlen(body));
    if (taken) serve(&mqtt); else close(pair[0]);
    status = answer(pair[1]);
    expect(!taken && status == 409 && atomic_load(&gcode_scan_passes) == 0,
           "a linked, busy printer refuses an internal start before the file is read");
    /* 6b. Linked and busy, with a mapping that misses a tool: still refused before reading (no file check). */
    snprintf(body, sizeof(body), "internal\ntwo.gcode\n0:1\nA\nsaved\n0");
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    taken = usb_start_begin(pair[0], &mqtt, body, strlen(body));
    if (taken) serve(&mqtt); else close(pair[0]);
    status = answer(pair[1]);
    expect(!taken && status == 409 && atomic_load(&gcode_scan_passes) == 0,
           "variant: linked and busy, wrong mapping: refused before the file is read");
    mqtt.machine_status = 1;
    /* 6c. No printer link: a wrong mapping is still answered with 409 (tests/test_gcode_files.py holds this),
     * a correct one with 503. */
    mqtt.registered = 0;
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    taken = usb_start_begin(pair[0], &mqtt, body, strlen(body));
    if (taken) serve(&mqtt); else close(pair[0]);
    status = answer(pair[1]);
    expect(status == 409, "no printer link, wrong mapping: 409 from the file check, as in develop");
    snprintf(body, sizeof(body), "internal\ninside.gcode\n\nA\nsaved\n0");
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    taken = usb_start_begin(pair[0], &mqtt, body, strlen(body));
    if (taken) serve(&mqtt); else close(pair[0]);
    status = answer(pair[1]);
    expect(status == 503, "no printer link, file in order: 503");
    mqtt.registered = 1;

    /* 3. A .part left behind when CC2 Control ended during a copy (procd starts it again at once, so the .part
     * is fresh): removed whatever its age, unless a copy holds its lock (flock) - that copy may run in another
     * instance on the same folders. Old and fresh leftovers both go; the locked one, other .part files,
     * finished copies and links stay. */
    write_size("gcode/CC2_USB_0000000a_left.gcode.part", 4096);
    age("gcode/CC2_USB_0000000a_left.gcode.part", time(NULL) - 3600);
    write_size("gcode/CC2_USB_0000000e_crash.gcode.part", 4096); /* just left by a crash */
    write_size("gcode/CC2_USB_0000000d_live.gcode.part", 4096);
    int live = open("gcode/CC2_USB_0000000d_live.gcode.part", O_RDONLY);
    assert(live >= 0 && !flock(live, LOCK_EX | LOCK_NB));
    write_size("gcode/mine.gcode.part", 4096);
    write_size("gcode/CC2_USB_0000000b_kept.gcode", 4096);
    assert(!symlink("CC2_USB_0000000b_kept.gcode", "gcode/CC2_USB_0000000c_link.gcode.part"));
    expect(gcode_usb_part_cleanup() == 2, "two leftover .part files are removed at start");
    expect(!exists("gcode/CC2_USB_0000000a_left.gcode.part"), "the old CC2_USB_ .part is gone");
    expect(!exists("gcode/CC2_USB_0000000e_crash.gcode.part"), "a fresh .part without a lock is gone too");
    expect(exists("gcode/CC2_USB_0000000d_live.gcode.part"), "a .part a running copy holds locked stays");
    expect(exists("gcode/mine.gcode.part") && exists("gcode/CC2_USB_0000000b_kept.gcode") &&
           exists("gcode/CC2_USB_0000000c_link.gcode.part"),
           "other .part files, finished copies and links stay");
    close(live);
    /* 3b. Before every USB start, too: a leftover from a crash while CC2 Control kept running is removed. */
    write_size("gcode/CC2_USB_0000000f_late.gcode.part", 4096);
    write_size("usb/after.gcode", 4096);
    snprintf(body, sizeof(body), "usb\nafter.gcode\n\nA\nsaved\n0");
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    if (usb_start_begin(pair[0], &mqtt, body, strlen(body))) serve(&mqtt);
    status = answer(pair[1]);
    expect(status == 202 && !exists("gcode/CC2_USB_0000000f_late.gcode.part") &&
           !exists("gcode/CC2_USB_0000000d_live.gcode.part"),
           "a USB start removes leftover .part files without a lock before it copies");
    /* 3c. A running copy holds its own .part: a cleanup meanwhile (another instance starting) leaves it. */
    usb_copy_synced_hook = cleanup_during_copy;
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    if (usb_start_begin(pair[0], &mqtt, body, strlen(body))) serve(&mqtt);
    status = answer(pair[1]);
    usb_copy_synced_hook = NULL;
    expect(status == 202 && removed_during_copy == 0 && part_there_during_copy,
           "a cleanup during a copy leaves the .part the copy holds");

    if (failures) { printf("%d FAIL\n", failures); return 1; }
    puts("PASS USB start fixes: room from old copies, stop in scan and read back, leftover .part removed");
    return 0;
}
