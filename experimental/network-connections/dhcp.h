#ifndef CC2_CONNECTIONS_DHCP_H
#define CC2_CONNECTIONS_DHCP_H
#include "platform.h"
static inline void ng_dhcp_path(int iface, const char *suffix, char out[512]) {
  snprintf(out, 512, "%s/%s.%s", NG_RUN, iface == NG_WIFI ? "wifi" : "ethernet",
           suffix);
}
/* Return 1 only for a native/owned DHCP client on this exact interface. No
 * global process names, shell grep matches or cross-interface termination. */
static inline int ng_dhcp_matches(pid_t pid, int iface, int owned) {
  if (pid <= 1)
    return 0;
  char path[128];
  snprintf(path, sizeof(path), "/proc/%ld", (long)pid);
  struct stat st;
  if (stat(path, &st) || st.st_uid != ng_owner())
    return 0;
  snprintf(path, sizeof(path), "/proc/%ld/cmdline", (long)pid);
  char command[4096];
  size_t size;
  if (!ng_read_file(path, command, sizeof(command), &size, 0) || !size ||
      command[size - 1])
    return 0;
  const char *args[48];
  int count = 0;
  for (size_t at = 0; at < size;) {
    if (count >= 48)
      return 0;
    size_t n = strnlen(command + at, size - at);
    if (n == size - at)
      return 0;
    args[count++] = command + at;
    at += n + 1;
  }
  if (!count)
    return 0;
  const char *name = strrchr(args[0], '/');
  name = name ? name + 1 : args[0];
  if (strcmp(name, "udhcpc"))
    return 0;
  char expected_pid[512];
  ng_dhcp_path(iface, "pid", expected_pid);
  int matches = 0, script = 0, pidfile = 0;
  for (int i = 1; i < count; i++) {
    if (!strcmp(args[i], "-i") && i + 1 < count) {
      if (matches++ || strcmp(args[++i], ng_name(iface)))
        return 0;
    } else if (!strcmp(args[i], "-s") && i + 1 < count) {
      const char *s = args[++i];
      if (script++)
        return 0;
      if (strcmp(s, NG_COMPONENT "/dhcp-hook.sh") &&
          (owned || iface != NG_WIFI ||
           (strcmp(s, "/usr/share/udhcpc/set_lan.sh") &&
            strcmp(s, NG_COMPONENT "/native-set_lan.sh"))))
        return 0;
    } else if (!strcmp(args[i], "-p") && i + 1 < count) {
      if (pidfile++ || strcmp(args[++i], expected_pid))
        return 0;
    }
  }
  return matches == 1 && script == 1 && (!owned || pidfile == 1);
}
static inline pid_t ng_dhcp_pid(int iface) {
  char path[512], text[40];
  size_t size;
  ng_dhcp_path(iface, "pid", path);
  if (!ng_read_file(path, text, sizeof(text), &size, 1))
    return 0;
  char *end;
  long pid = strtol(text, &end, 10);
  if (pid <= 1 || pid > INT_MAX || (*end && strcmp(end, "\n")))
    return 0;
  return ng_dhcp_matches((pid_t)pid, iface, 1) ? (pid_t)pid : 0;
}
static inline int ng_dhcp_stop(int iface) {
  char generation[512];
  ng_dhcp_path(iface, "generation", generation);
  struct stat st;
  if (!lstat(generation, &st)) {
    if (!S_ISREG(st.st_mode) || st.st_uid != ng_owner() ||
        (st.st_mode & 0777) != 0600 || st.st_nlink != 1 || unlink(generation))
      return 0;
  } else if (errno != ENOENT)
    return 0;
  DIR *directory = opendir("/proc");
  if (!directory)
    return 0;
  struct dirent *item;
  int ok = 1;
  while ((item = readdir(directory))) {
    char *end;
    long value = strtol(item->d_name, &end, 10);
    if (!*item->d_name || *end || value <= 1 || value > INT_MAX)
      continue;
    pid_t pid = (pid_t)value;
    if (!ng_dhcp_matches(pid, iface, 0))
      continue;
    if (kill(pid, SIGTERM) && errno != ESRCH) {
      ok = 0;
      continue;
    }
    long long deadline = ng_now() + 2000;
    while (ng_dhcp_matches(pid, iface, 0) && ng_now() < deadline)
      poll(NULL, 0, 50);
    if (ng_dhcp_matches(pid, iface, 0) && kill(pid, SIGKILL) && errno != ESRCH)
      ok = 0;
  }
  closedir(directory);
  return ok;
}
static inline int ng_dhcp_start(int iface, int restart) {
  if (!ng_directory(NG_RUN))
    return 0;
  if (!restart && ng_dhcp_pid(iface))
    return 1;
  if (!ng_dhcp_stop(iface))
    return 0;
  char token[33], generation[512], pidfile[512];
  if (!ng_nonce(token))
    return 0;
  ng_dhcp_path(iface, "generation", generation);
  const char *base = strrchr(generation, '/');
  if (!base || !ng_atomic(NG_RUN, base + 1, token, 32))
    return 0;
  ng_dhcp_path(iface, "pid", pidfile);
  struct stat st;
  if (!lstat(pidfile, &st)) {
    if (!S_ISREG(st.st_mode) || st.st_uid != ng_owner() ||
        (st.st_mode & 0777) != 0600 || st.st_nlink != 1 || unlink(pidfile))
      return 0;
  } else if (errno != ENOENT)
    return 0;
  const char *tool = ng_tool("udhcpc");
  if (!tool)
    return 0;
  char executable[512];
  if (!ng_copy(executable, sizeof(executable), tool))
    return 0;
  pid_t pid = fork();
  if (pid < 0)
    return 0;
  if (!pid) {
    setsid();
    umask(077);
    int fd = open("/dev/null", O_RDWR);
    if (fd < 0 || dup2(fd, 0) < 0 || dup2(fd, 1) < 0 || dup2(fd, 2) < 0)
      _exit(126);
    if (setenv("CC2_DHCP_GENERATION", token, 1))
      _exit(126);
    long max = sysconf(_SC_OPEN_MAX);
    if (max < 0 || max > 65536)
      max = 65536;
    for (int i = 3; i < max; i++)
      close(i);
    char *args[] = {"udhcpc",
                    "-f",
                    "-B",
                    "-i",
                    (char *)ng_name(iface),
                    "-s",
                    NG_COMPONENT "/dhcp-hook.sh",
                    "-p",
                    pidfile,
                    "-t",
                    "3",
                    "-T",
                    "3",
                    "-A",
                    "20",
                    NULL};
    execv(executable, args);
    _exit(127);
  }
  long long deadline = ng_now() + 2500;
  while (ng_now() < deadline) {
    if (ng_dhcp_pid(iface))
      return 1;
    int status;
    pid_t done = waitpid(pid, &status, WNOHANG);
    if (done == pid)
      return 0;
    poll(NULL, 0, 50);
  }
  kill(pid, SIGKILL);
  while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {
  }
  return 0;
}
static inline int ng_dhcp_generation(int iface) {
  char path[512], expected[40];
  size_t n;
  ng_dhcp_path(iface, "generation", path);
  const char *supplied = getenv("CC2_DHCP_GENERATION");
  return supplied && strlen(supplied) == 32 &&
         ng_read_file(path, expected, sizeof(expected), &n, 1) && n == 32 &&
         !memcmp(expected, supplied, 32);
}
static inline int ng_policy_refresh(void) {
  char *args[] = {"sh", NG_COMPONENT "/native-wan.sh", "cc2_refresh_policy",
                  NULL};
  return ng_exec("/bin/sh", args, NULL, 0, 4);
}
static inline int ng_dhcp_event(const char *event) {
  const char *name = getenv("interface");
  int iface = name && !strcmp(name, "wlan0")     ? NG_WIFI
              : name && !strcmp(name, "cc2lan0") ? NG_ETHERNET
                                                 : -1;
  if (iface < 0 || !ng_dhcp_generation(iface)) {
    ng_event("stale_dhcp_callback_ignored");
    return 1;
  }
  ng_config config;
  if (!ng_operating_config(&config))
    return 0;
  if (!config.link[iface].enabled || config.link[iface].method != NG_DHCP)
    return 1;
  int lock = ng_lock("lease.lock", 4);
  if (lock < 0)
    return 0;
  if (!ng_dhcp_generation(iface)) {
    close(lock);
    return 1;
  }
  int ok = 0;
  ng_lease lease;
  if (!strcmp(event, "deconfig")) {
    memset(&lease, 0, sizeof(lease));
    lease.schema = NG_SCHEMA;
    if (ng_boot_id(lease.boot) && ng_lease_save(iface, &lease) &&
        ng_command("ip", NULL, 0, 2, "-4", "addr", "flush", "dev",
                   ng_name(iface), "scope", "global", NULL))
      ok = ng_configure_routes(&config);
  } else if (!strcmp(event, "bound") || !strcmp(event, "renew")) {
    if (ng_lease_environment(&lease) && ng_lease_save(iface, &lease) &&
        ng_apply_address(iface, &lease) && ng_configure_routes(&config)) {
      ok = 1;
      if (config.selected == iface)
        ok = ng_resolver_selected(&config);
      if (ok)
        ok = ng_policy_refresh();
    }
  }
  close(lock);
  ng_event(ok ? "dhcp_event_applied" : "dhcp_event_failed");
  return ok;
}
#endif
