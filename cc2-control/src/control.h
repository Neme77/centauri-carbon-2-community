#ifndef CC2_CONTROL_H
#define CC2_CONTROL_H

#include <stddef.h>
#include "mqtt.h"

int control_build_script(const char *action, const mqtt_client *mqtt,
                         char *script, size_t script_cap,
                         char *reason, size_t reason_cap);
/* The same checks and the same script as control_build_script, without its effects: no screw session starts or ends
 * and no measurement is noted. A hold time (screws:soak) runs them when it is accepted and again when it starts. */
int control_check_script(const char *action, const mqtt_client *mqtt,
                         char *script, size_t script_cap,
                         char *reason, size_t reason_cap);

/* Interactive screw-tramming session (issue #329). Corner IDs are FL/FR/RR/RL
 * from a fixed table; re-probes home Z only after a session start that did G28.
 * The session is memory-only and is cleared on disconnect, error, timeout,
 * emergency stop, a started mesh calibration, or an explicit end. */
#define SCREWS_SETTLE_S 30
#define SCREWS_SESSION_TIMEOUT_S 600 /* 10 minutes without a session action end the session */
/* Seconds after the session's own job completed during which Manual homing (10)
 * and Auto leveling (5) are still accepted, because the report trails the job. */
#define SCREWS_STATUS_GRACE_S 15
/* Renew the session's 60 C bed target before the firmware's 15-minute idle
 * bed shutdown; a failed send is retried after 30 s. */
#define SCREWS_BED_RENEW_S 600
void control_screws_reset(void);
void control_screws_invalidate(void);
/* Why the last tramming session ended; kept until the next session starts. The names are
 * stored in the screw history and reported by GET /api/screws/history as "end_reason". */
enum {
    CONTROL_SCREWS_END_NONE,        /* no session has ended yet, or the reason is unknown */
    CONTROL_SCREWS_END_USER,        /* screws:tram:end */
    CONTROL_SCREWS_END_EMERGENCY,   /* emergency stop */
    CONTROL_SCREWS_END_DISCONNECT,  /* printer MQTT disconnected or not registered */
    CONTROL_SCREWS_END_PRINT,       /* a print started */
    CONTROL_SCREWS_END_TELEMETRY,   /* no printer report for more than 15 s */
    CONTROL_SCREWS_END_CONSOLE,     /* a session job failed or could not be queued */
    CONTROL_SCREWS_END_STATE,       /* another machine state, or none reported */
    CONTROL_SCREWS_END_TIMEOUT,     /* 10 minutes without a session action */
    CONTROL_SCREWS_END_XY,          /* X/Y homing lost */
    CONTROL_SCREWS_END_MEASURE,     /* a one-shot screws:measure replaced the session */
    CONTROL_SCREWS_END_HEATERS_OFF, /* heaters:off / system:heaters_off */
    CONTROL_SCREWS_END_NEW_SESSION, /* a new screws:tram:start replaced the session */
    CONTROL_SCREWS_END_RESTART,     /* CC2 Control restarted while the session was open */
    CONTROL_SCREWS_END_MESH,        /* a bed mesh calibration was started while no session job was running */
    CONTROL_SCREWS_END_COUNT
};
/* Ends an active session for `reason` (nothing happens without one). */
void control_screws_invalidate_for(int reason);
int control_screws_end_reason(void);
/* The stored name of a reason, or NULL for CONTROL_SCREWS_END_NONE and unknown values. */
const char *control_screws_end_name(int reason);
/* The reason for a stored name, or CONTROL_SCREWS_END_NONE. */
int control_screws_end_from_name(const char *name);
int control_screws_session_active(void);
/* Active session and MQTT currently reports homed X and Y. */
int control_screws_xy_homed(const mqtt_client *mqtt);
int control_screws_is_tram(const char *action);
void control_screws_note_job(unsigned long generation);
/* Returns 1 when an active session was just cleared and heaters should be
 * switched off (timeout, disconnect, failed job, lost XY homing, estop,
 * unexpected machine state). The shutdown also stays pending until
 * control_screws_heaters_off_sent(), a print starts, or a queued heater
 * action (control_screws_note_action) takes the heaters over. */
int control_screws_tick(const mqtt_client *mqtt, int console_completed, int console_success,
                        unsigned long generation, int emergency, time_t now);
int control_screws_heaters_off_pending(void);
void control_screws_heaters_off_sent(void);
/* Drop the deferred TURN_OFF_HEATERS when a job that heats the bed takes over. */
void control_screws_heaters_taken(void);
/* The console job (generation) running an explicit screws:tram:end. Its
 * successful completion clears the pending shutdown; a rejected or failed
 * run keeps it pending for the main loop to retry. */
void control_screws_note_end(unsigned long generation);
/* A previous process left a session or a pending shutdown behind (see the
 * session marker in main.c): the shutdown is pending again. */
void control_screws_restore_pending(void);
int control_heater_action(const char *action);
void control_screws_note_action(const char *action);
int control_screws_bed_renew_due(time_t now);
void control_screws_bed_renewed(time_t now, int delivered);

/* A completed session job, for the persistent measurement history. corner is
 * the SCREW_CORNERS index (FL, FR, RR, RL) of a single-corner re-probe, else -1;
 * started identifies the session (time of its screws:tram:start). */
enum { CONTROL_SCREWS_JOB_START, CONTROL_SCREWS_JOB_PROBE, CONTROL_SCREWS_JOB_CHECK, CONTROL_SCREWS_JOB_QUICK };
/* bed_set: bed is the bed target (°C) the job measured at; jobs from before it was chosen have none. */
typedef struct { int kind, corner, twice; time_t started; int bed_set, bed; } control_screws_job;
/* Returns 1 once after a session job or a one-shot screws:measure (kind QUICK,
 * no session, no settle) completed successfully, and fills *job. */
int control_screws_take_finished(control_screws_job *job);
/* A session job or the one-shot screws:measure is queued or running (its console job has not completed). */
int control_screws_measuring(void);
/* R11: what a cancelled measurement job runs instead of its remaining lines (NULL: the action is not such a job),
 * and the note that the user asked for the cancel. */
const char *control_screws_cancel_tail(const char *action);
/* The axes the mqtt client reports as homed (CONSOLE_AXES_* bits), 0 when the position is not known. */
unsigned control_axes_homed(const mqtt_client *m);
void control_screws_note_cancel(void);
/* The queued console job is the one-shot screws:measure. */
void control_screws_note_quick(unsigned long generation);
#define CONTROL_SCREW_CORNERS 4
/* Bed target of a tramming session or a one-shot measurement without one given (screws:tram:start,
 * screws:measure); screws:tram:start:<C> and screws:measure:<C> choose 0..110 C instead; 110 C is the highest bed target the
 * printer's touchscreen offers. */
#define CONTROL_SCREWS_BED_DEFAULT 60
#define CONTROL_SCREWS_BED_MAX 110
/* The running session's bed target, or 0 without a session. */
int control_screws_bed(void);
/* A valid screws:tram:start[:<C>] / screws:measure[:<C>] action. */
int control_screws_is_start(const char *action);
int control_screws_is_quick(const char *action);
/* The bed target of a valid quick measurement (four corners or one corner) or session start, else -1. */
int control_screws_action_bed(const char *action);
/* "session" (screws:tram:start), "corner" (screws:measure:corner:<ID>), "quick" (screws:measure) or NULL. */
const char *control_screws_action_kind(const char *action);

/* A hold time before a screw measurement: screws:soak:<minutes>:<measurement>, the minutes 0..60 (one or two digits, as
 * the bed mesh calibration's soak) and the measurement a screws:measure... or screws:tram:start... action. Returns 1
 * and copies the measurement into `inner` (cap bytes), 0 when `action` has no such prefix, -1 with a reason (400) when
 * the minutes or the kind of measurement are not accepted. The form of the measurement itself is checked as without
 * a hold time. */
#define CONTROL_SCREWS_SOAK_MAX 60
int control_screws_soak_parse(const char *action, int *minutes, char *inner, size_t cap, char *reason, size_t reason_cap);

/* A four-corner screws:measure or a screws:tram:start may follow a bed mesh directly: the caller passes the bed target
 * the mesh was just probed at (-1: none, the default) before it builds the script. With that target and X, Y and Z
 * homed the script leaves out the nozzle cleaning (G180 S3) and the full G28: M140, M190, G28 Z, the 30 s wait, then
 * the corners. control_screws_built_direct() tells whether the last built script is that variant, and
 * control_screws_after_mesh_running() whether the queued or running screw job is. */
void control_screws_after_mesh(int bed);
int control_screws_built_direct(void);
int control_screws_after_mesh_running(void);
/* Corner ID for a SCREW_CORNERS index, or NULL. */
const char *control_screws_corner_id(int corner);
/* SCREW_CORNERS index of a probe position (within 1.5 mm of that screw in X and Y), or -1. */
int control_screws_corner_at(double x, double y);

/* SET_TMC_FIELD values for the X/Y motor mode (motors:quiet / motors:normal).
 * The firmware refuses a literal 0, so the zero values overflow the field
 * width and are masked to 0 by the driver register helper. */
#define CONTROL_TPWMTHRS_ZERO "1048576"
#define CONTROL_TPWMTHRS_MAX "1048575"
#define CONTROL_SPREADCYCLE_ZERO "2"
/* Quiet caps the toolhead before StealthChop is enabled: StealthChop loses
 * steps at the stock 500 mm/s and 20000 mm/s^2. 80 mm/s and 2000 mm/s^2 is the
 * pair tested on a CC2. Normal restores the stock printer.cfg [printer]
 * max_velocity and max_accel: CC2 Control does not read the printer config,
 * and Klipper's toolhead limits are the current ones, which Quiet lowered. */
#define CONTROL_QUIET_VELOCITY "80"
#define CONTROL_QUIET_ACCEL "2000"
#define CONTROL_NORMAL_VELOCITY "500"
#define CONTROL_NORMAL_ACCEL "20000"

/* Quiet belongs to a print, not to the printer. Returns 1 once when a print
 * (printing or paused) has run while Quiet was requested and the printer is
 * idle again, so the caller switches back to Normal. *seen_print is the
 * caller's state, cleared while Quiet is not requested. */
int control_motor_mode_restore_due(int *seen_print, int quiet, int machine_status, const char *print_state);

/* Stock Klipper's idle_timeout releases the steppers after ten idle minutes.
 * The CC2 firmware's idle_timeout only switches heaters off, so after homing,
 * calibration or jogging the steppers stay energised and the board fan, which
 * runs while any of them is enabled, never stops. */
#define CONTROL_IDLE_MOTORS_SECONDS 600

typedef struct {
    int valid;              /* fresh MQTT state and UDS telemetry, known position */
    int machine_status;
    const char *print_state;
    int console_busy;
    double board_fan;       /* controller_fan speed, 0..1 */
    double nozzle_target, bed_target, velocity;
    double x, y, z;
    int session;            /* a screw-tramming session needs its X/Y homing: no M84 */
} control_idle_sample;

typedef struct {
    int holding, released;
    time_t since;
    double x, y, z;
} control_idle_motors;

/* Returns 1, once per stationary idle period, when M84 is due. */
int control_idle_motors_due(control_idle_motors *state, const control_idle_sample *sample, time_t now);

#endif
