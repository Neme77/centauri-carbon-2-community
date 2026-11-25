#define main cc2_main_original
#define mqtt_start_print mock_mqtt_start_print
#include "../src/main.c"
#undef main
#undef mqtt_start_print
#include <assert.h>

/* Review of the USB start (PR #390):
 * 1. A file of 2 GiB or more (printer_file_limit, lowered here) starts only with the confirmation line "large_ok", from
 *    the print dialog and from the queue; without it the start is refused before anything is copied.
 * 2. After such a confirmed start CC2 Control watches whether the printer begins it, reports the outcome and removes
 *    the internal copy of a start that failed.
 * 3. A USB read that stopped answering: the status says whether it still hangs, whether the stick was removed, and,
 *    once the read is back, whether it came back with an error or only late. */
static int failures;
static void expect(int ok, const char *what) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}
static int printed;
int mock_mqtt_start_print(mqtt_client *c, const char *media, const char *filename, const int *tools,
                          const int *trays, size_t count, char layout, int force, int timelapse) {
    (void)c; (void)media; (void)filename; (void)tools; (void)trays; (void)count; (void)layout; (void)force; (void)timelapse;
    printed++;
    return 0;
}
static int fake_uds(const char *q, char **r, size_t *l) {
    (void)q; static const char ok[] = "{\"id\":1001,\"result\":{}}"; *r = strdup(ok); *l = strlen(ok); return 0;
}
static void write_size(const char *path, size_t size) {
    FILE *file = fopen(path, "wb"); assert(file);
    char line[64]; memset(line, ';', sizeof(line)); line[63] = '\n';
    for (size_t done = 0; done < size; done += sizeof(line)) assert(fwrite(line, 1, sizeof(line), file) == sizeof(line));
    assert(!fclose(file));
}
static void write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "w"); assert(file); fputs(text, file); assert(!fclose(file));
}
static int exists(const char *path) { struct stat info; return lstat(path, &info) == 0; }
static int usb_copies(void) {
    DIR *dir = opendir("gcode"); assert(dir); struct dirent *entry; int count = 0;
    while ((entry = readdir(dir))) if (!strncmp(entry->d_name, GCODE_USB_IMPORT_PREFIX, strlen(GCODE_USB_IMPORT_PREFIX))) count++;
    closedir(dir);
    return count;
}
static char reply[2048];
static int answer(int peer) {
    size_t used = 0; ssize_t n;
    { struct timeval rcv_limit = {30, 0}; setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &rcv_limit, sizeof(rcv_limit)); }
    while (used + 1 < sizeof(reply) && (n = recv(peer, reply + used, sizeof(reply) - 1 - used, 0)) > 0) used += (size_t)n;
    reply[used] = 0; close(peer);
    return used ? atoi(strchr(reply, ' ') + 1) : -1;
}
static void serve(mqtt_client *mqtt) {
    for (int i = 0; i < 20000 && (!atomic_load(&usb_start_done) || usb_start_active()); ++i) {
        mqtt->last_message = time(NULL);
        usb_start_tick(mqtt);
        if (usb_start_active()) nanosleep(&(struct timespec){0, 1000000}, NULL);
    }
    usb_start_tick(mqtt);
}
/* One print dialog start through the worker; returns the HTTP status. */
static int start(mqtt_client *mqtt, const char *body) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    if (usb_start_begin(pair[0], mqtt, body, strlen(body))) serve(mqtt);
    else close(pair[0]); /* answered at once: the caller closes it, as the HTTP loop does */
    return answer(pair[1]);
}
static void sleep_ms(long ms) { nanosleep(&(struct timespec){ms / 1000, (ms % 1000) * 1000000L}, NULL); }
static char hang_mode; /* 's': only slow, 'e': the read comes back with an error */
static long hang_ms = 1500;
static void stick_hangs(void) {
    sleep_ms(hang_ms);
    if (hang_mode == 'e') atomic_store(&usb_start_read_failed, 1); /* stands for fread() returning EIO */
}
static void test_too_long(int signal_number) {
    (void)signal_number;
    static const char text[] = "FAIL: the test ran longer than 300 s (an answer that never came?)\n";
    if (write(2, text, sizeof(text) - 1) < 0) {}
    _exit(1);
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 0); /* each result at once, also into a file */
    signal(SIGALRM, test_too_long); alarm(300);
    char dir[] = "usb-large-stall-XXXXXX"; /* the harnesses share one working directory */
    assert(mkdtemp(dir) && !chdir(dir));
    assert(!mkdir("gcode", 0700) && !mkdir("usb", 0700));
    write_text("autosave.cfg", "[bed_mesh default]\nx_count = 11\ny_count = 11\npoints =\n  0, 0\n");
    gcode_internal_root = "gcode"; gcode_usb_root = "usb"; printer_autosave_path = "autosave.cfg";
    canvas_color_table_uds = fake_uds;
    mqtt_client mqtt = {0};
    mqtt.connected = mqtt.registered = 1; mqtt.have_machine_status = 1; mqtt.machine_status = 1;
    mqtt.last_message = time(NULL);
    printer_file_limit = 4096; /* stands for 2 GiB */
    write_size("usb/big.gcode", 8192);
    write_size("usb/small.gcode", 1024);
    write_size("gcode/inside.gcode", 8192);
    char body[256];

    /* 1. Without the confirmation: refused before the copy, from USB and from internal memory. */
    printed = 0;
    int status = start(&mqtt, "usb\nbig.gcode\n\nA\nsaved\n0");
    expect(status == 409 && strstr(reply, "\"reason\":\"file_too_large_for_printer\"") && !usb_copies() && !printed,
           "a USB file of 2 GiB or more without large_ok is refused with its reason, nothing copied or printed");
    status = start(&mqtt, "internal\ninside.gcode\n\nA\nsaved\n0");
    expect(status == 409 && strstr(reply, "\"reason\":\"file_too_large_for_printer\"") && !printed,
           "an internal file of 2 GiB or more without large_ok is refused as well");
    status = start(&mqtt, "usb\nbig.gcode\n\nA\nsaved\n0\n\n\nyes");
    expect(status == 400 && !usb_copies(), "a ninth line other than large_ok is an invalid request");
    unsigned long before = print_watch.sequence;
    status = start(&mqtt, "usb\nsmall.gcode\n\nA\nsaved\n0");
    expect(status == 202 && !strstr(reply, "\"watch\"") && print_watch.sequence == before,
           "a smaller file starts as before, without a watch");

    /* 2. Confirmed: started and watched; the printer refuses the start (1020, code 1021): failed, the copy removed. */
    printed = 0;
    status = start(&mqtt, "usb\nbig.gcode\n\nA\nsaved\n0\n\n\nlarge_ok");
    char copy[PATH_MAX_LOCAL * 2];
    snprintf(copy, sizeof(copy), "gcode/%s", print_watch.file);
    expect(status == 202 && strstr(reply, "\"watch\":") && printed == 1 && print_watch.state == PRINT_WATCH_WATCHING &&
           print_watch.imported && exists(copy), "with large_ok the file is copied, started and watched");
    print_watch_tick(&mqtt);
    expect(print_watch.state == PRINT_WATCH_WATCHING, "right after the start nothing is decided yet");
    mqtt.reply_errors++; mqtt.reply_error_method = 1020; mqtt.reply_error_code = 1021; mqtt.last_message = time(NULL);
    print_watch_tick(&mqtt);
    expect(print_watch.state == PRINT_WATCH_FAILED && print_watch.code == 1021 && !exists(copy),
           "a refusal of the start request fails the start and removes its internal copy");

    /* 2b. The printer begins the job (a new job uuid): started, the copy stays. */
    snprintf(mqtt.uuid, sizeof(mqtt.uuid), "old-job");
    status = start(&mqtt, "usb\nbig.gcode\n\nA\nsaved\n0\n\n\nlarge_ok");
    snprintf(copy, sizeof(copy), "gcode/%s", print_watch.file);
    snprintf(mqtt.uuid, sizeof(mqtt.uuid), "new-job");
    print_watch_tick(&mqtt);
    expect(status == 202 && print_watch.state == PRINT_WATCH_STARTED && exists(copy),
           "a new job uuid counts as begun and the copy stays");

    /* 2c. Nothing happens: after the watch time on an idle printer the start failed (copy removed). */
    print_watch_ms = 50;
    status = start(&mqtt, "usb\nbig.gcode\n\nA\nsaved\n0\n\n\nlarge_ok");
    snprintf(copy, sizeof(copy), "gcode/%s", print_watch.file);
    sleep_ms(100); mqtt.last_message = time(NULL);
    print_watch_tick(&mqtt);
    expect(status == 202 && print_watch.state == PRINT_WATCH_FAILED && print_watch.code == 0 && !exists(copy),
           "an idle and silent printer after the watch time fails the start");

    /* 2d. A busy printer (a calibrated start levels first) is watched on; idle again without a job: failed. */
    status = start(&mqtt, "internal\ninside.gcode\n\nA\nsaved\n0\n\n\nlarge_ok");
    expect(status == 202 && print_watch.state == PRINT_WATCH_WATCHING && !print_watch.imported,
           "a confirmed internal file is watched too");
    mqtt.machine_status = 5; sleep_ms(100);
    print_watch_tick(&mqtt);
    expect(print_watch.state == PRINT_WATCH_WATCHING, "while the printer is busy past the watch time, the watch goes on");
    mqtt.machine_status = 1; mqtt.last_message = time(NULL);
    print_watch_tick(&mqtt);
    expect(print_watch.state == PRINT_WATCH_FAILED && exists("gcode/inside.gcode"),
           "idle again without a job: failed, and an internal file of the operator is never removed");
    {
        int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
        printer_response(pair[0], &mqtt); close(pair[0]);
        answer(pair[1]);
        expect(strstr(reply, "\"print_watch\":{\"sequence\":") && strstr(reply, "\"state\":\"failed\"") &&
               strstr(reply, "\"usb_stall\":null"), "the status reports the watch, and no stall yet");
    }

    /* 3. The queue: a large file joins only with the confirmation, keeps it, and a failed start marks the copy. */
    queue_path = "queue.json"; queue_load();
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    snprintf(body, sizeof(body), "usb\nbig.gcode\n\nA\nsaved\n0\n\n\n1\nend\n\n");
    queue_add_response(pair[0], &mqtt, body, strlen(body)); close(pair[0]);
    status = answer(pair[1]);
    expect(status == 409 && strstr(reply, "\"reason\":\"file_too_large_for_printer\"") && queue.count == 0,
           "the queue refuses a large file without the confirmation");
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    snprintf(body, sizeof(body), "usb\nbig.gcode\n\nA\nsaved\n0\n\n\n1\nend\n\n\nlarge_ok");
    queue_add_response(pair[0], &mqtt, body, strlen(body)); close(pair[0]);
    status = answer(pair[1]);
    queue_load();
    expect(status == 201 && queue.count == 1 && queue.items[0].large_ok, "with it the job is added and keeps it on disk");
    int id = queue.items[0].id;
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    snprintf(body, sizeof(body), "%d", id);
    if (queue_start_response(pair[0], &mqtt, body, strlen(body))) serve(&mqtt);
    else close(pair[0]);
    status = answer(pair[1]);
    queue_item *item = queue_find(id);
    expect(status == 202 && print_watch.state == PRINT_WATCH_WATCHING && print_watch.queue_id == id && item &&
           !strcmp(item->state, "starting"), "its start sends the confirmation and is watched for the queue");
    mqtt.reply_errors++; mqtt.reply_error_method = 1020; mqtt.reply_error_code = 1021; mqtt.last_message = time(NULL);
    print_watch_tick(&mqtt);
    item = queue_find(id);
    expect(print_watch.state == PRINT_WATCH_FAILED && item && !strcmp(item->state, "pending") &&
           !strcmp(item->last_result, "not_opened") && !queue.active_id,
           "a failed start sets the queued copy back to waiting, marked not_opened");
    queue.count = 0; queue.active_id = 0; queue_save();
    print_watch_ms = PRINT_WATCH_MS;

    /* 4. The mount table: read without touching the stick, with the kernel's octal escapes. */
    write_text("mounts", "/dev/sda1 /mnt/ex\\040UDISK vfat rw 0 0\nproc /proc proc rw 0 0\n");
    usb_mounts_path = "mounts";
    gcode_usb_root = "/mnt/ex UDISK";
    expect(usb_root_mounted() == 1, "a mounted stick is found in the mount table");
    gcode_usb_root = "/mnt/other";
    expect(usb_root_mounted() == 0, "a stick that is not mounted is not found");
    usb_mounts_path = "missing";
    expect(usb_root_mounted() == -1, "an unreadable table says so");
    gcode_usb_root = "usb";

    /* 5. A read that stops answering, only late: hanging, stick removed, power cycle advice, then back as "slow". */
    char cwd[PATH_MAX_LOCAL], line[PATH_MAX_LOCAL + 64], usb_abs[PATH_MAX_LOCAL + 8];
    assert(getcwd(cwd, sizeof(cwd)));
    snprintf(usb_abs, sizeof(usb_abs), "%s/usb", cwd);
    snprintf(line, sizeof(line), "/dev/sda1 %s vfat rw 0 0\n", usb_abs);
    write_text("mounts", line); usb_mounts_path = "mounts"; gcode_usb_root = usb_abs;
    usb_start_stall_ms = 200; usb_start_copy_hook = stick_hangs; hang_mode = 's'; hang_ms = 2600;
    usb_stall_removed_ms = 300;
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    assert(usb_start_begin(pair[0], &mqtt, "usb\nsmall.gcode\n\nA\nsaved\n0", 26));
    char got[1024] = ""; ssize_t n = -1;
    for (int i = 0; i < 1000 && n <= 0; ++i) {
        mqtt.last_message = time(NULL); usb_start_tick(&mqtt);
        n = recv(pair[1], got, sizeof(got) - 1, MSG_DONTWAIT);
        if (n <= 0) sleep_ms(1);
    }
    if (n > 0) got[n] = 0;
    char stall[160]; usb_stall_json(stall, sizeof(stall));
    expect(n > 0 && strstr(got, "\"reason\":\"usb_unresponsive\"") && strstr(got, "\"stall\":") &&
           strstr(stall, "\"hanging\":true") && strstr(stall, "\"outcome\":null") && strstr(stall, "\"removed\":false"),
           "a hanging read is answered and reported as hanging, the stick still in");
    write_text("mounts", "proc /proc proc rw 0 0\n"); /* the operator pulls the stick */
    for (int i = 0; i < 1500 && !usb_stall.removed_ms; ++i) { mqtt.last_message = time(NULL); usb_start_tick(&mqtt); sleep_ms(1); }
    sleep_ms(350); mqtt.last_message = time(NULL); usb_start_tick(&mqtt);
    usb_stall_json(stall, sizeof(stall));
    expect(strstr(stall, "\"hanging\":true") && strstr(stall, "\"removed\":true") && strstr(stall, "\"power_cycle\":true"),
           "still hanging after the stick was removed for longer than the limit: power cycle advised");
    serve(&mqtt); close(pair[1]);
    usb_stall_json(stall, sizeof(stall));
    expect(!usb_start_active() && strstr(stall, "\"hanging\":false") && strstr(stall, "\"outcome\":\"slow\"") &&
           strstr(stall, "\"power_cycle\":false"), "the read came back without an error: free again, outcome slow");

    /* 5b. The same with a read that comes back with an error. */
    write_text("mounts", line); hang_mode = 'e'; hang_ms = 600;
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    assert(usb_start_begin(pair[0], &mqtt, "usb\nsmall.gcode\n\nA\nsaved\n0", 26));
    serve(&mqtt); answer(pair[1]);
    usb_stall_json(stall, sizeof(stall));
    expect(strstr(stall, "\"sequence\":2") && strstr(stall, "\"hanging\":false") && strstr(stall, "\"outcome\":\"read_error\""),
           "a read that came back with an error is reported as read_error");
    usb_start_copy_hook = NULL; usb_start_stall_ms = USB_START_STALL_MS; usb_stall_removed_ms = USB_STALL_REMOVED_MS;
    status = start(&mqtt, "usb\nsmall.gcode\n\nA\nsaved\n0");
    expect(status == 202 && !atomic_load(&usb_start_read_failed), "the next start begins without the earlier read error");

    /* 6. A scan on the start worker notes a read error of its file. */
    FILE *broken = fopen("broken.gcode", "w"); assert(broken);
    (void)fgetc(broken); /* reading a stream opened for writing sets its error flag */
    atomic_int no_cancel = 0;
    gcode_scan_cancel = &no_cancel; atomic_store(&usb_start_read_failed, 0);
    gcode_scan_read_error(broken);
    expect(atomic_load(&usb_start_read_failed) == 1, "a scan on the start worker notes a read error");
    gcode_scan_cancel = NULL; atomic_store(&usb_start_read_failed, 0);
    gcode_scan_read_error(broken);
    expect(atomic_load(&usb_start_read_failed) == 0, "a scan elsewhere (the dialog's analysis) does not");
    fclose(broken);

    if (failures) { printf("%d FAIL\n", failures); return 1; }
    puts("PASS USB start review: large files only confirmed, watched starts, hanging reads reported");
    return 0;
}
