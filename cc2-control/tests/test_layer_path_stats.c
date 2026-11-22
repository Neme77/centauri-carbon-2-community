/* /api/health counts the layer-path work (V7): scans, requests refused as not fitting the running print,
 * "busy" answers, bytes read, the longest and the last scan in milliseconds. In memory only. Answers
 * from the remembered layer and refusals before reading are not scans. */
#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

static int gate[2];
static int hold; /* 1: the scan waits for a byte on the gate; else it sleeps hold_ms */
static long hold_ms;
static void scan_hook(void) {
    if (hold) { char byte; assert(read(gate[0], &byte, 1) == 1); }
    else if (hold_ms) { struct timespec t = {hold_ms / 1000, (hold_ms % 1000) * 1000000L}; nanosleep(&t, NULL); }
}
static void sleep_ms(long ms) { struct timespec t = {ms / 1000, (ms % 1000) * 1000000L}; nanosleep(&t, NULL); }

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
static void idle_at(int line) {
    for (int i = 0; i < 2000 && atomic_load(&active_layer_paths); i++) sleep_ms(5);
    if (atomic_load(&active_layer_paths)) { fprintf(stderr, "line %d: scan still active (%d)\n", line, (int)atomic_load(&active_layer_paths)); abort(); }
}
#define idle() idle_at(__LINE__)
static int wait_idle = 1; /* 0 while a scan is held on purpose */
static char last_reply[LAYER_PATH_JSON_MAX + 1024]; /* the whole reply of the last get() */
/* One request to the service's own handler, like the guard test. */
static int get(mqtt_client *m, const char *query) {
    int pair[2], listener = socket(AF_INET, SOCK_STREAM, 0); assert(listener >= 0);
    struct sockaddr_in address = {0}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0);
    socklen_t size = sizeof(address); assert(getsockname(listener, (struct sockaddr *)&address, &size) == 0);
    assert(listen(listener, 1) == 0); pair[1] = socket(AF_INET, SOCK_STREAM, 0); assert(pair[1] >= 0);
    assert(connect(pair[1], (struct sockaddr *)&address, size) == 0);
    pair[0] = accept(listener, NULL, NULL); assert(pair[0] >= 0); close(listener);
    char request[600];
    int n = snprintf(request, sizeof(request), "GET %s HTTP/1.1\r\nHost: localhost\r\n\r\n", query);
    if (!handle_client(pair[0], request, (size_t)n, "./web", m, NULL)) close(pair[0]);
    size_t used = 0; ssize_t got;
    while ((got = recv(pair[1], last_reply + used, sizeof(last_reply) - 1 - used, 0)) > 0) used += (size_t)got;
    last_reply[used] = 0; close(pair[1]);
    if (wait_idle) idle();
    return atoi(strchr(last_reply, ' ') + 1);
}
static char health[4096];
static void read_health(mqtt_client *m) {
    int code = get(m, "/api/health");
    assert(code == 200);
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    health_response(pair[0], m);
    ssize_t n = recv(pair[1], health, sizeof(health) - 1, 0); assert(n > 0); health[n] = 0;
    close(pair[0]); close(pair[1]);
}
static long long field(const char *key) {
    char needle[64]; snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(health, needle); assert(p);
    return strtoll(p + strlen(needle), NULL, 10);
}
#define STATS(scans, rejected, busy) do { read_health(&m); \
    if (field("layer_path_scans") != (scans) || field("layer_path_rejected") != (rejected) || \
        field("layer_path_busy") != (busy)) { \
        fprintf(stderr, "line %d: scans %lld rejected %lld busy %lld, expected %d %d %d\n%s\n", __LINE__, \
            field("layer_path_scans"), field("layer_path_rejected"), field("layer_path_busy"), \
            (scans), (rejected), (busy), health); return 1; } } while (0)

int main(void) {
    char dir[] = "/tmp/layer-stats-XXXXXX"; assert(mkdtemp(dir));
    gcode_internal_root = dir;
    char path[512]; snprintf(path, sizeof(path), "%s/print.gcode", dir);
    FILE *f = fopen(path, "w"); assert(f);
    for (int layer = 0; layer < 20; layer++) {
        assert(fprintf(f, ";LAYER:%d\nG1 Z%.2f\n", layer, 0.2 * (layer + 1)) > 0);
        for (int line = 0; line < 50; line++) assert(fprintf(f, "G1 X%d.0 Y%d.0 E0.01\n", 20 + line, 20 + layer) > 0);
    }
    assert(!fclose(f));
    struct stat st; assert(!stat(path, &st));
    assert(!pipe(gate));
    layer_path_scan_hook = scan_hook;
    layer_path_rest_min_ms = 0; layer_path_rest_percent = 0;
    uds_init(&telemetry);
    mqtt_client m; memset(&m, 0, sizeof(m));
    m.connected = m.registered = m.have_machine_status = 1; m.machine_status = 1;
    m.last_message = time(NULL);
    int code;

    /* Nothing yet. */
    STATS(0, 0, 0);
    assert(field("layer_path_bytes_read") == 0 && field("layer_path_longest_ms") == 0 && field("layer_path_last_ms") == 0);
    assert(strstr(health, "\"mqtt_oversized_replies\":0,\"layer_path_scans\":0")); /* one valid JSON object */

    /* A cold scan of the last layer reads the whole file; it took at least the 120 ms the hook sleeps. */
    hold = 0; hold_ms = 120;
    int peer = start("storage=internal&file=print.gcode&layer=20", &code);
    assert(peer >= 0 && finish(peer) == 200); idle();
    STATS(1, 0, 0);
    assert(field("layer_path_bytes_read") == st.st_size);
    long long longest = field("layer_path_longest_ms");
    assert(longest >= 110 && longest < 3000 && field("layer_path_last_ms") == longest);

    /* The same layer again comes from memory: no scan, no bytes. */
    assert(start("storage=internal&file=print.gcode&layer=20", &code) < 0 && code == 200);
    STATS(1, 0, 0);
    assert(field("layer_path_bytes_read") == st.st_size);

    /* A quick scan: last is the new time, longest stays; bytes add up and stay within what a scan can read. */
    hold_ms = 0;
    peer = start("storage=internal&file=print.gcode&layer=19", &code);
    assert(peer >= 0 && finish(peer) == 200); idle();
    STATS(2, 0, 0);
    long long bytes = field("layer_path_bytes_read");
    /* It resumed at the remembered start of a late layer: only a small part of the file was read. */
    assert(bytes > st.st_size && bytes - st.st_size < st.st_size / 4);
    assert(field("layer_path_longest_ms") == longest && field("layer_path_last_ms") < longest);
    /* A scan that ends in "outside the file" is a scan too. */
    peer = start("storage=internal&file=print.gcode&layer=21", &code);
    assert(peer >= 0 && finish(peer) == 404); idle();
    STATS(3, 0, 0);

    /* Busy: a second request while one scan is held, and one in the pause after a scan. */
    hold = 1;
    peer = start("storage=internal&file=print.gcode&layer=5", &code);
    assert(peer >= 0);
    assert(start("storage=internal&file=print.gcode&layer=6", &code) < 0 && code == 503);
    wait_idle = 0; STATS(3, 0, 1); wait_idle = 1; /* the held scan is still running */
    assert(write(gate[1], "x", 1) == 1);
    assert(finish(peer) == 200); idle();
    hold = 0;
    layer_path_rest_min_ms = 400;
    peer = start("storage=internal&file=print.gcode&layer=7", &code);
    assert(peer >= 0 && finish(peer) == 200); idle();
    assert(start("storage=internal&file=print.gcode&layer=8", &code) < 0 && code == 503);
    STATS(5, 0, 2);
    layer_path_rest_min_ms = 0; sleep_ms(450);

    /* Layer maps: every scan so far found no map and read from the start (fallbacks); none was answered through a map. */
    read_health(&m);
    assert(field("layer_map_hits") == 0 && field("layer_map_fallbacks") == field("layer_path_scans") &&
           field("layer_map_built") == 0 && field("layer_map_failed") == 0);
    char maps[600]; snprintf(maps, sizeof(maps), "%s/maps", dir);
    assert(!mkdir(maps, 0700)); layer_store_dir = maps;
    {   char mp[1200]; FILE *src = fopen(path, "rb"); assert(src);
        layer_index built_index; assert(layer_index_build(src, &built_index, 0, NULL, NULL) == LAYER_PATH_OK); fclose(src);
        assert(layer_store_index_path(path, mp, sizeof(mp)) == 0 && layer_index_save(mp, &built_index) == 0);
        layer_index_free(&built_index); }
    long long scans_before_map = field("layer_path_scans"), fallbacks_before = field("layer_map_fallbacks");

    /* Refused as not fitting the running print: counted, not a scan, no bytes. */
    bytes = field("layer_path_bytes_read");
    m.machine_status = 2; m.last_message = time(NULL);
    snprintf(m.filename, sizeof(m.filename), "print.gcode");
    snprintf(m.print_state, sizeof(m.print_state), "printing");
    m.have_total_layers = 1; m.total_layers = 20; m.current_layer = 0;
    assert(get(&m, "/api/gcode-files/layer-path?storage=internal&file=print.gcode&layer=19") == 409);
    assert(get(&m, "/api/gcode-files/layer-path?storage=internal&file=print.gcode&layer=99") == 409);
    STATS(5, 2, 2);
    assert(field("layer_path_bytes_read") == bytes);
    /* An allowed request does not count as rejected. */
    m.current_layer = 4;
    assert(get(&m, "/api/gcode-files/layer-path?storage=internal&file=print.gcode&layer=11") == 200);
    STATS(6, 2, 2);
    /* That scan went through the map: a hit; the fallbacks stay as they were and every scan is one or the other. */
    assert(field("layer_map_hits") == 1 && field("layer_map_fallbacks") == fallbacks_before &&
           field("layer_path_scans") == scans_before_map + 1);

    /* Through the map only the layer is read: bytes_read grows by the layer's own size (and the next marker line that ends it),
     * not by the position in the file. A layer beyond the end is answered by the map without reading: no bytes at all. */
    m.machine_status = 1; m.print_state[0] = 0; m.last_message = time(NULL);
    char mp[1200]; static char good_reply[sizeof(last_reply)];
    assert(layer_store_index_path(path, mp, sizeof(mp)) == 0);
    long long o14, o15;
    { layer_index ix; FILE *src = fopen(path, "rb"); assert(src);
      assert(layer_index_build(src, &ix, 0, NULL, NULL) == LAYER_PATH_OK); fclose(src);
      assert(ix.count > 15 && ix.entries[14].layer == 14 && ix.entries[15].layer == 15);
      o14 = ix.entries[14].offset; o15 = ix.entries[15].offset; layer_index_free(&ix); }
    {
        read_health(&m);
        long long hits = field("layer_map_hits"), fallbacks = field("layer_map_fallbacks"), b0 = field("layer_path_bytes_read");
        assert(get(&m, "/api/gcode-files/layer-path?storage=internal&file=print.gcode&layer=15") == 200);
        memcpy(good_reply, last_reply, sizeof(good_reply));
        read_health(&m);
        assert(field("layer_map_hits") == hits + 1 && field("layer_map_fallbacks") == fallbacks && field("layer_map_bad") == 0);
        long long grew = field("layer_path_bytes_read") - b0, own = o15 - o14;
        if (grew < own || grew > own + 64) { fprintf(stderr, "bytes read grew by %lld for a layer of %lld bytes\n", grew, own); return 1; }
        b0 = field("layer_path_bytes_read");
        assert(get(&m, "/api/gcode-files/layer-path?storage=internal&file=print.gcode&layer=21") == 404); /* outside the file */
        read_health(&m);
        assert(field("layer_path_bytes_read") == b0 && field("layer_map_hits") == hits + 2);
    }

    /* A map whose offset for one layer points into the middle of a line (its checksum is right, it is saved after the
     * change): that layer is read from the start, counted as a fallback and answered exactly as through the intact map.
     * The map is dropped and its file queued for a new one, but only once for the same file: a builder that makes the same
     * wrong map again is not asked in a loop (layer_map_bad keeps counting). */
    {
#define BEND() do { layer_index ix; FILE *src = fopen(path, "rb"); assert(src); \
        assert(layer_index_build(src, &ix, 0, NULL, NULL) == LAYER_PATH_OK); fclose(src); \
        int found = 0; for (int i = 0; i < ix.count; i++) if (ix.entries[i].layer == 14) { ix.entries[i].offset += 3; found = 1; } \
        assert(found && layer_index_save(mp, &ix) == 0); layer_index_free(&ix); } while (0)
        assert(get(&m, "/api/gcode-files/layer-path?storage=internal&file=print.gcode&layer=16") == 200); /* leaves the memory */
        pthread_mutex_lock(&layer_store_lock); layer_store_dequeue_locked(path); pthread_mutex_unlock(&layer_store_lock);
        BEND();
        read_health(&m);
        long long hits = field("layer_map_hits"), fallbacks = field("layer_map_fallbacks");
        assert(get(&m, "/api/gcode-files/layer-path?storage=internal&file=print.gcode&layer=15") == 200);
        assert(!strcmp(last_reply, good_reply)); /* the same answer */
        read_health(&m);
        assert(field("layer_map_hits") == hits && field("layer_map_fallbacks") == fallbacks + 1 && field("layer_map_bad") == 1);
        /* First time for this file: the map is gone and the file is queued for a new one. */
        assert(access(mp, F_OK) != 0);
        { int found = 0; for (int i = 0; i < layer_store_count; i++) if (!strcmp(layer_store_queue[i], path)) found = 1; assert(found); }
        /* The builder makes it again: it serves again. */
        atomic_store(&layer_store_idle, 1);
        assert(layer_store_build_one(path) == 0 && access(mp, F_OK) == 0);
        atomic_store(&layer_store_idle, 0);
        pthread_mutex_lock(&layer_store_lock); layer_store_dequeue_locked(path); pthread_mutex_unlock(&layer_store_lock);
        assert(get(&m, "/api/gcode-files/layer-path?storage=internal&file=print.gcode&layer=17") == 200);
        read_health(&m);
        assert(field("layer_map_hits") == hits + 1 && field("layer_map_bad") == 1);
        /* The same file, wrong again: not dropped again, not queued again, still answered by the full scan. */
        BEND();
        assert(get(&m, "/api/gcode-files/layer-path?storage=internal&file=print.gcode&layer=15") == 200);
        assert(!strcmp(last_reply, good_reply));
        read_health(&m);
        assert(field("layer_map_bad") == 2 && field("layer_map_fallbacks") == fallbacks + 2 && field("layer_map_hits") == hits + 1);
        assert(access(mp, F_OK) == 0);
        { int found = 0; for (int i = 0; i < layer_store_count; i++) if (!strcmp(layer_store_queue[i], path)) found = 1; assert(!found); }
    }

    close(gate[0]); close(gate[1]);
    unlink(mp);
    rmdir(maps); unlink(path); rmdir(dir);
    puts("PASS layer path stats: scans, rejected, busy, bytes read, longest and last scan, and the layer map hits and fallbacks appear in /api/health; bytes read through a map are the layer's own; a map offset that is no marker line is a fallback with the same answer, dropped and queued once");
    return 0;
}
