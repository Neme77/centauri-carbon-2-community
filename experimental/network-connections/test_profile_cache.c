#define NG_NO_MAIN
#include "manager.c"
#include <assert.h>
static int calls(void) {
  char b[256];
  size_t n;
  if (!ng_read_file(NG_STATE "/calls", b, sizeof(b), &n, 0))
    return 0;
  return (int)n;
}
static int verifier(const char *kind) {
  (void)kind;
  int fd = open(NG_STATE "/calls", O_WRONLY | O_CREAT | O_APPEND, 0600);
  assert(fd >= 0 && write(fd, "x", 1) == 1);
  close(fd);
  poll(NULL, 0, 30);
  char b[64];
  size_t n;
  int ok = ng_read_file(NG_COMPONENT "/payload.bin", b, sizeof(b), &n, 0) &&
           !strcmp(b, "valid");
  if (!access(NG_STATE "/mutate", F_OK))
    assert(ng_atomic(NG_COMPONENT, "native.fixture", "changed", 7));
  return ok;
}
int main(void) {
  (void)ng_request_run;
  (void)ng_watch_loop;
  assert(ng_directory(NG_STATE) && ng_directory(NG_RUN));
  assert(ng_profile_cached("boot", verifier));
  assert(calls() == 1);
  assert(ng_profile_cached("ethernet", verifier));
  assert(calls() == 1);
  assert(ng_profile_cached("wifi", verifier));
  assert(calls() == 2);
  assert(!chmod(NG_COMPONENT "/payload.bin", 0640));
  assert(ng_profile_cached("boot", verifier));
  assert(calls() == 3);
  assert(ng_atomic(NG_COMPONENT, "payload.bin", "bad!!", 5));
  assert(!ng_profile_cached("boot", verifier));
  assert(calls() == 4);
  assert(!ng_profile_cached("boot", verifier));
  assert(calls() == 5);
  assert(ng_atomic(NG_COMPONENT, "payload.bin", "valid", 5));
  assert(ng_profile_cached("boot", verifier));
  assert(calls() == 6);
  ng_profile_proof saved;
  assert(ng_blob_read(NG_RUN, "profile-full.bin", &saved, sizeof(saved)));
  saved.boot[0] = saved.boot[0] == 'a' ? 'b' : 'a';
  assert(ng_blob_write(NG_RUN, "profile-full.bin", &saved, sizeof(saved)));
  assert(ng_profile_cached("boot", verifier));
  assert(calls() == 7);
  assert(ng_atomic(NG_COMPONENT, "native.fixture", "new", 3));
  int before = calls();
  pid_t children[2];
  for (int i = 0; i < 2; i++) {
    children[i] = fork();
    assert(children[i] >= 0);
    if (!children[i])
      _exit(ng_profile_cached("boot", verifier) ? 0 : 1);
  }
  for (int i = 0; i < 2; i++) {
    int status;
    assert(waitpid(children[i], &status, 0) == children[i]);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  }
  assert(calls() == before + 1);
  assert(ng_atomic(NG_COMPONENT, "payload.bin", "valid", 5));
  assert(ng_atomic(NG_STATE, "mutate", "1", 1));
  assert(!ng_profile_cached("boot", verifier));
  assert(!unlink(NG_STATE "/mutate"));
  assert(ng_profile_cached("boot", verifier));
  puts("PASS: one full verification, shared boot/ethernet proof, separate "
       "Wi-Fi, input/mode/boot invalidation, failed-check refusal, concurrent "
       "coalescing and mutation-during-check refusal");
  return 0;
}
