/* Files of 2 GiB or more get no layer map (the same limit as ACTIVE_SCAN_FILE_MAX and PRINTER_FILE_LIMIT in main.c,
 * #390/#398): the printer program cannot open them, and the 32-bit printer's long offsets of the scan would wrap from
 * 2 GiB on. Such a file is not queued, not read by the builder, gets no map and no failure count; the layer request
 * reads it as before the map existed. The real limit is checked with a sparse file (no disk space is used and the
 * file is never read); the boundary byte is checked with the limit lowered through its test variable. */
#include "../src/gcode_layer_index_store.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int too_large_events, built_events, skipped_events;
static void on_event(const char *event, const char *path) {
    (void)path;
    if (!strcmp(event, "too_large")) too_large_events++;
    else if (!strcmp(event, "built")) built_events++;
    else if (!strcmp(event, "skipped")) skipped_events++;
}

static void write_layers(const char *path, int layers) {
    FILE *f = fopen(path, "w"); assert(f);
    fprintf(f, "; total layer number: %d\nG90\nM83\n", layers);
    for (int l = 0; l < layers; l++)
        fprintf(f, ";LAYER_CHANGE\n;Z:%.2f\nG1 Z%.2f\nG1 X%d Y10 E0.01\n", 0.2 * (l + 1), 0.2 * (l + 1), 10 + l);
    assert(!fclose(f));
}

static int map_exists(const char *gcode) {
    char path[2048];
    assert(layer_store_index_path(gcode, path, sizeof(path)) == 0);
    return access(path, F_OK) == 0;
}

int main(void) {
    (void)layer_path_mark_pick; (void)gcode_layer_path_parse_at; (void)sha256_fd; (void)gcode_layer_path_query; (void)layer_store_distrust;
    (void)gcode_layer_path_json; (void)gcode_layer_path_parse_indexed; (void)gcode_layer_path_parse;
    (void)layer_index_can_serve; (void)layer_index_serves_all; (void)layer_store_stat_hits; (void)layer_store_stat_fallbacks;
    (void)layer_store_forget; (void)layer_store_load; (void)layer_store_start;
    char base[] = "/tmp/cc2-layer-size-XXXXXX";
    assert(mkdtemp(base));
    char maps[600], gdir[600], huge[700], small[700];
    snprintf(maps, sizeof(maps), "%s/maps", base);
    snprintf(gdir, sizeof(gdir), "%s/gcode", base);
    assert(!mkdir(gdir, 0700));
    snprintf(huge, sizeof(huge), "%s/huge.gcode", gdir);
    snprintf(small, sizeof(small), "%s/small.gcode", gdir);
    layer_store_dir = maps;
    layer_store_event = on_event;
    layer_store_pause_us = 0;
    layer_store_set_idle(1);

    /* The limit is the one of the printer program: 2 GiB minus one byte still gets a map, 2 GiB does not. */
    assert(layer_index_file_max == 2147483647LL && layer_index_file_max + 1 == 2147483648LL);

    /* A sparse file of exactly 2 GiB: it is only sized, never read. */
    {
        int fd = open(huge, O_WRONLY | O_CREAT, 0600); assert(fd >= 0);
        assert(ftruncate(fd, 2147483648LL) == 0);
        assert(close(fd) == 0);
        struct stat st; assert(!stat(huge, &st) && (long long)st.st_size == 2147483648LL);
    }
    FILE *f = fopen(huge, "rb"); assert(f);
    layer_index index;
    assert(layer_index_build(f, &index, 0, NULL, NULL) == LAYER_INDEX_NONE && index.count == 0 && !index.entries);
    fclose(f);
    /* Not queued, from an upload, copy or import (note), from a layer request (note_request) or from the prewarm. */
    assert(layer_store_note(huge) == -3);
    assert(layer_store_note_request(huge) == -3);
    assert(layer_store_prewarm(gdir, 8) == 0);
    pthread_mutex_lock(&layer_store_lock); int queued = layer_store_count; pthread_mutex_unlock(&layer_store_lock);
    assert(queued == 0);
    /* Even if it grew after it was queued, the builder opens it, refuses it, writes nothing and counts no failure. */
    {
        unsigned long failed_before = atomic_load(&layer_store_stat_failed), built_before = atomic_load(&layer_store_stat_built);
        assert(layer_store_build_one(huge) == 0 && too_large_events == 1);
        assert(!map_exists(huge) && atomic_load(&layer_store_stat_failed) == failed_before &&
               atomic_load(&layer_store_stat_built) == built_before);
    }
    /* A stale map of a file that grew past the limit is removed, not read. */
    {
        mkdir(maps, 0700);
        char map[2048]; assert(layer_store_index_path(huge, map, sizeof(map)) == 0);
        FILE *m = fopen(map, "w"); assert(m); fputs("x", m); fclose(m);
        assert(layer_store_build_one(huge) == 0 && !map_exists(huge) && too_large_events == 2);
    }
    assert(!unlink(huge));

    /* The boundary byte, with the limit lowered to the size of an ordinary file. */
    write_layers(small, 40);
    struct stat st; assert(!stat(small, &st));
    long long size = (long long)st.st_size;
    layer_index_file_max = size;           /* exactly at the limit: indexed */
    f = fopen(small, "rb"); assert(f);
    assert(layer_index_build(f, &index, 0, NULL, NULL) == LAYER_PATH_OK && index.count == 40);
    layer_index_free(&index); fclose(f);
    assert(layer_store_note(small) == 0);
    assert(layer_store_build_one(small) == 0 && map_exists(small) && too_large_events == 2);
    layer_index_file_max = size - 1;       /* one byte over the limit: not indexed, and its map is not read */
    f = fopen(small, "rb"); assert(f);
    assert(layer_index_build(f, &index, 0, NULL, NULL) == LAYER_INDEX_NONE);
    {
        layer_index key_only; char map[2048]; assert(layer_store_index_path(small, map, sizeof(map)) == 0);
        assert(layer_index_load_valid(map, f, &key_only) == 1); /* a valid map of a file now over the limit counts as stale */
    }
    fclose(f);
    assert(layer_store_note(small) == -3 && layer_store_note_request(small) == -3);
    layer_index_file_max = LAYER_INDEX_FILE_MAX_DEFAULT;

    {   char map[2048];
        if (layer_store_index_path(small, map, sizeof(map)) == 0) unlink(map);
        rmdir(maps); unlink(small); rmdir(gdir); rmdir(base); }
    puts("PASS files of 2 GiB or more get no layer map: not queued, not read, no map, no failure; the byte below the limit still does");
    return 0;
}
