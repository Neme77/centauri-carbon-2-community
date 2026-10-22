#ifndef CC2_EXPERIMENTS_H
#define CC2_EXPERIMENTS_H
/* Included after the existing update/plate guards. HTTP-loop-owned choices;
 * changing the source never sends a motor, heater or printer restart command. */
static int experiments_normal_confirmed(console_state *console) {
    if (!motor_mode_requested || strcmp(motor_mode_requested, "normal") ||
        telemetry.fd < 0 || telemetry.connections != motor_mode_connection) return 0;
    pthread_mutex_lock(&console->lock);
    int ok = !console->busy && console->completed && console->success;
    char normal[CONSOLE_COMMAND_MAX], reason[160];
    mqtt_client idle = {0};
    idle.connected = idle.registered = idle.have_machine_status = 1;
    idle.machine_status = 1; idle.last_message = time(NULL);
    if (!control_build_script("motors:normal", &idle, normal, sizeof(normal), reason, sizeof(reason)) ||
        strcmp(console->command, normal)) ok = 0;
    pthread_mutex_unlock(&console->lock);
    return ok;
}

static void experiments_response(int fd, const mqtt_client *mqtt, console_state *console) {
    char body[640];
    int n = snprintf(body, sizeof(body),
        "{\"schema\":1,\"editable\":%s,\"ready\":%s,\"canvas_uds\":%s,\"canvas_uds_available\":%s,"
        "\"quiet_motors\":%s,\"normal_confirmed\":%s,\"cli_canvas\":%s}\n",
        experiments_storage_valid ? "true" : "false",
        !app_update_active() && app_update_ready(mqtt, console) ? "true" : "false",
        telemetry.canvas_enabled ? "true" : "false", uds_canvas_available(&telemetry) ? "true" : "false",
        experiments.quiet_motors ? "true" : "false", experiments_normal_confirmed(console) ? "true" : "false",
        experiments_cli_canvas ? "true" : "false");
    respond(fd, 200, "OK", "application/json", body, (size_t)n);
}

static void experiments_put_response(int fd, const char *request, const char *body, size_t length,
                                     mqtt_client *mqtt, console_state *console) {
    if (!app_update_authorized(request, mqtt)) { app_update_fail(fd, 401, "LAN code confirmation required or temporarily locked"); return; }
    experiment_choices next = experiments;
    if (length == 13 && !memcmp(body, "canvas-uds:on", 13)) next.canvas_uds = 1;
    else if (length == 14 && !memcmp(body, "canvas-uds:off", 14)) next.canvas_uds = 0;
    else if (length == 15 && !memcmp(body, "quiet-motors:on", 15)) next.quiet_motors = 1;
    else if (length == 16 && !memcmp(body, "quiet-motors:off", 16)) next.quiet_motors = 0;
    else { app_update_fail(fd, 400, "Invalid experimental feature selection"); return; }
    if (!experiments_storage_valid) { app_update_fail(fd, 409, "Experimental settings storage is invalid"); return; }
    if (app_update_active() || !app_update_ready(mqtt, console)) {
        app_update_fail(fd, 409, "Printer must be connected, idle and cold with heating targets zero"); return;
    }
    if (next.canvas_uds != experiments.canvas_uds && experiments_cli_canvas) {
        app_update_fail(fd, 409, "Canvas UDS is controlled by a command-line option"); return;
    }
    if (next.canvas_uds != experiments.canvas_uds || next.quiet_motors != experiments.quiet_motors) {
        if (experiments.quiet_motors && (!next.quiet_motors || next.canvas_uds != experiments.canvas_uds) &&
            !experiments_normal_confirmed(console)) {
            app_update_fail(fd, 409, "Return the motors to Normal before disabling Quiet or changing the Canvas source"); return;
        }
        int source_changed = next.canvas_uds != experiments.canvas_uds;
        if (!experiments_save(next)) { app_update_fail(fd, 503, "Cannot save experimental settings"); return; }
        if (source_changed) {
            uds_close(&telemetry);
            telemetry.canvas_enabled = experiments.canvas_uds;
            uds_motion_light(&telemetry, mqtt); /* Immediately revoke stale direct-state authority. */
        }
    }
    experiments_response(fd, mqtt, console);
}

#endif
