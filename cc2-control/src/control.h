#ifndef CC2_CONTROL_H
#define CC2_CONTROL_H

#include <stddef.h>
#include "mqtt.h"

int control_build_script(const char *action, const mqtt_client *mqtt,
                         char *script, size_t script_cap,
                         char *reason, size_t reason_cap);

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
