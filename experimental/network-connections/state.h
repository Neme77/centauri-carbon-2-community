#ifndef CC2_CONNECTIONS_STATE_H
#define CC2_CONNECTIONS_STATE_H
#include "io.h"
enum {
  NG_IDLE = 0,
  NG_APPLYING = 1,
  NG_PENDING = 2,
  NG_SAVING = 3,
  NG_RESTORING = 4,
  NG_FAILED = 5
};
typedef struct {
  int id, enabled, priority;
  char ssid[33];
} ng_profile;
typedef struct {
  uint32_t schema;
  int phase, op, worker, watcher, port, proof_interface;
  long long deadline;
  unsigned long long worker_start;
  char boot[40], token[33], previous_ip[16], expected_ip[16], error[64];
  ng_config previous, desired;
  int previous_network, created_network, target_network, profile_count;
  int created_priority;
  char target_ssid[33];
  ng_profile profiles[32];
  char resolver[2048];
  int resolver_size;
} ng_journal;
typedef struct {
  uint32_t schema;
  int present, prefix, method;
  char ip[16], gateway[16], dns1[16], dns2[16], boot[40];
} ng_lease;
static inline int ng_boot_id(char out[40]) {
  size_t n;
  if (!ng_read_file("/proc/sys/kernel/random/boot_id", out, 40, &n, 0) ||
      n != 37 || out[36] != '\n')
    return 0;
  out[36] = 0;
  for (int i = 0; i < 36; i++)
    if (!(isxdigit((unsigned char)out[i]) || out[i] == '-'))
      return 0;
  return 1;
}
static inline int ng_config_load(ng_config *out, int *configured) {
  char path[512];
  snprintf(path, sizeof(path), "%s/config.bin", NG_STATE);
  struct stat s;
  if (lstat(path, &s)) {
    if (errno != ENOENT)
      return 0;
    ng_defaults(out);
    *configured = 0;
    return 1;
  }
  *configured = 1;
  return ng_blob_read(NG_STATE, "config.bin", out, sizeof(*out)) &&
         ng_config_valid(out);
}
static inline int ng_journal_valid(const ng_journal *j) {
  if (j->schema != NG_SCHEMA || j->phase < NG_IDLE || j->phase > NG_FAILED ||
      j->port < 1 || j->port > 65535 || j->proof_interface < 0 ||
      j->proof_interface > 1 || !ng_config_valid(&j->previous) ||
      !ng_config_valid(&j->desired) || !memchr(j->token, 0, sizeof(j->token)) ||
      !ng_token_valid(j->token) || j->profile_count < 0 ||
      j->profile_count > 32)
    return 0;
  if (!memchr(j->boot, 0, sizeof(j->boot)) ||
      !memchr(j->error, 0, sizeof(j->error)) ||
      !memchr(j->expected_ip, 0, sizeof(j->expected_ip)) ||
      !memchr(j->previous_ip, 0, sizeof(j->previous_ip)))
    return 0;
  if (j->resolver_size < 0 || j->resolver_size >= (int)sizeof(j->resolver))
    return 0;
  if (!memchr(j->target_ssid, 0, sizeof(j->target_ssid)))
    return 0;
  for (int i = 0; i < j->profile_count; i++)
    if (j->profiles[i].id < 0 || j->profiles[i].id > 4095 ||
        (j->profiles[i].enabled != 0 && j->profiles[i].enabled != 1) ||
        !memchr(j->profiles[i].ssid, 0, sizeof(j->profiles[i].ssid)))
      return 0;
  return 1;
}
static inline int ng_journal_load(ng_journal *j) {
  return ng_blob_read(NG_STATE, "journal.bin", j, sizeof(*j)) &&
         ng_journal_valid(j);
}
static inline int ng_journal_save(const ng_journal *j) {
  return ng_journal_valid(j) &&
         ng_blob_write(NG_STATE, "journal.bin", j, sizeof(*j));
}
static inline int ng_effective_config(ng_config *c) {
  ng_journal j;
  if (ng_journal_load(&j) && (j.phase == NG_APPLYING || j.phase == NG_PENDING ||
                              j.phase == NG_SAVING)) {
    *c = j.desired;
    return 1;
  }
  if (ng_journal_load(&j) && j.phase == NG_RESTORING) {
    *c = j.previous;
    return 1;
  }
  if (ng_blob_read(NG_RUN, "boot-config.bin", c, sizeof(*c)) &&
      ng_config_valid(c))
    return 1;
  int configured;
  return ng_config_load(c, &configured);
}
static inline int ng_lease_valid(const ng_lease *l) {
  if (l->schema != NG_SCHEMA ||
      (l->method != NG_DHCP && l->method != NG_STATIC) ||
      (l->present != 0 && l->present != 1) ||
      !memchr(l->boot, 0, sizeof(l->boot)))
    return 0;
  ng_iface i = {.enabled = 1, .method = NG_STATIC, .prefix = l->prefix};
  if (!memchr(l->ip, 0, 16) || !memchr(l->gateway, 0, 16) ||
      !memchr(l->dns1, 0, 16) || !memchr(l->dns2, 0, 16))
    return 0;
  strcpy(i.ip, l->ip);
  strcpy(i.gateway, l->gateway);
  strcpy(i.dns1, l->dns1);
  strcpy(i.dns2, l->dns2);
  return !l->present || ng_iface_valid(&i);
}
static inline int ng_lease_load(int iface, ng_lease *l) {
  char boot[40];
  return ng_blob_read(NG_RUN,
                      iface == NG_WIFI ? "wifi.lease" : "ethernet.lease", l,
                      sizeof(*l)) &&
         ng_lease_valid(l) && ng_boot_id(boot) && !strcmp(boot, l->boot);
}
static inline int ng_lease_save(int iface, const ng_lease *l) {
  return ng_lease_valid(l) &&
         ng_blob_write(NG_RUN,
                       iface == NG_WIFI ? "wifi.lease" : "ethernet.lease", l,
                       sizeof(*l));
}
static inline int ng_current_network(void) {
  char answer[4096], value[40];
  if (!ng_wpa("STATUS", answer, sizeof(answer), 500) ||
      !ng_wpa_field(answer, "id", value, sizeof(value)))
    return -1;
  char *end;
  long id = strtol(value, &end, 10);
  return *end || id < 0 || id > 4095 ? -1 : (int)id;
}
static inline int ng_wifi_connected(int expected) {
  char answer[4096], state[40];
  if (!ng_wpa("STATUS", answer, sizeof(answer), 500) ||
      !ng_wpa_field(answer, "wpa_state", state, sizeof(state)) ||
      strcmp(state, "COMPLETED"))
    return 0;
  return expected < 0 || ng_current_network() == expected;
}
static inline void ng_event(const char *event) {
  if (!ng_directory(NG_STATE))
    return;
  char path[512];
  snprintf(path, sizeof(path), "%s/action.log", NG_STATE);
  struct stat s;
  if (!lstat(path, &s)) {
    if (!S_ISREG(s.st_mode) || s.st_uid != ng_owner() ||
        (s.st_mode & 0777) != 0600 || s.st_nlink != 1)
      return;
    if (s.st_size > 16384) {
      char old[512];
      snprintf(old, sizeof(old), "%s/action.previous.log", NG_STATE);
      if (rename(path, old))
        return;
    }
  }
  int fd =
      open(path, O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (fd < 0)
    return;
  char line[192];
  int n = snprintf(line, sizeof(line), "uptime_ms=%lld event=%s\n", ng_now(),
                   event);
  if (n > 0 && (size_t)n < sizeof(line))
    ng_write_all(fd, line, (size_t)n);
  close(fd);
}
#endif
