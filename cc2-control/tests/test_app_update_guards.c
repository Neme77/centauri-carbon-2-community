#define main cc2_original_main
#include "../src/main.c"
#undef main
#include <assert.h>

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
    assert(!pthread_mutex_destroy(&console.lock));
    puts("PASS: update Idle/cold/fresh guards, LAN confirmation, duplicate headers and job confinement");
    return 0;
}
