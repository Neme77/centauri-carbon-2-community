/* Canvas pull-back after the cut: [canvas_dev] unload_fila_dist, how far the Canvas
 * pulls a cut filament back from the toolhead (mmu.cpp extruder_retract).
 *
 * The firmware reads it once at start-up from the printer configuration followed by
 * autosave.cfg, where a later value replaces an earlier one, and its SAVE_CONFIG
 * rewrites only autosave.cfg and keeps every section in it. CC2 Control therefore
 * stores its value as a "[canvas_dev]" override in the SAVE_CONFIG block and never
 * edits the vendor configuration; removing the line returns to the configured value.
 * The firmware refuses to start with a value outside 60-350 mm, so nothing else is
 * ever written, unless the printer runs the program with that limit raised to
 * 1000 mm (see below). The value applies at the next printer restart, which the page
 * can request through the same guarded reboot as the build-plate library. A saved
 * value is pending until the system has booted again (kernel boot id). */
#define CANVAS_UNLOAD_MIN 60
#define CANVAS_UNLOAD_MAX 350
#define CANVAS_UNLOAD_EXTENDED_MAX 1000
#define CANVAS_UNLOAD_KEY "unload_fila_dist"
#define CANVAS_UNLOAD_CONFIG_MAX (256 * 1024)

static const char *printer_config_path = "/opt/inst/printer_dsp.cfg";
/* Absolute: the service runs from /. The marker only has to outlive a CC2 Control restart, not a reboot. */
static const char *canvas_unload_pending_path = "/tmp/cc2-control-canvas-unload-pending";
static const char *boot_id_path = "/proc/sys/kernel/random/boot_id";
static int canvas_unload_restart_requested;

/* A longer pull-back for a 4-in-1 merger far from the toolhead (issue #172) needs the
 * community firmware's elegoo_printer (ELEGOO 02.01.01.07) with one constant changed:
 * the 350.0 maximum of getdouble("unload_fila_dist") at file offset 0x5bba38 raised to
 * 1000.0. Only that exact program, while it is the one running, unlocks values above
 * 350; any other program would refuse to start with them.
 *
 * Hashing the 29 MB file takes a few seconds on the printer, so it runs on a thread,
 * never in the HTTP loop, and the stock range applies until the result is known. A
 * result belongs to one process and one file: the process id with its start time (a
 * reused id has another start time) and the file's device, inode, size and
 * modification and change times. The file is hashed through one descriptor and must
 * be unchanged before and after reading, and the same process must still be running
 * the same file when the result is stored. A save above 350 checks the running
 * program again just before it writes. No printer process, two of them, or any read
 * error keeps 350. */
static const char *canvas_unload_extended_sha256 = "15ea20771b1424560bfa0c6e4170993e1ce20001b90715d2b00f57dfcb0ea1bc";
static const char *canvas_unload_program = NULL; /* tests: this file instead of the running printer */
static void (*canvas_unload_after_hash)(void) = NULL; /* tests: runs between hashing and storing the result */
static pthread_mutex_t canvas_unload_check_lock = PTHREAD_MUTEX_INITIALIZER;
static char canvas_unload_checked[448];  /* the process and file the result belongs to */
static int canvas_unload_checking, canvas_unload_extended_ok;

typedef struct {
    char path[300];      /* where to read the program */
    char file[160];      /* device, inode, size, mtime, ctime */
    char identity[448];  /* the process (or test file) and file */
} canvas_unload_program_id;

static void canvas_unload_file_identity(const struct stat *st, char *out, size_t size) {
    snprintf(out, size, "%llu:%llu:%lld:%lld.%09ld:%lld.%09ld", (unsigned long long)st->st_dev,
             (unsigned long long)st->st_ino, (long long)st->st_size, (long long)st->st_mtim.tv_sec,
             (long)st->st_mtim.tv_nsec, (long long)st->st_ctim.tv_sec, (long)st->st_ctim.tv_nsec);
}

/* Field 22 of a /proc/<pid>/stat line, the start time in clock ticks. The process name
 * in parentheses may contain spaces and parentheses, so fields count from the last ")". */
static int canvas_unload_parse_start_time(const char *line, unsigned long long *start) {
    const char *p = strrchr(line, ')');
    if (!p) return 0;
    p++;  /* the space before field 3 */
    for (int field = 4; field <= 22; ++field) {
        p = strchr(p + 1, ' ');
        if (!p) return 0;
    }
    char *end;
    errno = 0;
    *start = strtoull(p + 1, &end, 10);
    return !errno && end != p + 1 && (*end == ' ' || *end == '\n' || !*end);
}

static int canvas_unload_start_time(const char *pid, unsigned long long *start) {
    char path[300], line[1024];
    snprintf(path, sizeof(path), "/proc/%s/stat", pid);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    size_t n = fread(line, 1, sizeof(line) - 1, f);
    fclose(f);
    line[n] = 0;
    return canvas_unload_parse_start_time(line, start);
}

/* The one running elegoo_printer, or the test file. 0 when there is none or more than one. */
static int canvas_unload_find_program(canvas_unload_program_id *id) {
    struct stat st;
    if (canvas_unload_program) {
        if (stat(canvas_unload_program, &st) != 0) return 0;
        snprintf(id->path, sizeof(id->path), "%s", canvas_unload_program);
        canvas_unload_file_identity(&st, id->file, sizeof(id->file));
        snprintf(id->identity, sizeof(id->identity), "file:%s", id->file);
        return 1;
    }
    DIR *proc = opendir("/proc");
    if (!proc) return 0;
    struct dirent *entry;
    int found = 0;
    while ((entry = readdir(proc))) {
        if (!isdigit((unsigned char)entry->d_name[0])) continue;
        char comm_path[300], comm[32] = "";
        snprintf(comm_path, sizeof(comm_path), "/proc/%s/comm", entry->d_name);
        FILE *f = fopen(comm_path, "r");
        if (!f) continue;
        if (!fgets(comm, sizeof(comm), f)) comm[0] = 0;
        fclose(f);
        if (strcmp(comm, "elegoo_printer\n")) continue;
        /* The same start time on both sides of the stat: the id was not reused in between. */
        unsigned long long start = 0, again = 0;
        char exe[300];
        snprintf(exe, sizeof(exe), "/proc/%s/exe", entry->d_name);
        if (!canvas_unload_start_time(entry->d_name, &start) || stat(exe, &st) != 0 ||
            !canvas_unload_start_time(entry->d_name, &again) || start != again) continue;
        if (++found > 1) break;
        snprintf(id->path, sizeof(id->path), "%s", exe);
        canvas_unload_file_identity(&st, id->file, sizeof(id->file));
        snprintf(id->identity, sizeof(id->identity), "%s:%llu:%s", entry->d_name, start, id->file);
    }
    closedir(proc);
    return found == 1;
}

/* SHA-256 of the program, only if the file read is the one identified and did not change while it was read. */
static int canvas_unload_hash_program(const canvas_unload_program_id *id, char digest[65]) {
    int fd = open(id->path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    struct stat before, after;
    char file_before[160], file_after[160];
    int ok = fstat(fd, &before) == 0;
    if (ok) canvas_unload_file_identity(&before, file_before, sizeof(file_before));
    ok = ok && !strcmp(file_before, id->file) && sha256_fd(fd, digest) && fstat(fd, &after) == 0;
    if (ok) canvas_unload_file_identity(&after, file_after, sizeof(file_after));
    ok = ok && !strcmp(file_after, file_before);
    close(fd);
    return ok;
}

static void *canvas_unload_check_worker(void *arg) {
    canvas_unload_program_id *job = arg, now;
    char digest[65];
    int ok = canvas_unload_hash_program(job, digest) && !strcmp(digest, canvas_unload_extended_sha256);
    if (canvas_unload_after_hash) canvas_unload_after_hash();
    /* Still the same process and file now that the result is known. */
    int current = canvas_unload_find_program(&now) && !strcmp(now.identity, job->identity);
    pthread_mutex_lock(&canvas_unload_check_lock);
    if (!strcmp(canvas_unload_checked, job->identity)) {
        canvas_unload_extended_ok = ok && current;
        if (!current) canvas_unload_checked[0] = 0;  /* check whatever runs now on the next request */
    }
    canvas_unload_checking = 0;
    pthread_mutex_unlock(&canvas_unload_check_lock);
    free(job);
    return NULL;
}

/* 1 when the running program is known to accept up to 1000 mm. Starts the check for
 * a process or file not seen before and answers 0 until it has finished. */
static int canvas_unload_extended(void) {
    canvas_unload_program_id id;
    if (!canvas_unload_find_program(&id)) return 0;
    pthread_mutex_lock(&canvas_unload_check_lock);
    int known = !strcmp(id.identity, canvas_unload_checked), result = known && canvas_unload_extended_ok;
    if (!known && !canvas_unload_checking) {
        canvas_unload_program_id *job = malloc(sizeof(*job));
        pthread_t thread;
        if (job) {
            *job = id;
            snprintf(canvas_unload_checked, sizeof(canvas_unload_checked), "%s", id.identity);
            canvas_unload_extended_ok = 0; canvas_unload_checking = 1;
            if (pthread_create(&thread, NULL, canvas_unload_check_worker, job) == 0) pthread_detach(thread);
            else { canvas_unload_checking = 0; canvas_unload_checked[0] = 0; free(job); }
        }
    }
    pthread_mutex_unlock(&canvas_unload_check_lock);
    return result;
}

static int canvas_unload_max(void) { return canvas_unload_extended() ? CANVAS_UNLOAD_EXTENDED_MAX : CANVAS_UNLOAD_MAX; }

/* The "#*#" lines of autosave.cfg that belong to [canvas_dev]. */
typedef struct {
    int found;                    /* the section exists */
    size_t start, end;            /* its header and key lines */
    int keys;                     /* key lines in it */
    int has_value;                /* unload_fila_dist is one of them */
    size_t value_start, value_end; /* that line, with its newline */
    double value;
} canvas_unload_entry;

static int canvas_unload_number(const char *text, size_t length, double *out) {
    char number[32];
    while (length && isspace((unsigned char)*text)) { text++; length--; }
    while (length && isspace((unsigned char)text[length - 1])) length--;
    if (!length || length >= sizeof(number)) return 0;
    memcpy(number, text, length); number[length] = 0;
    char *end; errno = 0;
    double value = strtod(number, &end);
    if (errno || *end || !isfinite(value)) return 0;
    *out = value;
    return 1;
}

/* 1 parsed (the section may be absent), -1 not a SAVE_CONFIG block this code can edit:
 * no marker, a carriage return, a duplicate section or key, or an unreadable value. */
static int canvas_unload_autosave(const char *text, size_t length, canvas_unload_entry *e) {
    static const char header[] = "#*# [canvas_dev]";
    const size_t header_len = sizeof(header) - 1, key_len = strlen(CANVAS_UNLOAD_KEY);
    memset(e, 0, sizeof(*e));
    if (memchr(text, '\r', length) || memchr(text, '\0', length)) return -1;
    const char *marker = strstr(text, AUTOSAVE_MARKER);
    if (!marker) return -1;
    for (size_t pos = (size_t)(marker - text); pos < length;) {
        size_t eol = autosave_line_end(text, length, pos), next = eol < length ? eol + 1 : length;
        /* Equivalent INI headers with noncanonical SAVE_CONFIG spacing are
         * ambiguous to edit. Refuse them instead of appending a second section. */
        if (eol - pos >= 3 && !memcmp(text + pos, "#*#", 3)) {
            const char *section = text + pos + 3;
            size_t n = eol - pos - 3;
            while (n && isspace((unsigned char)*section)) { section++; n--; }
            while (n && isspace((unsigned char)section[n - 1])) n--;
            if (n == 12 && !memcmp(section, "[canvas_dev]", 12) &&
                (eol - pos != header_len || memcmp(text + pos, header, header_len))) return -1;
        }
        if (eol - pos != header_len || memcmp(text + pos, header, header_len)) { pos = next; continue; }
        if (e->found) return -1;
        e->found = 1; e->start = pos;
        size_t line = next;
        while (line < length) {
            size_t line_eol = autosave_line_end(text, length, line), after = line_eol < length ? line_eol + 1 : length;
            if (line_eol - line >= 3 && !memcmp(text + line, "#*#", 3)) {
                const char *payload = text + line + 3;
                while (payload < text + line_eol && isspace((unsigned char)*payload)) payload++;
                if (payload < text + line_eol && *payload == '[') break;
                if ((size_t)(text + line_eol - payload) > key_len &&
                    !memcmp(payload, CANVAS_UNLOAD_KEY, key_len)) {
                    const char *delimiter = payload + key_len;
                    while (delimiter < text + line_eol && isspace((unsigned char)*delimiter)) delimiter++;
                    if (delimiter < text + line_eol && (*delimiter == '=' || *delimiter == ':') &&
                        (line_eol - line < 4 || memcmp(text + line, "#*# ", 4) || payload != text + line + 4))
                        return -1;
                }
            }
            if (line_eol - line < 5 || memcmp(text + line, "#*# ", 4) || text[line + 4] == '[') break;
            e->keys++;
            const char *key = text + line + 4, *sep = memchr(key, '=', line_eol - line - 4);
            if (!sep) sep = memchr(key, ':', line_eol - line - 4);
            const char *original_key = key;
            while (sep && key < sep && isspace((unsigned char)*key)) key++;
            size_t name_len = sep ? (size_t)(sep - key) : 0;
            while (name_len && isspace((unsigned char)key[name_len - 1])) name_len--;
            if (name_len == key_len && !memcmp(key, CANVAS_UNLOAD_KEY, key_len)) {
                if (key != original_key) return -1;
                if (e->has_value || !canvas_unload_number(sep + 1, (size_t)(text + line_eol - sep - 1), &e->value))
                    return -1;
                e->has_value = 1; e->value_start = line; e->value_end = after;
            }
            line = after;
        }
        e->end = line;
        pos = line;
    }
    return 1;
}

/* The value the vendor configuration sets, without autosave.cfg: the last
 * unload_fila_dist of the last [canvas_dev] section, as the firmware's INI loader reads
 * it ('#' starts a comment anywhere on a line). 1 found, 0 absent or unreadable. */
static int canvas_unload_configured(const char *text, size_t length, double *out) {
    const size_t key_len = strlen(CANVAS_UNLOAD_KEY);
    int inside = 0, found = 0;
    for (size_t pos = 0; pos < length;) {
        size_t eol = autosave_line_end(text, length, pos), next = eol < length ? eol + 1 : length;
        const char *line = text + pos, *hash = memchr(line, '#', eol - pos);
        size_t n = hash ? (size_t)(hash - line) : eol - pos;
        while (n && isspace((unsigned char)*line)) { line++; n--; }
        while (n && isspace((unsigned char)line[n - 1])) n--;
        if (n && line[0] == '[') {
            inside = n == 12 && !memcmp(line, "[canvas_dev]", 12);
        } else if (inside && n > key_len && !memcmp(line, CANVAS_UNLOAD_KEY, key_len)) {
            size_t rest = key_len;
            while (rest < n && isspace((unsigned char)line[rest])) rest++;
            double value;
            if (rest < n && (line[rest] == '=' || line[rest] == ':') &&
                canvas_unload_number(line + rest + 1, n - rest - 1, &value)) { *out = value; found = 1; }
        }
        pos = next;
    }
    return found;
}

/* A copy of autosave.cfg with the override set to `value`, or removed when `value` is 0,
 * checked by parsing it back. Every byte outside the edited lines stays as it was. */
static char *canvas_unload_rewrite(const char *text, size_t length, int value, size_t *out_length) {
    static const char separator[] = "#*#\n";
    canvas_unload_entry e, check;
    if (canvas_unload_autosave(text, length, &e) != 1) return NULL;
    char line[64];
    int line_len = value ? snprintf(line, sizeof(line), "#*# " CANVAS_UNLOAD_KEY " = %d\n", value) : 0;
    size_t cut_start, cut_end; /* replaced by `insert` */
    char insert[128]; size_t insert_len = 0;
    if (value && e.has_value) {
        cut_start = e.value_start; cut_end = e.value_end;
        memcpy(insert, line, (size_t)line_len); insert_len = (size_t)line_len;
        if (cut_end == length && text[length - 1] != '\n') insert_len--; /* the file ended on that line */
    } else if (value && e.found) {
        size_t header_end = autosave_line_end(text, length, e.start);
        if (header_end == length) return NULL; /* a header without a newline: not ours to repair */
        cut_start = cut_end = header_end + 1;
        memcpy(insert, line, (size_t)line_len); insert_len = (size_t)line_len;
    } else if (value) { /* a new section after the last non-empty line, like SAVE_CONFIG lays them out */
        cut_start = cut_end = length;
        while (cut_start && text[cut_start - 1] == '\n') cut_start = --cut_end;
        insert_len = (size_t)snprintf(insert, sizeof(insert), "\n%s%s#*# [canvas_dev]\n%.*s", separator, separator,
                                      line_len - 1, line);
        if (cut_end == length) insert[insert_len++] = '\n';
    } else if (!e.has_value) {
        cut_start = cut_end = 0; /* nothing to remove */
    } else if (e.keys > 1) {
        cut_start = e.value_start; cut_end = e.value_end;
    } else { /* the whole section, with the separator lines before it */
        cut_start = e.start; cut_end = e.end;
        while (cut_start >= 4 && !memcmp(text + cut_start - 4, separator, 4) &&
               (cut_start == 4 || text[cut_start - 5] == '\n'))
            cut_start -= 4;
    }
    size_t used = cut_start + insert_len + (length - cut_end);
    char *copy = malloc(used + 1);
    if (!copy) return NULL;
    memcpy(copy, text, cut_start);
    memcpy(copy + cut_start, insert, insert_len);
    memcpy(copy + cut_start + insert_len, text + cut_end, length - cut_end);
    copy[used] = 0;
    if (canvas_unload_autosave(copy, used, &check) != 1 || check.has_value != (value != 0) ||
        (value && check.value != value)) {
        free(copy); return NULL;
    }
    *out_length = used;
    return copy;
}

static int canvas_unload_boot_id(char *out, size_t cap) {
    size_t length = 0;
    char *text = plates_read_file(boot_id_path, 64, &length);
    if (!text) return 0;
    while (length && isspace((unsigned char)text[length - 1])) length--;
    int ok = length && length < cap;
    if (ok) { memcpy(out, text, length); out[length] = 0; }
    free(text);
    return ok;
}

/* Saved since this boot: the pending file holds the boot id it was written in. A file
 * from an earlier boot is stale, so it goes. */
static int canvas_unload_pending(void) {
    char now[64], saved[64]; size_t length = 0;
    char *text = plates_read_file(canvas_unload_pending_path, sizeof(saved) - 1, &length);
    if (!text) return 0;
    snprintf(saved, sizeof(saved), "%.*s", (int)length, text);
    free(text);
    if (canvas_unload_boot_id(now, sizeof(now)) && !strcmp(now, saved)) return 1;
    unlink(canvas_unload_pending_path);
    return 0;
}

static void canvas_unload_number_json(json_builder *b, const char *name, int known, double value) {
    if (known) json_builder_printf(b, ",\"%s\":%.10g", name, value);
    else json_builder_printf(b, ",\"%s\":null", name);
}

static void canvas_unload_state(int fd, int status) {
    size_t length = 0;
    double configured = 0;
    canvas_unload_entry e = {0};
    char *text = plates_read_file(printer_config_path, CANVAS_UNLOAD_CONFIG_MAX, &length);
    int have_configured = text && canvas_unload_configured(text, length, &configured);
    free(text);
    text = plates_read_file(printer_autosave_path, AUTOSAVE_FILE_MAX, &length);
    int editable = text && canvas_unload_autosave(text, length, &e) == 1;
    free(text);
    const char *restart = "none";
    if (canvas_unload_restart_requested) {
        if (reboot_pending) restart = "restarting";
        else if (strcmp(reboot_error, "none")) { restart = reboot_error; canvas_unload_restart_requested = 0; }
    }
    int extended = canvas_unload_extended();
    char body[512];
    json_builder b = {body, 0, sizeof(body), 0};
    json_builder_printf(&b, "{\"ok\":true,\"editable\":%s,\"min\":%d,\"max\":%d,\"extended\":%s", editable ? "true" : "false",
                        CANVAS_UNLOAD_MIN, extended ? CANVAS_UNLOAD_EXTENDED_MAX : CANVAS_UNLOAD_MAX, extended ? "true" : "false");
    canvas_unload_number_json(&b, "configured", have_configured, configured);
    canvas_unload_number_json(&b, "override", editable && e.has_value, editable ? e.value : 0);
    json_builder_printf(&b, ",\"pending_restart\":%s,\"restart\":\"%s\"}\n",
                        canvas_unload_pending() ? "true" : "false", restart);
    plates_reply(fd, b.failed ? 500 : status, b.failed ? "{\"ok\":false,\"error\":\"Reply too large\"}\n" : body);
}

static void canvas_unload_response(int fd) { canvas_unload_state(fd, 200); }

/* Body: a whole number of millimetres, or "default" to remove the override. */
static void canvas_unload_save_response(int fd, const mqtt_client *mqtt, const char *body, size_t body_len) {
    while (body_len && isspace((unsigned char)*body)) { body++; body_len--; }
    while (body_len && isspace((unsigned char)body[body_len - 1])) body_len--;
    int value = -1, maximum = canvas_unload_max();
    if (body_len == 7 && !memcmp(body, "default", 7)) value = 0;
    else if (body_len >= 2 && body_len <= 4) {
        value = 0;
        for (size_t i = 0; i < body_len; ++i) value = isdigit((unsigned char)body[i]) ? value * 10 + body[i] - '0' : -100000;
        if (value < CANVAS_UNLOAD_MIN || value > maximum) value = -1;
    }
    if (value < 0) {
        char message[80];
        snprintf(message, sizeof(message), "Enter a whole number of millimetres from %d to %d", CANVAS_UNLOAD_MIN, maximum);
        plates_fail(fd, 400, message); return;
    }
    const char *busy = plates_printer_ready(mqtt);
    if (busy) { plates_fail(fd, 409, busy); return; }
    size_t length = 0, new_length = 0, again_length = 0;
    char *file = plates_read_file(printer_autosave_path, AUTOSAVE_FILE_MAX, &length);
    if (!file) { plates_fail(fd, 500, "Cannot read the printer configuration"); return; }
    char *updated = canvas_unload_rewrite(file, length, value, &new_length);
    if (!updated) {
        free(file);
        plates_fail(fd, 409, "The printer's autosave.cfg has a layout CC2 Control does not edit");
        return;
    }
    if (new_length == length && !memcmp(updated, file, length)) { /* already so */
        free(file); free(updated); canvas_unload_state(fd, 200); return;
    }
    char backup[PATH_MAX_LOCAL], boot[64];
    /* The firmware may have saved its configuration since the file was read. */
    char *again = plates_read_file(printer_autosave_path, AUTOSAVE_FILE_MAX, &again_length);
    int unchanged = again && again_length == length && !memcmp(again, file, length);
    free(again); free(file);
    if (!unchanged) { free(updated); plates_fail(fd, 409, "The printer changed its configuration meanwhile; try again"); return; }
    /* Above 350 only while the program checked for it is still the one running. */
    if (value > CANVAS_UNLOAD_MAX && !canvas_unload_extended()) {
        free(updated); plates_fail(fd, 409, "The printer program changed; it accepts at most 350 mm"); return;
    }
    /* Record the restart requirement before changing persistent configuration.
     * If /tmp or boot-id access fails, the configuration must stay untouched. */
    int already_pending = canvas_unload_pending();
    if (!canvas_unload_boot_id(boot, sizeof(boot)) ||
        plates_replace_file(canvas_unload_pending_path, NULL, boot, strlen(boot), 0644) != 0) {
        free(updated); plates_fail(fd, 500, "Cannot record the required printer restart; configuration unchanged"); return;
    }
    if (!autosave_backup_path(backup, sizeof(backup)) ||
        plates_replace_file(printer_autosave_path, backup, updated, new_length, autosave_mode()) != 0) {
        if (!already_pending) unlink(canvas_unload_pending_path);
        free(updated); plates_fail(fd, 500, "Cannot write the printer configuration"); return;
    }
    free(updated);
    canvas_unload_state(fd, 200);
}

/* Body: RESTART. Reboots the printer to apply a saved value, once it is still idle. */
static void canvas_unload_restart_response(int fd, mqtt_client *mqtt, const char *body, size_t body_len) {
    while (body_len && isspace((unsigned char)body[body_len - 1])) body_len--;
    if (body_len != 7 || memcmp(body, "RESTART", 7)) { plates_fail(fd, 400, "Explicit restart confirmation required"); return; }
    if (!canvas_unload_pending()) { plates_fail(fd, 409, "No saved pull-back distance is waiting for a restart"); return; }
    const char *busy = plates_printer_ready(mqtt);
    if (busy) { plates_fail(fd, 409, busy); return; }
    canvas_unload_restart_requested = 1; plates_mqtt = mqtt;
    reboot_guard = plates_reboot_guard;
    reboot_pending = 1; reboot_launched = 0; reboot_due = recovery_clock() + 2; reboot_error = "none";
    plates_reply(fd, 202, "{\"ok\":true,\"restarting\":true}\n");
}


