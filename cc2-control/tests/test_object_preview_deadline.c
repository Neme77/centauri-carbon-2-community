/* The object-preview scan stops at the layer path's time budget (lowered here, the worker held past it):
 * 413, the worker slot is free again, the main loop answers while the scan runs, and the file is refused
 * at once for 300 s, for any object, unless it changes. */
#define LAYER_PATH_TIME_MS 50
#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

static int gate[2];
static atomic_int hold = 1; /* set by the test, read by the worker */
static void hold_scan(void) { if (atomic_load(&hold)) { char byte; assert(read(gate[0], &byte, 1) == 1); } }
static void sleep_ms(long ms) { struct timespec t = {ms / 1000, (ms % 1000) * 1000000L}; nanosleep(&t, NULL); }
static void idle(void) {
    for (int i = 0; i < 2000 && atomic_load(&active_layer_paths); i++) sleep_ms(5);
    assert(atomic_load(&active_layer_paths) == 0);
}
static char reply[64 * 1024];
static int start(const char *query, int *code) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    if (gcode_object_preview_start(pair[0], query)) { *code = 0; return pair[1]; }
    close(pair[0]);
    ssize_t n = read(pair[1], reply, sizeof(reply) - 1); close(pair[1]);
    assert(n > 0); reply[n] = 0;
    *code = atoi(strchr(reply, ' ') + 1);
    return -1;
}
static int finish(int peer) {
    size_t used = 0; ssize_t n;
    while (used + 1 < sizeof(reply) && (n = read(peer, reply + used, sizeof(reply) - 1 - used)) > 0) used += (size_t)n;
    close(peer); reply[used] = 0;
    assert(used > 0);
    idle();
    return atoi(strchr(reply, ' ') + 1);
}
/* A scan held past the budget, then let go: it reports the limit. */
static int held_scan(const char *query) {
    int code;
    hold = 1;
    int peer = start(query, &code);
    assert(peer >= 0);
    sleep_ms(LAYER_PATH_TIME_MS + 30);
    assert(write(gate[1], "x", 1) == 1);
    return finish(peer);
}
static unsigned long scans(void) {
    pthread_mutex_lock(&layer_path_cache_lock); unsigned long n = layer_path_stats.scans; pthread_mutex_unlock(&layer_path_cache_lock);
    return n;
}

int main(void) {
    char dir[] = "/tmp/object-deadline-XXXXXX"; assert(mkdtemp(dir));
    gcode_internal_root = dir;
    layer_path_rest_min_ms = 0; layer_path_rest_percent = 0;
    char path[512];
    snprintf(path, sizeof(path), "%s/big.gcode", dir);
    FILE *f = fopen(path, "w"); assert(f);
    assert(fputs("M83\nEXCLUDE_OBJECT_DEFINE NAME=A\nEXCLUDE_OBJECT_DEFINE NAME=B\n", f) >= 0);
    for (int layer = 0; layer < 60; layer++) {
        assert(fprintf(f, ";LAYER_CHANGE\nG1 Z%.2f\nEXCLUDE_OBJECT_START NAME=A\n;TYPE:Outer wall\n", 0.2 * (layer + 1)) > 0);
        for (int line = 0; line < 100; line++) assert(fprintf(f, "G1 X%d.0 Y%d.0 E0.01\n", 20 + line % 50, 20 + line / 50) > 0);
        assert(fputs("EXCLUDE_OBJECT_END NAME=A\n", f) >= 0);
    }
    assert(!fclose(f));
    snprintf(path, sizeof(path), "%s/small.gcode", dir);
    f = fopen(path, "w"); assert(f);
    assert(fputs("M83\nEXCLUDE_OBJECT_START NAME=S\nG1 X10 Y10\nG1 X100 Y10 E1\nEXCLUDE_OBJECT_END NAME=S\n", f) >= 0 && !fclose(f));
    assert(!pipe(gate));
    object_preview_scan_hook = hold_scan;
    int code;

    /* While the scan runs the main loop answers at once; then the budget ends the scan. */
    hold = 1;
    int peer = start("file=big.gcode&object=A", &code);
    assert(peer >= 0);
    assert(start("file=small.gcode&object=S", &code) < 0 && code == 503 && strstr(reply, "Object preview busy; retry shortly"));
    sleep_ms(LAYER_PATH_TIME_MS + 30);
    assert(write(gate[1], "x", 1) == 1);
    assert(finish(peer) == 413 && strstr(reply, "Object preview scan limit reached"));
    assert(atomic_load(&active_layer_paths) == 0);

    /* The slot is free: another file is read. */
    hold = 0;
    peer = start("file=small.gcode&object=S", &code);
    assert(peer >= 0 && finish(peer) == 200 && strstr(reply, "\"object\":\"S\""));

    /* The limit is remembered for that file, for every object: refused without reading. */
    unsigned long before = scans();
    assert(start("file=big.gcode&object=A", &code) < 0 && code == 413 && strstr(reply, "Object preview scan limit reached"));
    assert(start("file=big.gcode&object=B", &code) < 0 && code == 413);
    assert(start("file=big.gcode&object=Z", &code) < 0 && code == 413);
    assert(scans() == before && atomic_load(&active_layer_paths) == 0);
    /* Still after 299 s. */
    pthread_mutex_lock(&object_preview_lock); object_preview_limited.at.tv_sec -= LAYER_PATH_LIMIT_HOLD_S - 1; pthread_mutex_unlock(&object_preview_lock);
    assert(start("file=big.gcode&object=A", &code) < 0 && code == 413 && scans() == before);
    /* After 300 s it is read again. */
    pthread_mutex_lock(&object_preview_lock); object_preview_limited.at.tv_sec -= 1; pthread_mutex_unlock(&object_preview_lock);
    assert(held_scan("file=big.gcode&object=A") == 413 && scans() == before + 1);
    assert(start("file=big.gcode&object=A", &code) < 0 && code == 413 && scans() == before + 1);
    /* A changed file is read afresh. */
    snprintf(path, sizeof(path), "%s/big.gcode", dir);
    f = fopen(path, "a"); assert(f && fputs("G1 X1 Y1\n", f) >= 0 && !fclose(f));
    hold = 0;
    peer = start("file=big.gcode&object=A", &code);
    assert(peer >= 0 && finish(peer) == 200 && strstr(reply, "\"layers\":60,\"step\":3") && scans() == before + 2);
    /* A layer-path scan keeps its own limit: the object preview's does not refuse it. */
    peer = -1;
    {
        int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
        assert(gcode_layer_path_start(pair[0], "file=big.gcode&layer=1") == 1);
        assert(finish(pair[1]) == 200);
    }

    close(gate[0]); close(gate[1]);
    unlink(path);
    snprintf(path, sizeof(path), "%s/small.gcode", dir); unlink(path);
    rmdir(dir);
    puts("PASS object preview deadline: scan limit reported and remembered for 300 s per file, worker released, main loop answers during the scan");
    return 0;
}
