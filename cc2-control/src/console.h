#ifndef CC2_CONSOLE_H
#define CC2_CONSOLE_H

#include <pthread.h>
#include <stddef.h>
#include <stdatomic.h>
#include "mqtt.h"

#define CONSOLE_OUTPUT_MAX 262144
#define CONSOLE_COMMAND_MAX 512

typedef struct {
    pthread_mutex_t lock;
    atomic_int emergency_sent; /* Armed only after an exact M112 has reached the UDS socket. */
    int busy;
    int completed;
    int success;
    unsigned long generation;
    int relative_xyz; /* modal state for direct G0/G1 range checks: G90=0, G91=1 */
    char command[CONSOLE_COMMAND_MAX];
    char output[CONSOLE_OUTPUT_MAX];
    size_t output_len;
    char socket_path[108];
} console_state;

void console_init(console_state *state, const char *socket_path);
void console_destroy(console_state *state);
int console_clear(console_state *state);
int console_command_allowed(console_state *state, const char *command, const mqtt_client *mqtt,
                            char *reason, size_t reason_cap);
int console_start(console_state *state, const char *command, char *reason, size_t reason_cap);
void console_json(console_state *state, char *out, size_t capacity);

#endif
