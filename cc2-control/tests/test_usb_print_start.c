#define main cc2_main_original
#define mqtt_start_print mock_mqtt_start_print
#include "../src/main.c"
#undef main
#undef mqtt_start_print
#include <assert.h>

/* #344: a print dialog start from USB scans and copies the file on a worker thread. The socket gets
 * its answer only from usb_start_tick() on the main loop, which checks the printer again first. */
static char events[128], started_name[PATH_MAX_LOCAL];

static int fake_uds(const char *request, char **reply, size_t *length) {
    (void)request;
    static const char acknowledged[] = "{\"id\":1001,\"result\":{}}";
    strcat(events, "clear;");
    *reply = strdup(acknowledged); *length = strlen(acknowledged);
    return *reply ? 0 : -1;
}

int mock_mqtt_start_print(mqtt_client *c, const char *media, const char *filename,
                          const int *tools, const int *trays, size_t count,
                          char layout, int bedlevel_force, int timelapse) {
    (void)c; (void)tools; (void)trays; (void)layout; (void)bedlevel_force; (void)timelapse;
    char text[32]; snprintf(text, sizeof(text), "1020:%s:%zu;", media, count); strcat(events, text);
    snprintf(started_name, sizeof(started_name), "%s", filename);
    return 0;
}

static void write_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb"); assert(file);
    assert(fputs(text, file) >= 0); assert(!fclose(file));
}

static int imports(void) {
    DIR *dir = opendir("gcode"); assert(dir);
    int count = 0; struct dirent *entry;
    while ((entry = readdir(dir))) if (!strncmp(entry->d_name, GCODE_USB_IMPORT_PREFIX, 8)) count++;
    closedir(dir);
    return count;
}

static int answer(int peer, char *response, size_t cap) {
    size_t used = 0; ssize_t n;
    while (used + 1 < cap && (n = recv(peer, response + used, cap - 1 - used, 0)) > 0) used += (size_t)n;
    response[used] = 0; close(peer);
    assert(used > 0);
    return atoi(strchr(response, ' ') + 1);
}

static int no_answer_yet(int peer) {
    char byte; return recv(peer, &byte, 1, MSG_DONTWAIT) < 0 && (errno == EAGAIN || errno == EWOULDBLOCK);
}

/* Begins a start; returns the peer socket when the worker took it, or answers in `response` and -1. */
static int begin(mqtt_client *mqtt, const char *body, char *response, size_t cap, int *status) {
    events[0] = 0; started_name[0] = 0;
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    if (usb_start_begin(pair[0], mqtt, body, strlen(body))) return pair[1];
    close(pair[0]);
    *status = answer(pair[1], response, cap);
    return -1;
}

static void wait_worker(void) {
    for (int i = 0; i < 5000 && !atomic_load(&usb_start_done); ++i) nanosleep(&(struct timespec){0, 1000000}, NULL);
    assert(atomic_load(&usb_start_done));
}

int main(void) {
    char response[1024]; int status = 0;
    char dir[] = "usb-start-XXXXXX"; /* the harnesses share one working directory */
    assert(mkdtemp(dir) && !chdir(dir));
    assert(!mkdir("gcode", 0700) && !mkdir("usb", 0700));
    static char big[3 * 65536 + 123];
    for (size_t i = 0; i + 1 < sizeof(big); ++i) big[i] = (i % 61 == 60) ? '\n' : (char)('A' + i % 26);
    big[0] = ';'; big[sizeof(big) - 1] = 0;
    write_file("usb/cube.gcode", big);
    write_file("usb/flat.gcode", "G28\nG1 X10 Y10 E1\n");
    write_file("usb/two.gcode", "T0\nG1 X1 E1\nT1\nG1 X2 E1\n");
    write_file("autosave.cfg", "[bed_mesh default]\nx_count = 11\ny_count = 11\npoints =\n  0, 0\n");
    gcode_internal_root = "gcode"; gcode_usb_root = "usb"; printer_autosave_path = "autosave.cfg";
    canvas_color_table_uds = fake_uds;
    mqtt_client mqtt = {0};
    mqtt.connected = mqtt.registered = 1; mqtt.have_machine_status = 1; mqtt.machine_status = 1;

    /* The worker takes the socket; nothing is answered or started before the main loop's tick. */
    int peer = begin(&mqtt, "usb\ncube.gcode\n\nA\nsaved\n0", response, sizeof(response), &status);
    assert(peer >= 0 && usb_start_active());
    assert(printer_change_blocked() && strstr(printer_change_blocked(), "USB file"));
    /* A second USB start while the first one copies is refused at once. */
    assert(begin(&mqtt, "usb\nflat.gcode\n\nA\nsaved\n0", response, sizeof(response), &status) < 0);
    assert(status == 409 && strstr(response, "still being copied"));
    wait_worker();
    assert(no_answer_yet(peer) && !events[0]);
    usb_start_tick(&mqtt);
    assert(!usb_start_active() && !printer_change_blocked());
    assert(answer(peer, response, sizeof(response)) == 202);
    assert(strstr(response, "\"imported_from_usb\":true") && strstr(response, "\"storage_media\":\"local\""));
    assert(!strcmp(events, "clear;1020:local:0;"));
    assert(!strncmp(started_name, GCODE_USB_IMPORT_PREFIX, 8) && strstr(started_name, "_cube.gcode"));
    char copy[PATH_MAX_LOCAL * 2]; snprintf(copy, sizeof(copy), "gcode/%s", started_name);
    FILE *file = fopen(copy, "rb"); assert(file);
    static char back[sizeof(big)]; size_t got = fread(back, 1, sizeof(back), file); fclose(file);
    assert(got == sizeof(big) - 1 && !memcmp(back, big, got));
    assert(imports() == 1);
    assert(!unlink(copy));

    /* The printer started something during the copy: refused, and the copy is removed again. */
    peer = begin(&mqtt, "usb\nflat.gcode\n\nA\nsaved\n0", response, sizeof(response), &status);
    assert(peer >= 0);
    wait_worker();
    assert(imports() == 1);
    mqtt.machine_status = 2;
    usb_start_tick(&mqtt);
    assert(answer(peer, response, sizeof(response)) == 409 && strstr(response, "idle printer"));
    assert(!events[0] && imports() == 0 && !usb_start_active());
    mqtt.machine_status = 1;

    /* An earlier import of the same file is never removed by a refused start. */
    write_file("gcode/" GCODE_USB_IMPORT_PREFIX "kept", "x"); /* stands in for an unrelated import */
    peer = begin(&mqtt, "usb\nflat.gcode\n\nA\nsaved\n0", response, sizeof(response), &status);
    assert(peer >= 0); wait_worker(); usb_start_tick(&mqtt);
    assert(answer(peer, response, sizeof(response)) == 202);
    char kept_copy[PATH_MAX_LOCAL * 2]; snprintf(kept_copy, sizeof(kept_copy), "gcode/%s", started_name);
    assert(imports() == 2);
    peer = begin(&mqtt, "usb\nflat.gcode\n\nA\nsaved\n0", response, sizeof(response), &status);
    assert(peer >= 0); wait_worker();
    mqtt.machine_status = 2;
    usb_start_tick(&mqtt);
    assert(answer(peer, response, sizeof(response)) == 409 && imports() == 2);
    struct stat kept; assert(!stat(kept_copy, &kept) && kept.st_size == 18);
    assert(!unlink(kept_copy) && !unlink("gcode/" GCODE_USB_IMPORT_PREFIX "kept"));
    mqtt.machine_status = 1;

    /* A start the scan refuses is answered without copying the file. */
    peer = begin(&mqtt, "usb\nflat.gcode\n\nA\nadaptive\n0", response, sizeof(response), &status);
    assert(peer >= 0); wait_worker(); usb_start_tick(&mqtt);
    assert(answer(peer, response, sizeof(response)) == 409 && strstr(response, "leveling mode"));
    assert(!events[0] && imports() == 0);
    peer = begin(&mqtt, "usb\ntwo.gcode\n0:1\nA\nsaved\n0", response, sizeof(response), &status);
    assert(peer >= 0); wait_worker(); usb_start_tick(&mqtt);
    assert(answer(peer, response, sizeof(response)) == 409 && strstr(response, "Canvas mapping"));
    assert(!events[0] && imports() == 0);
    peer = begin(&mqtt, "usb\ntwo.gcode\n0:1,1:3\nA\nsaved\n0", response, sizeof(response), &status);
    assert(peer >= 0); wait_worker(); usb_start_tick(&mqtt);
    assert(answer(peer, response, sizeof(response)) == 202 && !strcmp(events, "1020:local:2;"));
    snprintf(copy, sizeof(copy), "gcode/%s", started_name); assert(!unlink(copy));

    /* Refusals that need no file access are answered at once, without the worker. */
    mqtt.machine_status = 2;
    assert(begin(&mqtt, "usb\ncube.gcode\n\nA\nsaved\n0", response, sizeof(response), &status) < 0);
    assert(status == 409 && !usb_start_active());
    mqtt.machine_status = 1; mqtt.registered = 0;
    assert(begin(&mqtt, "usb\ncube.gcode\n\nA\nsaved\n0", response, sizeof(response), &status) < 0);
    assert(status == 503 && strstr(response, "MQTT"));
    mqtt.registered = 1;
    assert(begin(&mqtt, "usb\nmissing.gcode\n\nA\nsaved\n0", response, sizeof(response), &status) < 0);
    assert(status == 404 && !usb_start_active());
    assert(begin(&mqtt, "usb\ncube.gcode\n\nA\nsideways\n0", response, sizeof(response), &status) < 0);
    assert(status == 400);
    assert(gcode_request_is_usb("usb\ncube.gcode", 14) && !gcode_request_is_usb("internal\ncube.gcode", 19));

    /* The stick was pulled after the copy: refused like a missing file, and the copy is removed. */
    write_file("usb/gone.gcode", "G28\n");
    peer = begin(&mqtt, "usb\ngone.gcode\n\nA\nsaved\n0", response, sizeof(response), &status);
    assert(peer >= 0); wait_worker();
    assert(!unlink("usb/gone.gcode"));
    usb_start_tick(&mqtt);
    assert(answer(peer, response, sizeof(response)) == 404 && !events[0] && imports() == 0);

    puts("PASS USB print start: copy on a worker, start from the main loop, refusals clean up");
    return 0;
}
