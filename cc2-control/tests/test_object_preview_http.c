/* GET /api/gcode-files/object-preview through the service's own handler: the file is read once and every
 * object answered from memory afterwards; the layer path's worker slot, pause and /api/health counters are
 * shared in both directions; the 50 MB limit, out of memory, the start protection of the running print, and
 * the memory going after 30 minutes, with a changed file, at a print start and at a print end. */
#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

static int gate[2];
static atomic_int hold_preview, hold_layer; /* set by the test, read by the workers */
static void preview_hook(void) { if (atomic_load(&hold_preview)) { char byte; assert(read(gate[0], &byte, 1) == 1); } }
static void layer_hook(void) { if (atomic_load(&hold_layer)) { char byte; assert(read(gate[0], &byte, 1) == 1); } }
static void sleep_ms(long ms) { struct timespec t = {ms / 1000, (ms % 1000) * 1000000L}; nanosleep(&t, NULL); }
static void idle(void) {
    for (int i = 0; i < 2000 && atomic_load(&active_layer_paths); i++) sleep_ms(5);
    assert(atomic_load(&active_layer_paths) == 0);
}

static char reply[256 * 1024];
static mqtt_client m;
/* Sends one request to handle_client; returns the peer when a worker took it, else the status. */
static int send_request(const char *target, int *peer) {
    /* A loopback TCP connection, like a browser on the printer's own address (see test_layer_path_guard.c). */
    int pair[2], listener = socket(AF_INET, SOCK_STREAM, 0); assert(listener >= 0);
    struct sockaddr_in address = {0}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(bind(listener, (struct sockaddr *)&address, sizeof(address)) == 0);
    socklen_t size = sizeof(address); assert(getsockname(listener, (struct sockaddr *)&address, &size) == 0);
    assert(listen(listener, 1) == 0); pair[1] = socket(AF_INET, SOCK_STREAM, 0); assert(pair[1] >= 0);
    assert(connect(pair[1], (struct sockaddr *)&address, size) == 0);
    pair[0] = accept(listener, NULL, NULL); assert(pair[0] >= 0); close(listener);
    char request[1200];
    int n = snprintf(request, sizeof(request), "GET %s HTTP/1.1\r\nHost: localhost\r\n\r\n", target);
    assert(n > 0 && (size_t)n < sizeof(request));
    if (handle_client(pair[0], request, (size_t)n, "./web", &m, NULL)) { *peer = pair[1]; return 0; }
    close(pair[0]);
    *peer = pair[1];
    return -1;
}
static int read_reply(int peer) {
    size_t used = 0; ssize_t got;
    while (used + 1 < sizeof(reply) && (got = read(peer, reply + used, sizeof(reply) - 1 - used)) > 0) used += (size_t)got;
    close(peer); reply[used] = 0;
    assert(used > 0);
    return atoi(strchr(reply, ' ') + 1);
}
static int get(const char *target) {
    int peer; send_request(target, &peer);
    int code = read_reply(peer);
    idle();
    return code;
}
static const char *body(void) { const char *b = strstr(reply, "\r\n\r\n"); assert(b); return b + 4; }

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
static long long scans(void) { return stat_of("layer_path_scans"); }
static int has_text(const char *text) { return !text || strstr(reply, text) != NULL; }
#define EXPECT(code_expected, scans_added, target, text) do { long long before = scans(); int code = get(target); \
    if (code != (code_expected) || scans() - before != (scans_added) || !has_text(text)) { \
        fprintf(stderr, "line %d: %s -> %d (expected %d), scans +%lld (expected %d)\n%s\n", __LINE__, target, code, \
                (code_expected), scans() - before, (scans_added), reply); return 1; } } while (0)

static char dir[] = "/tmp/object-preview-XXXXXX";
static void write_file(const char *name, int objects, int layers) {
    char path[512]; snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w"); assert(f);
    assert(fputs("M83\n", f) >= 0);
    for (int o = 0; o < objects; o++) assert(fprintf(f, "EXCLUDE_OBJECT_DEFINE NAME=Part_%d\n", o) > 0);
    for (int l = 0; l < layers; l++) {
        assert(fprintf(f, ";LAYER_CHANGE\n;Z:%.1f\nG1 Z%.1f\n", 0.2 * (l + 1), 0.2 * (l + 1)) > 0);
        for (int o = 0; o < objects; o++) {
            int x = 20 + 40 * o;
            assert(fprintf(f, "EXCLUDE_OBJECT_START NAME=Part_%d\n;TYPE:Outer wall\nG1 X%d Y20\n"
                              "G1 X%d Y20 E1\nG1 X%d Y40 E1\nG1 X%d Y40 E1\nG1 X%d Y20 E1\n"
                              ";TYPE:Sparse infill\nG1 X%d Y30 E1\nEXCLUDE_OBJECT_END NAME=Part_%d\n",
                           o, x, x + 20, x + 20, x, x, x + 20, o) > 0);
        }
    }
    assert(!fclose(f));
}
static long long file_size(const char *name) {
    char path[512]; snprintf(path, sizeof(path), "%s/%s", dir, name);
    struct stat st; assert(!stat(path, &st)); return (long long)st.st_size;
}
static int cached(void) {
    pthread_mutex_lock(&object_preview_lock); int valid = object_preview_cached.valid; pthread_mutex_unlock(&object_preview_lock);
    return valid;
}

/* MQTT messages through the real client, as in test_print_start_reset.c. */
static int mqtt_peer = -1;
static void mq(const char *json) {
    unsigned char packet[2200];
    const char topic[] = "elegoo/test/api_status";
    size_t topic_length = sizeof(topic) - 1, length = strlen(json), n = 2 + topic_length + length, h = 0, rest = n;
    packet[h++] = 0x30;
    do { unsigned char byte = rest & 127; rest >>= 7; if (rest) byte |= 128; packet[h++] = byte; } while (rest);
    packet[h++] = 0; packet[h++] = (unsigned char)topic_length;
    memcpy(packet + h, topic, topic_length); h += topic_length;
    memcpy(packet + h, json, length); h += length;
    assert(send(mqtt_peer, packet, h, 0) == (ssize_t)h);
    assert(mqtt_process(&m) == 0);
}
static double event_time = 1;
static void ud(const char *state) {
    char json[400];
    int n = snprintf(json, sizeof(json), "{\"method\":\"cc2_status\",\"params\":{\"eventtime\":%.0f,\"status\":{\"print_stats\":{\"state\":\"%s\"}}}}",
                     event_time++, state);
    assert(n > 0 && (size_t)n < sizeof(json));
    assert(uds_message(&telemetry, json, strlen(json)));
}

#define Q(file, object) "/api/gcode-files/object-preview?storage=internal&file=" file "&object=" object
#define L(file, layer) "/api/gcode-files/layer-path?storage=internal&file=" file "&layer=" layer

int main(void) {
    assert(mkdtemp(dir));
    gcode_internal_root = dir;
    write_file("parts.gcode", 3, 30);
    write_file("other.gcode", 2, 10);
    write_file("plain.gcode", 0, 5);
    assert(!pipe(gate));
    object_preview_scan_hook = preview_hook;
    layer_path_scan_hook = layer_hook;
    layer_path_rest_min_ms = 0; layer_path_rest_percent = 0;
    uds_init(&telemetry);
    mqtt_init(&m);
    { int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair)); m.fd = pair[0]; mqtt_peer = pair[1]; m.connected = 1; }
    m.registered = 1; m.last_message = time(NULL);
    m.have_machine_status = 1; m.machine_status = 1;

    /* ---- One read for the whole file, every object from memory afterwards. ---- */
    EXPECT(200, 1, Q("parts.gcode", "Part_1"), "\"object\":\"Part_1\",\"layers\":30,\"step\":3,\"z_max\":6,\"bounds\":[60,20,80,40]");
    assert(strstr(body(), "\"rings\":[{\"z\":0.2,\"paths\":[[60,20,80,20,80,40,60,40,60,20]]},{\"z\":0.8,"));
    assert(strstr(body(), "\"truncated\":false,\"objects\":[\"Part_0\",\"Part_1\",\"Part_2\"]}"));
    assert(stat_of("layer_path_bytes_read") == file_size("parts.gcode"));
    EXPECT(200, 0, Q("parts.gcode", "Part_1"), "\"object\":\"Part_1\"");
    EXPECT(200, 0, Q("parts.gcode", "Part_2"), "\"bounds\":[100,20,120,40]");
    /* The name as Klipper reports it, in upper case: the same object, answered with the file's own name. */
    EXPECT(200, 0, Q("parts.gcode", "PART_2"), "\"object\":\"Part_2\",");
    EXPECT(404, 0, Q("parts.gcode", "Part_9"), "Object not found");
    assert(stat_of("layer_path_bytes_read") == file_size("parts.gcode"));
    /* Bad requests and missing files. */
    EXPECT(400, 0, "/api/gcode-files/object-preview?storage=internal&file=parts.gcode", "Invalid object");
    EXPECT(400, 0, "/api/gcode-files/object-preview?file=../x.gcode&object=A", "Invalid protected G-code path");
    EXPECT(404, 0, Q("missing.gcode", "A"), "G-code file unavailable");
    /* A file without object marks: one read, then from memory as well. */
    EXPECT(404, 1, Q("plain.gcode", "A"), "No objects in the G-code file");
    EXPECT(404, 0, Q("plain.gcode", "B"), "No objects in the G-code file");

    /* ---- Another file replaces the remembered one; a changed file is read again. ---- */
    EXPECT(200, 1, Q("parts.gcode", "Part_0"), "\"object\":\"Part_0\"");
    EXPECT(200, 1, Q("other.gcode", "Part_1"), "\"objects\":[\"Part_0\",\"Part_1\"]");
    EXPECT(200, 1, Q("parts.gcode", "Part_0"), NULL);
    {
        char path[512]; snprintf(path, sizeof(path), "%s/parts.gcode", dir);
        FILE *f = fopen(path, "a"); assert(f && fputs("G1 X1 Y1\n", f) >= 0 && !fclose(f));
    }
    EXPECT(200, 1, Q("parts.gcode", "Part_0"), NULL);
    EXPECT(200, 0, Q("parts.gcode", "Part_2"), NULL);

    /* ---- The worker slot is shared: busy in both directions. ---- */
    hold_preview = 1;
    int peer;
    assert(send_request(Q("other.gcode", "Part_0"), &peer) == 0);   /* a preview scan is held */
    long long busy = stat_of("layer_path_busy");
    { int p2; assert(send_request(L("parts.gcode", "3"), &p2) < 0 && read_reply(p2) == 503 && strstr(reply, "Layer path busy")); }
    { int p2; assert(send_request(Q("plain.gcode", "A"), &p2) < 0 && read_reply(p2) == 503 && strstr(reply, "Object preview busy; retry shortly")); }
    assert(stat_of("layer_path_busy") == busy + 2);
    assert(write(gate[1], "x", 1) == 1);
    assert(read_reply(peer) == 200 && strstr(reply, "\"file\":\"other.gcode\""));
    hold_preview = 0; idle();
    hold_layer = 1;
    assert(send_request(L("parts.gcode", "3"), &peer) == 0);         /* a layer-path scan is held */
    { int p2; assert(send_request(Q("parts.gcode", "Part_0"), &p2) < 0 && read_reply(p2) == 503 && strstr(reply, "Object preview busy")); }
    /* ... while the remembered file still answers at once. */
    { int p2; assert(send_request(Q("other.gcode", "Part_1"), &p2) < 0 && read_reply(p2) == 200); }
    assert(write(gate[1], "x", 1) == 1);
    assert(read_reply(peer) == 200);
    hold_layer = 0; idle();

    /* A scan that a drop overtakes (a print start while it reads) answers, but is not remembered. */
    hold_preview = 1;
    object_preview_forget();
    assert(send_request(Q("parts.gcode", "Part_0"), &peer) == 0);
    object_preview_forget();
    assert(write(gate[1], "x", 1) == 1);
    assert(read_reply(peer) == 200 && strstr(reply, "\"object\":\"Part_0\""));
    hold_preview = 0; idle();
    assert(!cached());
    EXPECT(200, 1, Q("parts.gcode", "Part_0"), NULL);
    assert(cached());
    EXPECT(200, 1, Q("other.gcode", "Part_0"), NULL);         /* as before this block: another file is remembered */

    /* ---- The pause after a scan is shared too; remembered answers still come. ---- */
    layer_path_rest_min_ms = 400;
    EXPECT(200, 1, Q("parts.gcode", "Part_1"), NULL);                     /* a preview scan sets the pause */
    EXPECT(503, 0, L("parts.gcode", "5"), "Layer path busy");
    EXPECT(503, 0, Q("plain.gcode", "A"), "Object preview busy");
    EXPECT(200, 0, Q("parts.gcode", "Part_2"), NULL);
    sleep_ms(450);
    EXPECT(200, 1, L("parts.gcode", "5"), NULL);                          /* a layer-path scan sets it */
    EXPECT(503, 0, Q("plain.gcode", "A"), "Object preview busy");
    EXPECT(200, 0, Q("parts.gcode", "Part_0"), NULL);
    layer_path_rest_min_ms = 0; sleep_ms(450);

    /* ---- 50 MB: a larger file is refused unread; exactly 50 MB is read. ---- */
    {
        char path[512]; snprintf(path, sizeof(path), "%s/huge.gcode", dir);
        int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644); assert(fd >= 0);
        assert(!ftruncate(fd, (off_t)OBJECT_PREVIEW_BYTES_MAX + 1)); close(fd);
        EXPECT(413, 0, Q("huge.gcode", "A"), "Object preview file too large");
        fd = open(path, O_WRONLY); assert(fd >= 0 && !ftruncate(fd, (off_t)OBJECT_PREVIEW_BYTES_MAX)); close(fd);
        EXPECT(404, 1, Q("huge.gcode", "A"), "No objects in the G-code file");
        unlink(path);
    }

    /* ---- Out of memory: 503, nothing remembered, the next request reads again. ---- */
    object_preview_forget();
    object_preview_alloc_budget = 3;
    EXPECT(503, 1, Q("parts.gcode", "Part_0"), "Out of memory");
    object_preview_alloc_budget = -1;
    assert(!cached());
    EXPECT(200, 1, Q("parts.gcode", "Part_0"), NULL);

    /* ---- The memory goes 30 minutes after the last request, also without one. ---- */
    assert(object_preview_keep_s == 30 * 60 && cached());
    assert(object_preview_cached.result.bytes > 0);
    object_preview_follow(&m, &telemetry);
    assert(cached());
    pthread_mutex_lock(&object_preview_lock);
    object_preview_cached.last_request.tv_sec -= 30 * 60 - 2; /* the last request was 29:58 ago */
    pthread_mutex_unlock(&object_preview_lock);
    object_preview_follow(&m, &telemetry);
    assert(cached());
    sleep_ms(2100);
    object_preview_follow(&m, &telemetry);                    /* the main loop's pass, no request */
    assert(!cached() && object_preview_cached.result.objects == NULL && object_preview_cached.result.bytes == 0);
    /* ... and at the next request when no pass came in between. */
    EXPECT(200, 1, Q("parts.gcode", "Part_0"), NULL);
    pthread_mutex_lock(&object_preview_lock);
    object_preview_cached.last_request.tv_sec -= 30 * 60;
    pthread_mutex_unlock(&object_preview_lock);
    EXPECT(200, 1, Q("parts.gcode", "Part_0"), NULL);
    /* Every request renews it, also one answered from memory. */
    pthread_mutex_lock(&object_preview_lock);
    object_preview_cached.last_request.tv_sec -= 30 * 60 - 2;
    pthread_mutex_unlock(&object_preview_lock);
    EXPECT(200, 0, Q("parts.gcode", "Part_1"), NULL);
    sleep_ms(2100);
    object_preview_follow(&m, &telemetry);
    assert(cached());

    /* ---- A print start drops it (MQTT: a new job uuid; UDS: idle to printing), and so does a print end. ---- */
    mq("{\"machine_status\":{\"status\":2},\"print_status\":{\"filename\":\"other.gcode\",\"state\":\"printing\",\"uuid\":\"j1\",\"current_layer\":4}}");
    object_preview_follow(&m, &telemetry);
    assert(cached());                                             /* the first uuid only tells which print runs */
    mq("{\"print_status\":{\"state\":\"paused\",\"uuid\":\"j1\"}}");
    object_preview_follow(&m, &telemetry);
    mq("{\"print_status\":{\"state\":\"printing\",\"uuid\":\"j1\"}}");
    object_preview_follow(&m, &telemetry);
    assert(cached());                                             /* pause and resume keep it */
    mq("{\"print_status\":{\"state\":\"printing\",\"uuid\":\"j2\"}}");
    object_preview_follow(&m, &telemetry);
    assert(!cached());                                            /* a new print */
    /* ... but what is remembered for the file of the new print stays: after layer 1 it answers without a second read. */
    mq("{\"print_status\":{\"state\":\"printing\",\"uuid\":\"j2\",\"current_layer\":3}}");
    EXPECT(200, 1, Q("other.gcode", "Part_0"), NULL);
    mq("{\"print_status\":{\"filename\":\"other.gcode\",\"state\":\"printing\",\"uuid\":\"j3\"}}");
    object_preview_follow(&m, &telemetry);
    assert(cached());
    EXPECT(409, 0, Q("other.gcode", "Part_1"), "waits for the print start"); /* the start protection is unchanged */
    mq("{\"print_status\":{\"state\":\"printing\",\"uuid\":\"j3\",\"current_layer\":1}}");
    EXPECT(200, 0, Q("other.gcode", "Part_1"), NULL);
    /* A read of that file which the print start overtakes is remembered as well ... */
    mq("{\"machine_status\":{\"status\":1},\"print_status\":{\"state\":\"complete\",\"uuid\":\"j3\"}}");
    object_preview_follow(&m, &telemetry);
    assert(!cached());
    hold_preview = 1;
    assert(send_request(Q("other.gcode", "Part_0"), &peer) == 0);
    mq("{\"machine_status\":{\"status\":2},\"print_status\":{\"filename\":\"other.gcode\",\"state\":\"printing\",\"uuid\":\"j4\"}}");
    object_preview_follow(&m, &telemetry);
    assert(write(gate[1], "x", 1) == 1);
    assert(read_reply(peer) == 200);
    hold_preview = 0; idle();
    assert(cached());
    /* ... a read of another file is not, as before. */
    mq("{\"machine_status\":{\"status\":1},\"print_status\":{\"state\":\"complete\",\"uuid\":\"j4\"}}");
    object_preview_follow(&m, &telemetry);
    assert(!cached());
    hold_preview = 1;
    assert(send_request(Q("parts.gcode", "Part_0"), &peer) == 0);
    mq("{\"machine_status\":{\"status\":2},\"print_status\":{\"filename\":\"other.gcode\",\"state\":\"printing\",\"uuid\":\"j5\"}}");
    object_preview_follow(&m, &telemetry);
    assert(write(gate[1], "x", 1) == 1);
    assert(read_reply(peer) == 200);
    hold_preview = 0; idle();
    assert(!cached());
    mq("{\"print_status\":{\"state\":\"printing\",\"uuid\":\"j5\",\"current_layer\":1}}"); /* before it, the read hold */
    EXPECT(200, 1, Q("parts.gcode", "Part_0"), NULL);
    /* In the service the main loop's next pass also turns on the read hold (the print has not reached layer 1):
     * such a read then ends at its next line with 503, for the print's file as for another, and is not remembered. */
    mq("{\"machine_status\":{\"status\":1},\"print_status\":{\"state\":\"complete\",\"uuid\":\"j5\"}}");
    object_preview_follow(&m, &telemetry);
    gcode_read_follow(&m, NULL);
    assert(!cached());
    for (int same = 1; same >= 0; same--) {
        long long stopped = stat_of("gcode_read_stopped");
        hold_preview = 1;
        assert(send_request(same ? Q("other.gcode", "Part_0") : Q("parts.gcode", "Part_0"), &peer) == 0);
        mq(same ? "{\"machine_status\":{\"status\":2},\"print_status\":{\"filename\":\"other.gcode\",\"state\":\"printing\",\"uuid\":\"j6\"}}"
                : "{\"machine_status\":{\"status\":2},\"print_status\":{\"filename\":\"other.gcode\",\"state\":\"printing\",\"uuid\":\"j7\"}}");
        object_preview_follow(&m, &telemetry);
        gcode_read_follow(&m, NULL);
        assert(write(gate[1], "x", 1) == 1);
        assert(read_reply(peer) == 503 && strstr(reply, "Printer is homing or starting a print; retry shortly"));
        hold_preview = 0; idle();
        assert(!cached() && stat_of("gcode_read_stopped") == stopped + 1);
        mq(same ? "{\"machine_status\":{\"status\":1},\"print_status\":{\"state\":\"complete\",\"uuid\":\"j6\"}}"
                : "{\"machine_status\":{\"status\":1},\"print_status\":{\"state\":\"complete\",\"uuid\":\"j7\"}}");
        object_preview_follow(&m, &telemetry);
        gcode_read_follow(&m, NULL);
    }
    mq("{\"machine_status\":{\"status\":2},\"print_status\":{\"filename\":\"other.gcode\",\"state\":\"printing\",\"uuid\":\"j2\",\"current_layer\":1}}");
    object_preview_follow(&m, &telemetry);
    EXPECT(200, 1, Q("parts.gcode", "Part_0"), NULL);
    mq("{\"machine_status\":{\"status\":1},\"print_status\":{\"state\":\"complete\",\"uuid\":\"j2\"}}");
    object_preview_follow(&m, &telemetry);
    assert(!cached());                                            /* the print ended */
    EXPECT(200, 1, Q("parts.gcode", "Part_0"), NULL);
    {
        int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair)); telemetry.fd = pair[0];
        ud("standby");
        object_preview_follow(&m, &telemetry);
        assert(cached());
        ud("printing");                                            /* the direct stream tells a new print */
        object_preview_follow(&m, &telemetry);
        assert(!cached());
        EXPECT(200, 1, Q("parts.gcode", "Part_0"), NULL);
        ud("paused"); object_preview_follow(&m, &telemetry);
        ud("printing"); object_preview_follow(&m, &telemetry);
        assert(cached());
        ud("cancelled");
        object_preview_follow(&m, &telemetry);
        assert(!cached());
        close(pair[1]); uds_close(&telemetry); uds_init(&telemetry);
    }

    /* ---- Start protection: the running print's file waits until layer 1 is seen; no read. ---- */
    m.machine_status = 2; m.last_message = time(NULL);
    snprintf(m.filename, sizeof(m.filename), "local/parts.gcode");
    snprintf(m.print_state, sizeof(m.print_state), "printing");
    m.current_layer = 0;
    long long rejected = stat_of("layer_path_rejected");
    EXPECT(409, 0, Q("parts.gcode", "Part_0"), "Object preview waits for the print start");
    assert(stat_of("layer_path_rejected") == rejected + 1);
    /* Other files are not refused, but wait until layer 1 like every read (the read hold). */
    EXPECT(503, 0, Q("other.gcode", "Part_0"), "Printer is homing or starting a print; retry shortly");
    assert(stat_of("layer_path_rejected") == rejected + 1);
    m.current_layer = 1;
    EXPECT(200, 1, Q("parts.gcode", "Part_0"), NULL);
    m.current_layer = 0;
    EXPECT(409, 0, Q("parts.gcode", "Part_1"), "waits for the print start"); /* also when it is remembered */
    snprintf(m.print_state, sizeof(m.print_state), "complete"); m.machine_status = 1;
    EXPECT(200, 0, Q("parts.gcode", "Part_1"), NULL);

    close(gate[0]); close(gate[1]);
    const char *names[] = {"parts.gcode", "other.gcode", "plain.gcode"};
    for (size_t i = 0; i < 3; i++) { char path[512]; snprintf(path, sizeof(path), "%s/%s", dir, names[i]); unlink(path); }
    rmdir(dir);
    puts("PASS object preview: one read per file, answers from memory, shared slot, pause and counters with the layer path, 50 MB, out of memory, start protection, memory dropped after 30 minutes, on a changed file, the start of a print of another file and a print end, kept over the start of a print of the same file, a read that the read hold overtakes ends with 503");
    return 0;
}
