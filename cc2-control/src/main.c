#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <poll.h>
#include <stdint.h>
#include <sys/statvfs.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "mqtt.h"
#include "panda.h"
#include "console.h"
#include "control.h"
#include "uds.h"
#include "http_guard.h"

static uds_client telemetry;
static int z_offset_pending;
static double z_offset_expected;
static int z_offset_session;
static double z_offset_reference;
static struct timespec z_offset_started;
static int z_offset_timed_out;
static void z_offset_expire(void){
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    if(z_offset_pending && now.tv_sec-z_offset_started.tv_sec>=5){
        z_offset_pending=0;z_offset_session=0;z_offset_timed_out=1;
    }
}

static int z_offset_readback(double *value){
    z_offset_expire();
    if(!uds_value(&telemetry,U_Z_OFFSET,value))return 0;
    if(z_offset_pending&&fabs(*value-z_offset_expected)<0.0005)z_offset_pending=0;
    else if(!z_offset_pending&&z_offset_session&&fabs(*value-z_offset_expected)>=0.0005)
        z_offset_session=0; /* Firmware/display/G-code changed the reference outside our controls. */
    return 1;
}

#define REQUEST_MAX 12288
#define FILE_UPLOAD_MAX (128UL * 1024UL * 1024UL)
#define UPLOAD_CHUNK 16384
#define PATH_MAX_LOCAL 512
#define GCODE_FILES_MAX 128
#define GCODE_SCAN_DEPTH_MAX 4
#define GCODE_SCAN_ENTRIES_MAX 4096
#define GCODE_JSON_CAP (192 * 1024)
#define GCODE_TOOLS_MAX 16
#define GCODE_THUMBNAIL_SCAN_MAX (4 * 1024 * 1024)
#define GCODE_THUMBNAIL_BASE64_MAX (1024 * 1024)

#define GCODE_INTERNAL_ROOT "/opt/usr/gcode/local"
#define GCODE_USB_ROOT "/mnt/exUDISK"
#define GCODE_USB_IMPORT_PREFIX "CC2_USB_"

#ifndef CC2_CONTROL_VERSION /* the Makefile sets it from VERSION */
#define CC2_CONTROL_VERSION "0.0.0-dev"
#endif
#ifndef CC2_COMMUNITY_FIRMWARE_VERSION /* the Makefile sets it from FIRMWARE_VERSION */
#define CC2_COMMUNITY_FIRMWARE_VERSION "0.0-dev"
#endif
#define CC2_DISCOVERY_API_VERSION 1

static volatile sig_atomic_t running = 1;
static volatile sig_atomic_t first_run_restart_requested = 0;
static const char *material_presets_path = "./material-presets.json";
static const char *mqtt_config_path = "./cc2-control.conf";
static const char *ui_preferences_path = "./ui-preferences.json";
static const char *plates_path = "./bed-plates.json";
static const char *spools_path = "./spools.json";
static const char *printer_autosave_path = "/opt/usr/cfg/autosave.cfg";
static const char *gcode_internal_root = GCODE_INTERNAL_ROOT;
static const char *gcode_usb_root = GCODE_USB_ROOT;
static int setup_mode = 0;
static int service_http_port = 8081;
static const char *http_host_alias = NULL;
static int service_panda_port = 7125;
static const char default_material_presets[] =
    "[{\"name\":\"PLA\",\"nozzle\":200,\"bed\":60,\"min\":190,\"max\":230},"
    "{\"name\":\"PETG\",\"nozzle\":240,\"bed\":70,\"min\":220,\"max\":260},"
    "{\"name\":\"ABS\",\"nozzle\":250,\"bed\":100,\"min\":230,\"max\":280},"
    "{\"name\":\"ASA\",\"nozzle\":255,\"bed\":100,\"min\":240,\"max\":280},"
    "{\"name\":\"TPU\",\"nozzle\":220,\"bed\":50,\"min\":200,\"max\":240},"
    "{\"name\":\"PA-CF\",\"nozzle\":285,\"bed\":100,\"min\":260,\"max\":300}]\n";

static void stop_server(int signal_number) {
    (void)signal_number;
    running = 0;
}

static int send_all(int fd, const void *buffer, size_t length) {
    const char *cursor = (const char *)buffer;
    while (length > 0) {
        ssize_t sent = send(fd, cursor, length, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        cursor += sent;
        length -= (size_t)sent;
    }
    return 0;
}

/* Submit one atomic script to the printer's local Klippy socket. Calibrated
 * starts cannot use MQTT method 1020: that method is routed to
 * INTERNAL_START_PRINT and skips the preparation performed by LAN START_PRINT. */
static int send_local_gcode_script(const char *script) {
    char escaped[8192], request[8448];
    size_t used = 0;
    if (!script) return -1;
    for (const unsigned char *cursor = (const unsigned char *)script; *cursor; ++cursor) {
        unsigned char ch = *cursor;
        const char *replacement = NULL;
        if (ch == '"') replacement = "\\\"";
        else if (ch == '\\') replacement = "\\\\";
        else if (ch == '\n') replacement = "\\n";
        else if (ch == '\r') replacement = "\\r";
        else if (ch == '\t') replacement = "\\t";
        if (replacement) {
            size_t replacement_length = strlen(replacement);
            if (used + replacement_length >= sizeof(escaped)) return -1;
            memcpy(escaped + used, replacement, replacement_length);
            used += replacement_length;
        } else {
            if (ch < 32 || used + 1 >= sizeof(escaped)) return -1;
            escaped[used++] = (char)ch;
        }
    }
    escaped[used] = '\0';
    int length = snprintf(request, sizeof(request),
        "{\"id\":301,\"method\":\"gcode/script\",\"params\":{\"script\":\"%s\"}}\003",
        escaped);
    if (length <= 0 || (size_t)length >= sizeof(request)) return -1;

    int uds = socket(AF_UNIX, SOCK_STREAM, 0);
    if (uds < 0) return -1;
    struct timeval timeout = {2, 0};
    setsockopt(uds, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    struct sockaddr_un address; memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", "/tmp/elegoo_uds");
    int result = connect(uds, (struct sockaddr *)&address, sizeof(address));
    if (result == 0) result = send_all(uds, request, (size_t)length);
    close(uds);
    return result;
}

static int saved_plate_mesh_exists(char print_layout) {
    const char *profile = print_layout == 'B' ? "default1" : "default";
    FILE *file = fopen(printer_autosave_path, "rb");
    if (!file) return 0;
    char line[256], marker[64];
    snprintf(marker, sizeof(marker), "[bed_mesh %s]", profile);
    int in_profile = 0, x_count = 0, y_count = 0, have_points = 0;
    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, "[bed_mesh ")) {
            if (in_profile) break;
            in_profile = strstr(line, marker) != NULL;
            continue;
        }
        if (!in_profile) continue;
        if (strstr(line, "points =")) have_points = 1;
        const char *x = strstr(line, "x_count =");
        const char *y = strstr(line, "y_count =");
        if (x) x_count = atoi(x + strlen("x_count ="));
        if (y) y_count = atoi(y + strlen("y_count ="));
    }
    fclose(file);
    return have_points && x_count == 11 && y_count == 11;
}

static void respond(int fd, int status, const char *status_text,
                    const char *content_type, const char *body, size_t body_len) {
    char header[512];
    int header_len = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n"
        "Cache-Control: no-store\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "X-Frame-Options: DENY\r\n"
        "Connection: close\r\n\r\n",
        status, status_text, content_type, (unsigned long)body_len);
    if (header_len <= 0 || (size_t)header_len >= sizeof(header)) return;
    if (send_all(fd, header, (size_t)header_len) == 0)
        (void)send_all(fd, body, body_len);
}

static long read_first_long(const char *path) {
    FILE *file = fopen(path, "r");
    long value = -1;
    if (file) {
        if (fscanf(file, "%ld", &value) != 1) value = -1;
        fclose(file);
    }
    return value;
}

static void memory_values(long *total, long *available) {
    FILE *file = fopen("/proc/meminfo", "r");
    char key[64];
    long value;
    char unit[32];
    *total = -1;
    *available = -1;
    if (!file) return;
    while (fscanf(file, "%63s %ld %31s", key, &value, unit) == 3) {
        if (strcmp(key, "MemTotal:") == 0) *total = value;
        else if (strcmp(key, "MemAvailable:") == 0) *available = value;
        if (*total >= 0 && *available >= 0) break;
    }
    fclose(file);
}

static void health_response(int fd, const mqtt_client *mqtt) {
    char body[768];
    char load[96] = "unknown";
    long uptime = read_first_long("/proc/uptime");
    long mem_total, mem_available;
    FILE *load_file = fopen("/proc/loadavg", "r");
    if (load_file) {
        if (!fgets(load, sizeof(load), load_file)) strcpy(load, "unknown");
        fclose(load_file);
        load[strcspn(load, "\r\n")] = '\0';
    }
    memory_values(&mem_total, &mem_available);
    int length = snprintf(body, sizeof(body),
        "{\"service\":\"cc2-control\",\"version\":\"" CC2_CONTROL_VERSION "\","
        "\"mode\":\"protected-control\",\"uptime_seconds\":%ld,"
        "\"mem_total_kb\":%ld,\"mem_available_kb\":%ld,"
        "\"loadavg\":\"%s\",\"mqtt_connected\":%s,\"mqtt_registered\":%s,"
        "\"snapshot_received\":%s,\"mqtt_received_publishes\":%lu,\"mqtt_skipped_requests\":%lu,\"mqtt_skipped_request_bytes\":%lu,\"mqtt_oversized_packets\":%lu,\"mqtt_oversized_snapshots\":%lu,\"mqtt_oversized_replies\":%lu}\n",
        uptime, mem_total, mem_available, load, mqtt->connected ? "true" : "false",
        mqtt->registered ? "true" : "false", mqtt->snapshot_len ? "true" : "false",
        mqtt->received_publishes,mqtt->skipped_requests,mqtt->skipped_request_bytes,mqtt->oversized_packets,mqtt->oversized_snapshots,mqtt->oversized_replies);
    if (length < 0 || (size_t)length >= sizeof(body)) return;
    respond(fd, 200, "OK", "application/json; charset=utf-8", body, (size_t)length);
}

static void json_escape(char *out,size_t cap,const char *in);

static void system_info_response(int fd, const mqtt_client *mqtt) {
    char body[3072], uuid[260];
    json_escape(uuid, sizeof(uuid), mqtt ? mqtt->uuid : "");
    int length = snprintf(body, sizeof(body),
        "{"
        "\"platform\":\"cc2-community\","
        "\"implementation\":\"Neme77/centauri-carbon-2-community\","
        "\"device\":\"ELEGOO Centauri Carbon 2\","
        "\"community_firmware\":\"%s\","
        "\"cc2_control\":\"%s\","
        "\"api_version\":%d,"
        "\"printer_uuid\":\"%s\","
        "\"services\":{"
            "\"cc2_control\":%d,"
            "\"moonraker_compat\":%d"
        "},"
        "\"capabilities\":{"
            "\"mqtt\":true,"
            "\"camera\":true,"
            "\"canvas\":true,"
            "\"file_manager\":true,"
            "\"gcode_thumbnails\":true,"
            "\"object_exclusion\":true,"
            "\"gcode_console\":true,"
            "\"orca_upload\":true,"
            "\"orca_print\":true,"
            "\"panda_compat\":%s,"
            "\"material_presets\":true,"
            "\"persistent_ui_preferences\":true,"
            "\"print_history\":true,"
            "\"canvas_auto_refill\":true,"
            "\"bed_plates\":true,"
            "\"spool_tracking\":true"
        "},"
        "\"endpoints\":{"
            "\"printer\":\"/api/printer\","
            "\"health\":\"/api/health\","
            "\"canvas\":\"/api/canvas\","
            "\"files\":\"/api/gcode-files\","
            "\"console\":\"/api/console\","
            "\"preferences\":\"/api/preferences\","
            "\"history\":\"/api/history\","
            "\"plates\":\"/api/plates\","
            "\"spools\":\"/api/spools\","
            "\"discovery\":\"/api/v1/system/info\""
        "},"
        "\"runtime\":{"
            "\"mqtt_connected\":%s,"
            "\"mqtt_registered\":%s"
        "}"
        "}\n",
        CC2_COMMUNITY_FIRMWARE_VERSION,
        CC2_CONTROL_VERSION,
        CC2_DISCOVERY_API_VERSION,
        uuid,
        service_http_port,
        service_panda_port,
        service_panda_port > 0 ? "true" : "false",
        mqtt && mqtt->connected ? "true" : "false",
        mqtt && mqtt->registered ? "true" : "false");
    if (length > 0 && (size_t)length < sizeof(body))
        respond(fd, 200, "OK", "application/json; charset=utf-8", body, (size_t)length);
}

static void json_number(char *out, size_t cap, int have, double value) {
    if (have) snprintf(out, cap, "%.1f", value);
    else snprintf(out, cap, "null");
}

static void json_z_offset(char *out, size_t cap, int have, double value) {
    if (have) snprintf(out, cap, "%.3f", fabs(value) < 0.0005 ? 0.0 : value);
    else snprintf(out, cap, "null");
}

static void json_escape(char *out,size_t cap,const char *in) {
    size_t n=0;
    while(*in&&n+1<cap){unsigned char ch=(unsigned char)*in++;
        if((ch=='"'||ch=='\\')&&n+2<cap){out[n++]='\\';out[n++]=(char)ch;}
        else if(ch>=32)out[n++]=(char)ch;}
    out[n]='\0';
}

typedef struct {
    char relative_path[PATH_MAX_LOCAL];
    long long size;
    long long modified;
} gcode_file_entry;

typedef struct {
    gcode_file_entry files[GCODE_FILES_MAX];
    size_t count;
    size_t total;   /* G-code files found, listed or not */
    size_t scanned; /* directory entries examined */
    int available;
    int truncated;
} gcode_file_list;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    int failed;
} json_builder;

static void json_builder_printf(json_builder *builder, const char *format, ...) {
    if (builder->failed || builder->length >= builder->capacity) return;
    va_list args;
    va_start(args, format);
    int written = vsnprintf(builder->data + builder->length,
                            builder->capacity - builder->length, format, args);
    va_end(args);
    if (written < 0 || (size_t)written >= builder->capacity - builder->length) {
        builder->failed = 1;
        return;
    }
    builder->length += (size_t)written;
}

static void json_builder_string(json_builder *builder, const char *value) {
    json_builder_printf(builder, "\"");
    for (const unsigned char *cursor = (const unsigned char *)value;
         *cursor && !builder->failed; ++cursor) {
        unsigned char ch = *cursor;
        if (ch == '"' || ch == '\\') json_builder_printf(builder, "\\%c", ch);
        else if (ch < 32) json_builder_printf(builder, "\\u%04x", (unsigned int)ch);
        else json_builder_printf(builder, "%c", ch);
    }
    json_builder_printf(builder, "\"");
}

static int is_gcode_filename(const char *name) {
    size_t length = strlen(name);
    return length > 6 && strcasecmp(name + length - 6, ".gcode") == 0;
}

static int safe_relative_path(const char *relative) {
    if (!relative[0] || relative[0] == '/' || strchr(relative, '\\')) return 0;
    const char *segment = relative;
    for (const char *cursor = relative; ; ++cursor) {
        unsigned char ch = (unsigned char)*cursor;
        if (ch && ch < 32) return 0;
        if (ch == '/' || ch == '\0') {
            size_t length = (size_t)(cursor - segment);
            if (!length || (length == 1 && segment[0] == '.') ||
                (length == 2 && segment[0] == '.' && segment[1] == '.')) return 0;
            if (!ch) break;
            segment = cursor + 1;
        }
    }
    return 1;
}

static int compare_gcode_files(const void *left, const void *right) {
    const gcode_file_entry *a = (const gcode_file_entry *)left;
    const gcode_file_entry *b = (const gcode_file_entry *)right;
    if (a->modified != b->modified) return a->modified < b->modified ? 1 : -1;
    return strcasecmp(a->relative_path, b->relative_path);
}

/* Keep the newest GCODE_FILES_MAX files in whatever order readdir returns
 * them. On ext4 that order follows name hashes, so keeping the first files
 * found would hide arbitrary ones, often the latest upload. */
static void add_gcode_file(gcode_file_list *list, const char *relative,
                           const struct stat *status) {
    gcode_file_entry candidate;
    snprintf(candidate.relative_path, sizeof(candidate.relative_path), "%s", relative);
    candidate.size = (long long)status->st_size;
    candidate.modified = (long long)status->st_mtime;
    list->total++;
    if (list->count < GCODE_FILES_MAX) {
        list->files[list->count++] = candidate;
        return;
    }
    list->truncated = 1;
    size_t oldest = 0;
    for (size_t index = 1; index < list->count; ++index)
        if (compare_gcode_files(&list->files[index], &list->files[oldest]) > 0) oldest = index;
    if (compare_gcode_files(&candidate, &list->files[oldest]) < 0) list->files[oldest] = candidate;
}

static void scan_gcode_directory(const char *root, const char *relative,
                                 unsigned int depth, gcode_file_list *list) {
    if (depth > GCODE_SCAN_DEPTH_MAX || list->scanned >= GCODE_SCAN_ENTRIES_MAX) {
        list->truncated = 1;
        return;
    }
    char directory_path[PATH_MAX_LOCAL * 2];
    int directory_length = relative[0]
        ? snprintf(directory_path, sizeof(directory_path), "%s/%s", root, relative)
        : snprintf(directory_path, sizeof(directory_path), "%s", root);
    if (directory_length < 0 || (size_t)directory_length >= sizeof(directory_path)) {
        list->truncated = 1;
        return;
    }
    DIR *directory = opendir(directory_path);
    if (!directory) return;
    struct dirent *item;
    while ((item = readdir(directory)) != NULL) {
        if (item->d_name[0] == '.') continue;
        if (list->scanned >= GCODE_SCAN_ENTRIES_MAX) {
            list->truncated = 1;
            break;
        }
        list->scanned++;
        char child_relative[PATH_MAX_LOCAL];
        int relative_length = relative[0]
            ? snprintf(child_relative, sizeof(child_relative), "%s/%s", relative, item->d_name)
            : snprintf(child_relative, sizeof(child_relative), "%s", item->d_name);
        if (relative_length < 0 || (size_t)relative_length >= sizeof(child_relative)) {
            list->truncated = 1;
            continue;
        }
        char child_path[PATH_MAX_LOCAL * 2];
        int path_length = snprintf(child_path, sizeof(child_path), "%s/%s", root, child_relative);
        if (path_length < 0 || (size_t)path_length >= sizeof(child_path)) {
            list->truncated = 1;
            continue;
        }
        struct stat status;
        if (lstat(child_path, &status) != 0 || S_ISLNK(status.st_mode)) continue;
        if (S_ISDIR(status.st_mode)) {
            if (depth < GCODE_SCAN_DEPTH_MAX)
                scan_gcode_directory(root, child_relative, depth + 1, list);
            else list->truncated = 1;
        } else if (S_ISREG(status.st_mode) && is_gcode_filename(item->d_name) &&
                   safe_relative_path(child_relative)) {
            add_gcode_file(list, child_relative, &status);
        }
    }
    closedir(directory);
}

static void collect_gcode_files(const char *root, gcode_file_list *list) {
    memset(list, 0, sizeof(*list));
    struct stat status;
    if (stat(root, &status) != 0 || !S_ISDIR(status.st_mode)) return;
    DIR *probe = opendir(root);
    if (!probe) return;
    closedir(probe);
    list->available = 1;
    scan_gcode_directory(root, "", 0, list);
    qsort(list->files, list->count, sizeof(list->files[0]), compare_gcode_files);
}

static void append_gcode_storage(json_builder *builder, const char *name,
                                 const gcode_file_list *list) {
    json_builder_string(builder, name);
    json_builder_printf(builder, ":{\"available\":%s,\"truncated\":%s,\"count\":%lu,\"total\":%lu,\"files\":[",
                        list->available ? "true" : "false",
                        list->truncated ? "true" : "false",
                        (unsigned long)list->count, (unsigned long)list->total);
    for (size_t index = 0; index < list->count; ++index) {
        const gcode_file_entry *entry = &list->files[index];
        if (index) json_builder_printf(builder, ",");
        json_builder_printf(builder, "{\"path\":");
        json_builder_string(builder, entry->relative_path);
        json_builder_printf(builder, ",\"size\":%lld,\"modified\":%lld}",
                            entry->size, entry->modified);
    }
    json_builder_printf(builder, "]}");
}

static void gcode_files_response(int fd) {
    gcode_file_list *internal_files = calloc(1, sizeof(*internal_files));
    gcode_file_list *usb_files = calloc(1, sizeof(*usb_files));
    char *body = malloc(GCODE_JSON_CAP);
    if (!internal_files || !usb_files || !body) {
        const char *error = "{\"error\":\"Out of memory\"}\n";
        respond(fd, 503, "Service Unavailable", "application/json; charset=utf-8",
                error, strlen(error));
        free(internal_files);
        free(usb_files);
        free(body);
        return;
    }
    collect_gcode_files(gcode_internal_root, internal_files);
    collect_gcode_files(gcode_usb_root, usb_files);
    json_builder builder = { body, 0, GCODE_JSON_CAP, 0 };
    json_builder_printf(&builder, "{");
    append_gcode_storage(&builder, "internal", internal_files);
    json_builder_printf(&builder, ",");
    append_gcode_storage(&builder, "usb", usb_files);
    json_builder_printf(&builder, "}\n");
    if (builder.failed) {
        const char *error = "{\"error\":\"File list is too large\"}\n";
        respond(fd, 507, "Insufficient Storage", "application/json; charset=utf-8",
                error, strlen(error));
    } else {
        respond(fd, 200, "OK", "application/json; charset=utf-8", body, builder.length);
    }
    free(internal_files);
    free(usb_files);
    free(body);
}

static int gcode_file_is_printable(const char *root, const char *relative) {
    if (!safe_relative_path(relative) || !is_gcode_filename(relative)) return 0;
    char current[PATH_MAX_LOCAL * 2];
    int root_length = snprintf(current, sizeof(current), "%s", root);
    if (root_length < 0 || (size_t)root_length >= sizeof(current)) return 0;
    const char *segment = relative;
    while (*segment) {
        const char *slash = strchr(segment, '/');
        size_t length = slash ? (size_t)(slash - segment) : strlen(segment);
        size_t used = strlen(current);
        if (used + 1 + length >= sizeof(current)) return 0;
        current[used++] = '/';
        memcpy(current + used, segment, length);
        current[used + length] = '\0';
        struct stat status;
        if (lstat(current, &status) != 0 || S_ISLNK(status.st_mode)) return 0;
        if (slash) {
            if (!S_ISDIR(status.st_mode)) return 0;
            segment = slash + 1;
        } else {
            return S_ISREG(status.st_mode);
        }
    }
    return 0;
}

static int gcode_resolved_path(const char *root, const char *relative,
                               char *path, size_t capacity) {
    if (!gcode_file_is_printable(root, relative)) return 0;
    int length = snprintf(path, capacity, "%s/%s", root, relative);
    return length >= 0 && (size_t)length < capacity;
}


static int gcode_request_file(const char *body, size_t body_len,
                              char storage[16], char filename[PATH_MAX_LOCAL],
                              const char **extra, size_t *extra_len,
                              const char **root, const char **media);
static size_t content_length_from_headers(const char *request);

static int gcode_storage_root(const char *storage, const char **root) {
    if(strcmp(storage,"internal")==0) {*root=gcode_internal_root;return 1;}
    if(strcmp(storage,"usb")==0) {*root=gcode_usb_root;return 1;}
    return 0;
}

#include "gcode_download.h"

/* USB root must actually be a mounted removable filesystem, not a plain
 * directory on internal flash left behind when the drive is unplugged. */
static int gcode_storage_writable(const char *storage, const char *root) {
    struct stat st,parent;
    if(stat(root,&st)!=0||!S_ISDIR(st.st_mode))return 0;
    if(strcmp(storage,"usb")==0){
        char upper[PATH_MAX_LOCAL*2];
        if(snprintf(upper,sizeof(upper),"%s/..",root)>=(int)sizeof(upper)||
           stat(upper,&parent)!=0||st.st_dev==parent.st_dev)return 0;
    }
    return access(root,W_OK|X_OK)==0;
}

static int gcode_idle_for_mutation(const mqtt_client *mqtt){
    return mqtt->connected && mqtt->registered && mqtt->have_machine_status &&
        mqtt->machine_status==1 && mqtt->last_message>0 &&
        time(NULL)-mqtt->last_message<=15;
}

/* Uploads publish new files without changing the active job. */
static int gcode_ready_for_upload(const mqtt_client *mqtt){
    return mqtt->connected && mqtt->registered && mqtt->have_machine_status &&
        (mqtt->machine_status==1 || mqtt->machine_status==2) &&
        mqtt->last_message>0 && time(NULL)-mqtt->last_message<=15;
}

static void gcode_delete_response(int fd,const char *request,const mqtt_client *mqtt,
                                  const char *body,size_t length){
    (void)request;
    if(!gcode_idle_for_mutation(mqtt)){
        const char *e="{\"error\":\"Printer must be idle and connected\"}\n";
        respond(fd,409,"Conflict","application/json",e,strlen(e));return;
    }
    char storage[16],file[PATH_MAX_LOCAL];const char *root,*media;
    if(!gcode_request_file(body,length,storage,file,NULL,NULL,&root,&media)||
       !gcode_storage_writable(storage,root)){
        const char *e="{\"error\":\"Storage unavailable or not writable\"}\n";
        respond(fd,400,"Bad Request","application/json",e,strlen(e));return;
    }
    (void)media;
    char path[PATH_MAX_LOCAL*2];
    if(!gcode_resolved_path(root,file,path,sizeof(path))){
        const char *e="{\"error\":\"G-code file not found or unsafe\"}\n";
        respond(fd,404,"Not Found","application/json",e,strlen(e));return;
    }
    /* Recheck direct filename and block hard-linked content. */
    struct stat st;
    if(lstat(path,&st)!=0||!S_ISREG(st.st_mode)||st.st_nlink!=1||
       unlink(path)!=0){
        const char *e="{\"error\":\"Cannot delete this file\"}\n";
        respond(fd,409,"Conflict","application/json",e,strlen(e));return;
    }
    const char *ok="{\"deleted\":true}\n";
    respond(fd,200,"OK","application/json",ok,strlen(ok));
}

static void gcode_copy_response(int fd,const mqtt_client *mqtt,const char *body,size_t length){
    if(!gcode_idle_for_mutation(mqtt)){
        const char *e="{\"error\":\"Printer must be idle and connected\"}\n";
        respond(fd,409,"Conflict","application/json",e,strlen(e));return;
    }
    char source_storage[16],destination_storage[16],file[PATH_MAX_LOCAL];
    const char *first=memchr(body,'\n',length);if(!first)goto invalid;
    const char *second=memchr(first+1,'\n',length-(size_t)(first+1-body));if(!second)goto invalid;
    size_t a=(size_t)(first-body),b=(size_t)(second-first-1),c=length-(size_t)(second+1-body);
    while(c&&isspace((unsigned char)second[1+c-1]))c--;
    if(!a||a>=sizeof(source_storage)||!b||b>=sizeof(destination_storage)||!c||c>=sizeof(file))goto invalid;
    memcpy(source_storage,body,a);source_storage[a]=0;memcpy(destination_storage,first+1,b);destination_storage[b]=0;memcpy(file,second+1,c);file[c]=0;
    if(strcmp(source_storage,destination_storage)==0)goto invalid;
    const char *source_root,*destination_root;
    if(!gcode_storage_root(source_storage,&source_root)||!gcode_storage_root(destination_storage,&destination_root)||
       !gcode_storage_writable(destination_storage,destination_root))goto invalid;
    char source[PATH_MAX_LOCAL*2],destination[PATH_MAX_LOCAL*2],temporary[PATH_MAX_LOCAL*2];
    if(!gcode_resolved_path(source_root,file,source,sizeof(source)))goto missing;
    const char *base=strrchr(file,'/');base=base?base+1:file;
    if(!safe_relative_path(base)||!is_gcode_filename(base)||
       snprintf(destination,sizeof(destination),"%s/%s",destination_root,base)>=(int)sizeof(destination)||
       snprintf(temporary,sizeof(temporary),"%s.part",destination)>=(int)sizeof(temporary))goto invalid;
    struct stat st;if(lstat(source,&st)!=0||!S_ISREG(st.st_mode)||S_ISLNK(st.st_mode))goto missing;
    if(lstat(destination,&st)==0||errno!=ENOENT){const char *e="{\"error\":\"Destination file already exists\"}\n";respond(fd,409,"Conflict","application/json",e,strlen(e));return;}
    FILE *in=fopen(source,"rb"),*out=NULL;int failed=0;if(!in)goto missing;
    out=fopen(temporary,"wb");if(!out){fclose(in);goto copy_failed;}
    char buffer[65536];for(;;){size_t n=fread(buffer,1,sizeof(buffer),in);if(n&&fwrite(buffer,1,n,out)!=n){failed=1;break;}if(n<sizeof(buffer)){if(ferror(in))failed=1;break;}}
    if(!failed&&fflush(out)!=0)failed=1;
    if(!failed&&fsync(fileno(out))!=0)failed=1;
    if(fclose(out)!=0)failed=1;
    fclose(in);
    if(failed||rename(temporary,destination)!=0){unlink(temporary);goto copy_failed;}
    {const char *ok="{\"copied\":true}\n";respond(fd,201,"Created","application/json",ok,strlen(ok));return;}
invalid:{const char *e="{\"error\":\"Invalid copy request or unavailable storage\"}\n";respond(fd,400,"Bad Request","application/json",e,strlen(e));return;}
missing:{const char *e="{\"error\":\"Source G-code file not found or unsafe\"}\n";respond(fd,404,"Not Found","application/json",e,strlen(e));return;}
copy_failed:{const char *e="{\"error\":\"Cannot copy this file\"}\n";respond(fd,409,"Conflict","application/json",e,strlen(e));return;}
}

/* A small URL parser: storage and basename are encoded independently; never
 * allow directories or URL-decoded path separators in an upload filename. */
static int gcode_decode_name(char *out,size_t cap,const char *encoded){
    size_t n=0;
    for(size_t i=0;encoded[i];i++){
        unsigned char ch=(unsigned char)encoded[i];
        if(ch=='%'){
            int a,b;char x=encoded[++i],y=x?encoded[++i]:0;
            if(!x||!y)return 0;
            a=x>='0'&&x<='9'?x-'0':x>='A'&&x<='F'?x-'A'+10:x>='a'&&x<='f'?x-'a'+10:-1;
            b=y>='0'&&y<='9'?y-'0':y>='A'&&y<='F'?y-'A'+10:y>='a'&&y<='f'?y-'a'+10:-1;
            if(a<0||b<0)return 0;
            ch=(unsigned char)((a<<4)|b);
        }
        if(ch<32||ch==127||ch=='/'||ch=='\\'||ch=='?'||ch=='#'||ch==':')return 0;
        if(n+1>=cap)return 0;
        out[n++]=(char)ch;
    }
    out[n]=0;
    return n>6&&strcmp(out,".")!=0&&strcmp(out,"..")!=0&&is_gcode_filename(out);
}

static int gcode_upload_query(const char *query,char storage[16],char name[PATH_MAX_LOCAL]){
    if(!query||strncmp(query,"storage=",8)!=0)return 0;
    const char *separator=strstr(query+8,"&name=");
    if(!separator||(size_t)(separator-(query+8))>=16)return 0;
    memcpy(storage,query+8,(size_t)(separator-(query+8)));
    storage[separator-(query+8)]=0;
    return gcode_decode_name(name,PATH_MAX_LOCAL,separator+6);
}

typedef struct {
    int fd;
    unsigned char initial[REQUEST_MAX+1];
    size_t initial_length;
    size_t header_length;
    size_t content_length;
    char storage[16];
    char name[PATH_MAX_LOCAL];
    const mqtt_client *mqtt;
    struct timespec started;
} gcode_upload_job;
#define UPLOAD_TOTAL_SECONDS 300
#define UPLOAD_STORAGE_MARGIN (16ULL*1024*1024)
static int (*upload_statvfs)(const char *,struct statvfs *)=statvfs;
static int upload_has_space(const char *root,size_t length,unsigned copies){
    struct statvfs st;
    if(upload_statvfs(root,&st))return 0;
    uint64_t block=st.f_frsize?st.f_frsize:st.f_bsize;
    if(!block)return 0;
    uint64_t available=st.f_bavail>UINT64_MAX/block?UINT64_MAX:(uint64_t)st.f_bavail*block;
    uint64_t required=(uint64_t)length*copies+UPLOAD_STORAGE_MARGIN;
    return available>=required;
}
static int upload_space_guard(int fd,const char *root,size_t length,unsigned copies){
    if(upload_has_space(root,length,copies))return 1;
    const char *error="{\"error\":\"Insufficient free space for upload and storage reserve\"}\n";
    respond(fd,507,"Insufficient Storage","application/json",error,strlen(error));return 0;
}
static int upload_seconds_left(const gcode_upload_job *job){
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    long elapsed=now.tv_sec-job->started.tv_sec;
    return elapsed>=UPLOAD_TOTAL_SECONDS?0:UPLOAD_TOTAL_SECONDS-(int)elapsed;
}
static ssize_t upload_receive(gcode_upload_job *job,void *buffer,size_t length){
    for(;;){
        int left=upload_seconds_left(job);if(!left){errno=ETIMEDOUT;return -1;}
        struct pollfd p={job->fd,POLLIN,0};
        int ready=poll(&p,1,(left<20?left:20)*1000);
        if(ready<0&&errno==EINTR)continue;
        if(ready<=0){errno=ETIMEDOUT;return -1;}
        ssize_t n=recv(job->fd,buffer,length,MSG_DONTWAIT);
        if(n<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;
        return n;
    }
}
/* Read and drop the rest of a rejected body before answering. Closing a
 * socket with unread input resets the connection, and a browser then reports
 * a network error instead of the response. */
static void upload_discard(gcode_upload_job *job,size_t received,unsigned char *buffer,size_t capacity){
    while(received<job->content_length){
        size_t want=job->content_length-received;
        if(want>capacity)want=capacity;
        ssize_t n=upload_receive(job,buffer,want);
        if(n<=0)return;
        received+=(size_t)n;
    }
}
/* One upload at a time. The connection thread takes the slot and the upload
 * worker releases it, so this is an atomic flag: a mutex must be unlocked by
 * the thread that locked it. */
static atomic_int upload_busy=0;
static int upload_slot_acquire(void){int expected=0;return atomic_compare_exchange_strong(&upload_busy,&expected,1);}
static void upload_slot_release(void){atomic_store(&upload_busy,0);}
/* One print request may await the operator's Canvas mapping. Never print from
 * the upload worker: the existing protected print route remains authoritative. */
static pthread_mutex_t orca_pending_mutex=PTHREAD_MUTEX_INITIALIZER;
static char orca_pending_filename[PATH_MAX_LOCAL];
static unsigned long orca_pending_generation=0;
static time_t orca_pending_created=0;

static void orca_pending_expire_locked(void){
    if(orca_pending_filename[0] &&
       (time(NULL)<orca_pending_created || time(NULL)-orca_pending_created>3600))
        orca_pending_filename[0]=0;
}
static void orca_pending_response(int fd){
    char filename[PATH_MAX_LOCAL*2],body[PATH_MAX_LOCAL*3];
    unsigned long generation=0;
    pthread_mutex_lock(&orca_pending_mutex);
    orca_pending_expire_locked();
    if(orca_pending_filename[0]){
        json_escape(filename,sizeof(filename),orca_pending_filename);
        generation=orca_pending_generation;
    }else filename[0]=0;
    pthread_mutex_unlock(&orca_pending_mutex);
    int n=snprintf(body,sizeof(body),
      "{\"pending\":%s,\"filename\":\"%s\",\"generation\":%lu}\n",
      generation?"true":"false",filename,generation);
    if(n>0 && (size_t)n<sizeof(body))respond(fd,200,"OK","application/json",body,(size_t)n);
}
static void orca_pending_clear_response(int fd,const char *body,size_t length){
    char number[32];
    if(!length||length>=sizeof(number)){
        static const char error[]="{\"error\":\"Invalid generation\"}\n";
        respond(fd,400,"Bad Request","application/json",error,sizeof(error)-1);return;
    }
    memcpy(number,body,length);number[length]=0;
    char *end=NULL;errno=0;
    unsigned long generation=strtoul(number,&end,10);
    if(errno||!generation||end==number||*end){
        static const char error[]="{\"error\":\"Invalid generation\"}\n";
        respond(fd,400,"Bad Request","application/json",error,sizeof(error)-1);return;
    }
    pthread_mutex_lock(&orca_pending_mutex);
    orca_pending_expire_locked();
    int cleared=orca_pending_filename[0] && generation==orca_pending_generation;
    if(cleared)orca_pending_filename[0]=0;
    pthread_mutex_unlock(&orca_pending_mutex);
    static const char ok[]="{\"cleared\":true}\n";
    static const char conflict[]="{\"error\":\"Print request no longer pending\"}\n";
    respond(fd,cleared?200:409,cleared?"OK":"Conflict","application/json",
            cleared?ok:conflict,cleared?sizeof(ok)-1:sizeof(conflict)-1);
}


static void *gcode_upload_worker(void *arg){
    gcode_upload_job *job=arg;
    const char *root=NULL;
    const char *error=NULL;
    int code=400;const char *status="Bad Request";
    int output=-1;char destination[PATH_MAX_LOCAL*2],temporary[PATH_MAX_LOCAL*2];
    temporary[0]=0;
    size_t received=job->initial_length-job->header_length;
    unsigned char chunk[UPLOAD_CHUNK];
    if(!gcode_storage_root(job->storage,&root)||!gcode_storage_writable(job->storage,root)){
        error="Storage unavailable or not writable";goto done;
    }
    if(!job->content_length||job->content_length>FILE_UPLOAD_MAX){
        error="Invalid upload size";goto done;
    }
    if(snprintf(destination,sizeof(destination),"%s/%s",root,job->name)>=(int)sizeof(destination)){
        error="File name too long";goto done;
    }
    /* Existing files are never overwritten, and symlinks are not followed. */
    struct stat st;
    if(lstat(destination,&st)==0){code=409;status="Conflict";error="File already exists";goto done;}
    if(errno!=ENOENT){error="Cannot inspect destination";goto done;}
    /* The temp filename must not end in .gcode, so the UI cannot list it. */
    if(snprintf(temporary,sizeof(temporary),"%s/.cc2-upload-XXXXXX",root)>=(int)sizeof(temporary)){
        error="Temporary path too long";goto done;
    }
    if(!upload_has_space(root,job->content_length,1)){code=507;status="Insufficient Storage";error="Insufficient free space";goto done;}
    output=mkstemp(temporary);
    if(output<0){code=507;status="Insufficient Storage";error="Cannot create temporary file";goto done;}
    (void)fchmod(output,0644);
    if(received>job->content_length){error="Invalid upload body";goto done;}
    {size_t offset=0;
     while(offset<received){
        ssize_t n=write(output,job->initial+job->header_length+offset,received-offset);
        if(n<0&&errno==EINTR)continue;
        if(n<=0){code=507;status="Insufficient Storage";error="Cannot write upload";goto done;}
        offset+=(size_t)n;
     }}
    while(received<job->content_length){
        size_t required=job->content_length-received;
        if(required>sizeof(chunk))required=sizeof(chunk);
        ssize_t n=upload_receive(job,chunk,required);
        if(n<0&&errno==EINTR)continue;
        if(n<=0){if(n<0&&errno==ETIMEDOUT){code=408;status="Request Timeout";}error="Upload interrupted or timed out";goto done;}
        received+=(size_t)n;
        size_t offset=0;
        while(offset<(size_t)n){
            ssize_t written=write(output,chunk+offset,(size_t)n-offset);
            if(written<0&&errno==EINTR)continue;
            if(written<=0){code=507;status="Insufficient Storage";error="Cannot write upload";goto done;}
            offset+=(size_t)written;
        }
    }
    if(!upload_seconds_left(job)){code=408;status="Request Timeout";error="Upload deadline exceeded";goto done;}
    if(fsync(output)!=0){code=507;status="Insufficient Storage";error="Cannot finalize upload";goto done;}
    if(close(output)!=0){output=-1;error="Cannot close uploaded file";goto done;}
    output=-1;
    /* FAT/exFAT USB drives do not support hard links. Atomically reserve the
     * destination name using O_EXCL, then rename the completed temp file. */
    int reservation=open(destination,O_WRONLY|O_CREAT|O_EXCL,0600);
    if(reservation<0){
        code=errno==EEXIST?409:507;
        status=code==409?"Conflict":"Insufficient Storage";
        error=code==409?"File already exists":"Cannot reserve uploaded file";goto done;
    }
    close(reservation);
    if(rename(temporary,destination)!=0){
        unlink(destination);
        code=507;status="Insufficient Storage";
        error="Cannot publish uploaded file";goto done;
    }
    temporary[0]=0;
    code=201;status="Created";
done:
    if(output>=0)close(output);
    if(temporary[0])unlink(temporary);
    /* A rejected body is drained while the slot is still held, so only one
     * drain runs at a time; a stalled client (408) is not waited for again. */
    if(code!=201&&code!=408)upload_discard(job,received,chunk,sizeof(chunk));
    /* Free the upload slot before answering: a client may start its next
     * upload as soon as it reads this response. */
    upload_slot_release();
    {char body[220];int n=code==201?snprintf(body,sizeof(body),"{\"uploaded\":true}\n"):
        snprintf(body,sizeof(body),"{\"error\":\"%s\"}\n",error?error:"Upload failed");
     respond(job->fd,code,status,"application/json",body,(size_t)n);}
    close(job->fd);free(job);
    return NULL;
}

static int gcode_upload_start(int fd,const char *request,const char *query,
                              size_t used,size_t header_length,const mqtt_client *mqtt){
    if(!gcode_ready_for_upload(mqtt)){
        const char *e="{\"error\":\"Printer must be idle or printing and connected\"}\n";
        respond(fd,409,"Conflict","application/json",e,strlen(e));return 0;
    }
    char storage[16],name[PATH_MAX_LOCAL];
    if(!gcode_upload_query(query,storage,name)){
        const char *e="{\"error\":\"Invalid storage or G-code name\"}\n";
        respond(fd,400,"Bad Request","application/json",e,strlen(e));return 0;
    }
    const char *root;
    if(!gcode_storage_root(storage,&root)||!gcode_storage_writable(storage,root)){
        const char *e="{\"error\":\"Storage unavailable or not writable\"}\n";
        respond(fd,400,"Bad Request","application/json",e,strlen(e));return 0;
    }
    size_t len=content_length_from_headers(request);
    if(!len||len>FILE_UPLOAD_MAX||header_length>used||used-header_length>len){
        const char *e="{\"error\":\"Invalid upload size (maximum 128 MiB)\"}\n";
        respond(fd,413,"Payload Too Large","application/json",e,strlen(e));return 0;
    }
    if(!upload_space_guard(fd,root,len,1))return 0;
    if(!upload_slot_acquire()){
        const char *e="{\"error\":\"Another upload is running\"}\n";
        respond(fd,409,"Conflict","application/json",e,strlen(e));return 0;
    }
    gcode_upload_job *job=calloc(1,sizeof(*job));
    if(!job){upload_slot_release();return 0;}
    clock_gettime(CLOCK_MONOTONIC,&job->started);
    job->fd=fd;job->initial_length=used;job->header_length=header_length;
    job->content_length=len;memcpy(job->initial,request,used);
    snprintf(job->storage,sizeof(job->storage),"%s",storage);
    snprintf(job->name,sizeof(job->name),"%s",name);
    struct timeval timeout={20,0};
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    pthread_t worker;
    if(pthread_create(&worker,NULL,gcode_upload_worker,job)!=0){
        free(job);upload_slot_release();return 0;
    }
    pthread_detach(worker);
    return 1; /* worker owns fd */
}

/* OctoPrint-compatible upload-only adapter for OrcaSlicer. Never starts a print.
 * A bounded spool on internal storage permits multipart parsing with small RAM. */
static int orca_file_name_ok(const char *name){
    if(!name || !*name || strlen(name)>=PATH_MAX_LOCAL ||
       strcmp(name,".")==0 || strcmp(name,"..")==0 || !is_gcode_filename(name))return 0;
    for(const unsigned char *p=(const unsigned char *)name;*p;p++)
        if(*p<32||*p==127||*p=='/'||*p=='\\'||*p==':'||*p=='?'||*p=='#')return 0;
    return 1;
}
/* OrcaSlicer cannot rename a refused upload, and re-slicing the same model
 * repeats its name. A taken name is therefore saved as "name (1).gcode",
 * "name (2).gcode" and so on; an existing file is never overwritten. */
#define ORCA_NAME_COPIES_MAX 99
static int orca_copy_name(char out[PATH_MAX_LOCAL],const char *name,unsigned copy){
    size_t length=strlen(name);
    if(length<6)return 0;
    int n=copy?snprintf(out,PATH_MAX_LOCAL,"%.*s (%u)%s",(int)(length-6),name,copy,name+length-6)
              :snprintf(out,PATH_MAX_LOCAL,"%s",name);
    return n>0&&n<PATH_MAX_LOCAL&&n<=NAME_MAX;
}
/* The first free name from copy number `first` on: its number, -1 when none
 * fits, or -2 when the destination cannot be inspected. */
static int orca_free_name(const char *root,const char *name,unsigned first,
                          char chosen[PATH_MAX_LOCAL],char *destination,size_t capacity){
    for(unsigned copy=first;copy<=ORCA_NAME_COPIES_MAX;copy++){
        struct stat st;
        if(!orca_copy_name(chosen,name,copy)||
           snprintf(destination,capacity,"%s/%s",root,chosen)>=(int)capacity)return -1;
        if(lstat(destination,&st)!=0)return errno==ENOENT?(int)copy:-2;
    }
    return -1;
}
static int orca_get_boundary(const char *request,char boundary[80]){
    const char *p=request;
    while(*p){
        const char *end=strstr(p,"\r\n");
        if(!end)break;
        if(end==p)break;
        if((size_t)(end-p)>13 && !strncasecmp(p,"Content-Type:",13)){
            const char *m=p+13;
            while(m<end && (*m==' '||*m=='\t'))m++;
            if((size_t)(end-m)<19||strncasecmp(m,"multipart/form-data",19))return 0;
            const char *b=m;
            while(b+9<end && strncasecmp(b,"boundary=",9))b++;
            if(b+9>=end)return 0;
            b+=9;
            char quote=0;
            if(*b=='"'){quote='"';b++;}
            size_t n=0;
            while(b<end && n<70 && (!quote?*b!=';'&&*b!=' '&&*b!='\t':*b!=quote)){
                unsigned char ch=(unsigned char)*b++;
                if(ch<33||ch>126||ch=='\\'||ch=='"')return 0;
                boundary[n++]=(char)ch;
            }
            boundary[n]=0;
            return n>=1 && n<=70 && (!quote || (b<end && *b==quote));
        }
        p=end+2;
    }
    return 0;
}
/* Return the first valid CRLF--boundary marker starting at offset.  The
 * lookahead also handles patterns split across read buffers. */
static int orca_next_boundary(FILE *f,long start,const char *boundary,
                              long *payload_end,long *next,long *is_final){
    char pattern[80];
    int pat=snprintf(pattern,sizeof(pattern),"\r\n--%s",boundary);
    if(pat<=0||(size_t)pat>=sizeof(pattern))return 0;
    unsigned char buffer[UPLOAD_CHUNK+96];
    long base=start;
    size_t keep=(size_t)pat+2;
    if(fseek(f,start,SEEK_SET)!=0)return 0;
    for(;;){
        size_t n=fread(buffer,1,UPLOAD_CHUNK+keep,f);
        if(n<keep)return 0;
        for(size_t i=0;i+(size_t)pat+2<=n;i++){
            if(memcmp(buffer+i,pattern,(size_t)pat))continue;
            unsigned char a=buffer[i+(size_t)pat],b=buffer[i+(size_t)pat+1];
            if(!((a=='\r'&&b=='\n')||(a=='-'&&b=='-')))continue;
            *payload_end=base+(long)i;
            *next=base+(long)i+pat+2;
            *is_final=(a=='-'&&b=='-');
            return 1;
        }
        if(n<=keep)return 0;
        base+=(long)(n-keep);
        if(fseek(f,base,SEEK_SET)!=0)return 0;
    }
}
static int orca_part_headers(FILE *f,long start,long *data_offset,
                             char field[32],char filename[PATH_MAX_LOCAL]){
    char line[4096];int disposition=0;field[0]=filename[0]=0;
    if(fseek(f,start,SEEK_SET)!=0)return 0;
    for(int i=0;i<24;i++){
        if(!fgets(line,sizeof(line),f))return 0;
        size_t n=strlen(line);
        if(n<2||line[n-2]!='\r'||line[n-1]!='\n')return 0;
        if(n==2){*data_offset=ftell(f);return disposition;}
        if(!strncasecmp(line,"Content-Disposition:",20)){
            if(!strstr(line,"form-data"))return 0;
            const char *a=line;
            while((a=strstr(a,"name=\"")) && a>line && a[-1]!=';' && !isspace((unsigned char)a[-1]))a+=5;
            if(!a)return 0;
            a+=6;const char *end=strchr(a,'"');
            if(!end || (size_t)(end-a)>=32)return 0;
            memcpy(field,a,(size_t)(end-a));field[end-a]=0;
            const char *b=strstr(line,"filename=\"");
            if(b){
                b+=10;end=strchr(b,'"');
                if(!end || (size_t)(end-b)>=PATH_MAX_LOCAL)return 0;
                memcpy(filename,b,(size_t)(end-b));filename[end-b]=0;
            }
            disposition=1;
        }
    }
    return 0;
}
static void *orca_upload_worker(void *arg){
    gcode_upload_job *job=arg;
    const char *root=gcode_internal_root,*error="Invalid multipart upload";
    int status=400;const char *status_text="Bad Request";
    int output=-1,reserve=-1;
    char spool[PATH_MAX_LOCAL*2]="",temporary[PATH_MAX_LOCAL*2]="",destination[PATH_MAX_LOCAL*2]="";
    char filename[PATH_MAX_LOCAL]="",saved[PATH_MAX_LOCAL]="";
    int copy=0;
    FILE *input=NULL;
    size_t bytes=0;long file_start=-1,file_end=-1;
    char boundary[80];
    char reply[PATH_MAX_LOCAL*5];int reply_len=0;
    if(!gcode_storage_writable("internal",root)){status=507;status_text="Insufficient Storage";error="Internal storage unavailable";goto fail;}
    if(!orca_get_boundary((const char *)job->initial,boundary)){error="Missing multipart boundary";goto fail;}
    if(snprintf(spool,sizeof(spool),"%s/.cc2-orca-body-XXXXXX",root)>=(int)sizeof(spool)){error="Storage path too long";goto fail;}
    if(!upload_has_space(root,job->content_length,2)){status=507;status_text="Insufficient Storage";error="Insufficient free space for multipart upload";goto fail;}
    output=mkstemp(spool);
    if(output<0){status=507;status_text="Insufficient Storage";error="Cannot spool upload";goto fail;}
    bytes=job->initial_length-job->header_length;
    if(bytes>job->content_length){error="Invalid request body";goto fail;}
    {size_t off=0;while(off<bytes){ssize_t n=write(output,job->initial+job->header_length+off,bytes-off);
       if(n<0&&errno==EINTR)continue;
       if(n<=0){error="Cannot write upload";goto fail;}off+=(size_t)n;}}
    unsigned char chunk[UPLOAD_CHUNK];
    while(bytes<job->content_length){
        size_t want=job->content_length-bytes;
        if(want>sizeof(chunk))want=sizeof(chunk);
        ssize_t n=upload_receive(job,chunk,want);
        if(n<0&&errno==EINTR)continue;
        if(n<=0){if(n<0&&errno==ETIMEDOUT){status=408;status_text="Request Timeout";}error="Incomplete or timed out upload";goto fail;}
        size_t off=0;while(off<(size_t)n){ssize_t w=write(output,chunk+off,(size_t)n-off);
            if(w<0&&errno==EINTR)continue;
            if(w<=0){error="Cannot write upload";goto fail;}off+=(size_t)w;}
        bytes+=(size_t)n;
    }
    if(close(output)!=0){output=-1;error="Cannot close upload";goto fail;}output=-1;
    input=fopen(spool,"rb");if(!input){error="Cannot read upload";goto fail;}
    char first[96];int flen=snprintf(first,sizeof(first),"--%s\r\n",boundary);
    if(flen<=0||(size_t)flen>=sizeof(first)||fread(chunk,1,(size_t)flen,input)!=(size_t)flen||
       memcmp(chunk,first,(size_t)flen)){error="Invalid multipart opening";goto fail;}
    long pos=flen;
    int saw_file=0,print_requested=0;
    for(int part=0;part<16;part++){
        char field[32],partname[PATH_MAX_LOCAL];long start,end,next,final;
        if(!orca_part_headers(input,pos,&start,field,partname)||
           !orca_next_boundary(input,start,boundary,&end,&next,&final)||end<start){error="Invalid multipart part";goto fail;}
        if(!strcmp(field,"file")){
            if(saw_file || !orca_file_name_ok(partname) || end==start){error="Invalid or duplicate G-code file";goto fail;}
            snprintf(filename,sizeof(filename),"%s",partname);
            file_start=start;file_end=end;saw_file=1;
        }else if(!strcmp(field,"print") || !strcmp(field,"select")){
            if(end-start>16){error="Invalid print or select field";goto fail;}
            char value[20]={0};
            if(fseek(input,start,SEEK_SET)!=0||fread(value,1,(size_t)(end-start),input)!=(size_t)(end-start)){error="Cannot read multipart field";goto fail;}
            if(!strcmp(field,"print") && (!strcasecmp(value,"true")||!strcmp(value,"1")))print_requested=1;
        }else if(!strcmp(field,"path")){
            if(end>start){error="Upload subdirectories are not supported";goto fail;}
        }
        pos=next;
        if(final)break;
        if(part==15){error="Too many multipart fields";goto fail;}
    }
    if(!saw_file){error="Missing file field";goto fail;}
    if((unsigned long)(file_end-file_start)>FILE_UPLOAD_MAX){status=413;status_text="Payload Too Large";error="G-code file too large";goto fail;}
    if(print_requested){
        if(!gcode_idle_for_mutation(job->mqtt)){status=409;status_text="Conflict";error="Printer must be idle for upload-and-print";goto fail;}
        pthread_mutex_lock(&orca_pending_mutex);
        orca_pending_expire_locked();
        int occupied=orca_pending_filename[0]!=0;
        pthread_mutex_unlock(&orca_pending_mutex);
        if(occupied){status=409;status_text="Conflict";error="Another upload-and-print awaits Canvas confirmation";goto fail;}
    }
    if(snprintf(temporary,sizeof(temporary),"%s/.cc2-orca-file-XXXXXX",root)>=(int)sizeof(temporary)){error="Storage path too long";goto fail;}
    copy=orca_free_name(root,filename,0,saved,destination,sizeof(destination));
    if(copy==-2){error="Cannot inspect destination";goto fail;}
    if(copy<0){status=409;status_text="Conflict";error="File already exists";goto fail;}
    if(!upload_has_space(root,(size_t)(file_end-file_start),1)){status=507;status_text="Insufficient Storage";error="Insufficient free space for G-code file";goto fail;}
    output=mkstemp(temporary);
    if(output<0){status=507;status_text="Insufficient Storage";error="Cannot create G-code file";goto fail;}
    (void)fchmod(output,0644);
    if(fseek(input,file_start,SEEK_SET)!=0){error="Cannot seek file data";goto fail;}
    long left=file_end-file_start;
    while(left>0){if(!upload_seconds_left(job)){error="Upload deadline exceeded";goto fail;}size_t want=left>(long)sizeof(chunk)?sizeof(chunk):(size_t)left;
        if(fread(chunk,1,want,input)!=want){error="Cannot read file data";goto fail;}
        size_t off=0;while(off<want){ssize_t w=write(output,chunk+off,want-off);
            if(w<0&&errno==EINTR)continue;
            if(w<=0){status=507;status_text="Insufficient Storage";error="Cannot write G-code file";goto fail;}
            off+=(size_t)w;}
        left-=(long)want;
    }
    if(fsync(output)!=0 || close(output)!=0){output=-1;status=507;status_text="Insufficient Storage";error="Cannot finalize G-code file";goto fail;}output=-1;
    /* Recheck before publication; upload-only may continue during printing. */
    if(!(print_requested ? gcode_idle_for_mutation(job->mqtt) : gcode_ready_for_upload(job->mqtt))){status=409;status_text="Conflict";error="Printer state changed during upload";goto fail;}
    /* O_EXCL is the authoritative check: a name taken since the scan moves
     * the file on to the next free copy name. */
    while((reserve=open(destination,O_WRONLY|O_CREAT|O_EXCL,0600))<0){
        if(errno!=EEXIST){status=507;status_text="Insufficient Storage";error="Cannot reserve destination";goto fail;}
        copy=orca_free_name(root,filename,(unsigned)copy+1,saved,destination,sizeof(destination));
        if(copy==-2){error="Cannot inspect destination";goto fail;}
        if(copy<0){status=409;status_text="Conflict";error="File already exists";goto fail;}
    }
    close(reserve);reserve=-1;
    if(rename(temporary,destination)!=0){unlink(destination);status=507;status_text="Insufficient Storage";error="Cannot publish file";goto fail;}
    temporary[0]=0;
    unsigned long pending_generation=0;
    if(print_requested){
        pthread_mutex_lock(&orca_pending_mutex);
        orca_pending_expire_locked();
        if(orca_pending_filename[0]){
            pthread_mutex_unlock(&orca_pending_mutex);
            /* The file is safely uploaded; never silently print or replace an
             * unrelated pending operator confirmation. */
            error="Upload finished, but another print is awaiting Canvas confirmation";
            status=409;status_text="Conflict";goto fail;
        }
        snprintf(orca_pending_filename,sizeof(orca_pending_filename),"%s",saved);
        orca_pending_created=time(NULL);
        pending_generation=++orca_pending_generation;
        if(!pending_generation)pending_generation=++orca_pending_generation;
        pthread_mutex_unlock(&orca_pending_mutex);
    }
    {char safe_name[PATH_MAX_LOCAL*2];
     json_escape(safe_name,sizeof(safe_name),saved);
     int n=snprintf(reply,sizeof(reply),"{\"files\":{\"local\":{\"name\":\"%s\",\"origin\":\"local\",\"size\":%ld,\"refs\":{\"resource\":\"/api/files/local/%s\"}}},\"done\":true,\"uploaded\":true,\"printed\":false,\"awaiting_canvas\":%s,\"generation\":%lu}\n",safe_name,file_end-file_start,safe_name,print_requested?"true":"false",pending_generation);
     if(n>0&&(size_t)n<sizeof(reply)){reply_len=n;status=201;status_text="Created";}}
    goto cleanup;
fail:
    reply_len=snprintf(reply,sizeof(reply),"{\"error\":\"%s\"}\n",error);
cleanup:
    if(input)fclose(input);
    if(output>=0)close(output);
    if(reserve>=0)close(reserve);
    if(temporary[0])unlink(temporary);
    if(spool[0])unlink(spool);
    /* Free the upload slot before answering: OrcaSlicer may start its next
     * upload as soon as it reads this response. */
    upload_slot_release();
    if(reply_len>0&&(size_t)reply_len<sizeof(reply))
        respond(job->fd,status,status_text,"application/json",reply,(size_t)reply_len);
    close(job->fd);free(job);
    return NULL;
}
static int orca_expect_continue(const char *request){
    const char *p=request;
    while(*p){
        const char *e=strstr(p,"\r\n");
        if(!e || e==p)break;
        if((size_t)(e-p)>7 && !strncasecmp(p,"Expect:",7)){
            const char *v=p+7;
            while(v<e && (*v==' '||*v=='\t'))v++;
            return (size_t)(e-v)==12 && !strncasecmp(v,"100-continue",12);
        }
        p=e+2;
    }
    return 0;
}
static int orca_upload_start(int fd,const char *request,size_t used,size_t header_length,const mqtt_client *mqtt){
    if(!gcode_ready_for_upload(mqtt)){
        const char *msg="{\"error\":\"Printer must be idle or printing and connected\"}\n";
        respond(fd,409,"Conflict","application/json",msg,strlen(msg));return 0;
    }
    char boundary[80];
    if(!orca_get_boundary(request,boundary)){
        const char *msg="{\"error\":\"Expected multipart/form-data\"}\n";
        respond(fd,415,"Unsupported Media Type","application/json",msg,strlen(msg));return 0;
    }
    size_t length=content_length_from_headers(request);
    if(!length||length>FILE_UPLOAD_MAX+8192||header_length>used||used-header_length>length){
        const char *msg="{\"error\":\"Invalid upload length\"}\n";
        respond(fd,413,"Payload Too Large","application/json",msg,strlen(msg));return 0;
    }
    if(!upload_space_guard(fd,gcode_internal_root,length,2))return 0;
    if(!upload_slot_acquire()){
        const char *msg="{\"error\":\"Another upload is running\"}\n";
        respond(fd,409,"Conflict","application/json",msg,strlen(msg));return 0;
    }
    gcode_upload_job *job=calloc(1,sizeof(*job));
    if(!job){upload_slot_release();return 0;}
    clock_gettime(CLOCK_MONOTONIC,&job->started);
    job->fd=fd;job->initial_length=used;job->header_length=header_length;
    job->content_length=length;job->mqtt=mqtt;memcpy(job->initial,request,used);
    struct timeval timeout={20,0};
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    if(orca_expect_continue(request)){
        const char *interim="HTTP/1.1 100 Continue\r\n\r\n";
        if(send_all(fd,interim,strlen(interim))!=0){
            free(job);upload_slot_release();return 0;
        }
    }
    pthread_t worker;
    if(pthread_create(&worker,NULL,orca_upload_worker,job)!=0){
        free(job);upload_slot_release();return 0;
    }
    pthread_detach(worker);
    return 1;
}
static void orca_version_response(int fd){
    const char *body="{\"api\":\"0.1\",\"server\":\"1.11.0\",\"text\":\"OctoPrint 1.11.0 (CC2 Control compatibility)\"}\n";
    respond(fd,200,"OK","application/json",body,strlen(body));
}

static unsigned long gcode_path_hash(const char *text) {
    unsigned long hash = 2166136261UL;
    while (*text) {
        hash ^= (unsigned char)*text++;
        hash *= 16777619UL;
    }
    return hash & 0xffffffffUL;
}

/* USB printing on the touchscreen uses a private import stage.  Method 1020
 * reliably accepts local files, so mirror that workflow before publishing. */
static int gcode_import_usb(const char *relative, char imported[PATH_MAX_LOCAL]) {
    char source[PATH_MAX_LOCAL * 2], destination[PATH_MAX_LOCAL * 2];
    char temporary[PATH_MAX_LOCAL * 2];
    if (!gcode_resolved_path(gcode_usb_root, relative, source, sizeof(source))) return -1;

    const char *base = strrchr(relative, '/');
    base = base ? base + 1 : relative;
    char safe_base[241]; size_t used = 0;
    for (const unsigned char *cursor = (const unsigned char *)base;
         *cursor && used + 1 < sizeof(safe_base); ++cursor) {
        unsigned char value = *cursor;
        safe_base[used++] = (value == '/' || value == '\\' || value < 32) ? '_' : (char)value;
    }
    safe_base[used] = '\0';
    if (!is_gcode_filename(safe_base)) return -1;

    int length = snprintf(imported, PATH_MAX_LOCAL, GCODE_USB_IMPORT_PREFIX "%08lx_%s",
                          gcode_path_hash(relative), safe_base);
    if (length < 0 || length >= PATH_MAX_LOCAL) return -1;
    length = snprintf(destination, sizeof(destination), "%s/%s",
                      gcode_internal_root, imported);
    if (length < 0 || (size_t)length >= sizeof(destination)) return -1;
    length = snprintf(temporary, sizeof(temporary), "%s.part", destination);
    if (length < 0 || (size_t)length >= sizeof(temporary)) return -1;

    FILE *input = fopen(source, "rb");
    if (!input) return -1;
    FILE *output = fopen(temporary, "wb");
    if (!output) { fclose(input); return -1; }
    char buffer[65536]; int failed = 0;
    for (;;) {
        size_t count = fread(buffer, 1, sizeof(buffer), input);
        if (count && fwrite(buffer, 1, count, output) != count) { failed = 1; break; }
        if (count < sizeof(buffer)) {
            if (ferror(input)) failed = 1;
            break;
        }
    }
    if (!failed && fflush(output) != 0) failed = 1;
    if (!failed && fsync(fileno(output)) != 0) failed = 1;
    if (fclose(output) != 0) failed = 1;
    fclose(input);
    if (failed || rename(temporary, destination) != 0) {
        unlink(temporary);
        return -1;
    }
    chmod(destination, 0644);
    return 0;
}

/* "; filament used [mm] = a, b, ...": the slicer's length per tool (OrcaSlicer and
 * PrusaSlicer write it near the end). A later line replaces an earlier one. */
static void gcode_filament_lengths(const char *comment, double lengths[GCODE_TOOLS_MAX]) {
    static const char marker[] = "filament used [mm]";
    double values[GCODE_TOOLS_MAX];
    int count = 0;
    for (comment++; *comment == ' ' || *comment == '\t'; comment++) {}
    if (strncmp(comment, marker, sizeof(marker) - 1)) return;
    for (comment += sizeof(marker) - 1; *comment == ' ' || *comment == '\t'; comment++) {}
    if (*comment++ != '=') return;
    while (count < GCODE_TOOLS_MAX) {
        char *end; errno = 0;
        double value = strtod(comment, &end);
        if (end == comment || errno || !isfinite(value) || value < 0 || value > 1e9) return;
        values[count++] = value;
        while (*end == ' ' || *end == '\t') end++;
        if (*end != ',') { if (*end && *end != '\r' && *end != '\n') return; break; }
        comment = end + 1;
    }
    for (int tool = 0; tool < GCODE_TOOLS_MAX; ++tool) lengths[tool] = tool < count ? values[tool] : -1;
}

/* `lengths`, when given, receives each tool's slicer filament length in mm, or -1. */
static int gcode_detect_tools(const char *root, const char *relative,
                              int tools[GCODE_TOOLS_MAX], size_t *tool_count, double *lengths) {
    char path[PATH_MAX_LOCAL * 2], line[2048];
    if (!gcode_resolved_path(root, relative, path, sizeof(path))) return -1;
    FILE *file = fopen(path, "r");
    if (!file) return -1;
    int present[GCODE_TOOLS_MAX] = {0};
    if (lengths) for (int tool = 0; tool < GCODE_TOOLS_MAX; ++tool) lengths[tool] = -1;
    while (fgets(line, sizeof(line), file)) {
        char *comment = strchr(line, ';');
        if (comment) {
            if (lengths) gcode_filament_lengths(comment, lengths);
            *comment = '\0';
        }
        for (char *cursor = line; *cursor; ++cursor) {
            if ((*cursor != 'T' && *cursor != 't') ||
                (cursor != line && !isspace((unsigned char)cursor[-1]))) continue;
            char *number = cursor + 1, *end = number;
            if (!isdigit((unsigned char)*number)) continue;
            long tool = strtol(number, &end, 10);
            if (tool < 0 || tool >= GCODE_TOOLS_MAX) continue;
            if (*end && !isspace((unsigned char)*end) && *end != '*') continue;
            present[tool] = 1;
            cursor = end - 1;
        }
    }
    int failed = ferror(file);
    fclose(file);
    if (failed) return -1;
    *tool_count = 0;
    for (int tool = 0; tool < GCODE_TOOLS_MAX; ++tool)
        if (present[tool]) tools[(*tool_count)++] = tool;
    if (!*tool_count) tools[(*tool_count)++] = 0;
    return 0;
}

/* Slicer filament lists use tool indices, not physical Canvas slot indices.
 * Read bounded comment metadata only; never interpret it as commands. */
typedef struct { char color[10]; char material[65]; } gcode_filament_info;

static int gcode_read_filaments(const char *root, const char *relative,
                                gcode_filament_info info[GCODE_TOOLS_MAX]) {
    char path[PATH_MAX_LOCAL * 2], line[4096];
    memset(info, 0, sizeof(*info) * GCODE_TOOLS_MAX);
    if (!gcode_resolved_path(root, relative, path, sizeof(path))) return -1;
    FILE *file = fopen(path, "r");
    if (!file) return -1;
    size_t scanned = 0;
    while (scanned < 256 * 1024 && fgets(line, sizeof(line), file)) {
        scanned += strlen(line);
        /* Discard truncated lines rather than assigning partial metadata. */
        if (!strchr(line, '\n') && !feof(file)) {
            int ch; while (scanned < 256 * 1024 && (ch = fgetc(file)) != EOF && ch != '\n') scanned++;
            continue;
        }
        char *cursor = line;
        while (isspace((unsigned char)*cursor)) cursor++;
        if (*cursor++ != ';') continue;
        while (isspace((unsigned char)*cursor)) cursor++;
        char *equals = strchr(cursor, '=');
        if (!equals) continue;
        char *key_end = equals;
        while (key_end > cursor && isspace((unsigned char)key_end[-1])) key_end--;
        *key_end = '\0';
        int color = strcmp(cursor, "filament_colour") == 0 || strcmp(cursor, "filament_color") == 0;
        if (!color && strcmp(cursor, "filament_type") != 0) continue;
        cursor = equals + 1;
        for (int tool = 0; tool < GCODE_TOOLS_MAX; tool++) {
            char *end = strpbrk(cursor, ";,\r\n");
            if (!end) end = cursor + strlen(cursor);
            char *next = (*end == ';' || *end == ',') ? end + 1 : NULL;
            while (cursor < end && isspace((unsigned char)*cursor)) cursor++;
            while (end > cursor && isspace((unsigned char)end[-1])) end--;
            if (end - cursor >= 2 && *cursor == '"' && end[-1] == '"') { cursor++; end--; }
            size_t length = (size_t)(end - cursor);
            if (color) {
                int valid = (length == 7 || length == 9) && *cursor == '#';
                for (size_t i = 1; valid && i < length; i++) valid = isxdigit((unsigned char)cursor[i]);
                if (valid) { memcpy(info[tool].color, cursor, length); info[tool].color[length] = '\0'; }
            } else if (length && length < sizeof(info[tool].material)) {
                memcpy(info[tool].material, cursor, length); info[tool].material[length] = '\0';
            }
            if (!next) break;
            cursor = next;
        }
    }
    int failed = ferror(file); fclose(file);
    return failed ? -1 : 0;
}

static int gcode_has_adaptive_mesh(const char *root, const char *relative, int *adaptive) {
    char path[PATH_MAX_LOCAL * 2], line[4096];
    if (!adaptive || !gcode_resolved_path(root, relative, path, sizeof(path))) return -1;
    FILE *file = fopen(path, "r");
    if (!file) return -1;
    *adaptive = 0;
    while (fgets(line, sizeof(line), file)) {
        char upper[sizeof(line)]; size_t index = 0;
        for (; line[index] && index + 1 < sizeof(upper); ++index)
            upper[index] = (char)toupper((unsigned char)line[index]);
        upper[index] = '\0';
        char *comment = strchr(upper, ';');
        if (comment) *comment = '\0';
        if (strstr(upper, "BED_MESH_CALIBRATE") &&
            (strstr(upper, "FROM_SLICER=1") ||
             (strstr(upper, "MESH_MIN=") && strstr(upper, "MESH_MAX=")) ||
             strstr(upper, "ADAPTIVE=1"))) {
            *adaptive = 1;
            break;
        }
    }
    int failed = ferror(file);
    fclose(file);
    return failed ? -1 : 0;
}

static int gcode_request_file(const char *body, size_t body_len,
                              char storage[16], char filename[PATH_MAX_LOCAL],
                              const char **extra, size_t *extra_len,
                              const char **root, const char **media) {
    const char *first = memchr(body, '\n', body_len);
    if (!first) return 0;
    size_t storage_length = (size_t)(first - body);
    const char *file_start = first + 1;
    size_t remaining = body_len - storage_length - 1;
    const char *second = memchr(file_start, '\n', remaining);
    size_t filename_length = second ? (size_t)(second - file_start) : remaining;
    if (!storage_length || storage_length >= 16 || !filename_length ||
        filename_length >= PATH_MAX_LOCAL) return 0;
    memcpy(storage, body, storage_length); storage[storage_length] = '\0';
    memcpy(filename, file_start, filename_length); filename[filename_length] = '\0';
    *root = NULL; *media = NULL;
    if (strcmp(storage, "internal") == 0) { *root = gcode_internal_root; *media = "local"; }
    else if (strcmp(storage, "usb") == 0) { *root = gcode_usb_root; *media = "u-disk"; }
    if (!*root) return 0;
    if (extra && extra_len) {
        *extra = second ? second + 1 : body + body_len;
        *extra_len = second ? body_len - (size_t)(*extra - body) : 0;
    }
    return 1;
}

typedef struct { int valid; struct stat stamp; char path[PATH_MAX_LOCAL*2]; size_t length; char response[4096]; } analysis_cache;
static analysis_cache analysis_cached[2];
static _Thread_local analysis_cache *analysis_capture;
static void analysis_respond(int fd,int code,const char *status,const char *type,const char *body,size_t length) {
    if(analysis_capture && code==200 && length<sizeof(analysis_capture->response)) {
        memcpy(analysis_capture->response,body,length);analysis_capture->length=length;
    }
    respond(fd,code,status,type,body,length);
}

static void gcode_inspect_response(int fd, const char *body, size_t body_len) {
    char storage[16], filename[PATH_MAX_LOCAL];
    const char *root, *media;
    if (!gcode_request_file(body, body_len, storage, filename, NULL, NULL, &root, &media) ||
        !gcode_file_is_printable(root, filename)) {
        const char *error = "{\"error\":\"File is unavailable or unsafe\"}\n";
        analysis_respond(fd, 404, "Not Found", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    (void)media;
    int tools[GCODE_TOOLS_MAX], adaptive = 0; size_t count = 0;
    double lengths[GCODE_TOOLS_MAX];
    if (gcode_detect_tools(root, filename, tools, &count, lengths) != 0) {
        const char *error = "{\"error\":\"Cannot inspect the G-code file\"}\n";
        analysis_respond(fd, 500, "Internal Server Error", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    if (gcode_has_adaptive_mesh(root, filename, &adaptive) != 0) {
        const char *error = "{\"error\":\"Cannot inspect bed leveling commands\"}\n";
        analysis_respond(fd, 500, "Internal Server Error", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    gcode_filament_info filaments[GCODE_TOOLS_MAX];
    if (gcode_read_filaments(root, filename, filaments) != 0) {
        const char *error = "{\"error\":\"Cannot inspect filament metadata\"}\n";
        analysis_respond(fd, 500, "Internal Server Error", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    char response[4096]; size_t used = 0;
    int length = snprintf(response, sizeof(response), "{\"tools\":[");
    if (length < 0 || (size_t)length >= sizeof(response)) return;
    used = (size_t)length;
    for (size_t index = 0; index < count; ++index) {
        length = snprintf(response + used, sizeof(response) - used,
                          "%s%d", index ? "," : "", tools[index]);
        if (length < 0 || (size_t)length >= sizeof(response) - used) return;
        used += (size_t)length;
    }
    length = snprintf(response + used, sizeof(response) - used, "],\"filaments\":[");
    if (length < 0 || (size_t)length >= sizeof(response) - used) return;
    used += (size_t)length;
    for (size_t index = 0; index < count; index++) {
        int tool = tools[index]; char material[130], mm[32] = "";
        json_escape(material, sizeof(material), filaments[tool].material);
        /* The slicer's filament length is only sent when the file states it. */
        if (lengths[tool] >= 0) snprintf(mm, sizeof(mm), ",\"mm\":%.1f", lengths[tool]);
        length = snprintf(response + used, sizeof(response) - used,
            "%s{\"tool\":%d,\"color\":\"%s\",\"material\":\"%s\"%s}",
            index ? "," : "", tool, filaments[tool].color, material, mm);
        if (length < 0 || (size_t)length >= sizeof(response) - used) return;
        used += (size_t)length;
    }
    length = snprintf(response + used, sizeof(response) - used,
                      "],\"multicolour\":%s,\"adaptive_mesh\":%s}\n",
                      count > 1 ? "true" : "false", adaptive ? "true" : "false");
    if (length > 0 && (size_t)length < sizeof(response) - used)
        analysis_respond(fd, 200, "OK", "application/json; charset=utf-8", response, used + (size_t)length);
}

static int parse_slot_map(const char *text, size_t length,
                          int tools[GCODE_TOOLS_MAX], int trays[GCODE_TOOLS_MAX],
                          size_t *count) {
    *count = 0;
    if (!length) return 1;
    char map[256];
    if (length >= sizeof(map)) return 0;
    memcpy(map, text, length); map[length] = '\0';
    char *cursor = map;
    while (*cursor) {
        if (*count >= GCODE_TOOLS_MAX) return 0;
        char *tool_end, *tray_end;
        long tool = strtol(cursor, &tool_end, 10);
        if (tool_end == cursor || *tool_end != ':') return 0;
        long tray = strtol(tool_end + 1, &tray_end, 10);
        if (tray_end == tool_end + 1 || (tray_end[0] && tray_end[0] != ',')) return 0;
        if (tool < 0 || tool >= GCODE_TOOLS_MAX || tray < 0 || tray > 3) return 0;
        for (size_t index = 0; index < *count; ++index)
            if (tools[index] == tool) return 0;
        tools[*count] = (int)tool; trays[*count] = (int)tray; (*count)++;
        cursor = *tray_end ? tray_end + 1 : tray_end;
        if (!*cursor) break;
    }
    return 1;
}

static int base64_value(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '+') return 62;
    if (ch == '/') return 63;
    return -1;
}

static unsigned char *decode_base64(const char *input, size_t length, size_t *decoded_length) {
    size_t capacity = length / 4 * 3 + 3, used = 0;
    unsigned char *output = malloc(capacity ? capacity : 1);
    int quartet[4]; size_t count = 0;
    if (!output) return NULL;
    for (size_t index = 0; index < length; ++index) {
        unsigned char ch = (unsigned char)input[index];
        if (isspace(ch)) continue;
        if (ch == '=') quartet[count++] = -2;
        else {
            int value = base64_value(ch);
            if (value < 0) { free(output); return NULL; }
            quartet[count++] = value;
        }
        if (count == 4) {
            if (quartet[0] < 0 || quartet[1] < 0 ||
                (quartet[2] == -2 && quartet[3] != -2)) { free(output); return NULL; }
            output[used++] = (unsigned char)((quartet[0] << 2) | (quartet[1] >> 4));
            if (quartet[2] >= 0) {
                output[used++] = (unsigned char)((quartet[1] << 4) | (quartet[2] >> 2));
                if (quartet[3] >= 0)
                    output[used++] = (unsigned char)((quartet[2] << 6) | quartet[3]);
                else if (quartet[3] != -2) { free(output); return NULL; }
            }
            count = 0;
        }
    }
    if (count != 0) { free(output); return NULL; }
    *decoded_length = used;
    return output;
}

static void gcode_thumbnail_response(int fd, const char *body, size_t body_len) {
    char storage[16], filename[PATH_MAX_LOCAL], path[PATH_MAX_LOCAL * 2], line[4096];
    const char *root, *media;
    if (!gcode_request_file(body, body_len, storage, filename, NULL, NULL, &root, &media) ||
        !gcode_resolved_path(root, filename, path, sizeof(path))) {
        const char *error = "{\"error\":\"File is unavailable or unsafe\"}\n";
        respond(fd, 404, "Not Found", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    (void)media;
    FILE *file = fopen(path, "rb");
    char *current = NULL, *best = NULL;
    size_t current_used = 0, best_used = 0, scanned = 0;
    unsigned long current_pixels = 0, best_pixels = 0;
    int collecting = 0;
    if (!file) {
        const char *error = "{\"error\":\"Cannot open G-code file\"}\n";
        respond(fd, 500, "Internal Server Error", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    current = malloc(GCODE_THUMBNAIL_BASE64_MAX + 1);
    best = malloc(GCODE_THUMBNAIL_BASE64_MAX + 1);
    if (!current || !best) { fclose(file); free(current); free(best); return; }
    while (scanned < GCODE_THUMBNAIL_SCAN_MAX && fgets(line, sizeof(line), file)) {
        size_t line_length = strlen(line); scanned += line_length;
        unsigned int width = 0, height = 0; unsigned long declared = 0;
        char *text = line;
        while (*text == ' ' || *text == '\t' || *text == ';') text++;
        if (sscanf(text, "thumbnail begin %ux%u %lu", &width, &height, &declared) >= 2 ||
            sscanf(text, "thumbnail_JPG begin %ux%u %lu", &width, &height, &declared) >= 2) {
            (void)declared;
            collecting = width > 0 && height > 0;
            current_pixels = (unsigned long)width * (unsigned long)height;
            current_used = 0;
            continue;
        }
        if (collecting && (strncasecmp(text, "thumbnail end", 13) == 0 ||
                           strncasecmp(text, "thumbnail_JPG end", 17) == 0)) {
            if (current_used && current_pixels > best_pixels) {
                memcpy(best, current, current_used); best_used = current_used; best_pixels = current_pixels;
            }
            collecting = 0; current_used = 0; current_pixels = 0;
            continue;
        }
        if (collecting) {
            while (*text && isspace((unsigned char)*text)) text++;
            size_t chunk = strcspn(text, "\r\n");
            if (current_used + chunk > GCODE_THUMBNAIL_BASE64_MAX) {
                collecting = 0; current_used = 0; current_pixels = 0;
            } else {
                memcpy(current + current_used, text, chunk); current_used += chunk;
            }
        }
    }
    fclose(file);
    if (!best_used) {
        free(current); free(best);
        const char *error = "{\"error\":\"No embedded thumbnail\"}\n";
        respond(fd, 404, "Not Found", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    size_t image_length = 0;
    unsigned char *image = decode_base64(best, best_used, &image_length);
    free(current); free(best);
    if (!image || image_length < 8) {
        free(image);
        const char *error = "{\"error\":\"Invalid embedded thumbnail\"}\n";
        respond(fd, 422, "Unprocessable Content", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    const char *content_type = NULL;
    static const unsigned char png_signature[8] = {137,80,78,71,13,10,26,10};
    if (memcmp(image, png_signature, sizeof(png_signature)) == 0) content_type = "image/png";
    else if (image[0] == 0xff && image[1] == 0xd8) content_type = "image/jpeg";
    if (!content_type) {
        free(image);
        const char *error = "{\"error\":\"Unsupported thumbnail image\"}\n";
        respond(fd, 415, "Unsupported Media Type", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    respond(fd, 200, "OK", content_type, (const char *)image, image_length);
    free(image);
}


/* SLICE_CFG_MODEL=0 enters the vendor preliminary print calibration.
 * Enable the file's FROM_SLICER calibration independently, while bypassing
 * that preliminary stage with SLICE_CFG_MODEL=1. Full CC2 calibration is
 * already performed explicitly below and must not be repeated either. */
static int build_calibrated_start_script(char *script, size_t cap, int force_full_mesh,
                                         char print_layout, const char *print_media,
                                         const char *print_filename, const int *tools,
                                         const int *trays, size_t slot_count) {
    size_t used = 0;
    if (!script || !cap || !print_media || !print_filename ||
        strchr(print_filename, '\"') || strchr(print_filename, '\\') ||
        (slot_count && (!tools || !trays))) return -1;
    int length;
    if (force_full_mesh) {
        length = snprintf(script, cap,
            "BED_MESH_CALIBRATE_SET EXECUTE_CALIBRATE_FROM_SLICER=0\n"
            "PRINT_SURFACE_SET PLANE=%d\n"
            "BED_MESH_CALIBRATE PROFILE=%s BED_TEMP=60\n",
            print_layout == 'B' ? 1 : 0,
            print_layout == 'B' ? "default1" : "default");
    } else {
        length = snprintf(script, cap,
            "BED_MESH_CALIBRATE_SET EXECUTE_CALIBRATE_FROM_SLICER=1\n"
            "PRINT_SURFACE_SET PLANE=%d\n", print_layout == 'B' ? 1 : 0);
    }
    if (length > 0 && (size_t)length < cap) used = (size_t)length;
    for (size_t index = 0; used && index < slot_count; ++index) {
        length = snprintf(script + used, cap - used,
            "CANVAS_SET_COLOR_TABLE T=%d ID=0 CHANNEL=%d\n", tools[index], trays[index]);
        if (length <= 0 || (size_t)length >= cap - used) { used = 0; break; }
        used += (size_t)length;
    }
    if (used) {
        length = snprintf(script + used, cap - used,
            "SDCARD_PRINT_FILE FILENAME=%s/\"%s\" SLICE_CFG_MODEL=1",
            print_media, print_filename);
        if (length <= 0 || (size_t)length >= cap - used) used = 0;
    }
    return used ? 0 : -1;
}

static void gcode_start_response(int fd, mqtt_client *mqtt,
                                 const char *body, size_t body_len) {
    char storage[16], filename[PATH_MAX_LOCAL];
    const char *mapping, *root, *media; size_t mapping_len = 0;
    if (!gcode_request_file(body, body_len, storage, filename, &mapping, &mapping_len,
                            &root, &media)) {
        const char *error = "{\"accepted\":false,\"error\":\"Invalid print request\"}\n";
        respond(fd, 400, "Bad Request", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    if (!gcode_file_is_printable(root, filename)) {
        const char *error = "{\"accepted\":false,\"error\":\"File is unavailable or unsafe\"}\n";
        respond(fd, 404, "Not Found", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    const char *layout_line = memchr(mapping, '\n', mapping_len);
    size_t map_len = layout_line ? (size_t)(layout_line - mapping) : mapping_len;
    const char *level_line = NULL, *video_line = NULL; size_t layout_len = 0, level_len = 0, video_len = 0;
    if (layout_line) {
        layout_line++;
        size_t remaining = mapping_len - (size_t)(layout_line - mapping);
        level_line = memchr(layout_line, '\n', remaining);
        layout_len = level_line ? (size_t)(level_line - layout_line) : remaining;
        if (level_line) {
            level_line++;size_t rest=mapping_len-(size_t)(level_line-mapping);
            video_line=memchr(level_line,'\n',rest);
            level_len=video_line?(size_t)(video_line-level_line):rest;
            if(video_line){video_line++;video_len=mapping_len-(size_t)(video_line-mapping);}
        }
    }
    char print_layout = 'A';
    if (layout_len) {
        if (layout_len != 1 || (layout_line[0] != 'A' && layout_line[0] != 'B')) {
            const char *error = "{\"accepted\":false,\"error\":\"Invalid build plate side\"}\n";
            respond(fd, 400, "Bad Request", "application/json; charset=utf-8", error, strlen(error));
            return;
        }
        print_layout = layout_line[0];
    }
    int timelapse=0;
    if(video_line){
        if(video_len!=1||(video_line[0]!='0'&&video_line[0]!='1')){
            const char *error="{\"accepted\":false,\"error\":\"Invalid timelapse option\"}\n";
            respond(fd,400,"Bad Request","application/json",error,strlen(error));return;
        }
        timelapse=video_line[0]=='1';
    }
    char leveling[16] = "saved";
    if (level_len) {
        if (level_len >= sizeof(leveling)) {
            const char *error = "{\"accepted\":false,\"error\":\"Invalid bed leveling mode\"}\n";
            respond(fd, 400, "Bad Request", "application/json; charset=utf-8", error, strlen(error));
            return;
        }
        memcpy(leveling, level_line, level_len); leveling[level_len] = '\0';
        if (strcmp(leveling, "saved") != 0 && strcmp(leveling, "calibrate") != 0 &&
            strcmp(leveling, "adaptive") != 0 && strcmp(leveling, "full") != 0) {
            const char *error = "{\"accepted\":false,\"error\":\"Invalid bed leveling mode\"}\n";
            respond(fd, 400, "Bad Request", "application/json; charset=utf-8", error, strlen(error));
            return;
        }
    }
    int tools[GCODE_TOOLS_MAX], trays[GCODE_TOOLS_MAX]; size_t slot_count = 0;
    if (!parse_slot_map(mapping, map_len, tools, trays, &slot_count)) {
        const char *error = "{\"accepted\":false,\"error\":\"Invalid Canvas slot mapping\"}\n";
        respond(fd, 400, "Bad Request", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    int adaptive = 0;
    if (gcode_has_adaptive_mesh(root, filename, &adaptive) != 0) {
        const char *error = "{\"accepted\":false,\"error\":\"Cannot inspect bed leveling commands\"}\n";
        respond(fd, 500, "Internal Server Error", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    if (strcmp(leveling, "adaptive") == 0 && !adaptive) {
        const char *error = "{\"accepted\":false,\"error\":\"Selected leveling mode does not match this G-code\"}\n";
        respond(fd, 409, "Conflict", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    if (slot_count) {
        int detected[GCODE_TOOLS_MAX]; size_t detected_count = 0;
        if (gcode_detect_tools(root, filename, detected, &detected_count, NULL) != 0 ||
            detected_count != slot_count) {
            const char *error = "{\"accepted\":false,\"error\":\"Canvas mapping does not match this G-code\"}\n";
            respond(fd, 409, "Conflict", "application/json; charset=utf-8", error, strlen(error));
            return;
        }
        for (size_t expected = 0; expected < detected_count; ++expected) {
            int found = 0;
            for (size_t supplied = 0; supplied < slot_count; ++supplied)
                if (tools[supplied] == detected[expected]) found = 1;
            if (!found) {
                const char *error = "{\"accepted\":false,\"error\":\"Canvas mapping does not match this G-code\"}\n";
                respond(fd, 409, "Conflict", "application/json; charset=utf-8", error, strlen(error));
                return;
            }
        }
    }
    if (!mqtt->connected || !mqtt->registered) {
        const char *error = "{\"accepted\":false,\"error\":\"Printer MQTT is not ready\"}\n";
        respond(fd, 503, "Service Unavailable", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    if (!mqtt->have_machine_status || mqtt->machine_status != 1) {
        const char *error = "{\"accepted\":false,\"error\":\"Starting a file requires an idle printer\"}\n";
        respond(fd, 409, "Conflict", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    char print_filename[PATH_MAX_LOCAL];
    const char *print_media = media;
    if (strcmp(storage, "usb") == 0) {
        if (gcode_import_usb(filename, print_filename) != 0) {
            const char *error = "{\"accepted\":false,\"error\":\"Cannot import the USB file into internal memory\"}\n";
            respond(fd, 507, "Insufficient Storage", "application/json; charset=utf-8", error, strlen(error));
            return;
        }
        print_media = "local";
    } else {
        snprintf(print_filename, sizeof(print_filename), "%s", filename);
    }
    int missing_plate_mesh = !saved_plate_mesh_exists(print_layout);
    int calibration_requested = strcmp(leveling, "calibrate") == 0;
    int force_full_mesh = strcmp(leveling, "full") == 0 || missing_plate_mesh ||
                          (calibration_requested && !adaptive);
    int calibrated_start = strcmp(leveling, "saved") != 0 || missing_plate_mesh;
    int start_result = -1;
    if (calibrated_start) {
        char script[4096];
        if (strchr(print_filename, '"') || strchr(print_filename, '\\')) {
            const char *error = "{\"accepted\":false,\"error\":\"Filename is incompatible with calibrated printing\"}\n";
            respond(fd, 422, "Unprocessable Content", "application/json; charset=utf-8", error, strlen(error));
            return;
        }
        if (mqtt_prepare_timelapse(mqtt,timelapse)==0 &&
            build_calibrated_start_script(script, sizeof(script), force_full_mesh,
                                          print_layout, print_media, print_filename,
                                          tools, trays, slot_count) == 0)
            start_result = send_local_gcode_script(script);
    } else {
        start_result = mqtt_start_print(mqtt, print_media, print_filename, tools, trays,
                                        slot_count, print_layout, 0, timelapse);
    }
    if (start_result != 0) {
        const char *error = "{\"accepted\":false,\"error\":\"Cannot send the printer start request\"}\n";
        respond(fd, 503, "Service Unavailable", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    char accepted[PATH_MAX_LOCAL * 2 + 128], escaped[PATH_MAX_LOCAL * 2];
    json_escape(escaped, sizeof(escaped), print_filename);
    int accepted_len = snprintf(accepted, sizeof(accepted),
        "{\"accepted\":true,\"method\":1020,\"storage_media\":\"%s\",\"filename\":\"%s\",\"imported_from_usb\":%s,\"print_layout\":\"%c\",\"leveling\":\"%s\",\"timelapse\":%s}\n",
        print_media, escaped, strcmp(storage, "usb") == 0 ? "true" : "false",
        print_layout, leveling, timelapse ? "true" : "false");
    if (accepted_len > 0 && (size_t)accepted_len < sizeof(accepted))
        respond(fd, 202, "Accepted", "application/json; charset=utf-8", accepted, (size_t)accepted_len);
}

static const char *machine_status_name(int status) {
    static const char *names[]={"Initializing","Idle","Printing","Loading","Unloading","Auto leveling","PID calibration","Resonance test","Self check","Upgrade","Manual homing","File sending","Timelapse generation","Extruding","Emergency stop","Power-loss recovery","Component upgrade","Foreign-object detection"};
    if(status<0)return "Offline";
    return status<(int)(sizeof(names)/sizeof(names[0]))?names[status]:"Unknown";
}

static const char *find_case_insensitive(const char *text, const char *needle) {
    size_t length = strlen(needle);
    if (!length) return text;
    for (; *text; ++text)
        if (strncasecmp(text, needle, length) == 0) return text;
    return NULL;
}

static int positive_integer_after(const char *line, const char *marker) {
    const char *match = find_case_insensitive(line, marker);
    if (!match) return 0;
    match += strlen(marker);
    while (*match && !isdigit((unsigned char)*match)) match++;
    if (!*match) return 0;
    long value = strtol(match, NULL, 10);
    return value > 0 && value <= 1000000 ? (int)value : 0;
}

static double number_after_marker(const char *line, const char *marker) {
    const char *match = find_case_insensitive(line, marker);
    if (!match) return -1.0;
    match += strlen(marker);
    while (*match && !isdigit((unsigned char)*match) && *match != '.' && *match != '-' && *match != '+') match++;
    if (!*match) return -1.0;
    char *end = NULL; double value = strtod(match, &end);
    return end != match && value >= 0.0 ? value : -1.0;
}

static double bounded_number_after_marker(const char *line, const char *marker,
                                          double maximum) {
    double value = number_after_marker(line, marker);
    return value >= 0.0 && value <= maximum ? value : -1.0;
}

static long duration_after_marker(const char *line, const char *marker) {
    const char *cursor = find_case_insensitive(line, marker);
    if (!cursor) return -1;
    cursor += strlen(marker);
    long seconds = 0; int found_unit = 0;
    while (*cursor) {
        while (*cursor && !isdigit((unsigned char)*cursor) && *cursor != '.') cursor++;
        if (!*cursor) break;
        char *end = NULL; double value = strtod(cursor, &end);
        if (end == cursor) break;
        while (*end && isspace((unsigned char)*end)) end++;
        if (*end == 'h' || *end == 'H') { seconds += (long)(value * 3600.0); found_unit = 1; }
        else if (*end == 'm' || *end == 'M') { seconds += (long)(value * 60.0); found_unit = 1; }
        else if (*end == 's' || *end == 'S') { seconds += (long)value; found_unit = 1; }
        else if (!found_unit) return (long)value;
        cursor = *end ? end + 1 : end;
    }
    return found_unit ? seconds : -1;
}

static void gcode_metadata_response(int fd, const char *body, size_t body_len) {
    char storage[16], filename[PATH_MAX_LOCAL], path[PATH_MAX_LOCAL * 2], line[4096];
    const char *root, *media;
    if (!gcode_request_file(body, body_len, storage, filename, NULL, NULL, &root, &media) ||
        !gcode_resolved_path(root, filename, path, sizeof(path))) {
        const char *error = "{\"error\":\"File is unavailable or unsafe\"}\n";
        analysis_respond(fd, 404, "Not Found", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    (void)media;
    FILE *file = fopen(path, "r");
    if (!file) {
        const char *error = "{\"error\":\"Cannot open G-code file\"}\n";
        analysis_respond(fd, 500, "Internal Server Error", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    int layers = 0, maximum_layer = -1; long estimated = -1;
    double filament = -1.0, nozzle = -1.0, bed = -1.0;
    while (fgets(line, sizeof(line), file)) {
        const char *comment=line;while(isspace((unsigned char)*comment))comment++;
        if(*comment!=';')continue;
        static const char *layer_markers[] = {"total layer number", "total_layer_count", "total layers count", "total layers", "layer_count:"};
        for (size_t index = 0; index < sizeof(layer_markers)/sizeof(layer_markers[0]); ++index) {
            int value = positive_integer_after(line, layer_markers[index]); if (value > layers) layers = value;
        }
        const char *layer = find_case_insensitive(line, ";LAYER:");
        if (layer) { int value = positive_integer_after(layer, ";LAYER:"); if (value > maximum_layer) maximum_layer = value; }
        if (estimated < 0) {
            static const char *markers[] = {"estimated printing time", "estimated print time", "total print time"};
            for (size_t index = 0; index < sizeof(markers)/sizeof(markers[0]) && estimated < 0; ++index) estimated = duration_after_marker(line, markers[index]);
        }
        if (filament < 0) {
            static const char *markers[] = {"filament used [g]", "filament weight", "filament_used_g"};
            for (size_t index = 0; index < sizeof(markers)/sizeof(markers[0]) && filament < 0; ++index) filament = number_after_marker(line, markers[index]);
        }
        if (nozzle < 0) {
            nozzle = bounded_number_after_marker(line, "nozzle_temperature", 500.0);
            if (nozzle < 0) nozzle = bounded_number_after_marker(line, "first_layer_temperature", 500.0);
        }
        if (bed < 0) {
            bed = bounded_number_after_marker(line, "bed_temperature", 200.0);
            if (bed < 0) bed = bounded_number_after_marker(line, "first_layer_bed_temperature", 200.0);
        }
    }
    fclose(file);
    if (!layers && maximum_layer >= 0) layers = maximum_layer + 1;
    char layer_value[32], time_value[32], filament_value[32], nozzle_value[32], bed_value[32], response[512];
    if (layers > 0) snprintf(layer_value, sizeof(layer_value), "%d", layers); else snprintf(layer_value, sizeof(layer_value), "null");
    if (estimated >= 0) snprintf(time_value, sizeof(time_value), "%ld", estimated); else snprintf(time_value, sizeof(time_value), "null");
    if (filament >= 0) snprintf(filament_value, sizeof(filament_value), "%.3f", filament); else snprintf(filament_value, sizeof(filament_value), "null");
    if (nozzle >= 0) snprintf(nozzle_value, sizeof(nozzle_value), "%.1f", nozzle); else snprintf(nozzle_value, sizeof(nozzle_value), "null");
    if (bed >= 0) snprintf(bed_value, sizeof(bed_value), "%.1f", bed); else snprintf(bed_value, sizeof(bed_value), "null");
    int length = snprintf(response, sizeof(response), "{\"layers\":%s,\"estimated_seconds\":%s,\"filament_grams\":%s,\"nozzle_temperature\":%s,\"bed_temperature\":%s}\n", layer_value, time_value, filament_value, nozzle_value, bed_value);
    if (length > 0 && (size_t)length < sizeof(response)) analysis_respond(fd, 200, "OK", "application/json; charset=utf-8", response, (size_t)length);
}

/* Four pending requests and one 128-KiB worker stack, independent of file size.
 * The worker owns each accepted socket. No MQTT or printer state is accessed. */
#define ANALYSIS_QUEUE_MAX 4
typedef struct {int fd,kind;unsigned generation;size_t length;char body[PATH_MAX_LOCAL+32];} analysis_job;
static analysis_job analysis_queue[ANALYSIS_QUEUE_MAX];
static pthread_mutex_t analysis_mutex=PTHREAD_MUTEX_INITIALIZER;
static size_t analysis_head,analysis_count;
static int analysis_running;
static char active_analysis_filename[PATH_MAX_LOCAL];
static int active_analysis_pending, active_analysis_ready, active_analysis_layers;
static unsigned active_analysis_generation;
static long active_analysis_seconds = -1;
static void active_gcode_scan(const char *filename,int *layers,long *seconds);
static int analysis_same_file(const struct stat *a,const struct stat *b) {
    return a->st_dev==b->st_dev && a->st_ino==b->st_ino && a->st_size==b->st_size &&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec && a->st_mtim.tv_nsec==b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec && a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static void *analysis_worker(void *unused) {
    (void)unused;
    for(;;) {
        pthread_mutex_lock(&analysis_mutex);
        if(!analysis_count){analysis_running=0;pthread_mutex_unlock(&analysis_mutex);return NULL;}
        analysis_job job=analysis_queue[analysis_head];
        analysis_head=(analysis_head+1)%ANALYSIS_QUEUE_MAX;analysis_count--;
        pthread_mutex_unlock(&analysis_mutex);
        if(job.kind==2) {
            int layers=0;long seconds=-1;
            active_gcode_scan(job.body,&layers,&seconds);
            pthread_mutex_lock(&analysis_mutex);
            if(job.generation==active_analysis_generation && !strcmp(job.body,active_analysis_filename)) {
                active_analysis_layers=layers;active_analysis_seconds=seconds;active_analysis_ready=1;
            }
            active_analysis_pending=0;
            pthread_mutex_unlock(&analysis_mutex);
            continue;
        }
        char storage[16],name[PATH_MAX_LOCAL],path[PATH_MAX_LOCAL*2];
        const char *root,*media;struct stat before,after;
        int keyed=gcode_request_file(job.body,job.length,storage,name,NULL,NULL,&root,&media) &&
            gcode_resolved_path(root,name,path,sizeof(path)) && stat(path,&before)==0;
        analysis_cache *cache=&analysis_cached[job.kind];
        if(keyed && cache->valid && !strcmp(path,cache->path) && analysis_same_file(&before,&cache->stamp)) {
            respond(job.fd,200,"OK","application/json; charset=utf-8",cache->response,cache->length);
        } else {
            cache->valid=0;cache->length=0;analysis_capture=cache;
            if(job.kind)gcode_metadata_response(job.fd,job.body,job.length);
            else gcode_inspect_response(job.fd,job.body,job.length);
            analysis_capture=NULL;
            if(keyed && cache->length && stat(path,&after)==0 && analysis_same_file(&before,&after)) {
                snprintf(cache->path,sizeof(cache->path),"%s",path);cache->stamp=after;cache->valid=1;
            }
        }
        close(job.fd);
    }
}
static int analysis_start(int fd,int kind,const char *body,size_t length) {
    if(length>=sizeof(analysis_queue[0].body)) {
        const char *error="{\"error\":\"File request too large\"}\n";
        respond(fd,400,"Bad Request","application/json",error,strlen(error));return 0;
    }
    pthread_mutex_lock(&analysis_mutex);
    if(analysis_count==ANALYSIS_QUEUE_MAX) {
        pthread_mutex_unlock(&analysis_mutex);
        const char *error="{\"error\":\"File analysis busy; retry shortly\"}\n";
        respond(fd,503,"Service Unavailable","application/json",error,strlen(error));return 0;
    }
    analysis_job *job=&analysis_queue[(analysis_head+analysis_count)%ANALYSIS_QUEUE_MAX];
    job->fd=fd;job->kind=kind;job->generation=active_analysis_generation;job->length=length;memcpy(job->body,body,length);job->body[length]=0;
    struct timeval timeout={5,0};setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    analysis_count++;
    if(!analysis_running) {
        pthread_attr_t attr;pthread_t thread;
        int ok=pthread_attr_init(&attr);
        if(ok==0){ok=pthread_attr_setstacksize(&attr,128*1024);if(ok==0)ok=pthread_create(&thread,&attr,analysis_worker,NULL);pthread_attr_destroy(&attr);}
        if(ok){analysis_count--;pthread_mutex_unlock(&analysis_mutex);const char *error="{\"error\":\"Cannot start file analysis\"}\n";respond(fd,503,"Service Unavailable","application/json",error,strlen(error));return 0;}
        analysis_running=1;pthread_detach(thread);
    }
    pthread_mutex_unlock(&analysis_mutex);return 1;
}

/* Elegoo's print_status does not consistently expose the total layer count.
 * Cache it from the active local G-code instead of leaving demo data in the UI. */
static long active_gcode_estimated_seconds = -1;

static void active_gcode_scan(const char *filename,int *layers,long *seconds) {
    char path[PATH_MAX_LOCAL * 2], line[4096];
    if (!gcode_resolved_path(gcode_internal_root, filename, path, sizeof(path))) return;
    FILE *file = fopen(path, "r");
    if (!file) return;
    int maximum_layer = -1;
    while (fgets(line, sizeof(line), file)) {
        const char *comment=line;while(isspace((unsigned char)*comment))comment++;
        if(*comment!=';')continue;
        if(*seconds < 0){
            static const char *time_markers[]={"estimated printing time","estimated print time","total print time"};
            for(size_t i=0;i<sizeof(time_markers)/sizeof(time_markers[0])&&*seconds<0;i++)
                *seconds=duration_after_marker(line,time_markers[i]);
        }
        static const char *markers[] = {
            "total layer number", "total_layer_count", "total layers count",
            "total layers", "layer_count:"
        };
        for (size_t index = 0; index < sizeof(markers)/sizeof(markers[0]); ++index) {
            int value = positive_integer_after(line, markers[index]);
            if (value > *layers) *layers = value;
        }
        const char *layer = find_case_insensitive(line, ";LAYER:");
        if (layer) {
            layer += 7;
            while (*layer && isspace((unsigned char)*layer)) layer++;
            if (isdigit((unsigned char)*layer)) {
                long value = strtol(layer, NULL, 10);
                if (value >= 0 && value < 1000000 && value > maximum_layer)
                    maximum_layer = (int)value;
            }
        }
    }
    fclose(file);
    if (!*layers && maximum_layer >= 0) *layers = maximum_layer + 1;
}

static int active_gcode_total_layers(const char *filename) {
    int layers=0,submit=0;active_gcode_estimated_seconds=-1;
    pthread_mutex_lock(&analysis_mutex);
    if(!filename || !filename[0]) {
        if(active_analysis_filename[0])active_analysis_generation++;
        active_analysis_filename[0]=0;active_analysis_ready=0;
    } else {
        if(strcmp(filename,active_analysis_filename)) {
            active_analysis_generation++;
            snprintf(active_analysis_filename,sizeof(active_analysis_filename),"%s",filename);
            active_analysis_ready=0;
        }
        if(active_analysis_ready) {
            layers=active_analysis_layers;active_gcode_estimated_seconds=active_analysis_seconds;
        } else if(!active_analysis_pending) {
            active_analysis_pending=1;submit=1;
        }
    }
    pthread_mutex_unlock(&analysis_mutex);
    if(submit && !analysis_start(-1,2,filename,strlen(filename))) {
        pthread_mutex_lock(&analysis_mutex);active_analysis_pending=0;pthread_mutex_unlock(&analysis_mutex);
    }
    return layers;
}

#include "recovery.h"

/* The page that last started live view. The UI streams the camera from one
 * page at a time: a page that sees another viewer in /api/printer stops its own
 * stream. Only the main loop touches it, so it needs no lock. */
static char camera_viewer[41];

static int camera_viewer_valid(const char *id, size_t length) {
    if (length < 8 || length >= sizeof(camera_viewer)) return 0;
    for (size_t i = 0; i < length; ++i)
        if (!(isdigit((unsigned char)id[i]) || (id[i] >= 'a' && id[i] <= 'z') || id[i] == '-')) return 0;
    return 1;
}

static void camera_claim_response(int fd, const char *body, size_t length) {
    while (length && isspace((unsigned char)body[length - 1])) length--;
    if (!camera_viewer_valid(body, length)) {
        static const char error[] = "{\"error\":\"Invalid camera viewer\"}\n";
        respond(fd, 400, "Bad Request", "application/json; charset=utf-8", error, sizeof(error) - 1);
        return;
    }
    memcpy(camera_viewer, body, length);
    camera_viewer[length] = '\0';
    char reply[64];
    int n = snprintf(reply, sizeof(reply), "{\"viewer\":\"%s\"}\n", camera_viewer);
    if (n > 0 && (size_t)n < sizeof(reply))
        respond(fd, 200, "OK", "application/json; charset=utf-8", reply, (size_t)n);
}

static void spools_summary(char *out, size_t cap);

static void printer_response(int fd, const mqtt_client *mqtt) {
    /* Render from the two caches without copying MQTT transport/history buffers. */
    double live;
    int progress=mqtt->progress,current_layer=mqtt->current_layer;
    long duration=mqtt->print_duration;
    if(uds_job_matches(&telemetry,mqtt->filename)){
        if(uds_value(&telemetry,U_PROGRESS,&live))progress=(int)(live*100);
        if(uds_value(&telemetry,U_LAYER,&live))current_layer=(int)live;
        if(uds_value(&telemetry,U_DURATION,&live))duration=(long)live;
    }
    char et[32], eg[32], bt[32], bg[32], ct[32], cf[32], hf[32], pf[32];
    char body[6144],filename[520],state[140],uuid[260],axes[40];
    if(uds_value(&telemetry,U_ET,&live))json_number(et,sizeof(et),1,live);
    else json_number(et,sizeof(et),mqtt->have_extruder_temp,mqtt->extruder_temp);
    if(uds_value(&telemetry,U_EG,&live))json_number(eg,sizeof(eg),1,live);
    else json_number(eg,sizeof(eg),mqtt->have_extruder_target,mqtt->extruder_target);
    if(uds_value(&telemetry,U_BT,&live))json_number(bt,sizeof(bt),1,live);
    else json_number(bt,sizeof(bt),mqtt->have_bed_temp,mqtt->bed_temp);
    if(uds_value(&telemetry,U_BG,&live))json_number(bg,sizeof(bg),1,live);
    else json_number(bg,sizeof(bg),mqtt->have_bed_target,mqtt->bed_target);
    json_number(ct,sizeof(ct),mqtt->have_chamber_temp,mqtt->chamber_temp);
    if(uds_value(&telemetry,U_CF,&live))json_number(cf,sizeof(cf),1,live*255);
    else json_number(cf,sizeof(cf),mqtt->have_controller_fan,mqtt->controller_fan);
    if(uds_value(&telemetry,U_HF,&live))json_number(hf,sizeof(hf),1,live*255);
    else json_number(hf,sizeof(hf),mqtt->have_heater_fan,mqtt->heater_fan);
    if(uds_value(&telemetry,U_PF,&live))json_number(pf,sizeof(pf),1,live*255);
    else json_number(pf,sizeof(pf),mqtt->have_part_fan,mqtt->part_fan);
    json_escape(filename,sizeof(filename),mqtt->filename);json_escape(state,sizeof(state),mqtt->print_state);
    json_escape(uuid,sizeof(uuid),mqtt->uuid);json_escape(axes,sizeof(axes),mqtt->homed_axes);
    time_t now=time(NULL);
    long age=mqtt->last_message ? (long)(now-mqtt->last_message) : -1;
    int file_layers=active_gcode_total_layers(mqtt->filename);
    int total_layers=mqtt->have_total_layers ? mqtt->total_layers : file_layers;
    long remaining=mqtt->remaining_time;
    const char *remaining_source=remaining>0 ? "mqtt" : "unavailable";
    int active_job=mqtt->connected&&mqtt->registered&&mqtt->last_message>0&&
        now-mqtt->last_message<=15&&mqtt->have_machine_status&&mqtt->machine_status==2&&
        mqtt->filename[0]&&(!strcmp(mqtt->print_state,"printing")||!strcmp(mqtt->print_state,"paused"));
    if(active_job&&remaining<=0&&active_gcode_estimated_seconds>0&&duration>=0){
        remaining=active_gcode_estimated_seconds>duration ? active_gcode_estimated_seconds-duration : 0;
        remaining_source="gcode";
    }
    char speed_percent[40],flow_percent[40],live_velocity[40];double tune;
    int have_speed=uds_value(&telemetry,U_SPEED_FACTOR,&tune);
    json_number(speed_percent,sizeof(speed_percent),have_speed,have_speed?tune*100:0);
    int have_flow=uds_value(&telemetry,U_FLOW_FACTOR,&tune);
    json_number(flow_percent,sizeof(flow_percent),have_flow,have_flow?tune*100:0);
    int have_velocity=uds_value(&telemetry,U_LIVE_SPEED,&tune);
    json_number(live_velocity,sizeof(live_velocity),have_velocity,have_velocity?tune:0);
    char zoffset[32],zreference[32],zadjustment[32];double offset;
    int have_offset=z_offset_readback(&offset);
    json_z_offset(zoffset,sizeof(zoffset),have_offset,have_offset?offset:0);
    double reference=z_offset_session?z_offset_reference:(have_offset?offset:0);
    json_z_offset(zreference,sizeof(zreference),have_offset,reference);
    json_z_offset(zadjustment,sizeof(zadjustment),have_offset,have_offset?offset-reference:0);
    /* The latest refusal of one of our MQTT requests (start, auto refill, history, time-lapse). */
    char printer_error[160]="null";
    if(mqtt->reply_errors)snprintf(printer_error,sizeof(printer_error),"{\"sequence\":%lu,\"method\":%d,\"code\":%d,\"age\":%ld}",
        mqtt->reply_errors,mqtt->reply_error_method,mqtt->reply_error_code,(long)(now-mqtt->reply_error_time));
    char printer_report[1280]="null";
    if(telemetry.report_sequence){
        struct timespec received_now;clock_gettime(CLOCK_MONOTONIC,&received_now);
        long report_age=received_now.tv_sec-telemetry.report_received.tv_sec;
        snprintf(printer_report,sizeof(printer_report),
            "{\"event_id\":\"%ld.%09ld-%lu\",\"sequence\":%lu,\"code\":%d,\"level\":%d,\"message\":%s,\"age\":%ld}",
            (long)telemetry.report_identity.tv_sec,telemetry.report_identity.tv_nsec,telemetry.report_sequence,
            telemetry.report_sequence,telemetry.report_code,telemetry.report_level,
            telemetry.report_message,report_age<0?0:report_age);
    }
    char viewer[sizeof(camera_viewer)+2];
    if(camera_viewer[0])snprintf(viewer,sizeof(viewer),"\"%s\"",camera_viewer);
    else snprintf(viewer,sizeof(viewer),"null");
    char spool_state[128];
    spools_summary(spool_state,sizeof(spool_state));
    int length=snprintf(body,sizeof(body),
        "{\"connected\":%s,\"messages\":%lu,\"last_message_age\":%ld,"
        "\"extruder\":{\"temperature\":%s,\"target\":%s},"
        "\"heater_bed\":{\"temperature\":%s,\"target\":%s},"
        "\"chamber\":{\"temperature\":%s},"
        "\"fans\":{\"controller\":%s,\"heater\":%s,\"part\":%s,\"aux\":%.1f,\"box\":%.1f},"
        "\"machine\":{\"status\":%d,\"status_name\":\"%s\",\"sub_status\":%d,\"reason\":%d,\"progress\":%d},"
        "\"print\":{\"enabled\":%s,\"filename\":\"%s\",\"state\":\"%s\",\"uuid\":\"%s\",\"current_layer\":%d,\"total_layers\":%d,\"duration\":%ld,\"remaining\":%ld,\"remaining_source\":\"%s\",\"total_duration\":%ld},"
        "\"motion\":{\"x\":%.3f,\"y\":%.3f,\"z\":%.3f,\"speed\":%.1f,\"speed_mode\":%d,\"homed_axes\":\"%s\"},"
        "\"tuning\":{\"speed_percent\":%s,\"flow_percent\":%s,\"live_velocity\":%s},"
        "\"z_offset\":{\"value\":%s,\"pending\":%s,\"timed_out\":%s,\"reference\":%s,\"adjustment\":%s},"
        "\"recovery\":{\"available\":%s,\"reboot_pending\":%s,\"error\":\"%s\"},"
        "\"hardware\":{\"camera\":%s,\"usb\":%s,\"light\":%d,\"filament_detection\":%s,\"filament_detected\":%s},"
        "\"spools\":%s,\"printer_report\":%s,\"printer_error\":%s,\"camera_viewer\":%s}\n",
        mqtt->connected?"true":"false",mqtt->messages,age,et,eg,bt,bg,ct,cf,hf,pf,
        mqtt->aux_fan,mqtt->box_fan,mqtt->machine_status,machine_status_name(mqtt->machine_status),mqtt->sub_status,mqtt->sub_status_reason,progress,
        mqtt->print_enabled?"true":"false",filename,state,uuid,current_layer,total_layers,duration,remaining,remaining_source,mqtt->total_duration,
        mqtt->x,mqtt->y,mqtt->z,mqtt->move_speed,mqtt->speed_mode,axes,speed_percent,flow_percent,live_velocity,zoffset,z_offset_pending?"true":"false",z_offset_timed_out?"true":"false",zreference,zadjustment,recovery_available()?"true":"false",reboot_pending?"true":"false",reboot_error,mqtt->camera?"true":"false",mqtt->u_disk?"true":"false",mqtt->led_status,
        mqtt->filament_detect_enabled?"true":"false",mqtt->filament_detected?"true":"false",spool_state,printer_report,
        printer_error,viewer);
    if(length>0&&(size_t)length<sizeof(body))
        respond(fd,200,"OK","application/json; charset=utf-8",body,(size_t)length);
}

static void uds_response(int fd){
    static const char *names[U_FIELDS]={"nozzle_temperature","nozzle_target","bed_temperature","bed_target",
        "controller_fan","heater_fan","part_fan","fan1","controller_rpm","heater_rpm","part_rpm","fan1_rpm",
        "speed_factor","extrude_factor","live_velocity","progress","current_layer","print_duration","total_elapsed","z_offset",
        "filament_used","canvas_channel"};
    char body[2048];json_builder b={body,0,sizeof(body),0};
    json_builder_printf(&b,"{\"connected\":%s,\"fresh\":%s,\"messages\":%lu,\"connections\":%lu,\"disconnects\":%lu,\"last_disconnect\":\"%s\",\"last_errno\":%d,\"ignored_messages\":%lu,\"values\":{",
        telemetry.fd>=0?"true":"false",uds_fresh(&telemetry)?"true":"false",telemetry.messages,
        telemetry.connections,telemetry.disconnects,telemetry.last_disconnect?telemetry.last_disconnect:"none",telemetry.last_errno,telemetry.ignored_messages);
    for(int i=0;i<U_FIELDS;i++){
        double value;int have=uds_value(&telemetry,(enum uds_field)i,&value);
        json_builder_printf(&b,"%s\"%s\":",i?",":"",names[i]);
        if(have)json_builder_printf(&b,"%.6f",value);else json_builder_printf(&b,"null");
    }
    const char *state=uds_print_state(&telemetry);
    json_builder_printf(&b,"},\"print_state\":");
    if(state)json_builder_printf(&b,"\"%s\"",state);else json_builder_printf(&b,"null");
    json_builder_printf(&b,"}\n");
    if(!b.failed)respond(fd,200,"OK","application/json",body,b.length);
}

static void snapshot_response(int fd, const mqtt_client *mqtt) {
    if(mqtt->snapshot_len)
        respond(fd,200,"OK","application/json; charset=utf-8",mqtt->snapshot,mqtt->snapshot_len);
    else {
        const char *body="{\"available\":false}\n";
        respond(fd,200,"OK","application/json; charset=utf-8",body,strlen(body));
    }
}

static void mqtt_diagnostic_response(int fd, const mqtt_client *mqtt) {
    size_t cap=mqtt->diagnostic_len+512;
    char *body=malloc(cap);
    if(!body){respond(fd,503,"Service Unavailable","text/plain","Diagnostic unavailable\n",23);return;}
    size_t n=mqtt_discovery_diagnostic(mqtt,body,cap);
    if(mqtt->diagnostic_len){memcpy(body+n,mqtt->diagnostic,mqtt->diagnostic_len);n+=mqtt->diagnostic_len;}
    respond(fd,200,"OK","text/plain; charset=utf-8",body,n);
    free(body);
}

static void canvas_response(int fd, const mqtt_client *mqtt) {
    if(mqtt->canvas_snapshot_len) {
        size_t cap=mqtt->canvas_snapshot_len+256;
        char *body=malloc(cap);
        if(!body)return;
        int length=snprintf(body,cap,
            "{\"available\":true,\"active_tray_id\":%d,\"machine_status\":%d,\"auto_refill\":%s,\"telemetry\":%.*s}\n",
            mqtt->have_canvas_active_tray?mqtt->canvas_active_tray_id:-1,mqtt->machine_status,
            mqtt->have_auto_refill?(mqtt->auto_refill?"true":"false"):"null",
            (int)mqtt->canvas_snapshot_len,mqtt->canvas_snapshot);
        if(length>0&&(size_t)length<cap)
            respond(fd,200,"OK","application/json; charset=utf-8",body,(size_t)length);
        free(body);
    } else {
        const char *body="{\"available\":false}\n";
        respond(fd,200,"OK","application/json; charset=utf-8",body,strlen(body));
    }
}

/* OrcaSlicer's Moonraker printer agent connects through /server/info and pulls
 * filament slots from the AFC lane_data namespace. Lanes are built from the
 * connected Canvas module in the cached snapshot, the same module the web UI shows. */
static const char *json_string_end(const char *p,const char *end){
    for(++p;p<end;++p){
        if(*p=='\\'){if(++p>=end)return NULL;}
        else if(*p=='"')return p+1;
    }
    return NULL;
}

static const char *json_container_end(const char *p,const char *end){
    int depth=0;
    while(p<end){
        if(*p=='"'){p=json_string_end(p,end);if(!p)return NULL;continue;}
        if(*p=='{'||*p=='[')depth++;
        else if((*p=='}'||*p==']')&&--depth==0)return p+1;
        p++;
    }
    return NULL;
}

static const char *json_skip_space(const char *p,const char *end){
    while(p<end&&isspace((unsigned char)*p))p++;
    return p;
}

/* Returns the start of the value of a top-level member of the object [obj,end). */
static const char *json_member(const char *obj,const char *end,const char *key){
    size_t key_len=strlen(key);
    const char *p=obj+1;
    while(1){
        p=json_skip_space(p,end);
        if(p<end&&*p==',')p=json_skip_space(p+1,end);
        if(p>=end||*p!='"')return NULL;
        const char *key_end=json_string_end(p,end);if(!key_end)return NULL;
        int match=(size_t)(key_end-p-2)==key_len&&memcmp(p+1,key,key_len)==0;
        p=json_skip_space(key_end,end);
        if(p>=end||*p!=':')return NULL;
        p=json_skip_space(p+1,end);
        if(p>=end)return NULL;
        if(match)return p;
        if(*p=='{'||*p=='[')p=json_container_end(p,end);
        else if(*p=='"')p=json_string_end(p,end);
        else while(p<end&&*p!=','&&*p!='}')p++;
        if(!p)return NULL;
    }
}

static const char *json_member_object(const char *obj,const char *end,const char *key,char open,const char **value_end){
    const char *value=json_member(obj,end,key);
    if(!value||*value!=open)return NULL;
    *value_end=json_container_end(value,end);
    return *value_end?value:NULL;
}

static int json_member_int(const char *obj,const char *end,const char *key,int *out){
    const char *value=json_member(obj,end,key);
    if(!value||!(isdigit((unsigned char)*value)||*value=='-'))return 0;
    *out=atoi(value);return 1;
}

/* Raw JSON string contents without quotes; escapes stay as they are. */
static int json_member_raw_string(const char *obj,const char *end,const char *key,const char **text,int *length){
    const char *value=json_member(obj,end,key);
    if(!value||*value!='"')return 0;
    const char *value_end=json_string_end(value,end);
    if(!value_end)return 0;
    *text=value+1;*length=(int)(value_end-value-2);return 1;
}

/* Iterates the elements of the array [array,end). */
static const char *json_next_element(const char *p,const char *end){
    p=json_skip_space(p,end);
    if(p<end&&(*p=='['||*p==','))p=json_skip_space(p+1,end);
    return p<end&&*p=='{'?p:NULL;
}

static void lane_data_build(const mqtt_client *mqtt,json_builder *builder){
    const char *snapshot=mqtt->canvas_snapshot,*end=snapshot+mqtt->canvas_snapshot_len;
    const char *root=json_skip_space(snapshot,end),*root_end,*result_end,*info_end,*list_end,*trays_end;
    if(root>=end||*root!='{'||!(root_end=json_container_end(root,end)))return;
    const char *result=json_member_object(root,root_end,"result",'{',&result_end);
    const char *info=result?json_member_object(result,result_end,"canvas_info",'{',&info_end):NULL;
    const char *list=info?json_member_object(info,info_end,"canvas_list",'[',&list_end):NULL;
    if(!list)return;
    const char *module=NULL,*module_end=NULL;
    for(const char *item=json_next_element(list,list_end);item;){
        const char *item_end=json_container_end(item,list_end);
        if(!item_end)return;
        int connected=0,is_connected=json_member_int(item,item_end,"connected",&connected)&&connected==1;
        if(!module||is_connected){module=item;module_end=item_end;}
        if(is_connected)break;
        item=json_next_element(item_end,list_end);
    }
    const char *trays=module?json_member_object(module,module_end,"tray_list",'[',&trays_end):NULL;
    if(!trays)return;
    int count=0;
    for(const char *tray=json_next_element(trays,trays_end);tray;){
        const char *tray_end=json_container_end(tray,trays_end);
        if(!tray_end)return;
        int tray_id,nozzle=0,material_len=0,color_len=0;
        const char *material="",*color="";
        if(json_member_int(tray,tray_end,"tray_id",&tray_id)&&tray_id>=0){
            json_member_raw_string(tray,tray_end,"filament_type",&material,&material_len);
            json_member_raw_string(tray,tray_end,"filament_color",&color,&color_len);
            json_member_int(tray,tray_end,"max_nozzle_temp",&nozzle);
            json_builder_printf(builder,"%s\"lane%d\":{\"lane\":\"%d\",\"material\":\"%.*s\",\"color\":\"%.*s\",\"nozzle_temp\":%d,\"bed_temp\":0}",
                count++?",":"",tray_id+1,tray_id,material_len,material,color_len,color,nozzle);
        }
        tray=json_next_element(tray_end,trays_end);
    }
}

static void orca_server_info_response(int fd, const mqtt_client *mqtt) {
    int ready=mqtt->connected&&mqtt->registered;
    char body[160];
    int length=snprintf(body,sizeof(body),
        "{\"result\":{\"klippy_connected\":%s,\"klippy_state\":\"%s\",\"hostname\":\"CC2\",\"components\":[]}}\n",
        ready?"true":"false",ready?"ready":"disconnected");
    if(length>0&&(size_t)length<sizeof(body))
        respond(fd,200,"OK","application/json; charset=utf-8",body,(size_t)length);
}

static void orca_lane_data_response(int fd, const mqtt_client *mqtt) {
    static const char prefix[]="{\"result\":{\"namespace\":\"lane_data\",\"value\":{";
    size_t cap=sizeof(prefix)+mqtt->canvas_snapshot_len*4+64;
    char *data=malloc(cap);
    if(!data)return;
    json_builder builder={data,0,cap,0};
    json_builder_printf(&builder,"%s",prefix);
    size_t lanes_start=builder.length;
    lane_data_build(mqtt,&builder);
    if(builder.failed){builder.failed=0;builder.length=lanes_start;}
    json_builder_printf(&builder,"}}}\n");
    if(!builder.failed)respond(fd,200,"OK","application/json; charset=utf-8",data,builder.length);
    free(data);
}

static void canvas_refresh_response(int fd, mqtt_client *mqtt) {
    if(mqtt_sync_canvas(mqtt)!=0) {
        const char *body="{\"accepted\":false,\"error\":\"MQTT API client is not ready\"}\n";
        respond(fd,503,"Service Unavailable","application/json; charset=utf-8",body,strlen(body));return;
    }
    const char *body="{\"accepted\":true,\"method\":2005}\n";
    respond(fd,202,"Accepted","application/json; charset=utf-8",body,strlen(body));
}

/* Canvas auto refill: switch to another slot with the same filament when one runs out.
 * Only offered once the printer has reported the setting (fail closed on old firmware). */
static void canvas_auto_refill_response(int fd,mqtt_client *mqtt,const char *body,size_t body_len){
    while(body_len&&isspace((unsigned char)*body)){body++;body_len--;}
    while(body_len&&isspace((unsigned char)body[body_len-1]))body_len--;
    int enabled=body_len==2&&!memcmp(body,"on",2)?1:body_len==3&&!memcmp(body,"off",3)?0:-1;
    const char *error=NULL;int status=409;
    if(enabled<0){error="{\"accepted\":false,\"error\":\"Use on or off\"}\n";status=400;}
    else if(!mqtt->have_auto_refill)error="{\"accepted\":false,\"error\":\"The printer has not reported Canvas auto refill\"}\n";
    else if(mqtt_set_auto_refill(mqtt,enabled)!=0){error="{\"accepted\":false,\"error\":\"MQTT API client is not ready\"}\n";status=503;}
    if(error){respond(fd,status,status==400?"Bad Request":status==503?"Service Unavailable":"Conflict","application/json; charset=utf-8",error,strlen(error));return;}
    const char *ok="{\"accepted\":true,\"method\":2004}\n";
    respond(fd,202,"Accepted","application/json; charset=utf-8",ok,strlen(ok));
}

#include "history.h"

#define EXCLUDE_OBJECT_RESPONSE_MAX (256UL * 1024UL)

static char *exclude_objects_cache = NULL;
static size_t exclude_objects_cache_len = 0;
static char exclude_objects_cache_job[256] = "";
static long long exclude_objects_asked_ms = LLONG_MIN / 2, exclude_objects_wait_ms = 5000;

/* The object list is cached for the whole job above.  The excluded/current
 * fields come from the telemetry subscription; while it is unavailable they
 * need a synchronous UDS round trip.  Share a very short-lived copy between
 * HTTP clients so several dashboards cannot multiply that printer-side work. */
static char *exclude_dynamic_cache = NULL;
static size_t exclude_dynamic_cache_len = 0;
static char exclude_dynamic_cache_job[256] = "";
static long long exclude_dynamic_cache_ms = 0;

static long long monotonic_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (long long)ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL;
}

/* Separate from telemetry: object replies can be much larger than subscriptions.
 * Keep successful RPC sessions open to avoid printer-side reactor churn. */
static int object_query_fd = -1;
static const char *object_query_path = "/tmp/elegoo_uds";
static long long object_query_retry_ms;
static unsigned long object_query_id = 1000;
static char *object_query_buffer;
static size_t object_query_used, object_query_capacity;

static void object_query_close(void) {
    if (object_query_fd >= 0) close(object_query_fd);
    object_query_fd = -1;
    free(object_query_buffer);
    object_query_buffer = NULL;
    object_query_used = object_query_capacity = 0;
}

static int object_query_wait(short events, long long deadline) {
    for (;;) {
        long long remaining = deadline - monotonic_ms();
        if (remaining <= 0) return -1;
        struct pollfd p = {object_query_fd, events, 0};
        int rc = poll(&p, 1, (int)remaining);
        if (rc < 0 && errno == EINTR) continue;
        if (rc <= 0 || !(p.revents & events)) return -1;
        return 0;
    }
}

static int uds_query_json(const char *query, char **result_out, size_t *length_out) {
    *result_out = NULL; *length_out = 0;
    long long now = monotonic_ms(), deadline = now + 2000;
    if (object_query_fd < 0) {
        if (now < object_query_retry_ms) return -1;
        struct sockaddr_un address; memset(&address, 0, sizeof(address));
        address.sun_family = AF_UNIX;
        if (strlen(object_query_path) >= sizeof(address.sun_path)) return -1;
        strcpy(address.sun_path, object_query_path);
        object_query_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (object_query_fd < 0) goto failed;
        if (fcntl(object_query_fd, F_SETFL, O_NONBLOCK) < 0) goto failed;
        if (connect(object_query_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
            if (errno != EINPROGRESS || object_query_wait(POLLOUT, deadline)) goto failed;
            int error = 0; socklen_t length = sizeof(error);
            if (getsockopt(object_query_fd, SOL_SOCKET, SO_ERROR, &error, &length) || error) goto failed;
        }
    }
    /* Replace the fixed caller ID with a unique session request ID. */
    const char *rest = strchr(query, ',');
    if (!rest || object_query_id == ULONG_MAX) goto failed;
    unsigned long id = ++object_query_id;
    char request[512];
    int size = snprintf(request, sizeof(request), "{\"id\":%lu%s", id, rest);
    if (size < 0 || (size_t)size >= sizeof(request)) goto failed;
    size_t sent = 0;
    while (sent < (size_t)size) {
        if (object_query_wait(POLLOUT, deadline)) goto failed;
        ssize_t n = send(object_query_fd, request + sent, (size_t)size - sent, MSG_NOSIGNAL);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n <= 0) goto failed;
        sent += (size_t)n;
    }
    for (;;) {
        if (monotonic_ms() >= deadline) goto failed;
        char *terminator = object_query_used ? memchr(object_query_buffer, 3, object_query_used) : NULL;
        if (terminator) {
            size_t length = (size_t)(terminator - object_query_buffer);
            const char *end = object_query_buffer + length;
            const char *root = json_skip_space(object_query_buffer, end);
            const char *root_end = root < end && *root == '{' ? json_container_end(root, end) : NULL;
            const char *value = root_end && json_skip_space(root_end, end) == end ?
                json_member(root, root_end, "id") : NULL;
            char *number_end = NULL;
            unsigned long received = value ? strtoul(value, &number_end, 10) : 0;
            int matches = value && isdigit((unsigned char)*value) && received == id &&
                number_end < root_end && (*number_end == ',' || *number_end == '}' || isspace((unsigned char)*number_end));
            char *result = matches ? malloc(length + 1) : NULL;
            if (matches && !result) goto failed;
            if (matches) { memcpy(result, object_query_buffer, length); result[length] = 0; }
            size_t consumed = length + 1;
            memmove(object_query_buffer, object_query_buffer + consumed, object_query_used - consumed);
            object_query_used -= consumed;
            if (matches) { *result_out = result; *length_out = length; return 0; }
            continue; /* Notifications and responses to other IDs are not our reply. */
        }
        if (object_query_used == object_query_capacity) {
            if (object_query_capacity >= EXCLUDE_OBJECT_RESPONSE_MAX) goto failed;
            size_t next = object_query_capacity ? object_query_capacity * 2 : 16384;
            if (next > EXCLUDE_OBJECT_RESPONSE_MAX) next = EXCLUDE_OBJECT_RESPONSE_MAX;
            char *result = object_query_buffer;
            char *grown = realloc(result, next + 1);
            if (!grown) goto failed;
            object_query_buffer = grown; object_query_capacity = next;
        }
        if (object_query_wait(POLLIN, deadline)) goto failed;
        ssize_t n = recv(object_query_fd, object_query_buffer + object_query_used,
            object_query_capacity - object_query_used, 0);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n <= 0) goto failed;
        object_query_used += (size_t)n;
        object_query_buffer[object_query_used] = 0;
    }
failed:
    object_query_close();
    object_query_retry_ms = monotonic_ms() + 5000;
    return -1;
}

static const char *exclude_object_payload(char *json, size_t length) {
    const char marker[] = "\"exclude_object\"";
    char *cursor = strstr(json, marker);
    if (!cursor || (size_t)(cursor - json) >= length) return NULL;
    cursor += sizeof(marker) - 1;
    while ((size_t)(cursor - json) < length && isspace((unsigned char)*cursor)) cursor++;
    if ((size_t)(cursor - json) >= length || *cursor++ != ':') return NULL;
    while ((size_t)(cursor - json) < length && isspace((unsigned char)*cursor)) cursor++;
    return (size_t)(cursor - json) < length && *cursor == '{' ? cursor : NULL;
}

static int cache_exclude_objects(const char *job) {
    static const char query[] =
        "{\"id\":202,\"method\":\"objects/query\",\"params\":{\"objects\":{\"exclude_object\":[\"objects\"]}}}\003";
    char *response = NULL; size_t response_len = 0;
    if (uds_query_json(query, &response, &response_len) != 0) return -1;
    const char *payload = exclude_object_payload(response, response_len);
    if (!payload) { free(response); return -1; }
    const char *payload_end = json_container_end(payload, response + response_len);
    const char *objects = json_member(payload, payload_end, "objects"), *objects_end = NULL;
    /* The printer reports null until a job defines its objects. */
    if (objects && payload_end - objects >= 4 && memcmp(objects, "null", 4) == 0) {
        objects = "[]"; objects_end = objects + 2;
    } else if (objects && *objects == '[') {
        objects_end = json_container_end(objects, payload_end);
    }
    if (!objects_end) { free(response); return -1; }
    size_t objects_len = (size_t)(objects_end - objects);
    char *copy = malloc(objects_len + 1);
    if (!copy) { free(response); return -1; }
    memcpy(copy, objects, objects_len); copy[objects_len] = '\0';
    free(response);
    free(exclude_objects_cache);
    exclude_objects_cache = copy;
    exclude_objects_cache_len = objects_len;
    snprintf(exclude_objects_cache_job, sizeof(exclude_objects_cache_job), "%s", job ? job : "");
    return 0;
}

static void exclude_objects_response(int fd, const mqtt_client *mqtt) {
    const char *job = mqtt ? mqtt->filename : "";
    long long now_ms = monotonic_ms();
    /* The printer keeps every UDS request in memory, so the list is asked once
     * per job and at most every five seconds. An empty list may predate the job's
     * EXCLUDE_OBJECT_DEFINE: it is asked again after 5 s, doubling to 5 min, or
     * after 5 s once the stream reports a current object. */
    int new_job = !exclude_objects_cache || strcmp(exclude_objects_cache_job, job) != 0;
    int empty = !new_job && strcmp(exclude_objects_cache, "[]") == 0;
    int in_object = telemetry.have_current_object && telemetry.current_object[0] == '"';
    long long since = now_ms - exclude_objects_asked_ms;
    if (since >= 5000 && (new_job || (empty && (in_object || since >= exclude_objects_wait_ms)))) {
        exclude_objects_asked_ms = now_ms;
        exclude_objects_wait_ms = new_job || exclude_objects_wait_ms < 5000 ? 5000 :
            exclude_objects_wait_ms >= 150000 ? 300000 : exclude_objects_wait_ms * 2;
        if (cache_exclude_objects(job) == 0) new_job = 0;
    }
    if (new_job) {
        const char *error = "{\"available\":false,\"error\":\"Object definitions unavailable\"}\n";
        respond(fd,503,"Service Unavailable","application/json; charset=utf-8",error,strlen(error));
        return;
    }

    static const char query[] =
        "{\"id\":203,\"method\":\"objects/query\",\"params\":{\"objects\":{\"exclude_object\":[\"excluded_objects\",\"current_object\"]}}}\003";
    char *dynamic = NULL; size_t dynamic_len = 0;
    int dynamic_owned = 0;
    /* The subscription carries the excluded and current objects; the query below
     * is only the fallback while that stream is unavailable. */
    char live[sizeof(telemetry.excluded_objects) + sizeof(telemetry.current_object) + 96];
    int live_len = uds_exclude_status(&telemetry, live, sizeof(live));
    int cache_fresh = exclude_dynamic_cache &&
        strcmp(exclude_dynamic_cache_job, job) == 0 &&
        now_ms > 0 && exclude_dynamic_cache_ms > 0 &&
        now_ms - exclude_dynamic_cache_ms < 1000;
    if (live_len > 0) {
        dynamic = live;
        dynamic_len = (size_t)live_len;
    } else if (cache_fresh) {
        dynamic = exclude_dynamic_cache;
        dynamic_len = exclude_dynamic_cache_len;
    } else {
        if (uds_query_json(query, &dynamic, &dynamic_len) != 0) {
            const char *error = "{\"available\":false,\"error\":\"Object status unavailable\"}\n";
            respond(fd,503,"Service Unavailable","application/json; charset=utf-8",error,strlen(error));
            return;
        }
        dynamic_owned = 1;
        free(exclude_dynamic_cache);
        exclude_dynamic_cache = dynamic;
        exclude_dynamic_cache_len = dynamic_len;
        snprintf(exclude_dynamic_cache_job, sizeof(exclude_dynamic_cache_job), "%s", job ? job : "");
        exclude_dynamic_cache_ms = now_ms;
        dynamic_owned = 0; /* cache owns this allocation */
    }
    const char *payload = exclude_object_payload(dynamic, dynamic_len);
    if (!payload) {
        if (dynamic_owned) free(dynamic);
        const char *error = "{\"available\":false,\"error\":\"Invalid object status response\"}\n";
        respond(fd,503,"Service Unavailable","application/json; charset=utf-8",error,strlen(error));
        return;
    }

    size_t prefix_len = (size_t)(payload + 1 - dynamic);
    size_t capacity = dynamic_len + exclude_objects_cache_len + 32;
    char *body = malloc(capacity);
    if (!body) { if (dynamic_owned) free(dynamic); return; }
    memcpy(body, dynamic, prefix_len);
    int inserted = snprintf(body + prefix_len, capacity - prefix_len,
                            "\"objects\":%s,", exclude_objects_cache);
    if (inserted < 0 || (size_t)inserted >= capacity - prefix_len) {
        free(body); if (dynamic_owned) free(dynamic); return;
    }
    size_t body_len = prefix_len + (size_t)inserted;
    memcpy(body + body_len, dynamic + prefix_len, dynamic_len - prefix_len);
    body_len += dynamic_len - prefix_len;
    respond(fd,200,"OK","application/json; charset=utf-8",body,body_len);
    free(body);
    if (dynamic_owned) free(dynamic);
}

static void mesh_response(int fd) {
    int uds = socket(AF_UNIX, SOCK_STREAM, 0);
    if (uds < 0) {
        const char *error="{\"available\":false,\"error\":\"Cannot create UDS socket\"}\n";
        respond(fd,503,"Service Unavailable","application/json; charset=utf-8",error,strlen(error)); return;
    }
    struct timeval timeout={2,0};
    setsockopt(uds,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    setsockopt(uds,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    struct sockaddr_un address; memset(&address,0,sizeof(address));
    address.sun_family=AF_UNIX;
    snprintf(address.sun_path,sizeof(address.sun_path),"%s","/tmp/elegoo_uds");
    if(connect(uds,(struct sockaddr *)&address,sizeof(address))<0){
        close(uds); const char *error="{\"available\":false,\"error\":\"Printer UDS unavailable\"}\n";
        respond(fd,503,"Service Unavailable","application/json; charset=utf-8",error,strlen(error)); return;
    }
    static const char query[]="{\"id\":201,\"method\":\"objects/query\",\"params\":{\"objects\":{\"bed_mesh\":null}}}\003";
    if(send_all(uds,query,sizeof(query)-1)!=0){
        close(uds); const char *error="{\"available\":false,\"error\":\"Mesh query failed\"}\n";
        respond(fd,503,"Service Unavailable","application/json; charset=utf-8",error,strlen(error)); return;
    }
    char *result=malloc(65537); size_t used=0;
    if(!result){close(uds);return;}
    while(used<65536){
        ssize_t n=recv(uds,result+used,65536-used,0);
        if(n<=0)break;
        used+=(size_t)n;
        char *end=memchr(result,3,used);
        if(end){used=(size_t)(end-result);break;}
    }
    close(uds); result[used]='\0';
    if(!used){free(result);const char *error="{\"available\":false,\"error\":\"No mesh response\"}\n";
        respond(fd,503,"Service Unavailable","application/json; charset=utf-8",error,strlen(error));return;}
    respond(fd,200,"OK","application/json; charset=utf-8",result,used); free(result);
}

#include "plates.h"
#include "spools.h"

static void serve_index(int fd, const char *web_root) {
    char path[PATH_MAX_LOCAL];
    if (snprintf(path, sizeof(path), "%s/index.html", web_root) >= (int)sizeof(path)) {
        const char *body = "Web root path is too long\n";
        respond(fd, 500, "Internal Server Error", "text/plain; charset=utf-8", body, strlen(body));
        return;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        const char *body = "Dashboard file not found\n";
        respond(fd, 500, "Internal Server Error", "text/plain; charset=utf-8", body, strlen(body));
        return;
    }
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return; }
    long size = ftell(file);
    if (size < 0 || size > 1024 * 1024) { fclose(file); return; }
    rewind(file);
    char *body = malloc((size_t)size);
    if (!body) { fclose(file); return; }
    size_t read_len = fread(body, 1, (size_t)size, file);
    fclose(file);
    if (read_len == (size_t)size)
        respond(fd, 200, "OK", "text/html; charset=utf-8", body, read_len);
    free(body);
}

/* Locale codes accepted in /i18n/<code>.json requests: 2-5 lowercase letters,
   optionally followed by '-' and 2 uppercase letters (e.g. "en", "pt-BR").
   Anchoring the charset this tightly also rules out any '..' or '/'. */
static int locale_code_valid(const char *code, size_t length) {
    if (length < 2 || length > 8) return 0;
    size_t letters = 0;
    while (letters < length && code[letters] >= 'a' && code[letters] <= 'z') letters++;
    if (letters < 2 || letters > 5) return 0;
    if (letters == length) return 1;
    if (length - letters != 3 || code[letters] != '-') return 0;
    return code[letters + 1] >= 'A' && code[letters + 1] <= 'Z' &&
           code[letters + 2] >= 'A' && code[letters + 2] <= 'Z';
}

/* Serves cc2-control/web/locales/<code>.json. The UI loads en.json as a
   fallback plus the requested locale, so a missing or partial file degrades
   to English strings instead of breaking the page. */
static void serve_locale(int fd, const char *web_root, const char *request_path) {
    const char *name = request_path + strlen("/i18n/");
    size_t name_len = strlen(name);
    static const char suffix[] = ".json";
    if (name_len <= sizeof(suffix) - 1 ||
        strcmp(name + name_len - (sizeof(suffix) - 1), suffix) != 0 ||
        !locale_code_valid(name, name_len - (sizeof(suffix) - 1))) {
        const char *body = "Not found\n";
        respond(fd, 404, "Not Found", "text/plain; charset=utf-8", body, strlen(body));
        return;
    }
    char path[PATH_MAX_LOCAL];
    if (snprintf(path, sizeof(path), "%s/locales/%s", web_root, name) >= (int)sizeof(path)) {
        const char *body = "Locale path is too long\n";
        respond(fd, 500, "Internal Server Error", "text/plain; charset=utf-8", body, strlen(body));
        return;
    }
    FILE *file = fopen(path, "rb");
    if (!file) {
        const char *body = "{}\n";
        respond(fd, 404, "Not Found", "application/json; charset=utf-8", body, strlen(body));
        return;
    }
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return; }
    long size = ftell(file);
    if (size < 0 || size > 262144) { fclose(file); return; }
    rewind(file);
    char *body = malloc((size_t)size);
    if (!body) { fclose(file); return; }
    size_t read_len = fread(body, 1, (size_t)size, file);
    fclose(file);
    if (read_len == (size_t)size)
        respond(fd, 200, "OK", "application/json; charset=utf-8", body, read_len);
    free(body);
}

static size_t content_length_from_headers(const char *request) {
    char value[32];int present=http_header(request,"Content-Length",value,sizeof(value));
    if(!present)return 0;
    if(present<0)return SIZE_MAX;
    size_t length=0;
    for(const char *p=value;*p;p++){
        if(!isdigit((unsigned char)*p)||length>(SIZE_MAX-(size_t)(*p-'0'))/10)return SIZE_MAX;
        length=length*10+(size_t)(*p-'0');
    }
    return length;
}

static void setup_status_response(int fd, const mqtt_client *mqtt) {
    if (mqtt->registered) setup_mode = 0;
    char body[256];
    int length = snprintf(body, sizeof(body),
        "{\"required\":%s,\"configured\":%s,\"mqtt_connected\":%s,"
        "\"mqtt_registered\":%s,\"snapshot_received\":%s}\n",
        setup_mode ? "true" : "false", mqtt->password[0] ? "true" : "false",
        mqtt->connected ? "true" : "false", mqtt->registered ? "true" : "false",
        mqtt->snapshot_len ? "true" : "false");
    if (length > 0 && (size_t)length < sizeof(body))
        respond(fd, 200, "OK", "application/json; charset=utf-8", body, (size_t)length);
}

static int access_code_valid(const char *code, size_t length) {
    if (!code || length < 1 || length > 128) return 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)code[i];
        if (!(isalnum(ch) || ch == '.' || ch == '_' || ch == '-')) return 0;
    }
    return 1;
}

static void setup_configure_response(int fd, mqtt_client *mqtt,
                                     const char *body, size_t body_len,
                                     int allow_revalidation) {
    if (mqtt->registered) setup_mode = 0;
    if (!setup_mode && !allow_revalidation) {
        const char *error = "{\"accepted\":false,\"error\":\"First-run setup is locked\"}\n";
        respond(fd, 403, "Forbidden", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    while (body_len && isspace((unsigned char)*body)) { body++; body_len--; }
    while (body_len && isspace((unsigned char)body[body_len - 1])) body_len--;
    if (!access_code_valid(body, body_len)) {
        const char *error = "{\"accepted\":false,\"error\":\"Invalid access code\"}\n";
        respond(fd, 400, "Bad Request", "application/json; charset=utf-8", error, strlen(error));
        return;
    }

    char code[129], temporary[PATH_MAX_LOCAL];
    memcpy(code, body, body_len); code[body_len] = '\0';
    if (snprintf(temporary, sizeof(temporary), "%s.new", mqtt_config_path) >= (int)sizeof(temporary)) {
        const char *error = "{\"accepted\":false,\"error\":\"Configuration path is too long\"}\n";
        respond(fd, 500, "Internal Server Error", "application/json; charset=utf-8", error, strlen(error));
        return;
    }

    FILE *file = fopen(temporary, "wb");
    int failed = !file;
    if (file) {
        if (fprintf(file, "mqtt_username=elegoo\nmqtt_password=%s\n"
                          "mqtt_topic=elegoo/+/api_status\nmqtt_serial=%s\n",
                    code, mqtt->serial) < 0 || fflush(file) != 0 || fsync(fileno(file)) != 0)
            failed = 1;
        if (fclose(file) != 0) failed = 1;
    }
    memset(code, 0, sizeof(code));
    if (!failed && chmod(temporary, S_IRUSR | S_IWUSR) != 0) failed = 1;
    if (!failed && rename(temporary, mqtt_config_path) != 0) failed = 1;
    if (!failed && chmod(mqtt_config_path, S_IRUSR | S_IWUSR) != 0) failed = 1;
    if (failed) {
        unlink(temporary);
        const char *error = "{\"accepted\":false,\"error\":\"Cannot save configuration\"}\n";
        respond(fd, 500, "Internal Server Error", "application/json; charset=utf-8", error, strlen(error));
        return;
    }

    /* This process started without credentials.  Restarting only CC2 Control
       makes MQTT registration, the initial snapshot and Canvas discovery use
       the same clean ordering as every later boot.  procd performs the respawn;
       elegoo_printer, Klipper, the broker and an active print are untouched. */
    const char *ok = "{\"accepted\":true,\"restarting\":true,\"message\":\"Configuration saved; restarting CC2 Control\"}\n";
    respond(fd, 202, "Accepted", "application/json; charset=utf-8", ok, strlen(ok));
    first_run_restart_requested = 1;
}

static int preset_json_safe(const char *body,size_t len) {
    if(!body||len<2||len>8192)return 0;
    while(len&&isspace((unsigned char)*body)){body++;len--;}
    while(len&&isspace((unsigned char)body[len-1]))len--;
    if(len<2||body[0]!='['||body[len-1]!=']')return 0;
    if(!strstr(body,"\"name\"")||!strstr(body,"\"nozzle\"")||!strstr(body,"\"bed\""))return 0;
    for(size_t i=0;i<len;i++){
        unsigned char ch=(unsigned char)body[i];
        if(isalnum(ch)||isspace(ch)||strchr("[]{}\":,._+#-",ch))continue;
        return 0;
    }
    return 1;
}

static void presets_get_response(int fd) {
    FILE *f=fopen(material_presets_path,"rb");
    if(!f){respond(fd,200,"OK","application/json; charset=utf-8",default_material_presets,strlen(default_material_presets));return;}
    char body[8193];size_t n=fread(body,1,sizeof(body)-1,f);fclose(f);body[n]='\0';
    if(!preset_json_safe(body,n)){respond(fd,200,"OK","application/json; charset=utf-8",default_material_presets,strlen(default_material_presets));return;}
    respond(fd,200,"OK","application/json; charset=utf-8",body,n);
}

static void presets_put_response(int fd,const char *body,size_t body_len) {
    if(!preset_json_safe(body,body_len)){
        const char *error="{\"saved\":false,\"error\":\"Invalid preset data\"}\n";
        respond(fd,400,"Bad Request","application/json; charset=utf-8",error,strlen(error));return;
    }
    char temporary[512];snprintf(temporary,sizeof(temporary),"%s.new",material_presets_path);
    FILE *f=fopen(temporary,"wb");
    int failed=!f;
    if(f){if(fwrite(body,1,body_len,f)!=body_len||fflush(f)!=0||fclose(f)!=0)failed=1;f=NULL;}
    if(!failed&&rename(temporary,material_presets_path)!=0)failed=1;
    if(failed){unlink(temporary);const char *error="{\"saved\":false,\"error\":\"Cannot save presets\"}\n";respond(fd,500,"Internal Server Error","application/json; charset=utf-8",error,strlen(error));return;}
    const char *ok="{\"saved\":true}\n";
    respond(fd,200,"OK","application/json; charset=utf-8",ok,strlen(ok));
}

/* Reads the value of "key":"..." out of a compact JSON object. Returns 0
   (leaving out untouched) if the key is absent or the value does not fit
   out_cap, including the terminator. */
static int extract_json_string(const char *input, const char *key, char *out, size_t out_cap) {
    char needle[32];
    int written = snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    if (written <= 0 || (size_t)written >= sizeof(needle)) return 0;
    const char *start = strstr(input, needle);
    if (!start) return 0;
    start += written;
    const char *end = strchr(start, '"');
    if (!end) return 0;
    size_t length = (size_t)(end - start);
    if (length == 0 || length >= out_cap) return 0;
    memcpy(out, start, length);
    out[length] = '\0';
    return 1;
}

/* Any locale that has a web/locales/<code>.json file is valid: the backend
   only stores the code, the frontend decides what to do with it (falling
   back to English for an unknown or not-yet-translated one). Empty until a
   language has been saved, so browsers can tell "never chosen" (and follow
   their own language) from an explicit choice. */
static const char *preferences_language(void) {
    static char language[9];
    language[0] = '\0';
    FILE *file = fopen(ui_preferences_path, "rb");
    if (!file) return language;
    char body[96];
    size_t length = fread(body, 1, sizeof(body) - 1, file);
    fclose(file);
    body[length] = '\0';
    char code[9];
    if (extract_json_string(body, "language", code, sizeof(code)) && locale_code_valid(code, strlen(code)))
        memcpy(language, code, strlen(code) + 1);
    return language;
}

/* Like the language, a theme is only an identifier the backend stores; the
   frontend owns the list of palettes and falls back to "dark" for an unknown
   one. Accepted shape: lowercase words joined by single hyphens, 2-24 chars
   ("dark", "light", "solarized-light"). */
static int theme_valid(const char *code, size_t length) {
    if (length < 2 || length > 24) return 0;
    for (size_t index = 0; index < length; ++index) {
        int hyphen = code[index] == '-';
        if (!hyphen && !(code[index] >= 'a' && code[index] <= 'z')) return 0;
        if (hyphen && (index == 0 || index == length - 1 || code[index - 1] == '-')) return 0;
    }
    return 1;
}

static const char *preferences_theme(void) {
    static char theme[25] = "dark";
    FILE *file = fopen(ui_preferences_path, "rb");
    if (!file) return theme;
    char body[160];
    size_t length = fread(body, 1, sizeof(body) - 1, file);
    fclose(file);
    body[length] = '\0';
    char code[25];
    if (extract_json_string(body, "theme", code, sizeof(code)) && theme_valid(code, strlen(code)))
        memcpy(theme, code, strlen(code) + 1);
    else
        memcpy(theme, "dark", 5);
    return theme;
}

static const char *quick_action_defaults[4] = {
    "home:ALL", "system:heaters_off", "system:fans_off", "system:motors_off"
};

static int quick_action_valid(const char *action) {
    static const char *allowed[] = {
        "home:ALL", "home:X", "home:Y", "home:Z",
        "system:heaters_off", "system:fans_off", "system:motors_off",
        "light:toggle", "page:control", "page:files", "page:bed", "page:canvas"
    };
    for (size_t index = 0; index < sizeof(allowed) / sizeof(allowed[0]); ++index)
        if (strcmp(action, allowed[index]) == 0) return 1;
    return 0;
}

static const char *preferences_quick_action(int slot) {
    static char actions[4][32];
    static const char *const keys[4] = {"quick1", "quick2", "quick3", "quick4"};
    if (slot < 0 || slot >= 4) return quick_action_defaults[0];
    snprintf(actions[slot], sizeof(actions[slot]), "%s", quick_action_defaults[slot]);
    FILE *file = fopen(ui_preferences_path, "rb");
    if (!file) return actions[slot];
    char body[512], value[32];
    size_t length = fread(body, 1, sizeof(body) - 1, file);
    fclose(file); body[length] = '\0';
    if (extract_json_string(body, keys[slot], value, sizeof(value)) && quick_action_valid(value))
        snprintf(actions[slot], sizeof(actions[slot]), "%s", value);
    return actions[slot];
}

static void preferences_get_response(int fd) {
    char body[256];
    int length = snprintf(body, sizeof(body),
        "{\"language\":\"%s\",\"theme\":\"%s\",\"quick_actions\":[\"%s\",\"%s\",\"%s\",\"%s\"]}\n",
        preferences_language(), preferences_theme(), preferences_quick_action(0),
        preferences_quick_action(1), preferences_quick_action(2), preferences_quick_action(3));
    respond(fd, 200, "OK", "application/json; charset=utf-8", body, (size_t)length);
}

static void preferences_put_response(int fd, const char *body, size_t body_len) {
    if (!body || body_len < 2 || body_len > 512) {
        const char *error = "{\"saved\":false,\"error\":\"Invalid preferences\"}\n";
        respond(fd, 400, "Bad Request", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    char input[513];
    size_t compact_len = 0;
    for (size_t index = 0; index < body_len; ++index) {
        if (!isspace((unsigned char)body[index])) input[compact_len++] = body[index];
    }
    input[compact_len] = '\0';
    char language_code[9];
    int has_language = strstr(input, "\"language\":") != NULL;
    int valid_language = has_language &&
        extract_json_string(input, "language", language_code, sizeof(language_code)) &&
        locale_code_valid(language_code, strlen(language_code));
    char theme_code[25];
    int has_theme = strstr(input, "\"theme\":") != NULL;
    int valid_theme = has_theme &&
        extract_json_string(input, "theme", theme_code, sizeof(theme_code)) &&
        theme_valid(theme_code, strlen(theme_code));
    char quick[4][32]; int has_quick = 0, valid_quick = 1;
    static const char *const keys[4] = {"quick1", "quick2", "quick3", "quick4"};
    for (int slot = 0; slot < 4; ++slot) {
        const char *key = keys[slot];
        snprintf(quick[slot], sizeof(quick[slot]), "%s", preferences_quick_action(slot));
        char key_marker[12]; snprintf(key_marker, sizeof(key_marker), "\"%s\":", key);
        if (strstr(input, key_marker)) {
            has_quick = 1;
            if (!extract_json_string(input, key, quick[slot], sizeof(quick[slot])) ||
                !quick_action_valid(quick[slot])) valid_quick = 0;
        }
    }
    if ((!has_language && !has_theme && !has_quick) || (has_language && !valid_language) ||
        !valid_quick ||
        (has_theme && !valid_theme)) {
        const char *error = "{\"saved\":false,\"error\":\"Unsupported UI preference\"}\n";
        respond(fd, 400, "Bad Request", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    const char *language = valid_language ? language_code : preferences_language();
    const char *theme = valid_theme ? theme_code : preferences_theme();
    char temporary[512];
    if (snprintf(temporary, sizeof(temporary), "%s.new", ui_preferences_path) >= (int)sizeof(temporary)) {
        const char *error = "{\"saved\":false,\"error\":\"Preferences path is too long\"}\n";
        respond(fd, 500, "Internal Server Error", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    char canonical[256];
    int canonical_len = snprintf(canonical, sizeof(canonical),
        "{\"language\":\"%s\",\"theme\":\"%s\",\"quick1\":\"%s\",\"quick2\":\"%s\",\"quick3\":\"%s\",\"quick4\":\"%s\"}\n",
        language, theme, quick[0], quick[1], quick[2], quick[3]);
    FILE *file = fopen(temporary, "wb");
    int failed = !file;
    if (file) {
        if (fwrite(canonical, 1, (size_t)canonical_len, file) != (size_t)canonical_len ||
            fflush(file) != 0 || fclose(file) != 0) failed = 1;
    }
    if (!failed && chmod(temporary, S_IRUSR | S_IWUSR) != 0) failed = 1;
    if (!failed && rename(temporary, ui_preferences_path) != 0) failed = 1;
    if (failed) {
        unlink(temporary);
        const char *error = "{\"saved\":false,\"error\":\"Cannot save preferences\"}\n";
        respond(fd, 500, "Internal Server Error", "application/json; charset=utf-8", error, strlen(error));
        return;
    }
    const char *ok = "{\"saved\":true}\n";
    respond(fd, 200, "OK", "application/json; charset=utf-8", ok, strlen(ok));
}

static void console_status_response(int fd, console_state *console) {
    size_t capacity = CONSOLE_OUTPUT_MAX * 2 + 1024;
    char *body = malloc(capacity);
    if (!body) {
        const char *error = "Out of memory\n";
        respond(fd,500,"Internal Server Error","text/plain; charset=utf-8",error,strlen(error));
        return;
    }
    console_json(console, body, capacity);
    respond(fd,200,"OK","application/json; charset=utf-8",body,strlen(body));
    free(body);
}

static void console_clear_response(int fd, console_state *console) {
    if (console_clear(console) != 0) {
        const char *error="{\"cleared\":false,\"error\":\"Cannot clear while a command is running\"}\n";
        respond(fd,409,"Conflict","application/json; charset=utf-8",error,strlen(error)); return;
    }
    const char *ok="{\"cleared\":true}\n";
    respond(fd,200,"OK","application/json; charset=utf-8",ok,strlen(ok));
}

static void console_command_response(int fd, console_state *console, const mqtt_client *mqtt,
                                     const char *body, size_t body_len) {
    char command[CONSOLE_COMMAND_MAX], reason[256];
    while (body_len && isspace((unsigned char)*body)) { body++; body_len--; }
    while (body_len && isspace((unsigned char)body[body_len-1])) body_len--;
    if (!body_len || body_len >= sizeof(command)) {
        const char *error="{\"accepted\":false,\"error\":\"Invalid command length\"}\n";
        respond(fd,400,"Bad Request","application/json; charset=utf-8",error,strlen(error)); return;
    }
    memcpy(command,body,body_len); command[body_len]='\0';
    if(plates_background_active() && strcmp(command,"M112")){plates_fail(fd,409,"Plate verification is still running");return;}
    if (!console_command_allowed(console,command,mqtt,reason,sizeof(reason)) ||
        console_start(console,command,reason,sizeof(reason)) != 0) {
        char escaped[300],response[420]; json_escape(escaped,sizeof(escaped),reason);
        int n=snprintf(response,sizeof(response),"{\"accepted\":false,\"error\":\"%s\"}\n",escaped);
        respond(fd,409,"Conflict","application/json; charset=utf-8",response,(size_t)n); return;
    }
    const char *ok="{\"accepted\":true}\n";
    respond(fd,202,"Accepted","application/json; charset=utf-8",ok,strlen(ok));
}

/* Compact PID readback: reuse console reports without polling the firmware. */
static int pid_completed_report(const char *output) {
    const char *p=output;
    while((p=strstr(p,"\"command\""))) {
        p+=9;while(isspace((unsigned char)*p))p++;
        if(*p!=':')continue;
        p++;
        while(isspace((unsigned char)*p))p++;
        if(strncmp(p,"\"pid_calibrate\"",15))continue;
        const char *line=strchr(p,'\n'),*result=strstr(p,"\"result\"");
        if(!result||(line&&result>=line))continue;
        result+=8;while(isspace((unsigned char)*result))result++;
        if(*result!=':')continue;
        result++;
        while(isspace((unsigned char)*result))result++;
        if(!strncmp(result,"\"completed\"",11))return 1;
    }
    return 0;
}
static int pid_ready_locked(const console_state *console) {
    return !console->busy && console->completed && console->success &&
        !strncmp(console->command,"PID_CALIBRATE HEATER=",21) && pid_completed_report(console->output);
}
static void pid_status_response(int fd,console_state *console) {
    char body[512],kp[40],ki[40],kd[40];
    pthread_mutex_lock(&console->lock);
    int selected=!strncmp(console->command,"PID_CALIBRATE HEATER=",21);
    int ready=pid_ready_locked(console),busy=console->busy;
    double p=number_after_marker(console->output,"pid_Kp="),i=number_after_marker(console->output,"pid_Ki="),d=number_after_marker(console->output,"pid_Kd=");
    json_number(kp,sizeof(kp),ready&&isfinite(p)&&p>=0,p);
    json_number(ki,sizeof(ki),ready&&isfinite(i)&&i>=0,i);
    json_number(kd,sizeof(kd),ready&&isfinite(d)&&d>=0,d);
    const char *heater=selected?(strstr(console->command,"HEATER=extruder ")?"extruder":"heater_bed"):"";
    int n=snprintf(body,sizeof(body),"{\"busy\":%s,\"heater\":\"%s\",\"ready\":%s,\"failed\":%s,\"kp\":%s,\"ki\":%s,\"kd\":%s}\n",
        busy?"true":"false",heater,ready?"true":"false",selected&&console->completed&&!ready?"true":"false",kp,ki,kd);
    pthread_mutex_unlock(&console->lock);
    respond(fd,200,"OK","application/json",body,(size_t)n);
}

static void control_response(int fd,console_state *console,const mqtt_client *mqtt,const char *body,size_t body_len){
    char action[160],script[700],reason[256];
    while(body_len&&isspace((unsigned char)*body)){body++;body_len--;}
    while(body_len&&isspace((unsigned char)body[body_len-1]))body_len--;
    if(!body_len||body_len>=sizeof(action)){
        const char *error="{\"accepted\":false,\"error\":\"Invalid action\"}\n";
        respond(fd,400,"Bad Request","application/json; charset=utf-8",error,strlen(error));return;
    }
    memcpy(action,body,body_len);action[body_len]='\0';
    if(plates_background_active() && strcmp(action,"system:emergency_stop")){plates_fail(fd,409,"Plate verification is still running");return;}
    if(!strcmp(action,"pid:save")) {
        pthread_mutex_lock(&console->lock);int ready=pid_ready_locked(console);pthread_mutex_unlock(&console->lock);
        if(!ready){const char *error="{\"error\":\"Complete a PID calibration before saving\"}\n";respond(fd,409,"Conflict","application/json",error,strlen(error));return;}
    }
    int z_action=!strncmp(action,"zoffset:",8);
    double z_next=0,z_reference=0;
    if(z_action){
        double current,delta;int end=0;
        const char *argument=!strncmp(action,"zoffset:adjust:",15)?action+15:
            !strncmp(action,"zoffset:undo:",13)?action+13:NULL;
        int valid=argument&&sscanf(argument,"%lf%n",&delta,&end)==1&&!argument[end]&&isfinite(delta);
        if(!valid||!z_offset_readback(&current)||z_offset_pending||
           fabs((current+delta)-(z_offset_session?z_offset_reference:current))>0.5001||
           (!strncmp(action,"zoffset:undo:",13)&&fabs((current+delta)-(z_offset_session?z_offset_reference:current))>0.0005)){
            const char *error="{\"accepted\":false,\"error\":\"Z offset requires fresh readback, confirmed previous motion and a session adjustment within +/-0.50 mm of the printer reference\"}\n";
            respond(fd,409,"Conflict","application/json; charset=utf-8",error,strlen(error));return;
        }
        z_next=current+delta;z_reference=z_offset_session?z_offset_reference:current;
    }
    double tune_value;
    if(strncmp(action,"tune:",5)==0&&
       !uds_value(&telemetry,strncmp(action,"tune:speed:",11)==0?U_SPEED_FACTOR:U_FLOW_FACTOR,&tune_value)){
        const char *error="{\"accepted\":false,\"error\":\"Fresh tuning readback is unavailable\"}\n";
        respond(fd,409,"Conflict","application/json; charset=utf-8",error,strlen(error));return;
    }
    if(!control_build_script(action,mqtt,script,sizeof(script),reason,sizeof(reason))||
       console_start(console,script,reason,sizeof(reason))!=0){
        char escaped[300],response[420];json_escape(escaped,sizeof(escaped),reason);
        int n=snprintf(response,sizeof(response),"{\"accepted\":false,\"error\":\"%s\"}\n",escaped);
        respond(fd,409,"Conflict","application/json; charset=utf-8",response,(size_t)n);return;
    }
    if(z_action){z_offset_reference=z_reference;z_offset_session=1;z_offset_expected=z_next;z_offset_pending=1;z_offset_timed_out=0;clock_gettime(CLOCK_MONOTONIC,&z_offset_started);}
    const char *ok="{\"accepted\":true}\n";
    respond(fd,202,"Accepted","application/json; charset=utf-8",ok,strlen(ok));
}

/* Release the steppers the firmware leaves energised once the printer has
 * stood idle with its heaters off; see control_idle_motors_due(). */
static control_idle_motors idle_motors;
static void idle_motors_tick(const mqtt_client *mqtt,console_state *console){
    control_idle_sample s={0};
    s.valid=gcode_idle_for_mutation(mqtt)&&mqtt->have_position&&
        uds_value(&telemetry,U_CF,&s.board_fan)&&uds_value(&telemetry,U_EG,&s.nozzle_target)&&
        uds_value(&telemetry,U_BG,&s.bed_target)&&uds_value(&telemetry,U_LIVE_SPEED,&s.velocity);
    s.machine_status=mqtt->machine_status;s.print_state=mqtt->print_state;
    s.x=mqtt->x;s.y=mqtt->y;s.z=mqtt->z;
    pthread_mutex_lock(&console->lock);s.console_busy=console->busy;pthread_mutex_unlock(&console->lock);
    if(control_idle_motors_due(&idle_motors,&s,recovery_clock())&&send_local_gcode_script("M84")!=0)
        idle_motors.holding=0; /* Not delivered: wait a full period before trying again. */
}

#define HTTP_PENDING_MAX 8
typedef struct {
    int fd;
    size_t used;
    struct timespec started;
    char request[REQUEST_MAX+1];
} http_pending;

static int http_request_complete(const char *request,size_t used){
    const char *end=strstr(request,"\r\n\r\n");size_t separator=4;
    if(!end){end=strstr(request,"\n\n");separator=2;}
    if(used==REQUEST_MAX)return 1;
    if(!end)return 0;
    if(!strncmp(request,"POST /api/gcode-files/upload?",29)||
       !strncmp(request,"POST /api/files/local ",22)||
       !strncmp(request,"POST /api/files/local?",22))return 1;
    size_t header=(size_t)(end-request)+separator;
    size_t length=content_length_from_headers(request);
    if(length>REQUEST_MAX-header)return -1;
    return used>=header+length;
}

static int handle_client(int fd,char *request,size_t used,const char *web_root,mqtt_client *mqtt,console_state *console){
    /* A full initial read may contain a valid header followed by upload bytes.
     * Reject only oversized headers, not a streaming body's first chunk. */
    const char *request_header_end = strstr(request, "\r\n\r\n");
    if (!request_header_end) request_header_end = strstr(request, "\n\n");
    if (used == REQUEST_MAX && !request_header_end) {
        const char *body = "Request headers too large\n";
        respond(fd, 431, "Request Header Fields Too Large",
                "text/plain; charset=utf-8", body, strlen(body));
        return 0;
    }
    char method[16], path[2048], version[16];
    if (sscanf(request, "%15s %2047s %15s", method, path, version) != 3) return 0;
    if(!http_browser_allowed(fd,request,method,path,service_http_port,http_host_alias)){
        const char *error="{\"error\":\"Untrusted HTTP host or browser origin\"}\n";
        respond(fd,403,"Forbidden","application/json; charset=utf-8",error,strlen(error));return 0;
    }
    char *query = strchr(path, '?');
    if (query) *query++ = '\0';
    char *header_end=strstr(request,"\r\n\r\n"); size_t separator=4;
    if(!header_end){header_end=strstr(request,"\n\n");separator=2;}
    const char *body=header_end?header_end+separator:request+used;
    size_t body_len=header_end&&used>=(size_t)(body-request)?used-(size_t)(body-request):0;
    if (strcmp(method,"POST")==0 && strcmp(path,"/api/files/local")==0) {
        return header_end ? orca_upload_start(fd,request,used,(size_t)(body-request),mqtt):0;
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/gcode-files/upload")==0) {
        return header_end ? gcode_upload_start(fd,request,query,used,(size_t)(body-request),mqtt):0;
    } else if (strcmp(method,"GET")==0 && (strcmp(path,"/")==0 || strcmp(path,"/index.html")==0)) {
        serve_index(fd, web_root);
    } else if (strcmp(method,"GET")==0 && strncmp(path,"/i18n/",6)==0) {
        serve_locale(fd, web_root, path);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/api/orca/pending-print")==0) {
        orca_pending_response(fd);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/orca/pending-print/clear")==0) {
        orca_pending_clear_response(fd,body,body_len);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/api/version")==0) {
        orca_version_response(fd);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/api/health")==0) {
        health_response(fd, mqtt);
    } else if (strcmp(method,"GET")==0 && (strcmp(path,"/api/v1/system/info")==0 || strcmp(path,"/api/system/capabilities")==0)) {
        system_info_response(fd, mqtt);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/api/setup")==0) {
        setup_status_response(fd, mqtt);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/setup")==0) {
        setup_configure_response(fd,mqtt,body,body_len,0);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/setup/revalidate")==0) {
        setup_configure_response(fd,mqtt,body,body_len,1);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/api/uds")==0) {
        uds_response(fd);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/api/printer")==0) {
        printer_response(fd, mqtt);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/camera/claim")==0) {
        camera_claim_response(fd, body, body_len);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/api/snapshot")==0) {
        snapshot_response(fd, mqtt);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/api/mqtt-diagnostic")==0) {
        mqtt_diagnostic_response(fd, mqtt);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/server/info")==0) {
        orca_server_info_response(fd,mqtt);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/server/database/item")==0 &&
               query && strcmp(query,"namespace=lane_data")==0) {
        orca_lane_data_response(fd, mqtt);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/api/canvas")==0) {
        canvas_response(fd, mqtt);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/canvas/refresh")==0) {
        canvas_refresh_response(fd, mqtt);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/canvas/auto-refill")==0) {
        canvas_auto_refill_response(fd,mqtt,body,body_len);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/api/history")==0) {
        history_response(fd, mqtt);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/history/refresh")==0) {
        history_refresh_response(fd, mqtt);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/history/delete")==0) {
        history_delete_response(fd,mqtt,body,body_len);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/history/timelapse")==0) {
        timelapse_generate_response(fd,mqtt,body,body_len);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/api/history/timelapse")==0) {
        return timelapse_download_start(fd,mqtt,query);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/api/material-presets")==0) {
        presets_get_response(fd);
    } else if (strcmp(method,"PUT")==0 && strcmp(path,"/api/material-presets")==0) {
        presets_put_response(fd,body,body_len);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/api/preferences")==0) {
        preferences_get_response(fd);
    } else if (strcmp(method,"PUT")==0 && strcmp(path,"/api/preferences")==0) {
        preferences_put_response(fd,body,body_len);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/api/gcode-files")==0) {
        gcode_files_response(fd);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/api/gcode-files/download")==0) {
        return gcode_download_start(fd,query);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/gcode-files/inspect")==0) {
        return analysis_start(fd,0,body,body_len);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/gcode-files/thumbnail")==0) {
        gcode_thumbnail_response(fd,body,body_len);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/gcode-files/metadata")==0) {
        return analysis_start(fd,1,body,body_len);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/gcode-files/delete")==0) {
        gcode_delete_response(fd,request,mqtt,body,body_len);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/gcode-files/copy")==0) {
        gcode_copy_response(fd,mqtt,body,body_len);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/gcode-files/print")==0) {
        if(plates_background_active())plates_fail(fd,409,"Plate verification is still running");
        else gcode_start_response(fd,mqtt,body,body_len);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/api/exclude-objects")==0) {
        exclude_objects_response(fd, mqtt);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/api/mesh")==0) {
        mesh_response(fd);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/api/plates")==0) {
        plates_get_response(fd);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/plates/save")==0) {
        plates_save_response(fd,mqtt,body,body_len);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/plates/recapture")==0) {
        plates_recapture_response(fd,mqtt,body,body_len);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/plates/edit")==0) {
        plates_edit_response(fd,mqtt,body,body_len);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/plates/delete")==0) {
        plates_delete_response(fd,body,body_len);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/plates/mount")==0) {
        plates_mount_response(fd,mqtt,body,body_len);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/plates/unmount")==0) {
        plates_unmount_response(fd);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/api/spools")==0) {
        spools_get_response(fd,mqtt);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/spools/enable")==0) {
        spools_enable_response(fd,mqtt,body,body_len);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/spools/save")==0) {
        spools_save_response(fd,body,body_len);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/spools/delete")==0) {
        spools_delete_response(fd,body,body_len);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/spools/assign")==0) {
        spools_assign_response(fd,body,body_len);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/spools/dismiss")==0) {
        spools_dismiss_response(fd,body,body_len);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/spools/adjust")==0) {
        spools_adjust_response(fd,body,body_len);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/api/pid")==0) {
        pid_status_response(fd,console);
    } else if (strcmp(method,"GET")==0 && strcmp(path,"/api/console")==0) {
        console_status_response(fd,console);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/console/clear")==0) {
        console_clear_response(fd,console);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/console/command")==0) {
        console_command_response(fd,console,mqtt,body,body_len);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/recovery/reboot")==0) {
        recovery_reboot_response(fd,body,body_len);
    } else if (strcmp(method,"POST")==0 && strcmp(path,"/api/control")==0) {
        control_response(fd,console,mqtt,body,body_len);
    } else if (strcmp(method,"GET")!=0 && strcmp(method,"POST")!=0 && strcmp(method,"PUT")!=0) {
        const char *error="Method not allowed\n";
        respond(fd,405,"Method Not Allowed","text/plain; charset=utf-8",error,strlen(error));
    } else {
        const char *body = "Not found\n";
        respond(fd, 404, "Not Found", "text/plain; charset=utf-8", body, strlen(body));
    }
    return 0;
}

int main(int argc, char **argv) {
    int port = 8081;
    int panda_port = 7125;
    const char *web_root = "./web";
    const char *uds_path = "/tmp/elegoo_uds";
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--http-host") == 0 && i + 1 < argc) http_host_alias = argv[++i];
        else if (strcmp(argv[i], "--panda-port") == 0 && i + 1 < argc) panda_port = atoi(argv[++i]);
        else if (strcmp(argv[i], "--web-root") == 0 && i + 1 < argc) web_root = argv[++i];
        else if (strcmp(argv[i], "--uds-socket") == 0 && i + 1 < argc) uds_path = argv[++i];
        else if (strcmp(argv[i], "--config") == 0 && i + 1 < argc) mqtt_config_path = argv[++i];
        else if (strcmp(argv[i], "--presets") == 0 && i + 1 < argc) material_presets_path = argv[++i];
        else if (strcmp(argv[i], "--preferences") == 0 && i + 1 < argc) ui_preferences_path = argv[++i];
        else if (strcmp(argv[i], "--plates") == 0 && i + 1 < argc) plates_path = argv[++i];
        else if (strcmp(argv[i], "--spools") == 0 && i + 1 < argc) spools_path = argv[++i];
        else if (strcmp(argv[i], "--autosave") == 0 && i + 1 < argc) printer_autosave_path = argv[++i];
        else if (strcmp(argv[i], "--gcode-internal") == 0 && i + 1 < argc) gcode_internal_root = argv[++i];
        else if (strcmp(argv[i], "--gcode-usb") == 0 && i + 1 < argc) gcode_usb_root = argv[++i];
        else {
            fprintf(stderr, "Usage: %s [--port 8081] [--web-root ./web] [--config FILE] [--presets FILE] [--preferences FILE] [--plates FILE] [--spools FILE] [--autosave FILE] [--gcode-internal DIR] [--gcode-usb DIR] [--uds-socket PATH]\n", argv[0]);
            return 2;
        }
    }
    if (port < 1024 || port > 65535) {
        fprintf(stderr, "Port must be between 1024 and 65535\n");
        return 2;
    }
    if (panda_port < 0 || panda_port > 65535 || (panda_port > 0 && panda_port < 1024) || panda_port == port) {
        fprintf(stderr, "Invalid Panda port (use 0 to disable, or 1024..65535 different from --port)\n");
        return 2;
    }
    service_http_port = port;
    service_panda_port = panda_port;
    signal(SIGINT, stop_server);
    signal(SIGTERM, stop_server);
    signal(SIGPIPE, SIG_IGN);

    mqtt_client mqtt;
    console_state console;
    console_init(&console,"/tmp/elegoo_uds");
    recovery_console=&console;
    uds_init(&telemetry);
    plates_load();
    if (!plates_available) fprintf(stderr, "Plate library disabled: %s (%s)\n", plates_error, plates_path);
    spools_load();
    if (!spools_available) fprintf(stderr, "Spool library disabled: %s (%s)\n", spools_error, spools_path);
    mqtt_init(&mqtt);
    if (mqtt_load_config(&mqtt, mqtt_config_path) != 0) {
        setup_mode = 1;
        fprintf(stderr, "MQTT disabled: missing or incomplete config %s\n", mqtt_config_path);
    }
    else
        (void)mqtt_connect_local(&mqtt);

    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) { perror("socket"); return 1; }
    int reuse = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((unsigned short)port);
    if (bind(server, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind"); close(server); return 1;
    }
    if (listen(server, 8) < 0) { perror("listen"); close(server); return 1; }
    panda_server panda;
    if (panda_start(&panda, panda_port, port) != 0) {
        perror("panda bridge");
        close(server); mqtt_close(&mqtt); console_destroy(&console);
        return 1;
    }
    printf("CC2 Control " CC2_CONTROL_VERSION " + Panda bridge listening on 0.0.0.0:%d (Panda port %d)\n", port, panda_port);
    fflush(stdout);

    http_pending *pending=calloc(HTTP_PENDING_MAX,sizeof(*pending));
    if(!pending){perror("HTTP receive pool");panda_stop(&panda);close(server);mqtt_close(&mqtt);console_destroy(&console);return 1;}
    for(int i=0;i<HTTP_PENDING_MAX;i++)pending[i].fd=-1;
    while (running) {
        recovery_tick();
        mqtt_tick(&mqtt);
        object_query_path = uds_path;
        uds_tick(&telemetry,uds_path);
        plates_tick(&mqtt);
        fd_set read_set;
        FD_ZERO(&read_set); FD_SET(server,&read_set);
        int max_fd=server;
        if(mqtt.fd>=0){FD_SET(mqtt.fd,&read_set);if(mqtt.fd>max_fd)max_fd=mqtt.fd;}
        if(telemetry.fd>=0){FD_SET(telemetry.fd,&read_set);if(telemetry.fd>max_fd)max_fd=telemetry.fd;}
        int receiving=0;
        for(int i=0;i<HTTP_PENDING_MAX;i++)if(pending[i].fd>=0){
            FD_SET(pending[i].fd,&read_set);if(pending[i].fd>max_fd)max_fd=pending[i].fd;receiving=1;
        }
        int short_wait=receiving||plates_background_active();
        struct timeval wait={short_wait?0:1,short_wait?100000:0};
        int ready=select(max_fd+1,&read_set,NULL,NULL,&wait);
        if(ready<0){if(errno==EINTR)continue;perror("select");break;}
        if(mqtt.fd>=0&&FD_ISSET(mqtt.fd,&read_set))(void)mqtt_process(&mqtt);
        if(telemetry.fd>=0&&FD_ISSET(telemetry.fd,&read_set))uds_process(&telemetry);
        idle_motors_tick(&mqtt,&console);
        spools_tick(&mqtt);
        struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
        for(int i=0;i<HTTP_PENDING_MAX;i++){
            http_pending *p=&pending[i];if(p->fd<0)continue;
            double age=(double)(now.tv_sec-p->started.tv_sec)+(now.tv_nsec-p->started.tv_nsec)/1e9;
            /* Probe every pending nonblocking socket before expiring it.
             * Readiness is a scheduling hint: an empty/stale select result
             * must not discard bytes that are already queued on the socket. */
            ssize_t n=recv(p->fd,p->request+p->used,REQUEST_MAX-p->used,MSG_DONTWAIT);
            if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)){
                if(age>=2){close(p->fd);p->fd=-1;}
                continue;
            }
            if(n<=0){close(p->fd);p->fd=-1;continue;}
            p->used+=(size_t)n;p->request[p->used]=0;
            int complete=http_request_complete(p->request,p->used);
            if(!complete){if(age>=2){close(p->fd);p->fd=-1;}continue;}
            int fd=p->fd;p->fd=-1;
            int flags=fcntl(fd,F_GETFL,0);
            if(flags<0||fcntl(fd,F_SETFL,flags&~O_NONBLOCK)<0){close(fd);continue;}
            if(complete<0){
                const char *error="Request body too large\n";
                respond(fd,413,"Payload Too Large","text/plain; charset=utf-8",error,strlen(error));close(fd);
            }else if(!handle_client(fd,p->request,p->used,web_root,&mqtt,&console))close(fd);
        }
        if(first_run_restart_requested){running=0;continue;}
        if(!FD_ISSET(server,&read_set))continue;
        int client = accept(server, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept"); break;
        }
        struct timeval timeout;
        timeout.tv_sec = 2;
        timeout.tv_usec = 0;
        (void)setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        (void)setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        int slot=-1;
        for(int i=0;i<HTTP_PENDING_MAX;i++)if(pending[i].fd<0){slot=i;break;}
        int flags=fcntl(client,F_GETFL,0);
        if(slot<0){
            const char overload[]="HTTP/1.1 503 Service Unavailable\r\nRetry-After: 2\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            (void)send(client,overload,sizeof(overload)-1,MSG_NOSIGNAL|MSG_DONTWAIT);close(client);continue;
        }
        if(client>=FD_SETSIZE||flags<0||fcntl(client,F_SETFL,flags|O_NONBLOCK)<0){close(client);continue;}
        pending[slot].fd=client;pending[slot].used=0;pending[slot].request[0]=0;
        clock_gettime(CLOCK_MONOTONIC,&pending[slot].started);
        if (first_run_restart_requested) {
            fprintf(stderr, "First-run configuration saved; requesting a clean CC2 Control restart\n");
            running = 0;
        }
    }
    for(int i=0;i<HTTP_PENDING_MAX;i++)if(pending[i].fd>=0)close(pending[i].fd);
    free(pending);
    spools_flush();
    object_query_close();
    uds_close(&telemetry);
    panda_stop(&panda);
    mqtt_close(&mqtt);
    console_destroy(&console);
    close(server);
    return 0;
}
