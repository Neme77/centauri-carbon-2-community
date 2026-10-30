/* The emergency stop goes to the firmware's emergency_stop endpoint on its own connection, so neither a busy
 * console nor a long-running firmware command can hold it back. */
#include "../src/console.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

static char path[108];
/* A one-shot firmware stand-in: checks the request, then answers with reply (NULL: close without answering). */
static pid_t firmware(const char *reply) {
    int server = socket(AF_UNIX, SOCK_STREAM, 0); assert(server >= 0);
    struct sockaddr_un a = {.sun_family = AF_UNIX};
    unlink(path); snprintf(a.sun_path, sizeof(a.sun_path), "%s", path);
    assert(!bind(server, (struct sockaddr *)&a, sizeof(a)) && !listen(server, 1));
    pid_t pid = fork(); assert(pid >= 0);
    if (!pid) {
        int c = accept(server, NULL, NULL); char in[256]; size_t used = 0;
        while (used < sizeof(in) - 1 && !memchr(in, 3, used)) { ssize_t n = read(c, in + used, sizeof(in) - 1 - used); if (n <= 0) _exit(2); used += (size_t)n; }
        in[used] = 0;
        if (strcmp(in, "{\"id\":112,\"method\":\"emergency_stop\",\"params\":{}}\003")) _exit(3);
        if (reply && write(c, reply, strlen(reply)) < 0) _exit(4);
        close(c); _exit(0);
    }
    close(server); return pid;
}
static void finished(pid_t pid) { int status; assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && !WEXITSTATUS(status)); }

int main(void) {
    snprintf(path, sizeof(path), "/tmp/cc2-estop-%d.sock", (int)getpid());
    static console_state state; char reason[160];
    console_init(&state, path);
    /* Answered: armed and accepted, even with another console command busy. */
    state.busy = 1;
    pid_t pid = firmware("{\"id\":112,\"result\":{}}\003");
    assert(console_emergency_stop(&state, reason, sizeof(reason)) == 0 && atomic_load(&state.emergency_sent));
    finished(pid); state.busy = 0;
    /* A firmware that shuts down and closes the socket still counts as sent. */
    atomic_store(&state.emergency_sent, 0);
    pid = firmware(NULL);
    assert(console_emergency_stop(&state, reason, sizeof(reason)) == 0 && atomic_load(&state.emergency_sent));
    finished(pid);
    /* An error reply is reported, so the caller falls back to M112. */
    pid = firmware("{\"id\":112,\"error\":{\"message\":\"no\"}}\003");
    assert(console_emergency_stop(&state, reason, sizeof(reason)) == -1 && strstr(reason, "refused"));
    finished(pid);
    /* No firmware socket: an error, nothing armed. */
    unlink(path); atomic_store(&state.emergency_sent, 0);
    assert(console_emergency_stop(&state, reason, sizeof(reason)) == -1 && !atomic_load(&state.emergency_sent));
    console_destroy(&state);
    puts("PASS test_emergency_stop_direct.c");
    return 0;
}
