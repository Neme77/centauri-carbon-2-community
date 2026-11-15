#define main cc2_main_original
#define mqtt_start_print mock_mqtt_start_print
#include "../src/main.c"
#undef main
#undef mqtt_start_print
#include <assert.h>

/* A print start from internal memory is checked on the start worker like a USB start (#344): the
 * HTTP request hands its socket over at once, CC2 Control answers other requests meanwhile, the
 * file cannot be deleted while its start is prepared, and the start goes out from the main loop
 * only for the file that was checked. */
static int failures;
static void expect(int ok, const char *what) {
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}
static mqtt_client model;
static console_state console;
static int starts;
int mock_mqtt_start_print(mqtt_client *c, const char *media, const char *filename, const int *tools, const int *trays,
                          size_t count, char layout, int force, int timelapse) {
    (void)c; (void)media; (void)filename; (void)tools; (void)trays; (void)count; (void)layout; (void)force; (void)timelapse;
    starts++; return 0;
}
static int fake_uds(const char *q, char **r, size_t *l) { (void)q; *r = strdup("{\"id\":1001,\"result\":{}}"); *l = strlen(*r); return 0; }
static int gate[2], reached[2];
/* The worker reports that it holds (its stamp is taken), then waits for the test. */
static void hold(void) { char byte; assert(write(reached[1], "r", 1) == 1); assert(read(gate[0], &byte, 1) == 1); }
static void at_hold(void) { char byte; assert(read(reached[0], &byte, 1) == 1); }
static void release(void) { assert(write(gate[1], "x", 1) == 1); }
static void write_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb"); assert(file); assert(fputs(text, file) >= 0); assert(!fclose(file));
}

/* A real loopback connection: the HTTP origin check needs an AF_INET address. */
static int connection(int *client) {
    int listener = socket(AF_INET, SOCK_STREAM, 0); assert(listener >= 0);
    struct sockaddr_in addr; memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(!bind(listener, (struct sockaddr *)&addr, sizeof(addr)) && !listen(listener, 1));
    socklen_t size = sizeof(addr); assert(!getsockname(listener, (struct sockaddr *)&addr, &size));
    service_http_port = ntohs(addr.sin_port);
    *client = socket(AF_INET, SOCK_STREAM, 0); assert(*client >= 0);
    assert(!connect(*client, (struct sockaddr *)&addr, sizeof(addr)));
    int server = accept(listener, NULL, NULL); assert(server >= 0); close(listener);
    return server;
}
/* Sends one request through handle_client. Returns its result (1: a worker holds the socket);
 * a socket nobody holds is closed as the main loop does. */
static int http(const char *method, const char *path, const char *body, int *client) {
    int server = connection(client);
    char request[2048];
    int n = snprintf(request, sizeof(request),
                     "%s %s HTTP/1.1\r\nHost: localhost:%d\r\nOrigin: http://localhost:%d\r\nX-CC2-Request: 1\r\n"
                     "Content-Length: %zu\r\n\r\n%s", method, path, service_http_port, service_http_port, strlen(body), body);
    int held = handle_client(server, request, (size_t)n, "none", &model, &console);
    if (!held) close(server);
    return held;
}
static int status_of(int client, char *reply, size_t cap) {
    size_t used = 0; ssize_t n;
    while (used + 1 < cap && (n = read(client, reply + used, cap - 1 - used)) > 0) used += (size_t)n;
    reply[used] = 0; close(client);
    int status = 0; return sscanf(reply, "HTTP/1.1 %d", &status) == 1 ? status : -1;
}
static int nothing_yet(int client) {
    char byte; return recv(client, &byte, 1, MSG_DONTWAIT) < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
}
static void wait_check(void) {
    for (int i = 0; i < 5000 && !atomic_load(&usb_start_done); ++i) nanosleep(&(struct timespec){0, 1000000}, NULL);
    assert(atomic_load(&usb_start_done));
}
static void idle(void) {
    model.connected = model.registered = model.have_machine_status = 1;
    model.machine_status = 1; model.last_message = time(NULL);
}

/* A test that waits for something that never comes ends red with a message instead of hanging the suite. */
static void test_too_long(int signal_number) {
    (void)signal_number;
    static const char text[] = "FAIL: the test ran longer than 300 s (an answer that never came?)\n";
    if (write(2, text, sizeof(text) - 1) < 0) {}
    _exit(1);
}
int main(void) {
    signal(SIGALRM, test_too_long); alarm(300);
    char dir[] = "internal-start-XXXXXX"; /* the harnesses share one working directory */
    assert(mkdtemp(dir) && !chdir(dir));
    assert(!mkdir("gcode", 0700) && !mkdir("usb", 0700));
    write_file("gcode/part.gcode", "G28\nG1 X10 Y10 E1\n");
    write_file("autosave.cfg", "[bed_mesh default]\nx_count = 11\ny_count = 11\npoints =\n  0, 0\n");
    gcode_internal_root = "gcode"; gcode_usb_root = "usb"; printer_autosave_path = "autosave.cfg";
    canvas_color_table_uds = fake_uds;
    console_init(&console, "/tmp/unused"); uds_init(&telemetry); idle();
    assert(!pipe(gate) && !pipe(reached));
    char reply[2048];
    static const char start[] = "internal\npart.gcode\n\nA\nsaved\n0";

    /* The start hands its socket to the worker; nothing is answered until the main loop's tick. */
    usb_start_copy_hook = hold;
    int client;
    expect(http("POST", "/api/gcode-files/print", start, &client) == 1 && nothing_yet(client),
           "an internal start is handed to the start worker, not answered while its file is checked");
    at_hold();
    int other;
    expect(http("GET", "/api/health", "", &other) == 0 && status_of(other, reply, sizeof(reply)) == 200,
           "CC2 Control answers other requests while the file is checked");
    expect(http("POST", "/api/gcode-files/delete", "internal\npart.gcode", &other) == 0 &&
           status_of(other, reply, sizeof(reply)) == 409 && strstr(reply, "file_delete_pending_print"),
           "the file cannot be deleted while its start is prepared");
    expect(http("POST", "/api/gcode-files/print", start, &other) == 0 && status_of(other, reply, sizeof(reply)) == 409 &&
           strstr(reply, "still being prepared"), "a second start waits for the first");
    release(); wait_check(); usb_start_tick(&model);
    expect(status_of(client, reply, sizeof(reply)) == 202 && strstr(reply, "\"storage_media\":\"local\"") &&
           strstr(reply, "\"imported_from_usb\":false") && starts == 1, "after the check the start is sent and answered 202");

    /* The file is replaced while it is checked: the start is refused, nothing is sent. */
    starts = 0;
    expect(http("POST", "/api/gcode-files/print", start, &client) == 1, "another start is handed over");
    at_hold();
    write_file("gcode/part.gcode", "G28\nG1 X20 Y20 E1\nG1 X30\n");
    release(); wait_check(); usb_start_tick(&model);
    expect(status_of(client, reply, sizeof(reply)) == 409 && strstr(reply, "changed while the start was prepared") && starts == 0,
           "a file replaced during the check is not started");

    /* A refusal of the check (adaptive leveling, no adaptive mesh in the file) is answered after it. */
    expect(http("POST", "/api/gcode-files/print", "internal\npart.gcode\n\nA\nadaptive\n0", &client) == 1, "a refused start is handed over too");
    at_hold();
    release(); wait_check(); usb_start_tick(&model);
    expect(status_of(client, reply, sizeof(reply)) == 409 && strstr(reply, "leveling mode") && starts == 0,
           "the refusal comes after the check, nothing is sent");

    /* The browser leaves during the check: nothing is sent. */
    expect(http("POST", "/api/gcode-files/print", start, &client) == 1, "a start whose browser then leaves");
    at_hold();
    close(client); usb_start_tick(&model);
    release(); wait_check(); usb_start_tick(&model);
    expect(starts == 0 && !usb_start_active(), "a browser gone during the check sends nothing");
    usb_start_copy_hook = NULL;

    close(gate[0]); close(gate[1]); close(reached[0]); close(reached[1]); console_destroy(&console);
    if (failures) { printf("%d FAIL\n", failures); return 1; }
    puts("PASS internal start in the background: socket handed over, other requests answered, delete blocked, checked file only");
    return 0;
}
