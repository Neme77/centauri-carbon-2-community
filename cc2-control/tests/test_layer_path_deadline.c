/* #342 review: force the layer-path scan deadline and check that the request reports the
 * limit, the scan worker is released for the next request, and the main loop keeps answering
 * while the scan runs. The budget is lowered so the deadline, not the byte cap, ends the scan. */
#define LAYER_PATH_TIME_MS 50
#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

static double now_ms(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1e6;
}

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

    /* ~100 MB numbered by ;LAYER:N only and without a layer count, so a request past the
     * end has to read the whole file: well under the byte cap, far beyond 50 ms. */
    snprintf(path, sizeof(path), "%s/big.gcode", dir);
    FILE *f = fopen(path, "w"); assert(f);
    static char block[1 << 16]; size_t used = 0; int layer = 0;
    for (long line = 0; line < 4000000; line++) {
        if (line % 2000 == 0) used += (size_t)sprintf(block + used, ";LAYER:%d\nG1 Z%.2f\n", layer, 0.2 * (layer + 1)), layer++;
        used += (size_t)sprintf(block + used, "G1 X%d.0 Y%d.0 E0.01\n", 20 + (int)(line % 200), 20 + (int)(line / 7 % 200));
        if (used > sizeof(block) - 128) { assert(fwrite(block, 1, used, f) == used); used = 0; }
    }
    assert(fwrite(block, 1, used, f) == used && !fclose(f));
    struct stat st; assert(!stat(path, &st) && (size_t)st.st_size < LAYER_PATH_BYTES_MAX);

    snprintf(path, sizeof(path), "%s/small.gcode", dir);
    f = fopen(path, "w"); assert(f);
    assert(fputs(";LAYER:0\nG1 Z0.2\nG1 X10 Y10\nG1 X100 Y10 E1\nG1 X100 Y100 E2\n", f) >= 0 && !fclose(f));

    char reply[LAYER_PATH_JSON_MAX + 512];
    int code;
    double begun = now_ms();
    int peer = start("storage=internal&file=big.gcode&layer=999999", &code, reply, sizeof(reply));
    assert(peer >= 0 && now_ms() - begun < 50);   /* handed to the worker, the main loop is free */

    /* While the scan runs the main loop answers at once: a second scan is told to retry. */
    begun = now_ms();
    assert(start("storage=internal&file=small.gcode&layer=1", &code, reply, sizeof(reply)) < 0 && code == 503);
    assert(now_ms() - begun < 50 && strstr(reply, "Layer path busy"));

    code = finish(peer, reply, sizeof(reply));
    assert(code == 413 && strstr(reply, "Layer path scan limit reached"));
    assert(now_ms() - begun < 5000);

    /* The worker is released: the next request gets a worker and a layer. */
    for (int i = 0; i < 200 && atomic_load(&active_layer_paths); i++) {
        struct timespec pause = {0, 5000000}; nanosleep(&pause, NULL);
    }
    assert(atomic_load(&active_layer_paths) == 0);
    peer = start("storage=internal&file=small.gcode&layer=1", &code, reply, sizeof(reply));
    assert(peer >= 0);
    assert(finish(peer, reply, sizeof(reply)) == 200 && strstr(reply, "\"layer\":1"));

    unlink(path);
    snprintf(path, sizeof(path), "%s/big.gcode", dir); unlink(path);
    rmdir(dir);
    puts("PASS layer path deadline: limit reported, worker released, main loop answers during the scan");
    return 0;
}
