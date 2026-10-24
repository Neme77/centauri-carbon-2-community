/* Persistent feature choices, independent of app update channels. No executable
 * or vendor configuration is stored here. Invalid storage always opts out. */
#ifndef CC2_EXPERIMENTS_STATE_H
#define CC2_EXPERIMENTS_STATE_H
typedef struct { int canvas_uds, quiet_motors; } experiment_choices;
static experiment_choices experiments;
static int experiments_storage_valid = 1;
static int experiments_cli_canvas;
static const char *experiments_path = "/opt/usr/cc2-control-experiments.json";

static int experiments_encode(char *out, size_t capacity, experiment_choices value) {
    int n = snprintf(out, capacity, "{\"schema\":1,\"canvas_uds\":%s,\"quiet_motors\":%s}\n",
                     value.canvas_uds ? "true" : "false", value.quiet_motors ? "true" : "false");
    return n > 0 && (size_t)n < capacity;
}

static int experiments_read(experiment_choices *out) {
    *out = (experiment_choices){0};
    int fd = open(experiments_path, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return errno == ENOENT;
    struct stat st;
    char body[128], expected[128];
    int ok = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == geteuid() &&
             !(st.st_mode & 0077) && st.st_size > 0 && st.st_size < (off_t)sizeof(body);
    ssize_t n = ok ? read(fd, body, sizeof(body) - 1) : -1;
    close(fd);
    if (!ok || n != st.st_size) return 0;
    body[n] = 0;
    for (int canvas = 0; canvas <= 1; canvas++) for (int quiet = 0; quiet <= 1; quiet++) {
        experiment_choices value = {canvas, quiet};
        if (experiments_encode(expected, sizeof(expected), value) && !strcmp(body, expected)) {
            *out = value; return 1;
        }
    }
    return 0;
}

static void experiments_load(void) {
    experiments_storage_valid = experiments_read(&experiments);
    if (!experiments_storage_valid) experiments = (experiment_choices){0};
}

static int experiments_save(experiment_choices next) {
    experiment_choices actual;
    if (!experiments_storage_valid || !experiments_read(&actual) ||
        actual.canvas_uds != experiments.canvas_uds || actual.quiet_motors != experiments.quiet_motors) return 0;
    char temporary[600], body[128];
    if (!experiments_encode(body, sizeof(body), next) ||
        snprintf(temporary, sizeof(temporary), "%s.new", experiments_path) >= (int)sizeof(temporary)) return 0;
    /* Refuse stale files and symlinks rather than clobbering somebody else's work. */
    int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return 0;
    int ok = write(fd, body, strlen(body)) == (ssize_t)strlen(body);
    if (fsync(fd)) ok = 0;
    if (close(fd)) ok = 0;
    if (ok && (!experiments_read(&actual) || actual.canvas_uds != experiments.canvas_uds ||
               actual.quiet_motors != experiments.quiet_motors)) ok = 0;
    if (ok && rename(temporary, experiments_path)) ok = 0;
    if (!ok) { unlink(temporary); return 0; }
    experiments = next;
    return 1;
}
#endif
