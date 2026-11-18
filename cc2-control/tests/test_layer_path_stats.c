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
    static char reply[LAYER_PATH_JSON_MAX + 1024];
    size_t used = 0; ssize_t got;
    while ((got = recv(pair[1], reply + used, sizeof(reply) - 1 - used, 0)) > 0) used += (size_t)got;
    reply[used] = 0; close(pair[1]);
    if (wait_idle) idle();
    return atoi(strchr(reply, ' ') + 1);
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

    close(gate[0]); close(gate[1]);
    unlink(path); rmdir(dir);
    puts("PASS layer path stats: scans, rejected, busy, bytes read, longest and last scan appear in /api/health");
    return 0;
}
