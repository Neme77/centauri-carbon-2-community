#ifndef CC2_CONNECTIONS_PUBLIC_H
#define CC2_CONNECTIONS_PUBLIC_H
#include "state.h"
static inline int ng_public_active(void) {
  int fd = open(NG_RUN "/operation.lock", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd >= 0) {
    int held = flock(fd, LOCK_SH | LOCK_NB) != 0 &&
               (errno == EWOULDBLOCK || errno == EAGAIN);
    close(fd);
    if (held)
      return 1;
  }
  ng_journal j;
  return ng_journal_load(&j) &&
         (j.phase == NG_APPLYING || j.phase == NG_PENDING ||
          j.phase == NG_SAVING || j.phase == NG_RESTORING);
}
static inline void ng_public_lease(FILE *out, int iface) {
  ng_lease l;
  int have = ng_lease_load(iface, &l) && l.present;
  ng_config config;
  int known = have && ng_effective_config(&config) && config.managed;
  fprintf(out, "{\"known\":%s,\"method\":\"%s\",\"prefix\":%d,\"gateway\":",
          known ? "true" : "false",
          known ? (l.method == NG_STATIC ? "static" : "dhcp") : "unknown",
          have ? l.prefix : 0);
  ng_json_quote(out, have ? l.gateway : "");
  fputs(",\"dns1\":", out);
  ng_json_quote(out, have ? l.dns1 : "");
  fputs(",\"dns2\":", out);
  ng_json_quote(out, have ? l.dns2 : "");
  fputc('}', out);
}
static inline void ng_public_json(FILE *out, int installed, int ready, int busy,
                                  int last_exit) {
  ng_config saved, effective;
  ng_journal j;
  int configured = 0;
  int valid =
      ng_config_load(&saved, &configured) && ng_effective_config(&effective);
  if (!valid) {
    ng_defaults(&saved);
    effective = saved;
  }
  int have = ng_journal_load(&j),
      active = have && (j.phase == NG_APPLYING || j.phase == NG_PENDING ||
                        j.phase == NG_SAVING || j.phase == NG_RESTORING);
  struct stat journal_info;
  if (!have &&
      (!lstat(NG_STATE "/journal.bin", &journal_info) || errno != ENOENT))
    valid = 0;
  static const char *phases[] = {"idle",   "applying",  "pending",
                                 "saving", "restoring", "failed"};
  fprintf(
      out,
      "{\"schema\":1,\"installed\":%s,\"state_valid\":%s,\"configured\":%s,"
      "\"ready\":%s,\"busy\":%s,\"last_exit\":%d,\"phase\":\"%s\",\"config\":",
      installed ? "true" : "false", valid ? "true" : "false",
      configured ? "true" : "false",
      installed && valid && ready ? "true" : "false",
      busy || active || ng_public_active() ? "true" : "false", last_exit,
      have ? phases[j.phase] : "idle");
  ng_config_json(out, &effective);
  fputs(",\"saved\":", out);
  ng_config_json(out, &saved);
  fputs(",\"actual\":{\"wifi\":", out);
  ng_public_lease(out, NG_WIFI);
  fputs(",\"ethernet\":", out);
  ng_public_lease(out, NG_ETHERNET);
  fputs("},\"last_error\":", out);
  ng_json_quote(out, have ? j.error : "");
  fputs(",\"pending\":", out);
  if (active) {
    long long remaining =
        j.deadline > ng_now() ? (j.deadline - ng_now() + 999) / 1000 : 0;
    fprintf(out,
            "{\"phase\":\"%s\",\"seconds\":%lld,\"interface\":\"%s\",\"ip\":",
            phases[j.phase], remaining,
            j.proof_interface == NG_WIFI ? "wifi" : "ethernet");
    ng_json_quote(out, j.expected_ip);
    fputs(",\"token\":", out);
    ng_json_quote(out, j.token);
    fputs(",\"origin\":", out);
    char origin[160];
    origin[0] = 0;
    if (ng_ipv4(j.expected_ip, NULL))
      snprintf(origin, sizeof(origin), "http://%s:%d/#settings/connection",
               j.expected_ip, j.port);
    ng_json_quote(out, origin);
    fputs(",\"previous_origin\":", out);
    origin[0] = 0;
    if (ng_ipv4(j.previous_ip, NULL))
      snprintf(origin, sizeof(origin), "http://%s:%d/#settings/connection",
               j.previous_ip, j.port);
    ng_json_quote(out, origin);
    fputc('}', out);
  } else
    fputs("null", out);
  char networks[16384];
  size_t n;
  ng_json *doc = malloc(sizeof(*doc));
  fputs(",\"networks\":", out);
  if (doc &&
      ng_read_file(NG_RUN "/networks.json", networks, sizeof(networks), &n,
                   1) &&
      ng_json_parse(doc, networks, n) && doc->v[0].type == '{')
    fputs(networks, out);
  else
    fputs("null", out);
  free(doc);
  fputs(",\"diagnostic\":", out);
  char diagnostic[2048];
  if (ng_read_file(NG_RUN "/last-command.txt", diagnostic, sizeof(diagnostic),
                   &n, 1))
    ng_json_quote(out, diagnostic);
  else
    ng_json_quote(out, "");
  fputs(",\"automatic_selection\":false,\"temporary\":false}\n", out);
}
#endif
