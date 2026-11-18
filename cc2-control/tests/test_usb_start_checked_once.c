#define main cc2_main_original
#define mqtt_start_print mock_mqtt_start_print
#include "../src/main.c"
#undef main
#undef mqtt_start_print
#include <assert.h>

/* A USB start that the print dialog inspected first takes its leveling and tool decision from that
 * cached inspection, so a refused start copies nothing and the file is read once; the copy scans
 * along with the same line logic and must reach the same decision, or nothing is printed. */
static int failures;
static void expect(int ok, const char *what) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}
static int starts;
int mock_mqtt_start_print(mqtt_client *c, const char *media, const char *filename, const int *tools,
                          const int *trays, size_t count, char layout, int force, int timelapse) {
    (void)c; (void)media; (void)filename; (void)tools; (void)trays; (void)count; (void)layout; (void)force; (void)timelapse;
    starts++; return 0;
}
static int fake_uds(const char *q, char **r, size_t *l) {
    (void)q; static const char ok[] = "{\"id\":1001,\"result\":{}}"; *r = strdup(ok); *l = strlen(ok); return 0;
}
static void write_bytes(const char *path, const char *bytes, size_t length) {
    FILE *file = fopen(path, "wb"); assert(file); assert(fwrite(bytes, 1, length, file) == length); assert(!fclose(file));
}
static int answer(int peer, char *response, size_t cap) {
    size_t used = 0; ssize_t n;
    { struct timeval rcv_limit = {30, 0}; /* never wait forever: a missing answer fails the test */
      setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &rcv_limit, sizeof(rcv_limit)); }
    while (used + 1 < cap && (n = recv(peer, response + used, cap - 1 - used, 0)) > 0) used += (size_t)n;
    response[used] = 0; close(peer);
    return used ? atoi(strchr(response, ' ') + 1) : -1;
}
static int copies(void) {
    DIR *dir = opendir("gcode"); assert(dir);
    int count = 0; struct dirent *entry;
    while ((entry = readdir(dir))) if (!strncmp(entry->d_name, GCODE_USB_IMPORT_PREFIX, 8)) count++;
    closedir(dir); return count;
}
static void inspect(const char *file) {
    char body[256]; snprintf(body, sizeof(body), "usb\n%s", file);
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    assert(analysis_start(pair[0], 0, body, strlen(body)));
    char response[4096]; assert(answer(pair[1], response, sizeof(response)) == 200);
}
static int usb_start_file(mqtt_client *mqtt, const char *file, const char *mapping, const char *leveling,
                          char *response, size_t cap) {
    char body[256]; snprintf(body, sizeof(body), "usb\n%s\n%s\nA\n%s\n0", file, mapping, leveling);
    /* The printer has reported its Canvas trays (a start with a mapping is judged with them) and answers each fresh
     * report the start asks for; the main loop ticks until the start is answered, at most 3 s. */
    mqtt->last_message = time(NULL); mqtt->canvas_revision++; mqtt->canvas_discovery_complete = 1;
    mqtt->canvas_snapshot_len = (size_t)snprintf(mqtt->canvas_snapshot, sizeof(mqtt->canvas_snapshot),
        "{\"id\":2005,\"result\":{\"canvas_info\":{\"canvas_list\":[{\"canvas_id\":0,\"connected\":1,\"tray_list\":["
        "{\"tray_id\":0,\"status\":1,\"filament_type\":\"PLA\"},{\"tray_id\":1,\"status\":1,\"filament_type\":\"PLA\"}]}]}}}");
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    assert(usb_start_begin(pair[0], mqtt, body, strlen(body)));
    for (int i = 0; i < 5000 && !atomic_load(&usb_start_done); ++i) nanosleep(&(struct timespec){0, 1000000}, NULL);
    for (int i = 0; i < 3000 && usb_start_active(); ++i) {
        if (mqtt->canvas_refresh_due) { mqtt->canvas_refresh_due = 0; mqtt->canvas_revision++; mqtt->last_message = time(NULL); }
        usb_start_tick(mqtt); if (usb_start_active()) nanosleep(&(struct timespec){0, 1000000}, NULL);
    }
    assert(!usb_start_active());
    return answer(pair[1], response, cap);
}

/* Old decision (the file scans) against the scan along the copy, fed in pieces of `chunk` bytes. */
static int same_decision(const char *bytes, size_t length, size_t chunk) {
    write_bytes("usb/same.gcode", bytes, length);
    int adaptive = 0, tools[GCODE_TOOLS_MAX]; size_t count = 0;
    assert(!gcode_has_adaptive_mesh("usb", "same.gcode", &adaptive));
    assert(!gcode_detect_tools("usb", "same.gcode", tools, &count, NULL));
    static gcode_stream_scan scan; memset(&scan, 0, sizeof(scan));
    for (size_t at = 0; at < length; at += chunk) gcode_stream_scan_feed(&scan, bytes + at, length - at < chunk ? length - at : chunk);
    gcode_stream_scan_finish(&scan);
    gcode_scan_expect expect = {adaptive, {0}, count};
    memcpy(expect.tools, tools, count * sizeof(tools[0]));
    return gcode_stream_scan_matches(&scan, &expect);
}
static unsigned long seed = 12345;
static unsigned next_random(void) { seed = seed * 6364136223846793005UL + 1442695040888963407UL; return (unsigned)(seed >> 33); }

/* A test that waits for something that never comes ends red with a message instead of hanging the suite. */
static void test_too_long(int signal_number) {
    (void)signal_number;
    static const char text[] = "FAIL: the test ran longer than 300 s (an answer that never came?)\n";
    if (write(2, text, sizeof(text) - 1) < 0) {}
    _exit(1);
}
int main(void) {
    signal(SIGALRM, test_too_long); alarm(300);
    char dir[] = "usb-once-XXXXXX"; /* the harnesses share one working directory */
    assert(mkdtemp(dir) && !chdir(dir));
    assert(!mkdir("gcode", 0700) && !mkdir("usb", 0700));
    FILE *file = fopen("autosave.cfg", "w"); assert(file);
    fputs("[bed_mesh default]\nx_count = 11\ny_count = 11\npoints =\n  0, 0\n", file); fclose(file);
    gcode_internal_root = "gcode"; gcode_usb_root = "usb"; printer_autosave_path = "autosave.cfg";
    canvas_color_table_uds = fake_uds;
    mqtt_client mqtt = {0};
    mqtt.connected = mqtt.registered = 1; mqtt.have_machine_status = 1; mqtt.machine_status = 1;
    char response[1024];

    /* --- The scan along the copy decides exactly as the file scans, line cuts included. --- */
    static char big[3 * 65536];
    /* A mesh command that the 4096-byte cut splits: the file scan does not see it, neither may the copy. */
    memset(big, 'x', 4090); memcpy(big + 4090, "BED_MESH_CALIBRATE ADAPTIVE=1\n", 30);
    for (size_t chunk = 1; chunk <= 65536; chunk *= 7)
        expect(same_decision(big, 4120, chunk), "mesh split at the 4095-byte cut");
    /* A mesh command whose last byte is the 4095th of its line: whole for fgets, so seen. */
    memset(big, 'z', 4095 - 29); memcpy(big + 4095 - 29, "BED_MESH_CALIBRATE ADAPTIVE=1", 29); memcpy(big + 4095, "1\n", 2);
    for (size_t chunk = 1; chunk <= 65536; chunk *= 7)
        expect(same_decision(big, 4097, chunk), "mesh command ending exactly at the 4095-byte cut");
    /* The same command starting right after a full 4094-byte line is seen. */
    memset(big, 'y', 4094); big[4094] = '\n'; memcpy(big + 4095, "BED_MESH_CALIBRATE FROM_SLICER=1\r\n", 34);
    for (size_t chunk = 1; chunk <= 65536; chunk *= 7)
        expect(same_decision(big, 4129, chunk), "mesh after a full line, CRLF");
    /* A tool change that the 2048-byte cut lands on, a tool in a comment, a NUL byte, no final newline. */
    memset(big, ' ', 2046); memcpy(big + 2046, "T3 \nG1 X1 ; T2\nT1\0T4\nT5*", 25);
    for (size_t chunk = 1; chunk <= 65536; chunk *= 5) expect(same_decision(big, 2071, chunk), "tool marks at the 2047-byte cut, comment, NUL, no final newline");
    /* 300 random files: random line lengths up to 6000, CRLF, tool marks and mesh lines anywhere. */
    static const char *const pieces[] = {"G1 X1 Y2 E0.1\n", "T0\n", "T1\r\n", " T2 ", "t3\n", "; T7 in a comment\n",
                                         "BED_MESH_CALIBRATE MESH_MIN=1,1 MESH_MAX=2,2\n", "bed_mesh_calibrate adaptive=1 ;x\n",
                                         "BED_MESH_CALIBRATE ; ADAPTIVE=1\n", "T12*3\n", "M104 S200\r\n"};
    int equal = 1;
    for (int f = 0; f < 300 && equal; f++) {
        size_t used = 0;
        while (used < sizeof(big) - 7000) {
            unsigned pick = next_random() % 14;
            if (pick < 11) { size_t l = strlen(pieces[pick]); memcpy(big + used, pieces[pick], l); used += l; }
            else { size_t l = next_random() % 6000; memset(big + used, 'A' + (int)(next_random() % 26), l); used += l;
                   if (next_random() % 2) big[used++] = '\n'; }
            if (next_random() % 50 == 0) break;
        }
        equal = same_decision(big, used, 1 + next_random() % 70000);
    }
    expect(equal, "300 random files: the scan along the copy decides as the file scans");

    /* --- A start after the dialog's inspection uses it; a refused start copies nothing. --- */
    static const char flat[] = "G28\nT0\nG1 X10 Y10 E1\nT1\nG1 X2 E1\n";
    write_bytes("usb/flat.gcode", flat, strlen(flat));
    inspect("flat.gcode");
    expect(usb_start_file(&mqtt, "flat.gcode", "", "adaptive", response, sizeof(response)) == 409 &&
           strstr(response, "leveling mode") && usb_start.from_cache && copies() == 0,
           "a refusal decided from the cached inspection copies nothing");
    expect(usb_start_file(&mqtt, "flat.gcode", "0:0,1:1", "saved", response, sizeof(response)) == 202 &&
           usb_start.from_cache && starts == 1 && copies() == 1, "an accepted start from the cached inspection is copied and printed");
    /* The cached decision is checked by the copy: here the cache is made to say the file has an
     * adaptive mesh line, which the copied bytes do not; the copy is discarded, nothing is printed. */
    char copy[PATH_MAX_LOCAL * 2], name[PATH_MAX_LOCAL];
    assert(!gcode_usb_import_name("flat.gcode", name)); snprintf(copy, sizeof(copy), "gcode/%s", name); assert(!unlink(copy));
    inspect("flat.gcode");
    pthread_mutex_lock(&analysis_mutex);
    char *flag = strstr(analysis_cached[0].response, "\"adaptive_mesh\":false");
    assert(flag); memcpy(flag, "\"adaptive_mesh\":true", 20);
    memmove(flag + 20, flag + 21, strlen(flag + 21) + 1); analysis_cached[0].length--;
    pthread_mutex_unlock(&analysis_mutex);
    starts = 0;
    expect(usb_start_file(&mqtt, "flat.gcode", "", "saved", response, sizeof(response)) == 507 &&
           strstr(response, "changed") && usb_start.from_cache && starts == 0 && copies() == 0,
           "a copy that does not confirm the cached decision is discarded and not printed");
    /* Without a usable cache entry the file is scanned before the copy, as before. */
    struct timespec later[2] = {{time(NULL) + 5, 0}, {time(NULL) + 5, 0}};
    assert(!utimensat(AT_FDCWD, "usb/flat.gcode", later, 0));
    expect(usb_start_file(&mqtt, "flat.gcode", "", "adaptive", response, sizeof(response)) == 409 &&
           !usb_start.from_cache && copies() == 0, "a file changed since its inspection is scanned again and refused without a copy");
    write_bytes("usb/other.gcode", flat, strlen(flat));
    expect(usb_start_file(&mqtt, "other.gcode", "", "saved", response, sizeof(response)) == 202 && !usb_start.from_cache,
           "a file that was never inspected is scanned before the copy");

    if (failures) { printf("%d FAIL\n", failures); return 1; }
    puts("PASS USB start checked once: cached inspection decides, the copy confirms it, same line logic as the file scan");
    return 0;
}
