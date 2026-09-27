#include "console.h"

#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static int append_output(console_state *state, const char *data, size_t length) {
    int truncated = 0;
    pthread_mutex_lock(&state->lock);
    size_t room = sizeof(state->output) - state->output_len - 1;
    if (length > room) { length = room; truncated = 1; }
    if (length) {
        memcpy(state->output + state->output_len, data, length);
        state->output_len += length;
        state->output[state->output_len] = '\0';
    }
    pthread_mutex_unlock(&state->lock);
    return truncated;
}

/* Responses from elegoo_printer may be split across read() calls.  Keep a
 * small rolling protocol window so the command-result marker is recognised
 * even after the user-visible output buffer has filled or the JSON token is
 * divided between packets. */
static int protocol_window_contains_result(char *window, size_t *used,
                                           const char *data, size_t length) {
    static const char marker[] = "\"id\":101";
    const size_t capacity = 511;
    if (length >= capacity) {
        data += length - capacity;
        length = capacity;
        *used = 0;
    } else if (*used + length > capacity) {
        size_t discard = *used + length - capacity;
        memmove(window, window + discard, *used - discard);
        *used -= discard;
    }
    memcpy(window + *used, data, length);
    *used += length;
    window[*used] = '\0';
    return strstr(window, marker) != NULL;
}

static int send_all_fd(int fd, const char *data, size_t length) {
    while (length) {
        ssize_t sent = send(fd, data, length, MSG_NOSIGNAL);
        if (sent < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        data += (size_t)sent;
        length -= (size_t)sent;
    }
    return 0;
}

static int json_escape_string(char *out, size_t cap, const char *in) {
    size_t used = 0;
    while (*in) {
        unsigned char c = (unsigned char)*in++;
        const char *escape = NULL;
        if (c == '"') escape = "\\\"";
        else if (c == '\\') escape = "\\\\";
        else if (c == '\n') escape = "\\n";
        else if (c == '\r') escape = "\\r";
        else if (c == '\t') escape = "\\t";
        if (escape) {
            size_t n = strlen(escape);
            if (used + n >= cap) return -1;
            memcpy(out + used, escape, n); used += n;
        } else if (c >= 32) {
            if (used + 1 >= cap) return -1;
            out[used++] = (char)c;
        }
    }
    if (used >= cap) return -1;
    out[used] = '\0';
    return 0;
}

static void finish(console_state *state, int success, const char *message) {
    if (message) append_output(state, message, strlen(message));
    pthread_mutex_lock(&state->lock);
    state->success = success;
    state->completed = 1;
    state->busy = 0;
    pthread_mutex_unlock(&state->lock);
}

static void *console_worker(void *opaque) {
    console_state *state = opaque;
    char command[CONSOLE_COMMAND_MAX], escaped[CONSOLE_COMMAND_MAX * 2];
    char request[CONSOLE_COMMAND_MAX * 2 + 128];
    char socket_path[108];
    struct sockaddr_un address;

    pthread_mutex_lock(&state->lock);
    snprintf(command, sizeof(command), "%s", state->command);
    snprintf(socket_path, sizeof(socket_path), "%s", state->socket_path);
    pthread_mutex_unlock(&state->lock);

    if (json_escape_string(escaped, sizeof(escaped), command) != 0) {
        finish(state, 0, "Command is too long after JSON encoding\n");
        return NULL;
    }

    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { finish(state, 0, "Cannot create UDS socket\n"); return NULL; }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path), "%s", socket_path);
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(fd); finish(state, 0, "Cannot connect to elegoo_printer\n"); return NULL;
    }

    const char subscribe[] = "{\"id\":100,\"method\":\"gcode/subscribe_report\",\"params\":{}}\003";
    if (send_all_fd(fd, subscribe, sizeof(subscribe) - 1) != 0) {
        close(fd); finish(state, 0, "Cannot subscribe to console reports\n"); return NULL;
    }
    int length = snprintf(request, sizeof(request),
        "{\"id\":101,\"method\":\"gcode/script\",\"params\":{\"script\":\"%s\"}}\003", escaped);
    if (length <= 0 || (size_t)length >= sizeof(request) ||
        send_all_fd(fd, request, (size_t)length) != 0) {
        close(fd); finish(state, 0, "Cannot send command\n"); return NULL;
    }

    append_output(state, "> ", 2);
    append_output(state, command, strlen(command));
    append_output(state, "\n", 1);

    int saw_command_result = 0;
    int output_saturated = 0;
    char protocol_window[512] = {0};
    size_t protocol_used = 0;
    time_t deadline = time(NULL) + 900;
    while (time(NULL) < deadline) {
        struct pollfd pfd = {fd, POLLIN, 0};
        int poll_timeout = saw_command_result ? 750 : (output_saturated ? 10000 : 2000);
        int ready = poll(&pfd, 1, poll_timeout);
        if (ready < 0) { if (errno == EINTR) continue; break; }
        if (ready == 0) {
            if (saw_command_result || output_saturated) {
                close(fd);
                finish(state, 1, output_saturated && !saw_command_result ?
                    "\n[CC2 Control: console output limit reached; operation released after 10 seconds without new reports]\n" : NULL);
                return NULL;
            }
            continue;
        }
        char buffer[4096];
        ssize_t got = read(fd, buffer, sizeof(buffer));
        if (got <= 0) break;
        if (protocol_window_contains_result(protocol_window, &protocol_used,
                                            buffer, (size_t)got))
            saw_command_result = 1;
        size_t start = 0;
        for (ssize_t i = 0; i < got; ++i) {
            if ((unsigned char)buffer[i] == 0x03) {
                if ((size_t)i > start)
                    output_saturated |= append_output(state, buffer + start, (size_t)i - start);
                output_saturated |= append_output(state, "\n", 1);
                start = (size_t)i + 1;
            }
        }
        if (start < (size_t)got)
            output_saturated |= append_output(state, buffer + start, (size_t)got - start);
    }
    close(fd);
    finish(state, 0, "Console connection ended or command timed out\n");
    return NULL;
}

void console_init(console_state *state, const char *socket_path) {
    memset(state, 0, sizeof(*state));
    pthread_mutex_init(&state->lock, NULL);
    snprintf(state->socket_path, sizeof(state->socket_path), "%s", socket_path);
}

void console_destroy(console_state *state) { pthread_mutex_destroy(&state->lock); }

int console_clear(console_state *state) {
    pthread_mutex_lock(&state->lock);
    if (state->busy) { pthread_mutex_unlock(&state->lock); return -1; }
    state->command[0]='\0'; state->output[0]='\0'; state->output_len=0;
    state->completed=0; state->success=0; state->generation++;
    pthread_mutex_unlock(&state->lock);
    return 0;
}

static void first_word(char *out, size_t cap, const char *command) {
    while (isspace((unsigned char)*command)) command++;
    size_t used = 0;
    while (*command && !isspace((unsigned char)*command) && *command != ';' && used + 1 < cap)
        out[used++] = (char)toupper((unsigned char)*command++);
    out[used] = '\0';
}

static int has_full_homing(const mqtt_client *mqtt) {
    if (!mqtt) return 0;
    return strchr(mqtt->homed_axes, 'x') && strchr(mqtt->homed_axes, 'y') && strchr(mqtt->homed_axes, 'z');
}

static int is_prehome_nonmotion(const char *word) {
    static const char *commands[] = {
        "HELP","STATUS","GET_POSITION","M105","M114","M115",
        "QUERY_PROBE","QUERY_ENDSTOPS","QUERY_FILAMENT_SENSOR","DUMP_TMC",
        "ACCELEROMETER_QUERY","LOAD_CELL_READ","LOAD_CELL_DIAGNOSTIC",
        "BED_MESH_OUTPUT","G90","G91","M82","M83",
        "SET_HEATER_TEMPERATURE","TURN_OFF_HEATERS","M104","M109","M140","M190",
        "M106","M107","SET_FAN_SPEED","SET_CAVITY_FAN"
    };
    for (size_t i = 0; i < sizeof(commands)/sizeof(commands[0]); ++i)
        if (strcmp(word, commands[i]) == 0) return 1;
    return 0;
}

static int is_kinematic_bypass(const char *word) {
    static const char *blocked[] = {
        "FORCE_MOVE", "SET_KINEMATIC_POSITION", "MANUAL_STEPPER", "STEPPER_BUZZ",
        "SET_STEPPER_ENABLE", "M18", "M84", "G92"
    };
    for (size_t i = 0; i < sizeof(blocked)/sizeof(blocked[0]); ++i)
        if (strcmp(word, blocked[i]) == 0) return 1;
    return 0;
}

static int parse_axis_value(const char *command, char axis, double *value) {
    const char *p = command;
    while (*p && !isspace((unsigned char)*p)) p++; /* skip G0/G1 token */
    while (*p) {
        if (*p == ';') break;
        if (toupper((unsigned char)*p) == axis) {
            char *end = NULL;
            double v = strtod(p + 1, &end);
            if (end == p + 1) return -1;
            *value = v;
            return 1;
        }
        p++;
    }
    return 0;
}

static int direct_move_safe(console_state *state, const char *command, const mqtt_client *mqtt,
                            char *reason, size_t reason_cap) {
    if (!mqtt->have_position) {
        snprintf(reason, reason_cap, "Current position is unavailable; movement rejected");
        return 0;
    }
    if (!has_full_homing(mqtt)) {
        snprintf(reason, reason_cap, "Home X, Y and Z before manual movement");
        return 0;
    }

    int relative;
    pthread_mutex_lock(&state->lock);
    relative = state->relative_xyz;
    pthread_mutex_unlock(&state->lock);

    const char axes[] = {'X','Y','Z'};
    const double current[] = {mqtt->x, mqtt->y, mqtt->z};
    const double minimum[] = {0.0, 0.0, -2.0};
    const double maximum[] = {256.0, 266.0, 256.0};
    int any_xyz = 0;
    for (size_t i = 0; i < 3; ++i) {
        double requested = 0.0;
        int found = parse_axis_value(command, axes[i], &requested);
        if (found < 0) {
            snprintf(reason, reason_cap, "Invalid %c coordinate", axes[i]);
            return 0;
        }
        if (!found) continue;
        any_xyz = 1;
        double target = relative ? current[i] + requested : requested;
        if (target < minimum[i] - 0.0001 || target > maximum[i] + 0.0001) {
            snprintf(reason, reason_cap,
                     "%c target %.3f is outside the protected range %.3f..%.3f",
                     axes[i], target, minimum[i], maximum[i]);
            return 0;
        }
    }
    /* E-only G0/G1 is still a manual move. Full homing above is intentionally
       required, but there is no XYZ target to range-check here. */
    (void)any_xyz;
    return 1;
}

static int console_idle_safe(const mqtt_client *mqtt, char *reason, size_t reason_cap) {
    if (!mqtt || !mqtt->connected || !mqtt->registered) {
        snprintf(reason, reason_cap, "Printer MQTT is not ready");
        return 0;
    }
    if (!mqtt->have_machine_status) {
        snprintf(reason, reason_cap, "Printer state is unavailable");
        return 0;
    }
    if (mqtt->last_message > 0 && time(NULL) - mqtt->last_message > 20) {
        snprintf(reason, reason_cap, "Printer telemetry is stale");
        return 0;
    }
    if (mqtt->machine_status == 2) {
        snprintf(reason, reason_cap, "Manual console is blocked while printing");
        return 0;
    }
    if (mqtt->machine_status != 1) {
        snprintf(reason, reason_cap, "Manual console requires the printer to be idle (machine_status=%d)", mqtt->machine_status);
        return 0;
    }
    return 1;
}

int console_command_allowed(console_state *state, const char *command, const mqtt_client *mqtt,
                            char *reason, size_t reason_cap) {
    char word[80];
    first_word(word, sizeof(word), command);
    if (!word[0] || strchr(command, '\n') || strchr(command, '\r')) {
        snprintf(reason, reason_cap, "Only one command at a time is allowed");
        return 0;
    }
    if (!console_idle_safe(mqtt, reason, reason_cap)) {
        return 0;
    }
    if (is_kinematic_bypass(word)) {
        snprintf(reason, reason_cap, "Command is blocked because it can bypass homing or motion limits");
        return 0;
    }
    if (strcmp(word, "G28") == 0) return 1;
    if (strcmp(word, "G0") == 0 || strcmp(word, "G00") == 0 ||
        strcmp(word, "G1") == 0 || strcmp(word, "G01") == 0)
        return direct_move_safe(state, command, mqtt, reason, reason_cap);

    if (!has_full_homing(mqtt) && !is_prehome_nonmotion(word)) {
        snprintf(reason, reason_cap,
                 "Home X, Y and Z before commands that may move the printer");
        return 0;
    }

    /* Once homed, behave like a normal Klipper console: pass supported
       commands/macros through to Klipper. Klipper remains the final authority
       for command validity and kinematic limits. */
    if (strcmp(word, "G90") == 0 || strcmp(word, "G91") == 0) {
        pthread_mutex_lock(&state->lock);
        state->relative_xyz = strcmp(word, "G91") == 0;
        pthread_mutex_unlock(&state->lock);
    }
    return 1;
}

int console_start(console_state *state, const char *command, char *reason, size_t reason_cap) {
    pthread_t thread;
    pthread_mutex_lock(&state->lock);
    if (state->busy) { pthread_mutex_unlock(&state->lock); snprintf(reason,reason_cap,"Another command is running"); return -1; }
    snprintf(state->command, sizeof(state->command), "%s", command);
    state->output[0]='\0'; state->output_len=0; state->busy=1; state->completed=0; state->success=0; state->generation++;
    pthread_mutex_unlock(&state->lock);
    if (pthread_create(&thread, NULL, console_worker, state) != 0) {
        finish(state, 0, "Cannot start console worker\n"); return -1;
    }
    pthread_detach(thread);
    return 0;
}

void console_json(console_state *state, char *out, size_t capacity) {
    char escaped[CONSOLE_OUTPUT_MAX * 2];
    char escaped_command[CONSOLE_COMMAND_MAX * 2];
    pthread_mutex_lock(&state->lock);
    if (json_escape_string(escaped, sizeof(escaped), state->output) != 0) snprintf(escaped,sizeof(escaped),"Output too large");
    if (json_escape_string(escaped_command, sizeof(escaped_command), state->command) != 0) escaped_command[0]='\0';
    snprintf(out, capacity, "{\"busy\":%s,\"completed\":%s,\"success\":%s,\"generation\":%lu,\"command\":\"%s\",\"output\":\"%s\"}\n",
        state->busy?"true":"false",state->completed?"true":"false",state->success?"true":"false",state->generation,escaped_command,escaped);
    pthread_mutex_unlock(&state->lock);
}
