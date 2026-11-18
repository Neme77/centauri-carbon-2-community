/* Layer index in the layer-path worker: follow-up requests resume from remembered layer starts,
 * and a file that is replaced or rewritten under the same name drops the old index, so a stale
 * offset never reads into the wrong place. Every answer is compared byte for byte with the answer
 * of a cold scan (index and response cache cleared). */
#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

static void wait_idle(void) {
    for (int i = 0; i < 2000 && atomic_load(&active_layer_paths); i++) {
        struct timespec pause = {0, 5000000}; nanosleep(&pause, NULL);
    }
    assert(atomic_load(&active_layer_paths) == 0);
}

/* Requests one layer and copies the JSON body into body; returns the HTTP status. */
static int request(int layer, char *body, size_t cap) {
    char query[96], reply[LAYER_PATH_JSON_MAX + 512];
    snprintf(query, sizeof(query), "storage=internal&file=print.gcode&layer=%d", layer);
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    int handed = gcode_layer_path_start(pair[0], query);
    if (!handed) close(pair[0]);
    size_t used = 0; ssize_t n;
    while (used + 1 < sizeof(reply) && (n = read(pair[1], reply + used, sizeof(reply) - 1 - used)) > 0) used += (size_t)n;
    close(pair[1]);
    reply[used] = 0;
    wait_idle();
    const char *start = strstr(reply, "\r\n\r\n");
    assert(used > 0 && start);
    snprintf(body, cap, "%s", start + 4);
    return atoi(strchr(reply, ' ') + 1);
}

static int cold(int layer, char *body, size_t cap) {
    pthread_mutex_lock(&layer_path_cache_lock);
    layer_path_index.valid = 0;
    layer_path_cached.valid = 0;
    pthread_mutex_unlock(&layer_path_cache_lock);
    return request(layer, body, cap);
}

static void forget_response(void) {
    pthread_mutex_lock(&layer_path_cache_lock);
    layer_path_cached.valid = 0;
    pthread_mutex_unlock(&layer_path_cache_lock);
}

/* layers layers; each has moves moves at X from x0, so versions differ in content and offsets. */
static void write_print(const char *path, int layers, int moves, int x0) {
    FILE *f = fopen(path, "w"); assert(f);
    assert(fprintf(f, "; total layer number: %d\nG90\nM82\n", layers) > 0);
    for (int l = 0; l < layers; l++) {
        assert(fprintf(f, ";LAYER_CHANGE\n;Z:%.2f\nG1 Z%.2f\nG1 X%d Y10\n", 0.2 * (l + 1), 0.2 * (l + 1), x0) > 0);
        for (int i = 0; i < moves; i++)
            assert(fprintf(f, "G1 X%d Y%d E%d\n", x0 + i + 1, 10 + l, l * moves + i + 1) > 0);
    }
    assert(!fclose(f));
}

static int index_count(void) {
    pthread_mutex_lock(&layer_path_cache_lock);
    int count = layer_path_index.valid ? layer_path_index.count : -1;
    pthread_mutex_unlock(&layer_path_cache_lock);
    return count;
}

static int index_matches(const char *path) {
    struct stat st; assert(!stat(path, &st));
    pthread_mutex_lock(&layer_path_cache_lock);
    int same = layer_path_index.valid && layer_path_same_file(&st, &layer_path_index.stamp);
    pthread_mutex_unlock(&layer_path_cache_lock);
    return same;
}

int main(void) {
    char dir[] = "/tmp/layer-index-XXXXXX"; assert(mkdtemp(dir));
    gcode_internal_root = dir;
    layer_path_rest_min_ms = 0; /* the pause between scans is tested in test_layer_path_load.c */
    layer_path_rest_percent = 0;
    char path[512], next[512];
    snprintf(path, sizeof(path), "%s/print.gcode", dir);
    snprintf(next, sizeof(next), "%s/print.gcode.new", dir);
    static char seen[LAYER_PATH_JSON_MAX], fresh[LAYER_PATH_JSON_MAX], first[LAYER_PATH_JSON_MAX];

    /* Two requests on one file: the second resumes from the index and gives the cold bytes. */
    write_print(path, 40, 12, 20);
    assert(request(30, first, sizeof(first)) == 200);
    int after_first = index_count();
    assert(after_first > 0 && index_matches(path));
    assert(request(35, seen, sizeof(seen)) == 200);
    assert(index_count() >= after_first && index_matches(path));
    assert(cold(35, fresh, sizeof(fresh)) == 200 && !strcmp(seen, fresh));

    /* Replaced under the same name (new inode, other offsets): the old index is dropped. */
    assert(request(30, seen, sizeof(seen)) == 200 && !strcmp(seen, first));
    assert(index_count() > 0);
    write_print(next, 40, 3, 150);
    assert(!rename(next, path));
    forget_response();
    assert(request(30, seen, sizeof(seen)) == 200);
    assert(index_matches(path) && strcmp(seen, first));
    assert(cold(30, fresh, sizeof(fresh)) == 200 && !strcmp(seen, fresh));

    /* Rewritten in place (same inode, other size): the old index is dropped too. */
    assert(request(35, seen, sizeof(seen)) == 200);
    write_print(path, 40, 7, 90);
    forget_response();
    assert(request(30, seen, sizeof(seen)) == 200 && index_matches(path));
    assert(cold(30, fresh, sizeof(fresh)) == 200 && !strcmp(seen, fresh));

    /* Numbered by ;LAYER:N with a layer count in the header (Cura): the scan stops following the
     * moves once it is past the requested layer, so it must not remember layer starts from there
     * on. A later layer asked afterwards still gives the cold bytes. */
    FILE *cura = fopen(path, "w"); assert(cura);
    assert(fputs(";LAYER_COUNT:3\n;LAYER:0\nG90\nM82\nG1 X0 Y0 E0.1\nG1 X10 Y0 E0.2\n"
                 ";LAYER:1\nG0 X50 Y50\nG1 X60 Y50 E0.3\n;LAYER:2\nG1 X70 Y50 E0.4\n", cura) >= 0);
    assert(!fclose(cura));
    assert(cold(1, seen, sizeof(seen)) == 200);
    assert(request(3, seen, sizeof(seen)) == 200);
    assert(cold(3, fresh, sizeof(fresh)) == 200 && !strcmp(seen, fresh));

    unlink(path);
    rmdir(dir);
    puts("PASS layer path index: follow-up requests resume from the index, a replaced or rewritten file drops it, no layer start is remembered past the requested layer of a ;LAYER:N file");
    return 0;
}
