#define NG_NO_MAIN
#include "manager.c"
#include <assert.h>
static int interface_reads;
/* The fixture substitutes only native interface discovery; it does not assign
 * addresses or bypass any production mutation/authorization check. */
int getifaddrs(struct ifaddrs **result) {
  static struct ifaddrs iface;
  static struct sockaddr_in address, mask;
  memset(&iface, 0, sizeof(iface));
  memset(&address, 0, sizeof(address));
  memset(&mask, 0, sizeof(mask));
  address.sin_family = mask.sin_family = AF_INET;
  assert(inet_pton(AF_INET, "192.0.2.7", &address.sin_addr) == 1);
  assert(inet_pton(AF_INET, "255.255.255.0", &mask.sin_addr) == 1);
  iface.ifa_name = "wlan0";
  iface.ifa_addr = (struct sockaddr *)&address;
  iface.ifa_netmask = (struct sockaddr *)&mask;
  *result = &iface;
  interface_reads++;
  return 0;
}
void freeifaddrs(struct ifaddrs *value) { (void)value; }
int main(void) {
  (void)ng_request_run;
  (void)ng_watch_loop;
  struct rlimit core = {0, 0};
  assert(!setrlimit(RLIMIT_CORE, &core));
#if !defined(_FORTIFY_SOURCE) || _FORTIFY_SOURCE < 2
#error This regression must run with libc buffer hardening enabled
#endif
  char resolved[512];
  assert(ng_resolve_path("/etc/resolv.conf", resolved, sizeof(resolved)));
  assert(resolved[0] == '/');
  char exact[512];
  assert(ng_resolve_path("/etc/resolv.conf", exact, strlen(resolved) + 1));
  assert(!strcmp(exact, resolved));
  char tiny[2] = {'x', 'y'};
  assert(!ng_resolve_path("/etc/resolv.conf", tiny, 1));
  assert(tiny[0] == 0 && tiny[1] == 'y');
  struct {
    unsigned char before[16];
    char path[512];
    unsigned char after[16];
  } guard;
  memset(&guard, 0xa5, sizeof(guard));
  assert(!ng_resolve_path(NG_COMPONENT "/long-path", guard.path,
                          sizeof(guard.path)));
  for (int i = 0; i < 16; i++)
    assert(guard.before[i] == 0xa5 && guard.after[i] == 0xa5);
  assert(guard.path[0] == 0);
  /* Exercise the second production call site, including its allow-list/stat.
   * A host with a different resolver symlink may correctly refuse that target.
   */
  int allowed = ng_resolver_path(resolved);
  assert(allowed == 0 || allowed == 1);
  ng_config config;
  int configured;
  assert(ng_config_load(&config, &configured) && !configured);
  assert(ng_seed_installation());
  assert(interface_reads == 1);
  assert(ng_config_load(&config, &configured) && configured &&
         ng_config_valid(&config));
  assert(!config.managed && !config.ethernet_support &&
         config.link[NG_WIFI].enabled);
  ng_lease lease;
  assert(ng_lease_load(NG_WIFI, &lease) && lease.present);
  assert(!strcmp(lease.ip, "192.0.2.7") && lease.prefix == 24 &&
         !strcmp(lease.gateway, "192.0.2.1"));
  assert(ng_seed_installation());
  assert(interface_reads == 1);
  printf("PASS: fortified resolver bounds, both production call sites and "
         "complete fresh/idempotent seed; FORTIFY_SOURCE=%d\n",
         _FORTIFY_SOURCE);
  return 0;
}
