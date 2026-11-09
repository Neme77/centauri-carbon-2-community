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
void control_screws_reset(void);
void control_screws_invalidate(void);
int control_screws_session_active(void);
int control_screws_is_tram(const char *action);
void control_screws_note_job(unsigned long generation);
/* Returns 1 when an active session was just cleared and heaters should be
 * switched off (timeout, disconnect, failed job, lost XY homing, estop). */
int control_screws_tick(const mqtt_client *mqtt, int console_completed, int console_success,
                        unsigned long generation, int emergency, time_t now);

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
