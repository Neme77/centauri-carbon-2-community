#include <arpa/inet.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/statvfs.h>
#include <limits.h>
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
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#include "mqtt.h"
#include "panda.h"
#include "console.h"
#include "control.h"

#define REQUEST_MAX 12288
#define FILE_UPLOAD_MAX (64UL * 1024UL * 1024UL)
#define UPLOAD_CHUNK 16384
#define PATH_MAX_LOCAL 512
#define GCODE_FILES_MAX 128
#define GCODE_SCAN_DEPTH_MAX 4
#define GCODE_JSON_CAP (192 * 1024)
#define GCODE_TOOLS_MAX 16
#define GCODE_THUMBNAIL_SCAN_MAX (4 * 1024 * 1024)
#define GCODE_THUMBNAIL_BASE64_MAX (1024 * 1024)

#define GCODE_INTERNAL_ROOT "/opt/usr/gcode/local"
#define GCODE_USB_ROOT "/mnt/exUDISK"
#define GCODE_USB_IMPORT_PREFIX "CC2_USB_"

#define CC2_CONTROL_VERSION "1.1.26"
#define CC2_COMMUNITY_FIRMWARE_VERSION "4.1"
#define CC2_DISCOVERY_API_VERSION 1

static volatile sig_atomic_t running = 1;
static const char *material_presets_path = "./material-presets.json";
static const char *mqtt_config_path = "./cc2-control.conf";
static const char *ui_preferences_path = "./ui-preferences.json";
static const char *gcode_internal_root = GCODE_INTERNAL_ROOT;
static const char *gcode_usb_root = GCODE_USB_ROOT;
static int setup_mode = 0;
static int service_http_port = 8081;
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
    char body[512];
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
        "\"snapshot_received\":%s}\n",
        uptime, mem_total, mem_available, load, mqtt->connected ? "true" : "false",
        mqtt->registered ? "true" : "false", mqtt->snapshot_len ? "true" : "false");
    if (length < 0) return;
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
            "\"persistent_ui_preferences\":true"
        "},"
        "\"endpoints\":{"
            "\"printer\":\"/api/printer\","
            "\"health\":\"/api/health\","
            "\"canvas\":\"/api/canvas\","
            "\"files\":\"/api/gcode-files\","
            "\"console\":\"/api/console\","
            "\"preferences\":\"/api/preferences\","
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
        else json_builder_printfa_port > 0 ? "true" : "false",
        mqtt && mqtt->connected ? "true" : "false",
        mqtt && mqtt->registered ? "true" : "false");
    if (length > 0 && (size_t)length < sizeof(body))
        respond(fd, 200, "OK", "application/json; charset=utf-8", body, (size_t)length);
}

static void json_number(char *out, size_t cap, int have, double value) {
    if (have) snprintf(out, cap, "%.1f", value);
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

static void scan_gcode_directory(const char *root, const char *relative,
                                 unsigned int depth, gcode_file_list *list) {
    if (depth > GCODE_SCAN_DEPTH_MAX || list->count >= GCODE_FILES_MAX) {
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
            if (list->count >= GCODE_FILES_MAX) {
                list->truncated = 1;
                break;
            }
            gcode_file_entry *entry = &list->files[list->count++];
            snprintf(entry->relative_path, sizeof(entry->relative_path), "%s", child_relative);
            entry->size = (long long)status.st_size;
            entry->modified = (long long)status.st_mtime;
        }
        if (list->count >= GCODE_FILES_MAX) {
            list->truncated = 1;
            break;
        }
    }
    closedir(directory);
}

static int compare_gcode_files(const void *left, const void *right) {
    const gcode_file_entry *a = (const gcode_file_entry *)left;
    const gcode_file_entry *b = (const gcode_file_entry *)right;
    if (a->modified != b->modified) return a->modified < b->modified ? 1 : -1;
    return strcasecmp(a->relative_path, b->relative_path);
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
    json_builder_printf(builder, ":{\"available\":%s,\"truncated\":%s,\"count\":%lu,\"files\":[",
                        list->available ? "true" : "false",
                        list->truncated ? "true" : "false",
                        (unsigned long)list->count);
    for (size_t index = 0; index < list->count; ++index) {
        const gcode_file_entry *entry = &list->files[index];
        if (index) json_builder_printf(builder, ",");
        json_builder_printf(builder, "{\"path\":");
        json_builder_string(