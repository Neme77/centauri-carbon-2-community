#ifndef CC2_CONTROL_H
#define CC2_CONTROL_H

#include <stddef.h>
#include "mqtt.h"

int control_build_script(const char *action, const mqtt_client *mqtt,
                         char *script, size_t script_cap,
                         char *reason, size_t reason_cap);

/* Interactive screw-tramming session (issue #329). Corner IDs are FL/FR/RR/RL
 * from a fixed table; re-probes home Z only after a session start that did G28.
 * The session is memory-only and is cleared on disconnect, error, timeout,
 * emergency stop, one-shot screws:measure, or an explicit end. */
#define SCREWS_SETTLE_S 30
#define SCREWS_SESSION_TIMEOUT_S 1200
/* Seconds after the session's own job completed during which Manual homing (10)
 * and Auto leveling (5) are still accepted, because the report trails the job. */
#define SCREWS_STATUS_GRACE_S 15
/* Renew the session's 60 C bed target before the firmware's 15-minute idle
 * bed shutdown; a failed send is retried after 30 s. */
#define SCREWS_BED_RENEW_S 600
void control_screws_reset(void);
void control_screws_invalidate(void);
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
/* Ask for the deferred TURN_OFF_HEATERS without a tram session (the mesh-guided
 * alignment session ended), and drop it when a job that heats the bed takes over. */
void control_screws_request_heaters_off(void);
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
typedef struct { int kind, corner, twice; time_t started; } control_screws_job;
/* Returns 1 once after a session job or a one-shot screws:measure (kind QUICK,
 * no session, no settle) completed successfully, and fills *job. */
int control_screws_take_finished(control_screws_job *job);
/* The queued console job is the one-shot screws:measure. */
void control_screws_note_quick(unsigned long generation);
#define CONTROL_SCREW_CORNERS 4
/* Corner ID for a SCREW_CORNERS index, or NULL. */
const char *control_screws_corner_id(int corner);
/* SCREW_CORNERS index of a probe position (rounded to whole mm), or -1. */
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
} control_idle_sample;

typedef struct {
    int holding, released;
    time_t since;
    double x, y, z;
} control_idle_motors;

/* Returns 1, once per stationary idle period, when M84 is due. */
int control_idle_motors_due(control_idle_motors *state, const control_idle_sample *sample, time_t now);

#endif
