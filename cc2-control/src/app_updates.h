/* Local signed application uploads. No HTTP filename, path, command or key
 * is used as an executable argument. The independent runner is installed
 * with the application, never supplied by an unauthenticated upload. */
static const char *app_update_root = "/opt/usr/cc2-control-updates";
static const char *app_update_verifier = "/opt/usr/cc2-control/cc2-update-worker";
static const char *app_update_runner = "/opt/usr/cc2-control/update-runner.sh";
static const char *app_update_downloader = "/opt/usr/cc2-control/cc2-https-download";
static const char *app_update_ca = "/opt/usr/cc2-control/ca-bundle.pem";
static atomic_int app_update_busy = 0;
static pid_t app_update_child;
#define APP_UPDATE_MAX (32U * 1024U * 1024U + 1108U)

static int app_update_token_valid(const char *token) {
    if (strlen(token) != 32) return 0;
    for (unsigned i = 0; i < 32; i++) if (!(token[i] >= '0' && token[i] <= '9') && !(token[i] >= 'a' && token[i] <= 'f')) return 0;
    return 1;
}

static int app_update_private(const char *path) {
    struct stat st;
    return !lstat(path, &st) && S_ISDIR(st.st_mode) && st.st_uid == geteuid() && !(st.st_mode & 0077);
}

static int app_update_program(const char *path) {
    struct stat st;
    return !lstat(path, &st) && S_ISREG(st.st_mode) && st.st_uid == geteuid() && !(st.st_mode & 0022) && !access(path, X_OK);
}

static int app_update_enabled(void) {
    return app_update_program(app_update_verifier) && app_update_program(app_update_runner) && app_update_program(app_update_downloader);
}

static int app_update_authorized(const char *request, const mqtt_client *mqtt) {
    static unsigned failures;
    static time_t retry_after;
    time_t now = time(NULL);
    if (now < retry_after) return 0;
    char code[128];
    int present = http_header(request, "X-CC2-LAN", code, sizeof(code));
    size_t expected = strlen(mqtt->password);
    unsigned difference = present == 1 && expected && strlen(code) == expected ? 0 : 1;
    if (!difference) for (size_t i = 0; i < expected; i++) difference |= (unsigned char)code[i] ^ (unsigned char)mqtt->password[i];
    memset(code, 0, sizeof(code));
    if (difference) {
        if (++failures >= 5) { retry_after = now + 60; failures = 0; }
        return 0;
    }
    failures = 0;
    return 1;
}

static int app_update_ready(const mqtt_client *mqtt, console_state *console) {
    int busy;
    pthread_mutex_lock(&console->lock); busy = console->busy; pthread_mutex_unlock(&console->lock);
    time_t age = time(NULL) - mqtt->last_message;
    return !drying_active() && (!experiments.quiet_motors || experiments_normal_confirmed(console)) &&
        gcode_idle_for_mutation(mqtt) && age >= 0 && !busy && !plates_background_active() &&
        mqtt->have_extruder_temp && mqtt->have_extruder_target && mqtt->have_bed_temp && mqtt->have_bed_target &&
        isfinite(mqtt->extruder_temp) && mqtt->extruder_temp >= 0 && mqtt->extruder_temp <= 50 &&
        isfinite(mqtt->bed_temp) && mqtt->bed_temp >= 0 && mqtt->bed_temp <= 50 &&
        mqtt->extruder_target == 0 && mqtt->bed_target == 0;
}

static int app_update_path(char *out, size_t capacity, const char *token, const char *name) {
    return app_update_token_valid(token) && snprintf(out, capacity, "%s/job-%s/%s", app_update_root, token, name) < (int)capacity;
}

static int app_update_read(const char *path, char *out, size_t capacity) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    struct stat st;
    if (fd < 0) return 0;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 0022) ||
        st.st_size < 1 || (uint64_t)st.st_size >= capacity) { close(fd); return 0; }
    ssize_t n = read(fd, out, capacity - 1);
    close(fd);
    if (n <= 0 || n != st.st_size) return 0;
    out[n] = 0;
    return 1;
}

static int app_update_current(char token[33]) {
    char path[512], text[40];
    if (!app_update_private(app_update_root) || snprintf(path, sizeof(path), "%s/current", app_update_root) >= (int)sizeof(path) ||
        !app_update_read(path, text, sizeof(text))) return 0;
    if (strlen(text) != 33 || text[32] != '\n') return 0;
    text[32] = 0;
    if (!app_update_token_valid(text)) return 0;
    memcpy(token, text, 33);
    return 1;
}

static int app_update_write(const char *path, const char *text) {
    char temporary[600];
    if (snprintf(temporary, sizeof(temporary), "%s.new", path) >= (int)sizeof(temporary)) return 0;
    int fd = open(temporary, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
    if (fd < 0) return 0;
    size_t length = strlen(text);
    int ok = write(fd, text, length) == (ssize_t)length;
    if (fsync(fd)) ok = 0;
    if (close(fd)) ok = 0;
    if (ok && rename(temporary, path)) ok = 0;
    if (!ok) unlink(temporary);
    return ok;
}

static int app_update_phase(const char *token, const char *phase, pid_t pid) {
    char path[600], text[200];
    if (!app_update_path(path, sizeof(path), token, "status.json")) return 0;
    snprintf(text, sizeof(text), "{\"phase\":\"%s\",\"pid\":%ld,\"started\":%lld}\n", phase, (long)pid, (long long)time(NULL));
    return app_update_write(path, text);
}

static int app_update_running(const char *token, const char *status) {
    char phase[24], path[600], cmdline[2048], stage[600];
    if (!extract_json_string(status, "phase", phase, sizeof(phase))) return 0;
    if (!strcmp(phase, "complete") || !strcmp(phase, "failed") || !strcmp(phase, "interrupted") || !strcmp(phase, "ready")) return 0;
    const char *number = strstr(status, "\"pid\":");
    long pid = number ? strtol(number + 6, NULL, 10) : 0;
    if (pid < 1 || pid > INT_MAX || !app_update_path(stage, sizeof(stage), token, "")) return 0;
    stage[strlen(stage) - 1] = 0;
    snprintf(path, sizeof(path), "/proc/%ld/cmdline", pid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    ssize_t n = read(fd, cmdline, sizeof(cmdline) - 1); close(fd);
    if (n <= 0) return 0;
    cmdline[n] = 0;
    int runner = 0, argument = 0;
    for (size_t i = 0; i < (size_t)n;) {
        size_t length = strnlen(cmdline + i, (size_t)n - i);
        if (length == (size_t)n - i) break;
        if (!strcmp(cmdline + i, app_update_runner)) runner = 1;
        if (!strcmp(cmdline + i, stage)) argument = 1;
        i += length + 1;
    }
    return runner && argument;
}

static int app_update_active(void) {
    if (atomic_load(&app_update_busy)) return 1;
    char token[33], path[600], status[256];
    if (!app_update_current(token) || !app_update_path(path, sizeof(path), token, "status.json") ||
        !app_update_read(path, status, sizeof(status))) return 0;
    if (app_update_child > 0) {
        int ignored;
        pid_t result = waitpid(app_update_child, &ignored, WNOHANG);
        if (result == 0 || (result < 0 && errno == EINTR)) return 1;
        app_update_child = 0;
    }
    return app_update_running(token, status);
}

static void app_update_fail(int fd, int code, const char *message) {
    char body[256];
    int length = snprintf(body, sizeof(body), "{\"error\":\"%s\"}\n", message);
    respond(fd, code, code == 409 ? "Conflict" : code == 401 ? "Unauthorized" : code == 400 ? "Bad Request" : code == 507 ? "Insufficient Storage" : "Service Unavailable", "application/json", body, (size_t)length);
}

/* Delete only payload/staging files in our private job. Keep the small status
 * and log; the installer backup is outside this directory and is never pruned. */
static void app_update_clean_stage(const char *stage) {
    if (!app_update_private(stage)) return;
    int root = open(stage, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (root < 0) return;
    static const char *const names[] = {"package.cc2update", "cc2-control", "start.sh", "launch.sh", "cc2-control.init",
        "install-on-printer.sh", "restore-on-printer.sh", "SHA256SUMS", "build-info.json", "cc2-update-worker",
        "updater-trusted-key.json", "THIRD_PARTY_NOTICES.txt", "update-runner.sh", "cc2-https-download", "ca-bundle.pem"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) unlinkat(root, names[i], 0);
    int web = openat(root, "web", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (web >= 0) {
        int locales = openat(web, "locales", O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (locales >= 0) {
            DIR *list = fdopendir(locales);
            if (list) {
                struct dirent *entry;
                while ((entry = readdir(list))) if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) unlinkat(locales, entry->d_name, 0);
                closedir(list);
            } else close(locales);
        }
        unlinkat(web, "locales", AT_REMOVEDIR); unlinkat(web, "index.html", 0); close(web);
        unlinkat(root, "web", AT_REMOVEDIR);
    }
    close(root);
}

static void app_update_cleanup(const char *token) {
    char stage[600], extracted[650];
    if (!app_update_path(stage, sizeof(stage), token, "")) return;
    stage[strlen(stage) - 1] = 0;
    if (!app_update_private(stage)) return;
    if (snprintf(extracted, sizeof(extracted), "%s/verified-payload", stage) >= (int)sizeof(extracted)) return;
    app_update_clean_stage(extracted);
    rmdir(extracted);
    app_update_clean_stage(stage);
}

static void app_update_status_response(int fd, const mqtt_client *mqtt, console_state *console) {
    char token[33] = "", path[600], state[256], phase[24] = "none", version[32] = "", verified[256];
    int running = app_update_active();
    if (app_update_current(token) && app_update_path(path, sizeof(path), token, "status.json") && app_update_read(path, state, sizeof(state))) {
        if (!extract_json_string(state, "phase", phase, sizeof(phase))) snprintf(phase, sizeof(phase), "interrupted");
        if (!running && (!strcmp(phase, "queued") || !strcmp(phase, "uploading") || !strcmp(phase, "downloading") || !strcmp(phase, "verifying") || !strcmp(phase, "installing"))) {
            snprintf(phase, sizeof(phase), "interrupted");
            app_update_phase(token, phase, 0);
        }
        if (app_update_path(path, sizeof(path), token, "verified.json") && app_update_read(path, verified, sizeof(verified)))
            extract_json_string(verified, "version", version, sizeof(version));
        if (!running && (!strcmp(phase, "complete") || !strcmp(phase, "failed") || !strcmp(phase, "interrupted"))) app_update_cleanup(token);
    }
    char body[600];
    int length = snprintf(body, sizeof(body), "{\"enabled\":%s,\"ready\":%s,\"busy\":%s,\"installed\":\"%s\",\"firmware\":\"%s\",\"job\":\"%s\",\"phase\":\"%s\",\"version\":\"%s\"}\n",
        app_update_enabled() ? "true" : "false", app_update_ready(mqtt, console) ? "true" : "false", running ? "true" : "false",
        CC2_CONTROL_VERSION, CC2_COMMUNITY_FIRMWARE_VERSION, token, phase, version);
    respond(fd, 200, "OK", "application/json", body, (size_t)length);
}

typedef struct {
    gcode_upload_job upload;
    char token[33];
    char download[1100];
} app_update_upload_job;

static int app_update_download_file(const char *url, int output) {
    int channel[2];
    if (pipe(channel)) return 0;
    long maximum = sysconf(_SC_OPEN_MAX); if (maximum < 0) maximum = 65536;
    pid_t child = fork();
    if (!child) {
        dup2(channel[1], STDOUT_FILENO);
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) { dup2(null, 0); dup2(null, 2); }
        for (int fd = 3; fd < maximum; fd++) close(fd);
        execl(app_update_downloader, app_update_downloader, url, app_update_ca, "33555540", (char *)NULL);
        _exit(127);
    }
    close(channel[1]);
    if (child < 0) { close(channel[0]); return 0; }
    unsigned char buffer[16384]; size_t total = 0;
    int ok = 1;
    time_t start = time(NULL);
    for (;;) {
        struct pollfd watch = {channel[0], POLLIN, 0};
        if (time(NULL) - start > 185) { ok = 0; break; }
        int ready = poll(&watch, 1, 1000);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) { ok = 0; break; }
        if (!ready) continue;
        ssize_t n = read(channel[0], buffer, sizeof(buffer));
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) { ok = 0; break; }
        if (!n) break;
        total += (size_t)n;
        if (total > APP_UPDATE_MAX) { ok = 0; break; }
        size_t offset = 0;
        while (offset < (size_t)n) {
            ssize_t written = write(output, buffer + offset, (size_t)n - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) { ok = 0; break; }
            offset += (size_t)written;
        }
        if (!ok) break;
    }
    close(channel[0]);
    if (!ok) kill(child, SIGKILL);
    int status = 0;
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    return ok && total >= 1108 && WIFEXITED(status) && !WEXITSTATUS(status);
}

static void *app_update_upload_worker(void *argument) {
    app_update_upload_job *job = argument;
    gcode_upload_job *upload = &job->upload;
    char path[600], stage[600], verified[600];
    unsigned char buffer[16384];
    int output = -1, ok = 0;
    size_t received = upload->initial_length - upload->header_length;
    const char *error = "Application upload failed";
    if (!app_update_path(path, sizeof(path), job->token, "package.cc2update") || received > upload->content_length) goto done;
    output = open(path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (output < 0) goto done;
    if (job->download[0]) {
        app_update_phase(job->token, "downloading", getpid());
        if (!app_update_download_file(job->download, output)) {
            error = "HTTPS download failed; check printer clock and Internet connection"; goto done;
        }
        received = upload->content_length;
    }
    size_t initial = 0;
    while (!job->download[0] && initial < received) {
        ssize_t n = write(output, upload->initial + upload->header_length + initial, received - initial);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) goto done;
        initial += (size_t)n;
    }
    while (received < upload->content_length) {
        size_t want = upload->content_length - received;
        if (want > sizeof(buffer)) want = sizeof(buffer);
        ssize_t n = upload_receive(upload, buffer, want);
        if (n <= 0) { error = "Incomplete or timed out application upload"; goto done; }
        size_t offset = 0;
        while (offset < (size_t)n) {
            ssize_t written = write(output, buffer + offset, (size_t)n - offset);
            if (written < 0 && errno == EINTR) continue;
            if (written <= 0) goto done;
            offset += (size_t)written;
        }
        received += (size_t)n;
    }
    if (fsync(output)) goto done;
    if (close(output)) { output = -1; goto done; }
    output = -1;
    if (!app_update_path(stage, sizeof(stage), job->token, "") || !app_update_path(verified, sizeof(verified), job->token, "verified.json")) goto done;
    stage[strlen(stage) - 1] = 0;
    int result = open(verified, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (result < 0) goto done;
    /* Do not execute any uploaded file. The installed verifier authenticates
     * and extracts only known regular files using bounded buffers. */
    long maximum = sysconf(_SC_OPEN_MAX);
    if (maximum < 0) maximum = 65536;
    pid_t child = fork();
    if (!child) {
        dup2(result, STDOUT_FILENO); dup2(result, STDERR_FILENO);
        for (int fd = 3; fd < maximum; fd++) close(fd);
        execl(app_update_verifier, app_update_verifier, "--extract", path, stage, (char *)NULL);
        _exit(127);
    }
    close(result);
    if (child < 0) goto done;
    int status = 0, reaped = 0;
    for (unsigned i = 0; i < 600; i++) {
        pid_t done_pid = waitpid(child, &status, WNOHANG);
        if (done_pid == child) { reaped = 1; ok = WIFEXITED(status) && WEXITSTATUS(status) == 0; break; }
        if (done_pid < 0 && errno != EINTR) { reaped = 1; break; }
        struct timespec pause = {0, 100000000}; nanosleep(&pause, NULL);
    }
    if (!ok) {
        if (!reaped) {
            kill(child, SIGKILL);
            while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
        }
        error = "Untrusted, incompatible or invalid application package";
    }
done:
    if (output >= 0) close(output);
    app_update_phase(job->token, ok ? "ready" : "failed", 0);
    if (!ok) app_update_cleanup(job->token);
    atomic_store(&app_update_busy, 0); upload_slot_release();
    if (ok) {
        char body[160];
        int n = snprintf(body, sizeof(body), "{\"verified\":true,\"job\":\"%s\"}\n", job->token);
        respond(upload->fd, 201, "Created", "application/json", body, (size_t)n);
    } else app_update_fail(upload->fd, 400, error);
    close(upload->fd); free(job);
    return NULL;
}

static int app_update_upload_start(int fd, const char *request, size_t used, size_t header_length, const mqtt_client *mqtt, console_state *console, const char *download) {
    if (!app_update_enabled()) { app_update_fail(fd, 503, "Application updater is not installed"); return 0; }
    if (!app_update_authorized(request, mqtt)) { app_update_fail(fd, 401, "LAN code confirmation required or temporarily locked"); return 0; }
    if (app_update_active() || !app_update_ready(mqtt, console)) { app_update_fail(fd, 409, "Printer must be connected, idle and cold with heating targets zero"); return 0; }
    size_t length = download ? APP_UPDATE_MAX : content_length_from_headers(request);
    if (length < 1108 || length > APP_UPDATE_MAX) { app_update_fail(fd, 400, "Invalid application update size"); return 0; }
    if ((mkdir(app_update_root, 0700) && errno != EEXIST) || !app_update_private(app_update_root)) {
        app_update_fail(fd, 503, "Private update storage unavailable"); return 0;
    }
    if (!upload_space_guard(fd, app_update_root, length, 3)) return 0;
    if (!upload_slot_acquire()) { app_update_fail(fd, 409, "Another upload is running"); return 0; }
    char previous[33];
    if (app_update_current(previous)) app_update_cleanup(previous);
    app_update_upload_job *job = calloc(1, sizeof(*job));
    if (!job) { upload_slot_release(); app_update_fail(fd, 503, "Update resources unavailable"); return 0; }
    if (download) {
        if (strlen(download) >= sizeof(job->download)) goto invalid;
        strcpy(job->download, download);
    }
    unsigned char random[16];
    int entropy = open("/dev/urandom", O_RDONLY);
    ssize_t count = entropy >= 0 ? read(entropy, random, sizeof(random)) : -1;
    if (entropy >= 0) close(entropy);
    if (count != (ssize_t)sizeof(random)) goto invalid;
    for (unsigned i = 0; i < 16; i++) snprintf(job->token + i * 2, 3, "%02x", random[i]);
    char stage[600], current[600], text[40];
    if (!app_update_path(stage, sizeof(stage), job->token, "")) goto invalid;
    stage[strlen(stage) - 1] = 0;
    if (mkdir(stage, 0700)) goto invalid;
    if (snprintf(current, sizeof(current), "%s/current", app_update_root) >= (int)sizeof(current)) goto invalid;
    snprintf(text, sizeof(text), "%s\n", job->token);
    if (!app_update_write(current, text) || !app_update_phase(job->token, "uploading", getpid())) goto invalid;
    job->upload.fd = fd; job->upload.content_length = length;
    job->upload.initial_length = download ? 0 : used; job->upload.header_length = download ? 0 : header_length;
    if (!download) memcpy(job->upload.initial, request, used);
    clock_gettime(CLOCK_MONOTONIC, &job->upload.started);
    atomic_store(&app_update_busy, 1);
    pthread_t thread;
    if (pthread_create(&thread, NULL, app_update_upload_worker, job)) {
        atomic_store(&app_update_busy, 0); app_update_phase(job->token, "failed", 0); goto invalid;
    }
    pthread_detach(thread);
    return 1;
invalid:
    free(job); upload_slot_release(); app_update_fail(fd, 503, "Cannot prepare private application upload"); return 0;
}

static void app_update_install_response(int fd, const char *request, const char *body, size_t length, const mqtt_client *mqtt, console_state *console) {
    if (!app_update_authorized(request, mqtt)) { app_update_fail(fd, 401, "LAN code confirmation required or temporarily locked"); return; }
    char token[33], requested[33], stage[600], state[256], path[600], phase[24];
    if (length != 32 || !app_update_current(token)) { app_update_fail(fd, 400, "Invalid update job"); return; }
    memcpy(requested, body, 32); requested[32] = 0;
    if (!app_update_token_valid(requested) || strcmp(requested, token)) { app_update_fail(fd, 409, "Application update selection changed"); return; }
    if (app_update_active() || !app_update_enabled() || !app_update_ready(mqtt, console)) {
        app_update_fail(fd, 409, "Printer must be connected, idle and cold with heating targets zero"); return;
    }
    if (!app_update_path(path, sizeof(path), token, "status.json") || !app_update_read(path, state, sizeof(state)) ||
        !extract_json_string(state, "phase", phase, sizeof(phase)) || strcmp(phase, "ready")) {
        app_update_fail(fd, 409, "A verified application package is required"); return;
    }
    if (!app_update_path(stage, sizeof(stage), token, "")) { app_update_fail(fd, 400, "Invalid staging directory"); return; }
    stage[strlen(stage) - 1] = 0;
    int barrier[2];
    if (pipe(barrier)) { app_update_fail(fd, 503, "Cannot start application update"); return; }
    long maximum = sysconf(_SC_OPEN_MAX); if (maximum < 0) maximum = 65536;
    pid_t child = fork();
    if (!child) {
        close(barrier[1]);
        char signal;
        if (read(barrier[0], &signal, 1) != 1) _exit(127);
        close(barrier[0]);
        if (setsid() < 0) _exit(127);
        for (int open_fd = 3; open_fd < maximum; open_fd++) close(open_fd);
        int null = open("/dev/null", O_RDWR);
        if (null >= 0) { dup2(null, 0); dup2(null, 1); dup2(null, 2); if (null > 2) close(null); }
        execl("/bin/sh", "sh", app_update_runner, stage, (char *)NULL);
        _exit(127);
    }
    close(barrier[0]);
    if (child < 0 || !app_update_phase(token, "queued", child)) {
        close(barrier[1]);
        if (child > 0) { kill(child, SIGKILL); waitpid(child, NULL, 0); }
        app_update_fail(fd, 503, "Cannot persist application update job"); return;
    }
    app_update_child = child;
    const char *reply = "{\"accepted\":true}\n";
    respond(fd, 202, "Accepted", "application/json", reply, strlen(reply));
    /* Finish the acceptance response before the independent worker can stop
     * the service. A post-response launch failure is persisted for polling. */
    if (write(barrier[1], "1", 1) != 1) {
        kill(child, SIGKILL);
        while (waitpid(child, NULL, 0) < 0 && errno == EINTR) {}
        app_update_child = 0;
        app_update_phase(token, "failed", 0);
    }
    close(barrier[1]);
}
