/* The scan of the running print's file (layer count and print time, #92) reads the whole file. Seen on the printer
 * (18.11.2026 18:32): that scan of a 52 MB file ran into the homing of the print start, then "Timer too close"
 * (error 803) and the printer program crashed. So it follows the read hold (gcode_read_blocked): /api/printer does
 * not start it while the printer homes, levels or starts a print (before layer 1), starts it once the hold is over,
 * and a scan that runs when the hold comes on ends at its next check without a result and is started again later. */
#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

static int failures;
static void expect(int ok, const char *what) { printf("%s: %s\n", ok ? "PASS" : "FAIL", what); if (!ok) failures++; }
static void too_long(int signal_number) {
    (void)signal_number;
    static const char text[] = "FAIL: the test ran longer than 120 s\n";
    if (write(2, text, sizeof(text) - 1) < 0) {}
    _exit(1);
}
static void sleep_ms(long ms) { struct timespec t = {ms / 1000, (ms % 1000) * 1000000L}; nanosleep(&t, NULL); }
static mqtt_client m;
static console_state console;
static char reply[8192];
static void ask_printer(void) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    printer_response(pair[0], &m);
    ssize_t n = recv(pair[1], reply, sizeof(reply) - 1, 0); assert(n > 0); reply[n] = 0;
    close(pair[0]); close(pair[1]);
}
static void worker_idle(void) {
    for (int i = 0; i < 3000; i++) {
        pthread_mutex_lock(&analysis_mutex); int busy = analysis_running || active_analysis_pending;
        pthread_mutex_unlock(&analysis_mutex);
        if (!busy) return;
        sleep_ms(2);
    }
    assert(!"the analysis worker did not finish");
}
static int ready(void) {
    pthread_mutex_lock(&analysis_mutex); int r = active_analysis_ready; pthread_mutex_unlock(&analysis_mutex); return r;
}
static int pending(void) {
    pthread_mutex_lock(&analysis_mutex); int p = active_analysis_pending; pthread_mutex_unlock(&analysis_mutex); return p;
}
static atomic_int checks;
/* Runs on the analysis worker: it touches only the hold itself (atomic), as the main loop's pass would set it when
 * the printer starts homing; the MQTT state belongs to the main thread. */
static void hold_at_first_check(void) {
    if (atomic_fetch_add(&checks, 1) == 0) atomic_store(&gcode_read_hold, 1);
}
static long long health_of(const char *key) {
    char out[512]; layer_path_health_json(out, sizeof(out));
    char needle[64]; snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char *p = strstr(out, needle); assert(p);
    return strtoll(p + strlen(needle), NULL, 10);
}
static void printing(int layer) {
    m.connected = m.registered = m.have_machine_status = 1; m.machine_status = 2; m.last_message = time(NULL);
    snprintf(m.print_state, sizeof(m.print_state), "printing"); m.current_layer = layer;
}

int main(void) {
    signal(SIGALRM, too_long); alarm(120);
    char dir[] = "/tmp/active-hold-XXXXXX"; assert(mkdtemp(dir));
    gcode_internal_root = dir;
    char path[512]; snprintf(path, sizeof(path), "%s/job.gcode", dir);
    FILE *f = fopen(path, "w"); assert(f);
    assert(fputs("; total layer number: 300\n; estimated printing time (normal mode) = 1h 0m 0s\n", f) >= 0);
    for (int i = 0; i < 300; i++) for (int k = 0; k < 120; k++) assert(fprintf(f, "G1 X%d Y%d E0.01 ; filler line\n", k, i) > 0);
    assert(!fclose(f)); /* about 1 MB: many 64 KiB checks */
    uds_init(&telemetry);
    mqtt_init(&m);
    memset(&console, 0, sizeof(console)); pthread_mutex_init(&console.lock, NULL);
    snprintf(m.filename, sizeof(m.filename), "job.gcode");

    /* Start phase (layer 0): /api/printer starts no scan and leaves nothing pending. */
    printing(0); gcode_read_follow(&m, &console);
    long long blocked = health_of("gcode_read_blocked");
    ask_printer(); ask_printer(); ask_printer();
    sleep_ms(150);
    expect(!pending() && !ready(), "start phase: /api/printer does not start the scan of the running print's file");
    expect(health_of("gcode_read_blocked") == blocked + 1, "start phase: the withheld scan is counted once");
    expect(strstr(reply, "\"total_layers\":0") != NULL, "start phase: no layer count from the file yet");
    /* The printer's own layer count is shown as before. */
    m.have_total_layers = 1; m.total_layers = 280; ask_printer();
    expect(strstr(reply, "\"total_layers\":280") != NULL, "start phase: the count the printer reports is shown");
    m.have_total_layers = 0;
    /* Homing and leveling (also without a print), and the console's own G28, hold it as well. */
    m.machine_status = 10; ask_printer(); sleep_ms(50);
    expect(!pending() && !ready(), "manual homing: no scan");
    m.machine_status = 5; ask_printer(); sleep_ms(50);
    expect(!pending() && !ready(), "auto leveling: no scan");
    printing(3);
    pthread_mutex_lock(&console.lock); console.busy = 1; snprintf(console.command, sizeof(console.command), "G28");
    pthread_mutex_unlock(&console.lock);
    gcode_read_follow(&m, &console); ask_printer(); sleep_ms(50);
    expect(!pending() && !ready(), "console G28: no scan (the main loop's hold)");
    pthread_mutex_lock(&console.lock); console.busy = 0; pthread_mutex_unlock(&console.lock);
    gcode_read_follow(&m, &console);

    /* The hold is over (layer 1 reached): the next /api/printer starts the scan, and its result comes. */
    printing(1); gcode_read_follow(&m, &console);
    ask_printer(); worker_idle();
    expect(ready(), "after the hold: the next /api/printer starts the scan");
    ask_printer();
    expect(strstr(reply, "\"total_layers\":300") != NULL, "after the hold: the file's layer count is shown");

    /* A scan that runs when the hold comes on ends at its next check, keeps no result and runs again later. */
    pthread_mutex_lock(&analysis_mutex); active_analysis_generation++; active_analysis_filename[0] = 0; active_analysis_ready = 0;
    pthread_mutex_unlock(&analysis_mutex);
    long long stopped = health_of("gcode_read_stopped");
    atomic_store(&checks, 0); active_scan_check_hook = hold_at_first_check;
    printing(1); gcode_read_follow(&m, &console);
    ask_printer(); worker_idle();
    active_scan_check_hook = NULL;
    expect(atomic_load(&checks) == 1, "running scan: it ends at the first check after the hold came on");
    expect(!ready() && !pending(), "running scan: no result is kept and nothing stays pending");
    expect(health_of("gcode_read_stopped") == stopped + 1, "running scan: counted as stopped");
    ask_printer(); sleep_ms(50);                                /* the hold is still on (nothing cleared it) */
    expect(!pending() && !ready(), "running scan: while the hold lasts it is not started again");
    printing(2); gcode_read_follow(&m, &console);
    ask_printer(); worker_idle();
    expect(ready(), "running scan: started again once the hold is over");
    ask_printer();
    expect(strstr(reply, "\"total_layers\":300") != NULL, "running scan: the layer count comes after the restart");

    /* Without a fresh printer connection nothing is held. */
    pthread_mutex_lock(&analysis_mutex); active_analysis_generation++; active_analysis_filename[0] = 0; active_analysis_ready = 0;
    pthread_mutex_unlock(&analysis_mutex);
    printing(0); m.last_message = time(NULL) - 60; gcode_read_follow(&m, &console);
    ask_printer(); worker_idle();
    expect(ready(), "stale printer status: the scan runs as before");

    unlink(path); rmdir(dir);
    if (failures) { printf("%d FAIL\n", failures); return 1; }
    puts("PASS the running print's file scan waits for the read hold, is started once it is over, and a running one ends without a result");
    return 0;
}
