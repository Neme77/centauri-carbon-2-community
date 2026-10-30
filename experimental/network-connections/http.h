/* Included after the application's response, update and authentication guards.
 * Every mutation uses its actual accepted socket endpoint; never
 * Host/X-Forwarded. The native HTTP loop only starts/reaps a worker and reads
 * private snapshots. */
#ifndef CC2_CONNECTIONS_HTTP_H
#define CC2_CONNECTIONS_HTTP_H
#include "connections/pin.h"
#include "connections/public.h"
static pid_t connections_child;
static int connections_result = -1;
static int connections_program(void) {
  int fd = open(NG_COMPONENT "/control", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0)
    return -1;
  struct stat st;
  char digest[65];
  int ok = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == geteuid() &&
           !(st.st_mode & 0022) && (st.st_mode & 0111) && st.st_nlink == 1 &&
           sha256_fd(fd, digest) && !strcmp(digest, CC2_CONNECTIONS_HELPER_SHA);
  if (!ok || lseek(fd, 0, SEEK_SET) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}
static void connections_reap(void) {
  if (connections_child > 0) {
    int status;
    pid_t done = waitpid(connections_child, &status, WNOHANG);
    if (done == connections_child) {
      connections_result = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
      connections_child = 0;
    } else if (done < 0 && errno == ECHILD) {
      connections_result = 128;
      connections_child = 0;
    }
  }
}
static int connections_busy(void) {
  connections_reap();
  return connections_child > 0 || ng_public_active();
}
static void connections_status(int fd, const mqtt_client *mqtt,
                               console_state *console) {
  connections_reap();
  struct stat program;
  int installed = !lstat(NG_COMPONENT "/control", &program) &&
                  S_ISREG(program.st_mode) && program.st_uid == geteuid() &&
                  !(program.st_mode & 0022) && (program.st_mode & 0111);
  char *text = NULL;
  size_t size = 0;
  FILE *out = open_memstream(&text, &size);
  if (!out) {
    app_update_fail(fd, 503, "Network state unavailable");
    return;
  }
  ng_public_json(out, installed, app_update_ready(mqtt, console),
                 connections_child > 0, connections_result);
  int failed = fclose(out);
  if (failed || size > 32768)
    app_update_fail(fd, 503, "Network state unavailable");
  else
    respond(fd, 200, "OK", "application/json", text, size);
  free(text);
}
static void connections_action(int fd, const char *body, size_t length,
                               int scan, mqtt_client *mqtt,
                               console_state *console) {
  if (!body || !length || length > 4095) {
    app_update_fail(fd, 400, "Invalid network request");
    return;
  }
  ng_request parsed;
  memset(&parsed, 0, sizeof(parsed));
  int recovery = 0;
  if (!scan) {
    ng_json *doc = malloc(sizeof(*doc));
    int valid = doc && ng_json_parse(doc, body, length) &&
                ng_request_parse(doc, &parsed);
    if (doc) {
      ng_erase(doc, sizeof(*doc));
      free(doc);
    }
    if (!valid) {
      ng_erase(&parsed, sizeof(parsed));
      app_update_fail(fd, 400, "Invalid network request");
      return;
    }
    recovery = parsed.op == NG_CONFIRM || parsed.op == NG_CANCEL;
  } else if (length != 4 || memcmp(body, "scan", 4)) {
    app_update_fail(fd, 400, "Invalid scan request");
    return;
  }
  ng_erase(&parsed, sizeof(parsed));
  connections_reap();
  if (connections_child > 0 || app_update_active() ||
      (!recovery && (ng_public_active() || !app_update_ready(mqtt, console)))) {
    app_update_fail(fd, 409,
                    "Printer must be idle and cold; a network operation may "
                    "already be running");
    return;
  }
  struct sockaddr_in local, peer;
  socklen_t ln = sizeof(local), pn = sizeof(peer);
  char address[16], remote[16], port[8];
  if (getsockname(fd, (struct sockaddr *)&local, &ln) ||
      local.sin_family != AF_INET ||
      getpeername(fd, (struct sockaddr *)&peer, &pn) ||
      peer.sin_family != AF_INET ||
      !inet_ntop(AF_INET, &local.sin_addr, address, sizeof(address)) ||
      !inet_ntop(AF_INET, &peer.sin_addr, remote, sizeof(remote)) ||
      !ng_ipv4(address, NULL) || !ng_ipv4(remote, NULL)) {
    app_update_fail(fd, 409, "Open the panel through its LAN or Wi-Fi address");
    return;
  }
  snprintf(port, sizeof(port), "%u", ntohs(local.sin_port));
  int helper = connections_program();
  if (helper < 0) {
    app_update_fail(fd, 409, "Qualified connections manager unavailable");
    return;
  }
  if (!ng_directory(NG_RUN)) {
    close(helper);
    app_update_fail(fd, 503, "Private network state unavailable");
    return;
  }
  int input[2];
  if (pipe2(input, O_CLOEXEC)) {
    close(helper);
    app_update_fail(fd, 503, "Network worker unavailable");
    return;
  }
  int output =
      open(NG_RUN "/last-action.json",
           O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
  struct stat st;
  if (output < 0 || fstat(output, &st) || !S_ISREG(st.st_mode) ||
      st.st_uid != geteuid() || (st.st_mode & 0777) != 0600 ||
      st.st_nlink != 1) {
    if (output >= 0)
      close(output);
    close(input[0]);
    close(input[1]);
    close(helper);
    app_update_fail(fd, 503, "Private network result unavailable");
    return;
  }
  long max = sysconf(_SC_OPEN_MAX);
  if (max < 0 || max > 65536)
    max = 65536;
  pid_t child = fork();
  if (!child) {
    if (dup2(input[0], 0) < 0 || dup2(output, 1) < 0 || dup2(output, 2) < 0)
      _exit(126);
    for (int i = 3; i < max; i++)
      if (i != helper)
        close(i);
    char *args[] = {
        "control", scan ? "--scan" : "--request", address, remote, port, NULL};
    if (scan)
      args[2] = NULL;
    char *env[] = {"PATH=/usr/sbin:/usr/bin:/sbin:/bin", NULL};
    fexecve(helper, args, env);
    _exit(127);
  }
  close(helper);
  close(input[0]);
  close(output);
  int sent = child > 0 && (scan || ng_write_all(input[1], body, length));
  close(input[1]);
  if (child < 0 || !sent) {
    if (child > 0)
      kill(child, SIGTERM);
    app_update_fail(fd, 503, "Network worker unavailable");
    return;
  }
  connections_child = child;
  connections_result = -1;
  const char *reply = "{\"accepted\":true,\"temporary\":false}\n";
  respond(fd, 202, "Accepted", "application/json", reply, strlen(reply));
}
#endif
