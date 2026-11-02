#define _GNU_SOURCE
#define if_nametoindex fixture_if_nametoindex
#include "bootstrap.h"
#include <assert.h>

unsigned int fixture_if_nametoindex(const char *name) {
  return !strcmp(name, "cc2lan0") &&
                 !access(NG_MODULE_SYSFS "/cc2_ue300_r8152", F_OK)
             ? 3u
             : 0u;
}
int main(int argc, char **argv) {
  assert(argc == 3);
  assert(ng_directory(NG_STATE) && ng_directory(NG_RUN));
  ng_usb_profile usb = ng_usb_observe();
  if (!usb.qualified && strcmp(argv[1], "blocked"))
    assert(ng_usb_approve("3000", 1));
  int result = ng_load_drivers();
  assert(result == atoi(argv[2]));
  printf("PASS: actual module loader %s; only fixture commands executed\n", argv[1]);
  return 0;
}
