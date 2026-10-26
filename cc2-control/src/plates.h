/* Build-plate library: named build-plate surfaces with the bed meshes measured on
 * them at different bed temperatures, and the nozzles they are printed with.
 *
 * Every print loads the mesh slot of its side (G180 S7): Side A prints with
 * profile `default`, Side B with `default1`. The firmware refuses
 * BED_MESH_PROFILE SAVE=default and its in-process RESTART hangs, so mounting a
 * plate whose mesh is not already in its slot rewrites that one [bed_mesh]
 * section of autosave.cfg and reboots the printer. The next CC2 Control process
 * checks the slot both in the file and in printer memory.
 *
 * A measurement whose mesh is in a slot is also copied to its own printer
 * profile, cc2_<id>, with BED_MESH_PROFILE SAVE, which needs no restart. A print
 * started here with another measurement of the mounted plate, such as the one
 * nearest to its bed temperature, loads that profile each time the print start
 * has loaded the side slot, until the first layer begins.
 *
 * A plate's Z offset plus the selected nozzle's correction is the user part of
 * the G-code offset (SET_GCODE_OFFSET Z=). The mesh is shared by all nozzles. The
 * stock print start keeps the offset when it adds the side's bed-roughness
 * offset, but a printer restart clears it, so it is applied again once the
 * printer is idle after the printer service restarts. A reconnect alone, such as
 * after a receive timeout during a busy print start, keeps it. The touchscreen's
 * Z offset control counts from its own zero and sends absolute values, so a
 * press there replaces the plate's value. Everything here runs in the main loop. */
#define PLATES_MAX 16
#define PLATE_MEASURES_MAX 20
#define PLATE_NOZZLES_MAX 8
#define PLATE_ID_LEN 16
#define PLATE_NAME_MAX 64
#define PLATE_GRID_MAX 15
#define PLATE_Z_LIMIT 1.0
#define PLATE_NOZZLE_Z_LIMIT 0.5
#define PLATE_TEMP_MIN 30
#define PLATE_TEMP_MAX 110
#define PLATE_TEMP_V1 60 /* the firmware's calibration temperature, before temperatures were recorded */
#define PLATE_PROFILE_PREFIX "cc2_"
#define PLATES_FILE_MAX (160 * 1024)
#define PLATES_REPLY_MAX (128 * 1024)
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
    char plate[PLATE_ID_LEN + 1];
    int temp; /* bed temperature while probing, deg C */
    char nozzle[PLATE_ID_LEN + 1]; /* "" when not recorded */
    long long measured;
    plate_mesh mesh;
} plate_measure;

typedef struct {
    char id[PLATE_ID_LEN + 1];
    char name[PLATE_NAME_MAX + 1];
    double diameter;
    double z; /* added to every plate's Z offset while this nozzle is selected */
} plate_nozzle;

typedef struct {
    char id[PLATE_ID_LEN + 1];
    char name[PLATE_NAME_MAX + 1];
    char side; /* 'A' or 'B' */
    double z;
    char measure[PLATE_ID_LEN + 1]; /* the measurement its side slot holds once mounted */
} plate_entry;

typedef struct {
    int count, measure_count, nozzle_count;
    char current[PLATE_ID_LEN + 1];
    char pending[PLATE_ID_LEN + 1]; /* mesh written, waiting for the printer restart */
    char nozzle[PLATE_ID_LEN + 1];  /* the selected nozzle, "" for none */
    plate_entry plates[PLATES_MAX];
    plate_measure measures[PLATE_MEASURES_MAX];
    plate_nozzle nozzles[PLATE_NOZZLES_MAX];
} plate_store;

static plate_store plates;
static plate_store plates_undo; /* the store before the change being saved */
static int plates_available;
static const char *plates_error = "Plate library not loaded";
/* Last mount outcome shown by the page: "", "rebooting", "mounted", "verify_failed" or "reboot_failed". */
static const char *plates_result = "";
static int plates_reboot_requested;
static const mqtt_client *plates_mqtt; /* main()'s client, read by the reboot guard */
static char plates_previous_current[PLATE_ID_LEN + 1];
static unsigned long plates_seen_connections = ULONG_MAX;
static int plates_pending_stage; /* stored slot, load, active matrix, offset */
static int plates_z_valid;
static double plates_z_applied;
static struct stat plates_z_service; /* the printer service's socket when the offset was applied */
static int plates_z_service_known;
static long long plates_next_tick_ms;
/* Tests replace the printer round trip. */
static int (*plates_uds)(const char *query, char **reply, size_t *length) = uds_query_json;

/* A bed mesh calibration run by CC2 Control, see plates_calibration_tick. */
enum { CAL_IDLE, CAL_HOMING, CAL_HEATING, CAL_SOAKING, CAL_PROBING, CAL_SAVING };
#define PLATE_SOAK_MAX 60 /* minutes */
static struct {
    int stage, temp, soak; /* soak in seconds */
    char side;
    char plate[PLATE_ID_LEN + 1], nozzle[PLATE_ID_LEN + 1], measure[PLATE_ID_LEN + 1];
    long long since, soak_until, done_at; /* stage start, end of the soak, when the console command finished */
    long long held; /* since when the end of the soak waits for fresh evidence */
    unsigned long generation; /* the console command the stage waits for */
    const char *result; /* "", "done" (no plate), "saved", "failed" or "cancelled" */
    const char *error;  /* what failed: "homing", "heating", "busy", "telemetry", "probing" or "saving" */
    char detail[160];
} plates_cal = {.result = "", .error = ""};

static const char *plate_slot(char side) { return side == 'B' ? "default1" : "default"; }

static plate_entry *plate_find(const char *id) {
    for (int i = 0; i < plates.count; ++i)
        if (!strcmp(plates.plates[i].id, id)) return &plates.plates[i];
    return NULL;
}

static plate_measure *measure_find(const char *id) {
    for (int i = 0; i < plates.measure_count; ++i)
        if (!strcmp(plates.measures[i].id, id)) return &plates.measures[i];
    return NULL;
}

static plate_nozzle *nozzle_find(const char *id) {
    for (int i = 0; i < plates.nozzle_count; ++i)
        if (!strcmp(plates.nozzles[i].id, id)) return &plates.nozzles[i];
    return NULL;
}

/* The measurement a plate mounts with; the store keeps it present. */
static plate_measure *plate_base(const plate_entry *p) {
    plate_measure *m = measure_find(p->measure);
    return m && !strcmp(m->plate, p->id) ? m : NULL;
}

static int plate_measure_count(const char *plate) {
    int count = 0;
    for (int i = 0; i < plates.measure_count; ++i) count += !strcmp(plates.measures[i].plate, plate);
    return count;
}

static void plate_profile_name(char *out, size_t cap, const plate_measure *m) {
    snprintf(out, cap, PLATE_PROFILE_PREFIX "%.16s", m->id);
}

static double plates_nozzle_z(void) {
    const plate_nozzle *n = plates.nozzle[0] ? nozzle_find(plates.nozzle) : NULL;
    return n ? n->z : 0.0;
}

/* What SET_GCODE_OFFSET Z= gets for a plate: its offset plus the selected nozzle's correction. */
static double plate_z_now(const plate_entry *p) {
    double z = round((p->z + plates_nozzle_z()) * 1000.0) / 1000.0;
    return z == 0.0 ? 0.0 : z;
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

/* Plain decimal millimetres such as "-0.020", within [min,max], kept to 3 decimals. */
static int plate_decimal_parse(const char *text, double min, double max, double *out) {
    size_t n = strlen(text);
    if (!n || n > 8 || strspn(text, "0123456789.-+") != n) return 0;
    char *end; errno = 0;
    double value = strtod(text, &end);
    if (end == text || *end || errno || !isfinite(value) || value < min - 1e-9 || value > max + 1e-9) return 0;
    *out = round(value * 1000.0) / 1000.0;
    if (*out == 0.0) *out = 0.0; /* never "-0.000" */
    return 1;
}

static int plate_z_parse(const char *text, double *z) {
    return plate_decimal_parse(text, -PLATE_Z_LIMIT, PLATE_Z_LIMIT, z);
}

/* Whole degrees within what the bed reaches and settles at above room temperature. */
static int plate_temp_parse(const char *text, int *temp) {
    size_t n = strlen(text);
    if (n < 2 || n > 3 || strspn(text, "0123456789") != n) return 0;
    int value = atoi(text);
    if (value < PLATE_TEMP_MIN || value > PLATE_TEMP_MAX) return 0;
    *temp = value;
    return 1;
}

/* "" or the id of a nozzle in the list. */
static int plate_nozzle_ref(const char *text) { return !*text || (plate_id_valid(text) && nozzle_find(text)); }

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

/* One [bed_mesh <name>] of a file read earlier: 1 read, 0 absent, -1 in an unknown format. */
static int autosave_find(const char *text, size_t length, const char *name, plate_mesh *mesh) {
    size_t start, end; int unknown;
    int found = autosave_section(text, length, name, &start, &end);
    if (found == 1 && (!autosave_mesh(text, start, end, mesh, &unknown) || unknown)) found = -1;
    return found;
}

/* 1 profile read, 0 profile absent, -1 file missing, unreadable or in an unknown format. */
static int autosave_profile(const char *name, plate_mesh *mesh) {
    size_t length;
    char *text = plates_read_file(printer_autosave_path, AUTOSAVE_FILE_MAX, &length);
    if (!text) return -1;
    int found = autosave_find(text, length, name, mesh);
    free(text);
    return found;
}

static int autosave_slot(char side, plate_mesh *mesh) { return autosave_profile(plate_slot(side), mesh); }

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

/* Read the actually applied mesh, not just the stored profiles. */
static const char plates_active_query[] =
    "{\"id\":203,\"method\":\"objects/query\",\"params\":{\"objects\":{\"bed_mesh\":[\"profile_name\",\"probed_matrix\",\"mesh_min\",\"mesh_max\"]}}}\003";
static int plates_pair(const char *obj, const char *end, const char *key, double *x, double *y) {
    const char *p = json_member(obj, end, key);
    if (!p || *p != '[') return 0;
    p = json_skip_space(p + 1, end);
    if (!plates_json_double(p, end, x, &p)) return 0;
    p = json_skip_space(p, end);
    if (p >= end || *p++ != ',') return 0;
    p = json_skip_space(p, end);
    if (!plates_json_double(p, end, y, &p)) return 0;
    p = json_skip_space(p, end);
    return p < end && *p == ']';
}
static int plates_active_reply(char side, const plate_mesh *expected, const char *reply, size_t length) {
    const char *end=reply+length, *root=json_skip_space(reply,end), *e0,*e1,*e2,*e3;
    if (root>=end || *root!='{' || !(e0=json_container_end(root,end))) return 0;
    const char *result=json_member_object(root,e0,"result",'{',&e1);
    const char *status=result?json_member_object(result,e1,"status",'{',&e2):NULL;
    const char *bed=status?json_member_object(status,e2,"bed_mesh",'{',&e3):NULL;
    const char *name=NULL; int count=0;
    if (!bed || !json_member_raw_string(bed,e3,"profile_name",&name,&count) ||
        (size_t)count!=strlen(plate_slot(side)) || memcmp(name,plate_slot(side),(size_t)count)) return 0;
    const char *points=json_member(bed,e3,"probed_matrix");
    plate_mesh actual=*expected;
    return points && plates_json_points(points,e3,&actual) &&
        plates_pair(bed,e3,"mesh_min",&actual.min_x,&actual.min_y) &&
        plates_pair(bed,e3,"mesh_max",&actual.max_x,&actual.max_y) &&
        plate_mesh_valid(&actual) && plate_mesh_equal(expected,&actual,0);
}
static int plates_activate_slot(char side, const plate_mesh *expected);

/* A gcode/script reply without an error. */
static int plates_reply_ok(const char *reply, size_t length) {
    const char *end = reply + length, *root = json_skip_space(reply, end);
    const char *root_end = root < end && *root == '{' ? json_container_end(root, end) : NULL;
    return root_end && json_member(root, root_end, "result") && !json_member(root, root_end, "error");
}

/* Runs one script of JSON-escaped G-code lines; 0 when the printer accepted it. */
static int plates_script(const char *script) {
    char query[256], *reply = NULL; size_t length = 0;
    if (snprintf(query, sizeof(query), "{\"id\":204,\"method\":\"gcode/script\",\"params\":{\"script\":\"%s\"}}\003",
                 script) >= (int)sizeof(query)) return -1;
    if (plates_uds(query, &reply, &length) != 0 || !reply) return -1;
    int ok = plates_reply_ok(reply, length);
    free(reply);
    return ok ? 0 : -1;
}

/* An RPC acknowledgement alone does not prove which matrix the printer applies. */
static int plates_activate_slot(char side, const plate_mesh *expected) {
    char script[80], *reply=NULL; size_t length=0;
    snprintf(script,sizeof(script),"BED_MESH_PROFILE LOAD=%s",plate_slot(side));
    if (plates_script(script)!=0) return 0;
    if (plates_uds(plates_active_query,&reply,&length)!=0 || !reply) return 0;
    int ok=plates_active_reply(side,expected,reply,length); free(reply); return ok;
}

/* SET_GCODE_OFFSET Z= without MOVE: nothing moves, the next move uses the new offset. */
static int plates_accept_z(double z,const char *reply,size_t length) {
    if (!plates_reply_ok(reply, length)) return -1;
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

/* ---- printer profiles of measurements ------------------------------------------- */

/* 1 when m has its own printer profile holding its mesh. */
static int plates_has_profile(const plate_measure *m) {
    char name[32]; plate_mesh saved;
    plate_profile_name(name, sizeof(name), m);
    return autosave_profile(name, &saved) == 1 && plate_mesh_equal(&saved, &m->mesh, 1);
}

/* The slot of `side` holds m's mesh: copy it to m's own printer profile, which a
 * print can load without a restart. SAVE writes autosave.cfg at once; reading the
 * file back confirms it. 1 stored, 0 not. */
static int plates_store_profile(char side, const plate_measure *m) {
    char name[32], script[128];
    plate_profile_name(name, sizeof(name), m);
    snprintf(script, sizeof(script), "BED_MESH_PROFILE LOAD=%s\\nBED_MESH_PROFILE SAVE=%s", plate_slot(side), name);
    return plates_script(script) == 0 && plates_has_profile(m);
}

/* The measurement whose mesh the side's slot holds in the file, if any. */
static plate_measure *plates_slot_measure(char side) {
    plate_mesh slot;
    if (autosave_slot(side, &slot) != 1) return NULL;
    for (int i = 0; i < plates.measure_count; ++i) {
        const plate_entry *p = plate_find(plates.measures[i].plate);
        if (p && p->side == side && plate_mesh_equal(&slot, &plates.measures[i].mesh, 1)) return &plates.measures[i];
    }
    return NULL;
}

/* Before the side's slot is replaced, the measurement it holds keeps a printer profile. */
static int plates_keep_slot(char side) {
    const plate_measure *m = plates_slot_measure(side);
    return !m || plates_has_profile(m) || plates_store_profile(side, m);
}

/* A deleted measurement's profile leaves printer memory; the file follows at the
 * firmware's next save. Best effort: a profile left behind is never loaded. */
static void plates_drop_profile(const char *id) {
    char script[96];
    snprintf(script, sizeof(script), "BED_MESH_PROFILE REMOVE=" PLATE_PROFILE_PREFIX "%.16s", id);
    (void)plates_script(script);
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

static void plates_nozzle_json(json_builder *b, const plate_nozzle *n) {
    json_builder_printf(b, "{\"id\":\"%s\",\"name\":", n->id);
    json_builder_string(b, n->name);
    json_builder_printf(b, ",\"diameter\":%.2f,\"z_offset\":%.3f}", n->diameter, n->z);
}

static void plates_plate_json(json_builder *b, const plate_entry *p) {
    json_builder_printf(b, "{\"id\":\"%s\",\"name\":", p->id);
    json_builder_string(b, p->name);
    json_builder_printf(b, ",\"side\":\"%c\",\"z_offset\":%.3f,\"measure\":\"%s\"", p->side, p->z, p->measure);
}

/* slot/profile: -1 leaves the field out (the library file), else whether the mesh is there. */
static void plates_measure_json(json_builder *b, const plate_measure *m, int with_plate, int slot, int profile) {
    json_builder_printf(b, "{\"id\":\"%s\",", m->id);
    if (with_plate) json_builder_printf(b, "\"plate\":\"%s\",", m->plate);
    json_builder_printf(b, "\"temp\":%d,\"nozzle\":\"%s\",\"measured\":%lld,", m->temp, m->nozzle, m->measured);
    if (slot >= 0) json_builder_printf(b, "\"slot\":%s,", slot ? "true" : "false");
    if (profile >= 0) json_builder_printf(b, "\"profile\":%s,", profile ? "true" : "false");
    json_builder_printf(b, "\"mesh\":");
    plates_mesh_json(b, &m->mesh);
    json_builder_printf(b, "}");
}

static char *plates_encode(const plate_store *store, size_t *length) {
    json_builder b = {malloc(PLATES_FILE_MAX), 0, PLATES_FILE_MAX, 0};
    if (!b.data) return NULL;
    json_builder_printf(&b, "{\"version\":2,\"current\":\"%s\",\"pending\":\"%s\",\"nozzle\":\"%s\",\"nozzles\":[",
                        store->current, store->pending, store->nozzle);
    for (int i = 0; i < store->nozzle_count; ++i) {
        if (i) json_builder_printf(&b, ",");
        plates_nozzle_json(&b, &store->nozzles[i]);
    }
    json_builder_printf(&b, "],\"plates\":[");
    int written = 0;
    for (int i = 0; i < store->count; ++i) {
        if (!store->plates[i].measure[0]) continue;
        if (written++) json_builder_printf(&b, ",");
        plates_plate_json(&b, &store->plates[i]);
        json_builder_printf(&b, "}");
    }
    /* Older apps keep reading calibrated plates; unfinished profiles are separate. */
    json_builder_printf(&b, "],\"uncalibrated\":["); written = 0;
    for (int i = 0; i < store->count; ++i) {
        if (store->plates[i].measure[0]) continue;
        if (written++) json_builder_printf(&b, ",");
        plates_plate_json(&b, &store->plates[i]); json_builder_printf(&b, "}");
    }
    json_builder_printf(&b, "],\"measures\":[");
    for (int i = 0; i < store->measure_count; ++i) {
        if (i) json_builder_printf(&b, ",");
        plates_measure_json(&b, &store->measures[i], 1, -1, -1);
    }
    json_builder_printf(&b, "]}\n");
    if (b.failed) { free(b.data); return NULL; }
    *length = b.length;
    return b.data;
}

static int plates_save(void) {
    size_t length = 0;
    char *data = plates_encode(&plates, &length);
    if (!data) return -1;
    int result = plates_replace_file(plates_path, NULL, data, length, 0644);
    free(data);
    return result;
}

static int plates_parse_id(const char *obj, const char *end, const char *key, char out[PLATE_ID_LEN + 1]) {
    const char *text; int length;
    out[0] = 0;
    if (!json_member_raw_string(obj, end, key, &text, &length) || (length && length != PLATE_ID_LEN)) return 0;
    memcpy(out, text, (size_t)length); out[length] = 0;
    return !length || plate_id_valid(out);
}

static int plates_parse_name(const char *obj, const char *end, char out[PLATE_NAME_MAX + 1]) {
    const char *text; int length;
    memset(out, 0, PLATE_NAME_MAX + 1);
    if (!json_member_raw_string(obj, end, "name", &text, &length) || length <= 0 || length > PLATE_NAME_MAX) return 0;
    memcpy(out, text, (size_t)length);
    return plate_name_valid(out);
}

static int plates_parse_mesh(const char *obj, const char *end, plate_mesh *m) {
    const char *mesh_end, *mesh = json_member_object(obj, end, "mesh", '{', &mesh_end);
    const char *points = mesh ? json_member(mesh, mesh_end, "points") : NULL;
    return points && plates_json_points(points, mesh_end, m) && plates_json_params(mesh, mesh_end, m) &&
           plates_member_double(mesh, mesh_end, "offset", &m->offset) && plate_mesh_valid(m);
}

/* A plate; in a version 1 file it carries its only mesh, which becomes a measurement. */
static int plates_parse_plate(const char *obj, const char *end, int version, plate_entry *p, plate_measure *legacy) {
    const char *text; int length;
    memset(p, 0, sizeof(*p));
    if (!plates_parse_id(obj, end, "id", p->id) || !p->id[0] || !plates_parse_name(obj, end, p->name) ||
        !json_member_raw_string(obj, end, "side", &text, &length) || length != 1 || (text[0] != 'A' && text[0] != 'B'))
        return 0;
    p->side = text[0];
    if (!plates_member_double(obj, end, "z_offset", &p->z) || fabs(p->z) > PLATE_Z_LIMIT + 1e-9) return 0;
    if (version >= 2) return plates_parse_id(obj, end, "measure", p->measure) && (version == 3 || p->measure[0]);
    double measured;
    memset(legacy, 0, sizeof(*legacy));
    if (!plates_member_double(obj, end, "measured", &measured) || measured < 0) return 0;
    memcpy(legacy->plate, p->id, sizeof(legacy->plate));
    legacy->temp = PLATE_TEMP_V1; legacy->measured = (long long)measured;
    return plates_parse_mesh(obj, end, &legacy->mesh);
}

static int plates_parse_measure(const char *obj, const char *end, plate_measure *m) {
    double measured;
    memset(m, 0, sizeof(*m));
    if (!plates_parse_id(obj, end, "id", m->id) || !m->id[0] || !plates_parse_id(obj, end, "plate", m->plate) ||
        !m->plate[0] || !json_member_int(obj, end, "temp", &m->temp) || m->temp < PLATE_TEMP_MIN ||
        m->temp > PLATE_TEMP_MAX || !plates_parse_id(obj, end, "nozzle", m->nozzle) ||
        !plates_member_double(obj, end, "measured", &measured) || measured < 0) return 0;
    m->measured = (long long)measured;
    return plates_parse_mesh(obj, end, &m->mesh);
}

static int plates_parse_nozzle(const char *obj, const char *end, plate_nozzle *n) {
    memset(n, 0, sizeof(*n));
    return plates_parse_id(obj, end, "id", n->id) && n->id[0] && plates_parse_name(obj, end, n->name) &&
           plates_member_double(obj, end, "diameter", &n->diameter) && n->diameter >= 0.1 - 1e-9 &&
           n->diameter <= 2.0 + 1e-9 && plates_member_double(obj, end, "z_offset", &n->z) &&
           fabs(n->z) <= PLATE_NOZZLE_Z_LIMIT + 1e-9;
}

/* An id is used once across plates, measurements and nozzles. */
static int plates_id_taken(const char *id) { return plate_find(id) || measure_find(id) || nozzle_find(id); }

/* `out` may be the id field of an entry already counted, so the candidate is built apart. */
static void plates_new_id(char out[PLATE_ID_LEN + 1]) {
    static unsigned long long counter;
    char id[PLATE_ID_LEN + 1];
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
        for (int i = 0; i < 8; ++i) snprintf(id + i * 2, 3, "%02x", bytes[i]);
    } while (plates_id_taken(id));
    memcpy(out, id, sizeof(id));
}

/* Each list of the root object, element by element; 0 when an element does not parse. */
typedef int (*plates_element)(const char *obj, const char *end, void *context);
static int plates_parse_list(const char *root, const char *root_end, const char *key, plates_element parse, void *context) {
    const char *list_end, *list = json_member_object(root, root_end, key, '[', &list_end);
    if (!list) return 0;
    for (const char *item = json_next_element(list, list_end); item;) {
        const char *item_end = json_container_end(item, list_end);
        if (!item_end || !parse(item, item_end, context)) return 0;
        item = json_next_element(item_end, list_end);
    }
    return 1;
}
static int plates_load_nozzle(const char *obj, const char *end, void *context) {
    (void)context;
    if (plates.nozzle_count >= PLATE_NOZZLES_MAX) return 0;
    plate_nozzle *n = &plates.nozzles[plates.nozzle_count];
    if (!plates_parse_nozzle(obj, end, n) || plates_id_taken(n->id)) return 0;
    for (int i = 0; i < plates.nozzle_count; ++i) if (!strcmp(plates.nozzles[i].name, n->name)) return 0;
    plates.nozzle_count++;
    return 1;
}
static int plates_load_plate(const char *obj, const char *end, void *context) {
    int version = *(int *)context;
    if (plates.count >= PLATES_MAX || (version < 2 && plates.measure_count >= PLATE_MEASURES_MAX)) return 0;
    plate_entry *p = &plates.plates[plates.count];
    plate_measure *legacy = &plates.measures[plates.measure_count];
    if (!plates_parse_plate(obj, end, version, p, legacy) || plates_id_taken(p->id)) return 0;
    for (int i = 0; i < plates.count; ++i) if (!strcmp(plates.plates[i].name, p->name)) return 0;
    plates.count++;
    if (version < 2) plates.measure_count++; /* its id is given once every id is known */
    return 1;
}
static int plates_load_measure(const char *obj, const char *end, void *context) {
    (void)context;
    if (plates.measure_count >= PLATE_MEASURES_MAX) return 0;
    plate_measure *m = &plates.measures[plates.measure_count];
    if (!plates_parse_measure(obj, end, m) || plates_id_taken(m->id)) return 0;
    plates.measure_count++;
    return 1;
}
static int plates_load_uncalibrated(const char *obj, const char *end, void *context) {
    (void)context;
    int draft = 3;
    return plates_load_plate(obj, end, &draft) && !plates.plates[plates.count - 1].measure[0];
}

/* Every reference points at an entry of the right kind. */
static int plates_consistent(void) {
    for (int i = 0; i < plates.measure_count; ++i) {
        const plate_measure *m = &plates.measures[i];
        if (!plate_find(m->plate) || !plate_nozzle_ref(m->nozzle)) return 0;
    }
    for (int i = 0; i < plates.count; ++i) {
        const plate_entry *p = &plates.plates[i];
        if (p->measure[0]) { if (!plate_base(p)) return 0; }
        else if (plate_measure_count(p->id) || !strcmp(plates.current, p->id) || !strcmp(plates.pending, p->id)) return 0;
    }
    return (!plates.current[0] || plate_find(plates.current)) && (!plates.pending[0] || plate_find(plates.pending)) &&
           plate_nozzle_ref(plates.nozzle);
}

/* A missing file is an empty library; anything unreadable keeps the library closed.
 * A version 1 file is rewritten once, with each plate's mesh as a measurement. */
static void plates_load(void) {
    plates_pending_stage = 0;
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
    const char *end = text + length, *root = json_skip_space(text, end), *root_end;
    int version = 0, ok = 0;
    if (root < end && *root == '{' && (root_end = json_container_end(root, end)) &&
        json_member_int(root, root_end, "version", &version) && (version == 1 || version == 2) &&
        plates_parse_id(root, root_end, "current", plates.current) &&
        plates_parse_id(root, root_end, "pending", plates.pending)) {
        if (version == 2)
            ok = plates_parse_id(root, root_end, "nozzle", plates.nozzle) &&
                 plates_parse_list(root, root_end, "nozzles", plates_load_nozzle, NULL) &&
                 plates_parse_list(root, root_end, "plates", plates_load_plate, &version) &&
                 plates_parse_list(root, root_end, "measures", plates_load_measure, NULL) &&
                 (!json_member(root, root_end, "uncalibrated") ||
                  plates_parse_list(root, root_end, "uncalibrated", plates_load_uncalibrated, NULL));
        else {
            ok = plates_parse_list(root, root_end, "plates", plates_load_plate, &version);
            for (int i = 0; ok && i < plates.measure_count; ++i) {
                plates_new_id(plates.measures[i].id);
                memcpy(plates.plates[i].measure, plates.measures[i].id, sizeof(plates.plates[i].measure));
            }
        }
        ok = ok && plates_consistent();
    }
    free(text);
    if (!ok) { memset(&plates, 0, sizeof(plates)); return; }
    plates_available = 1; plates_error = "";
    if (version == 1 && plates_save() != 0)
        fprintf(stderr, "Plate library: cannot rewrite %s in the current format\n", plates_path);
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
    if (plates_cal.stage && plates_cal.stage != CAL_SAVING) return "A bed mesh calibration is running";
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

/* Changes are made on the store, then saved; a failed save puts the store back. */
static void plates_begin(void) { plates_undo = plates; }
static int plates_commit(int fd) {
    if (plates_save() == 0) return 1;
    plates = plates_undo;
    plates_fail(fd, 500, "Cannot save the plate library");
    return 0;
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

/* Late loading of a measurement during a print start; see plates_late_tick. */
typedef struct {int fd,connecting,id;size_t sent,used;long long deadline;char request[512];char *buffer;} plates_exchange;
static struct {
    int state; /* 0 off, 1 waiting for the print to start, 2 printing before the first layer */
    int layer_reset, loads;
    char measure[PLATE_ID_LEN + 1], profile[32], slot[16];
    long long armed, sent;
    plates_exchange x;
} plates_late = {.x = {.fd = -1}};
/* "", "loaded", "missed", "adaptive", "failed", "not_started" or "ended". */
static const char *plates_late_result = "";

static void plates_get_response(int fd) {
    size_t file_length = 0;
    char *file = plates_read_file(printer_autosave_path, AUTOSAVE_FILE_MAX, &file_length);
    plate_mesh slots[2], saved; int present[2];
    for (int i = 0; i < 2; ++i) present[i] = file ? autosave_find(file, file_length, plate_slot(i ? 'B' : 'A'), &slots[i]) : -1;
    json_builder b = {malloc(PLATES_FILE_MAX + 8192), 0, PLATES_FILE_MAX + 8192, 0};
    if (!b.data) { free(file); plates_fail(fd, 500, "Out of memory"); return; }
    const plate_entry *current = plates.current[0] ? plate_find(plates.current) : NULL;
    const char *active = uds_mesh_profile(&telemetry);
    json_builder_printf(&b, "{\"available\":%s,\"error\":", plates_available ? "true" : "false");
    json_builder_string(&b, plates_error);
    json_builder_printf(&b, ",\"current\":\"%s\",\"pending\":\"%s\",\"result\":\"%s\",\"z_applied\":%s,",
        plates.current, plates.pending, plates_result,
        current && plates_z_valid && plate_close(plates_z_applied, plate_z_now(current)) ? "true" : "false");
    if (current) json_builder_printf(&b, "\"z_effective\":%.3f,", plate_z_now(current));
    else json_builder_printf(&b, "\"z_effective\":null,");
    json_builder_printf(&b, "\"nozzle\":\"%s\",\"mesh_profile\":", plates.nozzle);
    if (active) json_builder_string(&b, active); else json_builder_printf(&b, "null");
    static const char *stages[] = {"off", "homing", "heating", "soaking", "probing", "saving"};
    double bed = 0; int have_bed = uds_value(&telemetry, U_BT, &bed);
    long long left = plates_cal.stage == CAL_SOAKING ? (plates_cal.soak_until - monotonic_ms() + 999) / 1000 : 0;
    json_builder_printf(&b, ",\"calibration\":{\"stage\":\"%s\",\"side\":\"%c\",\"temp\":%d,\"soak\":%d,"
        "\"remaining\":%lld,\"plate\":\"%s\",\"nozzle\":\"%s\",\"measure\":\"%s\",\"result\":\"%s\","
        "\"error\":\"%s\",\"detail\":", stages[plates_cal.stage], plates_cal.side ? plates_cal.side : 'A',
        plates_cal.temp, plates_cal.soak, left > 0 ? left : 0, plates_cal.plate, plates_cal.nozzle,
        plates_cal.measure, plates_cal.result, plates_cal.error);
    json_builder_string(&b, plates_cal.detail);
    if (have_bed) json_builder_printf(&b, ",\"bed\":%.1f}", bed); else json_builder_printf(&b, ",\"bed\":null}");
    json_builder_printf(&b, ",\"print_mesh\":{\"state\":\"%s\",\"measure\":\"%s\",\"result\":\"%s\"},"
        "\"slots\":{\"A\":\"%s\",\"B\":\"%s\"},\"nozzles\":[",
        plates_late.state == 1 ? "waiting" : plates_late.state == 2 ? "active" : "off", plates_late.measure,
        plates_late_result, present[0] < 0 ? "unreadable" : present[0] ? "mesh" : "empty",
        present[1] < 0 ? "unreadable" : present[1] ? "mesh" : "empty");
    for (int i = 0; i < plates.nozzle_count; ++i) {
        if (i) json_builder_printf(&b, ",");
        plates_nozzle_json(&b, &plates.nozzles[i]);
    }
    json_builder_printf(&b, "],\"plates\":[");
    for (int i = 0; i < plates.count; ++i) {
        const plate_entry *p = &plates.plates[i];
        const plate_measure *base = plate_base(p);
        int slot = p->side == 'B';
        if (i) json_builder_printf(&b, ",");
        plates_plate_json(&b, p);
        json_builder_printf(&b, ",\"in_printer\":%s,\"measures\":[",
            base && present[slot] == 1 && plate_mesh_equal(&slots[slot], &base->mesh, 1) ? "true" : "false");
        /* Coolest first, then oldest. */
        int order[PLATE_MEASURES_MAX], count = 0;
        for (int k = 0; k < plates.measure_count; ++k) {
            const plate_measure *m = &plates.measures[k];
            if (strcmp(m->plate, p->id)) continue;
            int at = count++;
            while (at > 0) {
                const plate_measure *o = &plates.measures[order[at - 1]];
                if (o->temp < m->temp || (o->temp == m->temp && o->measured <= m->measured)) break;
                order[at] = order[at - 1]; at--;
            }
            order[at] = k;
        }
        for (int k = 0; k < count; ++k) {
            const plate_measure *m = &plates.measures[order[k]];
            char name[32]; plate_profile_name(name, sizeof(name), m);
            if (k) json_builder_printf(&b, ",");
            plates_measure_json(&b, m, 0, present[slot] == 1 && plate_mesh_equal(&slots[slot], &m->mesh, 1),
                file && autosave_find(file, file_length, name, &saved) == 1 && plate_mesh_equal(&saved, &m->mesh, 1));
        }
        json_builder_printf(&b, "]}");
    }
    json_builder_printf(&b, "]}\n");
    free(file);
    if (b.failed) plates_fail(fd, 500, "Plate library response is too large");
    else respond(fd, 200, "OK", "application/json; charset=utf-8", b.data, b.length);
    free(b.data);
}

/* The slot mesh as the printer uses it: the file and printer memory must agree.
 * NULL, or why not with the HTTP status in *status. */
static const char *plates_capture_mesh(char side, plate_mesh *mesh, int *status) {
    plate_mesh memory;
    int file = autosave_slot(side, mesh);
    *status = file < 0 ? 503 : 409;
    if (file < 0) return "Cannot read the printer mesh file";
    if (!file) return "This side has no saved mesh; run a bed mesh calibration first";
    int live = plates_memory_slot(side, &memory);
    *status = live < 0 ? 503 : 409;
    if (live < 0) return "Cannot read the printer mesh";
    if (!live || !plate_mesh_equal(&memory, mesh, 0))
        return "The printer mesh in memory differs from the saved file; restart the printer first";
    return NULL;
}
static int plates_capture(int fd, char side, plate_mesh *mesh) {
    int status; const char *error = plates_capture_mesh(side, mesh, &status);
    if (error) plates_fail(fd, status, error);
    return !error;
}

static const char *plates_closed(int *status) {
    *status = 409;
    if (plates_background_active()) return "Plate verification is still running";
    if (!plates_available) { *status = 503; return plates_error; }
    if (plates.pending[0]) return "A plate change is waiting for the printer restart";
    return NULL;
}
static int plates_open(int fd) {
    int status; const char *error = plates_closed(&status);
    if (error) plates_fail(fd, status, error);
    return !error;
}

static int plates_name_taken(const char *name, const plate_entry *except) {
    for (int i = 0; i < plates.count; ++i)
        if (&plates.plates[i] != except && !strcmp(plates.plates[i].name, name)) return 1;
    return 0;
}

/* The mounted plate's offset with the selected nozzle, applied now while the
 * printer is idle; otherwise plates_tick applies it once it is. */
static int plates_reapply_z(const mqtt_client *mqtt) {
    const plate_entry *p = plates.current[0] ? plate_find(plates.current) : NULL;
    return p && !plates_printer_ready(mqtt) && !z_offset_pending && plates_apply_z(plate_z_now(p)) == 0;
}

/* Register a surface before its first calibration. No printer mesh is read or written. */
static void plates_create_response(int fd, const mqtt_client *mqtt, const char *body, size_t length) {
    char fields[3][PLATE_NAME_MAX + 1]; double z; const char *reason;
    int count = plates_fields(body, length, fields, 3);
    if (count != 3 || strlen(fields[0]) != 1 || (fields[0][0] != 'A' && fields[0][0] != 'B') ||
        !plate_name_valid(fields[1]) || !plate_z_parse(fields[2], &z)) {
        plates_fail(fd, 400, "Invalid plate"); return;
    }
    if (!plates_open(fd)) return;
    if (plates.count >= PLATES_MAX) { plates_fail(fd, 409, "The plate library is full"); return; }
    if (plates_name_taken(fields[1], NULL)) { plates_fail(fd, 409, "A plate with this name already exists"); return; }
    if ((reason = plates_printer_ready(mqtt))) { plates_fail(fd, 409, reason); return; }
    plates_begin();
    plate_entry *p = &plates.plates[plates.count++];
    memset(p, 0, sizeof(*p)); plates_new_id(p->id);
    snprintf(p->name, sizeof(p->name), "%s", fields[1]); p->side = fields[0][0]; p->z = z;
    if (!plates_commit(fd)) return;
    char reply[96]; snprintf(reply, sizeof(reply), "{\"saved\":true,\"id\":\"%s\",\"measure\":\"\"}\n", p->id);
    plates_reply(fd, 201, reply);
}

/* side \n name \n z [\n temp [\n nozzle]]: a new plate from the mesh the printer now
 * keeps for that side, measured at `temp` (60 when not given) with `nozzle`. */
static void plates_save_response(int fd, const mqtt_client *mqtt, const char *body, size_t length) {
    char fields[5][PLATE_NAME_MAX + 1]; double z; int temp = PLATE_TEMP_V1; const char *reason;
    int count = plates_fields(body, length, fields, 5);
    if (count < 3 || strlen(fields[0]) != 1 || (fields[0][0] != 'A' && fields[0][0] != 'B') ||
        !plate_name_valid(fields[1]) || !plate_z_parse(fields[2], &z) ||
        (count >= 4 && !plate_temp_parse(fields[3], &temp)) || (count == 5 && !plate_nozzle_ref(fields[4]))) {
        plates_fail(fd, 400, "Invalid plate"); return;
    }
    if (!plates_open(fd)) return;
    if (plates.count >= PLATES_MAX) { plates_fail(fd, 409, "The plate library is full"); return; }
    if (plates.measure_count >= PLATE_MEASURES_MAX) { plates_fail(fd, 409, "The plate library has no room for another measurement"); return; }
    if (plates_name_taken(fields[1], NULL)) { plates_fail(fd, 409, "A plate with this name already exists"); return; }
    if ((reason = plates_printer_ready(mqtt))) { plates_fail(fd, 409, reason); return; }
    plate_mesh mesh;
    if (!plates_capture(fd, fields[0][0], &mesh)) return;
    plates_begin();
    plate_entry *p = &plates.plates[plates.count++];
    plate_measure *m = &plates.measures[plates.measure_count++];
    memset(p, 0, sizeof(*p)); memset(m, 0, sizeof(*m));
    plates_new_id(p->id);
    snprintf(p->name, sizeof(p->name), "%s", fields[1]);
    p->side = fields[0][0]; p->z = z;
    plates_new_id(m->id);
    memcpy(m->plate, p->id, sizeof(m->plate));
    m->temp = temp; if (count == 5) memcpy(m->nozzle, fields[4], sizeof(m->nozzle)); /* "" or a checked id */
    m->measured = (long long)time(NULL); m->mesh = mesh;
    memcpy(p->measure, m->id, sizeof(p->measure));
    if (!plates_commit(fd)) return;
    int profile = plates_store_profile(p->side, m);
    char reply[160];
    snprintf(reply, sizeof(reply), "{\"saved\":true,\"id\":\"%s\",\"measure\":\"%s\",\"profile\":%s}\n",
             p->id, m->id, profile ? "true" : "false");
    plates_reply(fd, 201, reply);
}

/* After a calibration on plate `id`, its side's slot becomes the plate's measurement
 * at `temp` with `nozzle` (replacing the one with the same temperature and nozzle),
 * gets its printer profile and is what the plate mounts with. The plate was on the
 * bed, so it is mounted, in place. */
static const char *plates_take_measure(const mqtt_client *mqtt, const char *id, int temp, const char *nozzle,
                                       int mounted_only, int *status, plate_measure **out, int *profile, int *applied) {
    const char *reason;
    if ((reason = plates_closed(status))) return reason;
    plate_entry *p = plate_find(id);
    *status = 404;
    if (!p) return "Unknown plate";
    *status = 409;
    if (mounted_only && strcmp(plates.current, p->id)) return "Only the mounted plate can take the printer mesh";
    plate_measure *m = NULL;
    for (int i = 0; i < plates.measure_count && !m; ++i) {
        plate_measure *o = &plates.measures[i];
        if (!strcmp(o->plate, p->id) && o->temp == temp && !strcmp(o->nozzle, nozzle)) m = o;
    }
    if (!m && plates.measure_count >= PLATE_MEASURES_MAX) return "The plate library has no room for another measurement";
    if ((reason = plates_printer_ready(mqtt))) return reason;
    if (z_offset_pending) return "A Z offset change is still being confirmed";
    plate_mesh mesh;
    if ((reason = plates_capture_mesh(p->side, &mesh, status))) return reason;
    plates_begin();
    if (!m) {
        m = &plates.measures[plates.measure_count++];
        memset(m, 0, sizeof(*m));
        plates_new_id(m->id);
        memcpy(m->plate, p->id, sizeof(m->plate));
        m->temp = temp; snprintf(m->nozzle, sizeof(m->nozzle), "%s", nozzle);
    }
    m->mesh = mesh; m->measured = (long long)time(NULL);
    memcpy(p->measure, m->id, sizeof(p->measure));
    memcpy(plates.current, p->id, sizeof(plates.current));
    if (plates_save() != 0) { plates = plates_undo; *status = 500; return "Cannot save the plate library"; }
    plates_result = "mounted";
    *profile = plates_store_profile(p->side, m);
    *applied = plates_apply_z(plate_z_now(p)) == 0;
    *out = m;
    return NULL;
}
static void plates_measure_into(int fd, const mqtt_client *mqtt, const char *id, int temp, const char *nozzle,
                                int mounted_only) {
    int status = 500, profile = 0, applied = 0; plate_measure *m = NULL;
    const char *error = plates_take_measure(mqtt, id, temp, nozzle, mounted_only, &status, &m, &profile, &applied);
    if (error) { plates_fail(fd, status, error); return; }
    char reply[160];
    snprintf(reply, sizeof(reply), "{\"saved\":true,\"measure\":\"%s\",\"profile\":%s,\"applied\":%s}\n",
             m->id, profile ? "true" : "false", applied ? "true" : "false");
    plates_reply(fd, 200, reply);
}

/* plate \n temp [\n nozzle] */
static void plates_measure_response(int fd, const mqtt_client *mqtt, const char *body, size_t length) {
    char fields[3][PLATE_NAME_MAX + 1]; int temp;
    int count = plates_fields(body, length, fields, 3);
    if (count < 2 || !plate_id_valid(fields[0]) || !plate_temp_parse(fields[1], &temp) ||
        (count == 3 && !plate_nozzle_ref(fields[2]))) { plates_fail(fd, 400, "Invalid measurement"); return; }
    plates_measure_into(fd, mqtt, fields[0], temp, count == 3 ? fields[2] : "", 0);
}

/* id \n temp \n nozzle: corrects what a measurement records, e.g. the nozzle of one made before nozzles
 * were listed. Its mesh and printer profile stay as they are. */
static void plates_measure_edit_response(int fd, const char *body, size_t length) {
    char fields[3][PLATE_NAME_MAX + 1] = {{0}}; int temp;
    int count = plates_fields(body, length, fields, 3);
    if (count < 2 || !plate_id_valid(fields[0]) || !plate_temp_parse(fields[1], &temp) ||
        !plate_nozzle_ref(fields[2])) { plates_fail(fd, 400, "Invalid measurement"); return; }
    if (!plates_open(fd)) return;
    plate_measure *m = measure_find(fields[0]);
    if (!m) { plates_fail(fd, 404, "Unknown measurement"); return; }
    for (int i = 0; i < plates.measure_count; ++i) {
        const plate_measure *o = &plates.measures[i];
        if (o != m && !strcmp(o->plate, m->plate) && o->temp == temp && !strcmp(o->nozzle, fields[2])) {
            plates_fail(fd, 409, "The plate already has a measurement at this temperature with this nozzle"); return;
        }
    }
    plates_begin();
    m->temp = temp; memcpy(m->nozzle, fields[2], sizeof(m->nozzle)); /* "" or a checked id */
    if (!plates_commit(fd)) return;
    plates_reply(fd, 200, "{\"saved\":true}\n");
}

/* id: the mounted plate's measurement takes the mesh the printer now keeps for its side, after a new calibration. */
static void plates_recapture_response(int fd, const mqtt_client *mqtt, const char *body, size_t length) {
    char fields[1][PLATE_NAME_MAX + 1];
    if (plates_fields(body, length, fields, 1) != 1 || !plate_id_valid(fields[0])) { plates_fail(fd, 400, "Invalid plate"); return; }
    const plate_entry *p = plate_find(fields[0]);
    const plate_measure *base = p ? plate_base(p) : NULL;
    char nozzle[PLATE_ID_LEN + 1] = "";
    if (base) memcpy(nozzle, base->nozzle, sizeof(nozzle));
    plates_measure_into(fd, mqtt, fields[0], base ? base->temp : PLATE_TEMP_V1, nozzle, 1);
}

/* id: one measurement leaves the library; a plate keeps at least one. */
static void plates_measure_delete_response(int fd, const mqtt_client *mqtt, const char *body, size_t length) {
    char fields[1][PLATE_NAME_MAX + 1];
    if (plates_fields(body, length, fields, 1) != 1 || !plate_id_valid(fields[0])) { plates_fail(fd, 400, "Invalid measurement"); return; }
    if (!plates_open(fd)) return;
    plate_measure *m = measure_find(fields[0]);
    if (!m) { plates_fail(fd, 404, "Unknown measurement"); return; }
    plate_entry *p = plate_find(m->plate);
    if (!p || plate_measure_count(p->id) < 2) { plates_fail(fd, 409, "A plate keeps at least one measurement; delete the plate instead"); return; }
    plates_begin();
    char removed[PLATE_ID_LEN + 1]; memcpy(removed, m->id, sizeof(removed));
    int index = (int)(m - plates.measures);
    memmove(m, m + 1, (size_t)(plates.measure_count - index - 1) * sizeof(*m));
    plates.measure_count--;
    if (!strcmp(p->measure, removed)) { /* the newest one left becomes what the plate mounts with */
        const plate_measure *newest = NULL;
        for (int i = 0; i < plates.measure_count; ++i)
            if (!strcmp(plates.measures[i].plate, p->id) && (!newest || plates.measures[i].measured > newest->measured))
                newest = &plates.measures[i];
        memcpy(p->measure, newest->id, sizeof(p->measure));
    }
    if (!plates_commit(fd)) return;
    if (!plates_printer_ready(mqtt)) plates_drop_profile(removed);
    plates_reply(fd, 200, "{\"deleted\":true}\n");
}

/* side: before a calibration replaces the slot, the measurement it holds gets its printer profile. */
static void plates_keep_response(int fd, const mqtt_client *mqtt, const char *body, size_t length) {
    char fields[1][PLATE_NAME_MAX + 1]; const char *reason;
    if (plates_fields(body, length, fields, 1) != 1 || strlen(fields[0]) != 1 || (fields[0][0] != 'A' && fields[0][0] != 'B')) {
        plates_fail(fd, 400, "Invalid side"); return;
    }
    if (!plates_open(fd)) return;
    if ((reason = plates_printer_ready(mqtt))) { plates_fail(fd, 409, reason); return; }
    const plate_measure *m = plates_slot_measure(fields[0][0]);
    int kept = !m || plates_has_profile(m) || plates_store_profile(fields[0][0], m);
    char reply[96];
    snprintf(reply, sizeof(reply), "{\"measure\":\"%s\",\"profile\":%s}\n", m ? m->id : "", kept && m ? "true" : "false");
    plates_reply(fd, kept ? 200 : 503, reply);
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
    plates_begin();
    snprintf(p->name, sizeof(p->name), "%s", fields[1]); p->z = z;
    if (!plates_commit(fd)) return;
    int applied = !strcmp(plates.current, p->id) && plates_reapply_z(mqtt);
    plates_reply(fd, 200, applied ? "{\"saved\":true,\"applied\":true}\n" : "{\"saved\":true,\"applied\":false}\n");
}

static void plates_delete_response(int fd, const mqtt_client *mqtt, const char *body, size_t length) {
    char fields[1][PLATE_NAME_MAX + 1];
    if (plates_fields(body, length, fields, 1) != 1 || !plate_id_valid(fields[0])) { plates_fail(fd, 400, "Invalid plate"); return; }
    if (!plates_open(fd)) return;
    plate_entry *p = plate_find(fields[0]);
    if (!p) { plates_fail(fd, 404, "Unknown plate"); return; }
    plates_begin();
    char id[PLATE_ID_LEN + 1]; memcpy(id, p->id, sizeof(id));
    int was_current = !strcmp(plates.current, id);
    memmove(p, p + 1, (size_t)(plates.count - (int)(p - plates.plates) - 1) * sizeof(*p));
    plates.count--;
    char removed[PLATE_MEASURES_MAX][PLATE_ID_LEN + 1]; int dropped = 0;
    for (int i = 0; i < plates.measure_count;) {
        if (strcmp(plates.measures[i].plate, id)) { i++; continue; }
        memcpy(removed[dropped++], plates.measures[i].id, PLATE_ID_LEN + 1);
        memmove(&plates.measures[i], &plates.measures[i + 1], (size_t)(plates.measure_count - i - 1) * sizeof(plates.measures[0]));
        plates.measure_count--;
    }
    if (was_current) plates.current[0] = 0;
    if (!plates_commit(fd)) return;
    if (was_current) plates_result = "";
    if (!plates_printer_ready(mqtt)) for (int i = 0; i < dropped; ++i) plates_drop_profile(removed[i]);
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

/* [id] \n name \n diameter \n z: a new nozzle (empty id) or a changed one. A new
 * correction of the selected nozzle applies at once while the printer is idle. */
static void plates_nozzle_save_response(int fd, const mqtt_client *mqtt, const char *body, size_t length) {
    char fields[4][PLATE_NAME_MAX + 1]; double diameter, z;
    if (plates_fields(body, length, fields, 4) != 4 || (fields[0][0] && !plate_id_valid(fields[0])) ||
        !plate_name_valid(fields[1]) || !plate_decimal_parse(fields[2], 0.1, 2.0, &diameter) ||
        !plate_decimal_parse(fields[3], -PLATE_NOZZLE_Z_LIMIT, PLATE_NOZZLE_Z_LIMIT, &z)) {
        plates_fail(fd, 400, "Invalid nozzle"); return;
    }
    if (!plates_open(fd)) return;
    plate_nozzle *n = fields[0][0] ? nozzle_find(fields[0]) : NULL;
    if (fields[0][0] && !n) { plates_fail(fd, 404, "Unknown nozzle"); return; }
    if (!n && plates.nozzle_count >= PLATE_NOZZLES_MAX) { plates_fail(fd, 409, "The nozzle list is full"); return; }
    for (int i = 0; i < plates.nozzle_count; ++i)
        if (&plates.nozzles[i] != n && !strcmp(plates.nozzles[i].name, fields[1])) {
            plates_fail(fd, 409, "A nozzle with this name already exists"); return;
        }
    plates_begin();
    if (!n) { n = &plates.nozzles[plates.nozzle_count++]; memset(n, 0, sizeof(*n)); plates_new_id(n->id); }
    snprintf(n->name, sizeof(n->name), "%s", fields[1]);
    n->diameter = round(diameter * 100.0) / 100.0; n->z = z;
    if (!plates_commit(fd)) return;
    int applied = !strcmp(plates.nozzle, n->id) && plates_reapply_z(mqtt);
    char reply[128];
    snprintf(reply, sizeof(reply), "{\"saved\":true,\"id\":\"%s\",\"applied\":%s}\n", n->id, applied ? "true" : "false");
    plates_reply(fd, 200, reply);
}

/* id: the nozzle leaves the list; measurements made with it no longer name a nozzle. */
static void plates_nozzle_delete_response(int fd, const mqtt_client *mqtt, const char *body, size_t length) {
    char fields[1][PLATE_NAME_MAX + 1];
    if (plates_fields(body, length, fields, 1) != 1 || !plate_id_valid(fields[0])) { plates_fail(fd, 400, "Invalid nozzle"); return; }
    if (!plates_open(fd)) return;
    plate_nozzle *n = nozzle_find(fields[0]);
    if (!n) { plates_fail(fd, 404, "Unknown nozzle"); return; }
    plates_begin();
    int selected = !strcmp(plates.nozzle, n->id);
    for (int i = 0; i < plates.measure_count; ++i)
        if (!strcmp(plates.measures[i].nozzle, n->id)) plates.measures[i].nozzle[0] = 0;
    if (selected) plates.nozzle[0] = 0;
    memmove(n, n + 1, (size_t)(plates.nozzle_count - (int)(n - plates.nozzles) - 1) * sizeof(*n));
    plates.nozzle_count--;
    if (!plates_commit(fd)) return;
    if (selected) (void)plates_reapply_z(mqtt);
    plates_reply(fd, 200, "{\"deleted\":true}\n");
}

/* id or empty: the nozzle on the printer now. */
static void plates_nozzle_select_response(int fd, const mqtt_client *mqtt, const char *body, size_t length) {
    char fields[1][PLATE_NAME_MAX + 1] = {{0}};
    int count = plates_fields(body, length, fields, 1);
    if (count < 0 || (count == 1 && !plate_nozzle_ref(fields[0]))) { plates_fail(fd, 400, "Invalid nozzle"); return; }
    if (!plates_open(fd)) return;
    plates_begin();
    memcpy(plates.nozzle, fields[0], sizeof(plates.nozzle)); /* "" or a checked id */
    if (!plates_commit(fd)) return;
    plates_reply(fd, 200, plates_reapply_z(mqtt) ? "{\"selected\":true,\"applied\":true}\n" : "{\"selected\":true,\"applied\":false}\n");
}

/* A print started on the screen or a slicer while the reboot waited must not be cut off. */
static int plates_reboot_guard(void) {
    time_t now=time(NULL);const mqtt_client *m=plates_mqtt;
    return m && m->connected && m->registered && m->have_machine_status && m->machine_status==1 &&
        m->last_message>0 && now>=m->last_message && now-m->last_message<=15 && uds_fresh(&telemetry) &&
        !plates_console_busy() && !atomic_load(&upload_busy) && !atomic_load(&active_downloads) && !z_offset_pending;
}

/* id [\n measurement] [\n REBOOT]: mount a plate, with the given measurement or the
 * one it mounted with last. Its Z offset applies at once. A mesh that is not in its
 * slot is written to autosave.cfg and needs a printer restart, which REBOOT
 * confirms; without it the reply only says that a restart is required. */
/* A failed activation must never leave a green mounted claim for a different mesh. */
static void plates_mount_unverified(void) {
    plates.current[0]=0; plates.pending[0]=0; plates_pending_stage=0;
    plates_z_valid=0; plates_result="verify_failed";
    if (plates_save()!=0) { plates_available=0; plates_error="Cannot record unverified plate activation"; }
}

static void plates_mount_response(int fd, const mqtt_client *mqtt, const char *body, size_t length) {
    char fields[3][PLATE_NAME_MAX + 1]; const char *reason, *chosen = NULL;
    int count = plates_fields(body, length, fields, 3), reboot = 0;
    if (count < 1 || !plate_id_valid(fields[0])) { plates_fail(fd, 400, "Invalid plate"); return; }
    for (int i = 1; i < count; ++i) {
        if (!reboot && !strcmp(fields[i], "REBOOT")) reboot = 1;
        else if (!chosen && plate_id_valid(fields[i])) chosen = fields[i];
        else { plates_fail(fd, 400, "Invalid plate"); return; }
    }
    if (!plates_open(fd)) return;
    plate_entry *p = plate_find(fields[0]);
    if (!p) { plates_fail(fd, 404, "Unknown plate"); return; }
    plate_measure *base = chosen ? measure_find(chosen) : plate_base(p);
    if (!base || strcmp(base->plate, p->id)) { plates_fail(fd, 404, "Unknown measurement"); return; }
    if ((reason = plates_printer_ready(mqtt))) { plates_fail(fd, 409, reason); return; }
    if (z_offset_pending) { plates_fail(fd, 409, "A Z offset change is still being confirmed"); return; }
    /* The measurement now in the slot keeps a profile before the restart replaces it. */
    if (reboot) (void)plates_keep_slot(p->side);
    size_t file_length = 0, new_length = 0, start, end; plate_mesh slot; int unknown;
    char *file = plates_read_file(printer_autosave_path, AUTOSAVE_FILE_MAX, &file_length);
    if (!file) { plates_fail(fd, 503, "Cannot read the printer mesh file"); return; }
    int found = autosave_section(file, file_length, plate_slot(p->side), &start, &end);
    if (found < 0 || (found && (!autosave_mesh(file, start, end, &slot, &unknown) || unknown))) {
        free(file); plates_fail(fd, 503, "The printer mesh file has an unexpected format"); return;
    }
    int matches=found && plate_mesh_equal(&slot,&base->mesh,1);
    if(matches){
        plate_mesh memory;int live=plates_memory_slot(p->side,&memory);
        if(live<0){free(file);plates_fail(fd,503,"Cannot verify the printer mesh in memory");return;}
        matches=live==1 && plate_mesh_equal(&memory,&base->mesh,0);
    }
    if (matches) {
        free(file);
        /* Profile creation may itself load the side slot. Always finish with an explicit,
         * verified side-slot activation, including when the cc2_ profile already exists. */
        if (!plates_has_profile(base)) (void)plates_store_profile(p->side, base);
        if (!plates_activate_slot(p->side, &base->mesh)) {
            plates_mount_unverified(); plates_fail(fd, 503, "Cannot verify the active plate mesh"); return;
        }
        if (plates_apply_z(plate_z_now(p)) != 0) {
            plates_mount_unverified(); plates_fail(fd, 503, "Cannot apply the plate Z offset"); return;
        }
        plates_begin();
        memcpy(plates.current, p->id, sizeof(plates.current));
        memcpy(p->measure, base->id, sizeof(p->measure));
        if (!plates_commit(fd)) { plates_mount_unverified(); return; }
        plates_result = "mounted";
        plates_reply(fd, 200, "{\"mounted\":true,\"reboot\":false}\n");
        return;
    }
    if (!reboot) {
        free(file);
        plates_reply(fd, 409, "{\"ok\":false,\"reboot_required\":true,"
                              "\"error\":\"Writing this mesh to the printer needs a printer restart\"}\n");
        return;
    }
    char backup[PATH_MAX_LOCAL], copy[PATH_MAX_LOCAL];
    plates_autosave_copy_path(copy, sizeof(copy));
    char *updated = autosave_with_mesh(file, file_length, plate_slot(p->side), &base->mesh, &new_length);
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
    char old_current[PLATE_ID_LEN + 1]; memcpy(old_current, plates.current, sizeof(old_current));
    plates_begin();
    memcpy(plates.current, p->id, sizeof(plates.current));
    memcpy(plates.pending, p->id, sizeof(plates.pending));
    plates_pending_stage=0;
    memcpy(p->measure, base->id, sizeof(p->measure));
    if (plates_save() != 0 || plates_replace_file(printer_autosave_path, backup, updated, new_length, autosave_mode()) != 0) {
        plates = plates_undo;
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
    const plate_measure *base=p?plate_base(p):NULL;
    char copy[PATH_MAX_LOCAL];size_t old_length=0,current_length=0;
    plates_autosave_copy_path(copy,sizeof(copy));
    char *old=plates_read_file(copy,AUTOSAVE_FILE_MAX,&old_length);
    char *current=plates_read_file(printer_autosave_path,AUTOSAVE_FILE_MAX,&current_length);
    int ok=0;char *restored=NULL;size_t restored_length=0;
    if(!base||!old||!current)goto done;
    size_t start,end,old_start=0,old_end=0;plate_mesh written;int unknown;
    const char *slot=plate_slot(p->side);
    if(autosave_section(current,current_length,slot,&start,&end)!=1 ||
       !autosave_mesh(current,start,end,&written,&unknown)||unknown||!plate_mesh_equal(&written,&base->mesh,1))goto done;
    int old_found=autosave_section(old,old_length,slot,&old_start,&old_end);
    if(old_found<0)goto done;
    size_t expected_length=0;char *expected=autosave_with_mesh(old,old_length,slot,&base->mesh,&expected_length);
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
 * a bounded reply and three attempts per plate/service generation. */
static plates_exchange plates_background={.fd=-1};
static int plates_background_active(void){return plates_background.fd>=0;}
static unsigned plates_attempts;
static char plates_attempt_id[PLATE_ID_LEN+1];
static double plates_attempt_z;
static struct stat plates_attempt_service;
static int plates_attempt_service_known;
static void plates_exchange_close(plates_exchange *x){
    if(x->fd>=0)close(x->fd);
    free(x->buffer);memset(x,0,sizeof(*x));x->fd=-1;
}
static void plates_background_close(void){plates_exchange_close(&plates_background);plates_pending_stage=0;}
/* 1 completed, 0 still pending, -1 failed. The caller owns a completed reply. */
static int plates_exchange_step(plates_exchange *x,const char *query,char **reply,size_t *length){
    *reply=NULL;*length=0;
    /* Existing unit fixtures use their in-memory firmware; production uses the
     * nonblocking transport below. */
    if(plates_uds!=uds_query_json)return plates_uds(query,reply,length)==0?1:-1;
    if(x->fd<0){
        if(strlen(query)>=sizeof(x->request)||strlen(object_query_path)>=sizeof(((struct sockaddr_un *)0)->sun_path))return -1;
        x->fd=socket(AF_UNIX,SOCK_STREAM,0);if(x->fd<0)return -1;
        if(fcntl(x->fd,F_SETFL,O_NONBLOCK)<0)goto failed;
        x->buffer=malloc(PLATES_REPLY_MAX+1);if(!x->buffer)goto failed;
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
                memcpy(body,x->buffer,frame);body[frame]=0;*reply=body;*length=frame;plates_exchange_close(x);return 1;
            }
            memmove(x->buffer,x->buffer+frame+1,x->used-frame-1);x->used-=frame+1;continue;
        }
        if(x->used==PLATES_REPLY_MAX)goto failed;
        ssize_t n=recv(x->fd,x->buffer+x->used,PLATES_REPLY_MAX-x->used,MSG_DONTWAIT);
        if(n<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))break;
        if(n<=0)goto failed;
        x->used+=(size_t)n;
    }
    if(monotonic_ms()>=x->deadline)goto failed;
    return 0;
 failed:
    plates_exchange_close(x);return -1;
}
static int plates_background_step(const char *query,char **reply,size_t *length){
    return plates_exchange_step(&plates_background,query,reply,length);
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
    plates_pending_stage=0;
    plates_attempts++;plates_next_tick_ms=now+(plates_attempts==1?5000:15000);
    if(plates_attempts>=3){
        plates_result="verify_failed";
        if(plates.pending[0]){plates.pending[0]=0;plates.current[0]=0;if(plates_save()!=0){plates_available=0;plates_error="Cannot record failed plate verification";}}
    }
}

/* ---- the measurement of a print ------------------------------------------------- */

#define PLATES_LATE_LOADS 8
static int plates_late_active(void) { return plates_late.x.fd >= 0; }
static void plates_late_finish(const char *result) {
    plates_exchange_close(&plates_late.x);
    if (plates_late.state) fprintf(stderr, "Plate mesh %s for this print: %s after %d load(s)\n", plates_late.profile, result, plates_late.loads);
    plates_late.state = 0; plates_late_result = result;
}

/* The print start loads the side slot (before the file, and again at G180 S7) and
 * the slicer's adaptive mesh replaces it (G180 S8) when the print probes. Whenever
 * the slot is the active mesh before the first layer, the chosen measurement's
 * profile is loaded instead. The firmware runs it between file lines, before the
 * moves that follow, and keeps every request it receives, so loads are few. */
static void plates_late_tick(void) {
    if (!plates_late.state) return;
    long long now = monotonic_ms();
    const char *state = uds_print_state(&telemetry), *profile = uds_mesh_profile(&telemetry);
    double layer = 0; int have_layer = uds_value(&telemetry, U_LAYER, &layer);
    int printing = state && (!strcmp(state, "printing") || !strcmp(state, "paused"));
    if (plates_late.state == 1) {
        if (!printing) { if (now - plates_late.armed > 180000) plates_late_finish("not_started"); return; }
        plates_late.state = 2;
    }
    if (now - plates_late.armed > 45 * 60000) { plates_late_finish("failed"); return; }
    int ours = profile && !strcmp(profile, plates_late.profile);
    if (state && !printing) { plates_late_finish(ours ? "loaded" : "ended"); return; }
    if (!state || !profile) return; /* the stream reconnects; the print start waits for nothing */
    /* A new print resets the layer to 0, so a stale count from the last print is not the first layer. */
    if (have_layer && layer < 1) plates_late.layer_reset = 1;
    if (plates_late.layer_reset && have_layer && layer >= 1) { plates_late_finish(ours ? "loaded" : "missed"); return; }
    if (!strcmp(profile, "ADAPTIVE")) { plates_late_finish("adaptive"); return; }
    /* A load needs positive proof that the first layer has not begun: fresh telemetry reading
     * layer 0 of this print right now. A stream that reconnects during a print, or a count that
     * never showed this print's 0, therefore never loads. */
    int startup = plates_late.layer_reset && have_layer && layer < 1 && !strcmp(state, "printing");
    int slot = !strcmp(profile, plates_late.slot);
    if (plates_late.x.fd >= 0 && !startup && plates_late.x.sent < strlen(plates_late.x.request)) {
        plates_exchange_close(&plates_late.x); /* not sent yet: the window closed meanwhile */
        return;
    }
    if (plates_late.x.fd < 0) { /* a load already sent finishes even if the push about it came first */
        if (slot && startup && plates_late.loads >= PLATES_LATE_LOADS && now - plates_late.sent >= 5000)
            plates_late_finish("failed");
        if (!slot || !startup || plates_late.loads >= PLATES_LATE_LOADS || now - plates_late.sent < 1500) return;
    }
    char query[192], *reply = NULL; size_t length = 0;
    snprintf(query, sizeof(query), "{\"id\":204,\"method\":\"gcode/script\",\"params\":{\"script\":\"BED_MESH_PROFILE LOAD=%s\"}}\003",
             plates_late.profile);
    int result = plates_exchange_step(&plates_late.x, query, &reply, &length);
    if (!result) return;
    plates_late.loads++; plates_late.sent = now;
    if (result > 0 && !plates_reply_ok(reply, length)) fprintf(stderr, "Plate mesh %s was refused\n", plates_late.profile);
    free(reply);
}

/* Before a print started here: `measure` (one of the mounted plate's measurements,
 * or empty) and `nozzle` (or empty) from the print dialog. NULL when the print may
 * start; `late` gets the measurement to load during the print start, or stays
 * empty when the slot already holds it or the print probes its own mesh. A newly
 * chosen nozzle is selected and its offset applied before the print starts. */
static const char *plates_print_prepare(const mqtt_client *mqtt, char side, int saved_mesh, const char *measure,
                                        const char *nozzle, char *late) {
    late[0] = 0;
    if (plates_cal.stage) return "A bed mesh calibration is running";
    if (!*measure && !*nozzle) return NULL;
    if (!plates_available) return plates_error;
    if (plates.pending[0]) return "A plate change is waiting for the printer restart";
    const plate_measure *m = NULL;
    if (*measure) {
        const plate_entry *p;
        if (!plate_id_valid(measure) || !(m = measure_find(measure)) || !(p = plate_find(m->plate)))
            return "Unknown plate measurement";
        if (strcmp(plates.current, p->id)) return "The measurement belongs to a plate that is not mounted";
        if (p->side != side) return "The mounted plate is on the other build-plate side";
    }
    if (*nozzle && !(plate_id_valid(nozzle) && nozzle_find(nozzle))) return "Unknown nozzle";
    if (m && saved_mesh) {
        plate_mesh slot;
        if (!(autosave_slot(side, &slot) == 1 && plate_mesh_equal(&slot, &m->mesh, 1))) {
            if (!plates_has_profile(m))
                return "This measurement is not stored in the printer; mount it from the build-plate library first";
            snprintf(late, PLATE_ID_LEN + 1, "%s", m->id);
        }
    }
    if (*nozzle && strcmp(plates.nozzle, nozzle)) {
        char old[PLATE_ID_LEN + 1]; memcpy(old, plates.nozzle, sizeof(old));
        memcpy(plates.nozzle, nozzle, sizeof(plates.nozzle)); /* a checked id */
        if (plates_save() != 0) { memcpy(plates.nozzle, old, sizeof(old)); late[0] = 0; return "Cannot save the plate library"; }
        const plate_entry *current = plates.current[0] ? plate_find(plates.current) : NULL;
        if (current && (z_offset_pending || plates_printer_ready(mqtt) || plates_apply_z(plate_z_now(current)) != 0)) {
            late[0] = 0; return "Cannot apply the nozzle Z offset";
        }
    }
    return NULL;
}

/* The print was accepted: watch its start for the measurement chosen in plates_print_prepare. */
static void plates_print_arm(const char *late, char side) {
    if (plates_late.state) plates_late_finish("ended");
    plates_exchange_close(&plates_late.x);
    plates_late_result = "";
    const plate_measure *m = late && *late ? measure_find(late) : NULL;
    if (!m) return;
    memcpy(plates_late.measure, m->id, sizeof(plates_late.measure));
    plate_profile_name(plates_late.profile, sizeof(plates_late.profile), m);
    snprintf(plates_late.slot, sizeof(plates_late.slot), "%s", plate_slot(side));
    plates_late.state = 1; plates_late.layer_reset = 0; plates_late.loads = 0;
    plates_late.armed = monotonic_ms(); plates_late.sent = 0;
}

/* ---- bed mesh calibration ---------------------------------------------------------- */

/* The firmware's BED_MESH_CALIBRATE ... BED_TEMP=t heats the bed and probes as soon as
 * the sensor reads t, while the plate is still expanding. Run here, the calibration
 * first holds the bed at t for the chosen time. Every step goes through the console,
 * like a command typed there; tests replace the console. */
static int plates_console_send(console_state *console, const char *command) {
    char reason[160];
    return console_start(console, command, reason, sizeof(reason));
}
static int (*plates_console)(console_state *console, const char *command) = plates_console_send;

static int plates_cal_command(console_state *console, int stage, const char *command) {
    if (plates_console(console, command) != 0) return -1;
    pthread_mutex_lock(&console->lock);
    plates_cal.generation = console->generation;
    pthread_mutex_unlock(&console->lock);
    plates_cal.stage = stage; plates_cal.since = monotonic_ms(); plates_cal.done_at = 0;
    return 0;
}

/* Tests count the heater switch-offs instead. */
static int plates_send_bed_off(void) { return send_local_gcode_script("M140 S0"); }
static int (*plates_bed_off)(void) = plates_send_bed_off;

/* The bed heater is the job's to switch off only while fresh MQTT reports Idle and fresh
 * telemetry shows the target the job set. A print started meanwhile by another client, a
 * target changed elsewhere or a printer that cannot be seen keeps its heater as it is. */
static int plates_cal_owns_bed(const mqtt_client *mqtt) {
    time_t now = time(NULL); double target;
    return mqtt->connected && mqtt->registered && mqtt->have_machine_status && mqtt->machine_status == 1 &&
           mqtt->last_message > 0 && now >= mqtt->last_message && now - mqtt->last_message <= 15 &&
           uds_value(&telemetry, U_BG, &target) && fabs(target - plates_cal.temp) <= 0.5;
}

/* With `bed_off`, a job that ends early switches the bed heater off when it still owns it. */
static void plates_cal_end(const mqtt_client *mqtt, const char *result, const char *error, const char *detail, int bed_off) {
    if (bed_off && plates_cal_owns_bed(mqtt)) (void)plates_bed_off();
    if (plates_cal.stage) fprintf(stderr, "Bed mesh calibration at %d C: %s %s %s\n", plates_cal.temp, result, error, detail);
    plates_cal.stage = CAL_IDLE; plates_cal.result = result; plates_cal.error = error;
    snprintf(plates_cal.detail, sizeof(plates_cal.detail), "%s", detail);
}

/* NULL when probing may start: MQTT reports Idle now and fresh telemetry reads the bed at its target. */
static const char *plates_cal_hold(const mqtt_client *mqtt, int have_bed, double bed, double target) {
    time_t now = time(NULL);
    if (!mqtt->connected || !mqtt->registered || !mqtt->have_machine_status || mqtt->last_message <= 0 ||
        now < mqtt->last_message || now - mqtt->last_message > 15) return "Printer status is stale";
    if (mqtt->machine_status != 1) return "The printer is not idle";
    if (!have_bed) return "Bed telemetry is unavailable";
    if (fabs(target - plates_cal.temp) > 0.5 || bed < plates_cal.temp - 1.0) return "The bed is not at the temperature";
    return NULL;
}

static int plates_homed(const mqtt_client *mqtt) {
    return strchr(mqtt->homed_axes, 'x') && strchr(mqtt->homed_axes, 'y') && strchr(mqtt->homed_axes, 'z');
}

/* side \n temp \n soak minutes [\n nozzle [\n plate]]: calibrate the side's mesh at `temp`.
 * CC2 Control homes the printer when needed, heats the bed, holds it for the soak time
 * and probes; with a plate the result becomes its measurement at `temp` with `nozzle`.
 * It needs no open page; it can be cancelled until the probing starts. */
static void plates_calibrate_response(int fd, const mqtt_client *mqtt, console_state *console, const char *body,
                                      size_t length) {
    char fields[5][PLATE_NAME_MAX + 1] = {{0}}; int temp, soak; const char *reason;
    int count = plates_fields(body, length, fields, 5);
    size_t n = strlen(fields[2]);
    if (count < 3 || strlen(fields[0]) != 1 || (fields[0][0] != 'A' && fields[0][0] != 'B') ||
        !plate_temp_parse(fields[1], &temp) || !n || n > 2 || strspn(fields[2], "0123456789") != n ||
        (soak = atoi(fields[2])) > PLATE_SOAK_MAX || !plate_nozzle_ref(fields[3]) ||
        (fields[4][0] && !plate_id_valid(fields[4]))) { plates_fail(fd, 400, "Invalid calibration"); return; }
    if (plates_cal.stage) { plates_fail(fd, 409, "A bed mesh calibration is already running"); return; }
    char side = fields[0][0];
    if (fields[4][0]) {
        if (!plates_open(fd)) return;
        const plate_entry *p = plate_find(fields[4]);
        if (!p) { plates_fail(fd, 404, "Unknown plate"); return; }
        if (p->side != side) { plates_fail(fd, 409, "The plate is on the other side"); return; }
    }
    if ((reason = plates_printer_ready(mqtt))) { plates_fail(fd, 409, reason); return; }
    if (z_offset_pending) { plates_fail(fd, 409, "A Z offset change is still being confirmed"); return; }
    /* The measurement now in the slot keeps a printer profile before the calibration replaces it. */
    if (plates_available && !plates.pending[0]) (void)plates_keep_slot(side);
    memset(&plates_cal, 0, sizeof(plates_cal));
    plates_cal.side = side; plates_cal.temp = temp; plates_cal.soak = soak * 60;
    memcpy(plates_cal.nozzle, fields[3], sizeof(plates_cal.nozzle)); /* "" or a checked id */
    memcpy(plates_cal.plate, fields[4], sizeof(plates_cal.plate));
    plates_cal.result = ""; plates_cal.error = "";
    char command[32];
    snprintf(command, sizeof(command), "M140 S%d", temp);
    if (plates_cal_command(console, plates_homed(mqtt) ? CAL_HEATING : CAL_HOMING,
                           plates_homed(mqtt) ? command : "G28") != 0) {
        plates_cal.stage = CAL_IDLE;
        plates_fail(fd, 409, "Another printer command is still running"); return;
    }
    plates_reply(fd, 202, "{\"started\":true}\n");
}

/* Until the probing starts. */
static void plates_calibrate_cancel_response(int fd, const mqtt_client *mqtt) {
    if (plates_cal.stage == CAL_PROBING || plates_cal.stage == CAL_SAVING) {
        plates_fail(fd, 409, "The probing has started; use the emergency stop to interrupt it"); return;
    }
    if (plates_cal.stage) plates_cal_end(mqtt, "cancelled", "", "", 1);
    plates_reply(fd, 200, "{\"cancelled\":true}\n");
}

static void plates_calibration_tick(const mqtt_client *mqtt, console_state *console) {
    if (!plates_cal.stage) return;
    long long now = monotonic_ms();
    pthread_mutex_lock(&console->lock);
    int ours = console->generation == plates_cal.generation, busy = console->busy;
    int finished = ours && !busy && console->completed, success = console->success;
    pthread_mutex_unlock(&console->lock);
    if (finished && !plates_cal.done_at) plates_cal.done_at = now;
    int idle = mqtt->have_machine_status && mqtt->machine_status == 1;
    double bed = 0, target = 0;
    int have_bed = uds_value(&telemetry, U_BT, &bed) && uds_value(&telemetry, U_BG, &target);
    switch (plates_cal.stage) {
    case CAL_HOMING:
        if (finished && !success) { plates_cal_end(mqtt, "failed", "homing", "", 0); return; }
        if (finished && plates_homed(mqtt) && !busy) {
            char command[32]; snprintf(command, sizeof(command), "M140 S%d", plates_cal.temp);
            if (plates_cal_command(console, CAL_HEATING, command) != 0 && now - plates_cal.done_at > 30000)
                plates_cal_end(mqtt, "failed", "busy", "", 0);
            return;
        }
        if ((finished && now - plates_cal.done_at > 30000) || now - plates_cal.since > 5 * 60000)
            plates_cal_end(mqtt, "failed", "homing", "", 0);
        return;
    case CAL_HEATING:
        if (finished && !success) { plates_cal_end(mqtt, "failed", "heating", "", 1); return; }
        if (!finished) { if (now - plates_cal.since > 60000) plates_cal_end(mqtt, "failed", "heating", "", 1); return; }
        if (!idle && mqtt->have_machine_status) { plates_cal_end(mqtt, "failed", "busy", "", 0); return; }
        if (have_bed && fabs(target - plates_cal.temp) > 0.5 && now - plates_cal.done_at > 10000) {
            plates_cal_end(mqtt, "failed", "heating", "The bed heater was changed", 0); return;
        }
        if (have_bed && fabs(target - plates_cal.temp) <= 0.5 && bed >= plates_cal.temp - 1.0) {
            plates_cal.stage = CAL_SOAKING; plates_cal.since = now;
            plates_cal.soak_until = now + (long long)plates_cal.soak * 1000;
            return;
        }
        if (now - plates_cal.since > 30 * 60000) plates_cal_end(mqtt, "failed", "heating", "The bed did not reach the temperature", 1);
        return;
    case CAL_SOAKING:
        if (!idle && mqtt->have_machine_status) { plates_cal_end(mqtt, "failed", "busy", "", 0); return; }
        if (have_bed && fabs(target - plates_cal.temp) > 0.5) {
            plates_cal_end(mqtt, "failed", "heating", "The bed heater was changed", 0); return;
        }
        if (now < plates_cal.soak_until || busy) return; /* a command typed meanwhile finishes first */
        {
            /* Waits up to a minute for fresh evidence, then stops; the bed is switched off only when
             * the printer is known to be idle, so a print started meanwhile keeps its heat. */
            const char *hold = plates_cal_hold(mqtt, have_bed, bed, target);
            if (hold) {
                if (!plates_cal.held) plates_cal.held = now;
                if (now - plates_cal.held > 60000) {
                    int stale = !strcmp(hold, "Printer status is stale") || !strcmp(hold, "Bed telemetry is unavailable");
                    plates_cal_end(mqtt, "failed", stale ? "telemetry" : !strcmp(hold, "The printer is not idle") ? "busy" : "heating",
                                   hold, 1);
                }
                return;
            }
            plates_cal.held = 0;
            char command[96];
            snprintf(command, sizeof(command), "BED_MESH_CALIBRATE PROFILE=%s BED_TEMP=%d", plate_slot(plates_cal.side),
                     plates_cal.temp);
            if (plates_cal_command(console, CAL_PROBING, command) != 0 && now - plates_cal.soak_until > 60000)
                plates_cal_end(mqtt, "failed", "busy", "", 1);
        }
        return;
    case CAL_PROBING:
        if (!finished) return; /* the console gives up on a command after 15 minutes */
        if (!success) { plates_cal_end(mqtt, "failed", "probing", "", 0); return; }
        if (!plates_cal.plate[0]) { plates_cal_end(mqtt, "done", "", "", 0); return; }
        plates_cal.stage = CAL_SAVING; plates_cal.since = now;
        return;
    case CAL_SAVING: {
        int status = 0, profile = 0, applied = 0; plate_measure *m = NULL;
        const char *error = plates_take_measure(mqtt, plates_cal.plate, plates_cal.temp, plates_cal.nozzle, 0,
                                                &status, &m, &profile, &applied);
        if (!error) {
            memcpy(plates_cal.measure, m->id, sizeof(plates_cal.measure));
            plates_cal_end(mqtt, "saved", "", profile ? "" : "The printer did not store the measurement profile", 0);
        } else if (status != 409 || now - plates_cal.since > 60000) plates_cal_end(mqtt, "failed", "saving", error, 0);
        return;
    }
    }
}

/* Main loop: undo a write whose restart failed, verify a mount after the restart,
 * keep the mounted plate's Z offset applied while the printer is idle and load the
 * measurement chosen for a print. */
static void plates_tick(const mqtt_client *mqtt) {
    plates_late_tick();
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
    double z = current ? plate_z_now(current) : 0;
    int z_due = current && (!plates_z_valid || !plate_close(plates_z_applied, z));
    long long now = monotonic_ms();
    if(current && (strcmp(plates_attempt_id,current->id)||!plate_close(plates_attempt_z,z))){
        snprintf(plates_attempt_id,sizeof(plates_attempt_id),"%s",current->id);plates_attempt_z=z;
        plates_attempts=0;plates_background_close();
    }
    if ((!plates.pending[0] && !z_due) || plates_printer_ready(mqtt) || z_offset_pending){
        plates_background_close(); plates_pending_stage=0; return;
    }
    if(plates_attempts>=3 || (now<plates_next_tick_ms && plates_background.fd<0))return;
    plates_next_tick_ms = now + 5000;
    if (plates.pending[0]) {
        const plate_entry *p=plate_find(plates.pending);
        const plate_measure *base=p?plate_base(p):NULL;
        if (!base) { plates_mount_unverified(); return; }
        /* At most four bounded nonblocking transactions; each stage retains its
         * in-flight exchange. Busy/stale telemetry resets verification above. */
        for (int step=0; step<4; ++step) {
            int result;
            if (plates_pending_stage==0) {
                plate_mesh file,memory;
                result=plates_memory_background(p->side,&memory);
                if (result==-2) return;
                if (result<0) { plates_background_failed(now); return; }
                if (result!=1 || autosave_slot(p->side,&file)!=1 ||
                    !plate_mesh_equal(&file,&base->mesh,1) || !plate_mesh_equal(&memory,&base->mesh,0)) {
                    plates_mount_unverified(); return;
                }
            } else if (plates_pending_stage<3) {
                char query[192], *reply=NULL; size_t length=0;
                if (plates_pending_stage==1)
                    snprintf(query,sizeof(query),"{\"id\":204,\"method\":\"gcode/script\",\"params\":{\"script\":\"BED_MESH_PROFILE LOAD=%s\"}}\003",plate_slot(p->side));
                else snprintf(query,sizeof(query),"%s",plates_active_query);
                result=plates_background_step(query,&reply,&length);
                if (!result) return;
                if (result<0) { plates_background_failed(now); return; }
                int ok=plates_pending_stage==1 ? plates_reply_ok(reply,length) : plates_active_reply(p->side,&base->mesh,reply,length);
                free(reply);
                if (!ok) { plates_mount_unverified(); return; }
            } else {
                result=plates_z_background(plate_z_now(p));
                if (result==-2) return;
                if (result<0) { plates_background_failed(now); return; }
                plates.pending[0]=0; plates_pending_stage=0;
                if (plates_save()!=0) { plates_mount_unverified(); return; }
                plates_attempts=0; plates_result="mounted"; return;
            }
            plates_pending_stage++; plates_next_tick_ms=now;
        }
        return;
    }
    if(current){int applied=plates_z_background(z);if(applied==-1)plates_background_failed(now);else if(applied==0)plates_attempts=0;}
}
