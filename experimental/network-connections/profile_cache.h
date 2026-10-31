#ifndef CC2_CONNECTIONS_PROFILE_CACHE_H
#define CC2_CONNECTIONS_PROFILE_CACHE_H
/* Cache a successful full verification only within this boot. Every use checks
 * the identity, mode, ownership, size and nanosecond mtime/ctime of every file
 * in both manifests and every additional native input. Changed input reruns the
 * original verifier; unsupported metadata falls back to the original verifier.
 * This does not cache printer readiness, network state or operation guards. */
typedef struct {
  uint32_t version;
  char boot[40], fingerprint[65];
} ng_profile_proof;
static inline void ng_profile_stat_hash(sha256_state *hash,
                                        const struct stat *s) {
  uint64_t fields[] = {
      (uint64_t)s->st_dev,          (uint64_t)s->st_ino,
      (uint64_t)s->st_mode,         (uint64_t)s->st_uid,
      (uint64_t)s->st_gid,          (uint64_t)s->st_nlink,
      (uint64_t)s->st_size,         (uint64_t)s->st_mtim.tv_sec,
      (uint64_t)s->st_mtim.tv_nsec, (uint64_t)s->st_ctim.tv_sec,
      (uint64_t)s->st_ctim.tv_nsec};
  sha256_update(hash, (const unsigned char *)fields, sizeof(fields));
}
static inline int ng_profile_file(sha256_state *hash, const char *path) {
  struct stat link, file;
  if (lstat(path, &link) || link.st_uid != ng_owner())
    return 0;
  sha256_update(hash, (const unsigned char *)path, strlen(path) + 1);
  ng_profile_stat_hash(hash, &link);
  char canonical[1024];
  const char *target = path;
  if (S_ISLNK(link.st_mode)) {
    char name[1024];
    ssize_t n = readlink(path, name, sizeof(name));
    if (n <= 0 || (size_t)n >= sizeof(name) ||
        !ng_resolve_path(path, canonical, sizeof(canonical)))
      return 0;
    sha256_update(hash, (const unsigned char *)name, (size_t)n);
    target = canonical;
    sha256_update(hash, (const unsigned char *)target, strlen(target) + 1);
  }
  int fd = open(target, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0)
    return 0;
  int ok =
      !fstat(fd, &file) && S_ISREG(file.st_mode) && file.st_uid == ng_owner();
  if (ok)
    ng_profile_stat_hash(hash, &file);
  close(fd);
  return ok;
}
static inline int ng_profile_manifest(sha256_state *hash, const char *path,
                                      int absolute) {
  char text[32768];
  size_t size;
  if (!ng_profile_file(hash, path) ||
      !ng_read_file(path, text, sizeof(text), &size, 0) || !size)
    return 0;
  sha256_update(hash, (const unsigned char *)text, size);
  char *line = text;
  while (*line) {
    char *end = strchr(line, '\n');
    if (!end || (size_t)(end - line) < 67)
      return 0;
    *end = 0;
    for (int i = 0; i < 64; i++)
      if (!((line[i] >= '0' && line[i] <= '9') ||
            (line[i] >= 'a' && line[i] <= 'f')))
        return 0;
    if (line[64] != ' ' || line[65] != ' ')
      return 0;
    const char *name = line + 66;
    if (name[0] == '.' && name[1] == '/')
      name += 2;
    if (!*name || strchr(name, '\r') || strstr(name, "..") ||
        (!!absolute != (name[0] == '/')))
      return 0;
    char file[1024];
    int n = absolute
                ? snprintf(file, sizeof(file), "%s", name)
                : snprintf(file, sizeof(file), "%s/%s", NG_COMPONENT, name);
    if (n < 0 || (size_t)n >= sizeof(file) || !ng_profile_file(hash, file))
      return 0;
    line = end + 1;
  }
  return 1;
}
static inline int ng_profile_fingerprint(int full, char out[65]) {
  sha256_state hash;
  sha256_init(&hash);
  if (!ng_profile_manifest(&hash, NG_COMPONENT "/SHA256SUMS", 0))
    return 0;
  if (!access(NG_COMPONENT "/startup.sha256", F_OK) &&
      !ng_profile_manifest(&hash, NG_COMPONENT "/startup.sha256", 1))
    return 0;
#ifdef NG_PROFILE_FIXTURE
  (void)full;
  const char *inputs[] = {NG_COMPONENT "/native.fixture", NULL};
#else
  const char *inputs[] = {
      "/bin/wifi_daemon",
      "/opt/bin/ai_camera",
      "/opt/lib/libcolib.so",
      "/opt/usr/cc2-reactor-runtime-v2/elegoo_printer",
      "/opt/usr/cc2-reactor-runtime-v2/launcher.sh",
      "/etc/init.d/printer",
      "/opt/usr/cc2-control/camera/libcc2-camera-reap-r2.so",
      "/opt/usr/cc2-control/camera/libcc2-camera-recovery-r2.so",
      "/opt/usr/cc2-control/camera/cc2-camera-reap-check",
      "/opt/usr/cc2-reactor-runtime-v2/libcc2-reactor-v2.so",
      "/lib/modules/5.4.61/aic8800_fdrv.ko",
      NULL};
#endif
  for (int i = 0; inputs[i]; i++) {
    if (!ng_profile_file(&hash, inputs[i]))
      return 0;
#ifndef NG_PROFILE_FIXTURE
    if (!full)
      break;
#endif
  }
  sha256_hex(&hash, out);
  return 1;
}
static inline int ng_profile_cached(const char *kind,
                                    int (*verify)(const char *)) {
  int full = !strcmp(kind, "boot") || !strcmp(kind, "ethernet");
  if (!full && strcmp(kind, "wifi"))
    return verify(kind);
  if (!ng_directory(NG_RUN))
    return verify(kind);
  int lock = ng_lock("profile-check.lock", 15);
  if (lock < 0)
    return verify(kind);
  ng_profile_proof current = {0}, saved;
  current.version = 1;
  const char *name = full ? "profile-full.bin" : "profile-wifi.bin";
  int metadata = ng_boot_id(current.boot) &&
                 ng_profile_fingerprint(full, current.fingerprint);
  if (metadata && ng_blob_read(NG_RUN, name, &saved, sizeof(saved)) &&
      saved.version == current.version &&
      !memcmp(saved.boot, current.boot, sizeof(saved.boot)) &&
      !memcmp(saved.fingerprint, current.fingerprint,
              sizeof(saved.fingerprint))) {
    ng_event(full ? "profile_cache_hit_full" : "profile_cache_hit_wifi");
    close(lock);
    return 1;
  }
  ng_event(full ? "profile_full_verification_started"
                : "profile_wifi_verification_started");
  int ok = verify(kind);
  if (ok && metadata) {
    char after[65];
    if (!ng_profile_fingerprint(full, after) ||
        strcmp(after, current.fingerprint)) {
      ng_event("profile_inputs_changed_during_check");
      ok = 0;
    } else if (!ng_blob_write(NG_RUN, name, &current, sizeof(current))) {
      /* Cache failure never makes a successful full verification invalid. */
      ng_event("profile_cache_write_unavailable");
    }
  }
  ng_event(ok ? "profile_verification_passed" : "profile_verification_failed");
  close(lock);
  return ok;
}
#endif
