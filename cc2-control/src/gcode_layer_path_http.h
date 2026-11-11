/* HTTP worker for GET /api/gcode-files/layer-path. Included after gcode_download.h. */
static atomic_int active_layer_paths;
static pthread_mutex_t layer_path_cache_lock = PTHREAD_MUTEX_INITIALIZER;
typedef struct {
    int valid, layer;
    char storage[16], file[PATH_MAX_LOCAL];
    struct stat stamp;
    size_t length;
    char response[LAYER_PATH_JSON_MAX];
} layer_path_cache_slot;
static layer_path_cache_slot layer_path_cached;
typedef struct {
    int client, source, layer;
    char storage[16], file[PATH_MAX_LOCAL];
    struct stat stamp;
} layer_path_job;

static int layer_path_same_file(const struct stat *a, const struct stat *b) {
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino &&
           a->st_size == b->st_size && a->st_mtime == b->st_mtime;
}

static void layer_path_fail(int fd, int code, const char *status, const char *error) {
    char body[160];
    int n = snprintf(body, sizeof(body), "{\"error\":\"%s\"}\n", error);
    if (n > 0 && (size_t)n < sizeof(body))
        respond(fd, code, status, "application/json; charset=utf-8", body, (size_t)n);
}

static void *layer_path_worker(void *opaque) {
    layer_path_job *job = opaque;
    FILE *file = fdopen(job->source, "r");
    if (!file) {
        close(job->source);
        layer_path_fail(job->client, 500, "Internal Server Error", "Cannot open G-code file");
        close(job->client);
        free(job);
        atomic_fetch_sub(&active_layer_paths, 1);
        return NULL;
    }
    layer_point *points = malloc(sizeof(*points) * LAYER_PATH_COLLECT_MAX);
    int *starts = malloc(sizeof(*starts) * LAYER_PATH_PATHS_MAX);
    char *json = malloc(LAYER_PATH_JSON_MAX);
    if (!points || !starts || !json) {
        fclose(file);
        free(points); free(starts); free(json);
        layer_path_fail(job->client, 503, "Service Unavailable", "Out of memory");
        close(job->client);
        free(job);
        atomic_fetch_sub(&active_layer_paths, 1);
        return NULL;
    }
    layer_path_info info;
    struct timespec started;
    clock_gettime(CLOCK_MONOTONIC, &started);
    int rc = gcode_layer_path_parse(file, job->layer, points, LAYER_PATH_COLLECT_MAX,
                                    starts, LAYER_PATH_PATHS_MAX, &info, &started,
                                    LAYER_PATH_TIME_MS, LAYER_PATH_BYTES_MAX);
    fclose(file);
    if (rc == LAYER_PATH_OUTSIDE)
        layer_path_fail(job->client, 404, "Not Found", "Layer is outside the file");
    else if (rc == LAYER_PATH_LIMIT)
        layer_path_fail(job->client, 413, "Payload Too Large", "Layer path scan limit reached");
    else if (rc == LAYER_PATH_TOO_LARGE)
        layer_path_fail(job->client, 413, "Payload Too Large", "Layer path is too large");
    else if (rc != LAYER_PATH_OK)
        layer_path_fail(job->client, 500, "Internal Server Error", "Cannot read the G-code layer");
    else {
        int n = gcode_layer_path_json(json, LAYER_PATH_JSON_MAX, job->storage, job->file, &info, points, starts);
        if (n < 0)
            layer_path_fail(job->client, 413, "Payload Too Large", "Layer path is too large");
        else {
            respond(job->client, 200, "OK", "application/json; charset=utf-8", json, (size_t)n);
            pthread_mutex_lock(&layer_path_cache_lock);
            layer_path_cached.valid = 1;
            layer_path_cached.layer = job->layer;
            snprintf(layer_path_cached.storage, sizeof(layer_path_cached.storage), "%s", job->storage);
            snprintf(layer_path_cached.file, sizeof(layer_path_cached.file), "%s", job->file);
            layer_path_cached.stamp = job->stamp;
            layer_path_cached.length = (size_t)n;
            memcpy(layer_path_cached.response, json, (size_t)n);
            pthread_mutex_unlock(&layer_path_cache_lock);
        }
    }
    free(points); free(starts); free(json);
    close(job->client);
    free(job);
    atomic_fetch_sub(&active_layer_paths, 1);
    return NULL;
}

static int gcode_layer_path_start(int fd, const char *query) {
    char storage[16], file[LAYER_PATH_NAME_MAX];
    int layer = 0;
    int parsed = gcode_layer_path_query(query, storage, file, &layer);
    if (parsed == LAYER_PATH_BAD_LAYER) {
        layer_path_fail(fd, 400, "Bad Request", "Invalid layer");
        return 0;
    }
    const char *root;
    if (parsed != LAYER_PATH_OK || !gcode_storage_root(storage, &root)) {
        layer_path_fail(fd, 400, "Bad Request", "Invalid protected G-code path");
        return 0;
    }
    struct stat st;
    int source = download_open(root, file, &st);
    if (source < 0) {
        layer_path_fail(fd, 404, "Not Found", "G-code file unavailable");
        return 0;
    }
    int flags = fcntl(source, F_GETFL);
    if (flags >= 0) fcntl(source, F_SETFL, flags & ~O_NONBLOCK);
    pthread_mutex_lock(&layer_path_cache_lock);
    int hit = layer_path_cached.valid && layer_path_cached.layer == layer &&
              !strcmp(layer_path_cached.storage, storage) && !strcmp(layer_path_cached.file, file) &&
              layer_path_same_file(&st, &layer_path_cached.stamp);
    if (hit) {
        respond(fd, 200, "OK", "application/json; charset=utf-8",
                layer_path_cached.response, layer_path_cached.length);
        pthread_mutex_unlock(&layer_path_cache_lock);
        close(source);
        return 0;
    }
    pthread_mutex_unlock(&layer_path_cache_lock);
    int previous = atomic_fetch_add(&active_layer_paths, 1);
    layer_path_job *job = NULL;
    pthread_attr_t attr;
    pthread_t thread;
    int initialized = 0, launched = 0;
    if (previous < 1 && (job = malloc(sizeof(*job)))) {
        job->client = fd;
        job->source = source;
        job->layer = layer;
        job->stamp = st;
        snprintf(job->storage, sizeof(job->storage), "%s", storage);
        snprintf(job->file, sizeof(job->file), "%s", file);
        if (!pthread_attr_init(&attr)) {
            initialized = 1;
            if (!pthread_attr_setstacksize(&attr, 128 * 1024) &&
                !pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
                launched = !pthread_create(&thread, &attr, layer_path_worker, job);
        }
    }
    if (initialized) pthread_attr_destroy(&attr);
    if (launched) return 1;
    free(job);
    close(source);
    atomic_fetch_sub(&active_layer_paths, 1);
    layer_path_fail(fd, 503, "Service Unavailable", "Layer path busy; retry shortly");
    return 0;
}
