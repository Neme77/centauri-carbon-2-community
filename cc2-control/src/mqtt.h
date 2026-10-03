#ifndef CC2_MQTT_H
#define CC2_MQTT_H

#include <stddef.h>
#include <time.h>

typedef struct {
    int fd;
    unsigned char input[16384];
    size_t input_len;
    size_t discard_remaining;
    unsigned long oversized_packets, oversized_snapshots;
    time_t last_connect_attempt;
    time_t last_message;
    time_t last_ping;
    time_t last_app_ping; /* CC2 application-level heartbeat, separate from MQTT PINGREQ */
    unsigned long messages;
    unsigned long info_responses;
    unsigned long received_publishes, skipped_requests, skipped_request_bytes;
    int connected;
    int registered;
    int register_sent;
    int snapshot_sent;
    time_t last_registration_request;
    time_t last_snapshot_request;
    unsigned int registration_attempts;
    unsigned int snapshot_request_attempts;
    int have_extruder_temp, have_extruder_target;
    int have_bed_temp, have_bed_target;
    int have_chamber_temp;
    int have_controller_fan, have_heater_fan, have_part_fan, have_aux_fan, have_box_fan;
    int have_machine_status, have_progress, have_sub_status;
    int have_position, have_speed, have_speed_mode;
    int machine_status, progress, sub_status, sub_status_reason;
    int print_enabled, current_layer, total_layers, have_total_layers;
    int filament_detect_enabled, filament_detected;
    int camera, u_disk, led_status;
    long print_duration, remaining_time, total_duration;
    double extruder_temp, extruder_target;
    double bed_temp, bed_target;
    double chamber_temp;
    double controller_fan, heater_fan, part_fan, aux_fan, box_fan;
    double x, y, z, move_speed;
    int speed_mode;
    char filename[256];
    char print_state[64];
    char uuid[128];
    char homed_axes[16];
    char username[64];
    char password[128];
    char topic[160];
    char serial[64];
    char config_path[256];
    int serial_persisted;
    time_t last_serial_discovery;
    unsigned int serial_discovery_attempts;
    char client_id[64];
    char snapshot[12288];
    size_t snapshot_len;
    char diagnostic[32768];
    size_t diagnostic_len;
    char canvas_snapshot[8192];
    size_t canvas_snapshot_len;
    time_t last_canvas_request;
    unsigned int canvas_request_attempts;
    int canvas_discovery_complete;
    int canvas_active_tray_id;
    int have_canvas_active_tray;
} mqtt_client;

void mqtt_init(mqtt_client *client);
int mqtt_load_config(mqtt_client *client, const char *path);
int mqtt_connect_local(mqtt_client *client);
void mqtt_close(mqtt_client *client);
int mqtt_process(mqtt_client *client);
void mqtt_tick(mqtt_client *client);
int mqtt_request_canvas(mqtt_client *client);
int mqtt_start_print(mqtt_client *client, const char *storage_media, const char *filename,
                     const int *tools, const int *trays, size_t slot_count,
                     char print_layout, int bedlevel_force);

#endif
