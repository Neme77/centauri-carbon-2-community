#define main cc2_original_main
#include "../src/main.c"
#undef main
#include <assert.h>

static mqtt_client machine;
static console_state command_state;
static const char *authorization = "POST /api/experiments HTTP/1.1\r\nX-CC2-LAN: fixture-confirmation\r\n\r\n";
static void call(const char *headers, const char *body, char *response, size_t capacity) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    experiments_put_response(pair[0], headers, body, strlen(body), &machine, &command_state);
    close(pair[0]); ssize_t n = read(pair[1], response, capacity - 1); assert(n > 0);
    response[n] = 0; close(pair[1]);
}
static void put(const char *text, int status) {
    char response[4096], expected[32]; call(authorization, text, response, sizeof(response));
    snprintf(expected, sizeof(expected), "HTTP/1.1 %d", status); assert(strstr(response, expected));
}
static void overwrite(const char *text) {
    FILE *f = fopen(experiments_path, "w"); assert(f); assert(fputs(text, f) >= 0); assert(!fclose(f));
    assert(!chmod(experiments_path, 0600));
}

int main(void) {
    char temporary[] = "/tmp/cc2-experiments-XXXXXX", path[256], response[4096];
    assert(mkdtemp(temporary)); snprintf(path, sizeof(path), "%s/settings", temporary); experiments_path = path;
    assert(!pthread_mutex_init(&command_state.lock, NULL));
    uds_init(&telemetry); experiments_load();
    assert(experiments_storage_valid && !experiments.canvas_uds && !experiments.quiet_motors);
    machine.connected = machine.registered = machine.have_machine_status = 1;
    machine.machine_status = 1; machine.last_message = time(NULL);
    machine.have_extruder_temp = machine.have_extruder_target = machine.have_bed_temp = machine.have_bed_target = 1;
    machine.extruder_temp = machine.bed_temp = 25;
    snprintf(machine.password, sizeof(machine.password), "fixture-confirmation");
    call("POST / HTTP/1.1\r\n\r\n", "canvas-uds:on", response, sizeof(response)); assert(strstr(response, "401"));
    put("canvas-uds:on trailing", 400); put("unknown:on", 400); assert(access(path, F_OK));
    machine.machine_status = 2; put("canvas-uds:on", 409); machine.machine_status = 1;
    machine.last_message -= 60; put("canvas-uds:on", 409); machine.last_message = time(NULL);
    machine.bed_target = 60; put("canvas-uds:on", 409); machine.bed_target = 0;
    machine.extruder_temp = 60; put("canvas-uds:on", 409); machine.extruder_temp = 25;
    command_state.busy = 1; put("canvas-uds:on", 409); command_state.busy = 0;
    atomic_store(&app_update_busy, 1); put("canvas-uds:on", 409); atomic_store(&app_update_busy, 0);
    assert(access(path, F_OK));
    /* Switch the existing subscription only: direct state becomes unknown and
     * the original MQTT snapshot is not overwritten. No motion command sent. */
    strcpy(machine.canvas_snapshot, "fixture-mqtt"); machine.canvas_snapshot_len = strlen(machine.canvas_snapshot);
    machine.canvas_direct_known = 1; machine.canvas_direct_idle = 1;
    put("canvas-uds:on", 200);
    assert(experiments.canvas_uds && telemetry.canvas_enabled && !machine.canvas_direct_known);
    assert(!strcmp(machine.canvas_snapshot, "fixture-mqtt")); assert(!command_state.command[0]);
    experiments_load(); assert(experiments.canvas_uds && !experiments.quiet_motors);
    put("quiet-motors:on", 200); assert(experiments.canvas_uds && experiments.quiet_motors);
    assert(!motor_mode_requested && !command_state.command[0]); /* Enabling controls moves nothing. */
    assert(!app_update_ready(&machine, &command_state)); /* App restart must not lose a Quiet session. */
    put("quiet-motors:off", 409); put("canvas-uds:off", 409);
    int stream[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, stream)); telemetry.fd = stream[0]; telemetry.connections = 9;
    motor_mode_requested = "normal"; motor_mode_connection = 8;
    put("quiet-motors:off", 409);
    motor_mode_connection = 9;
    char reason[160]; assert(control_build_script("motors:normal", &machine, command_state.command,
                                                 sizeof(command_state.command), reason, sizeof(reason)));
    command_state.completed = 1; command_state.success = 0; put("quiet-motors:off", 409);
    command_state.success = 1;
    assert(app_update_ready(&machine, &command_state));
    put("quiet-motors:off", 200); assert(!experiments.quiet_motors && experiments.canvas_uds);
    put("canvas-uds:off", 200); assert(!experiments.canvas_uds && !telemetry.canvas_enabled);
    close(stream[1]);
    /* Opt-out is authoritative at the protected control endpoint, not just UI. */
    command_state.completed = command_state.success = 0; command_state.command[0] = 0;
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    control_response(pair[0], &command_state, &machine, "motors:quiet", strlen("motors:quiet"));
    close(pair[0]); ssize_t n = read(pair[1], response, sizeof(response)-1); assert(n > 0); response[n] = 0; close(pair[1]);
    assert(strstr(response, "409") && !command_state.command[0]);
    experiments_cli_canvas = 1; put("canvas-uds:on", 409); experiments_cli_canvas = 0;
    /* Storage corruption, foreign changes, broad permissions and links cannot
     * opt in or be silently replaced. Existing unrelated contents survive. */
    overwrite("{\"schema\":1,\"canvas_uds\":true,\"quiet_motors\":false,\"unknown\":true}\n");
    experiments_load(); assert(!experiments_storage_valid && !experiments.canvas_uds);
    put("canvas-uds:on", 409);
    overwrite("{\"schema\":1,\"canvas_uds\":false,\"quiet_motors\":false}\n"); experiments_load();
    assert(experiments_storage_valid);
    assert(!chmod(path, 0644)); experiments_load(); assert(!experiments_storage_valid);
    assert(!chmod(path, 0600)); experiments_load();
    overwrite("{\"schema\":1,\"canvas_uds\":true,\"quiet_motors\":false}\n");
    assert(!experiments_save((experiment_choices){0, 1, 0}));
    assert(!unlink(path)); assert(!symlink("missing-target", path)); experiments_load();
    /* A dangling symlink is not an absent file. */
    assert(!experiments_storage_valid);
    assert(!unlink(path)); assert(!rmdir(temporary));
    pthread_mutex_destroy(&command_state.lock);
    puts("PASS independent opt-ins, persistence, no motion, MQTT fallback, authenticated cold/idle guards and Normal-before-disable");
    return 0;
}
