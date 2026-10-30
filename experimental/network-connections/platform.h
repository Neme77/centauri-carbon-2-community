#ifndef CC2_CONNECTIONS_PLATFORM_H
#define CC2_CONNECTIONS_PLATFORM_H
#include "wifi.h"
#include <stdarg.h>
#define NG_ETH_TABLE "22050"
#define NG_WIFI_TABLE "22051"
#define NG_SELECTED_PRIORITY "22060"
static inline const char *ng_tool(const char *name) {
#ifdef NG_TEST_TOOLS
  static char fixture[512];
  snprintf(fixture, sizeof(fixture), "%s/%s", NG_TEST_TOOLS, name);
  return access(fixture, X_OK) ? NULL : fixture;
#else
  static char path[512];
  const char *dirs[] = {"/sbin", "/usr/sbin", "/bin", "/usr/bin"};
  for (int i = 0; i < 4; i++) {
    snprintf(path, sizeof(path), "%s/%s", dirs[i], name);
    if (!access(path, X_OK))
      return path;
  }
  return NULL;
#endif
}
static inline int ng_command(const char *name, char *out, size_t cap,
                             int seconds, ...) {
  char *args[32];
  args[0] = (char *)name;
  int n = 1;
  va_list list;
  va_start(list, seconds);
  const char *value;
  while ((value = va_arg(list, const char *))) {
    if (n >= 31) {
      va_end(list);
      return 0;
    }
    args[n++] = (char *)value;
  }
  va_end(list);
  args[n] = NULL;
  const char *tool = ng_tool(name);
  return tool && ng_exec(tool, args, out, cap, seconds);
}
static inline int ng_prefix_mask(const char *mask) {
  struct in_addr ip;
  if (inet_pton(AF_INET, mask, &ip) != 1)
    return -1;
  uint32_t m = ntohl(ip.s_addr);
  int count = 0, zero = 0;
  for (int i = 31; i >= 0; i--) {
    if (m & (1u << i)) {
      if (zero)
        return -1;
      count++;
    } else
      zero = 1;
  }
  return count;
}
static inline void ng_subnet(char out[32], const char *ip, int prefix) {
  uint32_t n;
  struct in_addr net;
  if (!ng_ipv4(ip, &n) || prefix < 1 || prefix > 30) {
    *out = 0;
    return;
  }
  net.s_addr = htonl(n & (UINT32_MAX << (32 - prefix)));
  char network[16];
  inet_ntop(AF_INET, &net, network, sizeof(network));
  snprintf(out, 32, "%s/%d", network, prefix);
}
static inline int ng_first_ipv4(const char *text, char out[16]) {
  if (!text || !*text) {
    out[0] = 0;
    return 1;
  }
  size_t n = strcspn(text, " \t\r\n");
  if (n >= 16)
    return 0;
  memcpy(out, text, n);
  out[n] = 0;
  return ng_ipv4(out, NULL);
}
static inline int ng_dns_values(const char *text, char one[16], char two[16]) {
  one[0] = two[0] = 0;
  if (!text)
    return 1;
  int count = 0;
  const char *p = text;
  while (*p) {
    while (*p == ' ' || *p == '\t')
      p++;
    if (!*p)
      break;
    size_t n = strcspn(p, " \t");
    char value[16];
    if (n >= 16)
      return 0;
    memcpy(value, p, n);
    value[n] = 0;
    if (!ng_ipv4(value, NULL))
      return 0;
    if (count == 0)
      strcpy(one, value);
    if (count == 1)
      strcpy(two, value);
    count++;
    p += n;
  }
  return 1;
}
static inline int ng_lease_environment(ng_lease *l) {
  memset(l, 0, sizeof(*l));
  l->schema = NG_SCHEMA;
  l->present = 1;
  const char *ip = getenv("ip"), *mask = getenv("subnet");
  if (!ip || !ng_copy(l->ip, 16, ip))
    return 0;
  l->prefix = mask ? ng_prefix_mask(mask) : 24;
  if (!ng_first_ipv4(getenv("router"), l->gateway) ||
      !ng_dns_values(getenv("dns"), l->dns1, l->dns2) || !ng_boot_id(l->boot) ||
      !ng_lease_valid(l))
    return 0;
  /* Never interpret DHCP routes as shell text or silently apply unknown policy.
   */
  const char *routes = getenv("staticroutes"), *ms = getenv("msstaticroutes");
  if ((routes && *routes) || (ms && *ms))
    return 0;
  return 1;
}
static inline int ng_static_lease(const ng_iface *i, ng_lease *l) {
  memset(l, 0, sizeof(*l));
  l->schema = NG_SCHEMA;
  l->present = 1;
  l->prefix = i->prefix;
  strcpy(l->ip, i->ip);
  strcpy(l->gateway, i->gateway);
  strcpy(l->dns1, i->dns1);
  strcpy(l->dns2, i->dns2);
  return ng_boot_id(l->boot) && ng_lease_valid(l);
}
static inline int ng_seed_wifi(void) {
  ng_lease existing;
  if (ng_lease_load(NG_WIFI, &existing) && existing.present)
    return 1;
  struct ifaddrs *all = NULL;
  if (getifaddrs(&all))
    return 0;
  ng_lease lease;
  memset(&lease, 0, sizeof(lease));
  lease.schema = NG_SCHEMA;
  lease.present = 1;
  int count = 0;
  for (struct ifaddrs *p = all; p; p = p->ifa_next)
    if (!strcmp(p->ifa_name, "wlan0") && p->ifa_addr &&
        p->ifa_addr->sa_family == AF_INET) {
      inet_ntop(AF_INET, &((struct sockaddr_in *)p->ifa_addr)->sin_addr,
                lease.ip, 16);
      char mask[16];
      if (!p->ifa_netmask)
        continue;
      inet_ntop(AF_INET, &((struct sockaddr_in *)p->ifa_netmask)->sin_addr,
                mask, 16);
      lease.prefix = ng_prefix_mask(mask);
      count++;
    }
  freeifaddrs(all);
  if (count != 1 || lease.prefix == 32)
    return 0;
  char routes[2048];
  if (!ng_command("ip", routes, sizeof(routes), 2, "-4", "route", "show",
                  "default", "dev", "wlan0", NULL))
    return 0;
  if (*routes) {
    char gateway[16], extra[40];
    if (sscanf(routes, "default via %15s %39s", gateway, extra) < 1 ||
        !ng_ipv4(gateway, NULL))
      return 0;
    strcpy(lease.gateway, gateway);
  }
  char resolver[2048];
  size_t size;
  char resolved[512];
  if (realpath("/etc/resolv.conf", resolved) &&
      ng_read_file(resolved, resolver, sizeof(resolver), &size, 0)) {
    char *p = resolver;
    int dns = 0;
    while (*p) {
      char *end = strchr(p, '\n');
      if (end)
        *end = 0;
      char address[16], extra;
      if (sscanf(p, "nameserver %15s %c", address, &extra) == 1 &&
          ng_ipv4(address, NULL)) {
        if (dns == 0)
          strcpy(lease.dns1, address);
        if (dns == 1)
          strcpy(lease.dns2, address);
        dns++;
      }
      if (!end)
        break;
      p = end + 1;
    }
  }
  return ng_boot_id(lease.boot) && ng_lease_valid(&lease) &&
         ng_lease_save(NG_WIFI, &lease);
}
static inline int ng_rules_safe(void) {
  char rules[8192];
  if (!ng_command("ip", rules, sizeof(rules), 2, "-4", "rule", "show", NULL))
    return 0;
  int owned = 0;
  char marker[4];
  size_t size;
  owned =
      ng_read_file(NG_RUN "/routing.owned", marker, sizeof(marker), &size, 1) &&
      !strcmp(marker, "1\n");
  for (const char *p = rules; *p;) {
    char *end;
    long priority = strtol(p, &end, 10);
    if (end == p)
      return 0;
    if (priority == 22050 || priority == 22051 || priority == 22059 ||
        priority == 22060) {
      if (!owned)
        return 0;
    }
    const char *next = strchr(p, '\n');
    if (!next)
      break;
    p = next + 1;
  }
  if (!owned) {
    char table[4096];
    for (int i = 0; i < 2; i++) {
      if (!ng_command("ip", table, sizeof(table), 2, "-4", "route", "show",
                      "table", i ? NG_WIFI_TABLE : NG_ETH_TABLE, NULL) ||
          *table)
        return 0;
    }
    if (!ng_atomic(NG_RUN, "routing.owned", "1\n", 2))
      return 0;
  }
  return 1;
}
static inline int ng_rule_present(const char *priority, const char *source,
                                  const char *table) {
  char rules[8192];
  if (!ng_command("ip", rules, sizeof(rules), 2, "-4", "rule", "show", NULL))
    return -1;
  for (char *p = rules; *p;) {
    char *end = strchr(p, '\n');
    if (end)
      *end = 0;
    int pr, tb;
    char from[48], extra;
    if (sscanf(p, "%d: from %47s lookup %d %c", &pr, from, &tb, &extra) == 3 &&
        pr == atoi(priority) && tb == atoi(table)) {
      char canonical[48];
      strcpy(canonical, source);
      char *slash = strstr(canonical, "/32");
      if (slash)
        *slash = 0;
      char *fromslash = strstr(from, "/32");
      if (fromslash)
        *fromslash = 0;
      if (!strcmp(from, canonical))
        return 1;
    }
    if (!end)
      break;
    p = end + 1;
  }
  return 0;
}
static inline int ng_rule_add(const char *priority, const char *source,
                              const char *table) {
  int found = ng_rule_present(priority, source, table);
  return found == 1 ||
         (found == 0 &&
          ng_command("ip", NULL, 0, 2, "-4", "rule", "add", "priority",
                     priority, "from", source, "table", table, NULL));
}
static inline int ng_rule_delete(const char *priority, const char *source,
                                 const char *table) {
  return ng_command("ip", NULL, 0, 2, "-4", "rule", "del", "priority", priority,
                    "from", source, "table", table, NULL);
}
static inline int ng_configure_routes(const ng_config *config) {
  if (!ng_rules_safe())
    return 0;
  ng_lease leases[2];
  int have[2] = {0};
  char expected[2][24] = {{0}, {0}};
  for (int i = 0; i < 2; i++) {
    if (!config->link[i].enabled || !ng_lease_load(i, &leases[i]) ||
        !leases[i].present)
      continue;
    have[i] = 1;
    char subnet[32];
    ng_subnet(subnet, leases[i].ip, leases[i].prefix);
    snprintf(expected[i], sizeof(expected[i]), "%s/32", leases[i].ip);
    const char *table = i == NG_WIFI ? NG_WIFI_TABLE : NG_ETH_TABLE;
    if (!ng_command("ip", NULL, 0, 2, "-4", "route", "replace", subnet, "dev",
                    ng_name(i), "scope", "link", "src", leases[i].ip, "table",
                    table, NULL))
      return 0;
    if (*leases[i].gateway) {
      if (!ng_command("ip", NULL, 0, 2, "-4", "route", "replace", "default",
                      "via", leases[i].gateway, "dev", ng_name(i), "src",
                      leases[i].ip, "table", table, NULL))
        return 0;
    } else if (!ng_command("ip", NULL, 0, 2, "-4", "route", "replace",
                           "unreachable", "default", "table", table, NULL))
      return 0;
    if (!ng_rule_add(table, expected[i], table))
      return 0;
  }
  if (have[0] && have[1] && !strcmp(leases[0].ip, leases[1].ip))
    return 0;
  if (!have[config->selected])
    return 1; /* Target is not ready: keep the working path. */
  const char *selected =
      config->selected == NG_WIFI ? NG_WIFI_TABLE : NG_ETH_TABLE;
  /* Put the new default policy in front of the old one before replacing it.
   * A confirmed steady state has no recovery timer or automatic preference. */
  if (!ng_rule_add("22059", "all", selected))
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
      if (priority == 22060 && (table == 22050 || table == 22051) &&
          !strcmp(source, "all")) {
        char tb[16];
        snprintf(tb, sizeof(tb), "%d", table);
        if (strcmp(tb, selected) && !ng_rule_delete("22060", "all", tb))
          return 0;
      }
      if ((priority == 22050 && table == 22050) ||
          (priority == 22051 && table == 22051)) {
        int iface = priority == 22051 ? NG_WIFI : NG_ETHERNET;
        char canonical[48];
        strcpy(canonical, source);
        char *slash = strstr(canonical, "/32");
        if (slash)
          *slash = 0;
        if (!have[iface] || strcmp(canonical, leases[iface].ip)) {
          char pr[16];
          snprintf(pr, sizeof(pr), "%d", priority);
          if (!ng_rule_delete(pr, source, pr))
            return 0;
        }
      }
    } else if (!strncmp(p, "22050:", 6) || !strncmp(p, "22051:", 6) ||
               !strncmp(p, "22059:", 6) || !strncmp(p, "22060:", 6))
      return 0;
    if (!end)
      break;
    p = end + 1;
  }
  if (!ng_rule_add(NG_SELECTED_PRIORITY, "all", selected) ||
      !ng_rule_delete("22059", "all", selected))
    return 0;
  for (int i = 0; i < 2; i++)
    if (!have[i] &&
        !ng_command("ip", NULL, 0, 2, "-4", "route", "flush", "table",
                    i == NG_WIFI ? NG_WIFI_TABLE : NG_ETH_TABLE, NULL))
      return 0;
  return 1;
}
static inline int ng_resolver_path(char out[512]) {
  if (!realpath("/etc/resolv.conf", out))
    return 0;
  if (strcmp(out, "/etc/resolv.conf") && strcmp(out, "/tmp/resolv.conf") &&
      strcmp(out, "/tmp/resolv.conf.auto"))
    return 0;
  struct stat st;
  return !lstat(out, &st) && S_ISREG(st.st_mode) && st.st_uid == ng_owner() &&
         !(st.st_mode & 0022) && st.st_nlink == 1;
}
static inline int ng_resolver_write(const void *data, size_t size) {
  char path[512];
  if (size > 2047 || !ng_resolver_path(path))
    return 0;
  /* O_NOFOLLOW protects the qualified existing target; preserve its mode and
   * inode for the native resolver and writers which already opened it. */
  int fd = open(path, O_WRONLY | O_TRUNC | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0)
    return 0;
  int ok = ng_write_all(fd, data, size) && !fsync(fd);
  close(fd);
  return ok;
}
static inline int ng_resolver_selected(const ng_config *config) {
  ng_lease lease;
  if (!ng_lease_load(config->selected, &lease) || !lease.present)
    return 0;
  if (!*lease.dns1 && !*lease.dns2)
    return 1;
  char output[256];
  int n = snprintf(
      output, sizeof(output), "# CC2 selected connection\n%s%s%s%s%s%s",
      *lease.dns1 ? "nameserver " : "", lease.dns1, *lease.dns1 ? "\n" : "",
      *lease.dns2 ? "nameserver " : "", lease.dns2, *lease.dns2 ? "\n" : "");
  return n > 0 && (size_t)n < sizeof(output) &&
         ng_resolver_write(output, (size_t)n);
}
static inline int ng_set_mode(const ng_config *config) {
  const char *mode = config->selected == NG_WIFI ? "wifi\n" : "ethernet\n";
  /* The compatibility library requires exactly one private 0600 regular file
   * at its qualified path. Rename a freshly created file; never follow links.
   */
  char token[33], path[512];
  if (!ng_nonce(token))
    return 0;
  snprintf(path, sizeof(path), "/tmp/.cc2-network-mode-%s", token);
  int fd =
      open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (fd < 0)
    return 0;
  int ok = ng_write_all(fd, mode, strlen(mode)) && !fsync(fd);
  close(fd);
  if (ok && rename(path, "/tmp/cc2-network-compat-r1.mode"))
    ok = 0;
  if (!ok)
    unlink(path);
  return ok;
}
static inline int ng_apply_address(int iface, const ng_lease *lease) {
  if (!lease->present || !ng_lease_valid(lease))
    return 0;
  char address[24];
  snprintf(address, sizeof(address), "%s/32", lease->ip);
  if (!ng_command("ip", NULL, 0, 2, "-4", "addr", "flush", "dev",
                  ng_name(iface), "scope", "global", NULL))
    return 0;
  return ng_command("ip", NULL, 0, 2, "-4", "addr", "add", address, "dev",
                    ng_name(iface), NULL);
}
#endif
