/* #342 review: force the layer-path scan deadline and check that the request reports the
 * limit, the scan worker is released for the next request, and the main loop keeps answering
 * while the scan runs. The budget is lowered and the worker is held past it, so the outcome
 * does not depend on this machine's speed. */
#define LAYER_PATH_TIME_MS 50
#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

/* Holds the worker after its scan clock started until the test releases it: a scan that is
 * slow on demand, independent of how fast this machine reads the file. */
static int gate[2];
static void hold_scan(void) { char byte; assert(read(gate[0], &byte, 1) == 1); }

/* Starts a request; returns the peer when a worker took it, else the answered status in *code. */
static int start(const char *query, int *code, char *reply, size_t cap) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    if (gcode_layer_path_start(pair[0], query)) { *code = 0; return pair[1]; }
    close(pair[0]);
    ssize_t n = read(pair[1], reply, cap - 1); close(pair[1]);
    assert(n > 0); reply[n] = 0;
    *code = atoi(strchr(reply, ' ') + 1);
    return -1;
}

static int finish(int peer, char *reply, size_t cap) {
    size_t used = 0; ssize_t n;
    while (used + 1 < cap && (n = read(peer, reply + used, cap - 1 - used)) > 0) used += (size_t)n;
    close(peer); reply[used] = 0;
    assert(used > 0);
    return atoi(strchr(reply, ' ') + 1);
}

int main(void) {
    char dir[] = "/tmp/layer-deadline-XXXXXX"; assert(mkdtemp(dir));
    gcode_internal_root = dir;
    char path[512];

    /* Numbered by ;LAYER:N only and without a layer count, so a request past the last layer
     * has to read on: the deadline, not the end of the file, ends the scan. */
    snprintf(path, sizeof(path), "%s/big.gcode", dir);
    FILE *f = fopen(path, "w"); assert(f);
    for (int layer = 0; layer < 200; layer++) {
        assert(fprintf(f, ";LAYER:%d\nG1 Z%.2f\n", layer, 0.2 * (layer + 1)) > 0);
        for (int line = 0; line < 500; line++)
            assert(fprintf(f, "G1 X%d.0 Y%d.0 E0.01\n", 20 + line % 200, 20 + layer) > 0);
    }
    assert(!fclose(f));

    snprintf(path, sizeof(path), "%s/small.gcode", dir);
    f = fopen(path, "w"); assert(f);
    assert(fputs(";LAYER:0\nG1 Z0.2\nG1 X10 Y10\nG1 X100 Y10 E1\nG1 X100 Y100 E2\n", f) >= 0 && !fclose(f));

    char reply[LAYER_PATH_JSON_MAX + 512];
    int code;
    assert(!pipe(gate));
    layer_path_scan_hook = hold_scan;
    int peer = start("storage=internal&file=big.gcode&layer=999999", &code, reply, sizeof(reply));
    assert(peer >= 0);                            /* handed to the worker, the main loop is free */

    /* While the scan runs the main loop answers at once: a second scan is told to retry. */
    assert(start("storage=internal&file=small.gcode&layer=1", &code, reply, sizeof(reply)) < 0 && code == 503);
    assert(strstr(reply, "Layer path busy") && atomic_load(&active_layer_paths) == 1);

    /* Let the budget run out, then let the scan go on: it stops at once and reports the limit. */
    struct timespec budget = {0, (LAYER_PATH_TIME_MS + 30) * 1000000L}; nanosleep(&budget, NULL);
    assert(write(gate[1], "x", 1) == 1);
    code = finish(peer, reply, sizeof(reply));
    assert(code == 413 && strstr(reply, "Layer path scan limit reached"));
    layer_path_scan_hook = NULL;

    /* The worker is released: the next request gets a worker and a layer. */
    for (int i = 0; i < 2000 && atomic_load(&active_layer_paths); i++) {
        struct timespec pause = {0, 5000000}; nanosleep(&pause, NULL);
    }
    assert(atomic_load(&active_layer_paths) == 0);
    peer = start("storage=internal&file=small.gcode&layer=1", &code, reply, sizeof(reply));
    assert(peer >= 0);
    assert(finish(peer, reply, sizeof(reply)) == 200 && strstr(reply, "\"layer\":1"));

    for (int i = 0; i < 2000 && atomic_load(&active_layer_paths); i++) {
        struct timespec pause = {0, 5000000}; nanosleep(&pause, NULL);
    }
    /* The limit is remembered for that file: the same or a later layer is refused at once,
     * without a worker, so a print's following layers do not each burn the budget. */
    assert(start("storage=internal&file=big.gcode&layer=999999", &code, reply, sizeof(reply)) < 0 && code == 413);
    assert(strstr(reply, "Layer path scan limit reached") && atomic_load(&active_layer_paths) == 0);
    assert(start("storage=internal&file=big.gcode&layer=1000000", &code, reply, sizeof(reply)) < 0 && code == 413);
    /* An earlier layer and another file are still scanned. */
    peer = start("storage=internal&file=big.gcode&layer=1", &code, reply, sizeof(reply));
    assert(peer >= 0);
    assert(finish(peer, reply, sizeof(reply)) == 200 && strstr(reply, "\"layer\":1"));
    for (int i = 0; i < 2000 && atomic_load(&active_layer_paths); i++) {
        struct timespec pause = {0, 5000000}; nanosleep(&pause, NULL);
    }
    /* A changed file is scanned afresh: the worker takes the request again. */
    snprintf(path, sizeof(path), "%s/big.gcode", dir);
    f = fopen(path, "a"); assert(f && fputs("G1 X1 Y1\n", f) >= 0 && !fclose(f));
    layer_path_scan_hook = hold_scan;
    peer = start("storage=internal&file=big.gcode&layer=999999", &code, reply, sizeof(reply));
    assert(peer >= 0);
    nanosleep(&budget, NULL);
    assert(write(gate[1], "x", 1) == 1);
    assert(finish(peer, reply, sizeof(reply)) == 413);
    layer_path_scan_hook = NULL;
    for (int i = 0; i < 2000 && atomic_load(&active_layer_paths); i++) {
        struct timespec pause = {0, 5000000}; nanosleep(&pause, NULL);
    }
    /* The memory expires: after the hold time the layer is scanned again. */
    assert(start("storage=internal&file=big.gcode&layer=999999", &code, reply, sizeof(reply)) < 0 && code == 413);
    pthread_mutex_lock(&layer_path_cache_lock);
    layer_path_limited.at.tv_sec -= LAYER_PATH_LIMIT_HOLD_S;
    pthread_mutex_unlock(&layer_path_cache_lock);
    layer_path_scan_hook = hold_scan;
    peer = start("storage=internal&file=big.gcode&layer=999999", &code, reply, sizeof(reply));
    assert(peer >= 0);
    nanosleep(&budget, NULL);
    assert(write(gate[1], "x", 1) == 1);
    assert(finish(peer, reply, sizeof(reply)) == 413);
    layer_path_scan_hook = NULL;
    for (int i = 0; i < 2000 && atomic_load(&active_layer_paths); i++) {
        struct timespec pause = {0, 5000000}; nanosleep(&pause, NULL);
    }
    snprintf(path, sizeof(path), "%s/small.gcode", dir);
    close(gate[0]); close(gate[1]);

    unlink(path);
    snprintf(path, sizeof(path), "%s/big.gcode", dir); unlink(path);
    rmdir(dir);
    puts("PASS layer path deadline: limit reported and remembered per file, worker released, main loop answers during the scan");
    return 0;
}
