/* A new print must not show the previous print's layer, durations, progress or filament use, even when
 * it runs the same file (seen: right after a start /api/printer still said layer 193 of the previous
 * print, and the layer-path page asked for it). MQTT tells a new print by its job uuid, the direct
 * printer stream (UDS) by print_stats.state going from an idle state to "printing". Pausing, resuming
 * and reconnecting in the middle of a print must keep what was reported. */
#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

static int peer = -1;
static double event_time = 1;

static void connect_mqtt(mqtt_client *m) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    if (peer >= 0) close(peer);
    m->fd = pair[0]; peer = pair[1]; m->connected = 1;
}
static void mq(mqtt_client *m, const char *json) {
    unsigned char packet[2200];
    const char topic[] = "elegoo/test/api_status";
    size_t topic_length = sizeof(topic) - 1, length = strlen(json), n = 2 + topic_length + length, h = 0, rest = n;
    packet[h++] = 0x30;
    do { unsigned char byte = rest & 127; rest >>= 7; if (rest) byte |= 128; packet[h++] = byte; } while (rest);
    packet[h++] = 0; packet[h++] = (unsigned char)topic_length;
    memcpy(packet + h, topic, topic_length); h += topic_length;
    memcpy(packet + h, json, length); h += length;
    assert(send(peer, packet, h, 0) == (ssize_t)h);
    assert(mqtt_process(m) == 0);
}
static void ud(const char *state, const char *file) {
    char json[1200];
    int n = snprintf(json, sizeof(json),
        "{\"method\":\"cc2_status\",\"params\":{\"eventtime\":%.0f,\"status\":{\"print_stats\":{\"state\":\"%s\"%s%s%s}}}}",
        event_time++, state, file ? ",\"filename\":\"" : "", file ? file : "", file ? "\"" : "");
    assert(n > 0 && (size_t)n < sizeof(json));
    assert(uds_message(&telemetry, json, strlen(json)));
}
static void ud_raw(const char *json_with_time_hole, const char *members) {
    char json[1200];
    int n = snprintf(json, sizeof(json), json_with_time_hole, event_time++, members);
    assert(n > 0 && (size_t)n < sizeof(json));
    assert(uds_message(&telemetry, json, strlen(json)));
}
static char reply[8000];
static void read_printer(mqtt_client *m) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    printer_response(pair[0], m);
    ssize_t n = recv(pair[1], reply, sizeof(reply) - 1, 0); assert(n > 0); reply[n] = 0;
    close(pair[0]); close(pair[1]);
}
static long field(const char *key) {
    const char *p = strstr(reply, key); assert(p);
    return strtol(p + strlen(key), NULL, 10);
}
#define EXPECT(layer, dur, total, prog) do { read_printer(&m); \
    if (field("\"current_layer\":") != (layer) || field("\"duration\":") != (dur) || \
        field("\"total_duration\":") != (total) || field("\"progress\":") != (prog)) { \
        fprintf(stderr, "line %d: layer %ld dur %ld total %ld progress %ld, expected %d %d %d %d\n", __LINE__, \
            field("\"current_layer\":"), field("\"duration\":"), field("\"total_duration\":"), field("\"progress\":"), \
            (layer), (dur), (total), (prog)); return 1; } } while (0)

int main(void) {
    static mqtt_client m;
    mqtt_init(&m); connect_mqtt(&m);
    m.registered = 1; m.last_message = time(NULL);
    uds_init(&telemetry);

    /* ---- MQTT: the same file twice in a row ---- */
    /* The first message the service ever sees carries values together with a uuid: they stay. */
    mq(&m, "{\"machine_status\":{\"status\":2,\"progress\":60},\"print_status\":{\"filename\":\"a.gcode\",\"state\":\"printing\",\"uuid\":\"u1\",\"current_layer\":193,\"print_duration\":4000,\"total_duration\":4100,\"remaining_time_sec\":777}}");
    EXPECT(193, 4000, 4100, 60);
    assert(field("\"remaining\":") == 777);
    /* Pause and resume keep the uuid: nothing is dropped. */
    mq(&m, "{\"print_status\":{\"state\":\"paused\",\"uuid\":\"u1\"}}");
    EXPECT(193, 4000, 4100, 60);
    mq(&m, "{\"print_status\":{\"state\":\"printing\",\"uuid\":\"u1\"}}");
    EXPECT(193, 4000, 4100, 60);
    /* Print end: state complete, machine idle. The numbers of the finished print stay visible. */
    mq(&m, "{\"machine_status\":{\"status\":1},\"print_status\":{\"state\":\"complete\",\"uuid\":\"u1\"}}");
    EXPECT(193, 4000, 4100, 60);
    /* The same file is printed again: a new uuid, and no layer or time in the first messages. */
    mq(&m, "{\"machine_status\":{\"status\":2},\"print_status\":{\"filename\":\"a.gcode\",\"state\":\"printing\",\"uuid\":\"u2\"}}");
    EXPECT(0, 0, 0, 0);
    assert(field("\"remaining\":") == 0); /* the estimate of the print before goes as well */
    mq(&m, "{\"print_status\":{\"filename\":\"a.gcode\",\"state\":\"printing\",\"uuid\":\"u2\",\"print_duration\":3}}");
    EXPECT(0, 3, 0, 0);
    mq(&m, "{\"print_status\":{\"uuid\":\"u2\",\"current_layer\":1}}");
    EXPECT(1, 3, 0, 0);
    /* A reconnection in the middle of the print keeps the layer, the uuid is the same. */
    mqtt_close(&m); connect_mqtt(&m);
    mq(&m, "{\"print_status\":{\"filename\":\"a.gcode\",\"state\":\"printing\",\"uuid\":\"u2\"}}");
    EXPECT(1, 3, 0, 0);
    /* A message that starts a new print and already carries the new values keeps those values. */
    mq(&m, "{\"print_status\":{\"state\":\"printing\",\"uuid\":\"u3\",\"current_layer\":2,\"print_duration\":9}}");
    EXPECT(2, 9, 0, 0);
    /* Progress that comes in the same message as the new uuid is kept as well. */
    mq(&m, "{\"machine_status\":{\"status\":2,\"progress\":5},\"print_status\":{\"state\":\"printing\",\"uuid\":\"u3b\"}}");
    EXPECT(0, 0, 0, 5);
    /* The uuid is empty while idle: that resets nothing, and the next print is still told apart from the one before. */
    mq(&m, "{\"print_status\":{\"uuid\":\"u3b\",\"current_layer\":6}}");
    mq(&m, "{\"machine_status\":{\"status\":1,\"progress\":100},\"print_status\":{\"state\":\"complete\",\"uuid\":\"\"}}");
    EXPECT(6, 0, 0, 100);
    mq(&m, "{\"machine_status\":{\"status\":2},\"print_status\":{\"state\":\"printing\",\"uuid\":\"u4\"}}");
    EXPECT(0, 0, 0, 0);
    /* Another file with another uuid is reset as well. */
    mq(&m, "{\"print_status\":{\"filename\":\"a.gcode\",\"uuid\":\"u4\",\"current_layer\":5}}");
    mq(&m, "{\"print_status\":{\"filename\":\"b.gcode\",\"uuid\":\"u5\"}}");
    EXPECT(0, 0, 0, 0);

    /* Values that came before any uuid stay when the first uuid arrives (service start in the middle of a print). */
    mqtt_init(&m); connect_mqtt(&m); m.registered = 1; m.last_message = time(NULL);
    mq(&m, "{\"machine_status\":{\"status\":2,\"progress\":40},\"print_status\":{\"filename\":\"a.gcode\",\"state\":\"printing\",\"current_layer\":7,\"print_duration\":50}}");
    EXPECT(7, 50, 0, 40);
    mq(&m, "{\"print_status\":{\"uuid\":\"w1\"}}");
    EXPECT(7, 50, 0, 40);
    mq(&m, "{\"print_status\":{\"uuid\":\"w2\"}}"); /* a different uuid after that is a new print */
    EXPECT(0, 0, 0, 0);

    /* ---- UDS: the same file twice in a row ---- */
    mqtt_init(&m); connect_mqtt(&m); m.registered = 1; m.last_message = time(NULL);
    mq(&m, "{\"machine_status\":{\"status\":2},\"print_status\":{\"filename\":\"a.gcode\",\"state\":\"printing\",\"uuid\":\"v1\"}}");
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair)); telemetry.fd = pair[0];
    ud_raw("{\"id\":11,\"result\":{\"eventtime\":%.0f,\"status\":{\"print_stats\":{%s}}}}",
        "\"state\":\"printing\",\"filename\":\"local/a.gcode\",\"info\":{\"current_layer\":193},\"print_duration\":4000,\"total_duration\":4100,\"filament_used\":9000");
    ud_raw("{\"method\":\"cc2_status\",\"params\":{\"eventtime\":%.0f,\"status\":{\"virtual_sdcard\":{\"progress\":0.6}%s}}}", "");
    EXPECT(193, 4000, 0, 60);
    double v; assert(uds_value(&telemetry, U_FILAMENT_USED, &v) && v == 9000);
    assert(uds_value(&telemetry, U_TOTAL_DURATION, &v) && v == 4100); /* /api/printer shows total_duration from MQTT only */
    /* Pause and resume: the layer stays. */
    ud("paused", NULL); EXPECT(193, 4000, 0, 60);
    ud("printing", NULL); EXPECT(193, 4000, 0, 60);
    assert(uds_value(&telemetry, U_FILAMENT_USED, &v) && v == 9000);
    /* Printing again to printing is no start. */
    ud("printing", NULL); EXPECT(193, 4000, 0, 60);
    /* Print ends. */
    ud("complete", NULL); EXPECT(193, 4000, 0, 60);
    /* Same file again. The printer sends only the state first. */
    ud("printing", "local/a.gcode");
    assert(!uds_value(&telemetry, U_LAYER, &v) && !uds_value(&telemetry, U_DURATION, &v) &&
           !uds_value(&telemetry, U_TOTAL_DURATION, &v) && !uds_value(&telemetry, U_PROGRESS, &v) &&
           !uds_value(&telemetry, U_FILAMENT_USED, &v));
    EXPECT(0, 0, 0, 0);
    ud_raw("{\"method\":\"cc2_status\",\"params\":{\"eventtime\":%.0f,\"status\":{\"print_stats\":{\"info\":{\"current_layer\":1}%s}}}}", "");
    EXPECT(1, 0, 0, 0);
    /* Cancelled and error runs end the same way. */
    ud("cancelled", NULL); EXPECT(1, 0, 0, 0);
    ud("standby", NULL); EXPECT(1, 0, 0, 0); /* one idle state after another starts nothing */
    ud("printing", NULL); EXPECT(0, 0, 0, 0);
    ud_raw("{\"method\":\"cc2_status\",\"params\":{\"eventtime\":%.0f,\"status\":{\"print_stats\":{\"info\":{\"current_layer\":7}%s}}}}", "");
    ud("error", NULL); ud("printing", NULL); EXPECT(0, 0, 0, 0);
    ud_raw("{\"method\":\"cc2_status\",\"params\":{\"eventtime\":%.0f,\"status\":{\"print_stats\":{\"info\":{\"current_layer\":8}%s}}}}", "");
    ud("standby", NULL); ud("printing", NULL); EXPECT(0, 0, 0, 0);
    /* A cancelled print followed directly by a start (no standby in between). */
    ud_raw("{\"method\":\"cc2_status\",\"params\":{\"eventtime\":%.0f,\"status\":{\"print_stats\":{\"info\":{\"current_layer\":3}%s}}}}", "");
    ud("cancelled", NULL); EXPECT(3, 0, 0, 0);
    ud("printing", NULL); EXPECT(0, 0, 0, 0);
    /* Values that arrive after the start are taken. */
    ud_raw("{\"method\":\"cc2_status\",\"params\":{\"eventtime\":%.0f,\"status\":{\"print_stats\":{\"info\":{\"current_layer\":9}%s}}}}", "");
    EXPECT(9, 0, 0, 0);
    /* Reconnecting in the middle of the print: the snapshot says printing, the layer is the snapshot's. */
    uds_close(&telemetry); assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair)); telemetry.fd = pair[0];
    ud_raw("{\"id\":11,\"result\":{\"eventtime\":%.0f,\"status\":{\"print_stats\":{%s}}}}",
        "\"state\":\"printing\",\"filename\":\"local/a.gcode\",\"info\":{\"current_layer\":40},\"print_duration\":900");
    EXPECT(40, 900, 0, 0);
    ud("printing", NULL); EXPECT(40, 900, 0, 0);
    /* Another file: as before, the old values go. */
    ud("printing", "local/b.gcode");
    assert(!uds_value(&telemetry, U_LAYER, &v));
    puts("PASS print start reset: a new print drops the old layer, durations, progress and filament use; pause, resume and reconnect keep them");
    return 0;
}
