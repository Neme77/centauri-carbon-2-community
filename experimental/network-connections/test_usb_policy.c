#define _GNU_SOURCE
#include "usb_policy.h"
#include <assert.h>

static void attribute(const char *port, const char *name, const char *value) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", NG_USB_SYSFS, port);
  assert(!mkdir(path, 0700) || errno == EEXIST);
  snprintf(path, sizeof(path), "%s/%s/%s", NG_USB_SYSFS, port, name);
  FILE *out = fopen(path, "w");
  assert(out && fputs(value, out) >= 0 && !fclose(out));
}
static void adapter(const char *port, const char *revision) {
  attribute(port, "idVendor", "0bda\n");
  attribute(port, "idProduct", "8153\n");
  attribute(port, "bcdDevice", revision);
  attribute(port, "speed", "480\n");
}
static int request(const char *text) {
  ng_json json;
  ng_request r;
  return ng_json_parse(&json, text, strlen(text)) && ng_request_parse(&json, &r);
}
int main(void) {
  assert(ng_directory(NG_STATE));
  assert(!mkdir(NG_USB_SYSFS, 0700));
  ng_usb_profile usb = ng_usb_observe();
  assert(usb.readable && !usb.count && !ng_usb_allowed(&usb));
  assert(!ng_usb_approve("3000", 1));
  adapter("1-1", "31fd\n");
  usb = ng_usb_observe();
  assert(usb.qualified && !usb.approved && ng_usb_allowed(&usb));
  assert(!ng_usb_approve("31fd", 1));
  adapter("1-1", "3000\n");
  usb = ng_usb_observe();
  assert(usb.valid && !usb.qualified && !ng_usb_allowed(&usb));
  assert(!ng_usb_approve("3010", 1));
  assert(ng_usb_approve("3000", 1));
  usb = ng_usb_observe();
  assert(usb.approved && !usb.qualified && ng_usb_allowed(&usb));
  ng_usb_approval approval;
  assert(ng_usb_approval_load(&approval));
  assert(!strcmp(approval.revision, "3000"));
  assert(!chmod(NG_STATE "/usb-approval.bin", 0644));
  assert(!ng_usb_approval_load(&approval));
  assert(!chmod(NG_STATE "/usb-approval.bin", 0600));
  assert(ng_usb_approval_load(&approval));
  attribute("1-1", "speed", "5000\n");
  usb = ng_usb_observe();
  assert(!ng_usb_allowed(&usb));
  adapter("1-1", "3010\n");
  usb = ng_usb_observe();
  assert(usb.valid && !usb.approved && !ng_usb_allowed(&usb));
  assert(!ng_usb_approve("3010", 1));
  adapter("1-1", "3000\n");
  adapter("1-2", "31fd\n");
  usb = ng_usb_observe();
  assert(usb.count == 2 && !usb.valid && !ng_usb_allowed(&usb));
  assert(!ng_usb_approve("3000", 1));
  attribute("1-2", "idVendor", "ffff\n");
  const char *bad[] = {"3000", "3000\n\n", "3000junk\n", "0000\n", "ffff\n", "31FD\n"};
  for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
    adapter("1-1", bad[i]);
    usb = ng_usb_observe();
    assert(!ng_usb_allowed(&usb));
  }
  adapter("1-1", "3000\n");
  assert(ng_usb_approve("3000", 0));
  usb = ng_usb_observe();
  assert(!usb.approved && !ng_usb_allowed(&usb));
  assert(symlink("/dev/null", NG_STATE "/usb-approval.bin") == 0);
  assert(!ng_usb_approval_load(&approval));
  assert(!unlink(NG_STATE "/usb-approval.bin"));
  assert(ng_usb_approve("3000", 1));
  assert(!truncate(NG_STATE "/usb-approval.bin", 4));
  assert(!ng_usb_approval_load(&approval));
  assert(request("{\"op\":\"usb-approval\",\"enabled\":true,\"revision\":\"3000\"}"));
  assert(request("{\"op\":\"usb-approval\",\"enabled\":false,\"revision\":\"3000\"}"));
  const char *invalid[] = {
      "{\"op\":\"usb-approval\",\"revision\":\"3000\"}",
      "{\"op\":\"usb-approval\",\"enabled\":true,\"revision\":\"31fd\"}",
      "{\"op\":\"usb-approval\",\"enabled\":true,\"revision\":\"3010\"}",
      "{\"op\":\"usb-approval\",\"enabled\":true,\"revision\":\"3000\",\"interface\":\"ethernet\"}",
      "{\"op\":\"usb-approval\",\"enabled\":1,\"revision\":\"3000\"}"};
  for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++)
    assert(!request(invalid[i]));
  puts("PASS: exact USB revision consent, unchanged qualified path, malformed/duplicate devices, private approval integrity and strict request parsing; no modules or network accessed");
  return 0;
}
