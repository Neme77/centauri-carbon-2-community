/* Every way a G-code file reaches the printer's storage queues its layer map, a deleted
 * file takes its map with it, and the main loop's idle test gates the builder.
 * (The builder thread is not started here: the queue itself is checked.) */
#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>
#include <sys/socket.h>

static void expect(int condition, const char *msg) {
    if (!condition) { fprintf(stderr, "FAIL %s\n", msg); exit(1); }
    fprintf(stdout, "PASS %s\n", msg);
}

static int queued(const char *path) {
    pthread_mutex_lock(&layer_store_lock);
    int found = 0;
    for (int i = 0; i < layer_store_count; i++) if (!strcmp(layer_store_queue[i], path)) found = 1;
    pthread_mutex_unlock(&layer_store_lock);
    return found;
}

static int status_of(int fd) {
    char response[600];
    ssize_t size = recv(fd, response, sizeof(response) - 1, 0);
    expect(size > 0, "receive response");
    response[size] = 0;
    return atoi(strchr(response, ' ') + 1);
}

static int run_upload(mqtt_client *mqtt, const char *name, const char *content) {
    int pair[2]; expect(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0, "create upload socket");
    char req[1024];
    int len = snprintf(req, sizeof(req), "POST /api/gcode-files/upload?storage=internal&name=%s HTTP/1.1\r\nHost: test\r\nContent-Length: %zu\r\n\r\n%s",
                       name, strlen(content), content);
    size_t header = (size_t)(strstr(req, "\r\n\r\n") - req) + 4;
    char query[512]; snprintf(query, sizeof(query), "storage=internal&name=%s", name);
    int owned = gcode_upload_start(pair[0], req, query, (size_t)len, header, mqtt);
    int status = status_of(pair[1]);
    if (!owned) close(pair[0]);
    close(pair[1]);
    return status;
}

static int run_body(void (*handler)(int, const char *, const mqtt_client *, const char *, size_t),
                    void (*copy)(int, const mqtt_client *, const char *, size_t),
                    mqtt_client *mqtt, const char *body) {
    int pair[2]; expect(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0, "create socket");
    if (handler) handler(pair[0], "POST / HTTP/1.1\r\n\r\n", mqtt, body, strlen(body));
    else copy(pair[0], mqtt, body, strlen(body));
    int status = status_of(pair[1]);
    close(pair[0]); close(pair[1]);
    return status;
}

int main(void) {
    char root[] = "/tmp/cc2-layer-entries-XXXXXX";
    expect(mkdtemp(root) != NULL, "create test directory");
    char maps[600]; snprintf(maps, sizeof(maps), "%s-maps", root);
    gcode_internal_root = root; gcode_usb_root = "/tmp/cc2-usb-not-mounted";
    layer_store_dir = maps;
    mqtt_client mqtt; memset(&mqtt, 0, sizeof(mqtt));
    mqtt.connected = mqtt.registered = mqtt.have_machine_status = 1;
    mqtt.machine_status = 1; mqtt.last_message = time(NULL);

    /* Upload */
    expect(run_upload(&mqtt, "cube.gcode", ";LAYER_CHANGE\nG1 X1 Y1 E1\n") == 201, "upload into internal memory");
    char cube[700]; snprintf(cube, sizeof(cube), "%s/cube.gcode", root);
    expect(queued(cube), "an uploaded file is queued for its layer map");

    /* Delete: queue entry and stored map both go. */
    char map_path[1400]; expect(layer_store_index_path(cube, map_path, sizeof(map_path)) == 0, "map path");
    mkdir(maps, 0700);
    FILE *fake = fopen(map_path, "w"); expect(fake != NULL, "create a stored map"); fclose(fake);
    expect(run_body(gcode_delete_response, NULL, &mqtt, "internal\ncube.gcode") == 200, "delete the G-code file");
    expect(!queued(cube) && access(map_path, F_OK) != 0, "deleting a file removes its queue entry and its map");

    /* Copy and USB import need a separately mounted folder as the USB root. */
    struct stat shm_stat, dev_stat;
    int copy_import_ran = 0;
    if (stat("/dev/shm", &shm_stat) == 0 && stat("/dev", &dev_stat) == 0 && shm_stat.st_dev != dev_stat.st_dev) {
        copy_import_ran = 1;
        char usb_dir[] = "/dev/shm/cc2-layer-usb-XXXXXX";
        expect(mkdtemp(usb_dir) != NULL, "create USB-style folder");
        gcode_usb_root = usb_dir;
        char src[700]; snprintf(src, sizeof(src), "%s/part.gcode", usb_dir);
        FILE *f = fopen(src, "w"); expect(f != NULL, "write USB file"); fputs(";LAYER_CHANGE\nG1 X2 Y2 E1\n", f); fclose(f);

        expect(run_body(NULL, gcode_copy_response, &mqtt, "usb\ninternal\npart.gcode") == 201, "copy USB → internal");
        char copied[700]; snprintf(copied, sizeof(copied), "%s/part.gcode", root);
        expect(queued(copied), "a copied file is queued for its layer map");

        char imported[PATH_MAX_LOCAL];
        expect(gcode_import_usb("part.gcode", imported, NULL, NULL, NULL, 0, NULL, NULL) == 0,
               "import a USB file for printing");
        char import_path[1200]; snprintf(import_path, sizeof(import_path), "%s/%s", root, imported);
        expect(queued(import_path), "an imported USB copy is queued for its layer map");
        /* The cleanup of an internal USB copy (after the print, or after a start that failed) takes the map with it. */
        char import_map[2048];
        expect(layer_store_index_path(import_path, import_map, sizeof(import_map)) == 0, "map path of the imported copy");
        mkdir(maps, 0700);
        FILE *map_file = fopen(import_map, "w"); expect(map_file != NULL, "create the map of the imported copy");
        fputs("x", map_file); fclose(map_file);
        expect(gcode_usb_import_remove(&mqtt, imported) == 1, "cleanup removes the internal USB copy");
        expect(access(import_path, F_OK) != 0 && access(import_map, F_OK) != 0, "the cleanup takes the map with the copy");
        unlink(src); unlink(copied); unlink(import_path); rmdir(usb_dir);
    } else fputs("SKIP copy and USB import checks: /dev/shm is not a separate mount here, run this test where it is (Linux, CI)\n", stderr);

    /* The main loop's idle test is what gates the builder. */
    layer_store_set_idle(gcode_idle_for_mutation(&mqtt));
    expect(atomic_load(&layer_store_idle) == 1, "idle printer lets the builder run");
    mqtt.machine_status = 2;
    layer_store_set_idle(gcode_idle_for_mutation(&mqtt));
    expect(atomic_load(&layer_store_idle) == 0, "a printing printer stops the builder");
    mqtt.machine_status = 1; mqtt.last_message = time(NULL) - 60;
    layer_store_set_idle(gcode_idle_for_mutation(&mqtt));
    expect(atomic_load(&layer_store_idle) == 0, "stale telemetry counts as not idle");

    rmdir(maps); rmdir(root);
    if (!copy_import_ran) puts("PASS (PARTLY, copy and USB import SKIPPED) layer maps are queued by upload, removed on delete, and gated by idle");
    else puts("PASS layer maps are queued by upload, copy and USB import, removed on delete, and gated by idle");
    return 0;
}
