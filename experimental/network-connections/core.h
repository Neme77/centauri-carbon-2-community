#ifndef CC2_CONNECTIONS_CORE_H
#define CC2_CONNECTIONS_CORE_H
#include "json.h"
#include <arpa/inet.h>
#include <limits.h>
#define NG_SCHEMA 1u
enum { NG_WIFI = 0, NG_ETHERNET = 1 };
enum { NG_DHCP = 0, NG_STATIC = 1 };
enum {
  NG_SELECT = 1,
  NG_INTERFACE,
  NG_SUPPORT,
  NG_IP,
  NG_WIFI_CONNECT,
  NG_WIFI_SAVED,
  NG_WIFI_FORGET,
  NG_CONFIRM,
  NG_CANCEL
};
typedef struct {
  int enabled, method, prefix;
  char ip[16], gateway[16], dns1[16], dns2[16];
} ng_iface;
typedef struct {
  uint32_t schema;
  int ethernet_support, selected;
  ng_iface link[2];
} ng_config;
typedef struct {
  int op, iface, enabled, method, prefix, network_id;
  char ip[16], gateway[16], dns1[16], dns2[16], ssid[33], security[12],
      password[65], token[33];
} ng_request;
static inline void ng_erase(void *memory, size_t size) {
  volatile unsigned char *p = memory;
  while (size--)
    *p++ = 0;
}
static inline int ng_copy(char *out, size_t cap, const char *text) {
  if (!text || strlen(text) >= cap)
    return 0;
  memcpy(out, text, strlen(text) + 1);
  return 1;
}
static inline int ng_ipv4(const char *s, uint32_t *out) {
  struct in_addr ip;
  if (!s || inet_pton(AF_INET, s, &ip) != 1)
    return 0;
  uint32_t host = ntohl(ip.s_addr);
  if ((host >> 24) == 0 || (host >> 24) == 127 || (host >> 24) >= 224)
    return 0;
  if (out)
    *out = host;
  return 1;
}
static inline int ng_address(const char *ip, int prefix, const char *gateway) {
  uint32_t host, router;
  if (prefix < 1 || prefix > 30 || !ng_ipv4(ip, &host))
    return 0;
  uint32_t mask = UINT32_MAX << (32 - prefix), network = host & mask,
           broadcast = network | ~mask;
  if (host == network || host == broadcast)
    return 0;
  if (*gateway && (!ng_ipv4(gateway, &router) || (router & mask) != network ||
                   router == network || router == broadcast || router == host))
    return 0;
  return 1;
}
static inline int ng_iface_valid(const ng_iface *i) {
  if ((i->enabled != 0 && i->enabled != 1) ||
      (i->method != NG_DHCP && i->method != NG_STATIC))
    return 0;
  if (!memchr(i->ip, 0, sizeof(i->ip)) ||
      !memchr(i->gateway, 0, sizeof(i->gateway)) ||
      !memchr(i->dns1, 0, sizeof(i->dns1)) ||
      !memchr(i->dns2, 0, sizeof(i->dns2)))
    return 0;
  if (i->method == NG_STATIC && !ng_address(i->ip, i->prefix, i->gateway))
    return 0;
  if ((*i->dns1 && !ng_ipv4(i->dns1, NULL)) ||
      (*i->dns2 && !ng_ipv4(i->dns2, NULL)))
    return 0;
  if (i->method == NG_DHCP &&
      (*i->ip || *i->gateway || *i->dns1 || *i->dns2 || i->prefix))
    return 0;
  return 1;
}
static inline int ng_config_valid(const ng_config *c) {
  if (c->schema != NG_SCHEMA ||
      (c->ethernet_support != 0 && c->ethernet_support != 1) ||
      c->selected < 0 || c->selected > 1)
    return 0;
  if (!ng_iface_valid(&c->link[0]) || !ng_iface_valid(&c->link[1]) ||
      !c->link[c->selected].enabled)
    return 0;
  if (c->link[1].enabled && !c->ethernet_support)
    return 0;
  if (c->link[0].method == NG_STATIC && c->link[1].method == NG_STATIC &&
      !strcmp(c->link[0].ip, c->link[1].ip))
    return 0;
  return 1;
}
static inline void ng_defaults(ng_config *c) {
  memset(c, 0, sizeof(*c));
  c->schema = NG_SCHEMA;
  c->link[0].enabled = 1;
}
static inline int ng_token_valid(const char *s) {
  if (strlen(s) != 32)
    return 0;
  for (int i = 0; i < 32; i++)
    if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
      return 0;
  return 1;
}
static inline int ng_clean_text(const char *s, size_t min, size_t max) {
  if (!s)
    return 0;
  size_t n = strlen(s);
  if (n < min || n > max)
    return 0;
  for (size_t i = 0; i < n; i++)
    if ((unsigned char)s[i] < 32 || (unsigned char)s[i] == 127)
      return 0;
  return 1;
}
static inline int ng_integer(const ng_json *j, const char *key, int min,
                             int max, int *out) {
  double n;
  if (!ng_number(j, 0, key, &n) || n < min || n > max || n != (int)n)
    return 0;
  *out = (int)n;
  return 1;
}
static inline int ng_request_parse(ng_json *j, ng_request *r) {
  memset(r, 0, sizeof(*r));
  r->network_id = -1;
  r->iface = -1;
  if (j->v[0].type != '{')
    return 0;
  const char *op = ng_string(j, 0, "op");
  if (!op)
    return 0;
  static const char *ops[] = {
      "",           "select",      "interface", "support", "ip", "wifi-connect",
      "wifi-saved", "wifi-forget", "confirm",   "cancel"};
  for (int i = 1; i <= NG_CANCEL; i++)
    if (!strcmp(op, ops[i]))
      r->op = i;
  if (!r->op)
    return 0;
  for (int i = 1; i < j->v[0].next; i = j->v[i + 1].next) {
    const char *k = j->v[i].text;
    int allowed = !strcmp(k, "op");
    if (r->op == NG_SELECT)
      allowed |= !strcmp(k, "interface");
    if (r->op == NG_INTERFACE || r->op == NG_IP)
      allowed |= !strcmp(k, "interface");
    if (r->op == NG_INTERFACE || r->op == NG_SUPPORT)
      allowed |= !strcmp(k, "enabled");
    if (r->op == NG_IP)
      allowed |= !strcmp(k, "method") || !strcmp(k, "ip") ||
                 !strcmp(k, "prefix") || !strcmp(k, "gateway") ||
                 !strcmp(k, "dns1") || !strcmp(k, "dns2");
    if (r->op == NG_WIFI_CONNECT)
      allowed |= !strcmp(k, "ssid") || !strcmp(k, "security") ||
                 !strcmp(k, "password");
    if (r->op == NG_WIFI_SAVED || r->op == NG_WIFI_FORGET)
      allowed |= !strcmp(k, "network_id");
    if (r->op == NG_CONFIRM || r->op == NG_CANCEL)
      allowed |= !strcmp(k, "token");
    if (!allowed)
      return 0;
  }
  if (r->op == NG_SELECT || r->op == NG_INTERFACE || r->op == NG_IP) {
    const char *iface = ng_string(j, 0, "interface");
    if (!iface)
      return 0;
    r->iface = !strcmp(iface, "wifi") ? 0 : !strcmp(iface, "ethernet") ? 1 : -1;
    if (r->iface < 0)
      return 0;
  }
  if ((r->op == NG_INTERFACE || r->op == NG_SUPPORT) &&
      !ng_bool(j, 0, "enabled", &r->enabled))
    return 0;
  if (r->op == NG_IP) {
    const char *method = ng_string(j, 0, "method");
    if (!method)
      return 0;
    r->method = !strcmp(method, "dhcp")     ? 0
                : !strcmp(method, "static") ? 1
                                            : -1;
    if (r->method < 0)
      return 0;
    if (r->method == NG_DHCP) {
      if (j->v[0].count != 3)
        return 0;
      return 1;
    }
    if (!ng_copy(r->ip, sizeof(r->ip), ng_string(j, 0, "ip")) ||
        !ng_integer(j, "prefix", 1, 30, &r->prefix))
      return 0;
    const char *keys[] = {"gateway", "dns1", "dns2"};
    char *values[] = {r->gateway, r->dns1, r->dns2};
    for (int i = 0; i < 3; i++) {
      const char *s = ng_string(j, 0, keys[i]);
      if (!s || !ng_copy(values[i], 16, s))
        return 0;
    }
    if (!ng_address(r->ip, r->prefix, r->gateway) ||
        (*r->dns1 && !ng_ipv4(r->dns1, NULL)) ||
        (*r->dns2 && !ng_ipv4(r->dns2, NULL)))
      return 0;
  }
  if (r->op == NG_WIFI_CONNECT) {
    const char *ssid = ng_string(j, 0, "ssid"),
               *security = ng_string(j, 0, "security"),
               *password = ng_string(j, 0, "password");
    if (!ng_clean_text(ssid, 1, 32) ||
        !ng_copy(r->ssid, sizeof(r->ssid), ssid) || !security ||
        !ng_copy(r->security, sizeof(r->security), security))
      return 0;
    if (!strcmp(security, "open")) {
      if (password && *password)
        return 0;
    } else if (!strcmp(security, "wpa2")) {
      if (!ng_clean_text(password, 8, 64) ||
          !ng_copy(r->password, sizeof(r->password), password))
        return 0;
      if (strlen(password) == 64)
        for (int i = 0; i < 64; i++)
          if (!isxdigit((unsigned char)password[i]))
            return 0;
    } else
      return 0;
  }
  if ((r->op == NG_WIFI_SAVED || r->op == NG_WIFI_FORGET) &&
      !ng_integer(j, "network_id", 0, 4095, &r->network_id))
    return 0;
  if (r->op == NG_CONFIRM || r->op == NG_CANCEL) {
    const char *t = ng_string(j, 0, "token");
    if (!t || !ng_copy(r->token, sizeof(r->token), t) || !ng_token_valid(t))
      return 0;
  }
  return 1;
}
static inline int ng_propose(const ng_config *old, const ng_request *r,
                             ng_config *next) {
  *next = *old;
  switch (r->op) {
  case NG_SELECT:
    next->selected = r->iface;
    next->link[r->iface].enabled = 1;
    break;
  case NG_SUPPORT:
    next->ethernet_support = r->enabled;
    next->link[1].enabled = r->enabled;
    if (!r->enabled && next->selected == 1)
      next->selected = 0;
    break;
  case NG_INTERFACE:
    next->link[r->iface].enabled = r->enabled;
    if (!r->enabled && next->selected == r->iface)
      next->selected = 1 - r->iface;
    break;
  case NG_IP: {
    ng_iface *i = &next->link[r->iface];
    i->method = r->method;
    i->prefix = r->prefix;
    strcpy(i->ip, r->ip);
    strcpy(i->gateway, r->gateway);
    strcpy(i->dns1, r->dns1);
    strcpy(i->dns2, r->dns2);
    break;
  }
  case NG_WIFI_CONNECT:
  case NG_WIFI_SAVED:
    next->link[0].enabled = 1;
    break;
  default:
    return 0;
  }
  return ng_config_valid(next);
}
static inline int ng_field_bool(const ng_json *j, int at, const char *key,
                                int expected) {
  int value;
  return ng_bool(j, at, key, &value) && value == expected;
}
static inline int ng_heater_safe(const ng_json *j, int at) {
  double t, target;
  return ng_number(j, at, "temperature", &t) &&
         ng_number(j, at, "target", &target) && t >= 0 && t <= 50 &&
         target == 0;
}
static inline int ng_idle_guard(const ng_json *mqtt, const ng_json *uds) {
  double age, status;
  if (!ng_field_bool(mqtt, 0, "connected", 1) ||
      !ng_number(mqtt, 0, "last_message_age", &age) || age < 0 || age > 5)
    return 0;
  int machine = ng_member(mqtt, 0, "machine"),
      job = ng_member(mqtt, 0, "print");
  if (!ng_number(mqtt, machine, "status", &status) || status != 1 ||
      !ng_heater_safe(mqtt, ng_member(mqtt, 0, "extruder")) ||
      !ng_heater_safe(mqtt, ng_member(mqtt, 0, "heater_bed")))
    return 0;
  if (!ng_field_bool(uds, 0, "connected", 1) ||
      !ng_field_bool(uds, 0, "fresh", 1))
    return 0;
  const char *state = ng_string(uds, 0, "print_state"),
             *native = ng_string(mqtt, ng_member(mqtt, job, "native"), "state");
  if (!state || (!strcmp(state, "standby") ? 0 : strcmp(state, "complete")) ||
      !native || strcmp(native, state))
    return 0;
  int enabled;
  if (!ng_bool(mqtt, job, "enabled", &enabled) ||
      (enabled && strcmp(state, "complete")))
    return 0;
  int drying = ng_member(mqtt, 0, "bed_drying");
  if (drying >= 0 && !ng_field_bool(mqtt, drying, "active", 0))
    return 0;
  return 1;
}
static inline void ng_iface_json(FILE *out, const ng_iface *i) {
  fprintf(out, "{\"enabled\":%s,\"method\":\"%s\",\"ip\":",
          i->enabled ? "true" : "false",
          i->method == NG_STATIC ? "static" : "dhcp");
  ng_json_quote(out, i->ip);
  fprintf(out, ",\"prefix\":%d,\"gateway\":", i->prefix);
  ng_json_quote(out, i->gateway);
  fputs(",\"dns1\":", out);
  ng_json_quote(out, i->dns1);
  fputs(",\"dns2\":", out);
  ng_json_quote(out, i->dns2);
  fputc('}', out);
}
static inline void ng_config_json(FILE *out, const ng_config *c) {
  fprintf(out,
          "{\"schema\":1,\"ethernet_support\":%s,\"selected\":\"%s\",\"wifi\":",
          c->ethernet_support ? "true" : "false",
          c->selected == NG_WIFI ? "wifi" : "ethernet");
  ng_iface_json(out, &c->link[0]);
  fputs(",\"ethernet\":", out);
  ng_iface_json(out, &c->link[1]);
  fputc('}', out);
}
#endif
