/* Spool tracking: a filament inventory whose spools sit in the Canvas trays or on
 * the external spool holder and are charged with what the printer extrudes.
 *
 * Consumption is measured, not estimated from the slicer. The printer service
 * reports print_stats.filament_used through the UDS subscription: the net
 * extruder travel of the current print in millimetres, with purges and filament
 * changes but without moves made while paused. Every change is charged to the
 * spool in the tray that feeds the extruder at that moment (MQTT
 * canvas_info.active_tray_id; -1 is the external spool holder) and converted to
 * grams with that spool's diameter and density, so a cancelled or failed print
 * is charged with what it really used.
 *
 * The Canvas reports each tray's filament and a status: 0 no filament at the
 * tray's feeder, 1 inserted and pre-loaded, 2 feeding the extruder. Inserting
 * filament or changing a tray's filament on the touchscreen opens a question in
 * the web UI: which spool is this? The tray stays unbound until it is answered;
 * what it extrudes meanwhile is kept and charged to the spool that is chosen.
 * A tray that empties while it feeds a print (status 2 to 0) has run out: once
 * the printer has moved on, its spool is set to zero. A tray that stays empty
 * for 15 s gives its spool back to storage.
 *
 * Everything runs in the main loop. The library file is replaced atomically after
 * every change the user makes, and while printing at most every two minutes. */
#define SPOOLS_MAX 128
#define SPOOL_ID_LEN 16
#define SPOOL_TEXT_MAX 96
#define SPOOL_MATERIAL_MAX 32
#define SPOOL_SLOTS 5            /* Canvas trays 0..3 and the external spool holder */
#define SPOOL_EXTERNAL 4
#define SPOOL_LOG_MAX 150
#define SPOOL_JOB_MAX 128        /* bytes of a print's file name kept in the log */
#define SPOOLS_FILE_MAX (256 * 1024)
#define SPOOL_ABSENT_MS 15000LL  /* an empty tray this long has had its spool taken out */
#define SPOOL_ACCEPT_MS 30000LL  /* after an assignment the tray's new filament data is the spool's */
#define SPOOL_END_MS 8000LL      /* keep counting this long after the printer reports the end */
#define SPOOL_SAVE_MS 120000LL
#define SPOOL_MATCH_MS 30000LL   /* then the printer service's job is this print, whatever its name */
#define SPOOL_FRESH_START 300.0  /* seconds: a print this young is counted from its start */
#define SPOOL_DENSITY 1.24       /* g/cm3 for usage of trays without a spool, and new spools */
#define SPOOL_LOAD_MM 2000.0     /* at most this much is extruded before the printer names the first tray */

typedef struct {
    char id[SPOOL_ID_LEN + 1];
    char name[SPOOL_TEXT_MAX + 1], brand[SPOOL_TEXT_MAX + 1], note[SPOOL_TEXT_MAX + 1];
    char material[SPOOL_MATERIAL_MAX + 1];
    char color[8];                                /* #RRGGBB */
    double diameter, density;                     /* mm, g/cm3 */
    double net, remaining, tare, low, price;      /* grams; price per spool */
    long long created, used;                      /* unix seconds */
    int archived;
} spool_entry;

/* One Canvas tray as the printer reports it. */
typedef struct {
    int have, status;
    char type[SPOOL_MATERIAL_MAX + 1], name[65], color[8], brand[65], code[17];
} spool_tray;

typedef struct {
    char spool[SPOOL_ID_LEN + 1];   /* the spool in this tray, or "" */
    char last[SPOOL_ID_LEN + 1];    /* the spool it held before, suggested when it is refilled */
    spool_tray seen;                /* the filament as last reported (status 0 keeps the filament it had) */
    int question;                   /* 0 none, 1 filament inserted, 2 filament changed; implies no spool */
    long long question_since;
    double question_mm;             /* extruded from this tray while the question was open */
    char question_job[SPOOL_JOB_MAX + 1];
    long long absent_ms, runout_ms, accept_ms; /* monotonic; not saved */
} spool_slot;

/* The print being counted. A segment is one tray's usage while its spool stays the
 * same; it is logged when the spool changes or the print ends. */
typedef struct {
    int active, baseline, last_tray;
    char file[256], uuid[128];
    double used, duration;          /* the last print_stats.filament_used and total_duration */
    double between;                 /* mm extruded since the last tray stopped feeding, for the next one */
    double mm[SPOOL_SLOTS], grams[SPOOL_SLOTS];
    char spool[SPOOL_SLOTS][SPOOL_ID_LEN + 1];
    long long started, started_ms, ending_ms;
    char result[16];
} spool_job;

typedef struct {
    long long time;
    char spool[SPOOL_ID_LEN + 1];
    int slot;                       /* 0..4, or -1 */
    char kind;                      /* p print, r ran out, c correction, n new spool */
    double grams, mm;               /* grams < 0: taken from the spool */
    char job[SPOOL_JOB_MAX + 1], result[16];
} spool_event;

typedef struct {
    int enabled, count, log_count;
    spool_entry spools[SPOOLS_MAX];
    spool_slot slots[SPOOL_SLOTS];
    spool_job job;
    spool_event log[SPOOL_LOG_MAX]; /* oldest first */
} spool_store;

static spool_store spools;
static int spools_available;
static const char *spools_error = "Spool library not loaded";
static unsigned long spools_revision = 1;  /* changes the UI must notice; live remaining excluded */
static int spools_dirty, spools_urgent;
static double spools_unsaved;              /* grams charged since the last save */
static long long spools_saved_ms;
static unsigned long spools_canvas_seen;
/* Tests replace the clocks. */
static long long (*spools_clock)(void) = monotonic_ms;
static time_t (*spools_wall)(time_t *) = time;

/* ---- values ------------------------------------------------------------------------ */

/* Printable UTF-8 without quotes, backslashes, controls or line separators and
 * without surrounding spaces, so stored text never needs escaping. */
static int spool_text_ok(const char *text, size_t max, int empty_ok) {
    size_t n = strlen(text);
    if (!n) return empty_ok;
    if (n > max || text[0] == ' ' || text[n - 1] == ' ') return 0;
    for (size_t i = 0; i < n;) {
        unsigned char ch = (unsigned char)text[i++];
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
            unsigned char next = (unsigned char)text[i++];
            if ((next & 0xc0) != 0x80) return 0;
            value = (value << 6) | (next & 63);
        }
        if ((extra == 2 && value < 0x800) || (extra == 3 && value < 0x10000) || value > 0x10ffff ||
            (value >= 0xd800 && value <= 0xdfff) || (value >= 0x80 && value <= 0x9f) ||
            value == 0x2028 || value == 0x2029) return 0;
    }
    return 1;
}

/* Printer text (raw JSON, file names) made storable: what spool_text_ok refuses
 * becomes '_', the end is cut on a character boundary and spaces are trimmed. */
static void spool_clean(char *out, size_t cap, const char *in, size_t length) {
    size_t n = 0;
    for (size_t i = 0; i < length;) {
        unsigned char ch = (unsigned char)in[i];
        size_t extra = ch >= 0xc2 && ch <= 0xdf ? 1 : ch >= 0xe0 && ch <= 0xef ? 2 : ch >= 0xf0 && ch <= 0xf4 ? 3 : 0;
        if (extra && i + extra < length) {
            char piece[5] = {0};
            memcpy(piece, in + i, extra + 1);
            if (spool_text_ok(piece, 4, 0)) {
                if (n + extra + 1 >= cap) break;
                memcpy(out + n, piece, extra + 1);
                n += extra + 1; i += extra + 1;
                continue;
            }
        }
        if (n + 1 >= cap) break;
        out[n++] = ch < 32 || ch >= 127 || ch == '"' || ch == '\\' ? '_' : (char)ch;
        i++;
    }
    out[n] = 0;
    size_t start = 0;
    while (out[start] == ' ') start++;
    size_t end = strlen(out + start);
    while (end && out[start + end - 1] == ' ') end--;
    memmove(out, out + start, end);
    out[end] = 0;
}

/* "#rrggbb" (or the printer's "#RRGGBBAA") as "#RRGGBB". */
static int spool_color(char out[8], const char *in, size_t length) {
    if ((length != 7 && length != 9) || in[0] != '#') return 0;
    for (size_t i = 1; i < length; ++i) if (!isxdigit((unsigned char)in[i])) return 0;
    out[0] = '#';
    for (int i = 1; i < 7; ++i) out[i] = (char)toupper((unsigned char)in[i]);
    out[7] = 0;
    return 1;
}

static int spool_number(const char *text, double min, double max, double *out) {
    size_t n = text ? strlen(text) : 0;
    if (!n || n > 24 || strspn(text, "0123456789.-+") != n) return 0;
    char *end; errno = 0;
    double value = strtod(text, &end);
    if (end == text || *end || errno || !isfinite(value) || value < min || value > max) return 0;
    *out = value;
    return 1;
}

static double spool_grams_at(double mm, double diameter, double density) {
    return mm * 3.14159265358979323846 / 4.0 * diameter * diameter * density / 1000.0;
}

static double spool_grams(const spool_entry *s, double mm) { return spool_grams_at(mm, s->diameter, s->density); }

static spool_entry *spool_find(const char *id) {
    if (!id || !id[0]) return NULL;
    for (int i = 0; i < spools.count; ++i)
        if (!strcmp(spools.spools[i].id, id)) return &spools.spools[i];
    return NULL;
}

/* Ids are copied between fields of the one store, which snprintf may not overlap. */
static void spool_copy_id(char out[SPOOL_ID_LEN + 1], const char *id) {
    size_t n = id ? strnlen(id, SPOOL_ID_LEN) : 0;
    if (n) memmove(out, id, n);
    out[n] = 0;
}

static int spool_id_ok(const char *id) {
    if (strlen(id) != SPOOL_ID_LEN) return 0;
    for (const char *p = id; *p; ++p)
        if (!isdigit((unsigned char)*p) && (*p < 'a' || *p > 'f')) return 0;
    return 1;
}

static void spools_new_id(char out[SPOOL_ID_LEN + 1]) {
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
    } while (spool_find(out));
}

/* The tray that feeds the extruder: 0..3, or -1 (none or the external holder). */
/* The tray that feeds the extruder, -1 for none. The printer service's own channel comes in the same stream
 * as filament_used, so a colour change is charged from the moment it happens; MQTT's canvas_info reports it
 * seconds later, by when the new colour's purge is well under way. */
static int spools_active_tray(const mqtt_client *mqtt) {
    double channel;
    if (uds_value(&telemetry, U_CANVAS_CHANNEL, &channel)) return channel >= 0 && channel < 4 ? (int)channel : -1;
    return mqtt->have_canvas_active_tray && mqtt->canvas_active_tray_id >= 0 && mqtt->canvas_active_tray_id < 4
               ? mqtt->canvas_active_tray_id : -1;
}

/* How a print ended: the printer service's print_stats.state, else MQTT's; empty while it still prints.
 * At the end MQTT can drop the file name while its state still says printing. */
static const char *spools_end_state(const mqtt_client *mqtt) {
    const char *state = uds_print_state(&telemetry);
    if (!state || !strcmp(state, "printing") || !strcmp(state, "paused") || !strcmp(state, "standby"))
        state = mqtt->print_state;
    return strcmp(state, "printing") && strcmp(state, "paused") ? state : "";
}

/* A change the UI must see. `urgent` saves it in this main-loop turn. */
static void spools_touch(int urgent) {
    spools_revision++;
    spools_dirty = 1;
    if (urgent) spools_urgent = 1;
}

static void spools_log(char kind, const char *spool, int slot, double grams, double mm, const char *job,
                       const char *result) {
    if (spools.log_count == SPOOL_LOG_MAX) {
        memmove(spools.log, spools.log + 1, sizeof(spools.log[0]) * (SPOOL_LOG_MAX - 1));
        spools.log_count--;
    }
    spool_event *e = &spools.log[spools.log_count++];
    memset(e, 0, sizeof(*e));
    e->time = (long long)spools_wall(NULL);
    e->kind = kind; e->slot = slot; e->grams = grams; e->mm = mm;
    spool_copy_id(e->spool, spool);
    spool_clean(e->job, sizeof(e->job), job ? job : "", job ? strlen(job) : 0);
    spool_clean(e->result, sizeof(e->result), result ? result : "", result ? strlen(result) : 0);
    spools_touch(0);
}

/* ---- the print being counted ------------------------------------------------------------- */

/* Logs one tray's usage since its spool last changed and starts a new segment. A tray
 * whose question is open keeps that usage in question_mm instead. */
static void spools_job_split(int index, const char *result) {
    spool_job *job = &spools.job;
    if (!job->active) return;
    if (fabs(job->mm[index]) >= 1.0 && (job->spool[index][0] || !spools.slots[index].question)) {
        double grams = job->spool[index][0] ? job->grams[index] : spool_grams_at(job->mm[index], 1.75, SPOOL_DENSITY);
        spools_log('p', job->spool[index], index, -grams, job->mm[index], job->file, result);
    }
    job->mm[index] = job->grams[index] = 0;
    job->spool[index][0] = 0;
}

static void spools_charge(int index, double mm) {
    spool_job *job = &spools.job;
    spool_slot *s = &spools.slots[index];
    spool_entry *sp = spool_find(s->spool);
    job->mm[index] += mm;
    if (sp) {
        double grams = spool_grams(sp, mm);
        if (!job->spool[index][0]) spool_copy_id(job->spool[index], sp->id);
        job->grams[index] += grams;
        sp->remaining -= grams;
        if (sp->remaining < -1e6) sp->remaining = -1e6;
        sp->used = (long long)spools_wall(NULL);
        spools_unsaved += fabs(grams);
    } else if (s->question) {
        s->question_mm += mm;
        spool_clean(s->question_job, sizeof(s->question_job), job->file, strlen(job->file));
    }
    spools_dirty = 1;
}

static void spools_job_start(const mqtt_client *mqtt) {
    spool_job *job = &spools.job;
    memset(job, 0, sizeof(*job));
    job->active = job->baseline = 1;
    job->last_tray = -1;
    job->duration = -1;
    job->started = (long long)spools_wall(NULL);
    job->started_ms = spools_clock();
    snprintf(job->file, sizeof(job->file), "%s", mqtt->filename);
    snprintf(job->uuid, sizeof(job->uuid), "%s", mqtt->uuid);
    spools_touch(1);
}

static void spools_job_finish(const char *result) {
    spool_job *job = &spools.job;
    /* A print that ends between trays, in its last unload, leaves that to the last tray. */
    if (job->active && job->between != 0 && job->last_tray >= 0) spools_charge(job->last_tray, job->between);
    for (int i = 0; i < SPOOL_SLOTS; ++i) spools_job_split(i, result);
    memset(&spools.job, 0, sizeof(spools.job));
    spools.job.last_tray = -1;
    spools_touch(1);
}

/* ---- trays ------------------------------------------------------------------------------- */

static void spool_tray_text(char *out, size_t cap, const char *obj, const char *end, const char *key) {
    const char *text; int length;
    out[0] = 0;
    if (json_member_raw_string(obj, end, key, &text, &length) && length >= 0) spool_clean(out, cap, text, (size_t)length);
}

/* The trays of the connected Canvas module in the cached snapshot. Returns 0 when no
 * connected module reported its trays. */
static int spools_trays(const mqtt_client *mqtt, spool_tray trays[4]) {
    memset(trays, 0, sizeof(spool_tray) * 4);
    const char *snapshot = mqtt->canvas_snapshot, *end = snapshot + mqtt->canvas_snapshot_len;
    const char *root = json_skip_space(snapshot, end), *root_end, *result_end, *info_end, *list_end, *trays_end;
    if (!mqtt->canvas_snapshot_len || root >= end || *root != '{' || !(root_end = json_container_end(root, end))) return 0;
    const char *result = json_member_object(root, root_end, "result", '{', &result_end);
    const char *info = result ? json_member_object(result, result_end, "canvas_info", '{', &info_end) : NULL;
    const char *list = info ? json_member_object(info, info_end, "canvas_list", '[', &list_end) : NULL;
    if (!list) return 0;
    const char *module = NULL, *module_end = NULL;
    for (const char *item = json_next_element(list, list_end); item;) {
        const char *item_end = json_container_end(item, list_end);
        int connected = 0;
        if (!item_end) return 0;
        if (json_member_int(item, item_end, "connected", &connected) && connected == 1) {
            module = item; module_end = item_end; break;
        }
        item = json_next_element(item_end, list_end);
    }
    const char *tray_list = module ? json_member_object(module, module_end, "tray_list", '[', &trays_end) : NULL;
    if (!tray_list) return 0;
    for (const char *tray = json_next_element(tray_list, trays_end); tray;) {
        const char *tray_end = json_container_end(tray, trays_end);
        int id, status;
        if (!tray_end) return 0;
        if (json_member_int(tray, tray_end, "tray_id", &id) && id >= 0 && id < 4) {
            spool_tray *t = &trays[id];
            const char *text; int length;
            t->have = 1;
            /* Without a status the tray counts as filled: only edits are seen then. */
            t->status = json_member_int(tray, tray_end, "status", &status) && status >= 0 && status <= 2 ? status : 1;
            spool_tray_text(t->type, sizeof(t->type), tray, tray_end, "filament_type");
            spool_tray_text(t->name, sizeof(t->name), tray, tray_end, "filament_name");
            spool_tray_text(t->brand, sizeof(t->brand), tray, tray_end, "brand");
            spool_tray_text(t->code, sizeof(t->code), tray, tray_end, "filament_code");
            if (!json_member_raw_string(tray, tray_end, "filament_color", &text, &length) || length < 0 ||
                !spool_color(t->color, text, (size_t)length)) t->color[0] = 0;
        }
        tray = json_next_element(tray_end, trays_end);
    }
    return 1;
}

static int spool_tray_same(const spool_tray *a, const spool_tray *b) {
    return !strcmp(a->type, b->type) && !strcmp(a->name, b->name) && !strcmp(a->color, b->color) &&
           !strcmp(a->brand, b->brand) && !strcmp(a->code, b->code);
}

/* A product line such as "PLA Matte", "PLA+" or "PLA-CF" belongs to the base type "PLA". */
static int spool_base_type(const char *material, const char *type) {
    size_t n = strlen(type);
    return n && !strncasecmp(material, type, n) &&
           (!material[n] || material[n] == ' ' || material[n] == '-' || material[n] == '+');
}

/* Whether a tray reports this spool's material and colour (nothing reported fits). */
static int spool_fits(const spool_entry *s, const spool_tray *t) {
    int material = !t->type[0] || !strcasecmp(s->material, t->type) || !strcasecmp(s->material, t->name) ||
                   spool_base_type(s->material, t->type);
    return material && (!t->color[0] || !strcmp(s->color, t->color));
}

/* Unbinds a tray; its spool goes back to storage and is remembered as the tray's last.
 * `removed`: the filament left the tray, so an open question goes with it. */
static void spools_release(int index, int removed, const char *result) {
    spool_slot *s = &spools.slots[index];
    spools_job_split(index, result);
    if (s->spool[0]) {
        spool_copy_id(s->last, s->spool);
        s->spool[0] = 0;
    }
    if (removed) {
        s->question = 0; s->question_mm = 0; s->question_job[0] = 0; s->question_since = 0;
    }
    s->runout_ms = 0;
    spools_touch(1);
}

static void spools_ask(int index, int reason) {
    spool_slot *s = &spools.slots[index];
    if (s->spool[0] || !s->question) {
        spools_release(index, 0, "changed");
        s->question_mm = 0; s->question_job[0] = 0;
    }
    s->question = reason;
    s->question_since = (long long)spools_wall(NULL);
    spools_touch(1);
}

/* The spool of a tray that ran out is empty, whatever the count said: the difference
 * is logged, so the log shows how far the count was off. */
static void spools_runout(int index) {
    spool_entry *sp = spool_find(spools.slots[index].spool);
    char file[SPOOL_JOB_MAX + 1];
    spool_clean(file, sizeof(file), spools.job.file, spools.job.active ? strlen(spools.job.file) : 0);
    spools_release(index, 1, "runout");
    if (!sp) return;
    spools_log('r', sp->id, index, -sp->remaining, 0, file, "runout");
    sp->remaining = 0;
    sp->used = (long long)spools_wall(NULL);
}

/* Compares a new tray report with the last one: inserted, emptied or changed filament. */
static void spools_canvas(const mqtt_client *mqtt, long long now) {
    spool_tray trays[4];
    if (mqtt->canvas_revision == spools_canvas_seen) return;
    spools_canvas_seen = mqtt->canvas_revision;
    if (!spools_trays(mqtt, trays)) return;
    for (int i = 0; i < 4; ++i) {
        spool_slot *s = &spools.slots[i];
        const spool_tray *t = &trays[i];
        if (!t->have) continue;
        int present = t->status != 0;
        if (!s->seen.have) { s->seen = *t; spools_dirty = 1; continue; } /* first report: nothing happened */
        int was = s->seen.status != 0;
        if (was && !present) {
            /* Feeding filament that ends at the tray has run out; unloaded filament was taken out. */
            if (s->seen.status == 2 && s->spool[0] && spools.job.active) { if (!s->runout_ms) s->runout_ms = now; }
            else if (!s->absent_ms) s->absent_ms = now;
            s->seen.status = 0;
            spools_dirty = 1;
        } else if (!was && present) {
            int back = (s->runout_ms || (s->absent_ms && now - s->absent_ms < SPOOL_ABSENT_MS)) &&
                       spool_tray_same(&s->seen, t);
            if (!back) {
                if (s->runout_ms) spools_runout(i);
                else if (s->absent_ms) spools_release(i, 1, "removed");
                spools_ask(i, 1);
            }
            s->absent_ms = s->runout_ms = 0;
            s->seen = *t;
            spools_dirty = 1;
        } else if (!present) {
            /* Still empty, as after a restart: a spool or question left there goes after the delay. */
            if ((s->spool[0] || s->question) && !s->absent_ms && !s->runout_ms) s->absent_ms = now;
        } else {
            if (!spool_tray_same(&s->seen, t)) {
                const spool_entry *sp = spool_find(s->spool);
                if (!sp || (now >= s->accept_ms && !spool_fits(sp, t))) spools_ask(i, s->question == 1 ? 1 : 2);
                spools_dirty = 1;
            }
            if (s->seen.status != t->status) spools_dirty = 1;
            s->seen = *t;
        }
    }
}

/* Checks the bindings against the trays when tracking is switched on: spools swapped
 * while it was off would otherwise be charged for someone else's filament. */
static void spools_verify(const mqtt_client *mqtt) {
    spool_tray trays[4];
    spools_canvas_seen = mqtt->canvas_revision;
    if (!spools_trays(mqtt, trays)) return;
    for (int i = 0; i < 4; ++i) {
        spool_slot *s = &spools.slots[i];
        if (!trays[i].have) continue;
        s->absent_ms = s->runout_ms = 0;
        const spool_entry *sp = spool_find(s->spool);
        if (trays[i].status == 0) { if (s->spool[0] || s->question) spools_release(i, 1, "removed"); }
        else if (sp && !spool_fits(sp, &trays[i])) spools_ask(i, 2);
        s->seen = trays[i];
    }
    spools_dirty = 1;
}

/* ---- counting ----------------------------------------------------------------------------- */

/* Whether the printer service's print_stats belong to this print. Its file name can
 * carry another media prefix; and once a print runs, there is no other one. */
static int spools_job_file(const spool_job *job, long long now) {
    if (uds_job_matches(&telemetry, job->file)) return 1;
    const char *a = strrchr(telemetry.filename, '/'), *b = strrchr(job->file, '/');
    a = a ? a + 1 : telemetry.filename; b = b ? b + 1 : job->file;
    return telemetry.have_filename && *a && (!strcmp(a, b) || now - job->started_ms >= SPOOL_MATCH_MS);
}

static void spools_account(const mqtt_client *mqtt, long long now) {
    spool_job *job = &spools.job;
    time_t wall = spools_wall(NULL);
    int fresh = mqtt->connected && mqtt->last_message > 0 && wall >= mqtt->last_message &&
                wall - mqtt->last_message <= 15 && mqtt->have_machine_status;
    int printing = fresh && mqtt->filename[0] &&
                   (!strcmp(mqtt->print_state, "printing") || !strcmp(mqtt->print_state, "paused"));
    if (printing && job->active &&
        (strcmp(job->file, mqtt->filename) || (job->uuid[0] && mqtt->uuid[0] && strcmp(job->uuid, mqtt->uuid))))
        spools_job_finish(job->result[0] ? job->result : "ended");
    if (printing && !job->active) spools_job_start(mqtt);
    if (!job->active) return;
    if (printing) job->ending_ms = 0;
    else if (fresh) {
        if (!job->ending_ms) job->ending_ms = now;
        const char *end = spools_end_state(mqtt);
        if (end[0]) spool_clean(job->result, sizeof(job->result), end, strlen(end));
    }
    double used, duration, flow;
    if (uds_value(&telemetry, U_FILAMENT_USED, &used) && spools_job_file(job, now)) {
        int timed = uds_value(&telemetry, U_TOTAL_DURATION, &duration);
        if (job->baseline) {
            job->used = timed && duration < SPOOL_FRESH_START ? 0 : used;
            job->baseline = 0;
            spools_dirty = 1;
        } else if (timed && job->duration >= 0 && duration + 5 < job->duration) {
            /* print_stats restarted: the same file is printing again. */
            spools_job_finish(job->result[0] ? job->result : "ended");
            spools_job_start(mqtt);
            job->baseline = 0;
        }
        if (timed) job->duration = duration;
        double delta = used - job->used;
        if (fabs(delta) > 1e5) delta = 0; /* no print moves 100 m between two readings: resynchronise */
        job->used = used;
        if (delta != 0) {
            int tray = spools_active_tray(mqtt);
            if (!uds_value(&telemetry, U_FLOW_FACTOR, &flow) || flow <= 0) flow = 1;
            /* filament_used counts G-code travel; the flow override scales the real feed. */
            double mm = delta * flow;
            if (tray < 0 && job->last_tray >= 0) {
                /* Between two trays the old filament is cut and pulled back unseen, and the extruder pulls in
                 * the next one: what it moves now waits for the tray named next, or at the end the last one. */
                job->between += mm;
                spools_dirty = 1;
            } else {
                /* The printer names the first tray only once it has loaded it, so what the load extruded went
                 * to the external holder: it came from this tray. A print from the external holder names none. */
                if (tray >= 0 && job->last_tray < 0 && job->mm[SPOOL_EXTERNAL] != 0 &&
                    fabs(job->mm[SPOOL_EXTERNAL]) <= SPOOL_LOAD_MM) {
                    double load = job->mm[SPOOL_EXTERNAL];
                    spools_charge(SPOOL_EXTERNAL, -load);
                    spools_charge(tray, load);
                }
                if (tray >= 0 && job->between != 0) {
                    spools_charge(tray, job->between);
                    job->between = 0;
                }
                if (tray >= 0) job->last_tray = tray;
                spools_charge(tray >= 0 ? tray : SPOOL_EXTERNAL, mm);
            }
        }
    }
    if (job->ending_ms && now - job->ending_ms >= SPOOL_END_MS) spools_job_finish(job->result[0] ? job->result : "ended");
}

/* ---- the library file ---------------------------------------------------------------------- */

static void spools_entry_json(json_builder *b, const spool_entry *s) {
    json_builder_printf(b, "{\"id\":\"%s\",\"name\":", s->id);
    json_builder_string(b, s->name);
    json_builder_printf(b, ",\"brand\":");
    json_builder_string(b, s->brand);
    json_builder_printf(b, ",\"material\":");
    json_builder_string(b, s->material);
    json_builder_printf(b, ",\"color\":\"%s\",\"diameter\":%.3f,\"density\":%.3f,\"net\":%.1f,\"remaining\":%.3f,"
        "\"tare\":%.1f,\"low\":%.1f,\"price\":%.2f,\"note\":", s->color, s->diameter, s->density, s->net,
        s->remaining, s->tare, s->low, s->price);
    json_builder_string(b, s->note);
    json_builder_printf(b, ",\"created\":%lld,\"used\":%lld,\"archived\":%s}", s->created, s->used,
                        s->archived ? "true" : "false");
}

static void spools_tray_json(json_builder *b, const spool_tray *t) {
    json_builder_printf(b, "{\"status\":%d,\"type\":", t->status);
    json_builder_string(b, t->type);
    json_builder_printf(b, ",\"name\":");
    json_builder_string(b, t->name);
    json_builder_printf(b, ",\"color\":\"%s\",\"brand\":", t->color);
    json_builder_string(b, t->brand);
    json_builder_printf(b, ",\"code\":");
    json_builder_string(b, t->code);
    json_builder_printf(b, "}");
}

static const char *spools_question_name(int question) {
    return question == 1 ? "inserted" : question == 2 ? "changed" : "";
}

static void spools_event_json(json_builder *b, const spool_event *e) {
    json_builder_printf(b, "{\"time\":%lld,\"spool\":\"%s\",\"slot\":%d,\"kind\":\"%s\",\"grams\":%.3f,\"mm\":%.1f,\"job\":",
        e->time, e->spool, e->slot,
        e->kind == 'p' ? "print" : e->kind == 'r' ? "runout" : e->kind == 'c' ? "correction" : "new", e->grams, e->mm);
    json_builder_string(b, e->job);
    json_builder_printf(b, ",\"result\":");
    json_builder_string(b, e->result);
    json_builder_printf(b, "}");
}

static int spools_save(void) {
    json_builder b = {malloc(SPOOLS_FILE_MAX), 0, SPOOLS_FILE_MAX, 0};
    spools_saved_ms = spools_clock();
    if (!b.data) return -1;
    const spool_job *job = &spools.job;
    json_builder_printf(&b, "{\"version\":1,\"enabled\":%s,\"slots\":[", spools.enabled ? "true" : "false");
    for (int i = 0; i < SPOOL_SLOTS; ++i) {
        const spool_slot *s = &spools.slots[i];
        json_builder_printf(&b, "%s{\"spool\":\"%s\",\"last\":\"%s\",\"question\":%d,\"question_since\":%lld,"
            "\"question_mm\":%.3f,\"question_job\":", i ? "," : "", s->spool, s->last, s->question, s->question_since,
            s->question_mm);
        json_builder_string(&b, s->question_job);
        json_builder_printf(&b, ",\"seen\":");
        if (s->seen.have) spools_tray_json(&b, &s->seen);
        else json_builder_printf(&b, "null");
        json_builder_printf(&b, "}");
    }
    json_builder_printf(&b, "],\"job\":{\"active\":%s,\"baseline\":%s,\"last_tray\":%d,\"file\":",
                        job->active ? "true" : "false", job->baseline ? "true" : "false", job->last_tray);
    json_builder_string(&b, job->file);
    json_builder_printf(&b, ",\"uuid\":");
    json_builder_string(&b, job->uuid);
    json_builder_printf(&b, ",\"used\":%.3f,\"duration\":%.1f,\"between\":%.3f,\"started\":%lld,\"result\":", job->used,
                        job->duration, job->between, job->started);
    json_builder_string(&b, job->result);
    json_builder_printf(&b, ",\"slots\":[");
    for (int i = 0; i < SPOOL_SLOTS; ++i)
        json_builder_printf(&b, "%s{\"mm\":%.3f,\"grams\":%.4f,\"spool\":\"%s\"}", i ? "," : "", job->mm[i],
                            job->grams[i], job->spool[i]);
    json_builder_printf(&b, "]},\"spools\":[");
    for (int i = 0; i < spools.count; ++i) {
        if (i) json_builder_printf(&b, ",");
        spools_entry_json(&b, &spools.spools[i]);
    }
    json_builder_printf(&b, "],\"log\":[");
    for (int i = 0; i < spools.log_count; ++i) {
        if (i) json_builder_printf(&b, ",");
        spools_event_json(&b, &spools.log[i]);
    }
    json_builder_printf(&b, "]}\n");
    int result = b.failed ? -1 : plates_replace_file(spools_path, NULL, b.data, b.length, 0644);
    free(b.data);
    if (!result) { spools_dirty = 0; spools_urgent = 0; spools_unsaved = 0; }
    return result;
}

static int spools_text_member(const char *obj, const char *end, const char *key, char *out, size_t cap, size_t max,
                              int empty_ok) {
    const char *text; int length;
    out[0] = 0;
    if (!json_member_raw_string(obj, end, key, &text, &length) || length < 0 || (size_t)length > max ||
        (size_t)length >= cap) return 0;
    memcpy(out, text, (size_t)length); out[length] = 0;
    return spool_text_ok(out, max, empty_ok);
}

/* A JSON string as json_builder_string wrote it, unescaped. */
static int spools_raw_member(const char *obj, const char *end, const char *key, char *out, size_t cap) {
    const char *text; int length; size_t n = 0;
    if (!json_member_raw_string(obj, end, key, &text, &length) || length < 0) return 0;
    for (int i = 0; i < length; ++i) {
        char ch = text[i];
        if (ch == '\\') {
            if (++i >= length) return 0;
            ch = text[i];
            if (ch == 'u') {
                unsigned int value = 0;
                for (int k = 0; k < 4; ++k) {
                    if (++i >= length || !isxdigit((unsigned char)text[i])) return 0;
                    value = value * 16 + (unsigned int)(isdigit((unsigned char)text[i]) ? text[i] - '0' : (tolower((unsigned char)text[i]) - 'a' + 10));
                }
                if (!value || value >= 32) return 0;
                ch = (char)value;
            } else if (ch != '"' && ch != '\\' && ch != '/') return 0;
        }
        if (n + 1 >= cap) return 0;
        out[n++] = ch;
    }
    out[n] = 0;
    return 1;
}

static int spools_bool_member(const char *obj, const char *end, const char *key, int *out) {
    const char *value = json_member(obj, end, key);
    if (value && end - value >= 4 && !memcmp(value, "true", 4)) { *out = 1; return 1; }
    if (value && end - value >= 5 && !memcmp(value, "false", 5)) { *out = 0; return 1; }
    return 0;
}

static int spools_id_member(const char *obj, const char *end, const char *key, char out[SPOOL_ID_LEN + 1]) {
    const char *text; int length;
    out[0] = 0;
    if (!json_member_raw_string(obj, end, key, &text, &length) || (length && length != SPOOL_ID_LEN)) return 0;
    memcpy(out, text, (size_t)length); out[length] = 0;
    return !length || spool_id_ok(out);
}

static int spools_time_member(const char *obj, const char *end, const char *key, long long *out) {
    double value;
    if (!plates_member_double(obj, end, key, &value) || value < 0 || value > 1e11 || value != floor(value)) return 0;
    *out = (long long)value;
    return 1;
}

static int spools_parse_entry(const char *obj, const char *end, spool_entry *s) {
    const char *text; int length;
    memset(s, 0, sizeof(*s));
    return spools_id_member(obj, end, "id", s->id) && s->id[0] &&
        spools_text_member(obj, end, "name", s->name, sizeof(s->name), SPOOL_TEXT_MAX, 1) &&
        spools_text_member(obj, end, "brand", s->brand, sizeof(s->brand), SPOOL_TEXT_MAX, 1) &&
        spools_text_member(obj, end, "material", s->material, sizeof(s->material), SPOOL_MATERIAL_MAX, 0) &&
        spools_text_member(obj, end, "note", s->note, sizeof(s->note), SPOOL_TEXT_MAX, 1) &&
        json_member_raw_string(obj, end, "color", &text, &length) && length == 7 &&
        spool_color(s->color, text, (size_t)length) &&
        plates_member_double(obj, end, "diameter", &s->diameter) && s->diameter >= 1 && s->diameter <= 3.5 &&
        plates_member_double(obj, end, "density", &s->density) && s->density >= 0.5 && s->density <= 3 &&
        plates_member_double(obj, end, "net", &s->net) && s->net >= 1 && s->net <= 20000 &&
        plates_member_double(obj, end, "remaining", &s->remaining) && s->remaining >= -1e6 && s->remaining <= 25000 &&
        plates_member_double(obj, end, "tare", &s->tare) && s->tare >= 0 && s->tare <= 5000 &&
        plates_member_double(obj, end, "low", &s->low) && s->low >= 0 && s->low <= 20000 &&
        plates_member_double(obj, end, "price", &s->price) && s->price >= 0 && s->price <= 1e7 &&
        spools_time_member(obj, end, "created", &s->created) && spools_time_member(obj, end, "used", &s->used) &&
        spools_bool_member(obj, end, "archived", &s->archived);
}

static int spools_parse_tray(const char *obj, const char *end, spool_tray *t) {
    const char *text; int length;
    memset(t, 0, sizeof(*t));
    if (!json_member_int(obj, end, "status", &t->status) || t->status < 0 || t->status > 2 ||
        !spools_text_member(obj, end, "type", t->type, sizeof(t->type), SPOOL_MATERIAL_MAX, 1) ||
        !spools_text_member(obj, end, "name", t->name, sizeof(t->name), 64, 1) ||
        !spools_text_member(obj, end, "brand", t->brand, sizeof(t->brand), 64, 1) ||
        !spools_text_member(obj, end, "code", t->code, sizeof(t->code), 16, 1) ||
        !json_member_raw_string(obj, end, "color", &text, &length) ||
        (length && (length != 7 || !spool_color(t->color, text, (size_t)length)))) return 0;
    t->have = 1;
    return 1;
}

static int spools_parse_slot(const char *obj, const char *end, spool_slot *s) {
    memset(s, 0, sizeof(*s));
    if (!spools_id_member(obj, end, "spool", s->spool) || !spools_id_member(obj, end, "last", s->last) ||
        !json_member_int(obj, end, "question", &s->question) || s->question < 0 || s->question > 2 ||
        (s->question && s->spool[0]) || !spools_time_member(obj, end, "question_since", &s->question_since) ||
        !plates_member_double(obj, end, "question_mm", &s->question_mm) || fabs(s->question_mm) > 1e9 ||
        !spools_text_member(obj, end, "question_job", s->question_job, sizeof(s->question_job), SPOOL_JOB_MAX, 1))
        return 0;
    const char *seen = json_member(obj, end, "seen"), *seen_end;
    if (!seen) return 0;
    if (*seen == '{') {
        seen_end = json_container_end(seen, end);
        return seen_end && spools_parse_tray(seen, seen_end, &s->seen);
    }
    return end - seen >= 4 && !memcmp(seen, "null", 4);
}

static int spools_parse_job(const char *obj, const char *end, spool_job *job) {
    const char *list, *list_end = NULL;
    memset(job, 0, sizeof(*job));
    if (!spools_bool_member(obj, end, "active", &job->active) ||
        !spools_bool_member(obj, end, "baseline", &job->baseline) ||
        !json_member_int(obj, end, "last_tray", &job->last_tray) || job->last_tray < -1 || job->last_tray > 3 ||
        !plates_member_double(obj, end, "used", &job->used) || fabs(job->used) > 1e9 ||
        !plates_member_double(obj, end, "duration", &job->duration) || job->duration < -1 || job->duration > 1e9 ||
        !spools_time_member(obj, end, "started", &job->started) ||
        !spools_text_member(obj, end, "result", job->result, sizeof(job->result), sizeof(job->result) - 1, 1)) return 0;
    /* Files written before the count between trays have none. */
    double between;
    if (plates_member_double(obj, end, "between", &between) && fabs(between) <= 1e9) job->between = between;
    /* A name that cannot be restored only loses the print in progress. */
    if (!spools_raw_member(obj, end, "file", job->file, sizeof(job->file)) ||
        !spools_raw_member(obj, end, "uuid", job->uuid, sizeof(job->uuid))) job->active = 0;
    list = json_member_object(obj, end, "slots", '[', &list_end);
    int i = 0;
    for (const char *item = list ? json_next_element(list, list_end) : NULL; item; ++i) {
        const char *item_end = json_container_end(item, list_end);
        if (!item_end || i >= SPOOL_SLOTS || !plates_member_double(item, item_end, "mm", &job->mm[i]) ||
            !plates_member_double(item, item_end, "grams", &job->grams[i]) || fabs(job->mm[i]) > 1e9 ||
            fabs(job->grams[i]) > 1e7 || !spools_id_member(item, item_end, "spool", job->spool[i])) return 0;
        item = json_next_element(item_end, list_end);
    }
    if (!job->active) { memset(job, 0, sizeof(*job)); job->last_tray = -1; return list && i == SPOOL_SLOTS; }
    job->started_ms = spools_clock() - SPOOL_MATCH_MS; /* the print was running before this process */
    return list && i == SPOOL_SLOTS;
}

static int spools_parse_event(const char *obj, const char *end, spool_event *e) {
    const char *text; int length;
    memset(e, 0, sizeof(*e));
    if (!spools_time_member(obj, end, "time", &e->time) || !spools_id_member(obj, end, "spool", e->spool) ||
        !json_member_int(obj, end, "slot", &e->slot) || e->slot < -1 || e->slot >= SPOOL_SLOTS ||
        !plates_member_double(obj, end, "grams", &e->grams) || fabs(e->grams) > 1e7 ||
        !plates_member_double(obj, end, "mm", &e->mm) || fabs(e->mm) > 1e9 ||
        !spools_text_member(obj, end, "job", e->job, sizeof(e->job), SPOOL_JOB_MAX, 1) ||
        !spools_text_member(obj, end, "result", e->result, sizeof(e->result), sizeof(e->result) - 1, 1) ||
        !json_member_raw_string(obj, end, "kind", &text, &length)) return 0;
    static const char *kinds[] = {"print", "runout", "correction", "new"};
    for (size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); ++k)
        if ((size_t)length == strlen(kinds[k]) && !memcmp(text, kinds[k], (size_t)length)) e->kind = "prcn"[k];
    return e->kind != 0;
}

/* A missing file is an empty library with tracking off; anything unreadable keeps it
 * closed, so a damaged file is never overwritten. */
static void spools_load(void) {
    memset(&spools, 0, sizeof(spools));
    spools.job.last_tray = -1;
    spools_available = 0;
    spools_dirty = spools_urgent = 0;
    spools_unsaved = 0;
    size_t length = 0;
    char *text = plates_read_file(spools_path, SPOOLS_FILE_MAX, &length);
    if (!text) {
        spools_available = errno == ENOENT;
        spools_error = spools_available ? "" : "The spool library file is unreadable";
        return;
    }
    spools_error = "The spool library file is unreadable";
    const char *end = text + length, *root = json_skip_space(text, end), *root_end, *list, *list_end = NULL, *job,
               *job_end = NULL;
    int version, ok = 0;
    if (root < end && *root == '{' && (root_end = json_container_end(root, end)) &&
        json_member_int(root, root_end, "version", &version) && version == 1 &&
        spools_bool_member(root, root_end, "enabled", &spools.enabled) &&
        (job = json_member_object(root, root_end, "job", '{', &job_end)) &&
        spools_parse_job(job, job_end, &spools.job)) {
        list = json_member_object(root, root_end, "spools", '[', &list_end);
        ok = list != NULL;
        for (const char *item = list ? json_next_element(list, list_end) : NULL; ok && item;) {
            const char *item_end = json_container_end(item, list_end);
            spool_entry *s = &spools.spools[spools.count];
            ok = item_end && spools.count < SPOOLS_MAX && spools_parse_entry(item, item_end, s) && !spool_find(s->id);
            if (ok) { spools.count++; item = json_next_element(item_end, list_end); }
        }
        list = ok ? json_member_object(root, root_end, "slots", '[', &list_end) : NULL;
        int slot = 0;
        ok = list != NULL;
        for (const char *item = list ? json_next_element(list, list_end) : NULL; ok && item; ++slot) {
            const char *item_end = json_container_end(item, list_end);
            ok = item_end && slot < SPOOL_SLOTS && spools_parse_slot(item, item_end, &spools.slots[slot]);
            if (ok) item = json_next_element(item_end, list_end);
        }
        ok = ok && slot == SPOOL_SLOTS;
        list = ok ? json_member_object(root, root_end, "log", '[', &list_end) : NULL;
        ok = ok && list != NULL;
        for (const char *item = list ? json_next_element(list, list_end) : NULL; ok && item;) {
            const char *item_end = json_container_end(item, list_end);
            ok = item_end && spools.log_count < SPOOL_LOG_MAX &&
                 spools_parse_event(item, item_end, &spools.log[spools.log_count]);
            if (ok) { spools.log_count++; item = json_next_element(item_end, list_end); }
        }
        /* A binding to a spool that is gone is dropped; one spool in two places is damage. */
        for (int i = 0; ok && i < SPOOL_SLOTS; ++i) {
            if (spools.slots[i].spool[0] && !spool_find(spools.slots[i].spool)) spools.slots[i].spool[0] = 0;
            for (int k = 0; k < i; ++k)
                if (spools.slots[i].spool[0] && !strcmp(spools.slots[i].spool, spools.slots[k].spool)) ok = 0;
        }
    }
    free(text);
    if (ok) { spools_available = 1; spools_error = ""; }
    else { memset(&spools, 0, sizeof(spools)); spools.job.last_tray = -1; }
}

/* Main loop: tray events, counting, and saving what changed. */
static void spools_tick(const mqtt_client *mqtt) {
    if (!spools_available) return;
    long long now = spools_clock();
    if (spools.enabled) {
        spools_canvas(mqtt, now);
        spools_account(mqtt, now);
        int active = spools_active_tray(mqtt);
        for (int i = 0; i < 4; ++i) {
            spool_slot *s = &spools.slots[i];
            if (s->absent_ms && now - s->absent_ms >= SPOOL_ABSENT_MS) {
                s->absent_ms = 0;
                if (s->spool[0] || s->question) spools_release(i, 1, "removed");
            }
            if (s->runout_ms && now - s->runout_ms >= SPOOL_ABSENT_MS && (!spools.job.active || active != i))
                spools_runout(i);
        }
    }
    if (spools_dirty && (spools_urgent || now - spools_saved_ms >= SPOOL_SAVE_MS ||
                         (spools_unsaved >= 5 && now - spools_saved_ms >= 30000)))
        (void)spools_save();
}

/* ---- HTTP ------------------------------------------------------------------------------------ */

#define SPOOL_FIELDS_MAX 16
typedef struct {
    int count;
    char key[SPOOL_FIELDS_MAX][16];
    char value[SPOOL_FIELDS_MAX][SPOOL_TEXT_MAX + 1];
} spool_form;

/* key=value lines; a value is everything after the first '='. */
static int spool_form_parse(const char *body, size_t length, spool_form *f) {
    memset(f, 0, sizeof(*f));
    while (length && (body[length - 1] == '\n' || body[length - 1] == '\r')) length--;
    for (size_t pos = 0; length && pos <= length;) {
        const char *eol = memchr(body + pos, '\n', length - pos);
        size_t line_end = eol ? (size_t)(eol - body) : length, line_len = line_end - pos;
        if (line_len && body[line_end - 1] == '\r') line_len--;
        const char *line = body + pos, *equals = memchr(line, '=', line_len);
        if (!equals || f->count >= SPOOL_FIELDS_MAX || memchr(line, '\0', line_len)) return 0;
        size_t key_len = (size_t)(equals - line), value_len = line_len - key_len - 1;
        if (!key_len || key_len >= sizeof(f->key[0]) || value_len > SPOOL_TEXT_MAX) return 0;
        memcpy(f->key[f->count], line, key_len); f->key[f->count][key_len] = 0;
        memcpy(f->value[f->count], equals + 1, value_len); f->value[f->count][value_len] = 0;
        for (int i = 0; i < f->count; ++i) if (!strcmp(f->key[i], f->key[f->count])) return 0;
        f->count++;
        pos = line_end + 1;
    }
    return 1;
}

static const char *spool_form_get(const spool_form *f, const char *key) {
    for (int i = 0; i < f->count; ++i) if (!strcmp(f->key[i], key)) return f->value[i];
    return NULL;
}

static void spools_reply(int fd, int status, const char *body) {
    const char *text = status == 200 ? "OK" : status == 201 ? "Created" : status == 400 ? "Bad Request" :
        status == 404 ? "Not Found" : status == 409 ? "Conflict" : status == 503 ? "Service Unavailable" :
        "Internal Server Error";
    respond(fd, status, text, "application/json; charset=utf-8", body, strlen(body));
}

static void spools_fail(int fd, int status, const char *error) {
    char body[320], escaped[256];
    json_escape(escaped, sizeof(escaped), error);
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}\n", escaped);
    spools_reply(fd, status, body);
}

/* Keeps a copy to undo a change that cannot be saved. */
static spool_store *spools_snapshot(int fd) {
    spool_store *copy = malloc(sizeof(spools));
    if (copy) *copy = spools;
    else spools_fail(fd, 500, "Out of memory");
    return copy;
}

/* Saves a change made over HTTP; when that fails, memory goes back to `before`. */
static int spools_commit(int fd, spool_store *before) {
    spools_touch(1);
    int ok = spools_save() == 0;
    if (!ok) {
        spools = *before;
        spools_touch(0);
        spools_fail(fd, 500, "Cannot save the spool library");
    }
    free(before);
    return ok;
}

static int spools_open(int fd) {
    if (spools_available) return 1;
    spools_fail(fd, 503, spools_error);
    return 0;
}

/* "0".."3" for a Canvas tray, "4" or "external" for the external spool holder. */
static int spool_slot_value(const char *text) {
    if (!text) return -1;
    if (!strcmp(text, "external")) return SPOOL_EXTERNAL;
    return strlen(text) == 1 && text[0] >= '0' && text[0] <= '4' ? text[0] - '0' : -1;
}

/* Puts a spool into a tray. A spool sits in one place only, so it leaves any other.
 * What the tray extruded while its question was open is charged to it. */
static void spools_bind(int index, spool_entry *sp) {
    spool_slot *s = &spools.slots[index];
    spool_job *job = &spools.job;
    for (int i = 0; i < SPOOL_SLOTS; ++i)
        if (i != index && !strcmp(spools.slots[i].spool, sp->id)) spools_release(i, 0, "moved");
    if (strcmp(s->spool, sp->id)) {
        if (s->spool[0]) spools_release(index, 0, "changed");
        else if (!s->question) spools_job_split(index, "changed");
        if (s->question && s->question_mm != 0) {
            double grams = spool_grams(sp, s->question_mm);
            sp->remaining -= grams;
            sp->used = (long long)spools_wall(NULL);
            if (job->active && !job->spool[index][0] && fabs(job->mm[index] - s->question_mm) < 1e-6) {
                /* The open segment is this print's usage from the tray: it becomes the spool's. */
                spool_copy_id(job->spool[index], sp->id);
                job->grams[index] = grams;
            } else {
                spools_log('p', sp->id, index, -grams, s->question_mm, s->question_job, "late");
                if (job->active && !job->spool[index][0]) job->mm[index] = job->grams[index] = 0;
            }
        }
        spool_copy_id(s->spool, sp->id);
    }
    s->question = 0; s->question_mm = 0; s->question_job[0] = 0; s->question_since = 0;
    s->absent_ms = s->runout_ms = 0;
    s->accept_ms = spools_clock() + SPOOL_ACCEPT_MS;
    spools_touch(1);
}

static void spools_get_response(int fd, const mqtt_client *mqtt) {
    spool_tray trays[4];
    int canvas = spools_trays(mqtt, trays);
    size_t cap = SPOOLS_FILE_MAX + 16384;
    json_builder b = {malloc(cap), 0, cap, 0};
    if (!b.data) { spools_fail(fd, 500, "Out of memory"); return; }
    json_builder_printf(&b, "{\"available\":%s,\"error\":", spools_available ? "true" : "false");
    json_builder_string(&b, spools_error);
    json_builder_printf(&b, ",\"enabled\":%s,\"revision\":%lu,\"canvas\":%s,\"active_tray\":%d,\"slots\":[",
        spools.enabled ? "true" : "false", spools_revision, canvas ? "true" : "false", spools_active_tray(mqtt));
    for (int i = 0; i < SPOOL_SLOTS; ++i) {
        const spool_slot *s = &spools.slots[i];
        json_builder_printf(&b, "%s{\"slot\":%d,\"spool\":\"%s\",\"last\":\"%s\",\"question\":\"%s\","
            "\"question_since\":%lld,\"question_mm\":%.1f,\"runout\":%s,\"printer\":", i ? "," : "", i, s->spool,
            s->last, spools_question_name(s->question), s->question_since, s->question_mm,
            s->runout_ms ? "true" : "false");
        if (i < 4 && canvas && trays[i].have) spools_tray_json(&b, &trays[i]);
        else json_builder_printf(&b, "null");
        json_builder_printf(&b, "}");
    }
    const spool_job *job = &spools.job;
    json_builder_printf(&b, "],\"job\":{\"active\":%s,\"file\":", job->active ? "true" : "false");
    json_builder_string(&b, job->file);
    json_builder_printf(&b, ",\"started\":%lld,\"slots\":[", job->started);
    for (int i = 0; i < SPOOL_SLOTS; ++i)
        json_builder_printf(&b, "%s{\"mm\":%.1f,\"grams\":%.2f,\"spool\":\"%s\"}", i ? "," : "", job->mm[i],
                            job->grams[i], job->spool[i]);
    json_builder_printf(&b, "]},\"spools\":[");
    for (int i = 0; i < spools.count; ++i) {
        if (i) json_builder_printf(&b, ",");
        spools_entry_json(&b, &spools.spools[i]);
    }
    json_builder_printf(&b, "],\"log\":[");
    for (int i = spools.log_count - 1; i >= 0; --i) {
        if (i != spools.log_count - 1) json_builder_printf(&b, ",");
        spools_event_json(&b, &spools.log[i]);
    }
    json_builder_printf(&b, "]}\n");
    if (b.failed) spools_fail(fd, 500, "Spool library response is too large");
    else respond(fd, 200, "OK", "application/json; charset=utf-8", b.data, b.length);
    free(b.data);
}

/* The few fields /api/printer carries, so every page notices an open question. */
static void spools_summary(char *out, size_t cap) {
    char questions[16] = "";
    size_t used = 0;
    if (!spools_available) { snprintf(out, cap, "null"); return; }
    for (int i = 0; i < 4; ++i)
        if (spools.enabled && spools.slots[i].question)
            used += (size_t)snprintf(questions + used, sizeof(questions) - used, "%s%d", used ? "," : "", i);
    snprintf(out, cap, "{\"enabled\":%s,\"revision\":%lu,\"questions\":[%s]}", spools.enabled ? "true" : "false",
             spools_revision, questions);
}

static void spools_enable_response(int fd, const mqtt_client *mqtt, const char *body, size_t length) {
    while (length && isspace((unsigned char)body[length - 1])) length--;
    int on = length == 2 && !memcmp(body, "on", 2) ? 1 : length == 3 && !memcmp(body, "off", 3) ? 0 : -1;
    if (on < 0) { spools_fail(fd, 400, "Use on or off"); return; }
    if (!spools_open(fd)) return;
    if (on != spools.enabled) {
        spool_store *before = spools_snapshot(fd);
        if (!before) return;
        if (on) spools_verify(mqtt);
        else if (spools.job.active) spools_job_finish("tracking off");
        spools.enabled = on;
        if (!spools_commit(fd, before)) return;
    }
    spools_reply(fd, 200, on ? "{\"ok\":true,\"enabled\":true}\n" : "{\"ok\":true,\"enabled\":false}\n");
}

/* Creates (no id) or edits a spool from key=value lines. Its remaining weight is set only
 * when it is created: then prints change it, and /api/spools/adjust corrects it. */
static void spools_save_response(int fd, const char *body, size_t length) {
    spool_form f;
    if (!spool_form_parse(body, length, &f)) { spools_fail(fd, 400, "Invalid spool"); return; }
    if (!spools_open(fd)) return;
    const char *id = spool_form_get(&f, "id"), *value;
    spool_entry *existing = id && id[0] ? spool_find(id) : NULL;
    if (id && id[0] && !existing) { spools_fail(fd, 404, "Unknown spool"); return; }
    if (!existing && spools.count >= SPOOLS_MAX) { spools_fail(fd, 409, "The spool library is full"); return; }
    spool_entry s;
    if (existing) s = *existing;
    else {
        memset(&s, 0, sizeof(s));
        s.diameter = 1.75; s.density = SPOOL_DENSITY; s.net = 1000;
        s.created = (long long)spools_wall(NULL);
    }
    static const char *texts[] = {"name", "brand", "note"};
    char *targets[] = {s.name, s.brand, s.note};
    for (int i = 0; i < 3; ++i) {
        if (!(value = spool_form_get(&f, texts[i]))) continue;
        if (!spool_text_ok(value, SPOOL_TEXT_MAX, 1)) { spools_fail(fd, 400, "Invalid spool text"); return; }
        snprintf(targets[i], SPOOL_TEXT_MAX + 1, "%s", value);
    }
    if ((value = spool_form_get(&f, "material"))) {
        if (!spool_text_ok(value, SPOOL_MATERIAL_MAX, 0)) { spools_fail(fd, 400, "Invalid material"); return; }
        snprintf(s.material, sizeof(s.material), "%s", value);
    }
    if ((value = spool_form_get(&f, "color")) && !spool_color(s.color, value, strlen(value))) {
        spools_fail(fd, 400, "Invalid colour"); return;
    }
    static const struct { const char *key; double min, max; size_t offset; } numbers[] = {
        {"diameter", 1, 3.5, offsetof(spool_entry, diameter)}, {"density", 0.5, 3, offsetof(spool_entry, density)},
        {"net", 1, 20000, offsetof(spool_entry, net)}, {"tare", 0, 5000, offsetof(spool_entry, tare)},
        {"low", 0, 20000, offsetof(spool_entry, low)}, {"price", 0, 1e7, offsetof(spool_entry, price)},
    };
    for (size_t i = 0; i < sizeof(numbers) / sizeof(numbers[0]); ++i) {
        value = spool_form_get(&f, numbers[i].key);
        if (value && !spool_number(value, numbers[i].min, numbers[i].max, (double *)((char *)&s + numbers[i].offset))) {
            spools_fail(fd, 400, "Invalid spool number"); return;
        }
    }
    if ((value = spool_form_get(&f, "remaining"))) {
        if (existing) { spools_fail(fd, 400, "Correct the remaining weight with a weigh-in"); return; }
        if (!spool_number(value, 0, 20000, &s.remaining)) { spools_fail(fd, 400, "Invalid spool number"); return; }
    } else if (!existing) s.remaining = s.net;
    if ((value = spool_form_get(&f, "archived"))) {
        if (strcmp(value, "0") && strcmp(value, "1")) { spools_fail(fd, 400, "Invalid spool"); return; }
        s.archived = value[0] == '1';
    }
    int slot = -1;
    if ((value = spool_form_get(&f, "slot")) && ((slot = spool_slot_value(value)) < 0 || existing || s.archived)) {
        spools_fail(fd, 400, "Invalid slot"); return;
    }
    if (!s.material[0] || !s.color[0]) { spools_fail(fd, 400, "A spool needs a material and a colour"); return; }
    spool_store *before = spools_snapshot(fd);
    if (!before) return;
    spool_entry *target = existing;
    if (!target) {
        spools_new_id(s.id);
        target = &spools.spools[spools.count++];
    }
    *target = s;
    if (target->archived)
        for (int i = 0; i < SPOOL_SLOTS; ++i)
            if (!strcmp(spools.slots[i].spool, target->id)) spools_release(i, 0, "archived");
    if (!existing) spools_log('n', target->id, slot, target->remaining, 0, "", "");
    if (slot >= 0) spools_bind(slot, target);
    char reply[96];
    snprintf(reply, sizeof(reply), "{\"ok\":true,\"id\":\"%s\"}\n", target->id);
    if (spools_commit(fd, before)) spools_reply(fd, existing ? 200 : 201, reply);
}

static void spools_delete_response(int fd, const char *body, size_t length) {
    spool_form f;
    const char *id;
    if (!spool_form_parse(body, length, &f) || !(id = spool_form_get(&f, "id"))) { spools_fail(fd, 400, "Invalid spool"); return; }
    if (!spools_open(fd)) return;
    spool_entry *sp = spool_find(id);
    if (!sp) { spools_fail(fd, 404, "Unknown spool"); return; }
    spool_store *before = spools_snapshot(fd);
    if (!before) return;
    for (int i = 0; i < SPOOL_SLOTS; ++i) {
        if (!strcmp(spools.slots[i].spool, sp->id)) spools_release(i, 0, "deleted");
        if (!strcmp(spools.slots[i].last, sp->id)) spools.slots[i].last[0] = 0;
        if (!strcmp(spools.job.spool[i], sp->id)) spools.job.spool[i][0] = 0;
    }
    int index = (int)(sp - spools.spools);
    memmove(sp, sp + 1, (size_t)(spools.count - index - 1) * sizeof(*sp));
    spools.count--;
    if (spools_commit(fd, before)) spools_reply(fd, 200, "{\"ok\":true}\n");
}

/* slot=N and spool=<id>: the spool now in that tray; an empty id empties it. */
static void spools_assign_response(int fd, const char *body, size_t length) {
    spool_form f;
    const char *id;
    int slot;
    if (!spool_form_parse(body, length, &f) || (slot = spool_slot_value(spool_form_get(&f, "slot"))) < 0 ||
        !(id = spool_form_get(&f, "spool"))) { spools_fail(fd, 400, "Invalid assignment"); return; }
    if (!spools_open(fd)) return;
    spool_entry *sp = NULL;
    if (id[0] && !(sp = spool_find(id))) { spools_fail(fd, 404, "Unknown spool"); return; }
    if (sp && sp->archived) { spools_fail(fd, 409, "An archived spool cannot be loaded"); return; }
    spool_store *before = spools_snapshot(fd);
    if (!before) return;
    if (sp) spools_bind(slot, sp);
    else spools_release(slot, 1, "changed");
    if (spools_commit(fd, before)) spools_reply(fd, 200, "{\"ok\":true}\n");
}

/* slot=N: leave the tray without a spool; what it extruded meanwhile stays uncharged. */
static void spools_dismiss_response(int fd, const char *body, size_t length) {
    spool_form f;
    int slot;
    if (!spool_form_parse(body, length, &f) || (slot = spool_slot_value(spool_form_get(&f, "slot"))) < 0 || slot > 3) {
        spools_fail(fd, 400, "Invalid slot"); return;
    }
    if (!spools_open(fd)) return;
    spool_store *before = spools_snapshot(fd);
    if (!before) return;
    spool_slot *s = &spools.slots[slot];
    s->question = 0; s->question_mm = 0; s->question_job[0] = 0; s->question_since = 0;
    if (spools_commit(fd, before)) spools_reply(fd, 200, "{\"ok\":true}\n");
}

/* id, and remaining=<g> or gross=<g> (the spool on a scale, with its tare). */
static void spools_adjust_response(int fd, const char *body, size_t length) {
    spool_form f;
    const char *id, *remaining, *gross;
    double value;
    if (!spool_form_parse(body, length, &f) || !(id = spool_form_get(&f, "id"))) { spools_fail(fd, 400, "Invalid weight"); return; }
    remaining = spool_form_get(&f, "remaining");
    gross = spool_form_get(&f, "gross");
    if (!remaining == !gross) { spools_fail(fd, 400, "Invalid weight"); return; }
    if (!spools_open(fd)) return;
    spool_entry *sp = spool_find(id);
    if (!sp) { spools_fail(fd, 404, "Unknown spool"); return; }
    if (remaining && !spool_number(remaining, 0, 20000, &value)) { spools_fail(fd, 400, "Invalid weight"); return; }
    if (gross) {
        if (sp->tare <= 0) { spools_fail(fd, 409, "Enter the empty spool weight first"); return; }
        if (!spool_number(gross, 0, 25000, &value) || value < sp->tare) { spools_fail(fd, 400, "Invalid weight"); return; }
        value -= sp->tare;
    }
    spool_store *before = spools_snapshot(fd);
    if (!before) return;
    spools_log('c', sp->id, -1, value - sp->remaining, 0, "", gross ? "weighed" : "set");
    sp->remaining = value;
    char reply[96];
    snprintf(reply, sizeof(reply), "{\"ok\":true,\"remaining\":%.1f}\n", value);
    if (spools_commit(fd, before)) spools_reply(fd, 200, reply);
}

/* Saves what is still unsaved when CC2 Control stops. */
static void spools_flush(void) {
    if (spools_available && spools_dirty) (void)spools_save();
}
