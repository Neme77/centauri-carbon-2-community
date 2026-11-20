/* Background builder and store for layer maps (gcode_layer_index.h).
 *
 * New or copied G-code files are queued with layer_store_note(). One low-priority thread
 * builds their maps, but only while the printer is idle: the main loop reports that with
 * layer_store_set_idle(). When a print starts during a build, the pass stops at the next
 * 64 KiB block, writes nothing and resumes once the printer is idle again. The thread also
 * pauses briefly every 64 KiB so the build never takes a CPU for long.
 *
 * Maps live in layer_store_dir (CC2 Control's own data folder), one file per G-code path,
 * never next to the G-code. A deleted G-code file takes its map with it (layer_store_forget). */
#ifndef CC2_GCODE_LAYER_INDEX_STORE_H
#define CC2_GCODE_LAYER_INDEX_STORE_H
#include "gcode_layer_index.h"
#include <dirent.h>
#include <pthread.h>
#include <stdatomic.h>
#include <strings.h>
#include <sys/resource.h>
#include <sys/stat.h>

#define LAYER_STORE_QUEUE_MAX 32
#define LAYER_STORE_PATH_MAX 1024
/* Maps of files that were removed other than through CC2 Control (touchscreen, USB, SSH) are never asked
 * for again; only the newest LAYER_STORE_MAPS_MAX stay (one map is at most about 900 KB, usually far less). */
#define LAYER_STORE_MAPS_MAX 64
/* A full pass of a 250 MB file is far below this; it only ends a pass that hangs. */
#define LAYER_STORE_BUILD_MS (10L * 60L * 1000L)

static const char *layer_store_dir = "./layer-index";
static atomic_int layer_store_idle;
static atomic_int layer_store_stop;
static long layer_store_pause_us = 2000; /* per 64 KiB block */
static long layer_store_poll_ms = 2000;  /* how often a busy printer is checked again */
static int layer_store_max_maps = LAYER_STORE_MAPS_MAX;
/* Counted for /api/health (in memory only): maps built and builds that failed by the builder, layer requests answered through a
 * map (hits) and requests that read the file from its start although it was asked for (fallbacks: no current map, or one the
 * index cannot serve). */
static atomic_ulong layer_store_stat_built, layer_store_stat_failed, layer_store_stat_hits, layer_store_stat_fallbacks;
/* After a start the newest files already on the printer get their maps in the idle time; a file only gets one when somebody opens it
 * otherwise, and that first open pays the full scan. */
#define LAYER_STORE_PREWARM_MAX 32
/* A build that failed (full storage, unreadable file) is not started again by every layer request: a request-triggered
 * note waits this long; an upload, copy or import of the file (a new file) tries at once. */
#define LAYER_STORE_FAILED_MAX 16
static long layer_store_retry_s = 600;
static struct { char path[LAYER_STORE_PATH_MAX]; struct timespec at; } layer_store_failed[LAYER_STORE_FAILED_MAX];
static int layer_store_failed_next;
static pthread_mutex_t layer_store_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t layer_store_cond = PTHREAD_COND_INITIALIZER;
static char layer_store_queue[LAYER_STORE_QUEUE_MAX][LAYER_STORE_PATH_MAX];
static int layer_store_count;
static int layer_store_started;
/* Test hook: "built", "skipped", "paused", "failed", "dropped", "too_large" with the G-code path. */
static void (*layer_store_event)(const char *event, const char *path);

static void layer_store_emit(const char *event, const char *path) {
    if (layer_store_event) layer_store_event(event, path);
}

static void layer_store_set_idle(int idle) {
    int was = atomic_load(&layer_store_idle);
    atomic_store(&layer_store_idle, idle ? 1 : 0);
    if (idle && !was) {
        pthread_mutex_lock(&layer_store_lock);
        pthread_cond_signal(&layer_store_cond);
        pthread_mutex_unlock(&layer_store_lock);
    }
}

static int layer_store_index_path(const char *gcode_path, char *out, size_t cap) {
    return layer_index_path(layer_store_dir, gcode_path, out, cap);
}

static void layer_store_failed_clear_locked(const char *gcode_path) {
    for (int i = 0; i < LAYER_STORE_FAILED_MAX; i++)
        if (!strcmp(layer_store_failed[i].path, gcode_path)) layer_store_failed[i].path[0] = 0;
}

static void layer_store_failed_record(const char *gcode_path) {
    pthread_mutex_lock(&layer_store_lock);
    layer_store_failed_clear_locked(gcode_path);
    int slot = layer_store_failed_next++ % LAYER_STORE_FAILED_MAX; /* oldest entry makes room */
    snprintf(layer_store_failed[slot].path, LAYER_STORE_PATH_MAX, "%s", gcode_path);
    clock_gettime(CLOCK_MONOTONIC, &layer_store_failed[slot].at);
    pthread_mutex_unlock(&layer_store_lock);
}

static int layer_store_failed_recently_locked(const char *gcode_path) {
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    for (int i = 0; i < LAYER_STORE_FAILED_MAX; i++)
        if (layer_store_failed[i].path[0] && !strcmp(layer_store_failed[i].path, gcode_path))
            return (long)(now.tv_sec - layer_store_failed[i].at.tv_sec) < layer_store_retry_s;
    return 0;
}

static int layer_store_note_locked(const char *gcode_path) {
    for (int i = 0; i < layer_store_count; i++)
        if (!strcmp(layer_store_queue[i], gcode_path)) return 0;
    if (layer_store_count == LAYER_STORE_QUEUE_MAX) return -1;
    snprintf(layer_store_queue[layer_store_count++], LAYER_STORE_PATH_MAX, "%s", gcode_path);
    pthread_cond_signal(&layer_store_cond);
    return 0;
}

/* A file of 2 GiB or more gets no map (layer_index_file_max): it is not queued, so the builder never opens it. */
static int layer_store_too_large(const char *gcode_path) {
    struct stat st;
    return stat(gcode_path, &st) == 0 && (long long)st.st_size > layer_index_file_max;
}

/* Queue a G-code file for its map: an upload, copy or import (a new file, so an earlier failure no longer counts).
 * Returns 0, or -1 when the path is too long or the queue is full (the map is then built on first use instead),
 * or -3 when the file is 2 GiB or more (it never gets a map). A path already queued stays once. */
static int layer_store_note(const char *gcode_path) {
    if (!gcode_path || strlen(gcode_path) >= LAYER_STORE_PATH_MAX) return -1;
    if (layer_store_too_large(gcode_path)) return -3;
    pthread_mutex_lock(&layer_store_lock);
    layer_store_failed_clear_locked(gcode_path);
    int rc = layer_store_note_locked(gcode_path);
    pthread_mutex_unlock(&layer_store_lock);
    return rc;
}

/* Queue a file because a layer request found no current map. -2: its last build failed less than
 * layer_store_retry_s ago, so it is not started again by every request. -3: 2 GiB or more, no map ever. */
static int layer_store_note_request(const char *gcode_path) {
    if (!gcode_path || strlen(gcode_path) >= LAYER_STORE_PATH_MAX) return -1;
    if (layer_store_too_large(gcode_path)) return -3;
    pthread_mutex_lock(&layer_store_lock);
    int rc = layer_store_failed_recently_locked(gcode_path) ? -2 : layer_store_note_locked(gcode_path);
    pthread_mutex_unlock(&layer_store_lock);
    return rc;
}

static void layer_store_dequeue_locked(const char *gcode_path) {
    for (int i = 0; i < layer_store_count; i++)
        if (!strcmp(layer_store_queue[i], gcode_path)) {
            memmove(layer_store_queue[i], layer_store_queue[i + 1],
                    (size_t)(layer_store_count - i - 1) * LAYER_STORE_PATH_MAX);
            layer_store_count--;
            return;
        }
}

/* The G-code file is gone: drop its queue entry and its map. */
static void layer_store_forget(const char *gcode_path) {
    char path[LAYER_STORE_PATH_MAX + 128];
    pthread_mutex_lock(&layer_store_lock);
    layer_store_dequeue_locked(gcode_path);
    layer_store_failed_clear_locked(gcode_path);
    pthread_mutex_unlock(&layer_store_lock);
    if (layer_store_index_path(gcode_path, path, sizeof(path)) == 0) unlink(path);
}

/* A stored map that still matches the open file: 0 and *out filled, otherwise 1. */
static int layer_store_load(const char *gcode_path, FILE *file, layer_index *out) {
    char path[LAYER_STORE_PATH_MAX + 128];
    if (layer_store_index_path(gcode_path, path, sizeof(path)) != 0) return 1;
    return layer_index_load_valid(path, file, out);
}

static int layer_store_keep_going(void *unused) {
    (void)unused;
    if (atomic_load(&layer_store_stop) || !atomic_load(&layer_store_idle) ||
        atomic_load_explicit(&gcode_read_hold, memory_order_relaxed))
        return 0;
    if (layer_store_pause_us > 0) {
        struct timespec pause = { layer_store_pause_us / 1000000L, (layer_store_pause_us % 1000000L) * 1000L };
        nanosleep(&pause, NULL);
    }
    return 1;
}

typedef struct { char name[96]; long long mtime_ns; } layer_store_map;

static int layer_store_map_cmp(const void *a, const void *b) {
    const layer_store_map *x = a, *y = b;
    return (x->mtime_ns > y->mtime_ns) - (x->mtime_ns < y->mtime_ns);
}

/* After a map was saved: remove leftovers of an interrupted save (only this thread writes maps) and keep
 * the newest layer_store_max_maps maps, the one just saved among them. */
static void layer_store_prune(const char *saved_path) {
    DIR *dir = opendir(layer_store_dir);
    if (!dir) return;
    const char *saved = strrchr(saved_path, '/');
    saved = saved ? saved + 1 : saved_path;
    size_t cap = 16, n = 0;
    layer_store_map *list = malloc(cap * sizeof(*list));
    struct dirent *entry;
    while (list && (entry = readdir(dir))) {
        size_t len = strlen(entry->d_name);
        char full[LAYER_STORE_PATH_MAX + 128];
        if (len >= sizeof(list[0].name) || snprintf(full, sizeof(full), "%s/%s", layer_store_dir, entry->d_name) >= (int)sizeof(full)) continue;
        if (len > 9 && !strcmp(entry->d_name + len - 9, ".idx.part")) { unlink(full); continue; }
        if (len < 5 || strcmp(entry->d_name + len - 4, ".idx")) continue;
        struct stat st;
        if (stat(full, &st) != 0) continue;
        if (n == cap) {
            layer_store_map *bigger = realloc(list, cap * 2 * sizeof(*list));
            if (!bigger) break;
            list = bigger; cap *= 2;
        }
        memcpy(list[n].name, entry->d_name, len + 1); /* len < sizeof name, checked above */
#ifdef __APPLE__
        list[n].mtime_ns = (long long)st.st_mtimespec.tv_sec * 1000000000LL + st.st_mtimespec.tv_nsec;
#else
        list[n].mtime_ns = (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
#endif
        n++;
    }
    closedir(dir);
    if (list && n > (size_t)layer_store_max_maps) {
        qsort(list, n, sizeof(*list), layer_store_map_cmp);
        size_t excess = n - (size_t)layer_store_max_maps;
        for (size_t i = 0; i < n && excess; i++) {
            if (!strcmp(list[i].name, saved)) continue;
            char full[LAYER_STORE_PATH_MAX + 128];
            if (snprintf(full, sizeof(full), "%s/%s", layer_store_dir, list[i].name) < (int)sizeof(full)) unlink(full);
            excess--;
        }
    }
    free(list);
}

/* 0 = done (built, already current, or nothing to do), 1 = paused for a print, -1 = failed. */
static int layer_store_build_one(const char *gcode_path) {
    if (atomic_load_explicit(&gcode_read_hold, memory_order_relaxed)) return 1;
    char path[LAYER_STORE_PATH_MAX + 128];
    if (layer_store_index_path(gcode_path, path, sizeof(path)) != 0) return -1;
    FILE *file = fopen(gcode_path, "rb");
    if (!file) { unlink(path); return -1; }
    layer_index index;
    if (layer_index_load_valid(path, file, &index) == 0) {
        layer_index_free(&index); fclose(file); layer_store_emit("skipped", gcode_path); return 0;
    }
    int rc = layer_index_build(file, &index, LAYER_STORE_BUILD_MS, layer_store_keep_going, NULL);
    fclose(file);
    if (rc == LAYER_INDEX_NONE) { /* 2 GiB or more (it grew since it was queued): no map, no failure, no retry */
        unlink(path);
        layer_store_emit("too_large", gcode_path);
        return 0;
    }
    if (rc == LAYER_PATH_LIMIT &&
        (!atomic_load(&layer_store_idle) || atomic_load(&layer_store_stop) ||
         atomic_load_explicit(&gcode_read_hold, memory_order_relaxed)))
        return 1;
    if (rc != LAYER_PATH_OK) return -1;
    mkdir(layer_store_dir, 0700);
    rc = layer_index_save(path, &index);
    layer_index_free(&index);
    if (rc != 0) return -1;
    layer_store_prune(path);
    atomic_fetch_add(&layer_store_stat_built, 1);
    layer_store_emit("built", gcode_path);
    return 0;
}

typedef struct { char name[LAYER_STORE_PATH_MAX]; long long mtime_ns; } layer_store_candidate;

static int layer_store_candidate_newest_first(const void *a, const void *b) {
    const layer_store_candidate *x = a, *y = b;
    return (x->mtime_ns < y->mtime_ns) - (x->mtime_ns > y->mtime_ns);
}

/* Queue the newest max regular *.gcode files of dir (not subfolders, no temporary copies) for their maps. Returns how many were
 * queued. The builder still works only while the printer is idle and skips a map that is current. */
static int layer_store_prewarm(const char *dir, int max) {
    DIR *d = opendir(dir);
    if (!d) return 0;
    size_t cap = 32, n = 0;
    layer_store_candidate *list = malloc(cap * sizeof(*list));
    struct dirent *entry;
    while (list && (entry = readdir(d))) {
        size_t len = strlen(entry->d_name);
        char full[LAYER_STORE_PATH_MAX + 8];
        if (len < 7 || strcasecmp(entry->d_name + len - 6, ".gcode")) continue;
        if (snprintf(full, sizeof(full), "%s/%s", dir, entry->d_name) >= LAYER_STORE_PATH_MAX) continue;
        struct stat st;
        if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (n == cap) {
            layer_store_candidate *bigger = realloc(list, cap * 2 * sizeof(*list));
            if (!bigger) break;
            list = bigger; cap *= 2;
        }
        memcpy(list[n].name, full, strlen(full) + 1);
#ifdef __APPLE__
        list[n].mtime_ns = (long long)st.st_mtimespec.tv_sec * 1000000000LL + st.st_mtimespec.tv_nsec;
#else
        list[n].mtime_ns = (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
#endif
        n++;
    }
    closedir(d);
    int queued = 0;
    if (list) {
        qsort(list, n, sizeof(*list), layer_store_candidate_newest_first);
        for (size_t i = 0; i < n && queued < max; i++)
            if (layer_store_note(list[i].name) == 0) queued++;
    }
    free(list);
    return queued;
}

static void *layer_store_worker(void *unused) {
    (void)unused;
#ifdef __linux__
    /* Linux keeps the nice value per thread; 0 names the calling thread only. */
    (void)setpriority(PRIO_PROCESS, 0, 19);
#endif
    char current[LAYER_STORE_PATH_MAX];
    for (;;) {
        pthread_mutex_lock(&layer_store_lock);
        while (!atomic_load(&layer_store_stop) &&
               (!layer_store_count || !atomic_load(&layer_store_idle) ||
                atomic_load_explicit(&gcode_read_hold, memory_order_relaxed))) {
            if (!layer_store_count) pthread_cond_wait(&layer_store_cond, &layer_store_lock);
            else {
                struct timespec until; clock_gettime(CLOCK_REALTIME, &until);
                until.tv_sec += layer_store_poll_ms / 1000;
                until.tv_nsec += (layer_store_poll_ms % 1000) * 1000000L;
                if (until.tv_nsec >= 1000000000L) { until.tv_sec++; until.tv_nsec -= 1000000000L; }
                pthread_cond_timedwait(&layer_store_cond, &layer_store_lock, &until);
            }
        }
        if (atomic_load(&layer_store_stop)) { pthread_mutex_unlock(&layer_store_lock); return NULL; }
        snprintf(current, sizeof(current), "%s", layer_store_queue[0]);
        pthread_mutex_unlock(&layer_store_lock);
        int rc = layer_store_build_one(current);
        if (rc == 1) { layer_store_emit("paused", current); continue; } /* stays queued */
        if (rc < 0) { atomic_fetch_add(&layer_store_stat_failed, 1); layer_store_emit("failed", current); layer_store_failed_record(current); }
        pthread_mutex_lock(&layer_store_lock);
        layer_store_dequeue_locked(current);
        pthread_mutex_unlock(&layer_store_lock);
    }
}

static int layer_store_start(void) {
    pthread_mutex_lock(&layer_store_lock);
    if (layer_store_started) { pthread_mutex_unlock(&layer_store_lock); return 0; }
    pthread_attr_t attr; pthread_t thread;
    int failed = pthread_attr_init(&attr);
    if (!failed) {
        failed = pthread_attr_setstacksize(&attr, 256 * 1024);
        if (!failed) failed = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        if (!failed) failed = pthread_create(&thread, &attr, layer_store_worker, NULL);
        pthread_attr_destroy(&attr);
    }
    if (!failed) layer_store_started = 1;
    pthread_mutex_unlock(&layer_store_lock);
    return failed ? -1 : 0;
}
#endif
