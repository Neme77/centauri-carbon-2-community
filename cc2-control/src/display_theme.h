/* Native touchscreen appearance: capability detection, asset serving and the
 * reviewed, reversible theme pack installer.
 *
 * The touchscreen is a vendor LVGL program with its own resource directory.
 * Nothing here patches or restarts it. Only the presentation PNG files named
 * by a manifest shipped with the firmware are replaced, on disk, atomically,
 * after their exact baseline has been backed up. A full printer reboot is
 * required for the native program to load them. Fonts, layouts, widgets and
 * the native program itself are deliberately not part of this interface.
 *
 * Safety model:
 *  - a manifest is required; no manifest means "not supported", never
 *    "guess the files";
 *  - the active theme directory, the GUI hash and every replaced baseline
 *    hash are verified before a write;
 *  - the printer must be idle and no update may be running;
 *  - originals are copied to a baseline before the first change and the
 *    complete pack is validated (names, roles, dimensions, PNG chunks) before
 *    any target file is touched;
 *  - a journal restores the baseline after an interrupted apply, at the next
 *    CC2 Control start;
 *  - restoring refuses files that were changed outside this feature.
 *
 * Depends on main.c helpers: respond(), content_length_from_headers(),
 * upload_slot_acquire()/upload_slot_release(), gcode_idle_for_mutation(),
 * app_update_active(), sha256_*, cc2_crc32, json_builder.
 */
#ifndef CC2_DISPLAY_THEME_H
#define CC2_DISPLAY_THEME_H

#include "connections/json_large.h"

#define DISP_MAX_ASSETS 320
#define DISP_NAME_MAX 64
#define DISP_ROLE_MAX 16
#define DISP_MAX_PACK (4 * 1024 * 1024)
#define DISP_MAX_FILE (512 * 1024)
#define DISP_MAX_STAGE 256
/* Upper bound for one metadata document (state, journal, baseline manifest).
 * 320 assets x ~110 bytes = ~35 KiB, so 64 KiB is the supported maximum and
 * every writer and reader uses it. Baseline image files are separate. */
#define DISP_META_MAX (64 * 1024)

typedef struct {
    char name[DISP_NAME_MAX + 1];
    char role[DISP_ROLE_MAX];
    int width, height;
    char sha256[65];
    char baseline_sha256[65]; /* current on-disk bytes when the manifest was read */
    int present;
} disp_asset;

typedef struct {
    int valid;
    int schema;
    char firmware_base[32];
    char gui_sha256[65];
    char error[96];
    disp_asset assets[DISP_MAX_ASSETS];
    int count;
} disp_manifest;

typedef struct {
    int phase; /* 0 none, 1 applied, 2 failed, 3 recovered */
    char boot_id[40];
    char gui_marker[64];
    long long applied_at;
    char files[DISP_MAX_STAGE][DISP_NAME_MAX + 1];
    char hashes[DISP_MAX_STAGE][65];
    int file_count;
} disp_state;

static char disp_theme_dir[PATH_MAX_LOCAL] = "/opt/usr/images/theme_default";
static char disp_gui_path[PATH_MAX_LOCAL] = "/opt/bin/ec-eeb001-gui";
static char disp_state_dir[PATH_MAX_LOCAL];
static char disp_manifest_path[PATH_MAX_LOCAL];
static char disp_error[160];
static disp_manifest disp_manifest_cache;
static disp_state disp_state_cache;
static int disp_state_loaded;
/* The error string is written by the apply thread and read by status/asset
 * handlers on the single request thread; a dedicated mutex keeps it whole
 * without serialising the display state itself. */
static pthread_mutex_t disp_error_mutex = PTHREAD_MUTEX_INITIALIZER;
/* Guards the manifest and state caches and their files. It is never held
 * across a network transfer, so unrelated API requests stay responsive while
 * an upload streams in. */
static pthread_mutex_t disp_mutex = PTHREAD_MUTEX_INITIALIZER;
/* Serialises apply, restore and startup recovery (all mutate display files). */
static pthread_mutex_t disp_apply_mutex = PTHREAD_MUTEX_INITIALIZER;

static int disp_regular(const char *path) {
    struct stat st;
    return lstat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int disp_hex64(const char *value) {
    if (!value || strlen(value) != 64) return 0;
    for (int i = 0; i < 64; ++i)
        if (!isxdigit((unsigned char)value[i])) return 0;
    return 1;
}

static int disp_name_valid(const char *name) {
    size_t n = name ? strlen(name) : 0;
    if (n < 5 || n > DISP_NAME_MAX) return 0;
    if (strcasecmp(name + n - 4, ".png")) return 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (!(isalnum(c) || c == '_' || c == '-' || c == '.')) return 0;
    }
    return strcmp(name, ".") && strcmp(name, "..");
}

static int disp_path_join(char *out, size_t cap, const char *dir, const char *name) {
    size_t d = strlen(dir), n = strlen(name);
    if (d + n + 2 > cap) return 0;
    memcpy(out, dir, d);
    out[d] = '/';
    memcpy(out + d + 1, name, n + 1);
    return 1;
}

static int disp_sha_file(const char *path, char out[65]) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    sha256_state state;
    sha256_init(&state);
    unsigned char buffer[16384];
    size_t n;
    int ok = 1;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) sha256_update(&state, buffer, n);
    if (ferror(file)) ok = 0;
    fclose(file);
    if (ok) sha256_hex(&state, out);
    return ok;
}

static int disp_sha_bytes(const unsigned char *data, size_t length, char out[65]) {
    sha256_state state;
    sha256_init(&state);
    sha256_update(&state, data, length);
    sha256_hex(&state, out);
    return 1;
}

static void disp_set_error(const char *text) {
    pthread_mutex_lock(&disp_error_mutex);
    snprintf(disp_error, sizeof(disp_error), "%s", text ? text : "");
    pthread_mutex_unlock(&disp_error_mutex);
}

static void disp_copy_error(char *out, size_t cap) {
    pthread_mutex_lock(&disp_error_mutex);
    snprintf(out, cap, "%s", disp_error);
    pthread_mutex_unlock(&disp_error_mutex);
}

static void disp_clear_error(void) {
    pthread_mutex_lock(&disp_error_mutex);
    disp_error[0] = '\0';
    pthread_mutex_unlock(&disp_error_mutex);
}

/* Read one bounded JSON metadata file. Returns a malloc'd, NUL-terminated body
 * (caller frees) or NULL when the file is absent, unreadable, empty, too large
 * or shorter than its own size. Never passes a NULL FILE* to fread. */
static char *disp_read_json_file(const char *path, size_t *length) {
    struct stat st;
    if (lstat(path, &st) != 0) return NULL;
    if (!S_ISREG(st.st_mode) || st.st_size <= 0 || (size_t)st.st_size > DISP_META_MAX) {
        disp_set_error("display_metadata_size_invalid");
        return NULL;
    }
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        disp_set_error("display_metadata_unreadable");
        return NULL;
    }
    char *body = malloc((size_t)st.st_size + 1);
    if (!body) {
        close(fd);
        disp_set_error("display_metadata_oom");
        return NULL;
    }
    size_t done = 0;
    while (done < (size_t)st.st_size) {
        ssize_t n = read(fd, body + done, (size_t)st.st_size - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        done += (size_t)n;
    }
    close(fd);
    if (done != (size_t)st.st_size) {
        free(body);
        disp_set_error("display_metadata_truncated");
        return NULL;
    }
    body[done] = '\0';
    if (length) *length = done;
    return body;
}

/* ---- boot id ---------------------------------------------------------- */

static void disp_boot_id(char out[40]) {
    out[0] = '\0';
    FILE *file = fopen("/proc/sys/kernel/random/boot_id", "rb");
    if (!file) return;
    if (!fgets(out, 40, file)) out[0] = '\0';
    fclose(file);
    size_t n = strlen(out);
    while (n && (out[n - 1] == '\n' || out[n - 1] == '\r')) out[--n] = '\0';
}

/* Start time of the native GUI process, or "" when it is not running. The
 * marker changes exactly when the native program restarts, so a CC2 Control
 * restart alone does not make a pending "reboot required" disappear. */
static void disp_gui_marker(char out[64]) {
    out[0] = '\0';
    DIR *proc = opendir("/proc");
    if (!proc) return;
    struct dirent *entry;
    while ((entry = readdir(proc))) {
        if (entry->d_name[0] < '0' || entry->d_name[0] > '9') continue;
        char path[300], comm[64];
        snprintf(path, sizeof(path), "/proc/%s/comm", entry->d_name);
        FILE *file = fopen(path, "rb");
        if (!file) continue;
        char *got = fgets(comm, sizeof(comm), file);
        fclose(file);
        if (!got) continue;
        comm[strcspn(comm, "\r\n")] = '\0';
        if (strcmp(comm, "ec-eeb001-gui")) continue;
        snprintf(path, sizeof(path), "/proc/%s/stat", entry->d_name);
        file = fopen(path, "rb");
        if (!file) continue;
        char stat[1024];
        size_t n = fread(stat, 1, sizeof(stat) - 1, file);
        fclose(file);
        stat[n] = '\0';
        /* Field 22 is the process start time; skip past the command name
         * (which may contain spaces) by starting after the last ')'. */
        char *close = strrchr(stat, ')');
        if (!close) continue;
        int field = 2;
        char *p = close + 1;
        while (*p && field < 22) {
            while (*p == ' ') p++;
            if (!*p) break;
            field++;
            while (*p && *p != ' ') p++;
        }
        if (field == 22) {
            while (*p == ' ') p++;
            size_t len = strcspn(p, " \n");
            if (len && len < 40) {
                snprintf(out, 64, "%.12s:%.24s", entry->d_name, p);
                break;
            }
        }
    }
    closedir(proc);
}

/* ---- manifest --------------------------------------------------------- */

/* Returns 1 when the manifest is usable, 0 with disp_error explaining why. */
static int disp_load_manifest(int force) {
    if (!force && disp_manifest_cache.valid) return 1;
    memset(&disp_manifest_cache, 0, sizeof(disp_manifest_cache));
    if (!disp_manifest_path[0]) {
        if (snprintf(disp_manifest_path, sizeof(disp_manifest_path), "%s/cc2-display-manifest.json",
                     disp_theme_dir) >= (int)sizeof(disp_manifest_path)) {
            disp_set_error("Display manifest path is too long");
            return 0;
        }
    }
    struct stat st;
    if (stat(disp_theme_dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
        disp_set_error("display_theme_missing");
        return 0;
    }
    if (lstat(disp_theme_dir, &st) == 0 && S_ISLNK(st.st_mode)) {
        disp_set_error("display_theme_symlink");
        return 0;
    }
    if (!disp_regular(disp_manifest_path)) {
        disp_set_error("display_manifest_missing");
        return 0;
    }
    FILE *file = fopen(disp_manifest_path, "rb");
    if (!file) { disp_set_error("display_manifest_unreadable"); return 0; }
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); disp_set_error("display_manifest_unreadable"); return 0; }
    long size = ftell(file);
    if (size <= 0 || size > AP_MAX_JSON) {
        fclose(file);
        disp_set_error("display_manifest_unreadable");
        return 0;
    }
    rewind(file);
    char *body = malloc((size_t)size + 1);
    if (!body) { fclose(file); disp_set_error("display_manifest_unreadable"); return 0; }
    size_t got = fread(body, 1, (size_t)size, file);
    fclose(file);
    body[got] = '\0';
    ngl_json *doc = malloc(sizeof(*doc));
    int ok = got == (size_t)size && doc && ngl_json_parse(doc, body, got) && doc->v[0].type == '{';
    if (!ok) { free(doc); free(body); disp_set_error("display_manifest_invalid"); return 0; }
    disp_manifest *m = &disp_manifest_cache;
    double schema = 0;
    if (!ngl_number(doc, 0, "schema", &schema) || schema != 1) ok = 0;
    m->schema = 1;
    const char *base = ngl_string(doc, 0, "firmwareBase");
    if (base) snprintf(m->firmware_base, sizeof(m->firmware_base), "%s", base);
    const char *gui = ngl_string(doc, 0, "guiSha256");
    if (gui && !disp_hex64(gui)) ok = 0;
    if (gui) snprintf(m->gui_sha256, sizeof(m->gui_sha256), "%s", gui);
    int assets = ngl_member(doc, 0, "assets");
    if (assets < 0 || doc->v[assets].type != '[') ok = 0;
    if (ok) {
        for (int i = assets + 1; i < doc->v[assets].next; i = doc->v[i].next) {
            if (m->count >= DISP_MAX_ASSETS || doc->v[i].type != '{') { ok = 0; break; }
            disp_asset *a = &m->assets[m->count];
            const char *name = ngl_string(doc, i, "name");
            const char *role = ngl_string(doc, i, "role");
            const char *sha = ngl_string(doc, i, "sha256");
            double width = 0, height = 0;
            if (!name || !disp_name_valid(name) || !role ||
                (strcmp(role, "boot") && strcmp(role, "background") && strcmp(role, "accent")) ||
                !sha || !disp_hex64(sha) ||
                !ngl_number(doc, i, "width", &width) || !ngl_number(doc, i, "height", &height) ||
                width < 1 || height < 1 || width > 2048 || height > 2048 ||
                width != floor(width) || height != floor(height)) { ok = 0; break; }
            for (int p = 0; p < m->count; ++p)
                if (!strcmp(m->assets[p].name, name)) { ok = 0; break; }
            if (!ok) break;
            snprintf(a->name, sizeof(a->name), "%s", name);
            snprintf(a->role, sizeof(a->role), "%s", role);
            snprintf(a->sha256, sizeof(a->sha256), "%s", sha);
            a->width = (int)width;
            a->height = (int)height;
            m->count++;
        }
    }
    free(doc);
    free(body);
    if (!ok || !m->count) {
        memset(m, 0, sizeof(*m));
        disp_set_error("display_manifest_invalid");
        return 0;
    }
    m->valid = 1;
    disp_clear_error();
    return 1;
}

static int disp_manifest_identity(const disp_manifest *m, const char *base, const char *gui) {
    if (m->firmware_base[0] && base && strcmp(m->firmware_base, base)) return 0;
    if (m->gui_sha256[0] && gui && strcmp(m->gui_sha256, gui)) return 0;
    return 1;
}

/* The GUI is a large file: hash it once per inode/size/mtime. */
static int disp_gui_matches(const disp_manifest *m) {
    if (!m->gui_sha256[0]) return 1;
    static struct stat last;
    static char hash[65];
    static int have;
    struct stat st;
    if (stat(disp_gui_path, &st) != 0) { disp_set_error("display_gui_missing"); return 0; }
    if (!have || st.st_ino != last.st_ino || st.st_size != last.st_size ||
        st.st_mtim.tv_sec != last.st_mtim.tv_sec || st.st_mtim.tv_nsec != last.st_mtim.tv_nsec) {
        if (!disp_sha_file(disp_gui_path, hash)) { disp_set_error("display_gui_unreadable"); return 0; }
        last = st;
        have = 1;
    }
    if (strcasecmp(hash, m->gui_sha256)) { disp_set_error("display_gui_mismatch"); return 0; }
    return 1;
}

static int disp_active_theme_ok(void) {
    char link[PATH_MAX_LOCAL];
    if (snprintf(link, sizeof(link), "%s/current_theme", "/opt/usr/images") >= (int)sizeof(link)) return 0;
    struct stat st;
    if (lstat(link, &st) != 0) return 1; /* installed layouts that do not use the link */
    if (!S_ISLNK(st.st_mode)) { disp_set_error("display_active_theme_invalid"); return 0; }
    char target[PATH_MAX_LOCAL], here[PATH_MAX_LOCAL], resolved[PATH_MAX_LOCAL];
    ssize_t n = readlink(link, target, sizeof(target) - 1);
    if (n <= 0) { disp_set_error("display_active_theme_invalid"); return 0; }
    target[n] = '\0';
    if (!realpath(target, resolved) || !realpath(disp_theme_dir, here) || strcmp(here, resolved)) {
        disp_set_error("display_active_theme_mismatch");
        return 0;
    }
    return 1;
}

/* 1 supported; fills disp_error with a stable identifier otherwise. */
static int disp_capability(void) {
    if (!disp_load_manifest(0)) return 0;
    if (!disp_gui_matches(&disp_manifest_cache)) return 0;
    if (!disp_active_theme_ok()) return 0;
    disp_clear_error();
    return 1;
}

/* ---- state and baseline ----------------------------------------------- */

static int disp_ensure_state_dir(void) {
    if (!disp_state_dir[0]) {
        const char *slash = strrchr(ui_preferences_path, '/');
        int n = slash ? snprintf(disp_state_dir, sizeof(disp_state_dir), "%.*s/display-theme",
                                 (int)(slash - ui_preferences_path), ui_preferences_path)
                      : snprintf(disp_state_dir, sizeof(disp_state_dir), "display-theme");
        if (n <= 0 || (size_t)n >= sizeof(disp_state_dir)) return 0;
    }
    if (mkdir(disp_state_dir, 0700) != 0 && errno != EEXIST) return 0;
    struct stat st;
    if (lstat(disp_state_dir, &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
    return 1;
}

static int disp_journal_path(char *out, size_t cap) {
    return disp_path_join(out, cap, disp_state_dir, "journal.json");
}
static int disp_state_path(char *out, size_t cap) {
    return disp_path_join(out, cap, disp_state_dir, "state.json");
}
static int disp_baseline_dir(char *out, size_t cap) {
    return disp_path_join(out, cap, disp_state_dir, "baseline");
}
static int disp_baseline_manifest(char *out, size_t cap) {
    return disp_path_join(out, cap, disp_state_dir, "baseline.json");
}
static int disp_stage_dir(char *out, size_t cap) {
    return disp_path_join(out, cap, disp_state_dir, "stage");
}

static void disp_state_defaults(disp_state *s) {
    memset(s, 0, sizeof(*s));
    s->phase = 0;
}

static int disp_state_read(void) {
    if (disp_state_loaded) return 1;
    disp_state_loaded = 1;
    disp_state_defaults(&disp_state_cache);
    char path[PATH_MAX_LOCAL];
    if (!disp_ensure_state_dir() || !disp_state_path(path, sizeof(path))) return 0;
    struct stat st;
    if (lstat(path, &st) != 0) return 1; /* no state yet: defaults are valid */
    if (!S_ISREG(st.st_mode)) {
        disp_set_error("display_state_invalid");
        return 0;
    }
    size_t length = 0;
    char *body = disp_read_json_file(path, &length);
    if (!body) return 0;
    ngl_json *doc = malloc(sizeof(*doc));
    if (!doc || !ngl_json_parse(doc, body, length) || doc->v[0].type != '{') {
        free(doc);
        free(body);
        disp_set_error("display_state_invalid");
        return 0;
    }
    free(body);
    const char *phase = ngl_string(doc, 0, "phase");
    if (phase) {
        if (!strcmp(phase, "applied")) disp_state_cache.phase = 1;
        else if (!strcmp(phase, "failed")) disp_state_cache.phase = 2;
        else if (!strcmp(phase, "recovered")) disp_state_cache.phase = 3;
        else if (!strcmp(phase, "restored")) disp_state_cache.phase = 0;
    }
    const char *boot = ngl_string(doc, 0, "bootId");
    if (boot) snprintf(disp_state_cache.boot_id, sizeof(disp_state_cache.boot_id), "%s", boot);
    const char *marker = ngl_string(doc, 0, "guiMarker");
    if (marker) snprintf(disp_state_cache.gui_marker, sizeof(disp_state_cache.gui_marker), "%s", marker);
    double at = 0;
    if (ngl_number(doc, 0, "appliedAt", &at) && at > 0 && at < 4e9) disp_state_cache.applied_at = (long long)at;
    int files = ngl_member(doc, 0, "files");
    if (files >= 0 && doc->v[files].type == '[') {
        for (int i = files + 1; i < doc->v[files].next && disp_state_cache.file_count < DISP_MAX_STAGE;
             i = doc->v[i].next) {
            const char *name = ngl_string(doc, i, "name");
            const char *hash = ngl_string(doc, i, "sha256");
            if (!name || !hash || !disp_name_valid(name) || !disp_hex64(hash)) continue;
            snprintf(disp_state_cache.files[disp_state_cache.file_count],
                     sizeof(disp_state_cache.files[0]), "%s", name);
            snprintf(disp_state_cache.hashes[disp_state_cache.file_count],
                     sizeof(disp_state_cache.hashes[0]), "%s", hash);
            disp_state_cache.file_count++;
        }
    }
    free(doc);
    return 1;
}

static int disp_state_write(void) {
    if (!disp_ensure_state_dir()) return 0;
    char path[PATH_MAX_LOCAL];
    if (!disp_state_path(path, sizeof(path))) return 0;
    json_builder b;
    char *body = malloc(DISP_META_MAX);
    if (!body) return 0;
    b = (json_builder){body, 0, DISP_META_MAX, 0};
    const char *phase = disp_state_cache.phase == 1 ? "applied"
                      : disp_state_cache.phase == 2 ? "failed"
                      : disp_state_cache.phase == 3 ? "recovered" : "restored";
    json_builder_printf(&b, "{\"version\":1,\"phase\":\"%s\",\"bootId\":", phase);
    json_builder_string(&b, disp_state_cache.boot_id);
    json_builder_printf(&b, ",\"guiMarker\":");
    json_builder_string(&b, disp_state_cache.gui_marker);
    json_builder_printf(&b, ",\"appliedAt\":%lld,\"files\":[", disp_state_cache.applied_at);
    for (int i = 0; i < disp_state_cache.file_count; ++i) {
        if (i) json_builder_printf(&b, ",");
        json_builder_printf(&b, "{\"name\":");
        json_builder_string(&b, disp_state_cache.files[i]);
        json_builder_printf(&b, ",\"sha256\":");
        json_builder_string(&b, disp_state_cache.hashes[i]);
        json_builder_printf(&b, "}");
    }
    json_builder_printf(&b, "]}\n");
    int ok = !b.failed;
    if (ok) ok = ap_write_all(path, body, b.length, 0600);
    if (!ok) disp_set_error("display_state_write_failed");
    free(body);
    return ok;
}

/* Baseline manifest: identity of the firmware display the originals came from,
 * the theme directory they belong to, and each original's SHA-256. */
typedef struct {
    char firmware_base[32];
    char gui_sha256[65];
    char theme_dir[PATH_MAX_LOCAL];
} disp_baseline_identity;

/* A set of file names and expected SHA-256 digests: the state of an applied
 * pack, or the files recorded by an interrupted apply's journal. */
typedef struct {
    char names[DISP_MAX_STAGE][DISP_NAME_MAX + 1];
    char hashes[DISP_MAX_STAGE][65];
    int count;
} disp_hash_set;

static int disp_hash_set_add(disp_hash_set *set, const char *name, const char *hash) {
    if (set->count >= DISP_MAX_STAGE) return 0;
    snprintf(set->names[set->count], sizeof(set->names[0]), "%s", name);
    snprintf(set->hashes[set->count], sizeof(set->hashes[0]), "%s", hash);
    set->count++;
    return 1;
}

static int disp_hash_set_has(const disp_hash_set *set, const char *name, const char *hash) {
    for (int i = 0; i < set->count; ++i)
        if (!strcmp(set->names[i], name) && !strcasecmp(set->hashes[i], hash)) return 1;
    return 0;
}

/* The state cache is also a hash set; keep a small adapter so restore can use
 * both without copying. */
static int disp_state_has(const disp_state *state, const char *name, const char *hash) {
    for (int i = 0; i < state->file_count; ++i)
        if (!strcmp(state->files[i], name) && !strcasecmp(state->hashes[i], hash)) return 1;
    return 0;
}

static int disp_baseline_write(const disp_manifest *m) {
    char dir[PATH_MAX_LOCAL], path[PATH_MAX_LOCAL];
    if (!disp_baseline_dir(dir, sizeof(dir)) || !disp_baseline_manifest(path, sizeof(path))) return 0;
    DIR *d = opendir(dir);
    if (!d) return 0;
    json_builder b;
    char *body = malloc(DISP_META_MAX);
    if (!body) { closedir(d); return 0; }
    b = (json_builder){body, 0, DISP_META_MAX, 0};
    json_builder_printf(&b, "{\"version\":1,\"firmwareBase\":");
    json_builder_string(&b, m->firmware_base);
    json_builder_printf(&b, ",\"guiSha256\":");
    json_builder_string(&b, m->gui_sha256);
    json_builder_printf(&b, ",\"themeDir\":");
    json_builder_string(&b, disp_theme_dir);
    json_builder_printf(&b, ",\"files\":[");
    struct dirent *entry;
    int first = 1;
    while ((entry = readdir(d))) {
        if (!disp_name_valid(entry->d_name)) continue;
        char file[PATH_MAX_LOCAL], hash[65];
        if (!disp_path_join(file, sizeof(file), dir, entry->d_name) || !disp_regular(file) ||
            !disp_sha_file(file, hash)) continue;
        if (!first) json_builder_printf(&b, ",");
        first = 0;
        json_builder_printf(&b, "{\"name\":");
        json_builder_string(&b, entry->d_name);
        json_builder_printf(&b, ",\"sha256\":");
        json_builder_string(&b, hash);
        json_builder_printf(&b, "}");
    }
    closedir(d);
    json_builder_printf(&b, "]}\n");
    int ok = !b.failed && ap_write_all(path, body, b.length, 0600);
    free(body);
    if (!ok) disp_set_error("display_backup_manifest_failed");
    return ok;
}

typedef struct {
    char name[DISP_NAME_MAX + 1];
    char sha256[65];
    int present;
} disp_baseline_entry;

/* Read the baseline manifest.
 *   1  the manifest is valid; entries, count and identity are filled
 *   0  no usable baseline (absent, unreadable, invalid, or belongs to another
 *      theme directory)
 *  -1  the baseline exists but belongs to a different firmware/GUI, which only
 *      matters when `compare_current` is set (the apply path). Restore passes 0
 *      so it can still return to a verified baseline after an update. */
static int disp_baseline_read(disp_baseline_entry *entries, int *count,
                              disp_baseline_identity *identity, int compare_current) {
    *count = 0;
    if (identity) memset(identity, 0, sizeof(*identity));
    char path[PATH_MAX_LOCAL];
    if (!disp_baseline_manifest(path, sizeof(path))) return 0;
    size_t length = 0;
    char *body = disp_read_json_file(path, &length);
    if (!body) return 0;
    ngl_json *doc = malloc(sizeof(*doc));
    if (!doc || !ngl_json_parse(doc, body, length) || doc->v[0].type != '{') {
        free(doc);
        free(body);
        disp_set_error("display_backup_missing");
        return 0;
    }
    free(body);
    const char *base = ngl_string(doc, 0, "firmwareBase");
    const char *gui = ngl_string(doc, 0, "guiSha256");
    const char *theme = ngl_string(doc, 0, "themeDir");
    if (identity) {
        if (base) snprintf(identity->firmware_base, sizeof(identity->firmware_base), "%s", base);
        if (gui) snprintf(identity->gui_sha256, sizeof(identity->gui_sha256), "%s", gui);
        if (theme) snprintf(identity->theme_dir, sizeof(identity->theme_dir), "%s", theme);
    }
    if (theme && theme[0] && strcmp(theme, disp_theme_dir)) {
        free(doc);
        disp_set_error("display_theme_changed");
        return 0;
    }
    if (compare_current && !disp_manifest_identity(&disp_manifest_cache, base, gui)) {
        free(doc);
        disp_set_error("display_firmware_changed");
        return -1;
    }
    int files = ngl_member(doc, 0, "files");
    if (files < 0 || doc->v[files].type != '[') { free(doc); return 0; }
    for (int i = files + 1; i < doc->v[files].next && *count < DISP_MAX_STAGE; i = doc->v[i].next) {
        const char *name = ngl_string(doc, i, "name");
        const char *hash = ngl_string(doc, i, "sha256");
        if (!name || !hash || !disp_name_valid(name) || !disp_hex64(hash)) continue;
        disp_baseline_entry *e = &entries[(*count)++];
        snprintf(e->name, sizeof(e->name), "%s", name);
        snprintf(e->sha256, sizeof(e->sha256), "%s", hash);
        char dir[PATH_MAX_LOCAL], file_path[PATH_MAX_LOCAL];
        if (disp_baseline_dir(dir, sizeof(dir)) && disp_path_join(file_path, sizeof(file_path), dir, name)) {
            struct stat st;
            if (lstat(file_path, &st) == 0 && S_ISREG(st.st_mode)) e->present = 1;
        }
    }
    free(doc);
    return 1;
}

/* Read the hashes recorded by an interrupted apply, so startup recovery may
 * replace a file this feature was writing as well as one that is still the
 * original. Absent or invalid journals simply contribute nothing. */
static int disp_journal_read(disp_hash_set *set) {
    set->count = 0;
    char path[PATH_MAX_LOCAL];
    if (!disp_journal_path(path, sizeof(path))) return 0;
    if (!disp_regular(path)) return 0;
    size_t length = 0;
    char *body = disp_read_json_file(path, &length);
    if (!body) return 0;
    ngl_json *doc = malloc(sizeof(*doc));
    if (!doc || !ngl_json_parse(doc, body, length) || doc->v[0].type != '{') {
        free(doc);
        free(body);
        return 0;
    }
    free(body);
    int files = ngl_member(doc, 0, "files");
    if (files >= 0 && doc->v[files].type == '[') {
        for (int i = files + 1; i < doc->v[files].next; i = doc->v[i].next) {
            const char *name = ngl_string(doc, i, "name");
            const char *hash = ngl_string(doc, i, "sha256");
            if (!name || !hash || !disp_name_valid(name) || !disp_hex64(hash)) continue;
            if (!disp_hash_set_add(set, name, hash)) break;
        }
    }
    free(doc);
    return set->count > 0;
}

/* Back up one target before its first change. Returns 1 when a verified
 * original exists afterwards. The copy is atomic (temporary file, fsync,
 * rename, directory sync), so a power loss never leaves a final partial file;
 * a backup that does not match the manifest is repaired only when the target
 * still is the manifest original. */
static int disp_backup_original(const disp_asset *asset) {
    char dir[PATH_MAX_LOCAL], backup[PATH_MAX_LOCAL], target[PATH_MAX_LOCAL];
    if (!disp_baseline_dir(dir, sizeof(dir))) return 0;
    if (mkdir(dir, 0700) != 0 && errno != EEXIST) return 0;
    if (!disp_path_join(backup, sizeof(backup), dir, asset->name) ||
        !disp_path_join(target, sizeof(target), disp_theme_dir, asset->name)) return 0;
    char hash[65];
    struct stat st;
    if (lstat(backup, &st) == 0) {
        if (S_ISREG(st.st_mode) && disp_sha_file(backup, hash) && !strcasecmp(hash, asset->sha256))
            return 1; /* already preserved and verified */
        /* Unverified leftover: only a target that still is the manifest
         * original may replace it. A customised target means we would save
         * customised bytes as the original: refuse instead. */
        if (!disp_regular(target) || !disp_sha_file(target, hash) || strcasecmp(hash, asset->sha256)) {
            disp_set_error("display_backup_mismatch");
            return 0;
        }
        unlink(backup);
    }
    if (!disp_regular(target)) { disp_set_error("display_asset_missing"); return 0; }
    if (!disp_sha_file(target, hash)) { disp_set_error("display_asset_missing"); return 0; }
    if (strcasecmp(hash, asset->sha256)) {
        disp_set_error("display_asset_modified");
        return 0;
    }
    if (!ap_copy_verified(target, backup, 0600)) {
        disp_set_error("display_backup_write_failed");
        return 0;
    }
    if (!disp_sha_file(backup, hash) || strcasecmp(hash, asset->sha256)) {
        unlink(backup);
        disp_set_error("display_backup_mismatch");
        return 0;
    }
    return 1;
}

static const disp_asset *disp_find_asset_in(const disp_manifest *m, const char *name) {
    for (int i = 0; i < m->count; ++i)
        if (!strcmp(m->assets[i].name, name))
            return &m->assets[i];
    return NULL;
}

static const disp_asset *disp_find_asset(const char *name) {
    return disp_find_asset_in(&disp_manifest_cache, name);
}

/* Restore every baseline file. Used by restore, the apply failure path and
 * startup recovery. `strict` accepts only a current file that is the original
 * or one of our recorded versions (state plus `extra`, normally the journal),
 * so foreign changes are preserved. Every backup is verified against the
 * manifest hash before anything is copied. */
static int disp_restore_baseline(int strict, const disp_state *state, const disp_hash_set *extra) {
    disp_baseline_entry entries[DISP_MAX_STAGE];
    int count = 0;
    int read = disp_baseline_read(entries, &count, NULL, 0);
    if (read <= 0) {
        if (read == 0) disp_set_error("display_backup_missing");
        return 0;
    }
    char dir[PATH_MAX_LOCAL];
    if (!disp_baseline_dir(dir, sizeof(dir))) return 0;
    /* First pass: every backup must be exactly the recorded original and every
     * strict target must be one of ours. Nothing is written if one fails. */
    for (int i = 0; i < count; ++i) {
        if (!entries[i].present) {
            disp_set_error("display_backup_missing");
            return 0;
        }
        char backup[PATH_MAX_LOCAL], target[PATH_MAX_LOCAL], hash[65];
        if (!disp_path_join(backup, sizeof(backup), dir, entries[i].name) ||
            !disp_path_join(target, sizeof(target), disp_theme_dir, entries[i].name)) return 0;
        if (!disp_regular(backup) || !disp_sha_file(backup, hash) || strcasecmp(hash, entries[i].sha256)) {
            disp_set_error("display_backup_mismatch");
            return 0;
        }
        if (strict) {
            if (!disp_regular(target) || !disp_sha_file(target, hash)) {
                disp_set_error("display_asset_missing");
                return 0;
            }
            int ours = !strcasecmp(hash, entries[i].sha256) ||
                       (state && disp_state_has(state, entries[i].name, hash)) ||
                       (extra && disp_hash_set_has(extra, entries[i].name, hash));
            if (!ours) {
                disp_set_error("display_asset_foreign_change");
                return 0;
            }
        }
    }
    /* Second pass: copy and read back. */
    for (int i = 0; i < count; ++i) {
        char backup[PATH_MAX_LOCAL], target[PATH_MAX_LOCAL];
        if (!disp_path_join(backup, sizeof(backup), dir, entries[i].name) ||
            !disp_path_join(target, sizeof(target), disp_theme_dir, entries[i].name)) return 0;
        if (!ap_copy_verified(backup, target, 0644)) {
            disp_set_error("display_restore_failed");
            return 0;
        }
    }
    sync();
    return 1;
}

/* ---- PNG and tar validation ------------------------------------------- */

static int disp_png_valid(const unsigned char *data, size_t length, int width, int height, char *why, size_t why_cap) {
    static const unsigned char signature[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (length < 8 + 25 + 12 || memcmp(data, signature, 8)) {
        snprintf(why, why_cap, "not a PNG");
        return 0;
    }
    size_t p = 8;
    int seen_ihdr = 0, seen_iend = 0;
    while (p + 12 <= length) {
        uint32_t chunk = ((uint32_t)data[p] << 24) | ((uint32_t)data[p + 1] << 16) |
                         ((uint32_t)data[p + 2] << 8) | data[p + 3];
        if (chunk > length - p - 12 || chunk > DISP_MAX_FILE) {
            snprintf(why, why_cap, "invalid chunk length");
            return 0;
        }
        const unsigned char *type = data + p + 4;
        const unsigned char *body = data + p + 8;
        uint32_t expected = ((uint32_t)data[p + 8 + chunk] << 24) | ((uint32_t)data[p + 9 + chunk] << 16) |
                            ((uint32_t)data[p + 10 + chunk] << 8) | data[p + 11 + chunk];
        uint32_t actual = cc2_crc32_update(0, type, 4);
        actual = cc2_crc32_update(actual, body, chunk);
        if (actual != expected) {
            snprintf(why, why_cap, "PNG chunk checksum mismatch");
            return 0;
        }
        if (!memcmp(type, "IHDR", 4)) {
            if (seen_ihdr || chunk != 13) { snprintf(why, why_cap, "invalid IHDR"); return 0; }
            uint32_t w = ((uint32_t)body[0] << 24) | ((uint32_t)body[1] << 16) |
                         ((uint32_t)body[2] << 8) | body[3];
            uint32_t h = ((uint32_t)body[4] << 24) | ((uint32_t)body[5] << 16) |
                         ((uint32_t)body[6] << 8) | body[7];
            if ((int)w != width || (int)h != height) { snprintf(why, why_cap, "PNG dimensions changed"); return 0; }
            if (body[8] != 8 || (body[9] != 2 && body[9] != 6) || body[10] || body[11] || body[12]) {
                snprintf(why, why_cap, "unsupported PNG format");
                return 0;
            }
            seen_ihdr = 1;
        }
        if (!memcmp(type, "IEND", 4)) seen_iend = 1;
        p += 12 + chunk;
    }
    if (!seen_ihdr || !seen_iend || p != length) {
        snprintf(why, why_cap, "incomplete PNG");
        return 0;
    }
    return 1;
}

static int disp_octal(const char *field, size_t length, size_t *out) {
    size_t value = 0;
    int seen = 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)field[i];
        if (c == 0 || c == ' ') continue;
        if (c < '0' || c > '7') return 0;
        if (value > (SIZE_MAX >> 3)) return 0;
        value = (value << 3) | (size_t)(c - '0');
        seen = 1;
    }
    *out = value;
    return seen;
}

static int disp_tar_header(const unsigned char *header, char *name, size_t name_cap, size_t *size) {
    int all_zero = 1;
    for (int i = 0; i < 512; ++i) if (header[i]) { all_zero = 0; break; }
    if (all_zero) return 0;
    unsigned long sum = 0, expected = 0;
    for (int i = 0; i < 512; ++i) sum += (i >= 148 && i < 156) ? ' ' : header[i];
    for (int i = 148; i < 156; ++i) {
        if (header[i] < '0' || header[i] > '7') { if (header[i] == 0 || header[i] == ' ') continue; return -1; }
        expected = (expected << 3) | (unsigned long)(header[i] - '0');
    }
    if (sum != expected) return -1;
    if (header[156] != '0' && header[156] != 0) return -1;
    if (header[157] || header[345]) return -1; /* link name or prefix must be empty */
    size_t n = strnlen((const char *)header, 100);
    if (n >= name_cap || n == 0) return -1;
    memcpy(name, header, n);
    name[n] = '\0';
    return disp_octal((const char *)header + 124, 12, size);
}

/* ---- HTTP ------------------------------------------------------------- */

static void disp_json(int fd, int code, const char *status, const char *message) {
    char body[256];
    int n = snprintf(body, sizeof(body), "{\"ok\":%s,\"error\":\"%s\"}\n",
                     code < 300 ? "true" : "false", message ? message : "");
    if (n > 0 && (size_t)n < sizeof(body))
        respond(fd, code, status, "application/json; charset=utf-8", body, (size_t)n);
}

static int disp_asset_count_customized(void) {
    int count = 0;
    for (int i = 0; i < disp_state_cache.file_count; ++i) {
        char target[PATH_MAX_LOCAL], hash[65];
        if (!disp_path_join(target, sizeof(target), disp_theme_dir, disp_state_cache.files[i])) continue;
        if (disp_sha_file(target, hash) && !strcasecmp(hash, disp_state_cache.hashes[i])) count++;
    }
    return count;
}

static void disp_status_response(int fd) {
    pthread_mutex_lock(&disp_mutex);
    int supported = disp_capability();
    int state_ok = disp_state_read();
    int reboot = 0;
    if (supported && disp_state_cache.phase == 1) {
        char marker[64], boot[40];
        disp_gui_marker(marker);
        disp_boot_id(boot);
        /* A different boot id proves the printer rebooted even in the unlikely
         * case that the GUI process got the same pid and start time again. */
        int boot_changed = disp_state_cache.boot_id[0] && boot[0] &&
                           strcmp(boot, disp_state_cache.boot_id);
        if (!marker[0]) {
            /* The native program is not running (or this host has none): the
             * applied files are on disk but loading them is unconfirmed. */
            reboot = 1;
        } else if (!strcmp(marker, disp_state_cache.gui_marker) && !boot_changed) {
            reboot = 1;
        } else {
            /* The marker or the boot id differs: the native program restarted
             * and has loaded the new files. */
            snprintf(disp_state_cache.gui_marker, sizeof(disp_state_cache.gui_marker), "%s", marker);
            snprintf(disp_state_cache.boot_id, sizeof(disp_state_cache.boot_id), "%s", boot);
            (void)disp_state_write();
        }
    }
    /* A verified baseline stays restorable even when the current manifest or
     * GUI no longer matches it, so the UI can offer recovery after an update. */
    disp_baseline_entry baseline[DISP_MAX_STAGE];
    int baseline_count = 0;
    disp_baseline_identity identity;
    int baseline_read = disp_baseline_read(baseline, &baseline_count, &identity, 0);
    int backup = baseline_read > 0 && baseline_count > 0;
    int firmware_changed = backup && !disp_manifest_identity(&disp_manifest_cache, identity.firmware_base,
                                                             identity.gui_sha256);
    char error[160];
    disp_copy_error(error, sizeof(error));
    char *body = malloc(32768);
    if (!body) { pthread_mutex_unlock(&disp_mutex); disp_json(fd, 500, "Internal Server Error", "Out of memory"); return; }
    json_builder b = {body, 0, 32768, 0};
    json_builder_printf(&b, "{\"supported\":%s,\"error\":", supported ? "true" : "false");
    json_builder_string(&b, error);
    json_builder_printf(&b, ",\"schema\":%d,\"firmwareBase\":", supported ? disp_manifest_cache.schema : 0);
    json_builder_string(&b, supported ? disp_manifest_cache.firmware_base : "");
    json_builder_printf(&b, ",\"themeDir\":");
    json_builder_string(&b, disp_theme_dir);
    json_builder_printf(&b, ",\"assets\":[");
    if (supported) {
        for (int i = 0; i < disp_manifest_cache.count; ++i) {
            const disp_asset *a = &disp_manifest_cache.assets[i];
            if (i) json_builder_printf(&b, ",");
            json_builder_printf(&b, "{\"name\":");
            json_builder_string(&b, a->name);
            json_builder_printf(&b, ",\"role\":");
            json_builder_string(&b, a->role);
            json_builder_printf(&b, ",\"width\":%d,\"height\":%d}", a->width, a->height);
        }
    }
    char journal[PATH_MAX_LOCAL];
    int recovery = disp_journal_path(journal, sizeof(journal)) && disp_regular(journal);
    json_builder_printf(&b, "],\"fileCount\":%d,\"customizedCount\":%d,\"rebootRequired\":%s,"
                            "\"phase\":%d,\"recoveryRequired\":%s,\"backupAvailable\":%s,"
                            "\"restoreAvailable\":%s,\"firmwareChanged\":%s,\"stateValid\":%s,"
                            "\"experimental\":true}\n",
                        supported ? disp_manifest_cache.count : 0,
                        state_ok ? disp_asset_count_customized() : 0,
                        reboot ? "true" : "false", disp_state_cache.phase,
                        recovery ? "true" : "false", backup ? "true" : "false",
                        backup ? "true" : "false", firmware_changed ? "true" : "false",
                        state_ok ? "true" : "false");
    pthread_mutex_unlock(&disp_mutex);
    if (b.failed) {
        free(body);
        disp_json(fd, 500, "Internal Server Error", "Display status too large");
        return;
    }
    respond(fd, 200, "OK", "application/json; charset=utf-8", body, b.length);
    free(body);
}
static void disp_asset_response(int fd, const char *query) {
    if (!query || strncmp(query, "name=", 5)) {
        disp_json(fd, 400, "Bad Request", "Missing asset name");
        return;
    }
    char name[DISP_NAME_MAX + 1];
    size_t n = 0;
    for (const char *p = query + 5; *p && *p != '&'; ++p) {
        if (n >= sizeof(name) - 1) { name[0] = '\0'; break; }
        int value = -1;
        if (*p == '%' && isxdigit((unsigned char)p[1]) && isxdigit((unsigned char)p[2])) {
            int hi = p[1] <= '9' ? p[1] - '0' : (tolower((unsigned char)p[1]) - 'a' + 10);
            int lo = p[2] <= '9' ? p[2] - '0' : (tolower((unsigned char)p[2]) - 'a' + 10);
            value = hi * 16 + lo;
            p += 2;
        } else value = (unsigned char)*p;
        if (value == '/' || value == '\\' || value < 32) { name[0] = '\0'; break; }
        name[n++] = (char)value;
    }
    name[n] = '\0';
    char path[PATH_MAX_LOCAL];
    pthread_mutex_lock(&disp_mutex);
    if (!disp_capability() || !disp_name_valid(name) || !disp_find_asset(name) ||
        !disp_path_join(path, sizeof(path), disp_theme_dir, name)) {
        pthread_mutex_unlock(&disp_mutex);
        disp_json(fd, 404, "Not Found", "Unknown display asset");
        return;
    }
    /* Serve the preserved original when one exists: the browser uses these
     * bytes to build the next pack, and tinting an already tinted background
     * twice would darken it. The current file is served only when no verified
     * baseline is available. */
    disp_baseline_entry baseline[DISP_MAX_STAGE];
    int baseline_count = 0;
    if (disp_baseline_read(baseline, &baseline_count, NULL, 0) > 0) {
        for (int i = 0; i < baseline_count; ++i) {
            if (strcmp(baseline[i].name, name) || !baseline[i].present) continue;
            char original[PATH_MAX_LOCAL], digest[65];
            if (disp_baseline_dir(original, sizeof(original)) &&
                disp_path_join(path, sizeof(path), original, name) &&
                disp_regular(path) && disp_sha_file(path, digest) &&
                !strcasecmp(digest, baseline[i].sha256)) {
                /* path now names the verified original */
            } else if (!disp_path_join(path, sizeof(path), disp_theme_dir, name)) {
                pthread_mutex_unlock(&disp_mutex);
                disp_json(fd, 500, "Internal Server Error", "Asset path too long");
                return;
            }
            break;
        }
    }
    pthread_mutex_unlock(&disp_mutex);
    struct stat st;
    if (!disp_regular(path) || stat(path, &st) != 0 || st.st_size <= 0 || st.st_size > DISP_MAX_FILE) {
        disp_json(fd, 404, "Not Found", "Display asset missing");
        return;
    }
    int in = open(path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (in < 0) {
        disp_json(fd, 404, "Not Found", "Display asset missing");
        return;
    }
    unsigned char *data = malloc((size_t)st.st_size);
    size_t got = 0;
    if (data) {
        while (got < (size_t)st.st_size) {
            ssize_t r = read(in, data + got, (size_t)st.st_size - got);
            if (r < 0 && errno == EINTR) continue;
            if (r <= 0) break;
            got += (size_t)r;
        }
    }
    close(in);
    if (!data || got != (size_t)st.st_size) {
        free(data);
        disp_json(fd, 500, "Internal Server Error", "Cannot read display asset");
        return;
    }
    respond(fd, 200, "OK", "image/png", (const char *)data, got);
    free(data);
}

/* ---- tar upload and apply --------------------------------------------- */

typedef struct {
    int fd;
    unsigned char *initial;
    size_t initial_length;
    size_t content_length;
    size_t received;
    struct timespec started;
    const mqtt_client *mqtt;
} disp_upload_job;

static ssize_t disp_recv(disp_upload_job *job, void *buffer, size_t length) {
    for (;;) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        long elapsed = now.tv_sec - job->started.tv_sec;
        if (elapsed > 300) { errno = ETIMEDOUT; return -1; }
        struct pollfd p = {job->fd, POLLIN, 0};
        int ready = poll(&p, 1, 30000);
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) { errno = ETIMEDOUT; return -1; }
        ssize_t n = recv(job->fd, buffer, length, MSG_DONTWAIT);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        return n;
    }
}

static int disp_read(disp_upload_job *job, unsigned char *buffer, size_t length) {
    size_t done = 0;
    if (job->initial_length) {
        size_t take = job->initial_length < length ? job->initial_length : length;
        memcpy(buffer, job->initial, take);
        job->initial += take;
        job->initial_length -= take;
        done = take;
    }
    while (done < length) {
        ssize_t n = disp_recv(job, buffer + done, length - done);
        if (n <= 0) return 0;
        done += (size_t)n;
    }
    job->received += length;
    return 1;
}

typedef struct {
    char name[DISP_NAME_MAX + 1];
    char sha256[65];
    size_t size;
} disp_staged;

static int disp_stage_write(const char *stage, const char *name, const unsigned char *data, size_t length) {
    char temporary[PATH_MAX_LOCAL];
    if (!disp_path_join(temporary, sizeof(temporary), stage, name)) return 0;
    int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) return 0;
    size_t done = 0;
    while (done < length) {
        ssize_t n = write(fd, data + done, length - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); unlink(temporary); return 0; }
        done += (size_t)n;
    }
    if (fsync(fd) != 0 || close(fd) != 0) { unlink(temporary); return 0; }
    return 1;
}

/* Validate and stage a tar stream. Returns 0 and sets disp_error on refusal.
 * Network transfer, staging and validation run without any display lock; only
 * the short state snapshots and the state update take it, so unrelated API
 * requests stay responsive while a pack uploads. */
static int disp_receive_pack(disp_upload_job *job) {
    char stage[PATH_MAX_LOCAL];
    if (!disp_ensure_state_dir() || !disp_stage_dir(stage, sizeof(stage))) return 0;
    if (mkdir(stage, 0700) != 0 && errno != EEXIST) { disp_set_error("display_stage_failed"); return 0; }
    DIR *old = opendir(stage);
    if (old) {
        struct dirent *entry;
        while ((entry = readdir(old))) {
            if (disp_name_valid(entry->d_name)) {
                char path[PATH_MAX_LOCAL];
                if (disp_path_join(path, sizeof(path), stage, entry->d_name)) unlink(path);
            }
        }
        closedir(old);
    }
    disp_staged *staged = calloc(DISP_MAX_STAGE, sizeof(*staged));
    if (!staged) { disp_set_error("display_pack_oom"); return 0; }
    int staged_count = 0;
    unsigned char header[512];
    int result = 0;
    char journal[PATH_MAX_LOCAL];
    disp_state local_state;
    local_state.file_count = 0;
    while (job->received < job->content_length) {
        if (!disp_read(job, header, 512)) { disp_set_error("display_pack_truncated"); goto out; }
        char name[DISP_NAME_MAX + 1];
        size_t size = 0;
        int parsed = disp_tar_header(header, name, sizeof(name), &size);
        if (parsed == 0) {
            /* End-of-archive marker: the remaining bytes must be zero padding. */
            size_t left = job->content_length - job->received;
            while (left) {
                unsigned char zero[512];
                size_t take = left < sizeof(zero) ? left : sizeof(zero);
                if (!disp_read(job, zero, take)) { disp_set_error("display_pack_truncated"); goto out; }
                for (size_t z = 0; z < take; ++z)
                    if (zero[z]) { disp_set_error("display_pack_trailing_data"); goto out; }
                left -= take;
            }
            break;
        }
        if (parsed < 0 || !disp_name_valid(name) || size == 0 || size > DISP_MAX_FILE) {
            disp_set_error("display_pack_invalid_header");
            goto out;
        }
        pthread_mutex_lock(&disp_mutex);
        const disp_asset *asset = disp_find_asset(name);
        int allowed = asset != NULL;
        int asset_width = allowed ? asset->width : 0;
        int asset_height = allowed ? asset->height : 0;
        pthread_mutex_unlock(&disp_mutex);
        if (!allowed) { disp_set_error("display_asset_not_allowed"); goto out; }
        if (staged_count >= DISP_MAX_STAGE) { disp_set_error("display_pack_too_many_files"); goto out; }
        for (int i = 0; i < staged_count; ++i)
            if (!strcmp(staged[i].name, name)) { disp_set_error("display_pack_duplicate"); goto out; }
        unsigned char *data = malloc(size);
        if (!data) { disp_set_error("display_pack_oom"); goto out; }
        if (!disp_read(job, data, size)) { free(data); disp_set_error("display_pack_truncated"); goto out; }
        size_t padding = (512 - (size % 512)) % 512;
        if (padding) {
            unsigned char pad[512];
            if (!disp_read(job, pad, padding)) { free(data); disp_set_error("display_pack_truncated"); goto out; }
        }
        char why[64] = "";
        if (!disp_png_valid(data, size, asset_width, asset_height, why, sizeof(why))) {
            free(data);
            {
                char message[160];
                snprintf(message, sizeof(message), "display_png_invalid:%s", why);
                disp_set_error(message);
            }
            goto out;
        }
        if (!disp_stage_write(stage, name, data, size)) {
            free(data);
            disp_set_error("display_stage_write_failed");
            goto out;
        }
        snprintf(staged[staged_count].name, sizeof(staged[0].name), "%s", name);
        disp_sha_bytes(data, size, staged[staged_count].sha256);
        staged[staged_count].size = size;
        staged_count++;
        free(data);
    }
    if (!staged_count) { disp_set_error("display_pack_empty"); goto out; }
    if (job->received != job->content_length) { disp_set_error("display_pack_trailing_data"); goto out; }
    /* The upload can take minutes: confirm the printer is still idle, read the
     * state once, and confirm the manifest is still the one the client saw. */
    if (!job->mqtt || !gcode_idle_for_mutation(job->mqtt)) {
        disp_set_error("display_not_idle");
        goto out;
    }
    if (app_update_active() || maintenance_barrier_active()) {
        disp_set_error("display_busy");
        goto out;
    }
    pthread_mutex_lock(&disp_apply_mutex);
    pthread_mutex_lock(&disp_mutex);
    int state_ok = disp_state_read();
    if (state_ok) local_state = disp_state_cache;
    int supported = disp_capability();
    disp_manifest *manifest = supported ? malloc(sizeof(*manifest)) : NULL;
    if (manifest) *manifest = disp_manifest_cache;
    pthread_mutex_unlock(&disp_mutex);
    if (!supported || !manifest) {
        free(manifest);
        pthread_mutex_unlock(&disp_apply_mutex);
        goto out;
    }

    /* A baseline taken from another firmware/GUI must never be extended with
     * a new identity: restore it first, then customise the new display. */
    {
        disp_baseline_entry existing[DISP_MAX_STAGE];
        int existing_count = 0;
        disp_baseline_identity existing_identity;
        int existing_read = disp_baseline_read(existing, &existing_count, &existing_identity, 1);
        if (existing_read < 0) {
            free(manifest);
            pthread_mutex_unlock(&disp_apply_mutex);
            goto out;
        }
    }
    /* Every staged asset must have a verified original before any write. */
    for (int i = 0; i < staged_count; ++i) {
        const disp_asset *asset = disp_find_asset_in(manifest, staged[i].name);
        if (!asset || !disp_backup_original(asset)) {
            free(manifest);
            pthread_mutex_unlock(&disp_apply_mutex);
            goto out;
        }
    }
    if (!disp_baseline_write(manifest)) {
        disp_set_error("display_backup_manifest_failed");
        free(manifest);
        pthread_mutex_unlock(&disp_apply_mutex);
        goto out;
    }
    free(manifest);
    manifest = NULL;
    /* Journal: a power loss from here on is recovered at the next start. */
    if (!disp_journal_path(journal, sizeof(journal))) { pthread_mutex_unlock(&disp_apply_mutex); goto out; }
    json_builder jb;
    char *journal_body = malloc(DISP_META_MAX);
    if (!journal_body) { pthread_mutex_unlock(&disp_apply_mutex); goto out; }
    jb = (json_builder){journal_body, 0, DISP_META_MAX, 0};
    json_builder_printf(&jb, "{\"version\":1,\"phase\":\"applying\",\"stage\":");
    json_builder_string(&jb, stage);
    json_builder_printf(&jb, ",\"files\":[");
    for (int i = 0; i < staged_count; ++i) {
        if (i) json_builder_printf(&jb, ",");
        json_builder_printf(&jb, "{\"name\":");
        json_builder_string(&jb, staged[i].name);
        json_builder_printf(&jb, ",\"sha256\":");
        json_builder_string(&jb, staged[i].sha256);
        json_builder_printf(&jb, "}");
    }
    json_builder_printf(&jb, "]}\n");
    int ok = !jb.failed && ap_write_all(journal, journal_body, jb.length, 0600);
    free(journal_body);
    if (!ok) { pthread_mutex_unlock(&disp_apply_mutex); goto out; }
    /* Merge the new applied hashes into the local state before writing the
     * files, so a failed apply still knows which files are ours. */
    for (int i = 0; i < staged_count; ++i) {
        int slot = -1;
        for (int p = 0; p < local_state.file_count; ++p)
            if (!strcmp(local_state.files[p], staged[i].name)) slot = p;
        if (slot < 0) {
            if (local_state.file_count >= DISP_MAX_STAGE) continue;
            slot = local_state.file_count++;
        }
        snprintf(local_state.files[slot], sizeof(local_state.files[slot]), "%.64s", staged[i].name);
        snprintf(local_state.hashes[slot], sizeof(local_state.hashes[slot]), "%.64s", staged[i].sha256);
    }
    int applied = 0;
    for (int i = 0; i < staged_count; ++i) {
        char source[PATH_MAX_LOCAL], target[PATH_MAX_LOCAL], hash[65];
        if (!disp_path_join(source, sizeof(source), stage, staged[i].name) ||
            !disp_path_join(target, sizeof(target), disp_theme_dir, staged[i].name)) {
            disp_set_error("display_apply_path");
            ok = 0;
            break;
        }
        if (!ap_copy_verified(source, target, 0644)) {
            disp_set_error("display_apply_write_failed");
            ok = 0;
            break;
        }
        if (!disp_sha_file(target, hash) || strcasecmp(hash, staged[i].sha256)) {
            disp_set_error("display_apply_readback_failed");
            ok = 0;
            break;
        }
        applied++;
    }
    sync();
    if (ok) {
        char boot[40], marker[64];
        disp_boot_id(boot);
        disp_gui_marker(marker);
        pthread_mutex_lock(&disp_mutex);
        disp_state_cache = local_state;
        snprintf(disp_state_cache.boot_id, sizeof(disp_state_cache.boot_id), "%s", boot);
        snprintf(disp_state_cache.gui_marker, sizeof(disp_state_cache.gui_marker), "%s", marker);
        disp_state_cache.phase = 1;
        disp_state_cache.applied_at = (long long)time(NULL);
        ok = disp_state_write();
        pthread_mutex_unlock(&disp_mutex);
    }
    if (!ok) {
        /* Roll the already replaced files back from the baseline. */
        (void)disp_restore_baseline(0, &local_state, NULL);
        pthread_mutex_lock(&disp_mutex);
        disp_state_cache = local_state;
        disp_state_cache.phase = 2;
        (void)disp_state_write();
        pthread_mutex_unlock(&disp_mutex);
    } else {
        unlink(journal);
        ap_sync_parent(journal);
        for (int i = 0; i < staged_count; ++i) {
            char path[PATH_MAX_LOCAL];
            if (disp_path_join(path, sizeof(path), stage, staged[i].name)) unlink(path);
        }
        rmdir(stage);
    }
    pthread_mutex_unlock(&disp_apply_mutex);
    result = ok && applied == staged_count;
out:
    if (!result) {
        for (int i = 0; i < staged_count; ++i) {
            char path[PATH_MAX_LOCAL];
            if (disp_path_join(path, sizeof(path), stage, staged[i].name)) unlink(path);
        }
        rmdir(stage);
    }
    free(staged);
    return result;
}

static void *disp_upload_thread(void *argument) {
    disp_upload_job *job = argument;
    int ok = disp_receive_pack(job);
    char message[160];
    disp_copy_error(message, sizeof(message));
    char body[320];
    int n = snprintf(body, sizeof(body),
        "{\"ok\":%s,\"error\":\"%s\",\"rebootRequired\":%s}\n",
        ok ? "true" : "false", message, ok ? "true" : "false");
    if (n > 0 && (size_t)n < sizeof(body))
        respond(job->fd, ok ? 200 : 409, ok ? "OK" : "Conflict",
                "application/json; charset=utf-8", body, (size_t)n);
    close(job->fd);
    free(job->initial);
    free(job);
    upload_slot_release();
    return NULL;
}

static int disp_apply_start(int fd, const char *request, size_t used, size_t header_length, const mqtt_client *mqtt) {
    pthread_mutex_lock(&disp_mutex);
    int supported = disp_capability();
    char error[160];
    disp_copy_error(error, sizeof(error));
    pthread_mutex_unlock(&disp_mutex);
    if (!supported) {
        char body[256];
        int n = snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%.160s\"}\n", error);
        if (n > 0 && (size_t)n < sizeof(body))
            respond(fd, 409, "Conflict", "application/json; charset=utf-8", body, (size_t)n);
        return 0;
    }
    if (!mqtt || !gcode_idle_for_mutation(mqtt)) {
        disp_json(fd, 409, "Conflict", "Printer must be idle");
        return 0;
    }
    if (app_update_active() || maintenance_barrier_active()) {
        disp_json(fd, 409, "Conflict", "Another maintenance operation is running");
        return 0;
    }
    /* An interrupted apply is recovered before a new one may start. */
    char journal[PATH_MAX_LOCAL];
    if (disp_ensure_state_dir() && disp_journal_path(journal, sizeof(journal)) && disp_regular(journal)) {
        disp_json(fd, 409, "Conflict", "Display recovery is required");
        return 0;
    }
    size_t length = content_length_from_headers(request);
    if (length < 512 || length > DISP_MAX_PACK) {
        disp_json(fd, 400, "Bad Request", "Invalid display pack size");
        return 0;
    }
    size_t body_offset = header_length;
    if (body_offset > used) body_offset = used;
    size_t initial = used - body_offset;
    if (!upload_slot_acquire()) {
        disp_json(fd, 409, "Conflict", "Another upload is running");
        return 0;
    }
    disp_upload_job *job = calloc(1, sizeof(*job));
    if (!job) { upload_slot_release(); disp_json(fd, 503, "Service Unavailable", "Out of memory"); return 0; }
    if (initial) {
        job->initial = malloc(initial);
        if (!job->initial) { free(job); upload_slot_release(); disp_json(fd, 503, "Service Unavailable", "Out of memory"); return 0; }
        memcpy(job->initial, request + body_offset, initial);
    }
    job->initial_length = initial;
    job->content_length = length;
    job->received = 0;
    job->fd = fd;
    job->mqtt = mqtt;
    clock_gettime(CLOCK_MONOTONIC, &job->started);
    disp_clear_error();
    pthread_t thread;
    if (pthread_create(&thread, NULL, disp_upload_thread, job)) {
        free(job->initial);
        free(job);
        upload_slot_release();
        disp_json(fd, 503, "Service Unavailable", "Cannot start display upload");
        return 0;
    }
    pthread_detach(thread);
    return 1;
}

/* Restore the verified baseline. This path deliberately does not require the
 * current display manifest or GUI hash: after a firmware or panel update the
 * baseline must still be reachable. It does require that the target files are
 * the original or a version this feature recorded, so a newer firmware's own
 * files are never overwritten. */
static void disp_restore_response(int fd, const mqtt_client *mqtt) {
    if (!mqtt || !gcode_idle_for_mutation(mqtt)) {
        disp_json(fd, 409, "Conflict", "Printer must be idle");
        return;
    }
    pthread_mutex_lock(&disp_apply_mutex);
    disp_baseline_entry entries[DISP_MAX_STAGE];
    int count = 0;
    disp_baseline_identity identity;
    disp_state local_state;
    memset(&local_state, 0, sizeof(local_state));
    pthread_mutex_lock(&disp_mutex);
    int state_ok = disp_state_read();
    if (state_ok) local_state = disp_state_cache;
    struct stat dir_stat;
    int dir_ok = lstat(disp_theme_dir, &dir_stat) == 0 && S_ISDIR(dir_stat.st_mode);
    int baseline_ok = dir_ok && disp_baseline_read(entries, &count, &identity, 0) > 0 && count > 0;
    int supported = disp_capability(); /* only for the warning below */
    int firmware_changed = baseline_ok && supported &&
        !disp_manifest_identity(&disp_manifest_cache, identity.firmware_base, identity.gui_sha256);
    char error[160];
    disp_copy_error(error, sizeof(error));
    pthread_mutex_unlock(&disp_mutex);
    if (!dir_ok) {
        pthread_mutex_unlock(&disp_apply_mutex);
        disp_json(fd, 409, "Conflict", "display_theme_missing");
        return;
    }
    if (!baseline_ok) {
        pthread_mutex_unlock(&disp_apply_mutex);
        disp_json(fd, 409, "Conflict", error[0] ? error : "display_backup_missing");
        return;
    }
    disp_hash_set journal;
    disp_journal_read(&journal);
    int ok = disp_restore_baseline(1, &local_state, &journal);
    if (ok) {
        char path[PATH_MAX_LOCAL];
        if (disp_journal_path(path, sizeof(path))) {
            unlink(path);
            ap_sync_parent(path);
        }
        pthread_mutex_lock(&disp_mutex);
        disp_state_cache.phase = 0;
        disp_state_cache.file_count = 0;
        disp_state_cache.gui_marker[0] = '\0';
        disp_boot_id(disp_state_cache.boot_id);
        ok = disp_state_write();
        pthread_mutex_unlock(&disp_mutex);
    }
    disp_copy_error(error, sizeof(error));
    pthread_mutex_unlock(&disp_apply_mutex);
    if (!ok) {
        char body[320];
        int n = snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%.160s\"}\n", error);
        if (n > 0 && (size_t)n < sizeof(body))
            respond(fd, 409, "Conflict", "application/json; charset=utf-8", body, (size_t)n);
        return;
    }
    char body[256];
    int n = snprintf(body, sizeof(body),
                     "{\"ok\":true,\"rebootRequired\":true,\"firmwareChanged\":%s}\n",
                     firmware_changed ? "true" : "false");
    if (n > 0 && (size_t)n < sizeof(body))
        respond(fd, 200, "OK", "application/json; charset=utf-8", body, (size_t)n);
}

/* Startup recovery: an interrupted apply is rolled back before the web server
 * serves the first request. It relies on the baseline and the journal, never
 * on the current manifest, so a recovery works after a firmware update. The
 * strict target check keeps files written by another tool. Idempotent: a
 * repeated run restores the same bytes. No printer process is signalled. */
static void disp_startup_recover(void) {
    if (!disp_ensure_state_dir()) return;
    char journal[PATH_MAX_LOCAL], stage[PATH_MAX_LOCAL];
    if (!disp_journal_path(journal, sizeof(journal))) return;
    (void)disp_stage_dir(stage, sizeof(stage));
    if (!disp_regular(journal)) {
        if (stage[0]) rmdir(stage); /* empty leftovers only */
        return;
    }
    pthread_mutex_lock(&disp_apply_mutex);
    pthread_mutex_lock(&disp_mutex);
    int state_ok = disp_state_read();
    disp_state local_state;
    memset(&local_state, 0, sizeof(local_state));
    if (state_ok) local_state = disp_state_cache;
    pthread_mutex_unlock(&disp_mutex);
    disp_hash_set recorded;
    int have_journal = disp_journal_read(&recorded);
    if (!have_journal) {
        char error[160];
        disp_copy_error(error, sizeof(error));
        fprintf(stderr, "Display recovery: journal unreadable (%s); journal retained\n", error);
        pthread_mutex_unlock(&disp_apply_mutex);
        return;
    }
    if (disp_restore_baseline(1, &local_state, &recorded)) {
        pthread_mutex_lock(&disp_mutex);
        disp_state_cache.phase = 3;
        (void)disp_state_write();
        pthread_mutex_unlock(&disp_mutex);
        unlink(journal);
        ap_sync_parent(journal);
        if (stage[0]) rmdir(stage); /* empty leftovers only */
        fprintf(stderr, "Display recovery: previous theme files restored\n");
    } else {
        char error[160];
        disp_copy_error(error, sizeof(error));
        fprintf(stderr, "Display recovery failed (%s); keep the backup and inspect the display files\n", error);
    }
    pthread_mutex_unlock(&disp_apply_mutex);
}

#endif /* CC2_DISPLAY_THEME_H */
