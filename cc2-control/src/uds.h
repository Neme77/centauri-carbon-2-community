#ifndef CC2_UDS_H
#define CC2_UDS_H
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "mqtt.h"
/* Owned by the HTTP/MQTT event loop; workers never access this cache. */
enum uds_field { U_ET,U_EG,U_BT,U_BG,U_CF,U_HF,U_PF,U_AF,
 U_CRPM,U_HRPM,U_PRPM,U_ARPM,U_SPEED_FACTOR,U_FLOW_FACTOR,
 U_LIVE_SPEED,U_PROGRESS,U_LAYER,U_DURATION,U_TOTAL_DURATION,U_Z_OFFSET,U_FILAMENT_USED,U_CANVAS_CHANNEL,U_FIELDS };
#define UDS_CANVAS_JSON_MAX 4096
typedef struct {
 uint32_t present;
 int cid, status;
 double min_temp, max_temp;
 /* Validated JSON string literals, including quotes; no unbounded raw snapshot. */
 char brand[132], code[132], name[68], type[68], color[16];
} uds_canvas_slot;
typedef struct {
 uint32_t present;
 int did, active_did, active_cid, connected, auto_refill;
 int valid;
 unsigned long revision;
 uds_canvas_slot slots[4];
} uds_canvas;
#define UDS_CONSOLE_LINES 32
#define UDS_CONSOLE_LINE 1024
/* Fixed memory, event-loop owned. A visible viewer renews a short capture lease. */
typedef struct {
 char lines[UDS_CONSOLE_LINES][UDS_CONSOLE_LINE];
 unsigned int next, count, rate_count;
 unsigned long sequence, dropped;
 struct timespec until, rate_start;
} uds_console;

typedef struct {
 uds_console console;
 int canvas_enabled; /* Explicit experimental opt-in; preserved across reconnects. */
 uds_canvas canvas;
 int fd, ready;
 size_t used, sent;
 char input[16384];
 double values[U_FIELDS], eventtime;
 uint32_t present;
 char homed_axes[4];
 int have_homed_axes;
 int have_light, light;
 int have_head_filament,head_filament;
 char filename[512];
 int have_filename;
 /* print_stats.state: printing, paused, complete, cancelled, error or standby. */
 char print_state[16];
 int have_print_state;
 /* bed_mesh.profile_name: the mesh the printer applies now ("" when cleared). */
 char mesh_profile[32];
 int have_mesh_profile;
 /* exclude_object values as raw JSON (array or string, or null). */
 char excluded_objects[8192], current_object[512];
 int have_excluded_objects, have_current_object;
 struct timespec last_rx, retry, last_ping;
 unsigned long messages, connections, disconnects, ignored_messages;
 const char *last_disconnect, *parse_error;
 int last_errno;
 /* Last vendor event, retained across disconnects; never implies an active fault. */
 unsigned long report_sequence;
 int report_code, report_level;
 struct timespec report_received, report_identity;
 char report_message[1024]; /* validated JSON string, or null */
} uds_client;
void uds_console_lease(uds_client *c);
void uds_init(uds_client *c);
void uds_close(uds_client *c);
void uds_tick(uds_client *c,const char *path);
void uds_process(uds_client *c);
int uds_fresh(const uds_client *c);
int uds_value(const uds_client *c,enum uds_field field,double *out);
const char *uds_print_state(const uds_client *c);
const char *uds_mesh_profile(const uds_client *c);
int uds_message(uds_client *c,const char *json,size_t length);
int uds_job_matches(const uds_client *c,const char *filename);
void uds_motion_light(const uds_client *c,mqtt_client *view);
void uds_overlay(const uds_client *c,mqtt_client *view);
int uds_exclude_status(const uds_client *c,char *out,size_t cap);
int uds_canvas_available(const uds_client *c);
int uds_canvas_status(const uds_client *c,char *out,size_t cap);
#endif
