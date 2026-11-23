/* Load limits of the layer-path worker (seen: a full scan right after a print start came with an
 * MCU shutdown): one scan at a time, the worker at a lower CPU priority, a pause after each scan at
 * least as long as the scan, and cached layers still answered during that pause. */
#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

static int gate[2];
static atomic_int seen_nice = -100;
static void hold_scan(void) {
    errno = 0;
    int nice_now = getpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid));
    atomic_store(&seen_nice, errno ? -100 : nice_now);
    char byte; assert(read(gate[0], &byte, 1) == 1);
}

static int start(const char *query, int *code) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    if (gcode_layer_path_start(pair[0], query)) { *code = 0; return pair[1]; }
    close(pair[0]);
    char reply[512]; ssize_t n = read(pair[1], reply, sizeof(reply) - 1); close(pair[1]);
    assert(n > 0); reply[n] = 0;
    *code = atoi(strchr(reply, ' ') + 1);
    return -1;
}

static int finish(int peer) {
    static char reply[LAYER_PATH_JSON_MAX + 512];
    size_t used = 0; ssize_t n;
    while (used + 1 < sizeof(reply) && (n = read(peer, reply + used, sizeof(reply) - 1 - used)) > 0) used += (size_t)n;
    close(peer); reply[used] = 0;
    assert(used > 0);
    return atoi(strchr(reply, ' ') + 1);
}

static void idle(void) {
    for (int i = 0; i < 2000 && atomic_load(&active_layer_paths); i++) {
        struct timespec pause = {0, 5000000}; nanosleep(&pause, NULL);
    }
    assert(atomic_load(&active_layer_paths) == 0);
}

static void sleep_ms(long ms) { struct timespec t = {ms / 1000, (ms % 1000) * 1000000L}; nanosleep(&t, NULL); }

int main(void) {
    char dir[] = "/tmp/layer-load-XXXXXX"; assert(mkdtemp(dir));
    gcode_internal_root = dir;
    char path[512];
    snprintf(path, sizeof(path), "%s/print.gcode", dir);
    FILE *f = fopen(path, "w"); assert(f);
    for (int layer = 0; layer < 20; layer++) {
        assert(fprintf(f, ";LAYER:%d\nG1 Z%.2f\n", layer, 0.2 * (layer + 1)) > 0);
        for (int line = 0; line < 50; line++) assert(fprintf(f, "G1 X%d.0 Y%d.0 E0.01\n", 20 + line, 20 + layer) > 0);
    }
    assert(!fclose(f));
    assert(!pipe(gate));
    layer_path_rest_min_ms = 250;
    layer_path_rest_percent = 100;
    errno = 0;
    int base_nice = getpriority(PRIO_PROCESS, 0);
    assert(!errno);
    int code;

    /* The pause check compares two clock readings that can lie weeks apart (the last scan against
     * now). That span must not wrap where long has 32 bits, or the service would stay busy. */
    struct timespec boot = {0, 0}, weeks = {40L * 86400L, 0};
    assert(layer_path_ms_between(&weeks, &boot) < 0 && layer_path_ms_between(&boot, &weeks) > 0);

    /* A scan held for 400 ms: the worker runs at the lower priority, and a second scan is refused. */
    layer_path_scan_hook = hold_scan;
    int peer = start("storage=internal&file=print.gcode&layer=5", &code);
    assert(peer >= 0);
    for (int i = 0; i < 400 && atomic_load(&seen_nice) == -100; i++) sleep_ms(5);
    int nice_seen = atomic_load(&seen_nice);
    assert(nice_seen >= LAYER_PATH_NICE || nice_seen == base_nice);
    if (base_nice < LAYER_PATH_NICE) assert(nice_seen == LAYER_PATH_NICE);
    errno = 0;
    assert(getpriority(PRIO_PROCESS, 0) == base_nice && !errno); /* only the worker thread is lowered */
    assert(start("storage=internal&file=print.gcode&layer=6", &code) < 0 && code == 503);
    sleep_ms(400);
    assert(write(gate[1], "x", 1) == 1);
    assert(finish(peer) == 200);
    layer_path_scan_hook = NULL;
    idle();

    /* Right after it: the next scan waits (busy), a cached layer is still answered. */
    struct timespec ended; clock_gettime(CLOCK_MONOTONIC, &ended);
    assert(start("storage=internal&file=print.gcode&layer=6", &code) < 0 && code == 503);
    assert(start("storage=internal&file=print.gcode&layer=5", &code) < 0 && code == 200);
    /* The pause lasts at least as long as the scan (about 400 ms), not just the 250 ms minimum. */
    sleep_ms(300);
    assert(start("storage=internal&file=print.gcode&layer=6", &code) < 0 && code == 503);
    /* After the pause the next scan runs. */
    struct timespec now;
    for (;;) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (layer_path_ms_between(&ended, &now) > 700) break;
        sleep_ms(20);
    }
    peer = start("storage=internal&file=print.gcode&layer=6", &code);
    assert(peer >= 0 && finish(peer) == 200);
    idle();

    /* A short scan still leaves the minimum pause. */
    clock_gettime(CLOCK_MONOTONIC, &ended);
    assert(start("storage=internal&file=print.gcode&layer=7", &code) < 0 && code == 503);
    sleep_ms(300);
    peer = start("storage=internal&file=print.gcode&layer=7", &code);
    assert(peer >= 0 && finish(peer) == 200);
    idle();

    close(gate[0]); close(gate[1]);
    unlink(path); rmdir(dir);
    puts("PASS layer path load: one scan at a time, worker at lower priority, pause after each scan as long as the scan, cached layers answered during the pause");
    return 0;
}
