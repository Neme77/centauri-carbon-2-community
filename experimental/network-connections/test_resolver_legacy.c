#define _GNU_SOURCE
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
int main(void) {
  struct rlimit core = {0, 0};
  setrlimit(RLIMIT_CORE, &core);
#ifdef _FORTIFY_SOURCE
  printf("FORTIFY_SOURCE=%d\n", _FORTIFY_SOURCE);
#else
  puts("FORTIFY_SOURCE=undefined");
#endif
  printf("PATH_MAX=%d\n", PATH_MAX);
  fflush(stdout);
  char resolved[512];
  char *result = realpath("/etc/resolv.conf", resolved);
  if (!result) { perror("realpath"); return 1; }
  puts("LEGACY_REALPATH_RETURNED");
  return 0;
}
