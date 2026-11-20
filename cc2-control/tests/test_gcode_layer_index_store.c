/* The background builder builds layer maps only while the printer is idle, stops a pass when
 * a print starts and finishes it later, skips current maps, rebuilds stale ones, removes the
 * map of a deleted file, keeps the queue and the map folder bounded and never writes next to the G-code. */
#include "../src/gcode_layer_index_store.h"
#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

static void sleep_ms(long ms) {
    struct timespec t = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&t, NULL);
}

static pthread_mutex_t events_lock = PTHREAD_MUTEX_INITIALIZER;
static int built, skipped, paused, failed_count;

static void on_event(const char *event, const char *path) {
    (void)path;
    pthread_mutex_lock(&events_lock);
    if (!strcmp(event, "built")) built++;
    else if (!strcmp(event, "skipped")) skipped++;
    else if (!strcmp(event, "paused")) paused++;
    else if (!strcmp(event, "failed")) failed_count++;
    pthread_mutex_unlock(&events_lock);
}

static int counter(int *which) {
    pthread_mutex_lock(&events_lock);
    int v = *which;
    pthread_mutex_unlock(&events_lock);
    return v;
}

static int wait_for(int *which, int at_least, int ms) {
    for (int waited = 0; waited < ms; waited += 10) {
        if (counter(which) >= at_least) return 1;
        sleep_ms(10);
    }
    return counter(which) >= at_least;
}

static int queued(void) {
    pthread_mutex_lock(&layer_store_lock);
    int n = layer_store_count;
    pthread_mutex_unlock(&layer_store_lock);
    return n;
}

static void write_layers(const char *path, int layers, int moves_per_layer) {
    FILE *f = fopen(path, "w");
    assert(f);
    fprintf(f, "; total layer number: %d\nG90\nM83\n", layers);
    for (int l = 0; l < layers; l++) {
        fprintf(f, ";LAYER_CHANGE\n;Z:%.2f\nG1 Z%.2f\n", 0.2 * (l + 1), 0.2 * (l + 1));
        for (int m = 0; m < moves_per_layer; m++) fprintf(f, "G1 X%d.%03d Y%d E0.01\n", 10 + m % 200, m % 1000, 10 + l % 200);
    }
    assert(!fclose(f));
}

static int map_exists(const char *gcode) {
    char path[2048];
    assert(layer_store_index_path(gcode, path, sizeof(path)) == 0);
    return access(path, F_OK) == 0;
}

static int files_in(const char *dir) {
    DIR *d = opendir(dir); int n = 0; struct dirent *e;
    if (!d) return 0;
    while ((e = readdir(d))) if (e->d_name[0] != '.') n++;
    closedir(d);
    return n;
}

int main(void) {
    (void)layer_path_mark_pick;
    (void)gcode_layer_path_parse_at;
    (void)sha256_fd; (void)gcode_layer_path_query; (void)gcode_layer_path_json;
    (void)gcode_layer_path_parse_indexed; (void)gcode_layer_path_parse;
    (void)layer_store_stat_hits; (void)layer_store_stat_fallbacks; /* counted by the HTTP side, see test_layer_path_stats.c */
    char base[] = "/tmp/cc2-layer-store-XXXXXX";
    assert(mkdtemp(base));
    char gdir[600], mdir[600], small[700], big[700];
    snprintf(gdir, sizeof(gdir), "%s/gcode", base);
    snprintf(mdir, sizeof(mdir), "%s/maps", base);
    assert(!mkdir(gdir, 0700));
    snprintf(small, sizeof(small), "%s/small.gcode", gdir);
    snprintf(big, sizeof(big), "%s/big.gcode", gdir);
    write_layers(small, 50, 20);
    write_layers(big, 400, 4000); /* about 40 MB: long enough to stop in the middle */
    layer_store_dir = mdir;
    layer_store_event = on_event;
    layer_store_poll_ms = 20;
    layer_store_pause_us = 0;
    layer_store_set_idle(0);
    assert(layer_store_start() == 0);

    /* Printing: nothing is built, the file stays queued. */
    assert(layer_store_note(small) == 0);
    assert(layer_store_note(small) == 0 && queued() == 1); /* queued once */
    sleep_ms(200);
    assert(counter(&built) == 0 && !map_exists(small) && queued() == 1);

    /* Idle: the map is built in the store folder, not next to the G-code. */
    layer_store_set_idle(1);
    assert(wait_for(&built, 1, 5000));
    assert(map_exists(small) && files_in(gdir) == 2 && files_in(mdir) == 1);
    FILE *f = fopen(small, "rb"); layer_index index;
    assert(layer_store_load(small, f, &index) == 0 && index.total == 50 && index.count == 50);
    layer_index_free(&index); fclose(f);

    /* Already current: noting it again builds nothing. */
    assert(layer_store_note(small) == 0 && wait_for(&skipped, 1, 5000) && counter(&built) == 1);

    /* A changed file makes the map stale; noting it rebuilds it. */
    sleep(1);
    write_layers(small, 60, 20);
    f = fopen(small, "rb");
    assert(layer_store_load(small, f, &index) == 1);
    fclose(f);
    assert(layer_store_note(small) == 0 && wait_for(&built, 2, 5000));
    f = fopen(small, "rb");
    assert(layer_store_load(small, f, &index) == 0 && index.total == 60);
    layer_index_free(&index); fclose(f);

    /* gcode_read_hold with mutation-idle still true (main.c can set idle before hold is applied):
     * the builder must not finish a map while the hold is on. */
    char hold_file[700];
    snprintf(hold_file, sizeof(hold_file), "%s/hold.gcode", gdir);
    write_layers(hold_file, 200, 2000);
    int built_before = counter(&built);
    int paused_before = counter(&paused);
    atomic_store(&gcode_read_hold, 1);
    layer_store_set_idle(1);
    assert(layer_store_note(hold_file) == 0);
    sleep_ms(400);
    if (counter(&built) != built_before || map_exists(hold_file)) {
        fprintf(stderr, "FAIL: layer store built while gcode_read_hold was set (built %d->%d map=%d)\n",
                built_before, counter(&built), map_exists(hold_file));
        exit(1);
    }
    atomic_store(&gcode_read_hold, 0);
    assert(wait_for(&built, built_before + 1, 15000));
    assert(map_exists(hold_file));
    assert(counter(&paused) >= paused_before); /* may have paused while hold was on */

    /* A print starts during a build: the pass stops, nothing is written, it resumes later. */
    layer_store_pause_us = 200; /* slow the pass down so it can be caught in the middle */
    int built_big = counter(&built);
    assert(layer_store_note(big) == 0);
    sleep_ms(50);
    layer_store_set_idle(0);
    assert(wait_for(&paused, 1, 10000));
    assert(!map_exists(big) && queued() == 1);
    char part[2048]; assert(layer_store_index_path(big, part, sizeof(part)) == 0);
    strcat(part, ".part");
    assert(access(part, F_OK) != 0);
    layer_store_pause_us = 0;
    layer_store_set_idle(1);
    assert(wait_for(&built, built_big + 1, 20000) && map_exists(big) && queued() == 0);

    /* Deleted G-code: its map goes too. */
    assert(!unlink(small));
    layer_store_forget(small);
    assert(!map_exists(small) && map_exists(big));

    /* A missing file is dropped, not retried forever. */
    assert(layer_store_note(small) == 0 && wait_for(&failed_count, 1, 5000) && queued() == 0);

    /* A build that fails (here: the map folder cannot be created) is not started again by every layer request:
     * request-triggered notes wait out layer_store_retry_s; an upload, copy or import of the file clears the wait. */
    {
        const char *real_dir = layer_store_dir;
        char blocked[800], blocker[700];
        snprintf(blocker, sizeof(blocker), "%s/not-a-folder", base);
        FILE *bf = fopen(blocker, "w"); assert(bf); fclose(bf);
        snprintf(blocked, sizeof(blocked), "%s/sub", blocker); /* a folder below a plain file: mkdir and the save fail */
        layer_store_dir = blocked;
        char again[700]; snprintf(again, sizeof(again), "%s/again.gcode", gdir);
        write_layers(again, 20, 10);
        int failed_before = counter(&failed_count);
        assert(layer_store_note_request(again) == 0 && wait_for(&failed_count, failed_before + 1, 5000));
        sleep_ms(50);
        assert(layer_store_note_request(again) == -2 && queued() == 0); /* not queued again within the wait */
        assert(counter(&failed_count) == failed_before + 1);
        assert(layer_store_note(again) == 0 && wait_for(&failed_count, failed_before + 2, 5000)); /* a new upload tries at once */
        long saved_wait = layer_store_retry_s; layer_store_retry_s = 0;
        assert(layer_store_note_request(again) == 0 && wait_for(&failed_count, failed_before + 3, 5000)); /* after the wait */
        layer_store_retry_s = saved_wait;
        layer_store_dir = real_dir;
        unlink(blocker);
    }

    /* Prewarm: the newest regular .gcode files of a folder are queued, newest first, capped; subfolders, other names and
     * temporary copies are left out; the builder then builds their maps. */
    {
        char pw[700]; snprintf(pw, sizeof(pw), "%s/prewarm", base);
        assert(!mkdir(pw, 0700));
        char p[800]; time_t now = time(NULL);
        for (int i = 0; i < 5; i++) {
            snprintf(p, sizeof(p), "%s/f%d.gcode", pw, i);
            write_layers(p, 10, 5);
            struct timeval tv[2] = {{ now - 1000 + i * 100, 0 }, { now - 1000 + i * 100, 0 }}; /* f4 is the newest */
            assert(!utimes(p, tv));
        }
        snprintf(p, sizeof(p), "%s/notes.txt", pw); FILE *tf = fopen(p, "w"); assert(tf); fputs("x", tf); fclose(tf);
        snprintf(p, sizeof(p), "%s/part.gcode.part", pw); tf = fopen(p, "w"); assert(tf); fputs("x", tf); fclose(tf);
        snprintf(p, sizeof(p), "%s/folder.gcode", pw); assert(!mkdir(p, 0700));
        layer_store_set_idle(0); /* nothing builds yet: look at the queue */
        int before_built = counter(&built);
        assert(layer_store_prewarm(pw, 3) == 3);
        assert(queued() == 3);
        char newest[800]; snprintf(newest, sizeof(newest), "%s/f4.gcode", pw);
        pthread_mutex_lock(&layer_store_lock);
        int order_ok = !strcmp(layer_store_queue[0], newest);
        pthread_mutex_unlock(&layer_store_lock);
        assert(order_ok);
        assert(layer_store_prewarm("/nonexistent-folder-for-prewarm", 3) == 0);
        unsigned long built_before = atomic_load(&layer_store_stat_built);
        layer_store_set_idle(1);
        assert(wait_for(&built, before_built + 3, 10000));
        assert(queued() == 0 && atomic_load(&layer_store_stat_built) == built_before + 3);
        snprintf(p, sizeof(p), "%s/f4.gcode", pw); assert(map_exists(p));
        snprintf(p, sizeof(p), "%s/f0.gcode", pw); assert(!map_exists(p)); /* beyond the cap of 3 */
        assert(atomic_load(&layer_store_stat_failed) >= 1); /* the earlier failed builds were counted */
    }

    /* The store is bounded: only the newest maps stay, the one just saved among them, and the leftover of an
     * interrupted save is removed. Before this, hold and big are the two maps in the folder. */
    layer_store_set_idle(1);
    layer_store_max_maps = 3;
    char stray[2048]; snprintf(stray, sizeof(stray), "%s/00ff.idx.part", mdir);
    FILE *sf = fopen(stray, "w"); assert(sf); fclose(sf);
    char names[3][700];
    for (int i = 0; i < 3; i++) {
        snprintf(names[i], sizeof(names[i]), "%s/prune%d.gcode", gdir, i);
        write_layers(names[i], 20, 10);
        int before = counter(&built);
        assert(layer_store_note(names[i]) == 0 && wait_for(&built, before + 1, 5000));
        sleep_ms(30); /* distinct change times for the oldest-first order */
    }
    assert(files_in(mdir) == 3 && access(stray, F_OK) != 0);
    assert(map_exists(names[0]) && map_exists(names[1]) && map_exists(names[2]));
    assert(!map_exists(hold_file) && !map_exists(big));
    layer_store_max_maps = LAYER_STORE_MAPS_MAX;

    /* The queue is bounded. */
    layer_store_set_idle(0);
    char name[700]; int accepted = 0;
    for (int i = 0; i < LAYER_STORE_QUEUE_MAX + 5; i++) {
        snprintf(name, sizeof(name), "%s/q%d.gcode", gdir, i);
        if (layer_store_note(name) == 0) accepted++;
    }
    assert(accepted == LAYER_STORE_QUEUE_MAX && queued() == LAYER_STORE_QUEUE_MAX);
    char too_long[LAYER_STORE_PATH_MAX + 10]; memset(too_long, 'a', sizeof(too_long) - 1); too_long[sizeof(too_long) - 1] = 0;
    assert(layer_store_note(too_long) == -1);

    atomic_store(&layer_store_stop, 1);
    pthread_mutex_lock(&layer_store_lock); pthread_cond_broadcast(&layer_store_cond); pthread_mutex_unlock(&layer_store_lock);
    puts("PASS: layer store builds only while idle, pauses while gcode_read_hold is set, stops a pass "
         "when a print starts and resumes it, skips current maps, rebuilds stale ones, removes the map "
         "of a deleted file, drops missing files, keeps maps out of the G-code folder and bounds the queue and the map folder, and does not retry a failed build on every request, and prewarms the newest files");
    return 0;
}
