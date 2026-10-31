#define NG_NO_MAIN
#include "manager.c"
#include <assert.h>
int getifaddrs(struct ifaddrs **result) {
  static struct ifaddrs interfaces[2];
  static struct sockaddr_in addresses[2], masks[2];
  memset(interfaces, 0, sizeof(interfaces));
  memset(addresses, 0, sizeof(addresses));
  memset(masks, 0, sizeof(masks));
  for (int i = 0; i < 2; i++) {
    addresses[i].sin_family = masks[i].sin_family = AF_INET;
    assert(inet_pton(AF_INET, i ? "192.0.2.8" : "192.0.2.7",
                     &addresses[i].sin_addr) == 1);
    masks[i].sin_addr.s_addr = UINT32_MAX;
    interfaces[i].ifa_name = (char *)ng_name(i);
    interfaces[i].ifa_addr = (struct sockaddr *)&addresses[i];
    interfaces[i].ifa_netmask = (struct sockaddr *)&masks[i];
    interfaces[i].ifa_next = i ? NULL : &interfaces[1];
  }
  *result = interfaces;
  return 0;
}
void freeifaddrs(struct ifaddrs *p) { (void)p; }
int main(void) {
  (void)ng_request_run;
  (void)ng_watch_loop;
  assert(ng_directory(NG_STATE));
  assert(ng_directory(NG_RUN));
  /* Verify the observed BusyBox limit without touching the real kernel. */
  char result[512];
  assert(!ng_command("ip", result, sizeof(result), 2, "-4", "route", "show",
                     "table", "22050", NULL));
  assert(strstr(result, "invalid argument"));
  assert(ng_rules_check(0));
  assert(access(NG_RUN "/routing.owned", F_OK) != 0);
  ng_config c;
  ng_defaults(&c);
  c.managed = c.ethernet_support = 1;
  c.link[NG_ETHERNET].enabled = 1;
  for (int i = 0; i < 2; i++) {
    ng_lease lease;
    memset(&lease, 0, sizeof(lease));
    lease.schema = NG_SCHEMA;
    lease.present = 1;
    lease.prefix = 24;
    strcpy(lease.ip, i ? "192.0.2.8" : "192.0.2.7");
    strcpy(lease.gateway, "192.0.2.1");
    assert(ng_boot_id(lease.boot));
    assert(ng_lease_save(i, &lease));
  }
  assert(ng_configure_routes(&c));
  assert(ng_rule_present(NG_WIFI_PRIORITY, "192.0.2.7/32", NG_WIFI_TABLE) == 1);
  assert(ng_rule_present(NG_ETH_PRIORITY, "192.0.2.8/32", NG_ETH_TABLE) == 1);
  assert(ng_rule_present(NG_SELECTED_PRIORITY, "all", NG_WIFI_TABLE) == 1);
  c.selected = NG_ETHERNET;
  assert(ng_configure_routes(&c));
  assert(ng_rule_present(NG_SELECTED_PRIORITY, "all", NG_ETH_TABLE) == 1);
  assert(ng_rule_present(NG_SELECTED_PRIORITY, "all", NG_WIFI_TABLE) == 0);
  assert(ng_release_owned_routes());
  assert(ng_rules_check(0));
  assert(access(NG_RUN "/routing.owned", F_OK) != 0);
  puts("PASS: observed 8-bit table limit, read-only preflight, separate rule "
       "priorities, both selections and owned-route cleanup");
  return 0;
}
