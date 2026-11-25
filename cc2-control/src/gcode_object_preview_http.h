/* HTTP worker for GET /api/gcode-files/object-preview. Included after gcode_layer_path_http.h: it shares the
 * layer path's worker slot (active_layer_paths), its pause after a scan, its lower priority and its
 * /api/health counters, so a layer-path scan and an object-preview scan never run at the same time.
 * The first request for a file reads it once and remembers every object; later requests for that file,
 * for any object, are answered from memory without reading, also during the pause and the read hold. A print
 * start keeps what is remembered, or being read, for the file of that print; the start protection still
 * refuses its requests until layer 1, and then they are answered from memory instead of a second read. A read
 * that the read hold overtakes (gcode_read_hold: the printer homes, levels, probes or starts a print) ends at
 * its next line with 503 and is not remembered. */
#include "gcode_object_preview.h"

/* The remembered file goes this long after the last request to the route; a test build may lower it. */
#ifndef OBJECT_PREVIEW_KEEP_S
#define OBJECT_PREVIEW_KEEP_S (30 * 60)
#endif
static long object_preview_keep_s = OBJECT_PREVIEW_KEEP_S; /* test seam */
static pthread_mutex_t object_preview_lock = PTHREAD_MUTEX_INITIALIZER;
/* One file at a time, guarded by object_preview_lock. generation grows with every drop, so a scan that
 * started before a drop (a new print, another file) answers its request but is not remembered. */
static struct {
    int valid;
    char storage[16], file[PATH_MAX_LOCAL];
    struct stat stamp;
    object_preview_result result;
    unsigned long generation;
    int have_request;
    struct timespec last_request;
} object_preview_cached;
/* A file whose scan ran into the time limit, refused at once for LAYER_PATH_LIMIT_HOLD_S unless it changes.
 * Guarded by object_preview_lock. */
static struct {
    int valid;
    char storage[16], file[PATH_MAX_LOCAL];
    struct stat stamp;
    struct timespec at;
} object_preview_limited;
/* The file a scan is reading right now, so a print start can tell whether that scan is for its file.
 * Guarded by object_preview_lock. */
static struct {
    int active;
    char storage[16], file[PATH_MAX_LOCAL];
} object_preview_reading;
/* Test seam: runs on the worker after the scan clock has started. */
static void (*object_preview_scan_hook)(void);
typedef struct {
    int client, source;
    char storage[16], file[PATH_MAX_LOCAL], object[OBJECT_PREVIEW_NAME_MAX];
    struct stat stamp;
    unsigned long generation;
} object_preview_job;

/* Caller holds object_preview_lock. Frees the remembered file. */
static void object_preview_drop_locked(void) {
    if (object_preview_cached.valid) object_preview_free(&object_preview_cached.result);
    object_preview_cached.valid = 0;
    object_preview_cached.generation++;
}

/* A new print started or the running one ended (main loop), or the file changed. */
static void object_preview_forget(void) {
    pthread_mutex_lock(&object_preview_lock);
    object_preview_drop_locked();
    pthread_mutex_unlock(&object_preview_lock);
}

/* Caller holds object_preview_lock. Drops the file OBJECT_PREVIEW_KEEP_S after the last request. */
static void object_preview_expire_locked(const struct timespec *now) {
    if (object_preview_cached.valid && object_preview_cached.have_request &&
        layer_path_ms_between(&object_preview_cached.last_request, now) >= object_preview_keep_s * 1000L)
        object_preview_drop_locked();
}

/* Runs on every pass of the main loop (at least once a second), so the memory goes without a request. */
static void object_preview_expire(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    pthread_mutex_lock(&object_preview_lock);
    object_preview_expire_locked(&now);
    pthread_mutex_unlock(&object_preview_lock);
}

/* Answers one object of a complete result: 200, or 404 when the file has no objects or not this one. */
static void object_preview_answer(int fd, const char *storage, const char *file, const object_preview_result *r,
                                  const char *object) {
    if (r->count == 0) { layer_path_fail(fd, 404, "Not Found", "No objects in the G-code file"); return; }
    int index = object_preview_find(r, object);
    if (index < 0) { layer_path_fail(fd, 404, "Not Found", "Object not found"); return; }
    size_t length = 0;
    char *json = gcode_object_preview_json(storage, file, r, index, &length);
    if (!json) { layer_path_fail(fd, 503, "Service Unavailable", "Out of memory"); return; }
    respond(fd, 200, "OK", "application/json; charset=utf-8", json, length);
    free(json);
}

static void *object_preview_worker(void *opaque) {
    object_preview_job *job = opaque;
    layer_path_lower_priority();
    FILE *file = fdopen(job->source, "r");
    if (!file) {
        close(job->source);
        layer_path_fail(job->client, 500, "Internal Server Error", "Cannot open G-code file");
        pthread_mutex_lock(&object_preview_lock);
        object_preview_reading.active = 0;
        pthread_mutex_unlock(&object_preview_lock);
        close(job->client);
        free(job);
        layer_path_release(NULL);
        return NULL;
    }
    struct timespec started;
    clock_gettime(CLOCK_MONOTONIC, &started);
    if (object_preview_scan_hook) object_preview_scan_hook();
    object_preview_result result;
    size_t bytes = 0;
    int rc = gcode_object_preview_parse(file, &result, &started, LAYER_PATH_TIME_MS, OBJECT_PREVIEW_BYTES_MAX, &bytes);
    {
        long long at = (long long)ftello(file); /* what was read, also bytes the line count leaves out */
        if (at > 0) bytes = (size_t)at;
        struct timespec read_end;
        clock_gettime(CLOCK_MONOTONIC, &read_end);
        long took = layer_path_ms_between(&started, &read_end);
        if (took < 0) took = 0;
        pthread_mutex_lock(&layer_path_cache_lock);
        layer_path_stats.scans++;
        layer_path_stats.bytes_read += (unsigned long long)bytes;
        layer_path_stats.last_ms = took;
        if (took > layer_path_stats.longest_ms) layer_path_stats.longest_ms = took;
        pthread_mutex_unlock(&layer_path_cache_lock);
    }
    fclose(file);
    if (rc == OBJECT_PREVIEW_OK) {
        object_preview_answer(job->client, job->storage, job->file, &result, job->object);
        pthread_mutex_lock(&object_preview_lock);
        if (object_preview_cached.generation == job->generation) {
            if (object_preview_cached.valid) object_preview_free(&object_preview_cached.result);
            object_preview_cached.valid = 1;
            snprintf(object_preview_cached.storage, sizeof(object_preview_cached.storage), "%s", job->storage);
            snprintf(object_preview_cached.file, sizeof(object_preview_cached.file), "%s", job->file);
            object_preview_cached.stamp = job->stamp;
            object_preview_cached.result = result;
            memset(&result, 0, sizeof(result));
        }
        pthread_mutex_unlock(&object_preview_lock);
    } else if (rc == OBJECT_PREVIEW_LIMIT) {
        layer_path_fail(job->client, 413, "Payload Too Large", "Object preview scan limit reached");
        pthread_mutex_lock(&object_preview_lock);
        object_preview_limited.valid = 1;
        snprintf(object_preview_limited.storage, sizeof(object_preview_limited.storage), "%s", job->storage);
        snprintf(object_preview_limited.file, sizeof(object_preview_limited.file), "%s", job->file);
        object_preview_limited.stamp = job->stamp;
        clock_gettime(CLOCK_MONOTONIC, &object_preview_limited.at);
        pthread_mutex_unlock(&object_preview_lock);
    } else if (rc == OBJECT_PREVIEW_HELD) {
        gcode_read_count_stopped();
        layer_path_fail(job->client, 503, "Service Unavailable", GCODE_READ_HELD_ERROR);
    } else if (rc == OBJECT_PREVIEW_TOO_LARGE)
        layer_path_fail(job->client, 413, "Payload Too Large", "Object preview file too large");
    else if (rc == OBJECT_PREVIEW_NO_MEMORY)
        layer_path_fail(job->client, 503, "Service Unavailable", "Out of memory");
    else
        layer_path_fail(job->client, 500, "Internal Server Error", "Cannot read the G-code file");
    pthread_mutex_lock(&object_preview_lock);
    object_preview_reading.active = 0;
    pthread_mutex_unlock(&object_preview_lock);
    object_preview_free(&result); /* what was not remembered */
    close(job->client);
    free(job);
    layer_path_release(&started);
    return NULL;
}

/* `held`: the printer must not be disturbed now (gcode_read_hold_now in main.c). The remembered file and the
 * size and scan-limit answers still come; no read begins. */
static int gcode_object_preview_begin(int fd, const char *query, int held) {
    char storage[16], file[LAYER_PATH_NAME_MAX], object[OBJECT_PREVIEW_NAME_MAX];
    int parsed = gcode_object_preview_query(query, storage, file, object);
    if (parsed == OBJECT_PREVIEW_BAD_OBJECT) {
        layer_path_fail(fd, 400, "Bad Request", "Invalid object");
        return 0;
    }
    const char *root;
    if (parsed != OBJECT_PREVIEW_OK || !gcode_storage_root(storage, &root)) {
        layer_path_fail(fd, 400, "Bad Request", "Invalid protected G-code path");
        return 0;
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    pthread_mutex_lock(&object_preview_lock);
    object_preview_expire_locked(&now);
    object_preview_cached.have_request = 1;
    object_preview_cached.last_request = now;
    pthread_mutex_unlock(&object_preview_lock);
    struct stat st;
    int source = download_open(root, file, &st);
    if (source < 0) {
        layer_path_fail(fd, 404, "Not Found", "G-code file unavailable");
        return 0;
    }
    int flags = fcntl(source, F_GETFL);
    if (flags >= 0) fcntl(source, F_SETFL, flags & ~O_NONBLOCK);
    pthread_mutex_lock(&object_preview_lock);
    if (object_preview_cached.valid && !strcmp(object_preview_cached.storage, storage) &&
        !strcmp(object_preview_cached.file, file)) {
        if (layer_path_same_file(&st, &object_preview_cached.stamp)) {
            object_preview_answer(fd, storage, file, &object_preview_cached.result, object);
            pthread_mutex_unlock(&object_preview_lock);
            close(source);
            return 0;
        }
        object_preview_drop_locked(); /* the file changed */
    }
    if (object_preview_limited.valid && now.tv_sec - object_preview_limited.at.tv_sec >= LAYER_PATH_LIMIT_HOLD_S)
        object_preview_limited.valid = 0;
    int limited = object_preview_limited.valid && !strcmp(object_preview_limited.storage, storage) &&
                  !strcmp(object_preview_limited.file, file) && layer_path_same_file(&st, &object_preview_limited.stamp);
    pthread_mutex_unlock(&object_preview_lock);
    if ((unsigned long long)st.st_size > (unsigned long long)OBJECT_PREVIEW_BYTES_MAX) {
        close(source);
        layer_path_fail(fd, 413, "Payload Too Large", "Object preview file too large");
        return 0;
    }
    if (limited) {
        close(source);
        layer_path_fail(fd, 413, "Payload Too Large", "Object preview scan limit reached");
        return 0;
    }
    if (held) {
        close(source);
        gcode_read_count_blocked();
        layer_path_fail(fd, 503, "Service Unavailable", GCODE_READ_HELD_ERROR);
        return 0;
    }
    pthread_mutex_lock(&layer_path_cache_lock);
    int resting = layer_path_ms_between(&now, &layer_path_rest_until) > 0;
    pthread_mutex_unlock(&layer_path_cache_lock);
    if (resting) {
        close(source);
        layer_path_count_busy();
        layer_path_fail(fd, 503, "Service Unavailable", "Object preview busy; retry shortly");
        return 0;
    }
    int previous = atomic_fetch_add(&active_layer_paths, 1);
    object_preview_job *job = NULL;
    pthread_attr_t attr;
    pthread_t thread;
    int initialized = 0, launched = 0;
    if (previous < 1 && (job = malloc(sizeof(*job)))) {
        job->client = fd;
        job->source = source;
        job->stamp = st;
        snprintf(job->storage, sizeof(job->storage), "%s", storage);
        snprintf(job->file, sizeof(job->file), "%s", file);
        snprintf(job->object, sizeof(job->object), "%s", object);
        /* Another file replaces the remembered one: it goes now, not after the scan, so two files are
         * never held at once. */
        pthread_mutex_lock(&object_preview_lock);
        if (object_preview_cached.valid) object_preview_drop_locked();
        job->generation = object_preview_cached.generation;
        object_preview_reading.active = 1;
        snprintf(object_preview_reading.storage, sizeof(object_preview_reading.storage), "%s", storage);
        snprintf(object_preview_reading.file, sizeof(object_preview_reading.file), "%s", file);
        pthread_mutex_unlock(&object_preview_lock);
        if (!pthread_attr_init(&attr)) {
            initialized = 1;
            if (!pthread_attr_setstacksize(&attr, 128 * 1024) &&
                !pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED))
                launched = !pthread_create(&thread, &attr, object_preview_worker, job);
        }
    }
    if (initialized) pthread_attr_destroy(&attr);
    if (launched) return 1;
    if (job) {
        pthread_mutex_lock(&object_preview_lock);
        object_preview_reading.active = 0;
        pthread_mutex_unlock(&object_preview_lock);
    }
    free(job);
    close(source);
    atomic_fetch_sub(&active_layer_paths, 1);
    layer_path_count_busy();
    layer_path_fail(fd, 503, "Service Unavailable", "Object preview busy; retry shortly");
    return 0;
}
/* Without the read hold (the tests drive the worker directly). */
static inline int gcode_object_preview_start(int fd, const char *query) { return gcode_object_preview_begin(fd, query, 0); }
