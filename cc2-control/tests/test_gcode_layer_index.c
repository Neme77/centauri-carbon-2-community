/* The layer index must give exactly the layer the full scan gives: same result code, layer
 * count, height, points and path starts, for every layer. Also: storage round trip, a
 * changed file makes the stored index stale, files the index cannot serve are refused.
 * Extra G-code files can be passed as arguments (every layer of each is compared). */
#include "../src/gcode_layer_index.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static layer_point pts_a[LAYER_PATH_COLLECT_MAX], pts_b[LAYER_PATH_COLLECT_MAX];
static int starts_a[LAYER_PATH_PATHS_MAX], starts_b[LAYER_PATH_PATHS_MAX];

static int full(FILE *f, int layer, layer_path_info *info) {
    struct timespec start; clock_gettime(CLOCK_MONOTONIC, &start);
    rewind(f);
    return gcode_layer_path_parse(f, layer, pts_a, LAYER_PATH_COLLECT_MAX, starts_a, LAYER_PATH_PATHS_MAX,
                                  info, &start, 0x7fffffffL, 0);
}

static int jump(FILE *f, const layer_index *index, int layer, layer_path_info *info) {
    struct timespec start; clock_gettime(CLOCK_MONOTONIC, &start);
    return gcode_layer_path_parse_indexed(f, index, layer, pts_b, LAYER_PATH_COLLECT_MAX, starts_b,
                                          LAYER_PATH_PATHS_MAX, info, &start, 0x7fffffffL,
                                          LAYER_INDEX_LAYER_BYTES_MAX);
}

static int same_z(double a, double b) { return (isnan(a) && isnan(b)) || a == b; }


/* Returns the number of differing layers; prints the first few. total_differs counts layers
 * whose only difference is the layer count (see the note in main). */
static int compare_all(const char *name, FILE *f, int *layers_out, int *total_differs) {
    layer_index index;
    assert(layer_index_build(f, &index, 0, NULL, NULL) == LAYER_PATH_OK);
    assert(index.seen_marker && !index.repeats);
    int bad = 0, every_layer_served = 1;
    *total_differs = 0;
    for (int layer = 1; layer <= index.total + 1; layer++) {
        layer_path_info a, b;
        int ra = full(f, layer, &a), rb = jump(f, &index, layer, &b);
        /* The HTTP side asks this before reading: it must agree with what the read then does. */
        assert(layer_index_can_serve(&index, layer) == (rb != LAYER_INDEX_NONE));
        every_layer_served = every_layer_served && rb != LAYER_INDEX_NONE;
        int points_same = ra != LAYER_PATH_OK ||
            (a.point_count == b.point_count && a.path_count == b.path_count &&
             !memcmp(pts_a, pts_b, sizeof(layer_point) * (size_t)a.point_count) &&
             !memcmp(starts_a, starts_b, sizeof(int) * (size_t)a.path_count));
        int ok = ra == rb && points_same && a.truncated == b.truncated && same_z(a.z, b.z);
        if (ok && a.total_layers != b.total_layers) { (*total_differs)++; continue; }
        if (!ok) {
            if (bad < 5)
                fprintf(stderr, "%s layer %d: rc %d/%d points %d/%d paths %d/%d z %g/%g total %d/%d\n", name, layer,
                        ra, rb, a.point_count, b.point_count, a.path_count, b.path_count, a.z, b.z,
                        a.total_layers, b.total_layers);
            bad++;
        }
    }
    assert(layer_index_serves_all(&index) == every_layer_served); /* "serves every layer" is exactly that */
    *layers_out = index.total;
    layer_index_free(&index);
    return bad;
}

static FILE *gcode(const char *dir, const char *name, const char *text) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    FILE *f = fopen(path, "w+");
    assert(f && fputs(text, f) >= 0 && !fflush(f));
    rewind(f);
    return f;
}

/* Flips one bit of the byte at pos in the stored map. */
static void flip_bit(const char *path, long pos) {
    FILE *f = fopen(path, "r+b"); assert(f);
    assert(!fseek(f, pos, SEEK_SET));
    int c = fgetc(f); assert(c != EOF);
    assert(!fseek(f, pos, SEEK_SET) && fputc(c ^ 1, f) != EOF);
    assert(!fclose(f));
}

/* A map in the first stored format (version 1: no record sizes, no checksum). */
static void write_version_1(const char *path, const layer_index *ix) {
    FILE *f = fopen(path, "wb"); assert(f);
    int32_t version = 1;
    assert(fwrite("CC2LIDX1", 1, 8, f) == 8 && fwrite(&version, sizeof(version), 1, f) == 1 &&
           fwrite(&ix->key, sizeof(ix->key), 1, f) == 1 && fwrite(&ix->total, sizeof(int32_t), 5, f) == 5 &&
           fwrite(ix->entries, sizeof(*ix->entries), (size_t)ix->count, f) == (size_t)ix->count);
    assert(!fclose(f));
}

int main(int argc, char **argv) {
    (void)layer_path_mark_pick;
    (void)gcode_layer_path_parse_at;
    (void)sha256_fd; (void)gcode_layer_path_query; (void)gcode_layer_path_json;
    char dir[] = "/tmp/cc2-layer-index-XXXXXX";
    assert(mkdtemp(dir));
    int layers, totals;

    /* ElegooSlicer shape: ;LAYER:0 in the start block, then ;LAYER_CHANGE ... ;Z: ... ;LAYER:n. */
    FILE *f = gcode(dir, "elegoo.gcode",
        "; total layer number: 3\nG90\nM82\n;LAYER:0\nG1 X1 Y1 E1\n"
        ";LAYER_CHANGE\n;Z:0.2\n;LAYER:1\nG1 Z0.2\nG1 X10 Y10 E2\nG1 X20 Y10 E3\n"
        ";LAYER_CHANGE\n;Z:0.4\n;LAYER:2\nG1 Z0.4\nM83\nG1 X20 Y20 E1\nG92 E0\nG1 X10 Y20 E0.5\n"
        ";LAYER_CHANGE\n;Z:0.6\n;LAYER:3\nG91\nG1 X5 Y0 E0.2\nG90\nG2 X10 Y10 I5 J0 E0.3\nG1 Z5\n");
    assert(compare_all("elegoo", f, &layers, &totals) == 0 && layers == 3 && totals == 0);
    fclose(f);

    /* Cura shape: ;LAYER:N only, relative extrusion from the start, a layer without moves. */
    f = gcode(dir, "cura.gcode",
        ";LAYER_COUNT:4\nM83\n;LAYER:0\nG0 X0 Y0\nG1 X5 Y0 E1\n;LAYER:1\n;LAYER:2\nG1 X5 Y5 E1\n"
        "G0 X9 Y9\nG1 X9 Y1 E1\n;LAYER:3\nG3 X1 Y1 R4 E1\nN12 G1 X2 Y2 E1*33\n");
    assert(compare_all("cura", f, &layers, &totals) == 0 && layers == 4);
    fclose(f);

    /* The header promises more layers than the file marks: the counted layers win (layer count
     * only differs from the full scan, which still reads the header). */
    f = gcode(dir, "short.gcode", "; total layer number: 4\n;LAYER_CHANGE\nG1 X1 Y1 E1\n;LAYER_CHANGE\nG1 X2 Y2 E2\n");
    assert(compare_all("short", f, &layers, &totals) == 0 && layers == 2);
    fclose(f);

    /* A number the file leaves out is answered by the full scan: the index does not serve every layer. */
    f = gcode(dir, "gap.gcode", ";LAYER:0\nG1 X1 Y1 E1\n;LAYER:1\nG1 X2 Y2 E1\n;LAYER:3\nG1 X3 Y3 E1\n");
    {   layer_index gapped; assert(layer_index_build(f, &gapped, 0, NULL, NULL) == LAYER_PATH_OK);
        struct timespec at; clock_gettime(CLOCK_MONOTONIC, &at); layer_path_info gap_info;
        assert(gapped.count == 3 && !layer_index_serves_all(&gapped) && layer_index_can_serve(&gapped, 2) &&
               !layer_index_can_serve(&gapped, 3) && layer_index_can_serve(&gapped, 4));
        assert(gcode_layer_path_parse_indexed(f, &gapped, 3, pts_b, LAYER_PATH_COLLECT_MAX, starts_b, LAYER_PATH_PATHS_MAX,
                                              &gap_info, &at, 1000, 0) == LAYER_INDEX_NONE); /* the number it skips */
        layer_index_free(&gapped); }
    fclose(f);

    /* Long line across the 4096-byte read buffer and CRLF endings. */
    {
        size_t cap = 20000; char *text = malloc(cap); size_t used = 0;
        used += (size_t)snprintf(text + used, cap - used, ";LAYER_CHANGE\r\n;Z:0.2\r\nG1 X1 Y1 E1 ;");
        for (int i = 0; i < 9000; i++) text[used++] = 'x';
        used += (size_t)snprintf(text + used, cap - used, "\r\nG1 X2 Y2 E2\r\n;LAYER_CHANGE\r\n;Z:0.4\r\nG1 X3 Y3 E3\r\n");
        text[used] = 0;
        f = gcode(dir, "long.gcode", text);
        free(text);
        assert(compare_all("long", f, &layers, &totals) == 0 && layers == 2);
        fclose(f);
    }

    /* Files the index cannot serve exactly: they report LAYER_INDEX_NONE. */
    layer_index index; layer_path_info info; struct timespec start; clock_gettime(CLOCK_MONOTONIC, &start);
    f = gcode(dir, "plain.gcode", "G1 X1 Y1 E1\nG1 X2 Y2 E2\n");
    assert(layer_index_build(f, &index, 0, NULL, NULL) == LAYER_PATH_OK && !index.seen_marker);
    assert(!layer_index_can_serve(&index, 1));
    assert(gcode_layer_path_parse_indexed(f, &index, 1, pts_b, LAYER_PATH_COLLECT_MAX, starts_b, LAYER_PATH_PATHS_MAX,
                                          &info, &start, 1000, 0) == LAYER_INDEX_NONE);
    layer_index_free(&index); fclose(f);
    f = gcode(dir, "repeat.gcode", ";LAYER:0\nG1 X1 Y1 E1\n;LAYER:1\nG1 X2 Y2 E2\n;LAYER:0\nG1 X3 Y3 E3\n");
    assert(layer_index_build(f, &index, 0, NULL, NULL) == LAYER_PATH_OK && index.repeats);
    assert(!layer_index_can_serve(&index, 1));
    assert(gcode_layer_path_parse_indexed(f, &index, 1, pts_b, LAYER_PATH_COLLECT_MAX, starts_b, LAYER_PATH_PATHS_MAX,
                                          &info, &start, 1000, 0) == LAYER_INDEX_NONE);
    layer_index_free(&index); fclose(f);

    /* A layer above its own byte budget is refused; the worker can take the next request. */
    f = gcode(dir, "budget.gcode", ";LAYER_CHANGE\nG1 X1 Y1 E1\nG1 X2 Y2 E2\nG1 X3 Y3 E3\n;LAYER_CHANGE\nG1 X4 Y4 E4\n");
    assert(layer_index_build(f, &index, 0, NULL, NULL) == LAYER_PATH_OK);
    assert(gcode_layer_path_parse_indexed(f, &index, 1, pts_b, LAYER_PATH_COLLECT_MAX, starts_b, LAYER_PATH_PATHS_MAX,
                                          &info, &start, 1000, 20) == LAYER_PATH_LIMIT);
    assert(gcode_layer_path_parse_indexed(f, &index, 2, pts_b, LAYER_PATH_COLLECT_MAX, starts_b, LAYER_PATH_PATHS_MAX,
                                          &info, &start, 1000, 0) == LAYER_PATH_OK && info.point_count >= 1);
    assert(gcode_layer_path_parse_indexed(f, &index, 3, pts_b, LAYER_PATH_COLLECT_MAX, starts_b, LAYER_PATH_PATHS_MAX,
                                          &info, &start, 1000, 0) == LAYER_PATH_OUTSIDE && info.total_layers == 2);
    layer_index_free(&index);
    fclose(f);

    /* Index build must not treat a wall-clock timeout as success when target0 is open (-1). */
    {
        size_t cap = 256 * 1024, used = 0;
        char *text = malloc(cap);
        assert(text);
        for (int i = 0; i < 4000 && used + 80 < cap; i++)
            used += (size_t)snprintf(text + used, cap - used, ";LAYER_CHANGE\n;Z:%g\nG1 X%g Y%g E1\n", i * 0.2, i * 0.1, i * 0.1);
        f = gcode(dir, "timeout.gcode", text);
        free(text);
    }
    assert(layer_index_build(f, &index, 1, NULL, NULL) == LAYER_PATH_LIMIT);
    layer_index_free(&index);
    fclose(f);

    f = gcode(dir, "budget.gcode", ";LAYER_CHANGE\nG1 X1 Y1 E1\nG1 X2 Y2 E2\nG1 X3 Y3 E3\n;LAYER_CHANGE\nG1 X4 Y4 E4\n");
    assert(layer_index_build(f, &index, 0, NULL, NULL) == LAYER_PATH_OK);

    /* Storage: round trip, then a change of the G-code makes it stale. */
    char path[1024], gpath[600];
    snprintf(gpath, sizeof(gpath), "%s/budget.gcode", dir);
    assert(layer_index_path(dir, gpath, path, sizeof(path)) == 0);
    assert(layer_index_save(path, &index) == 0);
    layer_index loaded;
    assert(layer_index_load_valid(path, f, &loaded) == 0);
    assert(loaded.count == index.count && loaded.total == index.total &&
           !memcmp(loaded.entries, index.entries, sizeof(*index.entries) * (size_t)index.count));
    layer_index_free(&loaded);
    /* The stored map is checked against its own SHA-256: one flipped bit anywhere in it (version, key, counts,
     * the checksum, the middle of an entry) is refused, never read as a layer that is slightly wrong. */
    {
        assert(index.count == 2);
        long key_at = 8 + 4 + 2 * 4, head_at = key_at + (long)sizeof(layer_index_key);
        long entries_at = head_at + 5 * 4 + 32, stride = (long)sizeof(layer_index_entry);
        long where[] = { 10, key_at + 3, head_at, head_at + 4 * 4, head_at + 5 * 4 + 7,
                         entries_at + stride, entries_at + stride + 40, entries_at + 2 * stride - 5 };
        for (size_t i = 0; i < sizeof(where) / sizeof(where[0]); i++) {
            flip_bit(path, where[i]);
            if (layer_index_load(path, &loaded) == 0) {
                fprintf(stderr, "a flipped bit at byte %ld of the stored map was read\n", where[i]);
                layer_index_free(&loaded);
                assert(0);
            }
            flip_bit(path, where[i]);
            assert(layer_index_load(path, &loaded) == 0 && loaded.count == 2);
            layer_index_free(&loaded);
        }
    }
    /* A map written by the first version (no checksum) is refused and counts as stale, so it is rebuilt. */
    write_version_1(path, &index);
    assert(layer_index_load(path, &loaded) != 0 && layer_index_load_valid(path, f, &loaded) == 1);
    assert(layer_index_save(path, &index) == 0 && layer_index_load_valid(path, f, &loaded) == 0);
    layer_index_free(&loaded);
    /* A map that names another version or another record size than this build's is refused although its checksum is
     * right: it was written by other code (or the entry struct changed since), so its bytes mean something else. */
    for (int other = 0; other < 2; other++) {
        FILE *g = fopen(path, "wb"); assert(g);
        int32_t version = other ? LAYER_INDEX_VERSION + 1 : LAYER_INDEX_VERSION;
        int32_t sizes[2] = { (int32_t)sizeof(layer_index_entry) + (other ? 0 : 8), (int32_t)sizeof(layer_index_key) };
        int32_t head[5] = { index.total, index.max_layer, index.seen_marker, index.repeats, index.count };
        unsigned char digest[32];
        assert(layer_index_digest(version, sizes, &index.key, head, index.entries, index.count, digest) == 0);
        assert(fwrite("CC2LIDX1", 1, 8, g) == 8 && fwrite(&version, sizeof(version), 1, g) == 1 &&
               fwrite(sizes, sizeof(int32_t), 2, g) == 2 && fwrite(&index.key, sizeof(index.key), 1, g) == 1 &&
               fwrite(head, sizeof(int32_t), 5, g) == 5 && fwrite(digest, 1, sizeof(digest), g) == sizeof(digest) &&
               fwrite(index.entries, sizeof(*index.entries), (size_t)index.count, g) == (size_t)index.count);
        assert(!fclose(g));
        assert(layer_index_load(path, &loaded) != 0);
        assert(layer_index_save(path, &index) == 0 && layer_index_load_valid(path, f, &loaded) == 0);
        layer_index_free(&loaded);
    }
    sleep(1);
    fseek(f, 0, SEEK_END); fputs("G1 X5 Y5 E5\n", f); fflush(f);
    assert(layer_index_load_valid(path, f, &loaded) == 1);
    /* The key ties the map to its file: every field of it, alone, makes the map stale. */
    {
        FILE *g = gcode(dir, "key.gcode", ";LAYER_CHANGE\nG1 X1 Y1 E1\n;LAYER_CHANGE\nG1 X2 Y2 E2\n");
        layer_index_key k, o;
        assert(layer_index_key_of(g, &k) == 0 && layer_index_key_same(&k, &k));
#define KEY_DIFFERS(change) do { o = k; change; assert(!layer_index_key_same(&k, &o)); } while (0)
        KEY_DIFFERS(o.size += 1);
        KEY_DIFFERS(o.mtime_s += 1);
        KEY_DIFFERS(o.mtime_ns ^= 1);
        KEY_DIFFERS(o.ctime_s += 1);
        KEY_DIFFERS(o.ctime_ns ^= 1);
        KEY_DIFFERS(o.ino += 1);
        KEY_DIFFERS(o.edges[0] ^= 1);
        KEY_DIFFERS(o.edges[31] ^= 1);
#undef KEY_DIFFERS
        /* A file of the same size that replaces it, with its old modification time put back, is stale: another
         * inode, another change time, other bytes in the first and last 64 KiB. */
        char key_path[1024], map_path[1024], other_path[1024];
        snprintf(key_path, sizeof(key_path), "%s/key.gcode", dir);
        snprintf(other_path, sizeof(other_path), "%s/key-new.gcode", dir);
        layer_index kept;
        assert(layer_index_build(g, &kept, 0, NULL, NULL) == LAYER_PATH_OK);
        assert(layer_index_path(dir, key_path, map_path, sizeof(map_path)) == 0 && layer_index_save(map_path, &kept) == 0);
        struct stat before; assert(!fstat(fileno(g), &before));
        fclose(g);
        FILE *same_size = fopen(other_path, "w"); assert(same_size);
        assert(fputs(";LAYER_CHANGE\nG1 X9 Y9 E9\n;LAYER_CHANGE\nG1 X8 Y8 E8\n", same_size) >= 0 && !fclose(same_size));
        struct stat check; assert(!stat(other_path, &check) && check.st_size == before.st_size);
        assert(!rename(other_path, key_path));
#ifdef __APPLE__
        struct timespec back[2] = { before.st_mtimespec, before.st_mtimespec };
#else
        struct timespec back[2] = { before.st_mtim, before.st_mtim };
#endif
        assert(!utimensat(AT_FDCWD, key_path, back, 0));
        g = fopen(key_path, "rb"); assert(g);
        layer_index stale;
        assert(layer_index_load_valid(map_path, g, &stale) == 1);
        fclose(g); layer_index_free(&kept);
    }
    /* A damaged index file is refused, never half read. */
    FILE *broken = fopen(path, "r+b"); assert(broken); fseek(broken, -3, SEEK_END); fputc('!', broken);
    fseek(broken, 0, SEEK_END); fputc('!', broken); fclose(broken);
    assert(layer_index_load(path, &loaded) != 0);
    layer_index_free(&index); fclose(f);

    for (int i = 1; i < argc; i++) {
        f = fopen(argv[i], "rb");
        assert(f);
        int bad = compare_all(argv[i], f, &layers, &totals);
        printf("%s: %d layers, %d differing, %d with a different layer count only\n", argv[i], layers, bad, totals);
        assert(bad == 0);
        fclose(f);
    }
    puts("PASS: layer index equals the full scan for every layer (ElegooSlicer, Cura, relative/absolute, arcs, "
         "long lines, CRLF), refuses files without markers or with repeated layers, per-layer budget, "
         "storage round trip, stale and damaged index files, a checksum over the stored map, old versions and other record sizes refused");
    return 0;
}
