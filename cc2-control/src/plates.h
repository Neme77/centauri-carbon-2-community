/* Build-plate library: a named bed mesh and Z offset for each plate surface.
 *
 * Every print loads the mesh slot of its side (G180 S7): Side A prints with
 * profile `default`, Side B with `default1`. The firmware refuses
 * BED_MESH_PROFILE SAVE=default and its in-process RESTART hangs, so mounting a
 * plate whose mesh is not already in its slot rewrites that one [bed_mesh]
 * section of autosave.cfg and reboots the printer. The next CC2 Control process
 * checks the slot both in the file and in printer memory.
 *
 * A plate's Z offset is the user part of the G-code offset (SET_GCODE_OFFSET
 * Z=). The stock print start keeps it when it adds the side's bed-roughness
 * offset, but a printer restart clears it, so it is applied again once the
 * printer is idle after the printer service restarts. A reconnect alone, such as
 * after a receive timeout during a busy print start, keeps it. The touchscreen's
 * Z offset control counts from its own zero and sends absolute values, so a
 * press there replaces the plate's value. Everything here runs in the main loop. */
#define PLATES_MAX 16
#define PLATE_ID_LEN 16
#define PLATE_NAME_MAX 64
#define PLATE_GRID_MAX 15
#define PLATE_Z_LIMIT 1.0
#define PLATES_FILE_MAX (96 * 1024)
#define AUTOSAVE_FILE_MAX (256 * 1024)
#define AUTOSAVE_MARKER "#*# <---------------------- SAVE_CONFIG ---------------------->"

typedef struct {
    int x_count, y_count, x_pps, y_pps;
    double min_x, max_x, min_y, max_y, tension, offset;
    char algo[16];
    double points[PLATE_GRID_MAX * PLATE_GRID_MAX]; /* y_count rows of x_count values */
} plate_mesh;

typedef struct {
    char id[PLATE_ID_LEN + 1];
    char name[PLATE_NAME_MAX + 1];
    char side; /* 'A' or 'B' */
    double z;
    long long measured;
    plate_mesh mesh;
} plate_entry;

typedef struct {
    int count;
    char current[PLATE_ID_LEN + 1];
    char pending[PLATE_ID_LEN + 1]; /* mesh written, waiting for the printer restart */
    plate_entry plates[PLATES_MAX];
} plate_store;

static plate_store plates;
static int plates_available;
static const char *plates_error = "Plate library not loaded";
/* Last mount outcome shown by the page: "", "rebooting", "mounted", "verify_failed" or "reboot_failed". */
static const char *plates_result = "";
static int plates_reboot_requested;
static const mqtt_client *plates_mqtt; /* main()'s client, read by the reboot guard */
static char plates_previous_current[PLATE_ID_LEN + 1];
static unsigned long plates_seen_connections = ULONG_MAX;
static int plates_z_valid;
static double plates_z_applied;
static struct stat plates_z_service; /* the printer service's socket when the offset was applied */
static int plates_z_service_known;
static long long plates_next_tick_ms;
/* Tests replace the printer round trip. */
static int (*plates_uds)(const char *query, char **reply, size_t *length) = uds_query_json;

static const char *plate_slot(char side) { return side == 'B' ? "default1" : "default"; }

static plate_entry *plate_find(const char *id) {
    for (int i = 0; i < plates.count; ++i)
        if (!strcmp(plates.plates[i].id, id)) return &plates.plates[i];
    return NULL;
}

static int plate_id_valid(const char *id) {
    if (strlen(id) != PLATE_ID_LEN) return 0;
    for (const char *p = id; *p; ++p)
        if (!isdigit((unsigned char)*p) && (*p < 'a' || *p > 'f')) return 0;
    return 1;
}

/* Printable UTF-8 without quotes, backslashes, controls or line separators, so
 * a name never needs escaping where it is stored or shown. */
static int plate_name_valid(const char *name) {
    size_t n = strlen(name);
    if (!n || n > PLATE_NAME_MAX || name[0] == ' ' || name[n - 1] == ' ') return 0;
    for (size_t i = 0; i < n;) {
        unsigned char ch = (unsigned char)name[i++];
        if (ch < 128) {
            if (ch < 32 || ch == 127 || ch == '"' || ch == '\\') return 0;
            continue;
        }
        unsigned int value; size_t extra;
        if (ch >= 0xc2 && ch <= 0xdf) { value = ch & 31; extra = 1; }
        else if (ch >= 0xe0 && ch <= 0xef) { value = ch & 15; extra = 2; }
        else if (ch >= 0xf0 && ch <= 0xf4) { value = ch & 7; extra = 3; }
        else return 0;
        if (extra > n - i) return 0;
        for (size_t k = 0; k < extra; ++k) {
            unsigned char next = (unsigned char)name[i++];
            if ((next & 0xc0) != 0x80) return 0;
            value = (value << 6) | (next & 63);
        }
        if ((extra == 2 && value < 0x800) || (extra == 3 && value < 0x10000) || value > 0x10ffff ||
            (value >= 0xd800 && value <= 0xdfff) || (value >= 0x80 && value <= 0x9f) ||
            value == 0x2028 || value == 0x2029) return 0;
    }
    return 1;
}

/* Plain decimal millimetres such as "-0.020", within the plate limit, kept to 3 decimals. */
static int plate_z_parse(const char *text, double *z) {
    size_t n = strlen(text);
    if (!n || n > 8 || strspn(text, "0123456789.-+") != n) return 0;
    char *end; errno = 0;
    double value = strtod(text, &end);
    if (end == text || *end || errno || !isfinite(value) || fabs(value) > PLATE_Z_LIMIT + 1e-9) return 0;
    *z = round(value * 1000.0) / 1000.0;
    if (*z == 0.0) *z = 0.0; /* never "-0.000" */
    return 1;
}

static int plate_mesh_valid(const plate_mesh *m) {
    if (m->x_count < 3 || m->x_count > PLATE_GRID_MAX || m->y_count < 3 || m->y_count > PLATE_GRID_MAX) return 0;
    if (m->x_pps < 0 || m->x_pps > 10 || m->y_pps < 0 || m->y_pps > 10) return 0;
    if (strcmp(m->algo, "bicubic") && strcmp(m->algo, "lagrange")) return 0;
    const double values[] = {m->min_x, m->max_x, m->min_y, m->max_y, m->tension, m->offset};
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i)
        if (!isfinite(values[i])) return 0;
    if (m->min_x < -50 || m->max_x > 400 || m->min_y < -50 || m->max_y > 400 || m->min_x >= m->max_x ||
        m->min_y >= m->max_y || m->tension < 0 || m->tension > 2 || fabs(m->offset) > 10) return 0;
    for (int i = 0; i < m->x_count * m->y_count; ++i)
        if (!isfinite(m->points[i]) || fabs(m->points[i]) > 10) return 0;
    return 1;
}

/* Values are compared at the 6 decimals the firmware stores. */
static int plate_close(double a, double b) { return fabs(a - b) < 5e-7; }
static int plate_mesh_equal(const plate_mesh *a, const plate_mesh *b, int with_offset) {
    if (a->x_count != b->x_count || a->y_count != b->y_count || a->x_pps != b->x_pps || a->y_pps != b->y_pps ||
        strcmp(a->algo, b->algo) || !plate_close(a->min_x, b->min_x) || !plate_close(a->max_x, b->max_x) ||
        !plate_close(a->min_y, b->min_y) || !plate_close(a->max_y, b->max_y) ||
        !plate_close(a->tension, b->tension) || (with_offset && !plate_close(a->offset, b->offset))) return 0;
    for (int i = 0; i < a->x_count * a->y_count; ++i)
        if (!plate_close(a->points[i], b->points[i])) return 0;
    return 1;
}

/* ---- files ----------------------------------------------------------------- */

/* Writes and syncs a new file; the caller renames it into place. */
static int plates_write_new(const char *path, const char *data, size_t length, mode_t mode) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return -1;
    int failed = 0;
    for (size_t done = 0; !failed && done < length;) {
        ssize_t n = write(fd, data + done, length - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) failed = 1; else done += (size_t)n;
    }
    if (!failed && (fchmod(fd, mode) != 0 || fsync(fd) != 0)) failed = 1;
    if (close(fd) != 0) failed = 1;
    if (failed) unlink(path);
    return failed ? -1 : 0;
}

static void plates_sync_directory(const char *path) {
    char directory[PATH_MAX_LOCAL];
    snprintf(directory, sizeof(directory), "%s", path);
    char *slash = strrchr(directory, '/');
    if (!slash) snprintf(directory, sizeof(directory), ".");
    else if (slash == directory) slash[1] = 0;
    else *slash = 0;
    int fd = open(directory, O_RDONLY);
    if (fd >= 0) { (void)fsync(fd); close(fd); }
}

/* Atomic replacement; with `backup`, the old file is kept under that name the way
 * the firmware's own SAVE_CONFIG keeps autosave_backup.cfg. */
static char *plates_read_file(const char *path,size_t limit,size_t *length);
static int plates_replace_file(const char *path, const char *backup, const char *data, size_t length, mode_t mode) {
    char temporary[PATH_MAX_LOCAL];
    if (snprintf(temporary, sizeof(temporary), "%s.cc2-new", path) >= (int)sizeof(temporary) ||
        plates_write_new(temporary, data, length, mode) != 0) return -1;
    if (backup) {
        size_t old_length=0;char *old=plates_read_file(path,AUTOSAVE_FILE_MAX,&old_length);
        char backup_new[PATH_MAX_LOCAL]={0};
        int ok=old && snprintf(backup_new,sizeof(backup_new),"%s.cc2-new",backup)<(int)sizeof(backup_new) &&
            plates_write_new(backup_new,old,old_length,mode)==0 && rename(backup_new,backup)==0;
        free(old);
        if(!ok){if(backup_new[0])unlink(backup_new);unlink(temporary);return -1;}
        plates_sync_directory(backup);
    }
    if (rename(temporary, path) != 0) {
        unlink(temporary); return -1;
    }
    plates_sync_directory(path);
    return 0;
}

static char *plates_read_file(const char *path, size_t limit, size_t *length) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    char *text = malloc(limit + 1);
    size_t used = text ? fread(text, 1, limit + 1, file) : 0;
    int failed = !text || ferror(file) || used > limit;
    fclose(file);
    if (failed) { free(text); errno = EIO; return NULL; }
    text[used] = 0; *length = used;
    return text;
}

/* ---- autosave.cfg ------------------------------------------------------------ */

/* The firmware's own layout: one "#*# key = value" line per key, points on one line. */
static int autosave_format(char *out, size_t cap, const char *slot, const plate_mesh *m) {
    json_builder b = {out, 0, cap, 0}; /* a plain bounded printf appender */
    json_builder_printf(&b, "#*# [bed_mesh %s]\n#*# version = 1\n#*# points = ", slot);
    for (int i = 0; i < m->x_count * m->y_count; ++i) json_builder_printf(&b, "%s%.6f", i ? ", " : "", m->points[i]);
    json_builder_printf(&b, "\n#*# offset = %.6f\n#*# algo = %s\n#*# max_x = %.6f\n#*# max_y = %.6f\n"
                        "#*# mesh_x_pps = %d\n#*# mesh_y_pps = %d\n#*# min_x = %.6f\n#*# min_y = %.6f\n"
                        "#*# tension = %.6f\n#*# x_count = %d\n#*# y_count = %d\n",
                        m->offset, m->algo, m->max_x, m->max_y, m->x_pps, m->y_pps, m->min_x, m->min_y,
                        m->tension, m->x_count, m->y_count);
    return b.failed ? -1 : (int)b.length;
}

static size_t autosave_line_end(const char *text, size_t length, size_t pos) {
    const char *eol = memchr(text + pos, '\n', length - pos);
    return eol ? (size_t)(eol - text) : length;
}

/* [start,end) holds the header and key lines of "[bed_mesh <slot>]", without the
 * "#*#" separators. 1 found, 0 absent, -1 not a SAVE_CONFIG block this code understands. */
static int autosave_section(const char *text, size_t length, const char *slot, size_t *start, size_t *end) {
    if (memchr(text, '\r', length) || memchr(text, '\0', length)) return -1;
    const char *marker = strstr(text, AUTOSAVE_MARKER);
    if (!marker) return -1;
    char header[64]; snprintf(header, sizeof(header), "#*# [bed_mesh %s]", slot);
    size_t header_len = strlen(header); int found = 0;
    for (size_t pos = (size_t)(marker - text); pos < length;) {
        size_t eol = autosave_line_end(text, length, pos);
        if (eol - pos == header_len && !memcmp(text + pos, header, header_len)) {
            if (found) return -1; /* a duplicate section is not ours to resolve */
            found = 1; *start = pos;
            size_t line = eol < length ? eol + 1 : length;
            while (line < length) {
                size_t line_eol = autosave_line_end(text, length, line);
                if (line_eol - line < 5 || memcmp(text + line, "#*# ", 4) || text[line + 4] == '[') break;
                line = line_eol < length ? line_eol + 1 : length;
            }
            *end = line;
        }
        pos = eol < length ? eol + 1 : length;
    }
    return found;
}

static int autosave_number(const char *text, size_t length, double *out) {
    char buffer[40];
    if (!length || length >= sizeof(buffer)) return 0;
    memcpy(buffer, text, length); buffer[length] = 0;
    char *end; errno = 0;
    double value = strtod(buffer, &end);
    if (end == buffer || *end || errno || !isfinite(value)) return 0;
    *out = value; return 1;
}

static int autosave_integer(const char *text, size_t length, int *out) {
    double value;
    if (!autosave_number(text, length, &value) || value != floor(value) || fabs(value) > 1000) return 0;
    *out = (int)value; return 1;
}

/* Parses a section; *unknown counts keys that a rewrite would drop. */
static int autosave_mesh(const char *text, size_t start, size_t end, plate_mesh *m, int *unknown) {
    memset(m, 0, sizeof(*m)); *unknown = 0;
    int seen = 0, points = -1;
    for (size_t pos = autosave_line_end(text, end, start) + 1; pos < end;) {
        size_t eol = autosave_line_end(text, end, pos);
        const char *line = text + pos + 4, *equals = memchr(line, '=', eol - pos - 4);
        if (!equals || equals == line || equals[-1] != ' ' || equals + 2 > text + eol || equals[1] != ' ') return 0;
        size_t key_len = (size_t)(equals - 1 - line), value_len = (size_t)(text + eol - (equals + 2));
        const char *value = equals + 2;
        int ok = 1, bit = 0;
#define KEY(k) (key_len == sizeof(k) - 1 && !memcmp(line, k, key_len))
        if (KEY("version")) { int version = 0; ok = autosave_integer(value, value_len, &version) && version == 1; bit = 1; }
        else if (KEY("points")) {
            points = 0; bit = 2;
            for (const char *p = value, *stop = value + value_len; ok && p < stop;) {
                const char *comma = memchr(p, ',', (size_t)(stop - p)), *item_end = comma ? comma : stop;
                while (p < item_end && *p == ' ') p++;
                ok = points < PLATE_GRID_MAX * PLATE_GRID_MAX &&
                     autosave_number(p, (size_t)(item_end - p), &m->points[points]);
                points++;
                p = comma ? comma + 1 : stop;
                if (comma && p == stop) ok = 0;
            }
            ok = ok && points > 0;
        }
        else if (KEY("offset")) { ok = autosave_number(value, value_len, &m->offset); bit = 4; }
        else if (KEY("algo")) {
            ok = value_len > 0 && value_len < sizeof(m->algo); bit = 8;
            if (ok) { memcpy(m->algo, value, value_len); m->algo[value_len] = 0; }
        }
        else if (KEY("max_x")) { ok = autosave_number(value, value_len, &m->max_x); bit = 16; }
        else if (KEY("max_y")) { ok = autosave_number(value, value_len, &m->max_y); bit = 32; }
        else if (KEY("mesh_x_pps")) { ok = autosave_integer(value, value_len, &m->x_pps); bit = 64; }
        else if (KEY("mesh_y_pps")) { ok = autosave_integer(value, value_len, &m->y_pps); bit = 128; }
        else if (KEY("min_x")) { ok = autosave_number(value, value_len, &m->min_x); bit = 256; }
        else if (KEY("min_y")) { ok = autosave_number(value, value_len, &m->min_y); bit = 512; }
        else if (KEY("tension")) { ok = autosave_number(value, value_len, &m->tension); bit = 1024; }
        else if (KEY("x_count")) { ok = autosave_integer(value, value_len, &m->x_count); bit = 2048; }
        else if (KEY("y_count")) { ok = autosave_integer(value, value_len, &m->y_count); bit = 4096; }
        else ++*unknown;
#undef KEY
        if (!ok || (seen & bit)) return 0;
        seen |= bit;
        pos = eol + 1;
    }
    /* Every key but offset (4) is required. */
    return (seen | 4) == 8191 && points == m->x_count * m->y_count && plate_mesh_valid(m);
}

/* 1 slot read, 0 slot absent, -1 file missing, unreadable or in an unknown format. */
static int autosave_slot(char side, plate_mesh *mesh) {
    size_t length, start, end; int unknown;
    char *text = plates_read_file(printer_autosave_path, AUTOSAVE_FILE_MAX, &length);
    if (!text) return -1;
    int found = autosave_section(text, length, plate_slot(side), &start, &end);
    if (found == 1 && (!autosave_mesh(text, start, end, mesh, &unknown) || unknown)) found = -1;
    free(text);
    return found;
}

/* A copy of the file with the slot replaced, or appended after the last section,
 * checked by parsing it back. Every other byte stays as it was. */
static char *autosave_with_mesh(const char *text, size_t length, const char *slot, const plate_mesh *m,
                                size_t *out_length) {
    static const char separator[] = "\n#*#\n#*#\n";
    char section[4096]; size_t start, end; plate_mesh check; int unknown;
    int section_len = autosave_format(section, sizeof(section), slot, m);
    int found = section_len > 0 ? autosave_section(text, length, slot, &start, &end) : -1;
    if (found < 0 || (found && (!autosave_mesh(text, start, end, &check, &unknown) || unknown))) return NULL;
    const char *prefix = "", *suffix = "";
    size_t body = (size_t)section_len;
    if (!found) { /* after the last non-empty line, before the file's trailing newlines */
        start = end = length;
        while (start && text[start - 1] == '\n') start = --end;
        prefix = separator; body--;
        if (end == length) suffix = "\n";
    }
    size_t total = start + strlen(prefix) + body + strlen(suffix) + (length - end), used = 0;
    char *copy = malloc(total + 1);
    if (!copy) return NULL;
    memcpy(copy, text, start); used = start;
    memcpy(copy + used, prefix, strlen(prefix)); used += strlen(prefix);
    memcpy(copy + used, section, body); used += body;
    memcpy(copy + used, suffix, strlen(suffix)); used += strlen(suffix);
    memcpy(copy + used, text + end, length - end); used += length - end;
    copy[used] = 0;
    size_t check_start, check_end;
    if (autosave_section(copy, used, slot, &check_start, &check_end) != 1 ||
        !autosave_mesh(copy, check_start, check_end, &check, &unknown) || unknown || !plate_mesh_equal(&check, m, 1)) {
        free(copy); return NULL;
    }
    *out_length = used;
    return copy;
}

static int autosave_backup_path(char *out, size_t cap) {
    size_t base = strlen(printer_autosave_path);
    return base > 4 && !strcmp(printer_autosave_path + base - 4, ".cfg") &&
           snprintf(out, cap, "%.*s_backup.cfg", (int)(base - 4), printer_autosave_path) < (int)cap;
}

static mode_t autosave_mode(void) {
    struct stat info;
    return stat(printer_autosave_path, &info) == 0 ? info.st_mode & 07777 : 0644;
}

/* The unchanged file, kept next to the library until the restart has been verified. */
static void plates_autosave_copy_path(char *out, size_t cap) { snprintf(out, cap, "%s.autosave.bak", plates_path); }

/* ---- printer memory and offset ----------------------------------------------- */

static int plates_json_double(const char *p, const char *end, double *out, const char **after) {
    const char *stop = p;
    while (stop < end && (isdigit((unsigned char)*stop) || strchr("+-.eE", *stop))) stop++;
    if (!autosave_number(p, (size_t)(stop - p), out)) return 0;
    if (after) *after = stop;
    return 1;
}

static int plates_member_double(const char *obj, const char *end, const char *key, double *out) {
    const char *value = json_member(obj, end, key);
    return value && plates_json_double(value, end, out, NULL);
}

static int plates_member_count(const char *obj, const char *end, const char *key, int *out) {
    double value;
    if (!plates_member_double(obj, end, key, &value) || value != floor(value) || fabs(value) > 1000) return 0;
    *out = (int)value; return 1;
}

/* [[n,...],...] row by row into m->points; sets the counts it saw. */
static int plates_json_points(const char *p, const char *end, plate_mesh *m) {
    int rows = 0, cols = -1;
    p = json_skip_space(p, end);
    if (p >= end || *p != '[') return 0;
    for (p = json_skip_space(p + 1, end); p < end && *p == '[';) {
        int count = 0;
        for (p = json_skip_space(p + 1, end); p < end && *p != ']';) {
            if (count >= PLATE_GRID_MAX || rows >= PLATE_GRID_MAX ||
                !plates_json_double(p, end, &m->points[rows * PLATE_GRID_MAX + count], &p)) return 0;
            count++;
            p = json_skip_space(p, end);
            if (p < end && *p == ',') p = json_skip_space(p + 1, end);
        }
        if (p >= end || !count || (cols >= 0 && count != cols)) return 0;
        cols = count; rows++;
        p = json_skip_space(p + 1, end);
        if (p < end && *p == ',') p = json_skip_space(p + 1, end);
    }
    if (p >= end || *p != ']' || !rows) return 0;
    for (int r = 1; r < rows; ++r) /* drop the PLATE_GRID_MAX stride */
        memmove(&m->points[r * cols], &m->points[r * PLATE_GRID_MAX], (size_t)cols * sizeof(double));
    m->x_count = cols; m->y_count = rows;
    return 1;
}

/* Mesh parameters with the same names in bed_mesh.profiles and in the library file. */
static int plates_json_params(const char *obj, const char *end, plate_mesh *m) {
    const char *algo; int algo_len, x, y;
    if (!plates_member_count(obj, end, "x_count", &x) || !plates_member_count(obj, end, "y_count", &y) ||
        x != m->x_count || y != m->y_count || !plates_member_count(obj, end, "mesh_x_pps", &m->x_pps) ||
        !plates_member_count(obj, end, "mesh_y_pps", &m->y_pps) || !plates_member_double(obj, end, "min_x", &m->min_x) ||
        !plates_member_double(obj, end, "max_x", &m->max_x) || !plates_member_double(obj, end, "min_y", &m->min_y) ||
        !plates_member_double(obj, end, "max_y", &m->max_y) || !plates_member_double(obj, end, "tension", &m->tension) ||
        !json_member_raw_string(obj, end, "algo", &algo, &algo_len) || algo_len <= 0 ||
        (size_t)algo_len >= sizeof(m->algo)) return 0;
    memcpy(m->algo, algo, (size_t)algo_len); m->algo[algo_len] = 0;
    return 1;
}

/* 1 slot in memory, 0 absent from memory, -1 query failed. Memory carries no offset. */
static const char plates_profiles_query[] =
    "{\"id\":203,\"method\":\"objects/query\",\"params\":{\"objects\":{\"bed_mesh\":[\"profiles\"]}}}\003";
static int plates_memory_reply(char side,plate_mesh *mesh,const char *reply,size_t length) {
    const char *end = reply + length, *root = json_skip_space(reply, end), *root_end, *e1, *e2, *e3, *e4, *e5, *e6;
    int found = -1;
    if (root < end && *root == '{' && (root_end = json_container_end(root, end))) {
        const char *result = json_member_object(root, root_end, "result", '{', &e1);
        const char *status = result ? json_member_object(result, e1, "status", '{', &e2) : NULL;
        const char *bed = status ? json_member_object(status, e2, "bed_mesh", '{', &e3) : NULL;
        const char *profiles = bed ? json_member_object(bed, e3, "profiles", '{', &e4) : NULL;
        const char *slot = profiles ? json_member_object(profiles, e4, plate_slot(side), '{', &e5) : NULL;
        const char *params = slot ? json_member_object(slot, e5, "mesh_params", '{', &e6) : NULL;
        const char *points = slot ? json_member(slot, e5, "points") : NULL;
        memset(mesh, 0, sizeof(*mesh));
        if (profiles && !slot) found = 0;
        else if (params && points && plates_json_points(points, e5, mesh) && plates_json_params(params, e6, mesh) &&
                 plate_mesh_valid(mesh)) found = 1;
    }
    return found;
}
static int plates_memory_slot(char side,plate_mesh *mesh) {
    char *reply=NULL;size_t length=0;
    if(plates_uds(plates_profiles_query,&reply,&length)!=0||!reply)return -1;
    int found=plates_memory_reply(side,mesh,reply,length);free(reply);return found;
}

/* SET_GCODE_OFFSET Z= without MOVE: nothing moves, the next move uses the new offset. */
static int plates_accept_z(double z,const char *reply,size_t length) {
    const char *end = reply + length, *root = json_skip_space(reply, end);
    const char *root_end = root < end && *root == '{' ? json_container_end(root, end) : NULL;
    int ok = root_end && json_member(root, root_end, "result") && !json_member(root, root_end, "error");
    if (!ok) return -1;
    plates_z_valid = 1; plates_z_applied = z;
    plates_z_service_known = stat(object_query_path, &plates_z_service) == 0;
    z_offset_session = 0; /* live adjustments now start from the plate value */
    return 0;
}
static int plates_apply_z(double z) {
    char query[192],*reply=NULL;size_t length=0;
    snprintf(query,sizeof(query),"{\"id\":204,\"method\":\"gcode/script\",\"params\":{\"script\":\"SET_GCODE_OFFSET Z=%.3f\"}}\003",z);
    if(plates_uds(query,&reply,&length)!=0||!reply)return -1;
    int result=plates_accept_z(z,reply,length);free(reply);return result;
}

/* The printer service binds a new socket file each time it starts, so finding the
 * file seen when the offset was applied means the same service still holds it. */
static int plates_service_unchanged(void) {
    struct stat now;
    return plates_z_service_known && stat(object_query_path, &now) == 0 &&
           now.st_dev == plates_z_service.st_dev && now.st_ino == plates_z_service.st_ino &&
           now.st_ctim.tv_sec == plates_z_service.st_ctim.tv_sec &&
           now.st_ctim.tv_nsec == plates_z_service.st_ctim.tv_nsec;
}

/* ---- the library file --------------------------------------------------------- */

static void plates_mesh_json(json_builder *b, const plate_mesh *m) {
    json_builder_printf(b, "{\"x_count\":%d,\"y_count\":%d,\"min_x\":%.6f,\"max_x\":%.6f,\"min_y\":%.6f,"
        "\"max_y\":%.6f,\"mesh_x_pps\":%d,\"mesh_y_pps\":%d,\"algo\":\"%s\",\"tension\":%.6f,\"offset\":%.6f,"
        "\"points\":[", m->x_count, m->y_count, m->min_x, m->max_x, m->min_y, m->max_y, m->x_pps, m->y_pps,
        m->algo, m->tension, m->offset);
    for (int r = 0; r < m->y_count; ++r) {
        json_builder_printf(b, "%s[", r ? "," : "");
        for (int c = 0; c < m->x_count; ++c)
            json_builder_printf(b, "%s%.6f", c ? "," : "", m->points[r * m->x_count + c]);
        json_builder_printf(b, "]");
    }
    json_builder_printf(b, "]}");
}

/* in_printer: -1 leaves the field out (the library file), else whether the mesh is in its slot. */
static void plates_entry_json(json_builder *b, const plate_entry *p, int in_printer) {
    json_builder_printf(b, "{\"id\":\"%s\",\"name\":", p->id);
    json_builder_string(b, p->name);
    json_builder_printf(b, ",\"side\":\"%c\",\"z_offset\":%.3f,\"measured\":%lld,", p->side, p->z, p->measured);
    if (in_printer >= 0) json_builder_printf(b, "\"in_printer\":%s,", in_printer ? "true" : "false");
    json_builder_printf(b, "\"mesh\":");
    plates_mesh_json(b, &p->mesh);
    json_builder_printf(b, "}");
}

static int plates_save(void) {
    json_builder b = {malloc(PLATES_FILE_MAX), 0, PLATES_FILE_MAX, 0};
    if (!b.data) return -1;
    json_builder_printf(&b, "{\"version\":1,\"current\":\"%s\",\"pending\":\"%s\",\"plates\":[",
                        plates.current, plates.pending);
    for (int i = 0; i < plates.count; ++i) {
        if (i) json_builder_printf(&b, ",");
        plates_entry_json(&b, &plates.plates[i], -1);
    }
    json_builder_printf(&b, "]}\n");
    int result = b.failed ? -1 : plates_replace_file(plates_path, NULL, b.data, b.length, 0644);
    free(b.data);
    return result;
}

static int plates_parse_entry(const char *obj, const char *end, plate_entry *p) {
    const char *text, *mesh_end; int length; double measured;
    memset(p, 0, sizeof(*p));
    if (!json_member_raw_string(obj, end, "id", &text, &length) || length != PLATE_ID_LEN) return 0;
    memcpy(p->id, text, PLATE_ID_LEN);
    if (!plate_id_valid(p->id) || !json_member_raw_string(obj, end, "name", &text, &length) ||
        length <= 0 || length > PLATE_NAME_MAX) return 0;
    memcpy(p->name, text, (size_t)length);
    if (!plate_name_valid(p->name) || !json_member_raw_string(obj, end, "side", &text, &length) ||
        length != 1 || (text[0] != 'A' && text[0] != 'B')) return 0;
    p->side = text[0];
    if (!plates_member_double(obj, end, "z_offset", &p->z) || fabs(p->z) > PLATE_Z_LIMIT + 1e-9 ||
        !plates_member_double(obj, end, "measured", &measured) || measured < 0) return 0;
    p->measured = (long long)measured;
    const char *mesh = json_member_object(obj, end, "mesh", '{', &mesh_end);
    const char *points = mesh ? json_member(mesh, mesh_end, "points") : NULL;
    return points && plates_json_points(points, mesh_end, &p->mesh) &&
           plates_json_params(mesh, mesh_end, &p->mesh) &&
           plates_member_double(mesh, mesh_end, "offset", &p->mesh.offset) && plate_mesh_valid(&p->mesh);
}

static int plates_parse_id(const char *obj, const char *end, const char *key, char out[PLATE_ID_LEN + 1]) {
    const char *text; int length;
    out[0] = 0;
    if (!json_member_raw_string(obj, end, key, &text, &length) || (length && length != PLATE_ID_LEN)) return 0;
    memcpy(out, text, (size_t)length); out[length] = 0;
    return !length || plate_id_valid(out);
}

/* A missing file is an empty library; anything unreadable keeps the library closed. */
static void plates_load(void) {
    memset(&plates, 0, sizeof(plates));
    plates_available = 0;
    size_t length = 0;
    char *text = plates_read_file(plates_path, PLATES_FILE_MAX, &length);
    if (!text) {
        plates_available = errno == ENOENT;
        plates_error = plates_available ? "" : "The plate library file is unreadable";
        return;
    }
    plates_error = "The plate library file is unreadable";
    const char *end = text + length, *root = json_skip_space(text, end), *root_end, *list_end;
    int version, ok = 0;
    if (root < end && *root == '{' && (root_end = json_container_end(root, end)) &&
        json_member_int(root, root_end, "version", &version) && version == 1 &&
        plates_parse_id(root, root_end, "current", plates.current) &&
        plates_parse_id(root, root_end, "pending", plates.pending)) {
        const char *list = json_member_object(root, root_end, "plates", '[', &list_end);
        ok = list != NULL;
        for (const char *item = list ? json_next_element(list, list_end) : NULL; ok && item;) {
            const char *item_end = json_container_end(item, list_end);
            plate_entry *p = &plates.plates[plates.count];
            ok = item_end && plates.count < PLATES_MAX && plates_parse_entry(item, item_end, p);
            for (int i = 0; ok && i < plates.count; ++i)
                if (!strcmp(plates.plates[i].id, p->id) || !strcmp(plates.plates[i].name, p->name)) ok = 0;
            if (ok) { plates.count++; item = json_next_element(item_end, list_end); }
        }
        ok = ok && (!plates.current[0] || plate_find(plates.current)) && (!plates.pending[0] || plate_find(plates.pending));
    }
    free(text);
    if (ok) { plates_available = 1; plates_error = ""; }
    else memset(&plates, 0, sizeof(plates));
}

static void plates_new_id(char out[PLATE_ID_LEN + 1]) {
    static unsigned long long counter;
    do {
        unsigned char bytes[8];
        int fd = open("/dev/urandom", O_RDONLY);
        int got = fd >= 0 && read(fd, bytes, sizeof(bytes)) == (ssize_t)sizeof(bytes);
        if (fd >= 0) close(fd);
        if (!got) {
            unsigned long long mix = ((unsigned long long)time(NULL) << 20) ^ (unsigned long long)getpid() ^
                                     ++counter * 0x9e3779b97f4a7c15ULL;
            memcpy(bytes, &mix, sizeof(bytes));
        }
        for (int i = 0; i < 8; ++i) snprintf(out + i * 2, 3, "%02x", bytes[i]);
    } while (plate_find(out));
}

static int plates_background_active(void);

/* ---- HTTP ------------------------------------------------------------------------ */

static int plates_console_busy(void) {
    if (!recovery_console) return 0;
    pthread_mutex_lock(&recovery_console->lock);
    int busy = recovery_console->busy;
    pthread_mutex_unlock(&recovery_console->lock);
    return busy;
}

/* NULL when the printer may be touched: idle, fresh on both channels and busy with nothing of ours. */
static const char *plates_printer_ready(const mqtt_client *mqtt) {
    time_t now = time(NULL);
    if (!mqtt->connected || !mqtt->registered) return "Printer MQTT is not ready";
    if (!mqtt->have_machine_status || mqtt->machine_status != 1) return "The printer must be idle";
    if (mqtt->last_message <= 0 || now < mqtt->last_message || now - mqtt->last_message > 15)
        return "Printer telemetry is stale";
    if (!uds_fresh(&telemetry)) return "Printer service telemetry is unavailable";
    if (reboot_pending || plates_reboot_requested) return "A printer restart is already pending";
    if (plates_console_busy()) return "Another printer command is still running";
    if (atomic_load(&upload_busy) || atomic_load(&active_downloads)) return "A file transfer is in progress";
    return NULL;
}

static void plates_reply(int fd, int status, const char *body) {
    const char *text = status == 200 ? "OK" : status == 201 ? "Created" : status == 202 ? "Accepted" :
        status == 400 ? "Bad Request" : status == 404 ? "Not Found" : status == 409 ? "Conflict" :
        status == 503 ? "Service Unavailable" : "Internal Server Error";
    respond(fd, status, text, "application/json; charset=utf-8", body, strlen(body));
}

static void plates_fail(int fd, int status, const char *error) {
    char body[320], escaped[256];
    json_escape(escaped, sizeof(escaped), error);
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}\n", escaped);
    plates_reply(fd, status, body);
}

/* Splits a text/plain body into lines; returns their number, or -1 when too many or too long. */
static int plates_fields(const char *body, size_t length, char fields[][PLATE_NAME_MAX + 1], int max) {
    int count = 0;
    while (length && (body[length - 1] == '\n' || body[length - 1] == '\r')) length--;
    for (size_t pos = 0; length && pos <= length;) {
        const char *eol = memchr(body + pos, '\n', length - pos);
        size_t line_end = eol ? (size_t)(eol - body) : length, line_len = line_end - pos;
        if (line_len && body[line_end - 1] == '\r') line_len--;
        if (count >= max || line_len > PLATE_NAME_MAX || memchr(body + pos, '\0', line_len)) return -1;
        memcpy(fields[count], body + pos, line_len); fields[count][line_len] = 0;
        count++;
        pos = line_end + 1;
    }
    return count;
}

static void plates_get_response(int fd) {
    plate_mesh slots[2]; int present[2];
    for (int i = 0; i < 2; ++i) present[i] = autosave_slot(i ? 'B' : 'A', &slots[i]);
    json_builder b = {malloc(PLATES_FILE_MAX + 4096), 0, PLATES_FILE_MAX + 4096, 0};
    if (!b.data) { plates_fail(fd, 500, "Out of memory"); return; }
    const plate_entry *current = plates.current[0] ? plate_find(plates.current) : NULL;
    json_builder_printf(&b, "{\"available\":%s,\"error\":", plates_available ? "true" : "false");
    json_builder_string(&b, plates_error);
    json_builder_printf(&b, ",\"current\":\"%s\",\"pending\":\"%s\",\"result\":\"%s\",\"z_applied\":%s,"
        "\"slots\":{\"A\":\"%s\",\"B\":\"%s\"},\"plates\":[", plates.current, plates.pending, plates_result,
        current && plates_z_valid && plate_close(plates_z_applied, current->z) ? "true" : "false",
        present[0] < 0 ? "unreadable" : present[0] ? "mesh" : "empty",
        present[1] < 0 ? "unreadable" : present[1] ? "mesh" : "empty");
    for (int i = 0; i < plates.count; ++i) {
        const plate_entry *p = &plates.plates[i];
        int slot = p->side == 'B';
        if (i) json_builder_printf(&b, ",");
        plates_entry_json(&b, p, present[slot] == 1 && plate_mesh_equal(&slots[slot], &p->mesh, 1));
    }
    json_builder_printf(&b, "]}\n");
    if (b.failed) plates_fail(fd, 500, "Plate library response is too large");
    else respond(fd, 200, "OK", "application/json; charset=utf-8", b.data, b.length);
    free(b.data);
}

/* The slot mesh as the printer uses it: the file and printer memory must agree. */
static int plates_capture(int fd, char side, plate_mesh *mesh) {
    plate_mesh memory;
    int file = autosave_slot(side, mesh);
    if (file < 0) { plates_fail(fd, 503, "Cannot read the printer mesh file"); return 0; }
    if (!file) { plates_fail(fd, 409, "This side has no saved mesh; run a bed mesh calibration first"); return 0; }
    int live = plates_memory_slot(side, &memory);
    if (live < 0) { plates_fail(fd, 503, "Cannot read the printer mesh"); return 0; }
    if (!live || !plate_mesh_equal(&memory, mesh, 0)) {
        plates_fail(fd, 409, "The printer mesh in memory differs from the saved file; restart the printer first");
        return 0;
    }
    return 1;
}

static int plates_open(int fd) {
    if (plates_background_active()) { plates_fail(fd,409,"Plate verification is still running");return 0; }
    if (!plates_available) { plates_fail(fd, 503, plates_error); return 0; }
    if (plates.pending[0]) { plates_fail(fd, 409, "A plate change is waiting for the printer restart"); return 0; }
    return 1;
}

static int plates_name_taken(const char *name, const plate_entry *except) {
    for (int i = 0; i < plates.count; ++i)
        if (&plates.plates[i] != except && !strcmp(plates.plates[i].name, name)) return 1;
    return 0;
}

/* side \n name \n z: a new plate from the mesh the printer now keeps for that side. */
static void plates_save_response(int fd, const mqtt_client *mqtt, const char *body, size_t length) {
    char fields[3][PLATE_NAME_MAX + 1]; double z; const char *reason;
    if (plates_fields(body, length, fields, 3) != 3 || strlen(fields[0]) != 1 || (fields[0][0] != 'A' &&
        fields[0][0] != 'B') || !plate_name_valid(fields[1]) || !plate_z_parse(fields[2], &z)) {
        plates_fail(fd, 400, "Invalid plate"); return;
    }
    if (!plates_open(fd)) return;
    if (plates.count >= PLATES_MAX) { plates_fail(fd, 409, "The plate library is full"); return; }
    if (plates_name_taken(fields[1], NULL)) { plates_fail(fd, 409, "A plate with this name already exists"); return; }
    if ((reason = plates_printer_ready(mqtt))) { plates_fail(fd, 409, reason); return; }
    plate_entry *p = &plates.plates[plates.count];
    memset(p, 0, sizeof(*p));
    if (!plates_capture(fd, fields[0][0], &p->mesh)) return;
    plates_new_id(p->id);
    snprintf(p->name, sizeof(p->name), "%s", fields[1]);
    p->side = fields[0][0]; p->z = z; p->measured = (long long)time(NULL);
    plates.count++;
    if (plates_save() != 0) { plates.count--; plates_fail(fd, 500, "Cannot save the plate library"); return; }
    char reply[96];
    snprintf(reply, sizeof(reply), "{\"saved\":true,\"id\":\"%s\"}\n", p->id);
    plates_reply(fd, 201, reply);
}

/* id: the mounted plate takes the mesh the printer now keeps for its side, after a new calibration. */
static void plates_recapture_response(int fd, const mqtt_client *mqtt, const char *body, size_t length) {
    char fields[1][PLATE_NAME_MAX + 1]; const char *reason; plate_mesh mesh;
    if (plates_fields(body, length, fields, 1) != 1 || !plate_id_valid(fields[0])) { plates_fail(fd, 400, "Invalid plate"); return; }
    if (!plates_open(fd)) return;
    plate_entry *p = plate_find(fields[0]);
    if (!p) { plates_fail(fd, 404, "Unknown plate"); return; }
    if (strcmp(plates.current, p->id)) { plates_fail(fd, 409, "Only the mounted plate can take the printer mesh"); return; }
    if ((reason = plates_printer_ready(mqtt))) { plates_fail(fd, 409, reason); return; }
    if (!plates_capture(fd, p->side, &mesh)) return;
    plate_entry old = *p;
    p->mesh = mesh; p->measured = (long long)time(NULL);
    if (plates_save() != 0) { *p = old; plates_fail(fd, 500, "Cannot save the plate library"); return; }
    plates_reply(fd, 200, "{\"saved\":true}\n");
}

/* id \n name \n z. A new Z for the mounted plate is applied at once while the printer is idle. */
static void plates_edit_response(int fd, const mqtt_client *mqtt, const char *body, size_t length) {
    char fields[3][PLATE_NAME_MAX + 1]; double z;
    if (plates_fields(body, length, fields, 3) != 3 || !plate_id_valid(fields[0]) ||
        !plate_name_valid(fields[1]) || !plate_z_parse(fields[2], &z)) { plates_fail(fd, 400, "Invalid plate"); return; }
    if (!plates_open(fd)) return;
    plate_entry *p = plate_find(fields[0]);
    if (!p) { plates_fail(fd, 404, "Unknown plate"); return; }
    if (plates_name_taken(fields[1], p)) { plates_fail(fd, 409, "A plate with this name already exists"); return; }
    plate_entry old = *p;
    snprintf(p->name, sizeof(p->name), "%s", fields[1]); p->z = z;
    if (plates_save() != 0) { *p = old; plates_fail(fd, 500, "Cannot save the plate library"); return; }
    int applied = !strcmp(plates.current, p->id) && !plates_printer_ready(mqtt) && !z_offset_pending &&
                  plates_apply_z(z) == 0;
    plates_reply(fd, 200, applied ? "{\"saved\":true,\"applied\":true}\n" : "{\"saved\":true,\"applied\":false}\n");
}

static void plates_delete_response(int fd, const char *body, size_t length) {
    char fields[1][PLATE_NAME_MAX + 1];
    if (plates_fields(body, length, fields, 1) != 1 || !plate_id_valid(fields[0])) { plates_fail(fd, 400, "Invalid plate"); return; }
    if (!plates_open(fd)) return;
    plate_entry *p = plate_find(fields[0]);
    if (!p) { plates_fail(fd, 404, "Unknown plate"); return; }
    int index = (int)(p - plates.plates), was_current = !strcmp(plates.current, p->id);
    plate_entry removed = *p;
    memmove(p, p + 1, (size_t)(plates.count - index - 1) * sizeof(*p));
    plates.count--;
    if (was_current) plates.current[0] = 0;
    if (plates_save() != 0) {
        memmove(&plates.plates[index + 1], &plates.plates[index], (size_t)(plates.count - index) * sizeof(*p));
        plates.plates[index] = removed; plates.count++;
        if (was_current) memcpy(plates.current, removed.id, sizeof(plates.current));
        plates_fail(fd, 500, "Cannot save the plate library"); return;
    }
    if (was_current) plates_result = "";
    plates_reply(fd, 200, "{\"deleted\":true}\n");
}

/* Forget which plate is mounted; the printer keeps its mesh and offset. */
static void plates_unmount_response(int fd) {
    if (!plates_open(fd)) return;
    char old[PLATE_ID_LEN + 1]; memcpy(old, plates.current, sizeof(old));
    plates.current[0] = 0;
    if (plates_save() != 0) { memcpy(plates.current, old, sizeof(old)); plates_fail(fd, 500, "Cannot save the plate library"); return; }
    plates_result = "";
    plates_reply(fd, 200, "{\"mounted\":false}\n");
}

/* A print started from the screen or a slicer while the reboot waited must not be cut off. */
static int plates_reboot_guard(void) {
    time_t now=time(NULL);const mqtt_client *m=plates_mqtt;
    return m && m->connected && m->registered && m->have_machine_status && m->machine_status==1 &&
        m->last_message>0 && now>=m->last_message && now-m->last_message<=15 && uds_fresh(&telemetry) &&
        !plates_console_busy() && !atomic_load(&upload_busy) && !atomic_load(&active_downloads) && !z_offset_pending;
}

/* id [\n REBOOT]: mount a plate. Its Z offset applies at once. A mesh that is not in
 * its slot is written to autosave.cfg and needs a printer restart, which the second
 * line confirms; without it the reply only says that a restart is required. */
static void plates_mount_response(int fd, const mqtt_client *mqtt, const char *body, size_t length) {
    char fields[2][PLATE_NAME_MAX + 1]; const char *reason;
    int count = plates_fields(body, length, fields, 2);
    if (count < 1 || !plate_id_valid(fields[0]) || (count == 2 && strcmp(fields[1], "REBOOT"))) {
        plates_fail(fd, 400, "Invalid plate"); return;
    }
    if (!plates_open(fd)) return;
    plate_entry *p = plate_find(fields[0]);
    if (!p) { plates_fail(fd, 404, "Unknown plate"); return; }
    if ((reason = plates_printer_ready(mqtt))) { plates_fail(fd, 409, reason); return; }
    if (z_offset_pending) { plates_fail(fd, 409, "A Z offset change is still being confirmed"); return; }
    size_t file_length = 0, new_length = 0, start, end; plate_mesh slot; int unknown;
    char *file = plates_read_file(printer_autosave_path, AUTOSAVE_FILE_MAX, &file_length);
    if (!file) { plates_fail(fd, 503, "Cannot read the printer mesh file"); return; }
    int found = autosave_section(file, file_length, plate_slot(p->side), &start, &end);
    if (found < 0 || (found && (!autosave_mesh(file, start, end, &slot, &unknown) || unknown))) {
        free(file); plates_fail(fd, 503, "The printer mesh file has an unexpected format"); return;
    }
    char old_current[PLATE_ID_LEN + 1]; memcpy(old_current, plates.current, sizeof(old_current));
    int matches=found && plate_mesh_equal(&slot,&p->mesh,1);
    if(matches){
        plate_mesh memory;int live=plates_memory_slot(p->side,&memory);
        if(live<0){free(file);plates_fail(fd,503,"Cannot verify the printer mesh in memory");return;}
        matches=live==1 && plate_mesh_equal(&memory,&p->mesh,0);
    }
    if (matches) {
        free(file);
        if (plates_apply_z(p->z) != 0) { plates_fail(fd, 503, "Cannot apply the plate Z offset"); return; }
        memcpy(plates.current, p->id, sizeof(plates.current));
        if (plates_save() != 0) {
            memcpy(plates.current, old_current, sizeof(old_current));
            plates_fail(fd, 500, "Cannot save the plate library"); return;
        }
        plates_result = "mounted";
        plates_reply(fd, 200, "{\"mounted\":true,\"reboot\":false}\n");
        return;
    }
    if (count != 2) {
        free(file);
        plates_reply(fd, 409, "{\"ok\":false,\"reboot_required\":true,"
                              "\"error\":\"Writing this mesh to the printer needs a printer restart\"}\n");
        return;
    }
    char backup[PATH_MAX_LOCAL], copy[PATH_MAX_LOCAL];
    plates_autosave_copy_path(copy, sizeof(copy));
    char *updated = autosave_with_mesh(file, file_length, plate_slot(p->side), &p->mesh, &new_length);
    int prepared = updated && autosave_backup_path(backup, sizeof(backup)) &&
                   plates_replace_file(copy, NULL, file, file_length, 0644) == 0;
    /* The firmware may have saved its configuration since the file was read. */
    size_t again_length = 0;
    char *again = prepared ? plates_read_file(printer_autosave_path, AUTOSAVE_FILE_MAX, &again_length) : NULL;
    int unchanged = again && again_length == file_length && !memcmp(again, file, file_length);
    free(again); free(file);
    if (!prepared) { free(updated); plates_fail(fd, 500, "Cannot prepare the printer mesh file"); return; }
    if (!unchanged) { free(updated); plates_fail(fd, 409, "The printer changed its mesh file meanwhile; try again"); return; }
    /* Record the pending mount first, so the next process knows what to verify. */
    memcpy(plates.current, p->id, sizeof(plates.current));
    memcpy(plates.pending, p->id, sizeof(plates.pending));
    if (plates_save() != 0 || plates_replace_file(printer_autosave_path, backup, updated, new_length, autosave_mode()) != 0) {
        plates.pending[0] = 0; memcpy(plates.current, old_current, sizeof(old_current));
        (void)plates_save();
        free(updated); plates_fail(fd, 500, "Cannot write the printer mesh file"); return;
    }
    free(updated);
    memcpy(plates_previous_current, old_current, sizeof(old_current));
    plates_reboot_requested = 1; plates_result = "rebooting"; plates_mqtt = mqtt;
    reboot_guard = plates_reboot_guard;
    reboot_pending = 1; reboot_launched = 0; reboot_due = recovery_clock() + 2; reboot_error = "none";
    plates_reply(fd, 202, "{\"mounted\":true,\"reboot\":true}\n");
}

/* recovery_tick gave up on the restart, or its guard saw the printer busy: memory still
 * holds the old mesh, so the file goes back. */
/* Restore only the slot we wrote. Unrelated SAVE_CONFIG changes survive.
 * Refuse to overwrite a slot changed by another actor or an unreadable file. */
static int plates_restore_mesh(void) {
    const plate_entry *p=plate_find(plates.pending);
    char copy[PATH_MAX_LOCAL];size_t old_length=0,current_length=0;
    plates_autosave_copy_path(copy,sizeof(copy));
    char *old=plates_read_file(copy,AUTOSAVE_FILE_MAX,&old_length);
    char *current=plates_read_file(printer_autosave_path,AUTOSAVE_FILE_MAX,&current_length);
    int ok=0;char *restored=NULL;size_t restored_length=0;
    if(!p||!old||!current)goto done;
    size_t start,end,old_start=0,old_end=0;plate_mesh written;int unknown;
    const char *slot=plate_slot(p->side);
    if(autosave_section(current,current_length,slot,&start,&end)!=1 ||
       !autosave_mesh(current,start,end,&written,&unknown)||unknown||!plate_mesh_equal(&written,&p->mesh,1))goto done;
    int old_found=autosave_section(old,old_length,slot,&old_start,&old_end);
    if(old_found<0)goto done;
    size_t expected_length=0;char *expected=autosave_with_mesh(old,old_length,slot,&p->mesh,&expected_length);
    if(!expected)goto done;
    if(expected_length==current_length && !memcmp(expected,current,current_length)){
        restored=malloc(old_length+1);if(restored){memcpy(restored,old,old_length+1);restored_length=old_length;}
    }else{
        size_t section_length=old_found?old_end-old_start:0;
        restored_length=start+section_length+current_length-end;
        restored=malloc(restored_length+1);
        if(restored){memcpy(restored,current,start);if(section_length)memcpy(restored+start,old+old_start,section_length);
            memcpy(restored+start+section_length,current+end,current_length-end);restored[restored_length]=0;}
    }
    free(expected);
    if(!restored)goto done;
    size_t again_length=0;char *again=plates_read_file(printer_autosave_path,AUTOSAVE_FILE_MAX,&again_length);
    int unchanged=again && again_length==current_length && !memcmp(again,current,current_length);free(again);
    if(unchanged)ok=plates_replace_file(printer_autosave_path,NULL,restored,restored_length,autosave_mode())==0;
 done:
    free(restored);free(old);free(current);return ok?0:-1;
}
static void plates_restart_failed(void) {
    reboot_guard=NULL;
    if(plates_restore_mesh()!=0){
        plates_reboot_requested=0;plates_available=0;
        plates_error="Mesh rollback failed; backups and pending mount were retained";
        plates_result="reboot_failed";return;
    }
    plates_reboot_requested=0;plates.pending[0]=0;
    memcpy(plates.current,plates_previous_current,sizeof(plates.current));
    if(plates_save()!=0){plates_available=0;plates_error="Cannot record the mesh rollback";}
    plates_result="reboot_failed";
}

/* Automatic recovery never waits in poll/read. One short-lived UDS transaction,
 * at most 64 KiB of reply data and three attempts per plate/service generation. */
typedef struct {int fd,connecting,id;size_t sent,used;long long deadline;char request[512];char *buffer;} plates_exchange;
static plates_exchange plates_background={.fd=-1};
static int plates_background_active(void){return plates_background.fd>=0;}
static unsigned plates_attempts;
static char plates_attempt_id[PLATE_ID_LEN+1];
static double plates_attempt_z;
static struct stat plates_attempt_service;
static int plates_attempt_service_known;
static void plates_background_close(void){
    if(plates_background.fd>=0)close(plates_background.fd);
    free(plates_background.buffer);memset(&plates_background,0,sizeof(plates_background));plates_background.fd=-1;
}
/* 1 completed, 0 still pending, -1 failed. The caller owns a completed reply. */
static int plates_background_step(const char *query,char **reply,size_t *length){
    *reply=NULL;*length=0;
    /* Existing unit fixtures use their in-memory firmware; production uses the
     * nonblocking transport below. */
    if(plates_uds!=uds_query_json)return plates_uds(query,reply,length)==0?1:-1;
    plates_exchange *x=&plates_background;
    if(x->fd<0){
        if(strlen(query)>=sizeof(x->request)||strlen(object_query_path)>=sizeof(((struct sockaddr_un *)0)->sun_path))return -1;
        x->fd=socket(AF_UNIX,SOCK_STREAM,0);if(x->fd<0)return -1;
        if(fcntl(x->fd,F_SETFL,O_NONBLOCK)<0)goto failed;
        x->buffer=malloc(65537);if(!x->buffer)goto failed;
        snprintf(x->request,sizeof(x->request),"%s",query);x->id=strstr(query,"gcode/script")?204:203;
        x->deadline=monotonic_ms()+2000;
        struct sockaddr_un address;memset(&address,0,sizeof(address));address.sun_family=AF_UNIX;
        snprintf(address.sun_path,sizeof(address.sun_path),"%s",object_query_path);
        if(connect(x->fd,(struct sockaddr *)&address,sizeof(address))<0){if(errno!=EINPROGRESS)goto failed;x->connecting=1;}
    }
    if(strcmp(x->request,query))goto failed;
    if(x->connecting){
        struct pollfd pfd={x->fd,POLLOUT,0};int ready=poll(&pfd,1,0);
        if(ready<0&&errno!=EINTR)goto failed;
        if(ready<=0){if(monotonic_ms()>=x->deadline)goto failed;return 0;}
        int error=0;socklen_t size=sizeof(error);
        if(getsockopt(x->fd,SOL_SOCKET,SO_ERROR,&error,&size)||error)goto failed;
        x->connecting=0;
    }
    size_t wanted=strlen(x->request);
    if(x->sent<wanted){
        if(monotonic_ms()>=x->deadline)goto failed;
        ssize_t n=send(x->fd,x->request+x->sent,wanted-x->sent,MSG_NOSIGNAL|MSG_DONTWAIT);
        if(n<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))return 0;
        if(n<=0)goto failed;
        x->sent+=(size_t)n;
        if(x->sent<wanted)return 0;
    }
    for(int turn=0;turn<4;turn++){
        char *separator=x->used?memchr(x->buffer,3,x->used):NULL;
        if(separator){
            size_t frame=(size_t)(separator-x->buffer);const char *end=x->buffer+frame;
            const char *root=json_skip_space(x->buffer,end),*root_end=root<end&&*root=='{'?json_container_end(root,end):NULL;
            int id=-1;
            if(root_end&&json_member_int(root,root_end,"id",&id)&&id==x->id){
                char *body=malloc(frame+1);if(!body)goto failed;
                memcpy(body,x->buffer,frame);body[frame]=0;*reply=body;*length=frame;plates_background_close();return 1;
            }
            memmove(x->buffer,x->buffer+frame+1,x->used-frame-1);x->used-=frame+1;continue;
        }
        if(x->used==65536)goto failed;
        ssize_t n=recv(x->fd,x->buffer+x->used,65536-x->used,MSG_DONTWAIT);
        if(n<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))break;
        if(n<=0)goto failed;
        x->used+=(size_t)n;
    }
    if(monotonic_ms()>=x->deadline)goto failed;
    return 0;
 failed:
    plates_background_close();return -1;
}
static int plates_memory_background(char side,plate_mesh *mesh){
    char *reply=NULL;size_t length=0;int result=plates_background_step(plates_profiles_query,&reply,&length);
    if(!result)return -2;
    if(result<0)return -1;
    int found=plates_memory_reply(side,mesh,reply,length);free(reply);return found;
}
static int plates_z_background(double z){
    char query[192],*reply=NULL;size_t length=0;
    snprintf(query,sizeof(query),"{\"id\":204,\"method\":\"gcode/script\",\"params\":{\"script\":\"SET_GCODE_OFFSET Z=%.3f\"}}\003",z);
    int result=plates_background_step(query,&reply,&length);
    if(!result)return -2;
    if(result<0)return -1;
    int ok=plates_accept_z(z,reply,length);free(reply);return ok;
}
static void plates_background_failed(long long now){
    plates_attempts++;plates_next_tick_ms=now+(plates_attempts==1?5000:15000);
    if(plates_attempts>=3){
        plates_result="verify_failed";
        if(plates.pending[0]){plates.pending[0]=0;plates.current[0]=0;if(plates_save()!=0){plates_available=0;plates_error="Cannot record failed plate verification";}}
    }
}

/* Main loop: undo a write whose restart failed, verify a mount after the restart and
 * keep the mounted plate's Z offset applied while the printer is idle. */
static void plates_tick(const mqtt_client *mqtt) {
    if (telemetry.connections != plates_seen_connections) {
        plates_seen_connections = telemetry.connections;
        if (!plates_service_unchanged()){
            struct stat service;
            int known=stat(object_query_path,&service)==0;
            int changed=known && (!plates_attempt_service_known || service.st_dev!=plates_attempt_service.st_dev ||
                service.st_ino!=plates_attempt_service.st_ino || service.st_ctim.tv_sec!=plates_attempt_service.st_ctim.tv_sec ||
                service.st_ctim.tv_nsec!=plates_attempt_service.st_ctim.tv_nsec);
            plates_z_valid=0;plates_background_close();
            if(changed)plates_attempts=0;
            if(known){plates_attempt_service=service;plates_attempt_service_known=1;}
        }
    }
    if (plates_reboot_requested) {
        if (!reboot_pending) plates_restart_failed();
        return;
    }
    if (!plates_available) {plates_background_close();return;}
    const plate_entry *current = plates.current[0] ? plate_find(plates.current) : NULL;
    int z_due = current && (!plates_z_valid || !plate_close(plates_z_applied, current->z));
    long long now = monotonic_ms();
    if(current && (strcmp(plates_attempt_id,current->id)||!plate_close(plates_attempt_z,current->z))){
        snprintf(plates_attempt_id,sizeof(plates_attempt_id),"%s",current->id);plates_attempt_z=current->z;
        plates_attempts=0;plates_background_close();
    }
    if ((!plates.pending[0] && !z_due) || plates_printer_ready(mqtt) || z_offset_pending){plates_background_close();return;}
    if(plates_attempts>=3 || (now<plates_next_tick_ms && plates_background.fd<0))return;
    plates_next_tick_ms = now + 5000;
    if (plates.pending[0]) {
        const plate_entry *p = plate_find(plates.pending); /* plates_load and plates_open keep it present */
        plate_mesh file, memory;
        int in_memory=p?plates_memory_background(p->side,&memory):0;
        if(in_memory==-2)return;
        if(in_memory<0){plates_background_failed(now);return;}
        int ok = p && autosave_slot(p->side, &file) == 1 && plate_mesh_equal(&file, &p->mesh, 1) &&
                 in_memory == 1 && plate_mesh_equal(&memory, &p->mesh, 0);
        plates.pending[0] = 0;
        if (!ok) plates.current[0] = 0;
        if(plates_save()!=0){plates_available=0;plates_error="Cannot record plate verification";plates_result="verify_failed";return;}
        plates_result = ok ? "mounted" : "verify_failed";
        if (!ok) return;
        current = plate_find(plates.current);
        plates_z_valid = 0;
    }
    if(current){int applied=plates_z_background(current->z);if(applied==-1)plates_background_failed(now);else if(applied==0)plates_attempts=0;}
}
