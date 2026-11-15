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
    while (used + 1 < sizeof(response) && (n = recv(peer, response + used, sizeof(response) - 1 - used, 0)) > 0) used += (size_t)n;
    response[used] = 0; close(peer);
    return used ? atoi(strchr(response, ' ') + 1) : -1;
}
/* Waits for the worker and lets the main loop serve it meanwhile (room for the copy is made there). */
/* The printer's status arrives every few seconds on a live link; `status_stale` plays a link that went quiet,
 * under which no old copy may be removed (gcode_delete_guard). */
static int status_stale;
static void serve(mqtt_client *mqtt) {
    for (int i = 0; i < 20000 && !atomic_load(&usb_start_done); ++i) {
        mqtt->last_message = status_stale ? time(NULL) - 60 : time(NULL);
        usb_start_tick(mqtt);
        nanosleep(&(struct timespec){0, 1000000}, NULL);
    }
    usb_start_tick(mqtt);
}
static int copy_started;
static void note_copy(void) { copy_started = 1; }
static void cancel_now(const char *path) { (void)path; usb_start_cancelled = 2; }

int main(void) {
    char dir[] = "usb-fixes-XXXXXX"; /* the harnesses share one working directory */
    assert(mkdtemp(dir) && !chdir(dir));
    assert(!mkdir("gcode", 0700) && !mkdir("usb", 0700));
    FILE *file = fopen("autosave.cfg", "w"); assert(file);
    fputs("[bed_mesh default]\nx_count = 11\ny_count = 11\npoints =\n  0, 0\n", file); fclose(file);
    gcode_internal_root = "gcode"; gcode_usb_root = "usb"; printer_autosave_path = "autosave.cfg";
    canvas_color_table_uds = fake_uds;
    mqtt_client mqtt = {0};
    mqtt.connected = mqtt.registered = 1; mqtt.have_machine_status = 1; mqtt.machine_status = 1;
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

    /* 5. After the read back the copy's pages are dropped from the cache too. */
    usb_copy_readback_dropped = 0;
    snprintf(body, sizeof(body), "usb\nsmall.gcode\n\nA\nsaved\n0");
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    if (usb_start_begin(pair[0], &mqtt, body, strlen(body))) serve(&mqtt);
    status = answer(pair[1]);
    expect(status == 202 && usb_copy_readback_dropped == 1, "the read back drops the copy from the cache");

    /* 6. An internal start the printer cannot take (busy, or no printer link) is answered as develop answers
     * it: the request and the file are checked first, then the printer. The file is read on the worker, so the
     * main loop is never held up by it (develop read it there). */
    write_size("gcode/inside.gcode", 4 * MIB);
    file = fopen("gcode/two.gcode", "w"); assert(file); fputs("T0\nG1 X1\nT1\nG1 X2\n", file); fclose(file);
    mqtt.machine_status = 2;
    snprintf(body, sizeof(body), "internal\ninside.gcode\n\nA\nsaved\n0");
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    int taken = usb_start_begin(pair[0], &mqtt, body, strlen(body));
    if (taken) serve(&mqtt); else close(pair[0]);
    status = answer(pair[1]);
    expect(status == 409, "a busy printer refuses an internal start");
    /* 6b. Busy printer and a mapping that misses a tool: the file check answers first, as in develop. */
    snprintf(body, sizeof(body), "internal\ntwo.gcode\n0:1\nA\nsaved\n0");
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    taken = usb_start_begin(pair[0], &mqtt, body, strlen(body));
    if (taken) serve(&mqtt); else close(pair[0]);
    status = answer(pair[1]);
    expect(taken && status == 409, "variant: busy printer, wrong mapping: refused by the file check on the worker");
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

    /* 3. A .part left behind when CC2 Control ended during a copy: removed once it is older than the
     * time a running copy leaves between two writes; a fresh one may belong to another instance. */
    write_size("gcode/CC2_USB_0000000a_left.gcode.part", 4096);
    age("gcode/CC2_USB_0000000a_left.gcode.part", time(NULL) - 3600);
    write_size("gcode/CC2_USB_0000000d_live.gcode.part", 4096);
    write_size("gcode/mine.gcode.part", 4096);
    write_size("gcode/CC2_USB_0000000b_kept.gcode", 4096);
    assert(!symlink("CC2_USB_0000000b_kept.gcode", "gcode/CC2_USB_0000000c_link.gcode.part"));
    expect(gcode_usb_part_cleanup() == 1, "one leftover .part is removed at start");
    expect(!exists("gcode/CC2_USB_0000000a_left.gcode.part"), "the CC2_USB_ .part is gone");
    expect(exists("gcode/CC2_USB_0000000d_live.gcode.part"), "a fresh .part, perhaps of another instance's copy, stays");
    expect(exists("gcode/mine.gcode.part") && exists("gcode/CC2_USB_0000000b_kept.gcode") &&
           exists("gcode/CC2_USB_0000000c_link.gcode.part"),
           "other .part files, finished copies and links stay");

    if (failures) { printf("%d FAIL\n", failures); return 1; }
    puts("PASS USB start fixes: room from old copies, stop in scan and read back, leftover .part removed");
    return 0;
}
