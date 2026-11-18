#ifndef CC2_CONSOLE_H
#define CC2_CONSOLE_H

#include <pthread.h>
#include <stddef.h>
#include <stdatomic.h>
#include "mqtt.h"

#define CONSOLE_OUTPUT_MAX 262144
#define CONSOLE_COMMAND_MAX 512
#define CONSOLE_CANCEL_TAIL_MAX 160

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
    /* A staged job is sent line by line, so it can be cancelled between two lines: the rest is left out and
     * `cancel_tail` runs instead (console_arm_staged / console_cancel). */
    int staged_next; /* armed for the next console_start */
    int staged;      /* the running or last job was staged */
    int cancelled;   /* the last job ended by a cancel */
    atomic_int cancel_requested;
    char cancel_tail_next[CONSOLE_CANCEL_TAIL_MAX];
    char cancel_tail[CONSOLE_CANCEL_TAIL_MAX];
} console_state;

void console_init(console_state *state, const char *socket_path);
void console_destroy(console_state *state);
int console_clear(console_state *state);
int console_command_allowed(console_state *state, const char *command, const mqtt_client *mqtt,
                            char *reason, size_t reason_cap);
int console_start(console_state *state, const char *command, char *reason, size_t reason_cap);
/* The next console_start sends its script line by line and can be cancelled; `cancel_tail` (lines, may be NULL)
 * runs after a cancel instead of the lines not yet sent. A refused start disarms it. */
void console_arm_staged(console_state *state, const char *cancel_tail);
/* Ask the running staged job to stop after its current line. 0 when accepted, -1 when no staged job runs. */
int console_cancel(console_state *state);
/* Whether the job of `generation` ended by a cancel. */
int console_cancelled(console_state *state, unsigned long generation);
int console_emergency_stop(console_state *state, char *reason, size_t reason_cap);
void console_json(console_state *state, char *out, size_t capacity);

#endif
