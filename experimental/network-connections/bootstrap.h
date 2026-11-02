#ifndef CC2_CONNECTIONS_BOOTSTRAP_H
#define CC2_CONNECTIONS_BOOTSTRAP_H
#include "dhcp.h"
#include "profile-pins.h"
#include "profile_cache.h"
#include "usb_policy.h"
#ifndef NG_NET_SYSFS
#define NG_NET_SYSFS "/sys/class/net"
#endif
#ifndef NG_MODULE_SYSFS
#define NG_MODULE_SYSFS "/sys/module"
#endif
static inline int ng_profile_execute(const char *kind) {
  char *args[] = {"sh", NG_COMPONENT "/profile.sh", (char *)kind, NULL};
  char diagnostic[1024] = {0};
  int ok = ng_exec("/bin/sh", args, diagnostic, sizeof(diagnostic), 15);
  if (!ok && diagnostic[0])
    ng_atomic(NG_RUN, "last-command.txt", diagnostic, strlen(diagnostic));
  return ok;
}
static inline int ng_check_profile(const char *kind) {
  return ng_profile_cached(kind, ng_profile_execute);
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
static inline int ng_usb_failure(const char *reason) {
  ng_atomic(NG_RUN, "last-command.txt", reason, strlen(reason));
  ng_event("usb_qualification_failed");
  return 0;
}
static inline int ng_usb_bound(const ng_usb_profile *usb) {
  char path[512], selected[1024], device[1024], driver[1024];
  snprintf(path, sizeof(path), "%s/%s", NG_USB_SYSFS, usb->port);
  if (!ng_resolve_path(path, selected, sizeof(selected)) ||
      !ng_resolve_path(NG_NET_SYSFS "/cc2lan0/device", device, sizeof(device)) ||
      !ng_resolve_path(NG_NET_SYSFS "/cc2lan0/device/driver", driver,
                       sizeof(driver)))
    return 0;
  size_t n = strlen(selected);
  const char *name = strrchr(driver, '/');
  return !strncmp(selected, device, n) && device[n] == ':' && name &&
         !strcmp(name + 1, "cc2_ue300_r8152");
}
static inline int ng_load_drivers(void) {
  ng_usb_profile usb = ng_usb_observe();
  if (!usb.readable || !usb.valid)
    return ng_usb_failure("USB Ethernet needs one readable 0bda:8153 adapter at USB high speed (480 Mb/s).");
  if (!ng_usb_allowed(&usb))
    return ng_usb_failure(!strcmp(usb.revision, "3000")
        ? "RTL8153 revision 3000 is experimental: explicitly approve it in Connections first."
        : "This RTL8153 revision is not supported by this build; the qualified revision is 31fd.");
  if (!ng_check_profile("ethernet"))
    return 0;
  char digest[65];
  const char *probe = usb.qualified ? "cc2_ue300_abi_probe.ko"
                                   : "cc2_ue300_abi_probe_3000.ko";
  const char *probe_name = usb.qualified ? "cc2_ue300_abi_probe"
                                        : "cc2_ue300_abi_probe_3000";
  const char *probe_sha = usb.qualified ? NG_CC2_UE300_ABI_PROBE_KO_SHA
                                       : NG_CC2_UE300_ABI_PROBE_3000_KO_SHA;
  char probe_path[512], module_path[256];
  snprintf(probe_path, sizeof(probe_path), "%s/%s", NG_COMPONENT, probe);
  snprintf(module_path, sizeof(module_path), "%s/%s", NG_MODULE_SYSFS, probe_name);
  if (!ng_hash_file(NG_COMPONENT "/cc2_ue300_mii.ko", digest) ||
      strcmp(digest, NG_CC2_UE300_MII_KO_SHA) ||
      !ng_hash_file(NG_COMPONENT "/cc2_ue300_r8152.ko", digest) ||
      strcmp(digest, NG_CC2_UE300_R8152_KO_SHA) ||
      !ng_hash_file(probe_path, digest) || strcmp(digest, probe_sha))
    return ng_usb_failure("USB Ethernet module checksum mismatch; reinstall the matching component.");
  if (!access(module_path, F_OK))
    return ng_usb_failure("An ABI probe is already loaded; finish that operation first.");
  /* A manually preloaded driver does not bypass the ABI/identity probe. */
  if (!ng_command("insmod", NULL, 0, 5, probe_path, NULL))
    return 0;
  if (!ng_command("rmmod", NULL, 0, 5, probe_name, NULL))
    return 0;
  ng_usb_profile after = ng_usb_observe();
  if (!ng_usb_allowed(&after) || strcmp(usb.port, after.port) ||
      strcmp(usb.revision, after.revision))
    return ng_usb_failure("The USB adapter changed during qualification; reconnect and try again.");
  if (access(NG_MODULE_SYSFS "/cc2_ue300_r8152", F_OK)) {
    if (access(NG_MODULE_SYSFS "/cc2_ue300_mii", F_OK) &&
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
  return (if_nametoindex("cc2lan0") && ng_usb_bound(&usb)) ||
         ng_usb_failure("The qualified USB device did not bind to cc2lan0 with the expected driver.");
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
  if (iface == NG_WIFI && !ng_wifi_reconnect_ready(8000))
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
      int own = (priority == 22050 && table == NG_ETH_TABLE_ID) ||
                (priority == 22051 && table == NG_WIFI_TABLE_ID) ||
                ((priority == 22059 || priority == 22060) &&
                 (table == NG_ETH_TABLE_ID || table == NG_WIFI_TABLE_ID) &&
                 !strcmp(source, "all"));
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
      !ng_wifi_reconnect_ready(8000))
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
