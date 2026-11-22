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
    int cancel_tail_failed; /* ... and its cancel tail did not run to its end (the heaters may still be on) */
    atomic_int cancel_requested;
    char cancel_tail_next[CONSOLE_CANCEL_TAIL_MAX];
    char cancel_tail[CONSOLE_CANCEL_TAIL_MAX];
    unsigned staged_homed_next; /* the axes known as homed when the job is armed (CONSOLE_AXES_*) */
    unsigned staged_homed;      /* the same for the running or last staged job */
    char instance[17]; /* 16 lowercase hex digits, new at every start of the service: names this console's counters */
} console_state;

void console_init(console_state *state, const char *socket_path);
void console_destroy(console_state *state);
int console_clear(console_state *state);
int console_command_allowed(console_state *state, const char *command, const mqtt_client *mqtt,
                            char *reason, size_t reason_cap);
int console_start(console_state *state, const char *command, char *reason, size_t reason_cap);
/* The axes of the machine, as a set of bits (the homed axes of a printer, CONSOLE_AXES_ALL when all three are). */
#define CONSOLE_AXES_X 1u
#define CONSOLE_AXES_Y 2u
#define CONSOLE_AXES_Z 4u
#define CONSOLE_AXES_ALL 7u
/* A line of a cancel tail that starts with this is a move that needs referenced axes: it is sent only when all three
 * axes are known as homed at that point, and left out otherwise. The other lines of the tail always go out. */
#define CONSOLE_TAIL_IF_HOMED "?H "
/* The next console_start sends its script line by line and can be cancelled; `cancel_tail` (lines, may be NULL)
 * runs after a cancel instead of the lines not yet sent. `homed` is the set of axes the printer reports as homed now
 * (0 when that is not known): the job adds the axes its own completed lines home (G28, G28 X/Y/Z, G180 S3), and a
 * cancel tail line marked CONSOLE_TAIL_IF_HOMED waits for all three. A refused start disarms it. */
void console_arm_staged(console_state *state, const char *cancel_tail, unsigned homed);
/* Ask the running staged job to stop after its current line. 0 when accepted, -1 when no staged job runs. */
int console_cancel(console_state *state);
/* Whether the job of `generation` ended by a cancel. */
int console_cancelled(console_state *state, unsigned long generation);
int console_emergency_stop(console_state *state, char *reason, size_t reason_cap);
void console_json(console_state *state, char *out, size_t capacity);
/* Same reply as console_json(). A query string with since=<generation>:<output_len>:<instance> (the generation,
 * the stored output length and the instance the caller already holds, the instance as the reply gave it) shortens
 * the reply to "unchanged":true without the output while no command runs and all three still match. The generation
 * restarts with the service, so without the instance a restart could meet the same two numbers for other text: a
 * missing or different instance is answered in full, as is any other query. A reply to a valid since carries
 * output_len and instance. */
void console_json_query(console_state *state, const char *query, char *out, size_t capacity);

#endif
