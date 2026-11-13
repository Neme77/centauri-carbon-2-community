#define main cc2_main_original
#define mqtt_start_print mock_mqtt_start_print
#include "../src/main.c"
#undef main
#undef mqtt_start_print
#include <assert.h>

/* USB starts leave CC2_USB_ copies in internal memory. Only the newest three stay; an older one is
 * removed only where the file list would allow deleting it and no queued job names it. */
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
static void write_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb"); assert(file); assert(fputs(text, file) >= 0); assert(!fclose(file));
}
static void age(const char *path, time_t when) {
    struct timespec times[2] = {{when, 0}, {when, 0}};
    assert(!utimensat(AT_FDCWD, path, times, 0));
}
static int exists(const char *name) {
    char path[PATH_MAX_LOCAL + 16]; snprintf(path, sizeof(path), "gcode/%s", name);
    struct stat info; return lstat(path, &info) == 0;
}
static int imports(void) {
    DIR *dir = opendir("gcode"); assert(dir);
    int count = 0; struct dirent *entry;
    while ((entry = readdir(dir))) if (!strncmp(entry->d_name, GCODE_USB_IMPORT_PREFIX, 8) && !strstr(entry->d_name, ".part")) count++;
    closedir(dir);
    return count;
}
static int answer(int peer) {
    char response[1024]; size_t used = 0; ssize_t n;
    while (used + 1 < sizeof(response) && (n = recv(peer, response + used, sizeof(response) - 1 - used, 0)) > 0) used += (size_t)n;
    response[used] = 0; close(peer);
    return used ? atoi(strchr(response, ' ') + 1) : -1;
}
static int start_from_usb(mqtt_client *mqtt, const char *file) {
    char body[256]; snprintf(body, sizeof(body), "usb\n%s\n\nA\nsaved\n0", file);
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    assert(usb_start_begin(pair[0], mqtt, body, strlen(body)));
    for (int i = 0; i < 5000 && !atomic_load(&usb_start_done); ++i) nanosleep(&(struct timespec){0, 1000000}, NULL);
    usb_start_tick(mqtt);
    return answer(pair[1]);
}
static void copy_named(const char *usb, char name[PATH_MAX_LOCAL]) { assert(!gcode_usb_import_name(usb, name)); }

int main(void) {
    char dir[] = "usb-prune-XXXXXX"; /* the harnesses share one working directory */
    assert(mkdtemp(dir) && !chdir(dir));
    assert(!mkdir("gcode", 0700) && !mkdir("usb", 0700));
    write_file("autosave.cfg", "[bed_mesh default]\nx_count = 11\ny_count = 11\npoints =\n  0, 0\n");
    gcode_internal_root = "gcode"; gcode_usb_root = "usb"; printer_autosave_path = "autosave.cfg";
    canvas_color_table_uds = fake_uds;
    mqtt_client mqtt = {0};
    mqtt.connected = mqtt.registered = 1; mqtt.have_machine_status = 1; mqtt.machine_status = 1;
    mqtt.last_message = time(NULL);
    time_t t0 = time(NULL) - 10000;

    /* --- Four USB starts of four files: the oldest copy goes, three stay. --- */
    char names[4][PATH_MAX_LOCAL];
    for (int i = 0; i < 4; i++) {
        char usb[32], copy[sizeof(names) + 16]; snprintf(usb, sizeof(usb), "part%d.gcode", i);
        char path[64]; snprintf(path, sizeof(path), "usb/%s", usb); write_file(path, "G28\nG1 X10 Y10 E1\n");
        expect(start_from_usb(&mqtt, usb) == 202, "a USB start is accepted");
        copy_named(usb, names[i]);
        snprintf(copy, sizeof(copy), "gcode/%s", names[i]); age(copy, t0 + i * 100);
    }
    expect(imports() == 3 && !exists(names[0]) && exists(names[1]) && exists(names[2]) && exists(names[3]),
           "after the fourth USB start the oldest copy is removed and the newest three stay");

    /* --- The rules of the cleanup itself. --- */
    write_file("gcode/" GCODE_USB_IMPORT_PREFIX "aaaaaaaa_old1.gcode", "x");
    write_file("gcode/" GCODE_USB_IMPORT_PREFIX "aaaaaaaa_old2.gcode", "x");
    write_file("gcode/" GCODE_USB_IMPORT_PREFIX "aaaaaaaa_old3.gcode", "x");
    write_file("gcode/" GCODE_USB_IMPORT_PREFIX "aaaaaaaa_part.gcode.part", "x");
    write_file("gcode/" GCODE_USB_IMPORT_PREFIX "notes.txt", "x");
    write_file("gcode/my_own.gcode", "x");
    age("gcode/" GCODE_USB_IMPORT_PREFIX "aaaaaaaa_old1.gcode", t0 - 300);
    age("gcode/" GCODE_USB_IMPORT_PREFIX "aaaaaaaa_old2.gcode", t0 - 200);
    age("gcode/" GCODE_USB_IMPORT_PREFIX "aaaaaaaa_old3.gcode", t0 - 100);
    age("gcode/my_own.gcode", t0 - 1000);
    assert(!symlink("my_own.gcode", "gcode/" GCODE_USB_IMPORT_PREFIX "aaaaaaaa_link.gcode"));
    write_file("gcode/" GCODE_USB_IMPORT_PREFIX "aaaaaaaa_hard.gcode", "x");
    age("gcode/" GCODE_USB_IMPORT_PREFIX "aaaaaaaa_hard.gcode", t0 - 400);
    assert(!link("gcode/" GCODE_USB_IMPORT_PREFIX "aaaaaaaa_hard.gcode", "gcode/hard_twin.gcode"));

    /* While the printer state is unclear, nothing is removed. */
    mqtt.last_message = time(NULL) - 60;
    expect(gcode_usb_prune(&mqtt, names[3]) == 0 && exists(GCODE_USB_IMPORT_PREFIX "aaaaaaaa_old1.gcode"),
           "stale printer status: nothing is removed");
    mqtt.last_message = time(NULL); strcpy(mqtt.print_state, "paused");
    expect(gcode_usb_prune(&mqtt, names[3]) == 0 && exists(GCODE_USB_IMPORT_PREFIX "aaaaaaaa_old1.gcode"),
           "a paused print without native telemetry: nothing is removed");
    mqtt.print_state[0] = 0;

    /* Queued jobs keep the copies they name. */
    memset(&queue, 0, sizeof(queue));
    queue.count = 3;
    snprintf(queue.items[0].storage, sizeof(queue.items[0].storage), "internal");
    snprintf(queue.items[0].path, sizeof(queue.items[0].path), GCODE_USB_IMPORT_PREFIX "aaaaaaaa_old1.gcode");
    snprintf(queue.items[1].storage, sizeof(queue.items[1].storage), "usb");
    snprintf(queue.items[1].path, sizeof(queue.items[1].path), "part1.gcode");
    snprintf(queue.items[1].print_name, sizeof(queue.items[1].print_name), "%s", names[1]);
    snprintf(queue.items[2].storage, sizeof(queue.items[2].storage), "usb");
    snprintf(queue.items[2].path, sizeof(queue.items[2].path), "part2.gcode");
    /* Protected only by the queue: older than the three kept by count. */
    char older[PATH_MAX_LOCAL + 16];
    snprintf(older, sizeof(older), "gcode/%s", names[1]); age(older, t0 - 600);
    snprintf(older, sizeof(older), "gcode/%s", names[2]); age(older, t0 - 700);
    write_file("gcode/" GCODE_USB_IMPORT_PREFIX "aaaaaaaa_old0.gcode", "x");
    age("gcode/" GCODE_USB_IMPORT_PREFIX "aaaaaaaa_old0.gcode", t0 - 650);
    /* The copy just started is kept even when it is the oldest. */
    char keep[PATH_MAX_LOCAL + 16]; snprintf(keep, sizeof(keep), "gcode/%s", names[3]); age(keep, t0 - 5000);
    int removed = gcode_usb_prune(&mqtt, names[3]);
    expect(exists(names[3]), "the copy just started stays although it is the oldest");
    expect(exists(GCODE_USB_IMPORT_PREFIX "aaaaaaaa_old1.gcode"), "an internal queue job's own file stays");
    expect(exists(names[1]), "the copy a queued start was given stays");
    expect(exists(names[2]), "the copy a queued USB job imports to stays");
    expect(exists(GCODE_USB_IMPORT_PREFIX "aaaaaaaa_part.gcode.part") && exists(GCODE_USB_IMPORT_PREFIX "notes.txt") &&
           exists("my_own.gcode") && exists("hard_twin.gcode"), "partial copies, other names and other files are not touched");
    expect(exists(GCODE_USB_IMPORT_PREFIX "aaaaaaaa_link.gcode") && exists(GCODE_USB_IMPORT_PREFIX "aaaaaaaa_hard.gcode"),
           "a symbolic link and a hard-linked copy are not removed");
    /* Newest first: old3, old2, old1 (queued), hard (two links), names[1] (queued), old0, names[2] (queued),
     * names[3] (just started). Kept by count: names[3], old3, old2. Only old0 is free to go. */
    expect(exists(GCODE_USB_IMPORT_PREFIX "aaaaaaaa_old3.gcode") && exists(GCODE_USB_IMPORT_PREFIX "aaaaaaaa_old2.gcode"),
           "the two newest other copies stay with the one just started");
    expect(removed == 1 && !exists(GCODE_USB_IMPORT_PREFIX "aaaaaaaa_old0.gcode"),
           "an older unprotected copy beyond three is removed");

    /* A running print: the delete guard blocks the cleanup. */
    write_file("gcode/" GCODE_USB_IMPORT_PREFIX "aaaaaaaa_run.gcode", "x");
    write_file("gcode/" GCODE_USB_IMPORT_PREFIX "aaaaaaaa_spare.gcode", "x");
    age("gcode/" GCODE_USB_IMPORT_PREFIX "aaaaaaaa_run.gcode", t0 - 900);
    age("gcode/" GCODE_USB_IMPORT_PREFIX "aaaaaaaa_spare.gcode", t0 - 800);
    memset(&queue, 0, sizeof(queue));
    expect(gcode_delete_guard(&mqtt, GCODE_USB_IMPORT_PREFIX "aaaaaaaa_run.gcode") == NULL,
           "an idle printer with fresh status lets the delete guard pass");
    mqtt.machine_status = 2; strcpy(mqtt.print_state, "printing");
    snprintf(mqtt.filename, sizeof(mqtt.filename), "local/" GCODE_USB_IMPORT_PREFIX "aaaaaaaa_run.gcode");
    expect(gcode_delete_guard(&mqtt, GCODE_USB_IMPORT_PREFIX "aaaaaaaa_run.gcode") != NULL,
           "a running print blocks the delete guard");
    expect(gcode_usb_prune(&mqtt, NULL) == 0 && exists(GCODE_USB_IMPORT_PREFIX "aaaaaaaa_run.gcode") &&
           exists(GCODE_USB_IMPORT_PREFIX "aaaaaaaa_spare.gcode"), "during a print nothing is removed, the printed copy least of all");

    /* --- The size limit for all copies together (1 GiB). Sparse files stand in for large ones. --- */
    mqtt.machine_status = 1; mqtt.print_state[0] = 0; mqtt.filename[0] = 0; mqtt.last_message = time(NULL);
    memset(&queue, 0, sizeof(queue));
    assert(!mkdir("big", 0700)); gcode_internal_root = "big";
    const unsigned long long mib = 1024ULL * 1024;
    static const char *const big[] = {"big/" GCODE_USB_IMPORT_PREFIX "bbbbbbbb_one.gcode", "big/" GCODE_USB_IMPORT_PREFIX "bbbbbbbb_two.gcode",
                                      "big/" GCODE_USB_IMPORT_PREFIX "bbbbbbbb_new.gcode"};
    for (int i = 0; i < 3; i++) { write_file(big[i], "x"); assert(!truncate(big[i], (off_t)(500 * mib))); age(big[i], t0 + i * 100); }
    expect(gcode_usb_imports_max_bytes == 1024 * mib, "the limit starts at 1 GiB");
    expect(gcode_usb_prune(&mqtt, GCODE_USB_IMPORT_PREFIX "bbbbbbbb_new.gcode") == 1, "three copies of 500 MiB: one is removed");
    struct stat info;
    expect(lstat(big[0], &info) != 0 && !lstat(big[1], &info) && !lstat(big[2], &info),
           "the oldest goes; 2 x 500 MiB stay below 1 GiB");
    /* The copy just started is larger than the limit on its own: it stays with the protected copies only. */
    assert(!unlink(big[1]));
    write_file(big[0], "x"); assert(!truncate(big[0], (off_t)(10 * mib))); age(big[0], t0 - 500);
    write_file(big[1], "x"); assert(!truncate(big[1], (off_t)(10 * mib))); age(big[1], t0 - 400);
    assert(!truncate(big[2], (off_t)(1200 * mib)));
    queue.count = 1; snprintf(queue.items[0].storage, sizeof(queue.items[0].storage), "internal");
    snprintf(queue.items[0].path, sizeof(queue.items[0].path), GCODE_USB_IMPORT_PREFIX "bbbbbbbb_two.gcode");
    expect(gcode_usb_prune(&mqtt, GCODE_USB_IMPORT_PREFIX "bbbbbbbb_new.gcode") == 1 && lstat(big[0], &info) != 0 &&
           !lstat(big[1], &info) && !lstat(big[2], &info),
           "a new copy above the limit stays; the unprotected older one goes, the queued one stays");
    memset(&queue, 0, sizeof(queue));
    /* Below the limit and within three copies nothing is removed. */
    assert(!truncate(big[2], (off_t)(100 * mib)));
    expect(gcode_usb_prune(&mqtt, GCODE_USB_IMPORT_PREFIX "bbbbbbbb_new.gcode") == 0 && !lstat(big[1], &info),
           "two copies below 1 GiB are left alone");

    if (failures) { printf("%d FAIL\n", failures); return 1; }
    puts("PASS USB copy cleanup: newest three kept, printed, queued and unclear cases never removed");
    return 0;
}
