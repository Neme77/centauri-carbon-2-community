#ifndef CC2_CONNECTIONS_NATIVE_H
#define CC2_CONNECTIONS_NATIVE_H
/* Newly started native processes must not belong to the bounded transaction's
 * process group: cancelling a worker must never terminate a healthy printer. */
static inline int ng_native_spawn(const char *role, const char *token,
                                  int network) {
  if (!ng_token_valid(token) || (network != 0 && network != 1) ||
      (strcmp(role, "camera") && strcmp(role, "printer")))
    return 0;
  ng_journal j;
  if (!ng_journal_load(&j) || strcmp(j.token, token) ||
      (j.phase != NG_APPLYING && j.phase != NG_RESTORING) ||
      !ng_check_profile("ethernet"))
    return 0;
  char base[512], file[600], path[4096], library[4096];
  size_t n;
  snprintf(base, sizeof(base), "%s/activation-%s", NG_STATE, token);
  if (!ng_directory(base))
    return 0;
  snprintf(file, sizeof(file), "%s/%s.path", base, role);
  if (!ng_read_file(file, path, sizeof(path), &n, 1))
    return 0;
  path[strcspn(path, "\n")] = 0;
  snprintf(file, sizeof(file), "%s/%s.library", base, role);
  if (!ng_read_file(file, library, sizeof(library), &n, 1))
    return 0;
  library[strcspn(library, "\n")] = 0;
  if (!*path || strchr(path, '\r') || strchr(library, '\r'))
    return 0;
  pid_t child = fork();
  if (child < 0)
    return 0;
  if (!child) {
    if (setsid() < 0 || chdir("/"))
      _exit(126);
    signal(SIGHUP, SIG_IGN);
    struct rlimit limit = {0, 0};
    setrlimit(RLIMIT_CORE, &limit);
    int fd = open("/dev/null", O_RDWR);
    if (fd < 0 || dup2(fd, 0) < 0 || dup2(fd, 1) < 0 || dup2(fd, 2) < 0)
      _exit(126);
    long max = sysconf(_SC_OPEN_MAX);
    if (max < 0 || max > 65536)
      max = 65536;
    for (int i = 3; i < max; i++)
      close(i);
    clearenv();
    setenv("PATH", path, 1);
    setenv("LD_LIBRARY_PATH", library, 1);
    setenv("CC2_NETWORK_COMPAT_TRIAL", network ? "1" : "0", 1);
    char preload[1024];
    if (!strcmp(role, "camera")) {
      snprintf(preload, sizeof(preload),
               "%s%s/opt/usr/cc2-control/camera/libcc2-camera-reap-r2.so:/opt/"
               "usr/cc2-control/camera/libcc2-camera-recovery-r2.so",
               network ? NG_COMPONENT "/libcc2-network-compat-r1.so" : "",
               network ? ":" : "");
      setenv("CC2_CAMERA_REAP_TRIAL", "1", 1);
      setenv("CC2_CAMERA_REAP_ENABLE_LAN", "1", 1);
      setenv("CC2_CAMERA_RECOVERY_TRIAL", "1", 1);
      setenv("LD_PRELOAD", preload, 1);
      char *args[] = {"/opt/bin/ai_camera", NULL};
      execv(args[0], args);
    } else {
      snprintf(preload, sizeof(preload),
               "%s%s/opt/usr/cc2-reactor-runtime-v2/libcc2-reactor-v2.so",
               network ? NG_COMPONENT "/libcc2-network-compat-r1.so" : "",
               network ? ":" : "");
      setenv("CC2_REACTOR_RUNTIME_V2", "1", 1);
      setenv("LD_PRELOAD", preload, 1);
      char *args[] = {"/opt/usr/cc2-reactor-runtime-v2/elegoo_printer",
                      "/opt/inst/printer_dsp.cfg",
                      "-s",
                      "/opt/usr/cfg/autosave.cfg",
                      "-a",
                      "/tmp/elegoo_uds",
                      NULL};
      execv(args[0], args);
    }
    _exit(127);
  }
  return 1;
}
#endif
