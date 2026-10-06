/* /api/exclude-objects must not poll the printer: elegoo_printer keeps every
 * UDS request in memory. The object list is asked once per job (an empty one
 * again with a delay), and the excluded/current objects come from telemetry. */
#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>
#include <pthread.h>

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int list_queries, status_queries;
static const char *objects_reply = "null";

static void *peer(void *arg) {
    int fd = *(int *)arg;
    for (;;) {
        char request[512]; size_t used = 0;
        while (!used || request[used - 1] != 3) {
            ssize_t n = recv(fd, request + used, sizeof(request) - 1 - used, 0);
            if (n <= 0) return NULL;
            used += (size_t)n;
        }
        request[used] = 0;
        unsigned long id = strtoul(strstr(request, "\"id\":") + 5, NULL, 10);
        char reply[512];
        pthread_mutex_lock(&lock);
        if (strstr(request, "excluded_objects")) {
            status_queries++;
            snprintf(reply, sizeof(reply), "{\"id\":%lu,\"result\":{\"eventtime\":1,\"status\":{\"exclude_object\":"
                     "{\"excluded_objects\":[\"queried\"],\"current_object\":null}}}}\003", id);
        } else {
            list_queries++;
            snprintf(reply, sizeof(reply), "{\"id\":%lu,\"result\":{\"eventtime\":1,\"status\":{\"exclude_object\":"
                     "{\"objects\":%s}}}}\003", id, objects_reply);
        }
        pthread_mutex_unlock(&lock);
        assert(send_all(fd, reply, strlen(reply)) == 0);
    }
}

static void counts(int *list, int *status) {
    pthread_mutex_lock(&lock); *list = list_queries; *status = status_queries; pthread_mutex_unlock(&lock);
}

/* Calls the route and returns its status code; the body goes to body. */
static int fetch(const mqtt_client *mqtt, char *body, size_t cap) {
    int http[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, http) == 0);
    exclude_objects_response(http[0], mqtt);
    close(http[0]);
    char reply[4096]; size_t used = 0; ssize_t n;
    while ((n = recv(http[1], reply + used, sizeof(reply) - 1 - used, 0)) > 0) used += (size_t)n;
    close(http[1]); reply[used] = 0;
    const char *start = strstr(reply, "\r\n\r\n"); assert(start);
    snprintf(body, cap, "%s", start + 4);
    return atoi(reply + 9);
}

static void telemetry_reports(const char *excluded, const char *current) {
    telemetry.ready = 1; clock_gettime(CLOCK_MONOTONIC, &telemetry.last_rx);
    snprintf(telemetry.excluded_objects, sizeof(telemetry.excluded_objects), "%s", excluded);
    snprintf(telemetry.current_object, sizeof(telemetry.current_object), "%s", current);
    telemetry.have_excluded_objects = telemetry.have_current_object = 1;
}

int main(void) {
    int sockets[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    object_query_fd = sockets[0];
    assert(fcntl(object_query_fd, F_SETFL, O_NONBLOCK) == 0);
    pthread_t thread; assert(pthread_create(&thread, NULL, peer, &sockets[1]) == 0);
    int idle[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, idle) == 0);
    uds_init(&telemetry); telemetry.fd = idle[0];
    mqtt_client mqtt; memset(&mqtt, 0, sizeof(mqtt));
    snprintf(mqtt.filename, sizeof(mqtt.filename), "%s", "cube.gcode");
    char body[4096]; int list, status;

    /* Undefined objects (null) are an empty list, not an error, and are not
     * asked again on every poll. */
    telemetry_reports("[]", "null");
    assert(fetch(&mqtt, body, sizeof(body)) == 200);
    assert(strstr(body, "\"objects\":[]") && strstr(body, "\"current_object\":null"));
    for (int i = 0; i < 5; i++) assert(fetch(&mqtt, body, sizeof(body)) == 200);
    counts(&list, &status); assert(list == 1 && status == 0);

    /* A current object means the job defined its objects: ask once more after 5 s. */
    pthread_mutex_lock(&lock); objects_reply = "[{\"name\":\"A\"},{\"name\":\"B\"}]"; pthread_mutex_unlock(&lock);
    telemetry_reports("[]", "\"A\"");
    assert(fetch(&mqtt, body, sizeof(body)) == 200);
    counts(&list, &status); assert(list == 1);
    exclude_objects_asked_ms -= 5000;
    assert(fetch(&mqtt, body, sizeof(body)) == 200);
    counts(&list, &status); assert(list == 2 && status == 0);
    assert(strstr(body, "\"name\":\"B\"") && strstr(body, "\"current_object\":\"A\""));

    /* A defined list stays cached for the job; the live state comes from telemetry. */
    telemetry_reports("[\"B\"]", "\"A\"");
    exclude_objects_asked_ms -= 600000;
    for (int i = 0; i < 5; i++) assert(fetch(&mqtt, body, sizeof(body)) == 200);
    assert(strstr(body, "\"excluded_objects\":[\"B\"]") && strstr(body, "\"name\":\"A\""));
    counts(&list, &status); assert(list == 2 && status == 0);

    /* Without fresh telemetry the route falls back to one query per second. */
    telemetry.last_rx.tv_sec -= 10;
    assert(fetch(&mqtt, body, sizeof(body)) == 200 && strstr(body, "\"queried\""));
    assert(fetch(&mqtt, body, sizeof(body)) == 200);
    counts(&list, &status); assert(list == 2 && status == 1);

    /* A new job asks for its own list. */
    telemetry_reports("[]", "null");
    snprintf(mqtt.filename, sizeof(mqtt.filename), "%s", "next.gcode");
    assert(fetch(&mqtt, body, sizeof(body)) == 200);
    counts(&list, &status); assert(list == 3 && status == 1);

    object_query_close();
    assert(pthread_join(thread, NULL) == 0);
    close(idle[0]); close(idle[1]); close(sockets[1]);
    puts("PASS exclude-object route: one list query per job, live state from telemetry, fallback query");
    return 0;
}
