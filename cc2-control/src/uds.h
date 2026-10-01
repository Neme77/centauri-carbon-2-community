#ifndef CC2_UDS_H
#define CC2_UDS_H
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "mqtt.h"
/* Owned by the HTTP/MQTT event loop; workers never access this cache. */
enum uds_field { U_ET,U_EG,U_BT,U_BG,U_CF,U_HF,U_PF,U_AF,
 U_CRPM,U_HRPM,U_PRPM,U_ARPM,U_SPEED_FACTOR,U_FLOW_FACTOR,
 U_LIVE_SPEED,U_PROGRESS,U_LAYER,U_DURATION,U_TOTAL_DURATION,U_FIELDS };
typedef struct {
 int fd, ready;
 size_t used, sent;
 char input[16384];
 double values[U_FIELDS], eventtime;
 uint32_t present;
 char filename[512];
 int have_filename;
 struct timespec last_rx, retry, last_ping;
 unsigned long messages;
} uds_client;
void uds_init(uds_client *c);
void uds_close(uds_client *c);
void uds_tick(uds_client *c,const char *path);
void uds_process(uds_client *c);
int uds_fresh(const uds_client *c);
int uds_value(const uds_client *c,enum uds_field field,double *out);
int uds_message(uds_client *c,const char *json,size_t length);
void uds_overlay(const uds_client *c,mqtt_client *view);
#endif
