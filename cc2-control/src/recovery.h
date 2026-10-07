/* Included by main.c: all state is owned by the HTTP/main loop except the
 * atomic M112 latch written by the console worker. No shell or SSH is used. */
static console_state *recovery_console;
static int reboot_pending;
static pid_t reboot_pid;
static int reboot_launched;
static time_t reboot_due;
static const char *reboot_error = "none";

static pid_t launch_printer_reboot(void) {
    pid_t child = fork();
    if (child != 0) return child;
    /* Do not leave inherited HTTP/MQTT/UDS sockets open in the reboot utility. */
    for (int fd = 3; fd < FD_SETSIZE; ++fd) close(fd);
    execl("/sbin/reboot", "reboot", (char *)NULL);
    execl("/bin/reboot", "reboot", (char *)NULL);
    execl("/bin/busybox", "busybox", "reboot", (char *)NULL);
    _exit(127);
}
static pid_t (*reboot_launcher)(void) = launch_printer_reboot;
/* Optional last check before a requested reboot starts (plates.h: the printer must still be idle). */
static int (*reboot_guard)(void);

static int recovery_available(void) {
    return recovery_console && atomic_load(&recovery_console->emergency_sent);
}

static time_t recovery_clock(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec;
}

static void recovery_tick(void) {
    if (!reboot_pending) return;
    if (!reboot_launched && recovery_clock() >= reboot_due) {
        if (reboot_guard && !reboot_guard()) {
            reboot_guard = NULL; reboot_pending = 0; reboot_error = "printer_busy";
            return;
        }
        reboot_launched=1;
        reboot_pid = reboot_launcher();
        if (reboot_pid < 0) {
            reboot_pid = 0; reboot_pending = 0; reboot_error = "launch_failed";
        }
    } else if (reboot_pid > 0) {
        int status;
        pid_t result = waitpid(reboot_pid, &status, WNOHANG);
        if (result == reboot_pid || (result < 0 && errno != EINTR)) {
            /* A successful utility may return before the system goes down.
             * Keep the button locked; only a failed launch permits a retry. */
            if (result < 0 || !WIFEXITED(status) || WEXITSTATUS(status)) {
                reboot_pending = 0; reboot_error = "reboot_failed";
            }
            reboot_pid = 0;
        }
    }
}

static void recovery_reboot_response(int fd, const char *body, size_t length) {
    static const char confirmation[] = "REBOOT_AFTER_EMERGENCY";
    const char *reply;
    int status;
    if (length != sizeof(confirmation)-1 || memcmp(body,confirmation,length)) {
        status=400; reply="{\"error\":\"Explicit reboot confirmation required\"}\n";
    } else if (!recovery_available() || reboot_pending) {
        status=409; reply="{\"error\":\"Reboot requires an emergency stop sent by CC2 Control and no pending reboot\"}\n";
    } else {
        reboot_pending=1; reboot_launched=0; reboot_due=recovery_clock()+2; reboot_error="none";
        status=202; reply="{\"accepted\":true,\"reboot_pending\":true}\n";
    }
    respond(fd,status,status==202?"Accepted":status==400?"Bad Request":"Conflict",
            "application/json; charset=utf-8",reply,strlen(reply));
}
