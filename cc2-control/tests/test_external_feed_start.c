#define main cc2_main_original
#define mqtt_start_print mock_mqtt_start_print
#define mqtt_prepare_timelapse mock_mqtt_prepare_timelapse
#include "../src/main.c"
#undef main
#undef mqtt_start_print
#undef mqtt_prepare_timelapse
#include <assert.h>

/* Printer requests in the order they were sent: "clear" (the colour table),
 * "1019" (calibrated-start timelapse setup) and "1020:<mapped tools>". */
static char events[128], query[256];
static const char *reply_text;

static void event(const char *text) {
    size_t used = strlen(events);
    snprintf(events + used, sizeof(events) - used, "%s;", text);
}

static int fake_uds(const char *request, char **reply, size_t *length) {
    snprintf(query, sizeof(query), "%s", request);
    event("clear");
    if (!reply_text) return -1;
    *reply = strdup(reply_text); *length = strlen(reply_text);
    return *reply ? 0 : -1;
}

int mock_mqtt_start_print(mqtt_client *c, const char *media, const char *filename,
                          const int *tools, const int *trays, size_t count,
                          char layout, int bedlevel_force, int timelapse) {
    (void)c; (void)media; (void)filename; (void)tools; (void)trays;
    (void)layout; (void)bedlevel_force; (void)timelapse;
    char text[32]; snprintf(text, sizeof(text), "1020:%zu", count); event(text);
    return 0;
}

/* Refused, so a calibrated start never sends its script to a real printer socket. */
int mock_mqtt_prepare_timelapse(mqtt_client *c, int enabled) {
    (void)c; (void)enabled; event("1019");
    return -1;
}

static int start(mqtt_client *mqtt, const char *body, char *response, size_t cap) {
    events[0] = 0; query[0] = 0;
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    gcode_start_response(pair[0], mqtt, body, strlen(body));
    close(pair[0]);
    size_t used = 0; ssize_t n;
    while (used + 1 < cap && (n = recv(pair[1], response + used, cap - 1 - used, 0)) > 0) used += (size_t)n;
    response[used] = 0; close(pair[1]);
    assert(used > 0);
    return atoi(strchr(response, ' ') + 1);
}

static void write_file(const char *path, const char *text) {
    FILE *file = fopen(path, "wb"); assert(file);
    assert(fputs(text, file) >= 0); assert(!fclose(file));
}

int main(void) {
    static const char acknowledged[] = "{\"id\":1001,\"result\":{}}";
    char response[1024];
    assert(!mkdir("gcode", 0700));
    write_file("gcode/cube.gcode", "G28\nG1 X10 Y10 E1\n");
    write_file("gcode/two.gcode", "T0\nG1 X1 E1\nT1\nG1 X2 E1\n");
    write_file("autosave.cfg", "[bed_mesh default]\nx_count = 11\ny_count = 11\npoints =\n  0, 0\n");
    gcode_internal_root = "gcode"; printer_autosave_path = "autosave.cfg";
    canvas_color_table_uds = fake_uds;
    mqtt_client mqtt = {0};
    mqtt.connected = mqtt.registered = 1; mqtt.have_machine_status = 1; mqtt.machine_status = 1;

    /* External feed, saved mesh: the table is cleared, then method 1020 starts the file. */
    reply_text = acknowledged;
    assert(start(&mqtt, "internal\ncube.gcode\n\nA\nsaved\n0", response, sizeof(response)) == 202);
    assert(!strcmp(events, "clear;1020:0;"));
    assert(strstr(query, "\"method\":\"gcode/script\"") &&
           strstr(query, "\"script\":\"CANVAS_SET_COLOR_TABLE T= CHANNEL=\"}"));

    /* Any failed clear refuses the start: an error, a missing reply or no result. */
    const char *failures[] = {
        "{\"id\":1001,\"error\":{\"error\":\"WebRequestError\",\"message\":\"Unknown command\"}}",
        NULL, "{\"id\":1001}", "not json"};
    for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        reply_text = failures[i];
        assert(start(&mqtt, "internal\ncube.gcode\n\nA\nsaved\n0", response, sizeof(response)) == 503);
        assert(!strcmp(events, "clear;"));
        assert(strstr(response, "Cannot clear the Canvas colour table for the external feed"));
    }

    /* A Canvas mapping sets its own table in method 1020; nothing is cleared. */
    reply_text = acknowledged;
    assert(start(&mqtt, "internal\ntwo.gcode\n0:1,1:3\nA\nsaved\n0", response, sizeof(response)) == 202);
    assert(!strcmp(events, "1020:2;"));

    /* Calibrated starts clear the table before their preparation, and stop on a failed clear. */
    assert(start(&mqtt, "internal\ncube.gcode\n\nB\ncalibrate\n1", response, sizeof(response)) == 503);
    assert(!strcmp(events, "clear;1019;") && strstr(response, "Cannot send the printer start request"));
    reply_text = NULL;
    assert(start(&mqtt, "internal\ncube.gcode\n\nA\nfull\n0", response, sizeof(response)) == 503);
    assert(!strcmp(events, "clear;") && strstr(response, "Cannot clear the Canvas colour table"));
    reply_text = acknowledged;
    assert(!unlink("autosave.cfg")); /* a missing saved mesh forces a calibrated start */
    assert(start(&mqtt, "internal\ncube.gcode\n\nA\nsaved\n0", response, sizeof(response)) == 503);
    assert(!strcmp(events, "clear;1019;"));

    /* Requests the earlier guards refuse never reach the printer. */
    mqtt.machine_status = 2;
    assert(start(&mqtt, "internal\ncube.gcode\n\nA\nsaved\n0", response, sizeof(response)) == 409);
    assert(!events[0]);
    mqtt.machine_status = 1; mqtt.registered = 0;
    assert(start(&mqtt, "internal\ncube.gcode\n\nA\nsaved\n0", response, sizeof(response)) == 503);
    assert(!events[0] && strstr(response, "Printer MQTT is not ready"));
    mqtt.registered = 1;
    assert(start(&mqtt, "internal\ntwo.gcode\n0:1\nA\nsaved\n0", response, sizeof(response)) == 409);
    assert(!events[0]);

    puts("PASS external feed start: colour table cleared and acknowledged first, failures refuse the start");
    return 0;
}
