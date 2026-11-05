#define main cc2_original_main
#include "../src/main.c"
#undef main
#include <assert.h>
#include <sys/wait.h>
/* GET /api/mesh asks the printer only for the active mesh: `{"bed_mesh":null}` made the firmware serialise
 * every stored profile (39 KB, 615 ms on a CC2) on its motion thread and ended a print with "Timer too
 * close". The side slots and the adaptive mesh come from autosave.cfg instead, in the firmware's shape. */
static const char active[] =
    "{\"id\":201,\"result\":{\"eventtime\":570.05,\"status\":{\"bed_mesh\":{\"profile_name\":\"cc2b_7d29710cf494c5ff_105\","
    "\"mesh_min\":[6.0,6.0],\"mesh_max\":[246.0,246.0],\"probed_matrix\":[[-1.0,-1.1,-1.2],[-0.9,-1.0,-1.1],[-0.8,-0.9,-1.0]],"
    "\"mesh_matrix\":[[-1.0,-1.1,-1.2],[-0.9,-1.0,-1.1],[-0.8,-0.9,-1.0]]}}}}\003";

static char received[512];
static int received_fd = -1;
static void collect(void) { /* what the printer was sent, once the request is over */
    ssize_t r = read(received_fd, received, sizeof(received) - 1); received[r > 0 ? r : 0] = 0; close(received_fd); received_fd = -1;
}
static pid_t printer(const char *path, const char *answer) {
    struct sockaddr_un a; memset(&a, 0, sizeof(a)); a.sun_family = AF_UNIX;
    assert(strlen(path) < sizeof(a.sun_path)); memcpy(a.sun_path, path, strlen(path) + 1);
    unlink(path); int l = socket(AF_UNIX, SOCK_STREAM, 0); assert(l >= 0);
    assert(!bind(l, (struct sockaddr *)&a, sizeof(a)) && !listen(l, 2));
    int pipefd[2]; assert(!pipe(pipefd));
    pid_t pid = fork(); assert(pid >= 0);
    if (!pid) {
        int c = accept(l, NULL, NULL); char q[512]; size_t n = 0;
        while (n < sizeof(q) - 1) { ssize_t r = read(c, q + n, sizeof(q) - 1 - n); if (r <= 0) break; n += (size_t)r; if (memchr(q, 3, n)) break; }
        if (write(pipefd[1], q, n) < 0) _exit(2);
        if (answer && write(c, answer, strlen(answer)) < 0) _exit(2);
        close(c); _exit(0);
    }
    close(l); close(pipefd[1]); received_fd = pipefd[0];
    return pid;
}
static char response[131072];
static int call(void) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    int size = 1 << 20; setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    mesh_response(pair[0]); close(pair[0]);
    size_t used = 0; ssize_t n;
    while ((n = recv(pair[1], response + used, sizeof(response) - 1 - used, 0)) > 0) used += (size_t)n;
    response[used] = 0; close(pair[1]);
    int status = 0; sscanf(response, "HTTP/1.1 %d", &status); return status;
}
static plate_mesh grid(double base, int n) {
    plate_mesh m; memset(&m, 0, sizeof(m));
    m.x_count = m.y_count = n; m.x_pps = m.y_pps = 3; m.min_x = m.min_y = 6; m.max_x = m.max_y = 246; m.tension = 0.2;
    snprintf(m.algo, sizeof(m.algo), "bicubic");
    for (int i = 0; i < n * n; ++i) m.points[i] = round((base + i / 1000.0) * 1e6) / 1e6;
    return m;
}

int main(void) {
    char dir[] = "/tmp/cc2-mesh-api-XXXXXX"; assert(mkdtemp(dir));
    static char sock[128], cfg[128]; snprintf(sock, sizeof(sock), "%s/uds", dir); snprintf(cfg, sizeof(cfg), "%s/autosave.cfg", dir);
    object_query_path = sock; printer_autosave_path = cfg;
    FILE *f = fopen(cfg, "w"); assert(f);
    fputs("[include printer.cfg]\n\n" AUTOSAVE_MARKER "\n#*# DO NOT EDIT THIS BLOCK OR BELOW. The contents are auto-generated.\n#*#\n", f); fclose(f);
    const char *names[] = {"default", "default1", "ADAPTIVE", "cc2_90d2969e8a3df2b8", "cc2b_7d29710cf494c5ff_105"};
    plate_mesh meshes[] = {grid(-1.0, 11), grid(-0.5, 11), grid(-0.2, 5), grid(-0.7, 11), grid(-0.8, 11)};
    for (int i = 0; i < 5; ++i) {
        size_t length = 0, n = 0; char *text = plates_read_file(cfg, AUTOSAVE_FILE_MAX, &length); assert(text);
        char *more = autosave_with_mesh(text, length, names[i], &meshes[i], &n); assert(more);
        f = fopen(cfg, "w"); assert(f && fwrite(more, 1, n, f) == n); fclose(f); free(text); free(more);
    }

    pid_t pid = printer(sock, active);
    assert(call() == 200); waitpid(pid, NULL, 0); collect();
    /* the printer was asked for the active mesh only */
    assert(strstr(received, "\"bed_mesh\":[\"profile_name\",\"mesh_min\",\"mesh_max\",\"probed_matrix\",\"mesh_matrix\"]"));
    assert(!strstr(received, "null") && !strstr(received, "profiles"));
    /* the reply keeps the printer's active mesh and gains the three shown profiles from the file */
    const char *body = strstr(response, "\r\n\r\n"); assert(body); body += 4;
    const char *end = body + strlen(body);
    assert(json_container_end(body, end) == end);
    assert(strstr(body, "\"profile_name\":\"cc2b_7d29710cf494c5ff_105\"") && strstr(body, "\"probed_matrix\":[[-1.0,-1.1,-1.2]"));
    const char *e1, *e2, *e3, *e4, *e5, *e6;
    const char *result = json_member_object(body, end, "result", '{', &e1);
    const char *status = json_member_object(result, e1, "status", '{', &e2);
    const char *bed = json_member_object(status, e2, "bed_mesh", '{', &e3);
    const char *profiles = json_member_object(bed, e3, "profiles", '{', &e4);
    assert(profiles);
    for (int i = 0; i < 3; ++i) {
        const char *p = json_member_object(profiles, e4, names[i], '{', &e5); assert(p);
        const char *params = json_member_object(p, e5, "mesh_params", '{', &e6); assert(params);
        plate_mesh got; memset(&got, 0, sizeof(got)); got.x_count = meshes[i].x_count; got.y_count = meshes[i].y_count;
        assert(plates_json_points(json_member(p, e5, "points"), e5, &got) && plates_json_params(params, e6, &got));
        assert(plate_mesh_equal(&got, &meshes[i], 0));
    }
    assert(!json_member(profiles, e4, "cc2_90d2969e8a3df2b8") && !json_member(profiles, e4, "cc2b_7d29710cf494c5ff_105"));
    assert(strlen(body) < 12000);                       /* a few KB, however many profiles are stored */

    /* a printer that is down or answers nonsense is reported, not guessed */
    unlink(sock); assert(call() == 503 && strstr(response, "Printer UDS unavailable"));
    pid = printer(sock, "{\"id\":201,\"error\":{\"message\":\"no\"}}\003");
    assert(call() == 503 && strstr(response, "Unexpected mesh response")); waitpid(pid, NULL, 0); collect();
    /* no file: the active mesh still comes back, with no profiles */
    unlink(cfg); pid = printer(sock, active);
    assert(call() == 200 && strstr(response, "\"profiles\":{}") && strstr(response, "probed_matrix")); waitpid(pid, NULL, 0); collect();
    unlink(sock); rmdir(dir);
    puts("PASS: /api/mesh asks only for the active mesh, adds the side and adaptive profiles from autosave.cfg, hides other profiles, stays small, reports failures");
    return 0;
}
