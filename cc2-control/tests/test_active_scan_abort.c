/* The scan of the active print (layers and print time for the progress) reads the whole file. It must end as soon as
 * that print is no longer the active one, so a stopped or deleted print never keeps its file open and its space
 * taken; a job already stale must not open the file; a file of 2 GiB or more (which the printer program cannot open)
 * is not read at all. */
#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

static int failures, checks;
static void expect(int ok, const char *what) { printf("%s: %s\n", ok ? "PASS" : "FAIL", what); if (!ok) failures++; }
static void test_too_long(int signal_number) {
    (void)signal_number;
    static const char text[] = "FAIL: the test ran longer than 300 s (an answer that never came?)\n";
    if (write(2, text, sizeof(text) - 1) < 0) {}
    _exit(1);
}
/* At the first check while reading, the print stops being the active one. */
static void print_ends(void) {
    checks++;
    pthread_mutex_lock(&analysis_mutex); active_analysis_generation++; pthread_mutex_unlock(&analysis_mutex);
}
static void count_check(void) { checks++; }
/* The printer's MQTT message arrives while the scan reads; nobody asks /api/printer. */
static mqtt_client printer;
static void message_arrives(void) { checks++; active_scan_follow(&printer); }
static void watch(const char *name) {
    pthread_mutex_lock(&analysis_mutex);
    snprintf(active_analysis_filename, sizeof(active_analysis_filename), "%s", name); active_analysis_generation++;
    pthread_mutex_unlock(&analysis_mutex);
}

int main(void) {
    signal(SIGALRM, test_too_long); alarm(300);
    char dir[] = "active-scan-XXXXXX"; /* the harnesses share one working directory */
    assert(mkdtemp(dir) && !chdir(dir) && !mkdir("gcode", 0700));
    gcode_internal_root = "gcode";
    FILE *f = fopen("gcode/job.gcode", "w"); assert(f);
    for (int i = 0; i < 100000; i++) fprintf(f, ";LAYER:%d\nG1 X1 Y1 E0.1\n", i); /* about 2.4 MB, 100000 layers */
    assert(!fclose(f));

    int layers = 0; long seconds = -1; unsigned now = active_analysis_generation;
    active_scan_check_hook = count_check;
    active_gcode_scan("job.gcode", now, &layers, &seconds);
    expect(layers == 100000 && checks > 1, "the active print's file is read to the end, checking as it goes");

    checks = 0; layers = 0; seconds = -1; active_scan_check_hook = print_ends;
    active_gcode_scan("job.gcode", active_analysis_generation, &layers, &seconds);
    expect(checks == 1 && layers == 0, "the scan stops at the next check once the print is no longer active");

    checks = 0; layers = 0; seconds = -1; active_scan_check_hook = count_check;
    active_gcode_scan("job.gcode", active_analysis_generation - 1, &layers, &seconds);
    expect(checks == 0 && layers == 0, "a job already stale does not read the file");

    /* The real path on the printer: the stop is reported over MQTT, and the scan ends without any /api/printer request. */
    snprintf(printer.filename, sizeof(printer.filename), "job.gcode");
    snprintf(printer.print_state, sizeof(printer.print_state), "printing");
    watch("job.gcode"); checks = 0; layers = 0; seconds = -1; active_scan_check_hook = message_arrives;
    active_gcode_scan("job.gcode", active_analysis_generation, &layers, &seconds);
    expect(layers == 100000, "while the printer reports printing, MQTT messages do not end the scan");
    snprintf(printer.print_state, sizeof(printer.print_state), "cancelled");
    watch("job.gcode"); checks = 0; layers = 0; seconds = -1;
    active_gcode_scan("job.gcode", active_analysis_generation, &layers, &seconds);
    expect(checks == 1 && layers == 0, "a cancel reported over MQTT ends the scan at the next check");
    snprintf(printer.print_state, sizeof(printer.print_state), "printing"); printer.filename[0] = 0;
    watch("job.gcode"); checks = 0; layers = 0; seconds = -1;
    active_gcode_scan("job.gcode", active_analysis_generation, &layers, &seconds);
    expect(checks == 1 && layers == 0, "so does a report without a file");
    expect(!active_scan_print_running(&(mqtt_client){.filename = "job.gcode", .print_state = "complete"}) &&
           active_scan_print_running(&(mqtt_client){.filename = "job.gcode", .print_state = "paused"}) &&
           active_scan_print_running(&(mqtt_client){.filename = "job.gcode"}),
           "/api/printer scans only a print that is printing, paused or without a reported state");
    /* /api/printer after the print: it starts no new scan of the finished print's file. */
    watch(""); snprintf(printer.filename, sizeof(printer.filename), "job.gcode");
    snprintf(printer.print_state, sizeof(printer.print_state), "complete");
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    printer_response(pair[0], &printer); close(pair[0]); close(pair[1]);
    pthread_mutex_lock(&analysis_mutex); int queued = active_analysis_pending || active_analysis_filename[0];
    pthread_mutex_unlock(&analysis_mutex);
    expect(!queued, "/api/printer after a finished print queues no scan of its file");
    for (int i = 0; i < 500 && analysis_running; i++) nanosleep(&(struct timespec){0, 10000000}, NULL);
    active_scan_check_hook = count_check;

    int fd = open("gcode/big.gcode", O_WRONLY | O_CREAT | O_TRUNC, 0644); assert(fd >= 0);
    static const char head[] = ";LAYER:4\n";
    assert(write(fd, head, sizeof(head) - 1) == (ssize_t)(sizeof(head) - 1));
    assert(!ftruncate(fd, (off_t)2147483648LL)); assert(!close(fd)); /* sparse: no disk space */
    checks = 0; layers = 0; seconds = -1;
    active_gcode_scan("big.gcode", active_analysis_generation, &layers, &seconds);
    expect(checks == 0 && layers == 0, "a file of 2 GiB or more is not read");
    fd = open("gcode/big.gcode", O_WRONLY); assert(fd >= 0 && !ftruncate(fd, (off_t)2147483647LL) && !close(fd));
    checks = 0; layers = 0; seconds = -1; active_scan_check_hook = NULL;
    active_gcode_scan("big.gcode", active_analysis_generation, &layers, &seconds);
    expect(layers == 5, "a file of 2 GiB - 1 is still read (the printer opens it)");
    unlink("gcode/big.gcode"); unlink("gcode/job.gcode");
    if (failures) { printf("%d FAIL\n", failures); return 1; }
    puts("PASS the active print's scan ends with the print (also from MQTT alone), skips stale jobs and files of 2 GiB or more");
    return 0;
}
