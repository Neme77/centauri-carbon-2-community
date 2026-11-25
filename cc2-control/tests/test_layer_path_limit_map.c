/* The scan-limit memory (a 413 for a layer and every later one of the same file, for LAYER_PATH_LIMIT_HOLD_S) guards
 * against repeated full scans that never finish. It must not refuse what a layer map answers in milliseconds:
 *   - a limit that came from a full scan stays as it is,
 *   - a limit that came from a read through the map only refuses that one layer,
 *   - a map that becomes current for the file ends the memory (no 413 for layers the map serves),
 *   - a map that cannot serve the layer (repeated layer numbers) does not end it,
 *   - a map that serves most layers but not all lifts the refusal only for the layers it serves,
 *   - while the printer homes (read hold) no G-code is read to find out: the answer is the retry (503).
 * The budget is large for ordinary requests and lowered only for the scans that are held past it on purpose, so the
 * outcome does not depend on this machine's speed. */
static long layer_budget_ms = 20000;
#define LAYER_PATH_TIME_MS layer_budget_ms
#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

static int gate[2];
static void hold_scan(void) { char byte; assert(read(gate[0], &byte, 1) == 1); }

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

static void idle(void) {
    for (int i = 0; i < 2000 && atomic_load(&active_layer_paths); i++) {
        struct timespec pause = {0, 5000000}; nanosleep(&pause, NULL);
    }
    assert(atomic_load(&active_layer_paths) == 0);
}

/* One request; answered by the worker (waits for it) or at once. */
static int get(const char *file, int layer, char *reply, size_t cap) {
    char query[256]; int code;
    snprintf(query, sizeof(query), "storage=internal&file=%s&layer=%d", file, layer);
    int peer = start(query, &code, reply, cap);
    if (peer >= 0) code = finish(peer, reply, cap);
    idle();
    return code;
}

/* A request whose scan is held until the budget has run out: it ends in the limit. */
static int get_held(const char *file, int layer, char *reply, size_t cap) {
    char query[256]; int code;
    layer_budget_ms = 50;
    snprintf(query, sizeof(query), "storage=internal&file=%s&layer=%d", file, layer);
    layer_path_scan_hook = hold_scan;
    int peer = start(query, &code, reply, cap);
    assert(peer >= 0);
    struct timespec budget = {0, (50 + 30) * 1000000L}; nanosleep(&budget, NULL);
    assert(write(gate[1], "x", 1) == 1);
    code = finish(peer, reply, cap);
    layer_path_scan_hook = NULL;
    layer_budget_ms = 20000;
    idle();
    return code;
}

static void write_big(const char *path, int layers, int repeat_at_end, int skip_layer) {
    FILE *f = fopen(path, "w"); assert(f);
    for (int layer = 0; layer < layers; layer++) {
        if (layer == skip_layer) continue; /* a layer number the file leaves out */
        assert(fprintf(f, ";LAYER:%d\nG1 Z%.2f\n", layer, 0.2 * (layer + 1)) > 0);
        for (int line = 0; line < 200; line++)
            assert(fprintf(f, "G1 X%d.0 Y%d.0 E0.01\n", 20 + line % 100, 20 + layer % 100) > 0);
    }
    if (repeat_at_end) assert(fputs(";LAYER:0\nG1 X5 Y5 E0.01\n", f) >= 0); /* back to an earlier layer: no jump possible */
    assert(!fclose(f));
}

/* A map whose entries for the layer indexes `layer0` and `layer1` (-1: none) point `delta` bytes away from their marker line;
 * its checksum is right. */
static void save_bent_map2(const char *path, int layer0, int layer1, long delta) {
    char map[1200]; FILE *src = fopen(path, "rb"); assert(src);
    layer_index built; assert(layer_index_build(src, &built, 0, NULL, NULL) == LAYER_PATH_OK); fclose(src);
    int found = 0;
    for (int i = 0; i < built.count; i++)
        if (built.entries[i].layer == layer0 || built.entries[i].layer == layer1) { built.entries[i].offset += delta; found++; }
    assert(found >= 1 && layer_store_index_path(path, map, sizeof(map)) == 0 && layer_index_save(map, &built) == 0);
    layer_index_free(&built);
}
static void save_bent_map(const char *path, int layer0, long delta) { save_bent_map2(path, layer0, -1, delta); }

static int map_exists(const char *path) {
    char map[1200]; return layer_store_index_path(path, map, sizeof(map)) == 0 && access(map, F_OK) == 0;
}
static int queued(const char *path) {
    pthread_mutex_lock(&layer_store_lock);
    int found = 0; for (int i = 0; i < layer_store_count; i++) if (!strcmp(layer_store_queue[i], path)) found = 1;
    pthread_mutex_unlock(&layer_store_lock);
    return found;
}
static void unqueue(const char *path) {
    pthread_mutex_lock(&layer_store_lock); layer_store_dequeue_locked(path); pthread_mutex_unlock(&layer_store_lock);
}

static void save_map(const char *path) {
    char map[1200]; FILE *src = fopen(path, "rb"); assert(src);
    layer_index built; assert(layer_index_build(src, &built, 0, NULL, NULL) == LAYER_PATH_OK); fclose(src);
    assert(layer_store_index_path(path, map, sizeof(map)) == 0 && layer_index_save(map, &built) == 0);
    layer_index_free(&built);
}

/* A request while the printer homes or starts a print (read hold): only the refusal or the retry is answered. */
static int get_held_by_hold(const char *file, int layer, char *reply, size_t cap) {
    char query[256]; snprintf(query, sizeof(query), "storage=internal&file=%s&layer=%d", file, layer);
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    assert(!gcode_layer_path_begin(pair[0], query, 1));
    close(pair[0]);
    ssize_t n = read(pair[1], reply, cap - 1); close(pair[1]);
    assert(n > 0); reply[n] = 0;
    return atoi(strchr(reply, ' ') + 1);
}

static int limit_valid(void) {
    pthread_mutex_lock(&layer_path_cache_lock); int v = layer_path_limited.valid; pthread_mutex_unlock(&layer_path_cache_lock);
    return v;
}

static void drop_map(const char *path) {
    char map[1200];
    if (layer_store_index_path(path, map, sizeof(map)) == 0) unlink(map);
}

int main(void) {
    char dir[] = "/tmp/layer-limit-map-XXXXXX"; assert(mkdtemp(dir));
    gcode_internal_root = dir;
    layer_path_rest_min_ms = 0; layer_path_rest_percent = 0;
    char maps[600]; snprintf(maps, sizeof(maps), "%s/maps", dir);
    assert(!mkdir(maps, 0700)); layer_store_dir = maps;
    assert(!pipe(gate));
    char reply[LAYER_PATH_JSON_MAX + 512];
    char big[600], other[600], repeat[600], gap[600];
    snprintf(big, sizeof(big), "%s/big.gcode", dir);
    snprintf(other, sizeof(other), "%s/other.gcode", dir);
    snprintf(repeat, sizeof(repeat), "%s/repeat.gcode", dir);
    snprintf(gap, sizeof(gap), "%s/gap.gcode", dir);
    write_big(big, 60, 0, -1); write_big(other, 60, 0, -1); write_big(repeat, 60, 1, -1); write_big(gap, 60, 0, 24);

    /* (a) A limit from a full scan (no map yet) refuses that layer and every later one. */
    assert(get_held("big.gcode", 20, reply, sizeof(reply)) == 413);
    assert(get("big.gcode", 30, reply, sizeof(reply)) == 413 && strstr(reply, "Layer path scan limit reached"));
    assert(get("big.gcode", 20, reply, sizeof(reply)) == 413);
    assert(get("big.gcode", 10, reply, sizeof(reply)) == 200); /* an earlier layer is still scanned */

    /* While the printer homes (read hold), nothing of the file is read to find out: without a map file the 413 stays ... */
    assert(get_held_by_hold("big.gcode", 30, reply, sizeof(reply)) == 413);
    /* (c, d) The map becomes current: the memory no longer refuses what the map answers, for the layer
     * that hit the limit, for later ones, and for the last layer. */
    save_map(big);
    /* ... with one, the answer is the retry (503), and the limit is not touched: no read happened. */
    assert(get_held_by_hold("big.gcode", 30, reply, sizeof(reply)) == 503 && strstr(reply, "homing") && limit_valid());
    assert(get("big.gcode", 30, reply, sizeof(reply)) == 200 && strstr(reply, "\"layer\":30"));
    assert(!limit_valid()); /* a map that serves every layer ended the memory */
    assert(get("big.gcode", 20, reply, sizeof(reply)) == 200);
    assert(get("big.gcode", 60, reply, sizeof(reply)) == 200);
    assert(get("big.gcode", 61, reply, sizeof(reply)) == 404); /* past the end: answered through the map, not by the memory */

    /* The memory was ended, not paused: if the map goes away again, a full scan that hits the limit starts a new one. */
    drop_map(big);
    assert(get_held("big.gcode", 25, reply, sizeof(reply)) == 413);
    assert(get("big.gcode", 40, reply, sizeof(reply)) == 413);
    drop_map(big);

    /* (b) A limit hit while reading through the map refuses that one layer, not the later ones. */
    save_map(other);
    assert(get_held("other.gcode", 20, reply, sizeof(reply)) == 413);
    assert(get("other.gcode", 30, reply, sizeof(reply)) == 200 && strstr(reply, "\"layer\":30"));
    assert(get("other.gcode", 21, reply, sizeof(reply)) == 200);
    assert(get("other.gcode", 20, reply, sizeof(reply)) == 413); /* the same layer is not read again at once */
    assert(get("other.gcode", 10, reply, sizeof(reply)) == 200);

    /* A map that cannot serve the file (a layer number comes back) is no reason to end the memory. */
    assert(get_held("repeat.gcode", 20, reply, sizeof(reply)) == 413);
    save_map(repeat);
    assert(get("repeat.gcode", 30, reply, sizeof(reply)) == 413 && strstr(reply, "Layer path scan limit reached"));
    {   /* ... and the map is not read again for every refused request: the limit remembers it is useless */
        pthread_mutex_lock(&layer_path_cache_lock); int useless = layer_path_limited.map_useless; pthread_mutex_unlock(&layer_path_cache_lock);
        assert(useless);
        assert(get("repeat.gcode", 31, reply, sizeof(reply)) == 413);
    }

    /* A map with a gap in the numbering serves the layers it has and leaves the limit for the number it skips. */
    assert(get_held("gap.gcode", 20, reply, sizeof(reply)) == 413);
    save_map(gap);
    assert(get("gap.gcode", 30, reply, sizeof(reply)) == 200 && strstr(reply, "\"layer\":30"));
    assert(limit_valid());
    assert(get("gap.gcode", 25, reply, sizeof(reply)) == 413); /* layer number 25 (index 24) is not in the file */
    assert(get("gap.gcode", 26, reply, sizeof(reply)) == 200);

    /* A map that is current and serves every layer, but whose offset for one layer points into a line: for that layer it
     * serves nothing, so the refusal of the scan limit stays (it does not fall into a new full scan on every request);
     * the other layers still come through the map. */
    {
        char bent[600]; snprintf(bent, sizeof(bent), "%s/bent.gcode", dir);
        write_big(bent, 60, 0, -1);
        assert(get_held("bent.gcode", 30, reply, sizeof(reply)) == 413);
        save_bent_map(bent, 29, 3); /* layer number 30 */
        pthread_mutex_lock(&layer_path_cache_lock); unsigned long scans = layer_path_stats.scans; pthread_mutex_unlock(&layer_path_cache_lock);
        for (int again = 0; again < 3; again++) {
            assert(get("bent.gcode", 30, reply, sizeof(reply)) == 413 && strstr(reply, "Layer path scan limit reached"));
            assert(limit_valid());
        }
        pthread_mutex_lock(&layer_path_cache_lock); assert(layer_path_stats.scans == scans); pthread_mutex_unlock(&layer_path_cache_lock);
        assert(get("bent.gcode", 31, reply, sizeof(reply)) == 200 && strstr(reply, "\"layer\":31")); /* the rest is served */
        drop_map(bent); unlink(bent);
        /* A map that is off only at its last layer does not end the whole refusal ("serves every layer" is not true of it):
         * a layer it can read is served, the refusal stays for the others. */
        char bent2[600]; snprintf(bent2, sizeof(bent2), "%s/bent2.gcode", dir);
        write_big(bent2, 60, 0, -1);
        assert(get_held("bent2.gcode", 30, reply, sizeof(reply)) == 413);
        save_bent_map(bent2, 59, 3); /* layer number 60 */
        assert(get("bent2.gcode", 31, reply, sizeof(reply)) == 200 && strstr(reply, "\"layer\":31"));
        assert(limit_valid());
        assert(get("bent2.gcode", 60, reply, sizeof(reply)) == 413 && strstr(reply, "Layer path scan limit reached"));
        assert(get("bent2.gcode", 32, reply, sizeof(reply)) == 200);
        drop_map(bent2); unlink(bent2);
    }

    /* A map with a wrong offset is dropped and queued for a new one, once for the same file; the refusal of the scan limit
     * stays in both cases (the map never serves a layer whose offset it cannot read). Layer 40 holds the refusal, layer 20 is
     * an earlier layer that the worker reads, with a wrong offset for both. */
    {
        char bent3[600]; snprintf(bent3, sizeof(bent3), "%s/bent3.gcode", dir);
        write_big(bent3, 60, 0, -1);
        assert(get_held("bent3.gcode", 40, reply, sizeof(reply)) == 413);
        save_bent_map2(bent3, 19, 39, 3); unqueue(bent3);
        assert(get("bent3.gcode", 40, reply, sizeof(reply)) == 413 && limit_valid()); /* the map is not asked */
        unsigned long bad0 = atomic_load(&layer_store_stat_bad);
        assert(get("bent3.gcode", 20, reply, sizeof(reply)) == 200); /* read from the start, the map is dropped */
        assert(atomic_load(&layer_store_stat_bad) == bad0 + 1 && !map_exists(bent3));
        assert(queued(bent3));
        assert(get("bent3.gcode", 40, reply, sizeof(reply)) == 413 && limit_valid()); /* no map: the refusal stays */
        atomic_store(&layer_store_idle, 1);
        assert(layer_store_build_one(bent3) == 0 && map_exists(bent3)); /* the builder makes it again */
        atomic_store(&layer_store_idle, 0);
        unqueue(bent3);
        unsigned long hits0 = atomic_load(&layer_store_stat_hits);
        assert(get("bent3.gcode", 21, reply, sizeof(reply)) == 200 && atomic_load(&layer_store_stat_hits) == hits0 + 1);
        /* The same file, wrong again: not dropped again, not queued again; the refusal stays. */
        save_bent_map2(bent3, 19, 39, 3);
        assert(get("bent3.gcode", 20, reply, sizeof(reply)) == 200);
        assert(atomic_load(&layer_store_stat_bad) == bad0 + 2 && map_exists(bent3) && !queued(bent3));
        assert(get("bent3.gcode", 40, reply, sizeof(reply)) == 413 && limit_valid());
        drop_map(bent3); unlink(bent3);
    }

    close(gate[0]); close(gate[1]);
    drop_map(big); drop_map(other); drop_map(repeat); drop_map(gap);
    unlink(big); unlink(other); unlink(repeat); unlink(gap); rmdir(maps); rmdir(dir);
    puts("PASS layer path limit and layer map: a limit from a full scan stays, one from a map read refuses only that layer, "
         "a current map ends the memory, a map that cannot serve the file or a layer does not, nothing is read while the printer homes");
    return 0;
}
