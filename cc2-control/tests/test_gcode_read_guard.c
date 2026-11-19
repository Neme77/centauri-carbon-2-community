/* The read hold: while the printer homes, levels, probes or starts a print, no read of a G-code file begins for
 * the layer path, the object preview or a picture, whichever file is asked for (seen: a long read during the
 * G28 of a print start, then "Timer too close", error 803). Each state turns it on or off; remembered answers
 * still come; the running print's 409 refusals come first; a running scan ends at its next line with 503 and
 * leaves nothing remembered; /api/health counts both. Without a fresh printer connection nothing is held. */
#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

static int gate[2];
static atomic_int hold_scan; /* set by the test, read by the workers */
static void scan_hook(void) { if (atomic_load(&hold_scan)) { char byte; assert(read(gate[0], &byte, 1) == 1); } }
static void sleep_ms(long ms) { struct timespec t = {ms / 1000, (ms % 1000) * 1000000L}; nanosleep(&t, NULL); }
static void idle(void) {
    for (int i = 0; i < 2000 && atomic_load(&active_layer_paths); i++) sleep_ms(5);
    assert(atomic_load(&active_layer_paths) == 0);
}

static char reply[256 * 1024];
static mqtt_client m;
static console_state console;
/* One request through handle_client over loopback TCP; returns the peer when a worker took it, else -1. */
static int send_request(const char *method, const char *target, const char *payload, int *peer) {
    int pair[2], listener = socket(AF_INET, SOCK_STREAM, 0); assert(listener >= 0);
    struct sockaddr_in address = {0}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0);
    socklen_t size = sizeof(address); assert(getsockname(listener, (struct sockaddr *)&address, &size) == 0);
    assert(listen(listener, 1) == 0); pair[1] = socket(AF_INET, SOCK_STREAM, 0); assert(pair[1] >= 0);
    assert(connect(pair[1], (struct sockaddr *)&address, size) == 0);
    pair[0] = accept(listener, NULL, NULL); assert(pair[0] >= 0); close(listener);
    char request[1400];
    int n = payload
        ? snprintf(request, sizeof(request), "%s %s HTTP/1.1\r\nHost: localhost\r\nX-CC2-Request: 1\r\nContent-Length: %zu\r\n\r\n%s",
                   method, target, strlen(payload), payload)
        : snprintf(request, sizeof(request), "%s %s HTTP/1.1\r\nHost: localhost\r\n\r\n", method, target);
    assert(n > 0 && (size_t)n < sizeof(request));
    *peer = pair[1];
    if (handle_client(pair[0], request, (size_t)n, "./web", &m, &console)) return 0;
    close(pair[0]);
    return -1;
}
static int read_reply(int peer) {
    size_t used = 0; ssize_t got;
    while (used + 1 < sizeof(reply) && (got = read(peer, reply + used, sizeof(reply) - 1 - used)) > 0) used += (size_t)got;
    close(peer); reply[used] = 0;
    assert(used > 0);
    return atoi(strchr(reply, ' ') + 1);
}
static int request(const char *method, const char *target, const char *payload) {
    int peer; send_request(method, target, payload, &peer);
    int code = read_reply(peer);
    idle();
    return code;
}

static char health[4096];
static long long stat_of(const char *key) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    health_response(pair[0], &m);
    ssize_t n = recv(pair[1], health, sizeof(health) - 1, 0); assert(n > 0); health[n] = 0;
    close(pair[0]); close(pair[1]);
    char needle[64]; snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(health, needle); assert(p);
    return strtoll(p + strlen(needle), NULL, 10);
}
#define HELD_TEXT "Printer is homing or starting a print; retry shortly"
/* code, scans added, bytes read added (-1: any), read holds added, text in the reply (NULL: any). */
#define EXPECT(method, target, payload, code_expected, scans_added, bytes_added, blocked_added, text) do { \
    long long s0 = stat_of("layer_path_scans"), b0 = stat_of("layer_path_bytes_read"), h0 = stat_of("gcode_read_blocked"); \
    int code = request(method, target, payload); \
    long long s1 = stat_of("layer_path_scans") - s0, b1 = stat_of("layer_path_bytes_read") - b0, h1 = stat_of("gcode_read_blocked") - h0; \
    if (code != (code_expected) || s1 != (scans_added) || ((bytes_added) >= 0 && b1 != (bytes_added)) || \
        h1 != (blocked_added) || ((text) && !strstr(reply, (text)))) { \
        fprintf(stderr, "line %d: %s %s -> %d (expected %d), scans +%lld (%d), bytes +%lld (%d), held +%lld (%d)\n%s\n", \
                __LINE__, method, target, code, (code_expected), s1, (scans_added), b1, (bytes_added), h1, (blocked_added), reply); \
        return 1; } } while (0)
#define LAYER(file, layer) "/api/gcode-files/layer-path?storage=internal&file=" file "&layer=" layer
#define PREVIEW(file, object) "/api/gcode-files/object-preview?storage=internal&file=" file "&object=" object
#define THUMB "/api/gcode-files/thumbnail"

static char dir[] = "/tmp/read-guard-XXXXXX";
static void write_file(const char *name, int layers, int thumbnail) {
    char path[512]; snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w"); assert(f);
    /* A 12-byte PNG signature as the embedded picture. */
    if (thumbnail) assert(fputs("; thumbnail begin 2x2 16\n; iVBORw0KGgoAAAAA\n; thumbnail end\n", f) >= 0);
    assert(fputs("M83\nEXCLUDE_OBJECT_DEFINE NAME=Part_0\n", f) >= 0);
    for (int l = 0; l < layers; l++) {
        assert(fprintf(f, ";LAYER_CHANGE\n;Z:%.1f\nG1 Z%.1f\nEXCLUDE_OBJECT_START NAME=Part_0\n;TYPE:Outer wall\n", 0.2 * (l + 1), 0.2 * (l + 1)) > 0);
        assert(fputs("G1 X20 Y20\nG1 X40 Y20 E1\nG1 X40 Y40 E1\nG1 X20 Y40 E1\nG1 X20 Y20 E1\nEXCLUDE_OBJECT_END NAME=Part_0\n", f) >= 0);
    }
    assert(!fclose(f));
}
static int preview_cached(void) {
    pthread_mutex_lock(&object_preview_lock); int valid = object_preview_cached.valid; pthread_mutex_unlock(&object_preview_lock);
    return valid;
}
static void fresh(int status) { m.connected = m.registered = m.have_machine_status = 1; m.machine_status = status; m.last_message = time(NULL); }
static void printing(const char *state, int layer) {
    fresh(2); snprintf(m.print_state, sizeof(m.print_state), "%s", state); m.current_layer = layer;
}
static void idle_printer(void) { fresh(1); snprintf(m.print_state, sizeof(m.print_state), "standby"); m.current_layer = 0; }
static void console_job(int busy, const char *command) {
    pthread_mutex_lock(&console.lock); console.busy = busy; snprintf(console.command, sizeof(console.command), "%s", command);
    pthread_mutex_unlock(&console.lock);
}

int main(void) {
    assert(mkdtemp(dir));
    gcode_internal_root = dir;
    write_file("print.gcode", 20, 1);
    write_file("other.gcode", 30, 1);
    write_file("third.gcode", 10, 0);
    assert(!pipe(gate));
    layer_path_scan_hook = scan_hook;
    object_preview_scan_hook = scan_hook;
    layer_path_rest_min_ms = 0; layer_path_rest_percent = 0;
    uds_init(&telemetry);
    mqtt_init(&m);
    memset(&console, 0, sizeof(console)); pthread_mutex_init(&console.lock, NULL);
    snprintf(m.filename, sizeof(m.filename), "print.gcode");
    m.have_total_layers = 1; m.total_layers = 20;

    /* ---- Each state, with no file involved. ---- */
    idle_printer(); assert(!gcode_read_blocked(&m, &console));                  /* idle */
    fresh(10); assert(gcode_read_blocked(&m, &console));                        /* Manual homing */
    fresh(5); assert(gcode_read_blocked(&m, &console));                         /* Auto leveling */
    fresh(7); assert(gcode_read_blocked(&m, &console));                         /* Resonance test */
    fresh(8); assert(gcode_read_blocked(&m, &console));                         /* Self check */
    for (int status = 0; status <= 17; status++)                               /* nothing else holds while idle */
        if (status != 2 && status != 5 && status != 7 && status != 8 && status != 10) {
            fresh(status); assert(!gcode_read_blocked(&m, &console));
        }
    printing("printing", 0); assert(gcode_read_blocked(&m, &console));          /* start phase */
    printing("paused", 0); assert(gcode_read_blocked(&m, &console));            /* paused before layer 1 */
    printing("printing", 1); assert(!gcode_read_blocked(&m, &console));         /* layer 1 reached */
    printing("printing", 57); assert(!gcode_read_blocked(&m, &console));
    printing("complete", 0); assert(!gcode_read_blocked(&m, &console));         /* status 2 but no print state */
    m.filename[0] = 0; printing("printing", 0); assert(gcode_read_blocked(&m, &console)); /* the file name plays no part */
    snprintf(m.filename, sizeof(m.filename), "print.gcode");
    /* The direct stream's layer wins, as in /api/printer: MQTT still 0, the stream at 3. */
    {
        int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair)); telemetry.fd = pair[0];
        const char *snap = "{\"id\":11,\"result\":{\"eventtime\":1,\"status\":{\"print_stats\":{\"state\":\"printing\",\"filename\":\"local/print.gcode\",\"info\":{\"current_layer\":3}}}}}";
        assert(uds_message(&telemetry, snap, strlen(snap)));
        printing("printing", 0); assert(!gcode_read_blocked(&m, &console));
        const char *back = "{\"method\":\"cc2_status\",\"params\":{\"eventtime\":2,\"status\":{\"print_stats\":{\"info\":{\"current_layer\":0}}}}}";
        assert(uds_message(&telemetry, back, strlen(back)));
        m.current_layer = 5; assert(gcode_read_blocked(&m, &console));
        close(pair[1]); uds_close(&telemetry); uds_init(&telemetry);
    }
    /* No printer connection, or status that is not fresh: nothing is held, files can be browsed. */
    fresh(10); m.connected = 0; assert(!gcode_read_blocked(&m, &console));
    fresh(10); m.registered = 0; assert(!gcode_read_blocked(&m, &console));
    fresh(10); m.have_machine_status = 0; assert(!gcode_read_blocked(&m, &console));
    fresh(10); m.last_message = time(NULL) - 16; assert(!gcode_read_blocked(&m, &console));
    printing("printing", 0); m.last_message = time(NULL) - 60; assert(!gcode_read_blocked(&m, &console));
    fresh(10); m.last_message = 0; assert(!gcode_read_blocked(&m, &console));
    assert(!gcode_read_blocked(NULL, NULL));
    /* This service's bed mesh calibration: while it homes and probes, not while it heats or soaks. */
    idle_printer();
    plates_cal.stage = CAL_HOMING; assert(gcode_read_blocked(&m, &console));
    plates_cal.stage = CAL_PROBING; assert(gcode_read_blocked(&m, &console));
    plates_cal.stage = CAL_HEATING; assert(!gcode_read_blocked(&m, &console));
    plates_cal.stage = CAL_SOAKING; assert(!gcode_read_blocked(&m, &console));
    plates_cal.stage = CAL_SAVING; assert(!gcode_read_blocked(&m, &console));
    plates_cal.stage = CAL_IDLE;
    /* The console's own job, also before MQTT reports it, and also without MQTT. */
    console_job(1, "G28"); assert(gcode_read_blocked(&m, &console));
    console_job(1, "g28 z"); assert(gcode_read_blocked(&m, &console));
    console_job(1, "M140 S60\nG1 X30 Y30 F12000\nPROBE SAMPLES=3\nG1 Z10 F600"); assert(gcode_read_blocked(&m, &console));
    console_job(1, "G180 S3"); assert(gcode_read_blocked(&m, &console));
    console_job(1, "BED_MESH_CALIBRATE PROFILE=default BED_TEMP=60"); assert(gcode_read_blocked(&m, &console));
    console_job(1, "PROBE_ACCURACY"); assert(gcode_read_blocked(&m, &console));
    console_job(1, "SCREWS_TILT_CALCULATE"); assert(gcode_read_blocked(&m, &console));
    console_job(1, "QUERY_PROBE"); assert(!gcode_read_blocked(&m, &console));
    console_job(1, "M140 S60"); assert(!gcode_read_blocked(&m, &console));
    console_job(1, "G1 X10 ; G28 in a comment"); assert(!gcode_read_blocked(&m, &console));
    console_job(1, "G280"); assert(!gcode_read_blocked(&m, &console));
    console_job(0, "G28"); assert(!gcode_read_blocked(&m, &console));          /* finished */
    m.connected = 0; console_job(1, "G28"); assert(gcode_read_blocked(&m, &console));
    console_job(0, ""); assert(!gcode_read_blocked(&m, NULL));
    idle_printer();

    /* ---- Idle: everything reads; the layer, the preview and the picture are remembered where they are. ---- */
    EXPECT("GET", LAYER("other.gcode", "4"), NULL, 200, 1, -1, 0, "\"layer\":4");
    EXPECT("GET", PREVIEW("other.gcode", "Part_0"), NULL, 200, 1, -1, 0, "\"object\":\"Part_0\"");
    EXPECT("POST", THUMB, "internal\nother.gcode", 200, 0, 0, 0, "image/png");
    assert(preview_cached());

    /* ---- Homing (touchscreen, Manual homing): no new read of any file; what is remembered still comes. ---- */
    fresh(10);
    EXPECT("GET", LAYER("other.gcode", "5"), NULL, 503, 0, 0, 1, HELD_TEXT);
    EXPECT("GET", LAYER("third.gcode", "1"), NULL, 503, 0, 0, 1, HELD_TEXT);
    EXPECT("GET", LAYER("other.gcode", "4"), NULL, 200, 0, 0, 0, "\"layer\":4");            /* remembered */
    EXPECT("GET", PREVIEW("third.gcode", "Part_0"), NULL, 503, 0, 0, 1, HELD_TEXT);
    EXPECT("GET", PREVIEW("other.gcode", "Part_0"), NULL, 200, 0, 0, 0, "\"object\":\"Part_0\""); /* remembered */
    assert(preview_cached());                                  /* a refused read of another file keeps it */
    EXPECT("POST", THUMB, "internal\nother.gcode", 503, 0, 0, 1, HELD_TEXT);
    EXPECT("POST", THUMB, "internal\nthird.gcode", 503, 0, 0, 1, HELD_TEXT);
    assert(stat_of("layer_path_busy") == 0);                    /* the hold is not the "busy" of a running scan */
    /* Answers that need no read keep their own codes. */
    EXPECT("GET", LAYER("missing.gcode", "1"), NULL, 404, 0, 0, 0, "G-code file unavailable");
    EXPECT("GET", "/api/gcode-files/layer-path?storage=internal&file=other.gcode&layer=0", NULL, 400, 0, 0, 0, NULL);
    /* Leveling, and the console's own G28 before MQTT reports it. */
    fresh(5);
    EXPECT("GET", LAYER("third.gcode", "2"), NULL, 503, 0, 0, 1, HELD_TEXT);
    idle_printer(); console_job(1, "G28");
    EXPECT("GET", LAYER("third.gcode", "2"), NULL, 503, 0, 0, 1, HELD_TEXT);
    EXPECT("POST", THUMB, "internal\nthird.gcode", 503, 0, 0, 1, HELD_TEXT);
    console_job(0, "");
    plates_cal.stage = CAL_PROBING;
    EXPECT("GET", PREVIEW("third.gcode", "Part_0"), NULL, 503, 0, 0, 1, HELD_TEXT);
    plates_cal.stage = CAL_IDLE;
    /* The hold is over: the same requests read again. */
    EXPECT("GET", LAYER("third.gcode", "2"), NULL, 200, 1, -1, 0, "\"layer\":2");
    EXPECT("POST", THUMB, "internal\nthird.gcode", 404, 0, 0, 0, "No embedded thumbnail");

    /* ---- Start phase: every file waits, the running print's 409 refusals come first, as before. ---- */
    EXPECT("GET", PREVIEW("other.gcode", "Part_0"), NULL, 200, 0, 0, 0, NULL);       /* other.gcode is still remembered */
    printing("printing", 0);
    long long rejected = stat_of("layer_path_rejected");
    /* The guard asks for the file's layer count: the scan of the running print's file is withheld, counted once. */
    EXPECT("GET", LAYER("print.gcode", "2"), NULL, 409, 0, 0, 1, "has not reached");
    EXPECT("GET", LAYER("print.gcode", "21"), NULL, 409, 0, 0, 0, "beyond the running print");
    EXPECT("GET", PREVIEW("print.gcode", "Part_0"), NULL, 409, 0, 0, 0, "waits for the print start");
    assert(stat_of("layer_path_rejected") == rejected + 3);
    EXPECT("GET", LAYER("print.gcode", "1"), NULL, 503, 0, 0, 1, HELD_TEXT);   /* layer 1 of the print: no read now */
    EXPECT("GET", LAYER("other.gcode", "9"), NULL, 503, 0, 0, 1, HELD_TEXT);   /* another file: no read either */
    EXPECT("GET", PREVIEW("third.gcode", "Part_0"), NULL, 503, 0, 0, 1, HELD_TEXT);
    EXPECT("GET", PREVIEW("other.gcode", "Part_0"), NULL, 200, 0, 0, 0, NULL);       /* remembered */
    EXPECT("GET", LAYER("third.gcode", "2"), NULL, 200, 0, 0, 0, "\"layer\":2");     /* the remembered layer */
    EXPECT("POST", THUMB, "internal\nprint.gcode", 503, 0, 0, 1, HELD_TEXT);
    printing("printing", 1);
    EXPECT("GET", LAYER("print.gcode", "1"), NULL, 200, 1, -1, 0, "\"layer\":1");
    EXPECT("GET", LAYER("other.gcode", "9"), NULL, 200, 1, -1, 0, "\"layer\":9");
    EXPECT("POST", THUMB, "internal\nprint.gcode", 200, 0, 0, 0, "image/png");
    /* Without a fresh connection nothing waits. */
    printing("printing", 0); m.last_message = time(NULL) - 60;
    EXPECT("GET", LAYER("other.gcode", "10"), NULL, 200, 1, -1, 0, NULL);
    m.connected = 0;
    EXPECT("POST", THUMB, "internal\nother.gcode", 200, 0, 0, 0, "image/png");
    idle_printer();

    /* ---- A scan that runs when the hold comes on ends at its next line: 503, nothing remembered. ---- */
    long long stopped = stat_of("gcode_read_stopped");
    hold_scan = 1;
    int peer;
    assert(send_request("GET", LAYER("third.gcode", "7"), NULL, &peer) == 0);
    fresh(10); gcode_read_follow(&m, &console);                 /* the main loop's pass */
    assert(write(gate[1], "x", 1) == 1);
    assert(read_reply(peer) == 503 && strstr(reply, HELD_TEXT));
    hold_scan = 0; idle();
    assert(stat_of("gcode_read_stopped") == stopped + 1);
    idle_printer(); gcode_read_follow(&m, &console);
    assert(!atomic_load(&gcode_read_hold));
    EXPECT("GET", LAYER("third.gcode", "7"), NULL, 200, 1, -1, 0, "\"layer\":7"); /* read again: it was not remembered */
    /* The object preview the same way, here overtaken by a print start. */
    object_preview_forget();
    hold_scan = 1;
    assert(send_request("GET", PREVIEW("third.gcode", "Part_0"), NULL, &peer) == 0);
    printing("printing", 0); gcode_read_follow(&m, &console);
    assert(write(gate[1], "x", 1) == 1);
    assert(read_reply(peer) == 503 && strstr(reply, HELD_TEXT));
    hold_scan = 0; idle();
    assert(!preview_cached());
    assert(stat_of("gcode_read_stopped") == stopped + 2);
    printing("printing", 1); gcode_read_follow(&m, &console);
    EXPECT("GET", PREVIEW("third.gcode", "Part_0"), NULL, 200, 1, -1, 0, "\"object\":\"Part_0\"");
    assert(preview_cached());
    /* A scan that ends without the hold is not touched. */
    hold_scan = 1;
    assert(send_request("GET", LAYER("third.gcode", "8"), NULL, &peer) == 0);
    gcode_read_follow(&m, &console);
    assert(write(gate[1], "x", 1) == 1);
    assert(read_reply(peer) == 200);
    hold_scan = 0; idle();
    assert(stat_of("gcode_read_stopped") == stopped + 2);

    close(gate[0]); close(gate[1]);
    const char *names[] = {"print.gcode", "other.gcode", "third.gcode"};
    for (size_t i = 0; i < 3; i++) { char path[512]; snprintf(path, sizeof(path), "%s/%s", dir, names[i]); unlink(path); }
    rmdir(dir);
    puts("PASS read hold: homing, leveling, start phase, calibration and the console's homing hold every new G-code read (layer path, object preview, picture), idle and no connection do not; remembered answers and the 409 refusals stay; a running scan ends with 503 and leaves nothing remembered; counted in /api/health");
    return 0;
}
