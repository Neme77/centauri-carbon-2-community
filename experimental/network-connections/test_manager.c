#define NG_NO_MAIN
#include "manager.c"
#include <assert.h>

static void fixture_journal(ng_journal *j) {
  memset(j, 0, sizeof(*j));
  j->schema = NG_SCHEMA;
  j->phase = NG_IDLE;
  j->op = NG_SELECT;
  j->port = 8081;
  j->proof_interface = NG_WIFI;
  ng_defaults(&j->previous);
  j->desired = j->previous;
  j->previous_network = j->target_network = j->created_network = -1;
  assert(ng_boot_id(j->boot));
  assert(ng_nonce(j->token));
}
int main(int argc, char **argv) {
  (void)ng_request_run;
  (void)ng_stdin_request;
  if (argc == 3 && !strcmp(argv[1], "--watch"))
    return ng_watch_loop(argv[2]) ? 0 : 1;
  if (argc == 2 && !strcmp(argv[1], "--wifi-restore")) {
    ng_journal j;
    fixture_journal(&j);
    j.op = NG_WIFI_CONNECT;
    j.previous_network = 4;
    j.created_network = 8;
    j.target_network = 8;
    j.created_priority = 12;
    strcpy(j.target_ssid, "New fixture");
    j.profile_count = 2;
    j.profiles[0] = (ng_profile){.id = 4, .enabled = 1, .priority = 1};
    strcpy(j.profiles[0].ssid, "Old fixture");
    j.profiles[1] = (ng_profile){.id = 7, .enabled = 0, .priority = 0};
    strcpy(j.profiles[1].ssid, "Other fixture");
    /* Simulate a journal from the previous boot: numeric IDs cannot be trusted.
     */
    strcpy(j.boot, "00000000-0000-0000-0000-000000000000");
    return ng_wifi_restore(&j, 1) ? 0 : 1;
  }
  assert(ng_directory(NG_STATE));
  assert(ng_directory(NG_RUN));
  ng_journal j;
  fixture_journal(&j);
  assert(ng_journal_save(&j));
  assert(!ng_public_active());
  j.worker = getpid();
  j.worker_start = ng_pid_start(getpid());
  assert(j.worker_start && ng_worker_alive(&j));
  j.worker_start++;
  assert(!ng_worker_alive(&j));
  j.worker_start--;
  j.phase = NG_PENDING;
  j.deadline = ng_now() + 10000;
  pid_t child = fork();
  assert(child >= 0);
  if (!child) {
    char *args[] = {NG_COMPONENT "/control", "--watch", j.token, NULL};
    execv(args[0], args);
    _exit(127);
  }
  j.watcher = child;
  assert(ng_journal_save(&j));
  long long end = ng_now() + 4000;
  while (!ng_ready_watch(&j) && ng_now() < end)
    poll(NULL, 0, 20);
  assert(ng_ready_watch(&j));
  assert(ng_public_active());
  j.phase = NG_IDLE;
  j.deadline = 0;
  assert(ng_journal_save(&j));
  int status = 0, done = 0;
  end = ng_now() + 3000;
  while (ng_now() < end) {
    if (waitpid(child, &status, WNOHANG) == child) {
      done = 1;
      break;
    }
    poll(NULL, 0, 20);
  }
  assert(done && WIFEXITED(status) && WEXITSTATUS(status) == 0);
  assert(!ng_public_active());
  /* Confirmed state has no remaining timed worker or future expiry. */
  ng_journal saved;
  assert(ng_journal_load(&saved) && saved.deadline == 0 &&
         saved.phase == NG_IDLE);
  char output[16384];
  FILE *out = fmemopen(output, sizeof(output), "w");
  assert(out);
  ng_public_json(out, 1, 1, 0, 0);
  assert(!fclose(out));
  assert(strstr(output, "\"temporary\":false") &&
         strstr(output, "\"pending\":null"));
  assert(!strstr(output, "password"));
  int lock = ng_lock("operation.lock", 0);
  assert(lock >= 0);
  assert(ng_public_active());
  close(lock);
  assert(!ng_public_active());
  int corrupt = open(NG_STATE "/journal.bin", O_WRONLY);
  assert(corrupt >= 0);
  assert(write(corrupt, "bad", 3) == 3);
  close(corrupt);
  out = fmemopen(output, sizeof(output), "w");
  assert(out);
  ng_public_json(out, 1, 1, 0, 0);
  assert(!fclose(out));
  assert(strstr(output, "\"state_valid\":false"));
  puts("PASS: PID identity, independent confirmation watcher, permanent "
       "commit, lock visibility and corrupted-state refusal");
  return 0;
}
