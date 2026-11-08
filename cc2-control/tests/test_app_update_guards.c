#ifndef CC2_CONTROL_VERSION
#define CC2_CONTROL_VERSION "1.2.27"
#endif
#define main cc2_original_main
#include "../src/main.c"
#undef main
#include <assert.h>

static void test_update_progress(console_state *console) {
    char root[] = "/tmp/cc2-update-progress-XXXXXX";
    const char *token = "0123456789abcdef0123456789abcdef";
    char job[600], current[600], state[600], log[600], verified[600], progress[512] = "";
    const char *original = app_update_root;
    assert(mkdtemp(root));
    app_update_root = root;
    snprintf(job, sizeof(job), "%s/job-%s", root, token);
    snprintf(current, sizeof(current), "%s/current", root);
    assert(!mkdir(job, 0700));
    assert(app_update_write(current, "0123456789abcdef0123456789abcdef\n"));
    assert(app_update_path(log, sizeof(log), token, "install.log"));
    assert(app_update_path(verified, sizeof(verified), token, "verified.json"));
    assert(app_update_path(state, sizeof(state), token, "status.json"));
    assert(app_update_write(log, "INSTALL_PHASE=rfid-preflight\n"
                                 "STOP: secret-token\n"
                                 "INSTALLER_EXIT=1 LAST_PHASE=rfid-preflight\n"));
    app_update_progress_log(token, progress, sizeof(progress));
    assert(!strcmp(progress, "INSTALL_PHASE=rfid-preflight\\nINSTALLER_EXIT=1 LAST_PHASE=rfid-preflight\\n"));
    assert(app_update_version_reached(CC2_CONTROL_VERSION));
    assert(!app_update_version_reached("999999.0.0"));
    assert(!app_update_version_reached("invalid"));
    assert(app_update_write(verified, "{\"version\":\"" CC2_CONTROL_VERSION "\"}\n"));
    assert(app_update_phase(token, "failed", 0));
    int pair[2];
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    app_update_status_response(pair[0], console);
    assert(!close(pair[0]));
    char response[4096], stored[256];
    size_t used = 0;
    ssize_t count;
    while ((count = read(pair[1], response + used, sizeof(response) - 1 - used)) > 0)
        used += (size_t)count;
    assert(count == 0 && !close(pair[1]));
    response[used] = 0;
    assert(strstr(response, "\"phase\":\"reconciled\"") && strstr(response, "\"progress_log\":\"\""));
    assert(!strstr(response, token) && !strstr(response, "secret-token"));
    assert(app_update_read(state, stored, sizeof(stored)) && strstr(stored, "\"phase\":\"reconciled\""));
    assert(!unlink(current) && !unlink(state) && !unlink(log) && !unlink(verified));
    assert(!rmdir(job) && !rmdir(root));
    app_update_root = original;
}

int main(void) {
    mqtt_client mqtt = {0}; console_state console = {0};
    assert(!pthread_mutex_init(&console.lock, NULL));
    assert(!app_update_ready(&mqtt, &console));
    mqtt.connected = mqtt.registered = mqtt.have_machine_status = 1;
    mqtt.machine_status = 1; mqtt.last_message = time(NULL);
    mqtt.have_extruder_temp = mqtt.have_extruder_target = mqtt.have_bed_temp = mqtt.have_bed_target = 1;
    mqtt.extruder_temp = mqtt.bed_temp = 24;
    assert(app_update_ready(&mqtt, &console));
    mqtt.extruder_temp = 51; assert(!app_update_ready(&mqtt, &console)); mqtt.extruder_temp = 24;
    mqtt.bed_target = 60; assert(!app_update_ready(&mqtt, &console)); mqtt.bed_target = 0;
    mqtt.machine_status = 2; assert(!app_update_ready(&mqtt, &console)); mqtt.machine_status = 1;
    mqtt.last_message = time(NULL) - 16; assert(!app_update_ready(&mqtt, &console)); mqtt.last_message = time(NULL);
    mqtt.extruder_temp = NAN; assert(!app_update_ready(&mqtt, &console)); mqtt.extruder_temp = 24;
    console.busy = 1; assert(!app_update_ready(&mqtt, &console)); console.busy = 0;
    mqtt.last_message = time(NULL) + 30; assert(!app_update_ready(&mqtt, &console)); mqtt.last_message = time(NULL);
    snprintf(mqtt.password, sizeof(mqtt.password), "fixture-confirmation");
    const char *good = "POST /api/app-update/install HTTP/1.1\r\nX-CC2-LAN: fixture-confirmation\r\n\r\n";
    const char *duplicate = "POST / HTTP/1.1\r\nX-CC2-LAN: fixture-confirmation\r\nX-CC2-LAN: fixture-confirmation\r\n\r\n";
    assert(app_update_authorized(good, &mqtt));
    assert(!app_update_authorized(duplicate, &mqtt));
    assert(!app_update_authorized("POST / HTTP/1.1\r\n\r\n", &mqtt));
    assert(app_update_authorized(good, &mqtt));
    /* Verifying browser storage uses the same rate-limited authorization;
     * it must not accept duplicate headers or an incorrect candidate. */
    assert(app_update_authorized("POST /api/access/verify HTTP/1.1\r\nX-CC2-LAN: fixture-confirmation\r\n\r\n", &mqtt));
    assert(!app_update_authorized("POST /api/access/verify HTTP/1.1\r\nX-CC2-LAN: wrong\r\n\r\n", &mqtt));
    assert(app_update_authorized(good, &mqtt));
    assert(app_update_token_valid("0123456789abcdef0123456789abcdef"));
    assert(!app_update_token_valid("../escape"));
    assert(!app_update_token_valid("0123456789abcdef0123456789abcdeG"));
    char path[600]; assert(!app_update_path(path, sizeof(path), "../escape", "package.cc2update"));
    assert(!app_update_active()); atomic_store(&app_update_busy, 1); assert(app_update_active()); atomic_store(&app_update_busy, 0);
    test_update_progress(&console);
    assert(!pthread_mutex_destroy(&console.lock));
    puts("PASS: update Idle/cold/fresh guards, LAN confirmation, duplicate headers and job confinement");
    return 0;
}
