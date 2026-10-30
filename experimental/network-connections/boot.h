#ifndef CC2_CONNECTIONS_BOOT_H
#define CC2_CONNECTIONS_BOOT_H
/* Included after the transaction functions. No long-lived manager daemon. */
static inline int ng_wifi_compatible(void) {
  char sha[65];
  return ng_hash_file("/bin/wifi_daemon", sha) &&
         !strcmp(sha, "8e19a620921c6f4f21bde910cc3d960af5400d223493041eb96f1e92"
                      "1dcbe3c1") &&
         ng_hash_file("/etc/init.d/printer", sha) &&
         !strcmp(sha, "e7537298955e23b7a03cab4c8d7bd241c1dfcc070883cdf51c4889cd"
                      "255d1bc4");
}
static inline int ng_mark_qualified(void) {
  char boot[40];
  return ng_boot_id(boot) && ng_atomic(NG_RUN, "qualified", boot, 36);
}
static inline int ng_qualified_this_boot(void) {
  char boot[40], saved[40];
  size_t n;
  return ng_boot_id(boot) &&
         ng_read_file(NG_RUN "/qualified", saved, sizeof(saved), &n, 1) &&
         n == 36 && !memcmp(boot, saved, 36) && ng_wifi_compatible();
}
static inline int ng_managed(void) {
  ng_config c;
  return ng_qualified_this_boot() && ng_effective_config(&c) && c.managed;
}
static inline int ng_raw_runtime_markers(void) {
  const char *roles[] = {"camera", "printer"},
             *names[] = {"ai_camera", "elegoo_printer"};
  for (int role = 0; role < 2; role++) {
    DIR *d = opendir("/proc");
    if (!d)
      return 0;
    struct dirent *e;
    long pid = 0;
    int count = 0;
    while ((e = readdir(d))) {
      char *end;
      long n = strtol(e->d_name, &end, 10);
      if (!*e->d_name || *end || n <= 1)
        continue;
      char path[128], comm[64];
      size_t size;
      snprintf(path, sizeof(path), "/proc/%ld/comm", n);
      if (ng_read_file(path, comm, sizeof(comm), &size, 0)) {
        comm[strcspn(comm, "\n")] = 0;
        if (!strcmp(comm, names[role])) {
          pid = n;
          count++;
        }
      }
    }
    closedir(d);
    if (count != 1)
      return 0;
    char path[256], marker[512], expected[128];
    size_t size;
    snprintf(path, sizeof(path), "/tmp/cc2-network-compat-r1.%s.status",
             roles[role]);
    snprintf(expected, sizeof(expected), "pid=%ld role=%s patched=1", pid,
             roles[role]);
    if (!ng_read_file(path, marker, sizeof(marker), &size, 0) ||
        !strstr(marker, expected))
      return 0;
  }
  return 1;
}
static inline int ng_wpa_available(void) {
  char reply[64];
  return ng_wpa("PING", reply, sizeof(reply), 250) && !strcmp(reply, "PONG\n");
}
static inline int ng_legacy_restore(void) {
  /* Never leave the old unconditional timer running behind a permanent owner.
   * This is read-only: finish the old session before installing this manager.
   */
  if (access("/tmp/cc2-network-compat-r1/active", F_OK))
    return errno == ENOENT;
  char digest[65];
  if (!ng_hash_file("/tmp/cc2-network-compat-r1/manage.sh", digest) ||
      strcmp(
          digest,
          "5f548f24b150eda793a2e7c712a8e6f7a601f2238517b591660ed2df753df0c4"))
    return 0;
  char active[256];
  size_t size;
  if (!ng_read_file("/tmp/cc2-network-compat-r1/active", active, sizeof(active),
                    &size, 1))
    return 0;
  active[strcspn(active, "\n")] = 0;
  const char *prefix = "/tmp/cc2-network-compat-r1/state.";
  if (strncmp(active, prefix, strlen(prefix)) ||
      strchr(active + strlen(prefix), '/'))
    return 0;
  char marker[512];
  snprintf(marker, sizeof(marker), "%s/restored", active);
  return !access(marker, F_OK);
}
static inline int ng_adopt_legacy_rule(void) {
  char rules[8192];
  if (!ng_command("ip", rules, sizeof(rules), 2, "-4", "rule", "show", NULL))
    return 0;
  int found = 0;
  for (char *p = rules; *p;) {
    char *end = strchr(p, '\n');
    if (end)
      *end = 0;
    if (!strncmp(p, "22040:", 6))
      return 0;
    if (!strncmp(p, "22030:", 6)) {
      int priority, table;
      char source[40], extra;
      if (sscanf(p, "%d: from %39s lookup %d %c", &priority, source, &table,
                 &extra) != 3 ||
          table != 200 ||
          (strcmp(source, "192.168.1.107") &&
           strcmp(source, "192.168.1.107/32")))
        return 0;
      found++;
    }
    if (!end)
      break;
    p = end + 1;
  }
  if (!found)
    return 1;
  if (found != 1)
    return 0;
  char ip[16];
  if (!ng_live_ip(NG_ETHERNET, ip) || strcmp(ip, "192.168.1.107"))
    return 0;
  /* The rule is removed only after new source routing is usable. Leave table
   * 200 untouched: an inactive legacy table cannot affect other routing owners.
   */
  if (ng_rule_present(NG_ETH_TABLE, "192.168.1.107/32", NG_ETH_TABLE) != 1)
    return 0;
  return ng_command("ip", NULL, 0, 2, "-4", "rule", "del", "priority", "22030",
                    "from", "192.168.1.107/32", "table", "200", NULL);
}
static inline int ng_seed_installation(void) {
  if (!ng_directory(NG_STATE) || !ng_directory(NG_RUN) ||
      !ng_check_profile("wifi"))
    return 0;
  ng_config c;
  int configured;
  if (!ng_config_load(&c, &configured))
    return 0;
  if (configured)
    return 1;
  if (!ng_legacy_restore())
    return 0;
  ng_defaults(&c);
  ng_seed_wifi();
  return ng_blob_write(NG_STATE, "config.bin", &c, sizeof(c));
}
static inline int ng_boot_run(void) {
  if (!ng_directory(NG_RUN) || !ng_directory(NG_STATE))
    return 0;
  int lock = ng_lock("operation.lock", 0);
  if (lock < 0)
    return 1;
  char boot[40];
  int ok = 0;
  if (!ng_boot_id(boot))
    goto finish;
  ng_journal j;
  int exists;
  if (!ng_read_journal_required(&j, &exists))
    goto finish;
  if (exists && ng_phase_active(&j)) {
    if (!strcmp(j.boot, boot) && ng_ready_watch(&j)) {
      ok = 1;
      goto finish;
    }
    long long wait = ng_now() + 45000;
    while (!ng_wpa_available() && ng_now() < wait)
      poll(NULL, 0, 250);
    if (!ng_wpa_available()) {
      ng_event("boot_native_wifi_unavailable");
      goto finish;
    }
    ok = ng_restore(&j, "interrupted_restart", 1);
    if (!ok)
      goto finish;
  }
  ng_config config;
  int configured;
  if (!ng_config_load(&config, &configured))
    goto finish;
  if (!configured || !config.managed) {
    ok = 1;
    goto finish;
  }
  if (!ng_check_profile(config.ethernet_support ? "boot" : "wifi")) {
    ng_event("boot_profile_unsupported");
    goto finish;
  }
  if (config.ethernet_support) {
    long long wait = ng_now() + 80000;
    while (!ng_raw_runtime_markers() && ng_now() < wait)
      poll(NULL, 0, 500);
    if (!ng_raw_runtime_markers() || !ng_runtime_ready()) {
      ng_event("boot_native_compatibility_unavailable");
      goto wifi_recovery;
    }
  }
  if (!ng_mark_qualified())
    goto finish;
  {
    long long wait = ng_now() + 45000;
    while (!ng_wpa_available() && ng_now() < wait)
      poll(NULL, 0, 250);
  }
  if (ng_apply_config(&config, NULL, 0, 1)) {
    ok = 1;
    goto finish;
  }
wifi_recovery:
  /* Startup recovery preserves the saved preference but does not switch back
   * later when signal quality or cable state changes. Commuta remains manual.
   */
  config.selected = NG_WIFI;
  config.link[NG_WIFI].enabled = 1;
  config.link[NG_ETHERNET].enabled = 0;
  config.managed = 1;
  if (!ng_blob_write(NG_RUN, "boot-config.bin", &config, sizeof(config)) ||
      !ng_mark_qualified())
    goto finish;
  ok = ng_apply_config(&config, NULL, 0, 1);
  ng_event(ok ? "boot_wifi_recovery" : "boot_recovery_incomplete");
finish:
  if (ok)
    ng_atomic(NG_RUN, "boot.complete", boot, 36);
  close(lock);
  return ok;
}
static inline int ng_ensure_boot(void) {
  ng_config c;
  int configured;
  if (!ng_config_load(&c, &configured))
    return 0;
  ng_journal j;
  int exists;
  if (!ng_read_journal_required(&j, &exists))
    return 0;
  if ((!configured || !c.managed) && !(exists && ng_phase_active(&j)))
    return 1;
  if (!ng_directory(NG_RUN))
    return 0;
  if (exists && ng_phase_active(&j)) {
    char boot[40];
    if (ng_boot_id(boot) && !strcmp(boot, j.boot) && ng_ready_watch(&j))
      return 1;
  }
  char boot[40], done[40];
  size_t n;
  if (ng_boot_id(boot) &&
      ng_read_file(NG_RUN "/boot.complete", done, sizeof(done), &n, 1) &&
      n == 36 && !memcmp(boot, done, 36))
    return 1;
  pid_t pid = fork();
  if (pid < 0)
    return 0;
  if (!pid) {
    if (setsid() < 0)
      _exit(126);
    signal(SIGHUP, SIG_IGN);
    int null = open("/dev/null", O_RDWR);
    if (null < 0 || dup2(null, 0) < 0 || dup2(null, 1) < 0 || dup2(null, 2) < 0)
      _exit(126);
    long max = sysconf(_SC_OPEN_MAX);
    if (max < 0 || max > 65536)
      max = 65536;
    for (int i = 3; i < max; i++)
      close(i);
    char *args[] = {"control", "--boot", NULL};
    char *env[] = {"PATH=/usr/sbin:/usr/bin:/sbin:/bin", NULL};
    execve(NG_COMPONENT "/control", args, env);
    _exit(127);
  }
  return 1;
}
#endif
