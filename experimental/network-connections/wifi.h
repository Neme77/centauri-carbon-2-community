#ifndef CC2_CONNECTIONS_WIFI_H
#define CC2_CONNECTIONS_WIFI_H
#include "state.h"
static inline int ng_wifi_integer(const char *reply, int *number) {
  char *end;
  errno = 0;
  long v = strtol(reply, &end, 10);
  if (end == reply || errno || v < 0 || v > INT_MAX ||
      (*end && strcmp(end, "\n")))
    return 0;
  *number = (int)v;
  return 1;
}
static inline int ng_wifi_id_command(const char *verb, int id) {
  if (id < 0 || id > 4095)
    return 0;
  char command[96];
  snprintf(command, sizeof(command), "%s %d", verb, id);
  return ng_wpa_ok(command);
}
static inline int ng_wifi_snapshot(ng_journal *j) {
  char reply[8192];
  if (!ng_wpa("LIST_NETWORKS", reply, sizeof(reply), 1000))
    return 0;
  char *line = strchr(reply, '\n');
  if (!line)
    return 0;
  j->profile_count = 0;
  while (*++line) {
    char *end = strchr(line, '\n');
    if (!end)
      return 0;
    *end = 0;
    char *tab = strchr(line, '\t');
    if (!tab)
      return 0;
    *tab = 0;
    int id;
    if (!ng_wifi_integer(line, &id) || id > 4095 || j->profile_count >= 32)
      return 0;
    char *flags = tab + 1;
    for (int i = 0; i < 2; i++) {
      flags = strchr(flags, '\t');
      if (!flags)
        return 0;
      flags++;
    }
    ng_profile *p = &j->profiles[j->profile_count++];
    p->id = id;
    p->enabled = strstr(flags, "[DISABLED]") == NULL;
    char command[80], priority[96];
    snprintf(command, sizeof(command), "GET_NETWORK %d priority", id);
    if (!ng_wpa(command, priority, sizeof(priority), 1000) ||
        !ng_wifi_integer(priority, &p->priority))
      return 0;
    line = end;
  }
  j->previous_network = ng_current_network();
  return j->previous_network >= 0 || !j->previous.link[NG_WIFI].enabled;
}
static inline int ng_wifi_priority(int id, int priority) {
  char command[96];
  if (id < 0 || id > 4095 || priority < 0)
    return 0;
  snprintf(command, sizeof(command), "SET_NETWORK %d priority %d", id,
           priority);
  return ng_wpa_ok(command);
}
static inline int ng_wifi_restore(const ng_journal *j, int save) {
  if (j->created_network >= 0 &&
      !ng_wifi_id_command("REMOVE_NETWORK", j->created_network))
    return 0;
  int ok = j->previous_network >= 0
               ? ng_wifi_id_command("SELECT_NETWORK", j->previous_network)
               : ng_wpa_ok("DISCONNECT");
  for (int i = 0; i < j->profile_count; i++) {
    const ng_profile *p = &j->profiles[i];
    if (!ng_wifi_priority(p->id, p->priority) ||
        !ng_wifi_id_command(p->enabled ? "ENABLE_NETWORK" : "DISABLE_NETWORK",
                            p->id))
      ok = 0;
  }
  if (save && !ng_wpa_ok("SAVE_CONFIG"))
    ok = 0;
  return ok;
}
static inline int ng_wifi_choose(ng_journal *j, const ng_request *r) {
  int max = 0;
  for (int i = 0; i < j->profile_count; i++)
    if (j->profiles[i].priority > max)
      max = j->profiles[i].priority;
  if (max == INT_MAX)
    return 0;
  int id = r->network_id;
  j->target_network = id;
  if (r->op == NG_WIFI_CONNECT) {
    char answer[80];
    if (!ng_wpa("ADD_NETWORK", answer, sizeof(answer), 1000) ||
        !ng_wifi_integer(answer, &id) || id > 4095)
      return 0;
    /* Publish the created ID before any further mutation; rollback never needs
     * to read or duplicate a stored password. */
    j->created_network = id;
    j->target_network = id;
    if (!ng_journal_save(j)) {
      ng_wifi_id_command("REMOVE_NETWORK", id);
      return 0;
    }
    char hex[65];
    for (size_t i = 0; i < strlen(r->ssid); i++)
      snprintf(hex + i * 2, 3, "%02x", (unsigned char)r->ssid[i]);
    char command[512];
    snprintf(command, sizeof(command), "SET_NETWORK %d ssid %s", id, hex);
    if (!ng_wpa_ok(command))
      return 0;
    snprintf(command, sizeof(command), "SET_NETWORK %d key_mgmt %s", id,
             !strcmp(r->security, "open") ? "NONE" : "WPA-PSK");
    if (!ng_wpa_ok(command))
      return 0;
    if (!strcmp(r->security, "wpa2")) {
      char encoded[140];
      size_t at = 0;
      if (strlen(r->password) == 64) {
        strcpy(encoded, r->password);
      } else {
        encoded[at++] = '"';
        for (const char *p = r->password; *p; p++) {
          if (*p == '"' || *p == '\\')
            encoded[at++] = '\\';
          encoded[at++] = *p;
        }
        encoded[at++] = '"';
        encoded[at] = 0;
      }
      snprintf(command, sizeof(command), "SET_NETWORK %d psk %s", id, encoded);
      int ok = ng_wpa_ok(command);
      ng_erase(encoded, sizeof(encoded));
      ng_erase(command, sizeof(command));
      if (!ok)
        return 0;
    }
    snprintf(command, sizeof(command), "SET_NETWORK %d scan_ssid 1", id);
    if (!ng_wpa_ok(command))
      return 0;
  } else {
    int found = 0;
    for (int i = 0; i < j->profile_count; i++)
      if (j->profiles[i].id == id)
        found = 1;
    if (!found)
      return 0;
  }
  if (!ng_wifi_priority(id, max + 1) ||
      !ng_wifi_id_command("SELECT_NETWORK", id))
    return 0;
  j->target_network = id;
  return ng_journal_save(j);
}
static inline int ng_wifi_commit(const ng_journal *j) {
  /* SELECT_NETWORK temporarily disables other profiles. Restore their original
   * enable flags, retaining the selected network's higher native priority. */
  for (int i = 0; i < j->profile_count; i++) {
    const ng_profile *p = &j->profiles[i];
    if (p->id == j->target_network)
      continue;
    if (!ng_wifi_id_command(p->enabled ? "ENABLE_NETWORK" : "DISABLE_NETWORK",
                            p->id))
      return 0;
  }
  return ng_wpa_ok("SAVE_CONFIG");
}
static inline int ng_wifi_unescape(const char *s, char out[33]) {
  size_t n = 0;
  while (*s) {
    unsigned c = (unsigned char)*s++;
    if (c == '\\') {
      if (*s == 'x') {
        s++;
        if (!s[0] || !s[1])
          return 0;
        unsigned v = 0;
        for (int i = 0; i < 2; i++) {
          unsigned d = (unsigned char)*s++;
          if (d >= '0' && d <= '9')
            d -= '0';
          else if (d >= 'a' && d <= 'f')
            d = d - 'a' + 10;
          else if (d >= 'A' && d <= 'F')
            d = d - 'A' + 10;
          else
            return 0;
          v = (v << 4) | d;
        }
        c = v;
      } else if (*s == '\\' || *s == '"')
        c = (unsigned char)*s++;
      else
        return 0;
    }
    if (c < 32 || c == 127 || n >= 32)
      return 0;
    out[n++] = (char)c;
  }
  out[n] = 0;
  return n > 0;
}
static inline int ng_wifi_list(FILE *out, int scan) {
  char reply[16384];
  if (scan) {
    if (!ng_wpa_ok("SCAN"))
      return 0;
    poll(NULL, 0, 700);
  }
  if (!ng_wpa(scan ? "SCAN_RESULTS" : "LIST_NETWORKS", reply, sizeof(reply),
              1000))
    return 0;
  char *line = strchr(reply, '\n');
  if (!line)
    return 0;
  fputc('[', out);
  int count = 0;
  while (*++line && count < 96) {
    char *end = strchr(line, '\n');
    if (!end)
      return 0;
    *end = 0;
    char *field[5];
    field[0] = line;
    int fields = scan ? 5 : 4;
    for (int i = 1; i < fields; i++) {
      char *tab = strchr(field[i - 1], '\t');
      if (!tab)
        return 0;
      *tab = 0;
      field[i] = tab + 1;
    }
    char ssid[33];
    int valid = ng_wifi_unescape(field[scan ? 4 : 1], ssid);
    if (valid) {
      if (count++)
        fputc(',', out);
      fputs("{\"ssid\":", out);
      ng_json_quote(out, ssid);
      if (scan) {
        char *last;
        long signal = strtol(field[2], &last, 10);
        if (*last || signal < -127 || signal > 0)
          signal = -127;
        const char *flags = field[3];
        int psk = strstr(flags, "PSK") != NULL,
            unsupported =
                strstr(flags, "WEP") != NULL ||
                (!psk && (strstr(flags, "EAP") || strstr(flags, "SAE")));
        fprintf(out, ",\"signal\":%ld,\"security\":\"%s\",\"compatible\":%s",
                signal, psk ? "wpa2" : "open", unsupported ? "false" : "true");
      } else {
        int id;
        if (!ng_wifi_integer(field[0], &id) || id > 4095)
          return 0;
        fprintf(out, ",\"network_id\":%d,\"current\":%s", id,
                strstr(field[3], "[CURRENT]") ? "true" : "false");
      }
      fputc('}', out);
    }
    line = end;
  }
  fputc(']', out);
  return 1;
}
#endif
