#ifndef CC2_CONNECTIONS_IO_H
#define CC2_CONNECTIONS_IO_H
#define _GNU_SOURCE
#include "core.h"
#include "sha256.h"
#include <dirent.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>
#include <signal.h>
#include <stddef.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#ifndef NG_COMPONENT
#define NG_COMPONENT "/opt/usr/cc2-network-manager"
#endif
#ifndef NG_STATE
#define NG_STATE "/opt/usr/cc2-network-state"
#endif
#ifndef NG_RUN
#define NG_RUN "/tmp/cc2-network-manager"
#endif
#ifndef NG_WPA
#define NG_WPA "/opt/usr/wifi/sockets/wlan0"
#endif
static inline long long ng_now(void) {
  struct timespec n;
  if (clock_gettime(CLOCK_MONOTONIC, &n))
    return -1;
  return (long long)n.tv_sec * 1000 + n.tv_nsec / 1000000;
}
static inline uid_t ng_owner(void) {
#ifdef NG_TEST
  return geteuid();
#else
  return 0;
#endif
}
static inline int ng_hash_file(const char *path, char out[65]) {
  int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0)
    return 0;
  struct stat st;
  int ok = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == ng_owner() &&
           !(st.st_mode & 0022) && sha256_fd(fd, out);
  close(fd);
  return ok;
}
static inline int ng_directory(const char *path) {
  struct stat s;
  if (lstat(path, &s)) {
    if (errno != ENOENT || mkdir(path, 0700) || lstat(path, &s))
      return 0;
  }
  return S_ISDIR(s.st_mode) && s.st_uid == ng_owner() &&
         (s.st_mode & 0777) == 0700;
}
static inline int ng_private_fd(const char *path, int writable) {
  int fd = open(path, (writable ? O_RDWR : O_RDONLY) | O_NOFOLLOW | O_NONBLOCK |
                          O_CLOEXEC);
  if (fd < 0)
    return -1;
  struct stat s;
  if (fstat(fd, &s) || !S_ISREG(s.st_mode) || s.st_uid != ng_owner() ||
      s.st_nlink != 1 || (s.st_mode & 0777) != 0600) {
    close(fd);
    return -1;
  }
  return fd;
}
static inline int ng_read_file(const char *path, void *out, size_t cap,
                               size_t *size, int secret) {
  int fd = secret ? ng_private_fd(path, 0)
                  : open(path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0)
    return 0;
  struct stat st;
  if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 0 ||
      (secret && (uint64_t)st.st_size >= cap)) {
    close(fd);
    return 0;
  }
  size_t used = 0;
  while (used < cap) {
    ssize_t n = read(fd, (char *)out + used, cap - used);
    if (n < 0 && errno == EINTR)
      continue;
    if (n < 0) {
      close(fd);
      return 0;
    }
    if (!n)
      break;
    used += (size_t)n;
  }
  close(fd);
  if (used >= cap)
    return 0;
  ((char *)out)[used] = 0;
  if (size)
    *size = used;
  return 1;
}
static inline int ng_random(unsigned char *out, size_t size) {
  int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return 0;
  size_t at = 0;
  while (at < size) {
    ssize_t n = read(fd, out + at, size - at);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0) {
      close(fd);
      return 0;
    }
    at += (size_t)n;
  }
  close(fd);
  return 1;
}
static inline int ng_nonce(char token[33]) {
  unsigned char bytes[16];
  if (!ng_random(bytes, sizeof(bytes)))
    return 0;
  for (int i = 0; i < 16; i++)
    snprintf(token + i * 2, 3, "%02x", bytes[i]);
  ng_erase(bytes, sizeof(bytes));
  return 1;
}
static inline int ng_write_all(int fd, const void *data, size_t size) {
  size_t at = 0;
  while (at < size) {
    ssize_t n = write(fd, (const char *)data + at, size - at);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      return 0;
    at += (size_t)n;
  }
  return 1;
}
static inline int ng_atomic(const char *directory, const char *name,
                            const void *data, size_t size) {
  if (!ng_directory(directory) || strchr(name, '/') || !*name)
    return 0;
  char token[33], temp[512], target[512];
  if (!ng_nonce(token))
    return 0;
  int n = snprintf(temp, sizeof(temp), "%s/.write-%s", directory, token);
  int m = snprintf(target, sizeof(target), "%s/%s", directory, name);
  if (n < 0 || (size_t)n >= sizeof(temp) || m < 0 ||
      (size_t)m >= sizeof(target))
    return 0;
  int fd =
      open(temp, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (fd < 0)
    return 0;
  int ok = ng_write_all(fd, data, size) && !fsync(fd);
  if (close(fd))
    ok = 0;
  if (ok && rename(temp, target))
    ok = 0;
  if (!ok)
    unlink(temp);
  if (ok) {
    fd = open(directory, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
      return 0;
    ok = !fsync(fd);
    close(fd);
  }
  return ok;
}
static inline int ng_blob_write(const char *dir, const char *name,
                                const void *value, size_t size) {
  if (size > 8192)
    return 0;
  unsigned char *bytes = calloc(1, size + 80);
  if (!bytes)
    return 0;
  uint32_t magic = 0x434e4731u, length = (uint32_t)size;
  memcpy(bytes, &magic, 4);
  memcpy(bytes + 4, &length, 4);
  memcpy(bytes + 8, value, size);
  sha256_state state;
  sha256_init(&state);
  sha256_update(&state, bytes, size + 8);
  sha256_hex(&state, (char *)bytes + size + 8);
  int ok = ng_atomic(dir, name, bytes, size + 73);
  ng_erase(bytes, size + 80);
  free(bytes);
  return ok;
}
static inline int ng_blob_read(const char *dir, const char *name, void *value,
                               size_t size) {
  if (size > 8192)
    return 0;
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", dir, name);
  unsigned char *bytes = calloc(1, size + 80);
  if (!bytes)
    return 0;
  size_t got;
  int ok = ng_read_file(path, bytes, size + 80, &got, 1) && got == size + 73;
  uint32_t magic = 0, length = 0;
  memcpy(&magic, bytes, 4);
  memcpy(&length, bytes + 4, 4);
  if (magic != 0x434e4731u || length != size)
    ok = 0;
  char digest[65];
  sha256_state state;
  sha256_init(&state);
  sha256_update(&state, bytes, size + 8);
  sha256_hex(&state, digest);
  if (memcmp(digest, bytes + size + 8, 65))
    ok = 0;
  if (ok)
    memcpy(value, bytes + 8, size);
  ng_erase(bytes, size + 80);
  free(bytes);
  return ok;
}
static inline int ng_lock(const char *name, int seconds) {
  if (!ng_directory(NG_RUN))
    return -1;
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", NG_RUN, name);
  int fd = open(path, O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (fd < 0)
    return -1;
  struct stat s;
  if (fstat(fd, &s) || !S_ISREG(s.st_mode) || s.st_uid != ng_owner() ||
      s.st_nlink != 1 || (s.st_mode & 0777) != 0600) {
    close(fd);
    return -1;
  }
  long long deadline = ng_now() + (long long)seconds * 1000;
  while (flock(fd, LOCK_EX | LOCK_NB)) {
    if (errno != EWOULDBLOCK || ng_now() > deadline) {
      close(fd);
      return -1;
    }
    poll(NULL, 0, 50);
  }
  return fd;
}
/* Child output is bounded and never inherited by credential-bearing commands.
 */
static inline int ng_exec(const char *program, char *const argv[], char *out,
                          size_t capacity, int seconds) {
  if (out) {
    if (!capacity)
      return 0;
    out[0] = 0;
  }
  int pipes[2];
  if (pipe2(pipes, O_CLOEXEC | O_NONBLOCK))
    return 0;
  pid_t child = fork();
  if (child < 0) {
    close(pipes[0]);
    close(pipes[1]);
    return 0;
  }
  if (!child) {
    int null = open("/dev/null", O_RDWR);
    if (null < 0 || dup2(null, 0) < 0 || dup2(pipes[1], 1) < 0 ||
        dup2(pipes[1], 2) < 0)
      _exit(126);
    fcntl(1, F_SETFL, fcntl(1, F_GETFL) & ~O_NONBLOCK);
    long max = sysconf(_SC_OPEN_MAX);
    if (max < 0 || max > 65536)
      max = 65536;
    for (int fd = 3; fd < max; fd++)
      close(fd);
    execv(program, argv);
    _exit(127);
  }
  close(pipes[1]);
  size_t used = 0;
  int status = 0, done = 0, failed = 0;
  long long deadline = ng_now() + (long long)seconds * 1000;
  while (!done) {
    char discard[256];
    char *target = out ? out + used : discard;
    size_t room = out ? capacity - used - 1 : sizeof(discard);
    if (!room) {
      failed = 1;
      break;
    }
    ssize_t n = read(pipes[0], target, room);
    if (n > 0) {
      if (out)
        used += (size_t)n;
      continue;
    }
    if (n < 0 && errno != EAGAIN && errno != EINTR) {
      failed = 1;
      break;
    }
    pid_t result = waitpid(child, &status, WNOHANG);
    if (result == child) {
      done = 1;
      break;
    }
    if (result < 0 && errno != EINTR) {
      failed = 1;
      break;
    }
    if (ng_now() >= deadline) {
      failed = 1;
      break;
    }
    struct pollfd p = {.fd = pipes[0], .events = POLLIN};
    poll(&p, 1, 50);
  }
  if (!done) {
    kill(child, SIGKILL);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
  }
  /* Drain bytes written immediately before exit. */
  if (done && out)
    while (used + 1 < capacity) {
      ssize_t n = read(pipes[0], out + used, capacity - used - 1);
      if (n <= 0)
        break;
      used += (size_t)n;
    }
  close(pipes[0]);
  if (out)
    out[used] = 0;
  return !failed && done && WIFEXITED(status) && !WEXITSTATUS(status);
}
static inline int ng_wpa(const char *command, char *out, size_t cap,
                         int timeout) {
  if (!cap || strlen(command) > 1024)
    return 0;
  out[0] = 0;
  struct stat socket_info;
  if (lstat(NG_WPA, &socket_info) || !S_ISSOCK(socket_info.st_mode) ||
      socket_info.st_uid != ng_owner())
    return 0;
  int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0)
    return 0;
  struct sockaddr_un local = {.sun_family = AF_UNIX},
                     peer = {.sun_family = AF_UNIX};
  char nonce[33];
  if (!ng_nonce(nonce)) {
    close(fd);
    return 0;
  }
  int n = snprintf(local.sun_path + 1, sizeof(local.sun_path) - 1,
                   "cc2-connect-%ld-%s", (long)getpid(), nonce);
  strcpy(peer.sun_path, NG_WPA);
  int ok = n > 0 && (size_t)n < sizeof(local.sun_path) - 1 &&
           !bind(fd, (struct sockaddr *)&local,
                 (socklen_t)(offsetof(struct sockaddr_un, sun_path) + n + 1)) &&
           !connect(fd, (struct sockaddr *)&peer, sizeof(peer));
  if (ok)
    ok = send(fd, command, strlen(command), MSG_NOSIGNAL) ==
         (ssize_t)strlen(command);
  if (ok) {
    struct pollfd wait = {.fd = fd, .events = POLLIN};
    ok = poll(&wait, 1, timeout) > 0;
    if (ok) {
      ssize_t got = recv(fd, out, cap - 1, MSG_TRUNC);
      ok = got > 0 && (size_t)got < cap - 1;
      if (ok)
        out[got] = 0;
    }
  }
  close(fd);
  return ok;
}
static inline int ng_wpa_ok(const char *command) {
  char reply[128];
  return ng_wpa(command, reply, sizeof(reply), 2000) && !strcmp(reply, "OK\n");
}
static inline int ng_wpa_field(const char *reply, const char *key, char *out,
                               size_t cap) {
  size_t keylen = strlen(key);
  int found = 0;
  out[0] = 0;
  for (const char *p = reply; *p;) {
    const char *end = strchr(p, '\n');
    if (!end)
      return 0;
    if ((size_t)(end - p) > keylen && p[keylen] == '=' &&
        !memcmp(p, key, keylen)) {
      size_t n = (size_t)(end - p) - keylen - 1;
      if (found++ || n >= cap)
        return 0;
      memcpy(out, p + keylen + 1, n);
      out[n] = 0;
    }
    p = end + 1;
  }
  return found == 1;
}
static inline int ng_http(const char *endpoint, char *out, size_t cap,
                          int port) {
  if (port < 1 || port > 65535 || strchr(endpoint, '\r') ||
      strchr(endpoint, '\n'))
    return 0;
  int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (fd < 0)
    return 0;
  struct sockaddr_in peer = {.sin_family = AF_INET,
                             .sin_port = htons((unsigned short)port)};
  inet_pton(AF_INET, "127.0.0.1", &peer.sin_addr);
  int ok = connect(fd, (struct sockaddr *)&peer, sizeof(peer)) == 0 ||
           errno == EINPROGRESS;
  struct pollfd wait = {.fd = fd, .events = POLLOUT};
  if (ok)
    ok = poll(&wait, 1, 1000) > 0;
  int error = 0;
  socklen_t size = sizeof(error);
  if (ok)
    ok = !getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) && !error;
  char request[512];
  int n = snprintf(
      request, sizeof(request),
      "GET %s HTTP/1.0\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n\r\n",
      endpoint, port);
  if (n < 0 || (size_t)n >= sizeof(request))
    ok = 0;
  if (ok)
    ok = send(fd, request, (size_t)n, MSG_NOSIGNAL) == n;
  size_t used = 0;
  long long deadline = ng_now() + 2000;
  while (ok && used + 1 < cap) {
    ssize_t got = recv(fd, out + used, cap - used - 1, 0);
    if (got > 0) {
      used += (size_t)got;
      continue;
    }
    if (!got)
      break;
    if (errno != EAGAIN && errno != EINTR) {
      ok = 0;
      break;
    }
    wait.events = POLLIN;
    if (ng_now() >= deadline || poll(&wait, 1, 100) <= 0) {
      if (ng_now() >= deadline) {
        ok = 0;
        break;
      }
    }
  }
  close(fd);
  out[used] = 0;
  if (!ok || used + 1 >= cap ||
      (strncmp(out, "HTTP/1.1 200 ", 13) && strncmp(out, "HTTP/1.0 200 ", 13)))
    return 0;
  char *body = strstr(out, "\r\n\r\n");
  if (!body)
    return 0;
  body += 4;
  memmove(out, body, strlen(body) + 1);
  return 1;
}
static inline const char *ng_name(int iface) {
  return iface == NG_WIFI ? "wlan0" : "cc2lan0";
}
static inline int ng_live_ip(int iface, char ip[16]) {
  struct ifaddrs *all = NULL;
  if (getifaddrs(&all))
    return 0;
  int count = 0;
  ip[0] = 0;
  for (struct ifaddrs *p = all; p; p = p->ifa_next)
    if (!strcmp(p->ifa_name, ng_name(iface)) && p->ifa_addr &&
        p->ifa_addr->sa_family == AF_INET) {
      if (!inet_ntop(AF_INET, &((struct sockaddr_in *)p->ifa_addr)->sin_addr,
                     ip, 16))
        continue;
      count++;
    }
  freeifaddrs(all);
  if (count != 1 || !ng_ipv4(ip, NULL)) {
    ip[0] = 0;
    return 0;
  }
  return 1;
}
#endif
