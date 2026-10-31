#define _GNU_SOURCE
#include "platform.h"
#include <assert.h>
#include <sys/types.h>
static ng_json doc;
static int parse_request(const char *s, ng_request *r) {
  return ng_json_parse(&doc, s, strlen(s)) && ng_request_parse(&doc, r);
}
int main(void) {
  char ssid_test[33];
  assert(!ng_wifi_unescape("\\x", ssid_test));
  assert(!ng_wifi_unescape("\\x2", ssid_test));
  assert(ng_wifi_unescape("hello\\x20world", ssid_test));
  assert(!strcmp(ssid_test, "hello world"));
  const char *bad[] = {"",
                       "{",
                       "[1,]",
                       "{\"x\":1,}",
                       "{\"x\":1,\"x\":2}",
                       "{\"x\":1,\"\\u0078\":2}",
                       "[01]",
                       "[1.]",
                       "[1e]",
                       "[+2]",
                       "NaN",
                       "true false",
                       "\"\\u0000\"",
                       "\"\\ud800\"",
                       "\"\\udc00\"",
                       "\"\\ud800\\u1234\"",
                       "\"\xc0\xaf\"",
                       "\"\xf4\x90\x80\x80\""};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
    assert(!ng_json_parse(&doc, bad[i], strlen(bad[i])));
  const char *valid =
      "{\"unicode\":\"\\ud83d\\ude00 café\",\"a\":[1,-2.1e+3,true,null,{}]}";
  assert(ng_json_parse(&doc, valid, strlen(valid)));
  assert(!strcmp(ng_string(&doc, 0, "unicode"), "😀 café"));
  ng_request r;
  ng_config old, next;
  ng_defaults(&old);
  assert(ng_config_valid(&old));
  assert(parse_request("{\"op\":\"select\",\"interface\":\"ethernet\"}", &r));
  assert(!ng_propose(&old, &r, &next));
  assert(parse_request("{\"op\":\"support\",\"enabled\":true}", &r) &&
         ng_propose(&old, &r, &next));
  old = next;
  assert(parse_request("{\"op\":\"select\",\"interface\":\"ethernet\"}", &r) &&
         ng_propose(&old, &r, &next));
  old = next;
  assert(parse_request(
             "{\"op\":\"interface\",\"interface\":\"wifi\",\"enabled\":false}",
             &r) &&
         ng_propose(&old, &r, &next));
  old = next;
  assert(
      parse_request(
          "{\"op\":\"interface\",\"interface\":\"ethernet\",\"enabled\":false}",
          &r) &&
      !ng_propose(&old, &r, &next));
  assert(
      parse_request("{\"op\":\"ip\",\"interface\":\"wifi\",\"method\":"
                    "\"static\",\"ip\":\"192.0.2.7\",\"prefix\":24,\"gateway\":"
                    "\"192.0.2.1\",\"dns1\":\"192.0.2.1\",\"dns2\":\"\"}",
                    &r) &&
      ng_propose(&old, &r, &next));
  assert(!ng_address("192.0.2.0", 24, "192.0.2.1"));
  assert(!ng_address("192.0.2.255", 24, "192.0.2.1"));
  assert(!ng_address("192.0.2.7", 24, "198.51.100.1"));
  assert(!ng_ipv4("127.0.0.1", NULL));
  assert(!ng_ipv4("192.0.2.1;reboot", NULL));
  const char *invalid[] = {
      "{\"op\":\"interface\",\"interface\":\"wlan0\",\"enabled\":true}",
      "{\"op\":\"select\",\"interface\":\"wifi\",\"command\":\"reboot\"}",
      "{\"op\":\"support\",\"enabled\":1}",
      "{\"op\":\"wifi-saved\",\"network_id\":-1}",
      "{\"op\":\"wifi-saved\",\"network_id\":1.5}",
      "{\"op\":\"wifi-connect\",\"ssid\":\"home\",\"security\":\"wpa2\","
      "\"password\":\"short\"}",
      "{\"op\":\"wifi-connect\",\"ssid\":\"home\",\"security\":\"wpa2\","
      "\"password\":\"12345678\\nSAVE_CONFIG\"}",
      "{\"op\":\"wifi-connect\",\"ssid\":\"home\",\"security\":\"open\","
      "\"password\":\"secret\"}",
      "{\"op\":\"confirm\",\"token\":\"abc\"}",
      "{\"op\":\"ip\",\"interface\":\"wifi\",\"method\":\"dhcp\",\"ip\":\"192."
      "0.2.1\"}"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
    assert(!parse_request(invalid[i], &r));
  assert(parse_request(
      "{\"op\":\"wifi-connect\",\"ssid\":\"café "
      "\\\"X\\\"\",\"security\":\"wpa2\",\"password\":\"some\\\"password\"}",
      &r));
  assert(!strcmp(r.ssid, "café \"X\""));
  ng_json m, u;
  const char *mq =
      "{\"connected\":true,\"last_message_age\":1,\"machine\":{\"status\":1},"
      "\"extruder\":{\"temperature\":29,\"target\":0},\"heater_bed\":{"
      "\"temperature\":24,\"target\":0},\"print\":{\"enabled\":true,\"native\":"
      "{\"state\":\"complete\"}},\"bed_drying\":{\"active\":false}}";
  const char *us =
      "{\"connected\":true,\"fresh\":true,\"print_state\":\"complete\"}";
  assert(ng_json_parse(&m, mq, strlen(mq)) &&
         ng_json_parse(&u, us, strlen(us)) && ng_idle_guard(&m, &u));
  const char *stale =
      "{\"connected\":true,\"fresh\":false,\"print_state\":\"complete\"}";
  assert(ng_json_parse(&u, stale, strlen(stale)) && !ng_idle_guard(&m, &u));
  char path[] = "/tmp/cc2-connections-core-XXXXXX";
  assert(mkdtemp(path));
  assert(chmod(path, 0700) == 0);
  assert(ng_blob_write(path, "config", &old, sizeof(old)));
  ng_config loaded;
  assert(ng_blob_read(path, "config", &loaded, sizeof(loaded)) &&
         !memcmp(&old, &loaded, sizeof(old)));
  char file[512];
  snprintf(file, sizeof(file), "%s/config", path);
  int fd = open(file, O_WRONLY);
  assert(fd >= 0);
  assert(write(fd, "bad", 3) == 3);
  close(fd);
  assert(!ng_blob_read(path, "config", &loaded, sizeof(loaded)));
  assert(unlink(file) == 0);
  assert(rmdir(path) == 0);
  /* Parser mutation corpus under ASan/UBSan: bounds and duplicate handling. */
  uint32_t seed = 1234567;
  char fuzz[1500];
  for (int k = 0; k < 8000; k++) {
    seed = seed * 1664525u + 1013904223u;
    size_t length = seed % sizeof(fuzz);
    for (size_t i = 0; i < length; i++) {
      seed = seed * 1664525u + 1013904223u;
      fuzz[i] = (char)(seed >> 24);
    }
    ng_json_parse(&doc, fuzz, length);
  }
  puts("PASS: strict JSON/UTF-8, command/config validation, last-interface "
       "guard, native completion, private atomic checksummed state and parser "
       "corpus");
  return 0;
}
