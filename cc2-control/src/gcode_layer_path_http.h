/* HTTP worker for GET /api/gcode-files/layer-path. Included after gcode_download.h. */
#include <sys/resource.h>
#include <sys/syscall.h>
static atomic_int active_layer_paths;
static pthread_mutex_t layer_path_cache_lock = PTHREAD_MUTEX_INITIALIZER;
/* Load limits for the printer's small CPU (seen: a full scan of a 78.7 MB file right after a print
 * start came with an MCU "Timer too close" shutdown). The scan worker runs at a low CPU and, where the
 * kernel's I/O scheduler supports it, idle I/O priority, so the printer's own processes come first.
 * After a scan, the next one waits at least as long as the last one took (LAYER_PATH_REST_PERCENT),
 * but not less than LAYER_PATH_REST_MIN_MS: a scan never runs more than about half of the time.
 * Requests in that pause are answered "busy", which the interface already retries. */
#define LAYER_PATH_NICE 10
#define LAYER_PATH_REST_MIN_MS 250
#define LAYER_PATH_REST_PERCENT 100
static long layer_path_rest_min_ms = LAYER_PATH_REST_MIN_MS; /* test seam */
static long layer_path_rest_percent = LAYER_PATH_REST_PERCENT; /* test seam */
static struct timespec layer_path_rest_until; /* guarded by layer_path_cache_lock */
typedef struct {
    int valid, layer;
    char storage[16], file[PATH_MAX_LOCAL];
    struct stat stamp;
    size_t length;
    char response[LAYER_PATH_JSON_MAX];
} layer_path_cache_slot;
static layer_path_cache_slot layer_path_cached;
/* Layer starts for the file currently being viewed. Dropped when the file's
 * device, inode, size or mtime changes (layer_path_same_file). One file only,
 * at most LAYER_PATH_INDEX_MAX marks, so a print does not keep a second copy
 * of the G-code. The worker is the only writer; the request thread never reads it. */
static struct {
    int valid, count;
    char storage[16], file[PATH_MAX_LOCAL];
    struct stat stamp;
    layer_path_mark marks[LAYER_PATH_INDEX_MAX];
} layer_path_index;
/* The lowest layer of a file whose scan ran into the limit. Reading up to a later layer costs
 * at least as much, so those requests are refused at once instead of scanning again: while
 * a large file prints, every new layer would otherwise burn the whole budget. Guarded by
 * layer_path_cache_lock; a changed file (size, time, inode) is scanned afresh, and so is
 * any layer once LAYER_PATH_LIMIT_HOLD_S have passed, in case the printer was only busy.
 * This holds for a scan that read the file from its start. A layer read through the file's layer
 * map costs the same whatever its number, so a limit hit there (only_layer) refuses just that layer,
 * and a layer map that is current for the file and serves the layer ends the refusal
 * (layer_path_map_serves): the map answers in milliseconds what the scan could not. */
#define LAYER_PATH_LIMIT_HOLD_S 300
static struct {
    int valid, layer, only_layer, map_useless; /* map_useless: a current map was found that serves no layer of this file */
    char storage[16], file[PATH_MAX_LOCAL];
    struct stat stamp;
    struct timespec at;
} layer_path_limited;
/* Counters for /api/health, kept in memory only (they start at 0 with the service and hold no file names,
 * paths or other private data). Guarded by layer_path_cache_lock. "scans" counts scans the worker ran,
 * also those that ended in a refusal such as "outside the file" or the scan limit; answers from the
 * remembered layer, or refused before reading, are no scan. "rejected" counts requests refused as not
 * fitting the running print (layer_path_guard), "busy" the 503 busy answers. "bytes_read" is what the
 * scans read from the file, counted from where each scan started; longest and last are scan times in
 * milliseconds, from the start of the scan to the end of reading. "read_blocked" counts the 503 answers
 * of the read hold (gcode_read_hold: the printer homes, levels, probes or starts a print) that refused a
 * new read of the layer path, the object preview or a picture, "read_stopped" the scans it ended. */
static struct {
    unsigned long scans, rejected, busy, read_blocked, read_stopped;
    unsigned long long bytes_read;
    long longest_ms, last_ms;
} layer_path_stats;
/* The answer while the hold is on; the page waits and asks again (lib/layer-path.ts gcodeReadHeld). */
#define GCODE_READ_HELD_ERROR "Printer is homing or starting a print; retry shortly"
static void layer_path_count_busy(void) {
    pthread_mutex_lock(&layer_path_cache_lock);
    layer_path_stats.busy++;
    pthread_mutex_unlock(&layer_path_cache_lock);
}
static void gcode_read_count_blocked(void) {
    pthread_mutex_lock(&layer_path_cache_lock);
    layer_path_stats.read_blocked++;
    pthread_mutex_unlock(&layer_path_cache_lock);
}
static void gcode_read_count_stopped(void) {
    pthread_mutex_lock(&layer_path_cache_lock);
    layer_path_stats.read_stopped++;
    pthread_mutex_unlock(&layer_path_cache_lock);
}
static void layer_path_count_rejected(void) {
    pthread_mutex_lock(&layer_path_cache_lock);
    layer_path_stats.rejected++;
    pthread_mutex_unlock(&layer_path_cache_lock);
}
/* The members that /api/health adds, without braces. */
static void layer_path_health_json(char *out, size_t cap) {
    pthread_mutex_lock(&layer_path_cache_lock);
    int n = snprintf(out, cap,
        "\"layer_path_scans\":%lu,\"layer_path_rejected\":%lu,\"layer_path_busy\":%lu,"
        "\"layer_path_bytes_read\":%llu,\"layer_path_longest_ms\":%ld,\"layer_path_last_ms\":%ld,"
        "\"gcode_read_blocked\":%lu,\"gcode_read_stopped\":%lu,"
        "\"layer_map_hits\":%lu,\"layer_map_fallbacks\":%lu,\"layer_map_built\":%lu,\"layer_map_failed\":%lu,"
        "\"layer_map_bad\":%lu",
        layer_path_stats.scans, layer_path_stats.rejected, layer_path_stats.busy,
        layer_path_stats.bytes_read, layer_path_stats.longest_ms, layer_path_stats.last_ms,
        layer_path_stats.read_blocked, layer_path_stats.read_stopped,
        atomic_load(&layer_store_stat_hits), atomic_load(&layer_store_stat_fallbacks),
        atomic_load(&layer_store_stat_built), atomic_load(&layer_store_stat_failed), atomic_load(&layer_store_stat_bad));
    pthread_mutex_unlock(&layer_path_cache_lock);
    if (n < 0 || (size_t)n >= cap) out[0] = 0;
}
/* Test seam: runs on the worker after the scan clock has started. */
static void (*layer_path_scan_hook)(void);
typedef struct {
    int client, source, layer;
    char storage[16], file[PATH_MAX_LOCAL];
    char path[PATH_MAX_LOCAL * 2]; /* root/file: the key of the file's layer map */
    struct stat stamp;
} layer_path_job;

/* The two readings can lie weeks apart (the last scan against now), so the span is computed in
 * 64 bits and clamped: long has 32 bits on the printer. */
static long layer_path_ms_between(const struct timespec *from, const struct timespec *to) {
    long long ms = (long long)(to->tv_sec - from->tv_sec) * 1000LL + (to->tv_nsec - from->tv_nsec) / 1000000L;
    return ms > LONG_MAX ? LONG_MAX : ms < LONG_MIN ? LONG_MIN : (long)ms;
}

/* Runs on the worker thread: only this thread gets the lower priority. Failures are ignored, the
 * scan then runs as before. */
static void layer_path_lower_priority(void) {
#if defined(__linux__) && defined(SYS_gettid)
    pid_t tid = (pid_t)syscall(SYS_gettid);
    (void)setpriority(PRIO_PROCESS, (id_t)tid, LAYER_PATH_NICE);
#ifdef SYS_ioprio_set
    (void)syscall(SYS_ioprio_set, 1 /* IOPRIO_WHO_PROCESS */, (int)tid, 3 << 13 /* IOPRIO_CLASS_IDLE */);
#endif
#endif
}

/* Ends a worker: sets the pause before the next scan and frees the worker slot. */
static void layer_path_release(const struct timespec *started) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    long took = started ? layer_path_ms_between(started, &now) : 0;
    if (took < 0) took = 0;
    long rest = took * layer_path_rest_percent / 100;
    if (rest < layer_path_rest_min_ms) rest = layer_path_rest_min_ms;
    struct timespec until = now;
    until.tv_sec += rest / 1000;
    until.tv_nsec += (rest % 1000) * 1000000L;
    if (until.tv_nsec >= 1000000000L) { until.tv_sec++; until.tv_nsec -= 1000000000L; }
    pthread_mutex_lock(&layer_path_cache_lock);
    layer_path_rest_until = until;
    pthread_mutex_unlock(&layer_path_cache_lock);
    atomic_fetch_sub(&active_layer_paths, 1);
}

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

/* The remembered limit of this file (see layer_path_limited) must not refuse a layer that a current layer map serves.
 * Reads the map like the worker does (and the first and last 64 KiB of the file for its key). */
enum { LAYER_MAP_NO = 0, LAYER_MAP_THIS_LAYER, LAYER_MAP_ALL_LAYERS, LAYER_MAP_USELESS };
static int layer_path_map_serves(const char *path, int source, int layer) {
    int copy = dup(source);
    FILE *file = copy >= 0 ? fdopen(copy, "r") : NULL;
    if (!file) { if (copy >= 0) close(copy); return LAYER_MAP_NO; }
    layer_index index;
    int verdict = LAYER_MAP_NO;
    if (layer_store_load(path, file, &index) == 0) {
        if (!index.seen_marker || index.repeats) verdict = LAYER_MAP_USELESS; /* every layer goes to the full scan */
        else if (layer_index_serves_all(&index)) verdict = LAYER_MAP_ALL_LAYERS;
        else if (layer_index_can_serve(&index, layer)) verdict = LAYER_MAP_THIS_LAYER;
        /* A map that cannot be read at this layer's stored offset serves nothing for it: the refusal stays, as without a map
         * (the worker would turn to the full scan and run into the limit again, on every request). */
        if (verdict == LAYER_MAP_THIS_LAYER || verdict == LAYER_MAP_ALL_LAYERS) {
            const layer_index_entry *entry = layer_index_find(&index, layer - 1);
            if (entry && !layer_index_entry_plausible(file, &index, entry)) verdict = LAYER_MAP_NO;
        }
        /* "Serves every layer" ends the whole refusal, so the map's last entry (the first to be off when a map is shifted
         * from some line on) must be readable too; otherwise it serves this layer only and the refusal stays. */
        if (verdict == LAYER_MAP_ALL_LAYERS && index.count > 0 &&
            !layer_index_entry_plausible(file, &index, &index.entries[index.count - 1]))
            verdict = LAYER_MAP_THIS_LAYER;
        layer_index_free(&index);
    }
    fclose(file);
    return verdict;
}

static void *layer_path_worker(void *opaque) {
    layer_path_job *job = opaque;
    layer_path_lower_priority();
    FILE *file = fdopen(job->source, "r");
    if (!file) {
        close(job->source);
        layer_path_fail(job->client, 500, "Internal Server Error", "Cannot open G-code file");
        close(job->client);
        free(job);
        layer_path_release(NULL);
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
        layer_path_release(NULL);
        return NULL;
    }
    layer_path_info info;
    struct timespec started;
    clock_gettime(CLOCK_MONOTONIC, &started);
    if (layer_path_scan_hook) layer_path_scan_hook();
    /* With a current layer map the layer is read directly from its start; without one (or for
     * a file the map cannot serve) the file is read from the start as before, and a missing or
     * stale map is queued so the next request can jump. */
    int rc = LAYER_INDEX_NONE;
    int mapped = 0; /* the layer was read through the map, not from the start of the file */
    long long map_from = -1; /* where that read began: the map's offset of the layer (-1: no read through the map) */
    layer_index index;
    if (layer_store_load(job->path, file, &index) == 0) {
        const layer_index_entry *at_layer = layer_index_find(&index, job->layer - 1);
        layer_index_key identity = index.key;
        rc = gcode_layer_path_parse_indexed(file, &index, job->layer, points, LAYER_PATH_COLLECT_MAX,
                                            starts, LAYER_PATH_PATHS_MAX, &info, &started,
                                            LAYER_PATH_TIME_MS, LAYER_INDEX_LAYER_BYTES_MAX);
        if (at_layer && rc != LAYER_INDEX_NONE && rc != LAYER_INDEX_BAD) map_from = at_layer->offset;
        layer_index_free(&index);
        if (rc == LAYER_INDEX_BAD) {
            /* A current map with a wrong offset: read from the start now, and have the map made again (once). */
            layer_store_distrust(job->path, &identity);
            rc = LAYER_INDEX_NONE;
        }
        mapped = rc != LAYER_INDEX_NONE;
        atomic_fetch_add(rc == LAYER_INDEX_NONE ? &layer_store_stat_fallbacks : &layer_store_stat_hits, 1);
    } else if (!strcmp(job->storage, "internal")) {
        /* Only files in the printer's own storage get a map: a background read of a USB stick could hang on a
         * removed stick, and a USB print runs from its internal copy anyway. */
        layer_store_note_request(job->path);
        atomic_fetch_add(&layer_store_stat_fallbacks, 1);
    } else {
        atomic_fetch_add(&layer_store_stat_fallbacks, 1);
    }
    layer_path_mark *found = NULL;
    int found_count = 0;
    layer_path_mark resume;
    int have_resume = 0;
    if (rc == LAYER_INDEX_NONE) {
        found = malloc(sizeof(*found) * LAYER_PATH_INDEX_MAX);
        pthread_mutex_lock(&layer_path_cache_lock);
        int indexed = layer_path_index.valid && !strcmp(layer_path_index.storage, job->storage) &&
                      !strcmp(layer_path_index.file, job->file) &&
                      layer_path_same_file(&job->stamp, &layer_path_index.stamp);
        if (!indexed) {
            layer_path_index.valid = 1;
            layer_path_index.count = 0;
            snprintf(layer_path_index.storage, sizeof(layer_path_index.storage), "%s", job->storage);
            snprintf(layer_path_index.file, sizeof(layer_path_index.file), "%s", job->file);
            layer_path_index.stamp = job->stamp;
        } else if (layer_path_mark_pick(layer_path_index.marks, layer_path_index.count, job->layer - 1, &resume))
            have_resume = 1;
        pthread_mutex_unlock(&layer_path_cache_lock);
        rewind(file);
        rc = gcode_layer_path_parse_at(file, job->layer, points, LAYER_PATH_COLLECT_MAX,
                                       starts, LAYER_PATH_PATHS_MAX, &info, &started,
                                       LAYER_PATH_TIME_MS, LAYER_PATH_BYTES_MAX,
                                       have_resume ? &resume : NULL,
                                       found, found ? &found_count : NULL,
                                       found ? LAYER_PATH_INDEX_MAX : 0);
    }
    {
        /* The parser resumes at the saved mark only when it lies within the byte budget (as it checks itself). */
        long long from = have_resume && resume.offset >= 0 &&
                         (unsigned long long)resume.offset <= (unsigned long long)LAYER_PATH_BYTES_MAX ? resume.offset : 0;
        long long at = (long long)ftello(file);
        /* A read through the map began at its offset of the layer; one that answered without reading ("outside the file")
         * read nothing, and the file position is wherever the key left it. */
        if (mapped) { if (map_from >= 0) from = map_from; else at = from; }
        struct timespec read_end;
        clock_gettime(CLOCK_MONOTONIC, &read_end);
        long took = layer_path_ms_between(&started, &read_end);
        pthread_mutex_lock(&layer_path_cache_lock);
        layer_path_stats.scans++;
        if (at > from) layer_path_stats.bytes_read += (unsigned long long)(at - from);
        if (took < 0) took = 0;
        layer_path_stats.last_ms = took;
        if (took > layer_path_stats.longest_ms) layer_path_stats.longest_ms = took;
        pthread_mutex_unlock(&layer_path_cache_lock);
    }
    fclose(file);
    if (found && found_count > 0) {
        pthread_mutex_lock(&layer_path_cache_lock);
        int same = layer_path_index.valid && !strcmp(layer_path_index.storage, job->storage) &&
                   !strcmp(layer_path_index.file, job->file) &&
                   layer_path_same_file(&job->stamp, &layer_path_index.stamp);
        if (same) {
            for (int i = 0; i < found_count; i++)
                layer_path_mark_add(layer_path_index.marks, &layer_path_index.count, LAYER_PATH_INDEX_MAX,
                                    found[i].layer, &found[i]);
        }
        pthread_mutex_unlock(&layer_path_cache_lock);
    }
    free(found);
    if (rc == LAYER_PATH_HELD) {
        /* The hold came on while reading: nothing of this scan is answered or remembered as a result
         * (the layer starts it passed stay, they are exact either way). */
        gcode_read_count_stopped();
        layer_path_fail(job->client, 503, "Service Unavailable", GCODE_READ_HELD_ERROR);
    } else if (rc == LAYER_PATH_OUTSIDE)
        layer_path_fail(job->client, 404, "Not Found", "Layer is outside the file");
    else if (rc == LAYER_PATH_LIMIT) {
        layer_path_fail(job->client, 413, "Payload Too Large", "Layer path scan limit reached");
        pthread_mutex_lock(&layer_path_cache_lock);
        int same = layer_path_limited.valid && !strcmp(layer_path_limited.storage, job->storage) &&
                   !strcmp(layer_path_limited.file, job->file) &&
                   layer_path_same_file(&job->stamp, &layer_path_limited.stamp);
        /* A scan from the start of the file is the genuine overload: it replaces a single-layer limit and lowers
         * an earlier one. A limit from a map read never replaces one from a scan, and stays on its one layer. */
        int replace = !same || (mapped ? layer_path_limited.only_layer
                                       : layer_path_limited.only_layer || job->layer < layer_path_limited.layer);
        if (replace) {
            layer_path_limited.valid = 1;
            layer_path_limited.layer = job->layer;
            layer_path_limited.only_layer = mapped;
            layer_path_limited.map_useless = 0;
            snprintf(layer_path_limited.storage, sizeof(layer_path_limited.storage), "%s", job->storage);
            snprintf(layer_path_limited.file, sizeof(layer_path_limited.file), "%s", job->file);
            layer_path_limited.stamp = job->stamp;
            clock_gettime(CLOCK_MONOTONIC, &layer_path_limited.at);
        }
        pthread_mutex_unlock(&layer_path_cache_lock);
    }
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
    layer_path_release(&started);
    return NULL;
}

/* `held`: the printer must not be disturbed now (gcode_read_hold_now in main.c). The remembered layer and the
 * scan-limit answer still come; no scan begins. */
static int gcode_layer_path_begin(int fd, const char *query, int held) {
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
    struct timespec limit_now;
    clock_gettime(CLOCK_MONOTONIC, &limit_now);
    if (layer_path_limited.valid && limit_now.tv_sec - layer_path_limited.at.tv_sec >= LAYER_PATH_LIMIT_HOLD_S)
        layer_path_limited.valid = 0;
    int limited = layer_path_limited.valid &&
                  (layer_path_limited.only_layer ? layer == layer_path_limited.layer : layer >= layer_path_limited.layer) &&
                  !strcmp(layer_path_limited.storage, storage) && !strcmp(layer_path_limited.file, file) &&
                  layer_path_same_file(&st, &layer_path_limited.stamp);
    int scan_limit = limited && !layer_path_limited.only_layer && !layer_path_limited.map_useless;
    struct timespec limit_at = layer_path_limited.at;
    int resting = layer_path_ms_between(&limit_now, &layer_path_rest_until) > 0;
    pthread_mutex_unlock(&layer_path_cache_lock);
    if (scan_limit) {
        /* The limit came from a scan from the start of the file; a current layer map answers without one. */
        char map_file[PATH_MAX_LOCAL * 2], map_path[PATH_MAX_LOCAL * 2 + 128];
        if (snprintf(map_file, sizeof(map_file), "%s/%s", root, file) < (int)sizeof(map_file)) {
            if (held) {
                /* No G-code read while the printer homes or starts a print (gcode_read_hold): only look whether a
                 * map file exists; the held answer below (503, the page asks again) then replaces the 413. */
                if (layer_store_index_path(map_file, map_path, sizeof(map_path)) == 0 && access(map_path, F_OK) == 0)
                    limited = 0;
            } else {
                int verdict = layer_path_map_serves(map_file, source, layer);
                if (verdict != LAYER_MAP_NO) {
                    pthread_mutex_lock(&layer_path_cache_lock);
                    /* Only the limit that was seen above: a newer one (another scan) is left alone. */
                    if (layer_path_limited.valid && !layer_path_limited.only_layer &&
                        layer_path_limited.at.tv_sec == limit_at.tv_sec && layer_path_limited.at.tv_nsec == limit_at.tv_nsec &&
                        !strcmp(layer_path_limited.storage, storage) && !strcmp(layer_path_limited.file, file) &&
                        layer_path_same_file(&st, &layer_path_limited.stamp)) {
                        if (verdict == LAYER_MAP_ALL_LAYERS) layer_path_limited.valid = 0; /* the map serves every layer */
                        else if (verdict == LAYER_MAP_USELESS) layer_path_limited.map_useless = 1; /* do not read it again */
                    }
                    pthread_mutex_unlock(&layer_path_cache_lock);
                    if (verdict != LAYER_MAP_USELESS) limited = 0; /* served by the map, even if the limit stays for other layers */
                }
            }
        }
    }
    if (limited) {
        close(source);
        layer_path_fail(fd, 413, "Payload Too Large", "Layer path scan limit reached");
        return 0;
    }
    if (held) {
        close(source);
        gcode_read_count_blocked();
        layer_path_fail(fd, 503, "Service Unavailable", GCODE_READ_HELD_ERROR);
        return 0;
    }
    if (resting) {
        close(source);
        layer_path_count_busy();
        layer_path_fail(fd, 503, "Service Unavailable", "Layer path busy; retry shortly");
        return 0;
    }
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
        snprintf(job->path, sizeof(job->path), "%s/%s", root, file);
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
    layer_path_count_busy();
    layer_path_fail(fd, 503, "Service Unavailable", "Layer path busy; retry shortly");
    return 0;
}
/* Without the read hold (the tests drive the worker directly). */
static inline int gcode_layer_path_start(int fd, const char *query) { return gcode_layer_path_begin(fd, query, 0); }
