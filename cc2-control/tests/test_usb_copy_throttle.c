#define main cc2_main_original
#define mqtt_start_print mock_mqtt_start_print
#include "../src/main.c"
#undef main
#undef mqtt_start_print
#include <assert.h>

/* A USB start copies on its worker at low priority (nice 10, best-effort I/O level 7), pauses after each
 * full block and writes its data out every 8 MiB, dropping those pages of source and copy; the main
 * loop keeps its priority, and the synchronous queue import is not slowed down. */
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
#define BLOCKS 8
static char content[BLOCKS * 65536 + 777];
static int worker_nice = -100, worker_ioprio = -1;
static void sample_worker(void) {
#if defined(__linux__) && defined(SYS_gettid)
    pid_t tid = (pid_t)syscall(SYS_gettid);
    errno = 0; worker_nice = getpriority(PRIO_PROCESS, (id_t)tid);
#ifdef SYS_ioprio_get
    worker_ioprio = (int)syscall(SYS_ioprio_get, 1, (int)tid);
#endif
#endif
}
static int answer(int peer) {
    char response[1024]; size_t used = 0; ssize_t n;
    while (used + 1 < sizeof(response) && (n = recv(peer, response + used, sizeof(response) - 1 - used, 0)) > 0) used += (size_t)n;
    response[used] = 0; close(peer);
    return used ? atoi(strchr(response, ' ') + 1) : -1;
}
/* Cancels the running start once its first block is written, while the worker pauses after it. */
static atomic_size_t first_copied;
static struct timespec cancel_time;
static atomic_int blocks_after_cancel;
static void note_block(size_t copied) {
    size_t none = 0; atomic_compare_exchange_strong(&first_copied, &none, copied);
    if (usb_start_cancelled) atomic_fetch_add(&blocks_after_cancel, 1);
}
static void *cancel_in_pause(void *unused) {
    (void)unused;
    while (!atomic_load(&first_copied)) nanosleep(&(struct timespec){0, 100000}, NULL);
    nanosleep(&(struct timespec){0, 20000000}, NULL); /* well inside the 2 s pause */
    clock_gettime(CLOCK_MONOTONIC, &cancel_time);
    usb_start_cancelled = 2;
    return NULL;
}
static int copies(void) {
    DIR *dir = opendir("gcode"); assert(dir);
    int count = 0; struct dirent *entry;
    while ((entry = readdir(dir))) if (!strncmp(entry->d_name, GCODE_USB_IMPORT_PREFIX, 8)) count++;
    closedir(dir); return count;
}

int main(void) {
    char dir[] = "usb-throttle-XXXXXX"; /* the harnesses share one working directory */
    assert(mkdtemp(dir) && !chdir(dir));
    assert(!mkdir("gcode", 0700) && !mkdir("usb", 0700));
    for (size_t i = 0; i < sizeof(content); ++i) content[i] = (i % 61 == 60) ? '\n' : (char)('A' + i % 26);
    FILE *file = fopen("usb/part.gcode", "wb"); assert(file);
    assert(fwrite(content, 1, sizeof(content), file) == sizeof(content)); assert(!fclose(file));
    file = fopen("autosave.cfg", "w"); assert(file);
    fputs("[bed_mesh default]\nx_count = 11\ny_count = 11\npoints =\n  0, 0\n", file); fclose(file);
    gcode_internal_root = "gcode"; gcode_usb_root = "usb"; printer_autosave_path = "autosave.cfg";
    canvas_color_table_uds = fake_uds;
    mqtt_client mqtt = {0};
    mqtt.connected = mqtt.registered = 1; mqtt.have_machine_status = 1; mqtt.machine_status = 1;
    static const char body[] = "usb\npart.gcode\n\nA\nsaved\n0";
    errno = 0; int main_nice = getpriority(PRIO_PROCESS, 0);

    /* The dialog start: worker at nice 10 and best-effort I/O level 7, one pause after each full block. */
    usb_start_copy_hook = sample_worker;
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    assert(usb_start_begin(pair[0], &mqtt, body, strlen(body)));
    for (int i = 0; i < 5000 && !atomic_load(&usb_start_done); ++i) nanosleep(&(struct timespec){0, 1000000}, NULL);
    usb_start_tick(&mqtt);
    expect(answer(pair[1]) == 202 && copies() == 1, "the throttled copy is still started");
    usb_start_copy_hook = NULL;
    expect(worker_nice == USB_COPY_NICE, "the copy runs at nice 10 on its worker thread");
    if (worker_ioprio < 0) puts("SKIP I/O class: ioprio_get unavailable here");
    else expect(worker_ioprio == (2 << 13 | 7),
                "the copy runs in the best-effort I/O class at its lowest level 7, not idle (which can starve)");
    expect(usb_copy_pauses == BLOCKS, "one pause after each full block that more data follows");
    errno = 0;
    expect(getpriority(PRIO_PROCESS, 0) == main_nice, "the main loop keeps its priority");

    /* A file of exactly whole blocks: the last full block is followed by the end, still one pause each. */
    assert(!truncate("usb/part.gcode", BLOCKS * 65536));
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    assert(usb_start_begin(pair[0], &mqtt, body, strlen(body)));
    for (int i = 0; i < 5000 && !atomic_load(&usb_start_done); ++i) nanosleep(&(struct timespec){0, 1000000}, NULL);
    usb_start_tick(&mqtt);
    expect(answer(pair[1]) == 202 && usb_copy_pauses == BLOCKS, "a file of whole blocks pauses once per block");
    expect(usb_copy_flushes == 0, "a file under 8 MiB is only synced at the end");

    /* Every 8 MiB the copy is written out and those pages of source and copy are dropped. */
    for (int round = 0; round < 2; ++round) {
        size_t size = round ? 2 * USB_COPY_FLUSH_BYTES : 2 * USB_COPY_FLUSH_BYTES + 777;
        file = fopen("usb/part.gcode", "wb"); assert(file);
        for (size_t done = 0; done < size;) {
            size_t n = size - done < sizeof(content) ? size - done : sizeof(content);
            assert(fwrite(content, 1, n, file) == n); done += n;
        }
        assert(!fclose(file));
        assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
        assert(usb_start_begin(pair[0], &mqtt, body, strlen(body)));
        for (int i = 0; i < 20000 && !atomic_load(&usb_start_done); ++i) nanosleep(&(struct timespec){0, 1000000}, NULL);
        usb_start_tick(&mqtt);
        int status = answer(pair[1]);
        expect(status == 202 && usb_copy_flushes == 2,
               round ? "a file of exactly 16 MiB is written out twice, the second time at its end"
                     : "16 MiB and a rest are written out twice, the rest by the final sync");
        expect(usb_copy_dropped_out == 2 * USB_COPY_FLUSH_BYTES && usb_copy_dropped_in == 2 * USB_COPY_FLUSH_BYTES,
               "each written-out range is dropped from the cache for the copy and for the source");
        /* The copy still reads back the same bytes. */
        char copy[PATH_MAX_LOCAL + 8] = "";
        DIR *dir = opendir("gcode"); assert(dir); struct dirent *entry;
        while ((entry = readdir(dir)))
            if (!strncmp(entry->d_name, GCODE_USB_IMPORT_PREFIX, 8)) snprintf(copy, sizeof(copy), "gcode/%s", entry->d_name);
        closedir(dir);
        FILE *a = fopen("usb/part.gcode", "rb"), *b = fopen(copy, "rb");
        assert(a && b);
        int same = 1; size_t total = 0; char x[65536], y[65536]; size_t na, nb;
        while ((na = fread(x, 1, sizeof(x), a)) > 0) {
            nb = fread(y, 1, sizeof(y), b);
            if (na != nb || memcmp(x, y, na)) { same = 0; break; }
            total += na;
        }
        if (fread(y, 1, 1, b) != 0) same = 0;
        fclose(a); fclose(b);
        expect(same && total == size, "the written-out copy is byte for byte the USB file");
    }

    /* The queue's synchronous import runs on the main loop: no pause, no priority change. */
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    expect(gcode_start_response(pair[0], &mqtt, body, strlen(body), NULL) == 202, "the synchronous import is started");
    close(pair[0]); (void)answer(pair[1]);
    errno = 0;
    expect(usb_copy_pauses == 0 && usb_copy_flushes == 0 && getpriority(PRIO_PROCESS, 0) == main_nice,
           "the synchronous import neither pauses, writes out early nor lowers the main loop");

    /* An emergency stop during a pause ends the copy at once, not when the pause is over. */
    usb_copy_pause_us = 2000000;
    usb_copy_block_hook = note_block;
    pthread_t canceller; assert(!pthread_create(&canceller, NULL, cancel_in_pause, NULL));
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    assert(usb_start_begin(pair[0], &mqtt, body, strlen(body)));
    while (!atomic_load(&usb_start_done)) nanosleep(&(struct timespec){0, 100000}, NULL);
    struct timespec stopped; clock_gettime(CLOCK_MONOTONIC, &stopped);
    assert(!pthread_join(canceller, NULL));
    double after = (double)(stopped.tv_sec - cancel_time.tv_sec) + (stopped.tv_nsec - cancel_time.tv_nsec) / 1e9;
    int import_error = usb_start.import_error;
    usb_start_tick(&mqtt);
    expect(answer(pair[1]) == 409 && import_error == GCODE_IMPORT_CANCELLED && after < 0.2,
           "an emergency stop during a 2 s pause ends the copy within 0.2 s");
    expect(atomic_load(&blocks_after_cancel) == 0, "and the copy writes no further block after it");
    usb_copy_block_hook = NULL; usb_copy_pause_us = USB_COPY_PAUSE_US;

    /* The pause is a setting: 0 switches it off. */
    usb_copy_pause_us = 0;
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    assert(usb_start_begin(pair[0], &mqtt, body, strlen(body)));
    for (int i = 0; i < 5000 && !atomic_load(&usb_start_done); ++i) nanosleep(&(struct timespec){0, 1000000}, NULL);
    usb_start_tick(&mqtt);
    expect(answer(pair[1]) == 202 && usb_copy_pauses == 0, "a pause of 0 copies without pausing");

    if (failures) { printf("%d FAIL\n", failures); return 1; }
    puts("PASS USB copy throttle: worker at low priority with a pause per block and write-out every 8 MiB, main loop and queue import unchanged");
    return 0;
}
