#ifndef CC2_CONNECTIONS_BOOTSTRAP_H
#define CC2_CONNECTIONS_BOOTSTRAP_H
#include "dhcp.h"
#include "profile-pins.h"
static inline int ng_check_profile(const char *kind) {
  char *args[] = {"sh", NG_COMPONENT "/profile.sh", (char *)kind, NULL};
  char diagnostic[1024] = {0};
  int ok = ng_exec("/bin/sh", args, diagnostic, sizeof(diagnostic), 15);
  if (!ok && diagnostic[0])
    ng_atomic(NG_RUN, "last-command.txt", diagnostic, strlen(diagnostic));
  return ok;
}
static inline int ng_sysctl_iface(int iface, const char *family,
                                  const char *key, const char *value) {
  char path[256];
  snprintf(path, sizeof(path), "/proc/sys/net/%s/conf/%s/%s", family,
           ng_name(iface), key);
  int fd = open(path, O_WRONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0)
    return 0;
  int ok = ng_write_all(fd, value, strlen(value));
  close(fd);
  return ok;
}
static inline int ng_carrier(int iface) {
  char path[256], text[16];
  size_t size;
  snprintf(path, sizeof(path), "/sys/class/net/%s/carrier", ng_name(iface));
  return ng_read_file(path, text, sizeof(text), &size, 0) &&
         !strcmp(text, "1\n");
}
static inline int ng_qualified_usb(void) {
  DIR *d = opendir("/sys/bus/usb/devices");
  if (!d)
    return 0;
  struct dirent *entry;
  int matches = 0;
  while ((entry = readdir(d))) {
    if (entry->d_name[0] == '.')
      continue;
    char path[512], v[16], p[16], b[16];
    size_t size;
    snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/idVendor",
             entry->d_name);
    if (!ng_read_file(path, v, sizeof(v), &size, 0) || strcmp(v, "0bda\n"))
      continue;
    snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/idProduct",
             entry->d_name);
    if (!ng_read_file(path, p, sizeof(p), &size, 0) || strcmp(p, "8153\n"))
      continue;
    snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/bcdDevice",
             entry->d_name);
    if (ng_read_file(path, b, sizeof(b), &size, 0) && !strcmp(b, "31fd\n"))
      matches++;
  }
  closedir(d);
  return matches == 1;
}
static inline int ng_load_drivers(void) {
  if (!ng_qualified_usb() || !ng_check_profile("ethernet"))
    return 0;
  char digest[65];
  if (!ng_hash_file(NG_COMPONENT "/cc2_ue300_mii.ko", digest) ||
      strcmp(digest, NG_CC2_UE300_MII_KO_SHA) ||
      !ng_hash_file(NG_COMPONENT "/cc2_ue300_r8152.ko", digest) ||
      strcmp(digest, NG_CC2_UE300_R8152_KO_SHA))
    return 0;
  if (access("/sys/module/cc2_ue300_r8152", F_OK)) {
    if (!ng_command("insmod", NULL, 0, 5,
                    NG_COMPONENT "/cc2_ue300_abi_probe.ko", NULL))
      return 0;
    if (!ng_command("rmmod", NULL, 0, 5, "cc2_ue300_abi_probe", NULL))
      return 0;
    if (access("/sys/module/cc2_ue300_mii", F_OK) &&
        !ng_command("insmod", NULL, 0, 5, NG_COMPONENT "/cc2_ue300_mii.ko",
                    NULL))
      return 0;
    if (!ng_command("insmod", NULL, 0, 5, NG_COMPONENT "/cc2_ue300_r8152.ko",
                    NULL))
      return 0;
  }
  long long deadline = ng_now() + 5000;
  while (!if_nametoindex("cc2lan0") && ng_now() < deadline)
    poll(NULL, 0, 100);
  return if_nametoindex("cc2lan0") != 0;
}
static inline int ng_runtime_ready(void) {
  char *args[] = {"sh", NG_COMPONENT "/activate.sh", "ready", NULL};
  return ng_exec("/bin/sh", args, NULL, 0, 6);
}
static inline int ng_runtime_enable(const ng_journal *j) {
  if (ng_runtime_ready())
    return 1;
  char port[16];
  snprintf(port, sizeof(port), "%d", j->port);
  char *args[] = {
      "sh", NG_COMPONENT "/activate.sh", "start", (char *)j->token, port, NULL};
  char diagnostic[2048] = {0};
  int ok = ng_exec("/bin/sh", args, diagnostic, sizeof(diagnostic), 65);
  if (!ok && diagnostic[0])
    ng_atomic(NG_RUN, "last-command.txt", diagnostic, strlen(diagnostic));
  return ok;
}
static inline int ng_runtime_repair(const ng_journal *j) {
  char *args[] = {"sh", NG_COMPONENT "/activate.sh", "repair", (char *)j->token,
                  NULL};
  return ng_exec("/bin/sh", args, NULL, 0, 20);
}
static inline int ng_iface_ready(int iface, int network) {
  char ip[16];
  ng_lease lease;
  if (!ng_live_ip(iface, ip) || !ng_lease_load(iface, &lease) ||
      !lease.present || strcmp(ip, lease.ip))
    return 0;
  if (iface == NG_WIFI)
    return ng_wifi_connected(network);
  return ng_carrier(NG_ETHERNET);
}
static inline int ng_prepare_link(int iface, const ng_iface *config,
                                  int restart, int required) {
  if (!config->enabled)
    return 1;
  if (iface == NG_ETHERNET &&
      (!ng_load_drivers() ||
       !ng_sysctl_iface(iface, "ipv6", "disable_ipv6", "1\n")))
    return 0;
  if (!ng_command("ip", NULL, 0, 2, "link", "set", "dev", ng_name(iface), "up",
                  NULL) ||
      !ng_sysctl_iface(iface, "ipv4", "arp_ignore", "1\n"))
    return 0;
  if (iface == NG_WIFI && !ng_wifi_connected(-1) && !ng_wpa_ok("RECONNECT"))
    return 0;
  ng_iface operating = *config;
  if (iface == NG_WIFI && config->method == NG_STATIC && config->ssid[0]) {
    long long deadline = ng_now() + (required ? 25000 : 0);
    while (!ng_wifi_connected(-1) && ng_now() < deadline)
      poll(NULL, 0, 200);
    char ssid[33];
    if (!ng_wifi_connected(-1) || !ng_wifi_ssid(ssid))
      return 0;
    if (strcmp(ssid, config->ssid)) {
      int enabled = operating.enabled;
      memset(&operating, 0, sizeof(operating));
      operating.enabled = enabled;
      config = &operating;
    }
  }
  if (config->method == NG_STATIC) {
    ng_lease lease;
    if (!ng_dhcp_stop(iface) || !ng_static_lease(config, &lease) ||
        !ng_lease_save(iface, &lease) || !ng_apply_address(iface, &lease))
      return 0;
  } else if (!ng_dhcp_start(iface, restart))
    return 0;
  return 1;
}
static inline int ng_disable_link(int iface) {
  if (!ng_dhcp_stop(iface))
    return 0;
  if (!if_nametoindex(ng_name(iface)))
    return 1;
  if (iface == NG_WIFI && !ng_wpa_ok("DISCONNECT"))
    return 0;
  if (!ng_command("ip", NULL, 0, 2, "link", "set", "dev", ng_name(iface),
                  "down", NULL) ||
      !ng_command("ip", NULL, 0, 2, "-4", "addr", "flush", "dev",
                  ng_name(iface), "scope", "global", NULL))
    return 0;
  ng_lease empty;
  memset(&empty, 0, sizeof(empty));
  empty.schema = NG_SCHEMA;
  return ng_boot_id(empty.boot) && ng_lease_save(iface, &empty);
}
static inline int ng_apply_config(const ng_config *config, const ng_journal *j,
                                  int repair, int boot) {
  if (!ng_config_valid(config) || !ng_directory(NG_RUN))
    return 0;
  int primary = config->selected;
  int restart_wifi = j && (j->op == NG_WIFI_CONNECT || j->op == NG_WIFI_SAVED ||
                           (j->op == NG_IP && j->proof_interface == NG_WIFI));
  for (int order = 0; order < 2; order++) {
    int iface = order ? 1 - primary : primary;
    int required = iface == primary || (j && j->proof_interface == iface &&
                                        config->link[iface].enabled);
    if (!ng_prepare_link(iface, &config->link[iface],
                         iface == NG_WIFI && restart_wifi, required)) {
      if (required)
        return 0;
      ng_event("secondary_connection_unavailable");
    }
  }
  long long deadline = ng_now() + (boot ? 45000 : 30000);
  int proof = j ? j->proof_interface : primary;
  int wanted =
      j && (j->op == NG_WIFI_CONNECT || j->op == NG_WIFI_SAVED)
          ? (j->phase == NG_RESTORING ? j->previous_network : j->target_network)
          : -1;
  while (ng_now() < deadline) {
    if (ng_iface_ready(primary, primary == NG_WIFI ? wanted : -1) &&
        (!config->link[proof].enabled ||
         ng_iface_ready(proof, proof == NG_WIFI ? wanted : -1)))
      break;
    poll(NULL, 0, 250);
  }
  if (!ng_iface_ready(primary, primary == NG_WIFI ? wanted : -1))
    return 0;
  if (config->link[proof].enabled &&
      !ng_iface_ready(proof, proof == NG_WIFI ? wanted : -1))
    return 0;
  int lock = ng_lock("lease.lock", 4);
  if (lock < 0)
    return 0;
  int ok = ng_configure_routes(config) && ng_resolver_selected(config) &&
           ng_set_mode(config) && ng_policy_refresh();
  close(lock);
  if (!ok)
    return 0;
  /* The working previous interface is disabled only after the replacement has
   * an address, source route and native selection. */
  for (int i = 0; i < 2; i++)
    if (!config->link[i].enabled && !ng_disable_link(i))
      return 0;
  if (repair && j && !ng_runtime_repair(j))
    return 0;
  return 1;
}
static inline int ng_release_owned_routes(void) {
  char marker[4];
  size_t n;
  if (!ng_read_file(NG_RUN "/routing.owned", marker, sizeof(marker), &n, 1))
    return access(NG_RUN "/routing.owned", F_OK) != 0 && errno == ENOENT;
  if (strcmp(marker, "1\n"))
    return 0;
  char rules[8192];
  if (!ng_command("ip", rules, sizeof(rules), 2, "-4", "rule", "show", NULL))
    return 0;
  for (char *p = rules; *p;) {
    char *end = strchr(p, '\n');
    if (end)
      *end = 0;
    int priority, table;
    char source[48], extra;
    if (sscanf(p, "%d: from %47s lookup %d %c", &priority, source, &table,
               &extra) == 3) {
      int own = (priority == 22050 && table == 22050) ||
                (priority == 22051 && table == 22051) ||
                ((priority == 22059 || priority == 22060) &&
                 (table == 22050 || table == 22051) && !strcmp(source, "all"));
      if (own) {
        char pr[16], tb[16];
        snprintf(pr, sizeof(pr), "%d", priority);
        snprintf(tb, sizeof(tb), "%d", table);
        if (!ng_rule_delete(pr, source, tb))
          return 0;
      }
    }
    if (!end)
      break;
    p = end + 1;
  }
  if (!ng_command("ip", NULL, 0, 2, "-4", "route", "flush", "table",
                  NG_ETH_TABLE, NULL) ||
      !ng_command("ip", NULL, 0, 2, "-4", "route", "flush", "table",
                  NG_WIFI_TABLE, NULL))
    return 0;
  return !unlink(NG_RUN "/routing.owned");
}
/* Return a never-managed Wi-Fi installation to the original DHCP/address
 * ownership. Do not leave source policy or an extra DHCP client behind. */
static inline int ng_restore_native_wifi(const ng_journal *j, int boot) {
  if (!ng_command("ip", NULL, 0, 2, "link", "set", "dev", "wlan0", "up",
                  NULL) ||
      !ng_wpa_ok("RECONNECT"))
    return 0;
  long long deadline = ng_now() + 25000;
  while (!ng_wifi_connected(-1) && ng_now() < deadline)
    poll(NULL, 0, 200);
  if (!ng_wifi_connected(-1) || !ng_dhcp_stop(NG_WIFI) ||
      !ng_dhcp_stop(NG_ETHERNET))
    return 0;
  if (!ng_command("udhcpc", NULL, 0, 22, "-f", "-n", "-q", "-B", "-t", "5",
                  "-T", "3", "-i", "wlan0", "-s",
                  NG_COMPONENT "/native-set_lan.sh", NULL))
    return 0;
  char ip[16];
  if (!ng_live_ip(NG_WIFI, ip) || !ng_release_owned_routes() ||
      !ng_set_mode(&j->previous))
    return 0;
  if (!j->previous.link[NG_ETHERNET].enabled && !ng_disable_link(NG_ETHERNET))
    return 0;
  return boot || ng_runtime_repair(j);
}
#endif
