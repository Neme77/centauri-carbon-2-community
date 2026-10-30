#define _GNU_SOURCE
#include "bootstrap.h"
#include <sys/resource.h>
static inline int ng_mark_qualified(void);
static inline int ng_adopt_legacy_rule(void);

static int ng_restore(ng_journal *journal, const char *reason, int boot);
static int ng_guard(int port) {
  char printer[12288], uds[8192];
  ng_json *m = malloc(sizeof(*m)), *u = malloc(sizeof(*u));
  if (!m || !u) {
    free(m);
    free(u);
    return 0;
  }
  int ok = 0;
  long long deadline = ng_now() + 15000;
  do {
    if (!ng_http("/api/printer", printer, sizeof(printer), port) ||
        !ng_json_parse(m, printer, strlen(printer)))
      break;
    double age;
    if (!ng_number(m, 0, "last_message_age", &age) || age < 0)
      break;
    if (age <= 5) {
      if (ng_http("/api/uds", uds, sizeof(uds), port) &&
          ng_json_parse(u, uds, strlen(uds)))
        ok = ng_idle_guard(m, u);
      break;
    }
    poll(NULL, 0, 500);
  } while (ng_now() < deadline);
  ng_erase(printer, sizeof(printer));
  ng_erase(uds, sizeof(uds));
  ng_erase(m, sizeof(*m));
  ng_erase(u, sizeof(*u));
  free(m);
  free(u);
  return ok;
}
static int ng_phase_active(const ng_journal *j) {
  return j->phase == NG_APPLYING || j->phase == NG_PENDING ||
         j->phase == NG_SAVING || j->phase == NG_RESTORING;
}
static int ng_read_journal_required(ng_journal *j, int *exists) {
  struct stat st;
  if (lstat(NG_STATE "/journal.bin", &st)) {
    *exists = 0;
    return errno == ENOENT;
  }
  *exists = 1;
  return ng_journal_load(j);
}
static int ng_self_pid(pid_t pid) {
  if (pid <= 1)
    return 0;
  char path[128], exe[512];
  snprintf(path, sizeof(path), "/proc/%ld/exe", (long)pid);
  ssize_t n = readlink(path, exe, sizeof(exe) - 1);
  if (n <= 0 || (size_t)n >= sizeof(exe) - 1)
    return 0;
  exe[n] = 0;
  return !strcmp(exe, NG_COMPONENT "/control");
}
static unsigned long long ng_pid_start(pid_t pid) {
  char path[128], text[2048];
  size_t size;
  snprintf(path, sizeof(path), "/proc/%ld/stat", (long)pid);
  if (!ng_read_file(path, text, sizeof(text), &size, 0))
    return 0;
  char *p = strrchr(text, ')');
  if (!p)
    return 0;
  p++;
  for (int field = 3; field <= 22; field++) {
    while (*p == ' ')
      p++;
    if (!*p)
      return 0;
    char *end = p;
    while (*end && *end != ' ')
      end++;
    if (field == 22) {
      char saved = *end;
      *end = 0;
      char *last;
      errno = 0;
      unsigned long long n = strtoull(p, &last, 10);
      int ok = !errno && last != p && !*last;
      *end = saved;
      return ok ? n : 0;
    }
    p = end;
  }
  return 0;
}
static int ng_worker_alive(const ng_journal *j) {
  return ng_self_pid(j->worker) && j->worker_start &&
         ng_pid_start(j->worker) == j->worker_start;
}
static int ng_ready_watch(const ng_journal *j) {
  char text[96], expected[96];
  size_t n;
  int size =
      snprintf(expected, sizeof(expected), "%d %s\n", j->watcher, j->token);
  return size > 0 && ng_self_pid(j->watcher) &&
         ng_read_file(NG_RUN "/watch.ready", text, sizeof(text), &n, 1) &&
         n == (size_t)size && !memcmp(text, expected, n);
}
static int ng_watch_loop(const char *token) {
  if (!ng_token_valid(token))
    return 0;
  signal(SIGHUP, SIG_IGN);
  if (setsid() < 0)
    return 0;
  long long starting = ng_now() + 3000;
  ng_journal j;
  while (ng_now() < starting) {
    if (ng_journal_load(&j) && !strcmp(j.token, token) &&
        j.watcher == (int)getpid())
      break;
    poll(NULL, 0, 50);
  }
  if (!ng_journal_load(&j) || strcmp(j.token, token) ||
      j.watcher != (int)getpid())
    return 0;
  char ready[96];
  int n = snprintf(ready, sizeof(ready), "%ld %s\n", (long)getpid(), token);
  if (n < 0 || (size_t)n >= sizeof(ready) ||
      !ng_atomic(NG_RUN, "watch.ready", ready, (size_t)n))
    return 0;
  while (1) {
    if (!ng_journal_load(&j) || strcmp(j.token, token))
      return 0;
    if (!ng_phase_active(&j))
      return 1;
    if (j.phase == NG_RESTORING) {
      if (ng_worker_alive(&j)) {
        poll(NULL, 0, 500);
        continue;
      }
      int repair_lock = ng_lock("operation.lock", 5);
      if (repair_lock < 0)
        return 0;
      int repaired = ng_journal_load(&j) && !strcmp(j.token, token) &&
                     ng_restore(&j, "recovery_interrupted", 0);
      close(repair_lock);
      return repaired;
    }
    char boot[40];
    int bad_clock = !ng_boot_id(boot) || strcmp(boot, j.boot) || ng_now() < 0;
    if (bad_clock || ng_now() >= j.deadline) {
      if (ng_worker_alive(&j) && getpgid(j.worker) == j.worker) {
        kill(-j.worker, SIGTERM);
        poll(NULL, 0, 200);
        if (ng_worker_alive(&j) && getpgid(j.worker) == j.worker)
          kill(-j.worker, SIGKILL);
      }
      int lock = ng_lock("operation.lock", 5);
      if (lock < 0) {
        poll(NULL, 0, 250);
        continue;
      }
      if (!ng_journal_load(&j) || strcmp(j.token, token)) {
        close(lock);
        return 0;
      }
      if (!ng_phase_active(&j)) {
        close(lock);
        return 1;
      }
      int ok = ng_restore(
          &j, bad_clock ? "clock_changed" : "confirmation_timeout", 0);
      close(lock);
      return ok;
    }
    poll(NULL, 0, 500);
  }
}
static int ng_start_watch(ng_journal *j) {
  pid_t watcher = fork();
  if (watcher < 0)
    return 0;
  if (!watcher) {
    int null = open("/dev/null", O_RDWR);
    if (null < 0 || dup2(null, 0) < 0 || dup2(null, 1) < 0 || dup2(null, 2) < 0)
      _exit(126);
    long maximum = sysconf(_SC_OPEN_MAX);
    if (maximum < 0 || maximum > 65536)
      maximum = 65536;
    for (int fd = 3; fd < maximum; fd++)
      close(fd);
    char *args[] = {"control", "--watch", j->token, NULL};
    char *env[] = {"PATH=/usr/sbin:/usr/bin:/sbin:/bin", NULL};
    execve(NG_COMPONENT "/control", args, env);
    _exit(127);
  }
  j->watcher = (int)watcher;
  if (!ng_journal_save(j)) {
    kill(watcher, SIGTERM);
    return 0;
  }
  long long deadline = ng_now() + 3000;
  while (ng_now() < deadline) {
    if (ng_ready_watch(j))
      return 1;
    int status;
    if (waitpid(watcher, &status, WNOHANG) == watcher)
      return 0;
    poll(NULL, 0, 50);
  }
  kill(watcher, SIGTERM);
  return 0;
}
static int ng_route_proof(int iface, const char *local, const char *peer) {
  if (!ng_ipv4(local, NULL) || !ng_ipv4(peer, NULL))
    return 0;
  char actual[16];
  if (!ng_live_ip(iface, actual) || strcmp(actual, local))
    return 0;
  char route[1024];
  if (!ng_command("ip", route, sizeof(route), 2, "-4", "route", "get", peer,
                  "from", local, NULL))
    return 0;
  char needle[40];
  snprintf(needle, sizeof(needle), " dev %s", ng_name(iface));
  char *at = strstr(route, needle);
  return at && (at[strlen(needle)] == 0 ||
                isspace((unsigned char)at[strlen(needle)]));
}
static int ng_confirm(ng_journal *j, const char *token, const char *local,
                      const char *peer, int port) {
  if (strcmp(j->token, token))
    return 0;
  if (j->phase == NG_IDLE && !j->error[0])
    return 1;
  j->worker = (int)getpid();
  j->worker_start = ng_pid_start(getpid());
  if (!ng_journal_save(j))
    return 0;
  if (j->phase != NG_PENDING || ng_now() >= j->deadline ||
      strcmp(local, j->expected_ip) || !ng_ready_watch(j) || !ng_guard(port) ||
      !ng_route_proof(j->proof_interface, local, peer))
    return 0;
  if (ng_now() >= j->deadline)
    return 0;
  if (j->proof_interface == NG_WIFI && !ng_wifi_connected(j->target_network))
    return 0;
  if (j->desired.ethernet_support && !ng_runtime_ready())
    return 0;
  j->worker = (int)getpid();
  j->worker_start = ng_pid_start(getpid());
  j->phase = NG_SAVING;
  if (!ng_journal_save(j))
    return 0;
  if ((j->op == NG_WIFI_CONNECT || j->op == NG_WIFI_SAVED) &&
      !ng_wifi_commit(j)) {
    ng_restore(j, "wifi_save_failed", 0);
    return 0;
  }
  if (!ng_blob_write(NG_STATE, "config.bin", &j->desired, sizeof(j->desired))) {
    ng_restore(j, "configuration_save_failed", 0);
    return 0;
  }
  unlink(NG_RUN "/boot-config.bin");
  j->phase = NG_IDLE;
  j->deadline = 0;
  j->error[0] = 0;
  if (!ng_journal_save(j))
    return 0;
  ng_event("connection_confirmed");
  return 1;
}
static int ng_restore(ng_journal *j, const char *reason, int boot) {
  j->worker = (int)getpid();
  j->worker_start = ng_pid_start(getpid());
  j->phase = NG_RESTORING;
  ng_copy(j->error, sizeof(j->error), reason);
  if (!ng_journal_save(j))
    return 0;
  ng_event("restoring_previous_connection");
  int wifi = 1;
  if (j->op == NG_WIFI_CONNECT || j->op == NG_WIFI_SAVED) {
    ng_dhcp_stop(NG_WIFI);
    wifi = ng_wifi_restore(j, 1);
  }
  if (boot && !ng_boot_id(j->boot))
    return 0;
  int ok = wifi &&
           (j->previous.managed ? ng_apply_config(&j->previous, j, !boot, boot)
                                : ng_restore_native_wifi(j, boot));
  if (ok && j->resolver_size > 0)
    ok = ng_resolver_write(j->resolver, (size_t)j->resolver_size);
  if (ok)
    ok = ng_blob_write(NG_STATE, "config.bin", &j->previous,
                       sizeof(j->previous));
  j->phase = ok ? NG_IDLE : NG_FAILED;
  j->deadline = 0;
  if (!ok)
    strcpy(j->error, "recovery_incomplete");
  if (!ng_journal_save(j))
    ok = 0;
  ng_event(ok ? "previous_connection_restored" : "recovery_incomplete");
  return ok;
}
static int ng_error(const char *message) {
  ng_event(message);
  printf("{\"accepted\":false,\"error\":");
  ng_json_quote(stdout, message);
  puts("}");
  return 1;
}
static int ng_request_run(ng_request *r, const char *local, const char *peer,
                          int port) {
  if (!ng_directory(NG_STATE) || !ng_directory(NG_RUN))
    return ng_error("private_state_unavailable");
  int lock = ng_lock("operation.lock", 0);
  if (lock < 0)
    return ng_error("network_busy");
  ng_journal j;
  int exists = 0;
  int result = 1;
  if (!ng_read_journal_required(&j, &exists)) {
    result = ng_error("journal_invalid");
    goto done;
  }
  if (r->op == NG_CONFIRM) {
    result = exists && ng_confirm(&j, r->token, local, peer, port)
                 ? 0
                 : ng_error("confirmation_not_valid");
    goto done;
  }
  if (r->op == NG_CANCEL) {
    result = exists && !strcmp(j.token, r->token) &&
                     (ng_phase_active(&j) || j.phase == NG_FAILED) &&
                     ng_restore(&j, "cancelled", 0)
                 ? 0
                 : ng_error("recovery_not_available");
    goto done;
  }
  if (exists && ng_phase_active(&j)) {
    result = ng_error("network_busy");
    goto done;
  }
  if (r->op == NG_WIFI_FORGET) {
    result = ng_error("unsupported_action");
    goto done;
  }
  if (!ng_check_profile("wifi")) {
    result = ng_error("native_wifi_profile");
    goto done;
  }
  if (!ng_guard(port)) {
    result = ng_error("printer_not_ready");
    goto done;
  }
  ng_config previous, desired;
  int configured;
  if (!ng_effective_config(&previous) ||
      !ng_config_load(&desired, &configured) ||
      !ng_propose(&previous, r, &desired)) {
    result = ng_error("invalid_configuration");
    goto done;
  }
  if (desired.ethernet_support && !ng_check_profile("ethernet")) {
    result = ng_error("ethernet_profile");
    goto done;
  }
  memset(&j, 0, sizeof(j));
  j.schema = NG_SCHEMA;
  j.phase = NG_APPLYING;
  j.op = r->op;
  j.worker = (int)getpid();
  j.worker_start = ng_pid_start(getpid());
  j.port = port;
  j.previous = previous;
  j.desired = desired;
  j.previous_network = j.created_network = j.target_network = -1;
  j.proof_interface = desired.selected;
  if (r->op == NG_IP && desired.link[r->iface].enabled)
    j.proof_interface = r->iface;
  if (r->op == NG_INTERFACE && r->enabled)
    j.proof_interface = r->iface;
  if (r->op == NG_SUPPORT && r->enabled)
    j.proof_interface = NG_ETHERNET;
  if (r->op == NG_WIFI_CONNECT || r->op == NG_WIFI_SAVED) {
    j.proof_interface = NG_WIFI;
    if (!ng_wifi_snapshot(&j)) {
      result = ng_error("wifi_profiles_unavailable");
      goto done;
    }
  }
  if (r->op == NG_WIFI_SAVED && r->network_id == j.previous_network) {
    j.desired.link[NG_WIFI] = previous.link[NG_WIFI];
    j.desired.link[NG_WIFI].enabled = 1;
  }
  char resolver_path[512];
  size_t resolver_size;
  if (!ng_resolver_path(resolver_path) ||
      !ng_read_file(resolver_path, j.resolver, sizeof(j.resolver),
                    &resolver_size, 0) ||
      !ng_boot_id(j.boot) || !ng_nonce(j.token) ||
      ng_now() > LLONG_MAX - 120000) {
    result = ng_error("recovery_snapshot_unavailable");
    goto done;
  }
  if (r->op == NG_IP && r->iface == NG_WIFI && r->method == NG_STATIC) {
    if (!ng_wifi_ssid(j.desired.link[NG_WIFI].ssid)) {
      result = ng_error("wifi_network_required");
      goto done;
    }
  }
  j.resolver_size = (int)resolver_size;
  j.deadline = ng_now() + 120000;
  ng_copy(j.previous_ip, sizeof(j.previous_ip), local);
  if (!ng_journal_save(&j) || !ng_start_watch(&j)) {
    j.phase = NG_FAILED;
    strcpy(j.error, "watchdog_unavailable");
    ng_journal_save(&j);
    result = ng_error("watchdog_unavailable");
    goto done;
  }
  ng_event("applying_connection");
  unlink(NG_RUN "/last-command.txt");
  if (!ng_mark_qualified()) {
    ng_restore(&j, "qualification_marker_failed", 0);
    result = ng_error("qualification_marker_failed");
    goto done;
  }
  if (desired.ethernet_support && !ng_runtime_enable(&j)) {
    ng_restore(&j, "native_activation_failed", 0);
    result = ng_error("native_activation_failed");
    goto done;
  }
  if (r->op == NG_WIFI_CONNECT || r->op == NG_WIFI_SAVED) {
    ng_dhcp_stop(NG_WIFI);
    if (!ng_wifi_choose(&j, r)) {
      ng_erase(r->password, sizeof(r->password));
      ng_restore(&j, "wifi_connection_failed", 0);
      result = ng_error("wifi_connection_failed");
      goto done;
    }
  }
  ng_erase(r->password, sizeof(r->password));
  if (!ng_apply_config(&j.desired, &j, 0, 0) || !ng_adopt_legacy_rule() ||
      !ng_live_ip(j.proof_interface, j.expected_ip)) {
    ng_restore(&j, "connection_apply_failed", 0);
    result = ng_error("connection_apply_failed");
    goto done;
  }
  j.phase = NG_PENDING;
  j.deadline = ng_now() + 120000;
  if (!ng_journal_save(&j)) {
    ng_restore(&j, "journal_write_failed", 0);
    result = ng_error("journal_write_failed");
    goto done;
  }
  if (r->op != NG_IP && r->op != NG_WIFI_CONNECT && r->op != NG_WIFI_SAVED &&
      !strcmp(local, j.expected_ip) &&
      ng_route_proof(j.proof_interface, local, peer)) {
    if (!ng_confirm(&j, j.token, local, peer, port)) {
      ng_restore(&j, "confirmation_failed", 0);
      result = ng_error("confirmation_failed");
      goto done;
    }
  }
  result = 0;
done:
  ng_erase(r->password, sizeof(r->password));
  close(lock);
  return result;
}
#include "cli.h"
#ifndef NG_NO_MAIN
int main(int argc, char **argv) {
  umask(077);
  if (geteuid() != 0)
    return 1;
  if (argc == 2 && !strcmp(argv[1], "--managed"))
    return ng_managed() ? 0 : 1;
  if (argc == 2 && !strcmp(argv[1], "--busy")) {
    ng_journal j;
    int exists;
    return !ng_read_journal_required(&j, &exists) || ng_public_active() ? 0 : 1;
  }
  if (argc == 2 && !strcmp(argv[1], "--legacy-idle"))
    return ng_legacy_restore() ? 0 : 1;
  if (argc == 2 && !strcmp(argv[1], "--enabled")) {
    ng_config c;
    return ng_effective_config(&c) && c.ethernet_support &&
                   ng_check_profile("boot")
               ? 0
               : 1;
  }
  if (argc == 2 && !strcmp(argv[1], "--ensure"))
    return ng_ensure_boot() ? 0 : 1;
  if (argc == 2 && !strcmp(argv[1], "--boot"))
    return ng_boot_run() ? 0 : 1;
  if (argc == 2 && !strcmp(argv[1], "--seed"))
    return ng_seed_installation() ? 0 : 1;
  if (argc == 2 && !strcmp(argv[1], "--networks"))
    return ng_cache_networks(0) ? 0 : 1;
  if (argc == 2 && !strcmp(argv[1], "--scan"))
    return ng_cache_networks(1) ? 0 : 1;
  if (argc == 2 && !strcmp(argv[1], "--subnets"))
    return ng_local_networks() ? 0 : 1;
  if (argc == 2 && !strcmp(argv[1], "--status")) {
    ng_public_json(stdout, 1, 1, 0, 0);
    return 0;
  }
  if (argc == 3 && !strcmp(argv[1], "--watch"))
    return ng_watch_loop(argv[2]) ? 0 : 1;
  if (argc == 3 && !strcmp(argv[1], "--guard")) {
    char *end;
    long port = strtol(argv[2], &end, 10);
    return !*end && port > 0 && port <= 65535 && ng_guard((int)port) ? 0 : 1;
  }
  if (argc == 3 && !strcmp(argv[1], "--watch-ready")) {
    ng_journal j;
    return ng_token_valid(argv[2]) && ng_journal_load(&j) &&
                   !strcmp(j.token, argv[2]) && ng_ready_watch(&j)
               ? 0
               : 1;
  }
  if (argc == 3 && !strcmp(argv[1], "--lease"))
    return ng_dhcp_event(argv[2]) ? 0 : 1;
  if (argc == 4 && !strcmp(argv[1], "--client"))
    return ng_client_action(argv[2], ng_interface_arg(argv[3])) ? 0 : 1;
  if (argc == 3 && !strcmp(argv[1], "--hotplug"))
    return ng_client_action("start", ng_interface_arg(argv[2])) ? 0 : 1;
  if (argc == 5 && !strcmp(argv[1], "--native-spawn")) {
    if (strcmp(argv[4], "0") && strcmp(argv[4], "1"))
      return 1;
    return ng_native_spawn(argv[2], argv[3], argv[4][0] - '0') ? 0 : 1;
  }
  if (argc == 5 && !strcmp(argv[1], "--request")) {
    char *end;
    long port = strtol(argv[4], &end, 10);
    if (*end || port < 1 || port > 65535)
      return 1;
    return ng_stdin_request(argv[2], argv[3], (int)port);
  }
  return 1;
}
#endif
