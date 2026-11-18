/* The layer-path endpoint refuses, before it reads any G-code, a request for the running print's file that
 * cannot belong to the print: a layer above the layer count, or above 1 while no layer >= 1 has been seen
 * (seen: a stale layer 193 right after a start meant a 20 s scan of 78.7 MB). Other files and idle
 * browsing are untouched. The refusal is 409, not the "busy" 503 the page retries. Until the print reaches
 * layer 1 the read hold (test_gcode_read_guard.c) also keeps every other request from reading: 503, after
 * the 409 refusals. */
#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

static char reply[LAYER_PATH_JSON_MAX + 1024];
static int scans;
static void count_scan(void) { scans++; }

static int get(mqtt_client *m, const char *query) {
    int pair[2], listener = socket(AF_INET, SOCK_STREAM, 0); assert(listener >= 0);
    struct sockaddr_in address = {0}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0);
    socklen_t size = sizeof(address); assert(getsockname(listener, (struct sockaddr *)&address, &size) == 0);
    assert(listen(listener, 1) == 0); pair[1] = socket(AF_INET, SOCK_STREAM, 0); assert(pair[1] >= 0);
    assert(connect(pair[1], (struct sockaddr *)&address, size) == 0);
    pair[0] = accept(listener, NULL, NULL); assert(pair[0] >= 0); close(listener);
    char request[600];
    int n = snprintf(request, sizeof(request), "GET /api/gcode-files/layer-path?%s HTTP/1.1\r\nHost: localhost\r\n\r\n", query);
    if (!handle_client(pair[0], request, (size_t)n, "./web", m, NULL)) close(pair[0]); /* else the worker closes it */
    size_t used = 0; ssize_t got;
    while ((got = recv(pair[1], reply + used, sizeof(reply) - 1 - used, 0)) > 0) used += (size_t)got;
    reply[used] = 0; close(pair[1]);
    for (int i = 0; i < 2000 && atomic_load(&active_layer_paths); i++) { struct timespec t = {0, 5000000}; nanosleep(&t, NULL); }
    assert(atomic_load(&active_layer_paths) == 0);
    return atoi(strchr(reply, ' ') + 1);
}
#define ASK(code_expected, scans_expected, query) do { int before = scans; int code = get(&m, query); \
    if (code != (code_expected) || scans - before != (scans_expected)) { \
        fprintf(stderr, "line %d: %s -> %d (expected %d), scans %d (expected %d)\n%s\n", __LINE__, query, code, \
                (code_expected), scans - before, (scans_expected), reply); return 1; } } while (0)
#define REFUSED(text, query) do { ASK(409, 0, query); assert(strstr(reply, text)); } while (0)

static void file_with_layers(const char *dir, const char *name, int layers) {
    char path[512]; snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w"); assert(f);
    for (int layer = 0; layer < layers; layer++) {
        assert(fprintf(f, ";LAYER:%d\nG1 Z%.2f\n", layer, 0.2 * (layer + 1)) > 0);
        for (int line = 0; line < 10; line++) assert(fprintf(f, "G1 X%d.0 Y%d.0 E0.01\n", 20 + line, 20 + layer) > 0);
    }
    assert(!fclose(f));
}

int main(void) {
    char dir[] = "/tmp/layer-guard-XXXXXX"; assert(mkdtemp(dir));
    gcode_internal_root = dir;
    file_with_layers(dir, "print.gcode", 20);
    file_with_layers(dir, "other.gcode", 30);
    layer_path_rest_min_ms = 0; layer_path_rest_percent = 0;
    layer_path_scan_hook = count_scan;
    uds_init(&telemetry);

    mqtt_client m; memset(&m, 0, sizeof(m));
    m.connected = m.registered = m.have_machine_status = 1; m.machine_status = 2;
    m.last_message = time(NULL);
    snprintf(m.filename, sizeof(m.filename), "print.gcode");
    snprintf(m.print_state, sizeof(m.print_state), "printing");
    m.have_total_layers = 1; m.total_layers = 20; m.current_layer = 0;

    /* Right after a start nothing but layer 1 is plausible, and even layer 1 is not read before the print reaches
     * it: the read hold answers 503 without a scan. */
    ASK(503, 0, "storage=internal&file=print.gcode&layer=1");
    assert(strstr(reply, "Printer is homing or starting a print; retry shortly"));
    REFUSED("has not reached", "storage=internal&file=print.gcode&layer=2");
    REFUSED("beyond the running print", "storage=internal&file=print.gcode&layer=193");
    /* Above the layer count, whatever the printer says. */
    REFUSED("beyond the running print", "storage=internal&file=print.gcode&layer=21");
    m.current_layer = 3;
    REFUSED("beyond the running print", "storage=internal&file=print.gcode&layer=21");
    REFUSED("beyond the running print", "storage=internal&file=print.gcode&layer=1000");
    /* Once layer 1 or more is reported, any layer up to the count is read (browsing, Live, previous layer). */
    ASK(200, 1, "storage=internal&file=print.gcode&layer=5");
    ASK(200, 1, "storage=internal&file=print.gcode&layer=20");
    ASK(200, 1, "storage=internal&file=print.gcode&layer=2");
    /* A paused print is still the running print. */
    m.current_layer = 0; snprintf(m.print_state, sizeof(m.print_state), "paused");
    REFUSED("has not reached", "storage=internal&file=print.gcode&layer=7");
    snprintf(m.print_state, sizeof(m.print_state), "printing");
    /* Other files of the same storage, and other storages, are not the running print's: not refused, but before
     * layer 1 they wait for the read hold like every file. */
    ASK(503, 0, "storage=internal&file=other.gcode&layer=25");
    assert(strstr(reply, "Printer is homing or starting a print"));
    m.current_layer = 3;
    ASK(200, 1, "storage=internal&file=other.gcode&layer=25");
    m.current_layer = 0;
    assert(get(&m, "storage=usb&file=print.gcode&layer=9") != 409);
    /* The printer's file name may carry its storage prefix. */
    snprintf(m.filename, sizeof(m.filename), "local/print.gcode");
    REFUSED("has not reached", "storage=internal&file=print.gcode&layer=8");
    snprintf(m.filename, sizeof(m.filename), "print.gcode");
    /* The direct stream's layer wins over MQTT's, like in /api/printer: a stale 193 from MQTT does not confirm. */
    m.current_layer = 193;
    {
        int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair)); telemetry.fd = pair[0];
        const char *snap = "{\"id\":11,\"result\":{\"eventtime\":1,\"status\":{\"print_stats\":{\"state\":\"printing\",\"filename\":\"local/print.gcode\",\"info\":{\"current_layer\":0}}}}}";
        assert(uds_message(&telemetry, snap, strlen(snap)));
        REFUSED("has not reached", "storage=internal&file=print.gcode&layer=10");
        const char *up = "{\"method\":\"cc2_status\",\"params\":{\"eventtime\":2,\"status\":{\"print_stats\":{\"info\":{\"current_layer\":4}}}}}";
        assert(uds_message(&telemetry, up, strlen(up)));
        ASK(200, 1, "storage=internal&file=print.gcode&layer=10");
        uds_init(&telemetry);
    }
    /* The printer's own count wins over the file's: 15 reported, 20 in the file. */
    m.current_layer = 3; m.total_layers = 15;
    REFUSED("beyond the running print", "storage=internal&file=print.gcode&layer=16");
    ASK(200, 1, "storage=internal&file=print.gcode&layer=15");
    m.total_layers = 20; m.current_layer = 0;
    /* Each part of "a print is running" is needed: connected, registered, fresh, machine status 2, state. */
    m.connected = 0; ASK(200, 1, "storage=internal&file=print.gcode&layer=16"); m.connected = 1;
    m.registered = 0; ASK(200, 1, "storage=internal&file=print.gcode&layer=17"); m.registered = 1;
    m.have_machine_status = 0; ASK(200, 1, "storage=internal&file=print.gcode&layer=18"); m.have_machine_status = 1;
    m.machine_status = 1; ASK(200, 1, "storage=internal&file=print.gcode&layer=19"); m.machine_status = 2;
    /* Without a file name it is not the running print's file (no 409); the start phase still holds the read. */
    m.filename[0] = 0; ASK(503, 0, "storage=internal&file=print.gcode&layer=11");
    m.current_layer = 1; ASK(200, 1, "storage=internal&file=print.gcode&layer=11"); m.current_layer = 0;
    snprintf(m.filename, sizeof(m.filename), "print.gcode");
    REFUSED("has not reached", "storage=internal&file=print.gcode&layer=16");
    /* Not printing: browsing by hand stays as it was, including the file's own "outside" answer. */
    m.current_layer = 0; m.machine_status = 1; snprintf(m.print_state, sizeof(m.print_state), "complete");
    ASK(200, 1, "storage=internal&file=print.gcode&layer=12");
    ASK(404, 1, "storage=internal&file=print.gcode&layer=21");
    /* Telemetry that has gone stale is no running print either. */
    m.machine_status = 2; snprintf(m.print_state, sizeof(m.print_state), "printing");
    m.last_message = time(NULL) - 60;
    ASK(200, 1, "storage=internal&file=print.gcode&layer=13");
    m.last_message = time(NULL);
    /* The layer count may come from the file when the printer does not report it. */
    m.have_total_layers = 0;
    for (int i = 0; i < 2000 && active_gcode_total_layers(m.filename) != 20; i++) { struct timespec t = {0, 5000000}; nanosleep(&t, NULL); }
    assert(active_gcode_total_layers(m.filename) == 20);
    m.current_layer = 2;
    REFUSED("beyond the running print", "storage=internal&file=print.gcode&layer=21");
    ASK(200, 1, "storage=internal&file=print.gcode&layer=14");
    /* Malformed requests keep their own answers. */
    assert(get(&m, "storage=internal&file=print.gcode&layer=0") == 400);
    assert(get(&m, "storage=internal&file=print.gcode&layer=x") == 400);

    char path[512];
    snprintf(path, sizeof(path), "%s/print.gcode", dir); unlink(path);
    snprintf(path, sizeof(path), "%s/other.gcode", dir); unlink(path);
    rmdir(dir);
    puts("PASS layer path guard: stale or impossible layers of the running print are refused before reading, other files and idle browsing pass");
    return 0;
}
