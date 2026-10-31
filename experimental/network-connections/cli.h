#ifndef CC2_CONNECTIONS_CLI_H
#define CC2_CONNECTIONS_CLI_H
#include "boot.h"
#include "native.h"
#include "public.h"
static inline int ng_interface_arg(const char *s) {
  return !strcmp(s, "wifi") || !strcmp(s, "wlan0")         ? NG_WIFI
         : !strcmp(s, "ethernet") || !strcmp(s, "cc2lan0") ? NG_ETHERNET
                                                           : -1;
}
static inline int ng_cache_networks(int scan) {
  if (!ng_directory(NG_RUN))
    return 0;
  int lock = ng_lock("scan.lock", 0);
  if (lock < 0)
    return 0;
  char *text = NULL;
  size_t size = 0;
  FILE *out = open_memstream(&text, &size);
  int ok = out != NULL;
  if (ok) {
    fprintf(out, "{\"uptime_ms\":%lld,\"saved\":", ng_now());
    ok = ng_wifi_list(out, 0);
    fputs(",\"visible\":", out);
    if (scan)
      ok = ng_wifi_list(out, 1) && ok;
    else
      fputs("[]", out);
    fputc('}', out);
    if (fclose(out))
      ok = 0;
  }
  if (ok && size < 16384)
    ok = ng_atomic(NG_RUN, "networks.json", text, size);
  else
    ok = 0;
  free(text);
  close(lock);
  return ok;
}
static inline int ng_local_networks(void) {
  ng_config c;
  if (!ng_operating_config(&c))
    return 0;
  int count = 0;
  for (int i = 0; i < 2; i++) {
    ng_lease l;
    char live[16], subnet[32];
    if (c.link[i].enabled && ng_live_ip(i, live) && ng_lease_load(i, &l) &&
        l.present && !strcmp(l.ip, live)) {
      ng_subnet(subnet, l.ip, l.prefix);
      if (*subnet) {
        puts(subnet);
        count++;
      }
    }
  }
  return count > 0;
}
static inline int ng_client_action(const char *action, int iface) {
  if (iface < 0 || !ng_managed())
    return 0;
  int lock = ng_lock("operation.lock", 0);
  if (lock < 0)
    return 1; /* The transaction/boot worker already owns DHCP. */
  ng_config c;
  int ok = ng_operating_config(&c);
  if (ok && !strcmp(action, "stop"))
    ok = ng_dhcp_stop(iface);
  else if (ok && (!strcmp(action, "start") || !strcmp(action, "restart")))
    ok = !c.link[iface].enabled ||
         ng_prepare_link(iface, &c.link[iface], !strcmp(action, "restart"), 0);
  else
    ok = 0;
  close(lock);
  return ok;
}
static inline int ng_stdin_request(const char *local, const char *peer,
                                   int port) {
  if (!ng_ipv4(local, NULL) || !ng_ipv4(peer, NULL))
    return ng_error("invalid_endpoint");
  struct rlimit core = {0, 0};
  setrlimit(RLIMIT_CORE, &core);
  signal(SIGHUP, SIG_IGN);
  if (setsid() < 0 && getpgrp() != getpid())
    return ng_error("session_unavailable");
  char input[4097];
  size_t used = 0;
  int ok = 1;
  long long end = ng_now() + 2000;
  while (used < sizeof(input) - 1) {
    struct pollfd p = {.fd = 0, .events = POLLIN};
    int wait = (int)(end - ng_now());
    if (wait <= 0 || poll(&p, 1, wait) <= 0) {
      ok = 0;
      break;
    }
    ssize_t n = read(0, input + used, sizeof(input) - 1 - used);
    if (!n)
      break;
    if (n < 0) {
      if (errno == EINTR)
        continue;
      ok = 0;
      break;
    }
    used += (size_t)n;
  }
  if (used >= sizeof(input) - 1)
    ok = 0;
  input[used] = 0;
  ng_json *doc = malloc(sizeof(*doc));
  ng_request r;
  memset(&r, 0, sizeof(r));
  ok =
      ok && doc && ng_json_parse(doc, input, used) && ng_request_parse(doc, &r);
  ng_erase(input, sizeof(input));
  if (doc) {
    ng_erase(doc, sizeof(*doc));
    free(doc);
  }
  int result =
      ok ? ng_request_run(&r, local, peer, port) : ng_error("invalid_request");
  ng_erase(&r, sizeof(r));
  return result;
}
#endif
