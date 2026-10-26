/* The browser fetches a small public index. Only the installed trust anchor
 * may turn it into a catalog; GitHub HTML, release order and filenames are
 * never treated as permission to execute an update. */
static int app_catalog_path(char *out, size_t capacity, const char *name) {
    return snprintf(out, capacity, "%s/%s", app_update_root, name) < (int)capacity;
}

static void app_catalog_get(int fd) {
    char path[600], json[8193];
    if (!app_update_private(app_update_root) || !app_catalog_path(path, sizeof(path), "catalog.json") ||
        !app_update_read(path, json, sizeof(json))) {
        const char *empty = "{\"schema\":1,\"expires\":0,\"releases\":[]}\n";
        respond(fd, 200, "OK", "application/json", empty, strlen(empty)); return;
    }
    respond(fd, 200, "OK", "application/json", json, strlen(json));
}

static void app_catalog_store(int fd, const char *body, size_t length) {
    if (!app_update_enabled() || length < 74 || length > 8264 || memcmp(body, "CC2CAT1\n", 8) ||
        app_update_active() || (mkdir(app_update_root, 0700) && errno != EEXIST) || !app_update_private(app_update_root)) {
        app_update_fail(fd, 400, "Invalid or unavailable signed update index"); return;
    }
    char input[600], output[600], final[600];
    if (!app_catalog_path(input, sizeof(input), "catalog.upload") || !app_catalog_path(output, sizeof(output), "catalog.checked") ||
        !app_catalog_path(final, sizeof(final), "catalog.json")) { app_update_fail(fd, 503, "Update storage unavailable"); return; }
    unlink(input); unlink(output);
    int source = open(input, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (source < 0) { app_update_fail(fd, 503, "Update storage unavailable"); return; }
    size_t written = 0;
    while (written < length) {
        ssize_t n = write(source, body + written, length - written);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        written += (size_t)n;
    }
    close(source);
    int result = open(output, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    int status = 0, ok = 0;
    pid_t child = -1;
    if (written == length && result >= 0) {
        long maximum = sysconf(_SC_OPEN_MAX); if (maximum < 0) maximum = 65536;
        child = fork();
        if (!child) {
            dup2(result, STDOUT_FILENO);
            int null = open("/dev/null", O_RDWR);
            if (null >= 0) { dup2(null, 0); dup2(null, 2); }
            for (int open_fd = 3; open_fd < maximum; open_fd++) close(open_fd);
            execl(app_update_verifier, app_update_verifier, "--catalog", input, (char *)NULL);
            _exit(127);
        }
    }
    if (result >= 0) close(result);
    if (child > 0) {
        int reaped = 0;
        for (unsigned i = 0; i < 300; i++) {
            pid_t done = waitpid(child, &status, WNOHANG);
            if (done == child) { reaped = 1; ok = WIFEXITED(status) && !WEXITSTATUS(status); break; }
            if (done < 0 && errno != EINTR) { reaped = 1; break; }
            struct timespec pause = {0, 10000000}; nanosleep(&pause, NULL);
        }
        if (!reaped) { kill(child, SIGKILL); while (waitpid(child, &status, 0) < 0 && errno == EINTR) {} }
    }
    unlink(input);
    if (!ok || rename(output, final)) {
        unlink(output); app_update_fail(fd, 400, "Update index signature rejected"); return;
    }
    app_catalog_get(fd);
}

static int app_catalog_download(int fd, const char *request, const char *body, size_t length,
    const mqtt_client *mqtt, console_state *console) {
    char path[600], json[8193], selected[1100], exact[1130];
    const char prefix[] = "https://github.com/Neme77/centauri-carbon-2-community/releases/download/";
    if (length < sizeof(prefix) || length >= sizeof(selected) || memchr(body, 0, length)) goto invalid;
    memcpy(selected, body, length); selected[length] = 0;
    if (strncmp(selected, prefix, sizeof(prefix) - 1)) goto invalid;
    for (size_t i = sizeof(prefix) - 1; i < length; i++)
        if (!isalnum((unsigned char)selected[i]) && selected[i] != '/' && selected[i] != '.' && selected[i] != '_' && selected[i] != '-') goto invalid;
    if (length < 10 || strcmp(selected + length - 10, ".cc2update")) goto invalid;
    if (!app_update_private(app_update_root) || !app_catalog_path(path, sizeof(path), "catalog.json") ||
        !app_update_read(path, json, sizeof(json))) goto invalid;
    snprintf(exact, sizeof(exact), "\"url\":\"%s\"", selected);
    if (!strstr(json, exact)) goto invalid;
    return app_update_upload_start(fd, request, 0, 0, mqtt, console, selected);
invalid:
    app_update_fail(fd, 400, "Select a package from the verified GitHub index"); return 0;
}
