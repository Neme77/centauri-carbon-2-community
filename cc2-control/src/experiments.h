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

static enum { QUIET_DISABLE_IDLE, QUIET_DISABLE_RUNNING, QUIET_DISABLE_FAILED } quiet_disable_state;
static unsigned long quiet_disable_generation;
static int quiet_disable_ready(const mqtt_client *mqtt, console_state *console) {
    return experiments.quiet_motors && quiet_disable_state != QUIET_DISABLE_RUNNING &&
        !printer_change_blocked() && app_update_idle_cold(mqtt,console,50);
}

static void quiet_disable_tick(console_state *console) {
    if(quiet_disable_state!=QUIET_DISABLE_RUNNING)return;
    pthread_mutex_lock(&console->lock);
    int same=console->generation==quiet_disable_generation;
    int busy=same&&console->busy;
    int success=same&&console->completed&&console->success;
    pthread_mutex_unlock(&console->lock);
    if(busy)return;
    experiment_choices next=experiments;
    next.quiet_motors=0;
    quiet_disable_state=success&&telemetry.fd>=0&&telemetry.connections==motor_mode_connection&&
        experiments_save(next)?QUIET_DISABLE_IDLE:QUIET_DISABLE_FAILED;
}

static void experiments_response(int fd, const mqtt_client *mqtt, console_state *console) {
    static const char *const quiet_states[]={"idle","running","failed"};
    char body[640];
    int n = snprintf(body, sizeof(body),
        "{\"schema\":1,\"editable\":%s,\"ready\":%s,\"canvas_uds\":%s,\"canvas_uds_available\":%s,"
        "\"external_feed\":%s,\"bed_drying\":%s,\"nozzle_expansion\":%s,\"mesh_align_settings\":%s,\"quiet_motors\":%s,\"normal_confirmed\":%s,\"cli_canvas\":%s,"
        "\"quiet_disable_ready\":%s,\"quiet_disable_state\":\"%s\"}\n",
        experiments_storage_valid ? "true" : "false",
        !app_update_active() && app_update_ready(mqtt, console) ? "true" : "false",
        telemetry.canvas_enabled ? "true" : "false", uds_canvas_available(&telemetry) ? "true" : "false",
        external_feed_enabled ? "true" : "false",
        experiments.bed_drying ? "true" : "false",
        experiments.nozzle_expansion ? "true" : "false",
        experiments.mesh_align_settings ? "true" : "false",
        experiments.quiet_motors ? "true" : "false", experiments_normal_confirmed(console) ? "true" : "false",
        experiments_cli_canvas ? "true" : "false",
        quiet_disable_ready(mqtt,console)?"true":"false",quiet_states[quiet_disable_state]);
    respond(fd, 200, "OK", "application/json", body, (size_t)n);
}

static void experiments_put_response(int fd, const char *request, const char *body, size_t length,
                                     mqtt_client *mqtt, console_state *console) {
    if (!app_update_authorized(request, mqtt)) { app_update_fail(fd, 401, "LAN code confirmation required or temporarily locked"); return; }
    if((length==16&&!memcmp(body,"external-feed:on",16))||(length==17&&!memcmp(body,"external-feed:off",17))){
        int enabled=length==16;
        if(app_update_active()||!app_update_ready(mqtt,console)){
            app_update_fail(fd,409,"Printer must be connected, idle and cold with heating targets zero");return;
        }
        if(!enabled&&external_source_selected){app_update_fail(fd,409,"Select Canvas with the printhead empty before disabling external feed");return;}
        if(!external_feed_store(enabled,enabled?external_source_selected:0)){app_update_fail(fd,503,"Cannot save external-feed settings");return;}
        experiments_response(fd,mqtt,console);return;
    }
    experiment_choices next = experiments;
    if (length == 13 && !memcmp(body, "canvas-uds:on", 13)) next.canvas_uds = 1;
    else if (length == 14 && !memcmp(body, "canvas-uds:off", 14)) next.canvas_uds = 0;
    else if (length == 15 && !memcmp(body, "quiet-motors:on", 15)) next.quiet_motors = 1;
    else if (length == 16 && !memcmp(body, "quiet-motors:off", 16)) next.quiet_motors = 0;
    else if (length == 13 && !memcmp(body,"bed-drying:on",13)) next.bed_drying=1;
    else if (length == 14 && !memcmp(body,"bed-drying:off",14)) next.bed_drying=0;
    else if (length == 19 && !memcmp(body,"nozzle-expansion:on",19)) next.nozzle_expansion=1;
    else if (length == 20 && !memcmp(body,"nozzle-expansion:off",20)) next.nozzle_expansion=0;
    else if (length == 22 && !memcmp(body,"mesh-align-settings:on",22)) next.mesh_align_settings=1;
    else if (length == 23 && !memcmp(body,"mesh-align-settings:off",23)) next.mesh_align_settings=0;
    else { app_update_fail(fd, 400, "Invalid experimental feature selection"); return; }
    if (!experiments_storage_valid) { app_update_fail(fd, 409, "Experimental settings storage is invalid"); return; }
    if (experiments.quiet_motors && !next.quiet_motors && !experiments_normal_confirmed(console)) {
        if(!quiet_disable_ready(mqtt,console)){
            app_update_fail(fd,409,"Restoring Normal requires an idle and cold printer with heating targets zero");return;
        }
        char script[CONSOLE_COMMAND_MAX], reason[160];
        if(!control_build_script("motors:normal",mqtt,script,sizeof(script),reason,sizeof(reason))||
           console_start(console,script,reason,sizeof(reason))!=0){
            app_update_fail(fd,409,reason);return;
        }
        pthread_mutex_lock(&console->lock);
        quiet_disable_generation=console->generation;
        pthread_mutex_unlock(&console->lock);
        quiet_disable_state=QUIET_DISABLE_RUNNING;
        motor_mode_requested="normal";motor_mode_connection=telemetry.connections;
        experiments_response(fd,mqtt,console);return;
    }
    if (app_update_active() || !app_update_ready(mqtt, console)) {
        app_update_fail(fd, 409, "Printer must be connected, idle and cold with heating targets zero"); return;
    }
    if (next.nozzle_expansion != experiments.nozzle_expansion && nx_active()) {
        app_update_fail(fd,409,"Wait for the nozzle calibration to finish before changing its opt-in");return;
    }
    if (next.canvas_uds != experiments.canvas_uds && experiments_cli_canvas) {
        app_update_fail(fd, 409, "Canvas UDS is controlled by a command-line option"); return;
    }
    if (next.canvas_uds != experiments.canvas_uds || next.quiet_motors != experiments.quiet_motors || next.bed_drying != experiments.bed_drying || next.nozzle_expansion != experiments.nozzle_expansion ||
        next.mesh_align_settings != experiments.mesh_align_settings) {
        if (experiments.quiet_motors && (!next.quiet_motors || next.canvas_uds != experiments.canvas_uds) &&
            !experiments_normal_confirmed(console)) {
            app_update_fail(fd, 409, "Return the motors to Normal before disabling Quiet or changing the Canvas source"); return;
        }
        int source_changed = next.canvas_uds != experiments.canvas_uds;
        if (!experiments_save(next)) { app_update_fail(fd, 503, "Cannot save experimental settings"); return; }
        if(!next.quiet_motors)quiet_disable_state=QUIET_DISABLE_IDLE;
        if (source_changed) {
            uds_close(&telemetry);
            telemetry.canvas_enabled = experiments.canvas_uds;
            uds_motion_light(&telemetry, mqtt); /* Immediately revoke stale direct-state authority. */
        }
    }
    experiments_response(fd, mqtt, console);
}

#endif
