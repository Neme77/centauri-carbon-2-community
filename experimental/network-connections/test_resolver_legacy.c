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
  /* Same form as the shipped resolver (connections/io.h): a caller buffer must
   * hold PATH_MAX bytes for the fortified realpath(), and a 512-byte one does
   * not, so libc allocates the canonical name. */
  char *result = realpath("/etc/resolv.conf", NULL);
  if (!result) { perror("realpath"); return 1; }
  puts("LEGACY_REALPATH_RETURNED");
  free(result);
  return 0;
}
