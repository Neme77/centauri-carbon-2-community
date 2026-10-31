#define NG_NO_MAIN
#include "manager.c"
#include <assert.h>
int main(int argc, char **argv) {
  (void)ng_request_run;
  (void)ng_watch_loop;
  assert(argc == 2);
  assert(ng_directory(NG_STATE));
  assert(ng_directory(NG_RUN));
  int expected = !strcmp(argv[1], "success");
  int result = ng_wifi_reconnect_ready(expected ? 3000 : 400);
  assert(result == expected);
  puts(expected ? "WIFI_ENABLE_READY" : "WIFI_ENABLE_TIMEOUT");
  return 0;
}
