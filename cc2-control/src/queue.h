/* ---- Print queue --------------------------------------------------------------------
 * Jobs prepared ahead of time: a G-code file, the print dialog's choices (Canvas filaments,
 * build plate side, bed leveling mode, timelapse, plate measurement and nozzle) and a
 * number of copies. Each copy is one protected print start through gcode_start_response(),
 * with every guard that applies to a print started from the Files page. Nothing starts on
 * its own: the operator presses Start for every copy, unless the "auto" policy was chosen
 * for a printer that clears its own bed.
 *
 * A job that prints from the Canvas is bound to filaments, not to slots: for every tool of its
 * file it keeps a material and a colour, as a Canvas slot labels them. A copy starts from the
 * slots that hold those filaments now, whichever they are, and a job can be added for a
 * filament that is in no slot yet; a start is refused while one of its filaments is in none
 * (see "Canvas slots at a start"). The slot a job was added with is only where to look first.
 *
 * The queue is kept in queue_path (--queue FILE) as JSON and survives restarts. It is
 * included part-way through main.c: it uses the static state above it (the MQTT cache,
 * the UDS telemetry, the plate library's file helpers and the print start itself) and
 * printer_change_blocked(), defined after the sessions it asks about. */

#define QUEUE_MAX 32
#define QUEUE_FILE_MAX (128 * 1024)
#define QUEUE_MAPPING_MAX 96
#define QUEUE_COPIES_MAX 99
#define QUEUE_DELAY_MIN 10
#define QUEUE_DELAY_MAX 3600
#define QUEUE_START_GRACE_MS 180000   /* idle for this long after a start request: it never ran */
#define QUEUE_START_CAP_MS 1800000    /* a start request this old is given up whatever the printer does */
#define QUEUE_FOREIGN_MS 5000         /* MQTT must name another file for this long before the start counts as lost */
#define QUEUE_IDLE_GRACE_MS 60000     /* idle again without "complete": the copy ended otherwise */

/* A filament as a Canvas slot labels it: the material type ("PLA"), the product line ("PLA Matte") and the
 * colour as "#RRGGBB". A type or a name left empty is not stated. */
typedef struct {
    char type[SPOOL_MATERIAL_MAX + 1], name[65], color[8];
} queue_filament;

/* One tool of a job that prints from the Canvas. */
typedef struct {
    int tool;
    int slot;                /* the Canvas slot the job was added with (0-3), or -1: added for a filament alone */
    queue_filament filament; /* what the tool prints with; unknown until the Canvas has labelled `slot` (queue_note_filaments) */
} queue_tool;

typedef struct {
    int id;
    char storage[16];
    char path[PATH_MAX_LOCAL];
    queue_tool tool[GCODE_TOOLS_MAX]; /* ascending by tool; none: the external feed */
    int tools;
    char side;
    char leveling[16];
    int timelapse;
    char measure[24], nozzle[24]; /* plate library ids, or empty */
    int copies, done;
    long estimated_seconds;     /* -1 unknown; the slicer's estimate, per copy */
    double filament_grams;      /* <0 unknown; per copy */
    long long added;            /* wall-clock seconds */
    char state[12];             /* pending | starting | printing | done */
    char last_result[16];       /* "" | stopped | not_started | auto_failed */
    char print_name[PATH_MAX_LOCAL]; /* the file the printer was given for the running copy (USB imports are renamed) */
    int native_seen;            /* this copy was seen as its own job: a later "complete" is its own */
    char job_uuid[128];         /* the running job as MQTT reports it, once it differs from the one before */
    /* Runtime only. */
    long long started_ms;       /* monotonic; the last moment the printer was seen busy with the start */
    long long requested_ms;     /* monotonic; when the start was requested (restarts with the service) */
    long long mismatch_ms;      /* monotonic; since when MQTT has named another file during the start */
    double duration_baseline;   /* print_stats.total_duration when the start was requested, or -1 */
    double bed_baseline, nozzle_baseline; /* heater targets when the start was requested */
    char request_uuid[128];     /* MQTT's job uuid when the start was requested: the previous job's */
} queue_item;

typedef struct {
    int count, next_id;
    queue_item items[QUEUE_MAX];
    char after[8];              /* wait | auto */
    int delay;                  /* seconds before an automatic start */
    int remove_done;
    int active_id;              /* the item whose copy is starting or printing, 0 none */
    long long complete_ms;      /* when a queued copy was counted under the auto policy (0: none pending) */
    unsigned long complete_canvas; /* the Canvas reports received by then: the automatic start waits for a newer one */
    unsigned long learned_canvas;  /* the Canvas report the jobs last took their filaments from (queue_learn_filaments) */
    unsigned long revision;
} queue_state;

static queue_state queue;
static int queue_available;
static const char *queue_error = "";

static void queue_defaults(void) {
    memset(&queue, 0, sizeof(queue));
    queue.next_id = 1;
    snprintf(queue.after, sizeof(queue.after), "wait");
    queue.delay = 60;
    queue.remove_done = 1;
}

static queue_item *queue_find(int id) {
    if (id <= 0) return NULL;
    for (int i = 0; i < queue.count; ++i) if (queue.items[i].id == id) return &queue.items[i];
    return NULL;
}

static const char *queue_root(const queue_item *item) {
    return strcmp(item->storage, "usb") == 0 ? gcode_usb_root : gcode_internal_root;
}

static int queue_present(const queue_item *item) {
    return gcode_file_is_printable(queue_root(item), item->path);
}

/* The first job that can start: pending, with its file in place. */
static queue_item *queue_next(void) {
    for (int i = 0; i < queue.count; ++i) {
        queue_item *item = &queue.items[i];
        if (!strcmp(item->state, "pending") && queue_present(item)) return item;
    }
    return NULL;
}

static void queue_remove_at(int at) {
    memmove(&queue.items[at], &queue.items[at + 1], (size_t)(queue.count - at - 1) * sizeof(queue_item));
    queue.count--;
}

/* Every copy printed: the job is finished, and gone when the policy says so. `item` is invalid afterwards. */
static void queue_finish_item(queue_item *item) {
    snprintf(item->state, sizeof(item->state), "done");
    if (queue.remove_done) queue_remove_at((int)(item - queue.items));
}

/* ---- Canvas slots at a start ----------------------------------------------------------- */

static int queue_mqtt_fresh(const mqtt_client *mqtt) {
    time_t wall = time(NULL);
    return mqtt->connected && mqtt->have_machine_status && mqtt->last_message > 0 &&
           wall >= mqtt->last_message && wall - mqtt->last_message <= 15;
}

/* The Canvas trays a start is judged by: the printer's last report, while the printer is heard from. The
 * printer answers a Canvas request, and newer firmware may not report a change by itself (mqtt_tick). So the
 * queue asks while it decides, as a page that shows the Canvas does (canvas_watch_until): whenever a page
 * shows a start on offer (queue_get_response) and through the delay before an automatic start (queue_tick). */
static int queue_trays(const mqtt_client *mqtt, spool_tray trays[4]) {
    memset(trays, 0, sizeof(spool_tray) * 4);
    return queue_mqtt_fresh(mqtt) && spools_trays(mqtt, trays);
}

/* A filament is known by a material (type or name) and a colour; a label without either names none. */
static int queue_filament_known(const queue_filament *f) { return (f->type[0] || f->name[0]) && f->color[0]; }

/* Whether a tray's label names this filament: the same colour, and the type and the name the filament states.
 * A job's filament is as exact as the label or the request it came from: one that states no product line is
 * not held to the tray's. (The printer's own auto refill asks for the same type and colour.) */
static int queue_filament_in(const queue_filament *f, const spool_tray *tray) {
    return !strcmp(f->color, tray->color) && (!f->type[0] || !strcasecmp(f->type, tray->type)) &&
           (!f->name[0] || !strcasecmp(f->name, tray->name));
}

static void queue_filament_of(queue_filament *f, const spool_tray *tray) {
    memset(f, 0, sizeof(*f));
    snprintf(f->type, sizeof(f->type), "%s", tray->type);
    snprintf(f->name, sizeof(f->name), "%s", tray->name);
    snprintf(f->color, sizeof(f->color), "%s", tray->color);
}

/* The lowest tray that holds a filament: reported, not empty, labelled with it. -1 when none does. */
static int queue_filament_tray(const queue_filament *f, const spool_tray trays[4]) {
    for (int tray = 0; tray < 4; ++tray)
        if (trays[tray].have && trays[tray].status != 0 && queue_filament_in(f, &trays[tray])) return tray;
    return -1;
}

static queue_tool *queue_tool_of(queue_item *item, int tool) {
    for (int i = 0; i < item->tools; ++i) if (item->tool[i].tool == tool) return &item->tool[i];
    return NULL;
}

/* Adds a tool to a job, keeping the list in tool order. Returns 0 for a tool the job already has. */
static int queue_tool_add(queue_item *item, const queue_tool *fresh) {
    if (item->tools >= GCODE_TOOLS_MAX || queue_tool_of(item, fresh->tool)) return 0;
    int at = item->tools;
    while (at > 0 && item->tool[at - 1].tool > fresh->tool) { item->tool[at] = item->tool[at - 1]; at--; }
    item->tool[at] = *fresh;
    item->tools++;
    return 1;
}

/* A tool added with a Canvas slot takes the slot's filament for its own: what the label says when the job is
 * added, which is what the print dialog showed. An empty slot counts too: it keeps the label of the filament
 * it held. A tool added while the printer had reported no such label takes it from the first report that has
 * one (queue_learn_filaments), and so does a job stored before filaments were recorded; until then the tool
 * prints with whatever its slot holds. Returns whether anything was recorded. */
static int queue_note_filaments(queue_item *item, const spool_tray trays[4]) {
    int noted = 0;
    for (int i = 0; i < item->tools; ++i) {
        queue_tool *t = &item->tool[i];
        queue_filament seen;
        if (t->slot < 0 || queue_filament_known(&t->filament) || !trays[t->slot].have) continue;
        queue_filament_of(&seen, &trays[t->slot]);
        if (!queue_filament_known(&seen)) continue;
        t->filament = seen;
        noted = 1;
    }
    return noted;
}

#define QUEUE_FEED_OK 0      /* the tool has a feed and nothing to say about it */
#define QUEUE_FEED_EMPTY 1   /* the slot the job was added with is empty */
#define QUEUE_FEED_OTHER 2   /* the slot the job was added with holds another filament */
#define QUEUE_FEED_ABSENT 3  /* added for a filament alone, and no slot holds it */
#define QUEUE_FEED_UNKNOWN 4 /* added for a filament alone, and the Canvas has reported no slots to look in */

static const char *queue_feed_word(int why) {
    return why == QUEUE_FEED_EMPTY ? "empty" : why == QUEUE_FEED_OTHER ? "other" : why == QUEUE_FEED_ABSENT ? "absent" :
           why == QUEUE_FEED_UNKNOWN ? "unknown" : "";
}

/* Where each tool of a job feeds from when a copy starts now: the slot that holds the tool's filament. The
 * slot the job was added with is taken while it holds that filament; otherwise the lowest slot that does,
 * and tools that print with one filament may share a slot. Nothing about the job changes: a slot that holds
 * its filament again is used again.
 * A tool that knows no filament yet (queue_note_filaments) stays on its slot, with whatever it holds; when
 * that slot is empty, the filament its label names is looked for instead.
 * `trays`: the Canvas trays, or NULL when they are not known (no fresh Canvas report). Nothing is then known
 * against a slot the job names, and a tool added for a filament alone cannot be placed.
 * use[i] is the tray for item->tool[i], or -1 when no slot holds its filament; why[i] says what was found
 * (QUEUE_FEED_*): for a tool with a feed, why it is not the slot the job was added with. Returns the number
 * of tools without a feed. */
static int queue_resolve(const queue_item *item, const spool_tray *trays, int use[GCODE_TOOLS_MAX], int why[GCODE_TOOLS_MAX]) {
    int missing = 0;
    for (int i = 0; i < item->tools; ++i) {
        const queue_tool *t = &item->tool[i];
        const spool_tray *own = trays && t->slot >= 0 ? &trays[t->slot] : NULL;
        queue_filament want = t->filament;
        int known = queue_filament_known(&want);
        use[i] = -1; why[i] = QUEUE_FEED_OK;
        if (!trays) {
            if (t->slot >= 0) use[i] = t->slot;
            else why[i] = QUEUE_FEED_UNKNOWN;
        } else if (own && !own->have) use[i] = t->slot; /* not reported: nothing is known against it */
        else if (own && own->status != 0 && (!known || queue_filament_in(&want, own))) use[i] = t->slot;
        else {
            if (own && !known) queue_filament_of(&want, own); /* an empty slot is judged by its own label */
            if (queue_filament_known(&want)) use[i] = queue_filament_tray(&want, trays);
            if (own) why[i] = own->status == 0 ? QUEUE_FEED_EMPTY : QUEUE_FEED_OTHER;
            else if (use[i] < 0) why[i] = QUEUE_FEED_ABSENT;
        }
        if (use[i] < 0) missing++;
    }
    return missing;
}

/* The slots a job was added with, as the print path's mapping ("tool:slot,..."): the tools added for a
 * filament alone are not in it. */
static void queue_mapping_text(const queue_item *item, char *out, size_t cap) {
    size_t used = 0;
    out[0] = 0;
    for (int i = 0; i < item->tools; ++i) {
        if (item->tool[i].slot < 0) continue;
        int n = snprintf(out + used, cap - used, "%s%d:%d", used ? "," : "", item->tool[i].tool, item->tool[i].slot);
        if (n < 0 || (size_t)n >= cap - used) { out[used] = 0; return; }
        used += (size_t)n;
    }
}

/* ---- persistence --------------------------------------------------------------------- */

/* `trays`: the Canvas trays for the browser's view of the job's slots, or NULL when they are not known. */
static void queue_item_json(json_builder *b, const queue_item *item, int stored, const spool_tray *trays) {
    json_builder_printf(b, "{\"id\":%d,\"storage\":", item->id);
    json_builder_string(b, item->storage);
    json_builder_printf(b, ",\"path\":");
    json_builder_string(b, item->path);
    char mapping[QUEUE_MAPPING_MAX];
    queue_mapping_text(item, mapping, sizeof(mapping));
    json_builder_printf(b, ",\"mapping\":");
    json_builder_string(b, mapping);
    json_builder_printf(b, ",\"side\":\"%c\",\"leveling\":", item->side);
    json_builder_string(b, item->leveling);
    json_builder_printf(b, ",\"timelapse\":%s,\"measure\":", item->timelapse ? "true" : "false");
    json_builder_string(b, item->measure);
    json_builder_printf(b, ",\"nozzle\":");
    json_builder_string(b, item->nozzle);
    /* Unknown estimates: -1 in the stored file (the loader wants numbers), null for the browser. */
    json_builder_printf(b, ",\"copies\":%d,\"done\":%d,\"estimated_seconds\":", item->copies, item->done);
    if (item->estimated_seconds >= 0) json_builder_printf(b, "%ld", item->estimated_seconds);
    else json_builder_printf(b, stored ? "-1" : "null");
    json_builder_printf(b, ",\"filament_grams\":");
    if (item->filament_grams >= 0) json_builder_printf(b, stored ? "%.3f" : "%.1f", item->filament_grams);
    else json_builder_printf(b, stored ? "-1" : "null");
    json_builder_printf(b, ",\"added\":%lld,\"state\":", item->added);
    json_builder_string(b, item->state);
    json_builder_printf(b, ",\"last_result\":");
    json_builder_string(b, item->last_result);
    /* The job's filaments. Stored: the known ones by tool; "mapping" above holds the slots, and a tool that is
     * here and not there was added for a filament alone. For the browser: every tool, with the slot the job
     * was added with (null: none), the slot a start would take now (null: no slot holds the filament) and
     * what was found (queue_resolve); a filament not known yet has empty texts. */
    int use[GCODE_TOOLS_MAX] = {0}, why[GCODE_TOOLS_MAX] = {0};
    if (!stored) {
        queue_resolve(item, trays, use, why);
        json_builder_printf(b, ",\"present\":%s", queue_present(item) ? "true" : "false");
    }
    json_builder_printf(b, ",\"filaments\":[");
    for (int i = 0, listed = 0; i < item->tools; ++i) {
        const queue_tool *t = &item->tool[i];
        if (stored && !queue_filament_known(&t->filament)) continue;
        json_builder_printf(b, "%s{\"tool\":%d,\"type\":", listed++ ? "," : "", t->tool);
        json_builder_string(b, t->filament.type);
        json_builder_printf(b, ",\"name\":");
        json_builder_string(b, t->filament.name);
        json_builder_printf(b, ",\"color\":");
        json_builder_string(b, t->filament.color);
        if (!stored) {
            if (t->slot >= 0) json_builder_printf(b, ",\"slot\":%d,\"use\":", t->slot);
            else json_builder_printf(b, ",\"slot\":null,\"use\":");
            if (use[i] >= 0) json_builder_printf(b, "%d", use[i]);
            else json_builder_printf(b, "null");
            json_builder_printf(b, ",\"reason\":\"%s\"", queue_feed_word(why[i]));
        }
        json_builder_printf(b, "}");
    }
    json_builder_printf(b, "]");
    if (stored) {
        json_builder_printf(b, ",\"print_name\":");
        json_builder_string(b, item->print_name);
        json_builder_printf(b, ",\"native_seen\":%s,\"job_uuid\":", item->native_seen ? "true" : "false");
        json_builder_string(b, item->job_uuid);
        /* The uuid before the start travels with the record: after a restart the previous job's uuid, still
         * reported while the printer starts, must not pass for this copy's own. */
        json_builder_printf(b, ",\"request_uuid\":");
        json_builder_string(b, item->request_uuid);
    }
    json_builder_printf(b, "}");
}

/* A job with a tool added for a filament alone has no slot to name in "mapping". */
static int queue_has_free_tool(void) {
    for (int i = 0; i < queue.count; ++i)
        for (int k = 0; k < queue.items[i].tools; ++k) if (queue.items[i].tool[k].slot < 0) return 1;
    return 0;
}

static int queue_save(void) {
    char *data = malloc(QUEUE_FILE_MAX);
    if (!data) return -1;
    json_builder b = {data, 0, QUEUE_FILE_MAX, 0};
    /* Version 2 only when it is needed: a build that reads version 1 alone would take a job without a mapping
     * for one that prints from the external feed, so it must refuse the file instead. */
    json_builder_printf(&b, "{\"version\":%d,\"next_id\":%d,\"policy\":{\"after\":\"%s\",\"delay\":%d,\"remove_done\":%s},\"items\":[",
                        queue_has_free_tool() ? 2 : 1, queue.next_id, queue.after, queue.delay, queue.remove_done ? "true" : "false");
    for (int i = 0; i < queue.count; ++i) {
        if (i) json_builder_printf(&b, ",");
        queue_item_json(&b, &queue.items[i], 1, NULL);
    }
    json_builder_printf(&b, "]}\n");
    int result = b.failed ? -1 : plates_replace_file(queue_path, NULL, data, b.length, 0644);
    free(data);
    if (result == 0) queue.revision++;
    return result;
}

static int queue_valid_leveling(const char *leveling) {
    return !strcmp(leveling, "saved") || !strcmp(leveling, "calibrate") ||
           !strcmp(leveling, "adaptive") || !strcmp(leveling, "full");
}

static int queue_valid_mapping(const char *mapping) {
    int tools[GCODE_TOOLS_MAX], trays[GCODE_TOOLS_MAX]; size_t count = 0;
    size_t n = strlen(mapping);
    if (n >= QUEUE_MAPPING_MAX) return 0;
    for (size_t i = 0; i < n; ++i) if ((unsigned char)mapping[i] < 32 || mapping[i] == 127) return 0;
    return parse_slot_map(mapping, n, tools, trays, &count);
}

/* A plate measurement or nozzle reference: empty, or an id of the plate library. */
static int queue_valid_ref(const char *text) { return !*text || plate_id_valid(text); }

static int queue_valid_result(const char *text) {
    return !*text || !strcmp(text, "stopped") || !strcmp(text, "not_started") || !strcmp(text, "auto_failed") ||
           !strcmp(text, "auto_material");
}

static int queue_json_bool(const char *obj, const char *end, const char *key, int *out) {
    const char *value = json_member(obj, end, key);
    if (!value) return 0;
    if (end - value >= 4 && !strncmp(value, "true", 4)) { *out = 1; return 1; }
    if (end - value >= 5 && !strncmp(value, "false", 5)) { *out = 0; return 1; }
    return 0;
}

static int queue_plain_text(const char *text) {
    for (const char *p = text; *p; ++p) if ((unsigned char)*p < 32 || *p == 127) return 0;
    return 1;
}

/* The tools of a mapping ("tool:slot,..."), each on its slot and with no filament known yet. */
static int queue_mapping_tools(queue_item *item, const char *mapping) {
    int tools[GCODE_TOOLS_MAX], slots[GCODE_TOOLS_MAX];
    size_t count = 0;
    if (!queue_valid_mapping(mapping) || !parse_slot_map(mapping, strlen(mapping), tools, slots, &count)) return 0;
    for (size_t i = 0; i < count; ++i) {
        queue_tool fresh;
        memset(&fresh, 0, sizeof(fresh));
        fresh.tool = tools[i]; fresh.slot = slots[i];
        if (!queue_tool_add(item, &fresh)) return 0;
    }
    return 1;
}

/* A filament as a request or the stored file states it: a type and a name that need no escaping (either may
 * be empty, not both) and a colour as "#RRGGBB". */
static int queue_filament_read(queue_filament *f, const char *type, const char *name, const char *color) {
    memset(f, 0, sizeof(*f));
    if (!spool_text_ok(type, sizeof(f->type) - 1, 1) || !spool_text_ok(name, sizeof(f->name) - 1, 1) ||
        strlen(color) != 7 || !spool_color(f->color, color, 7)) return 0;
    snprintf(f->type, sizeof(f->type), "%s", type);
    snprintf(f->name, sizeof(f->name), "%s", name);
    return queue_filament_known(f);
}

/* One filament of a stored job, as queue_item_json wrote it: the filament of a tool of the mapping, or a tool
 * added for that filament alone. */
static int queue_load_filament(const char *obj, const char *end, void *context) {
    queue_item *item = context;
    queue_tool fresh, *known;
    char type[sizeof(fresh.filament.type)], name[sizeof(fresh.filament.name)], color[16];
    memset(&fresh, 0, sizeof(fresh));
    fresh.slot = -1;
    if (!json_member_int(obj, end, "tool", &fresh.tool) || fresh.tool < 0 || fresh.tool >= GCODE_TOOLS_MAX) return 0;
    if (!spools_raw_member(obj, end, "type", type, sizeof(type)) || !spools_raw_member(obj, end, "name", name, sizeof(name)) ||
        !spools_raw_member(obj, end, "color", color, sizeof(color)) || !queue_filament_read(&fresh.filament, type, name, color)) return 0;
    if (!(known = queue_tool_of(item, fresh.tool))) return queue_tool_add(item, &fresh);
    if (queue_filament_known(&known->filament)) return 0; /* listed twice */
    known->filament = fresh.filament;
    return 1;
}

static int queue_load_item(const char *obj, const char *end, void *context) {
    (void)context;
    if (queue.count >= QUEUE_MAX) return 0;
    queue_item *item = &queue.items[queue.count];
    memset(item, 0, sizeof(*item));
    const char *text; int length; double number;
    if (!json_member_int(obj, end, "id", &item->id) || item->id <= 0 || queue_find(item->id)) return 0;
    if (!spools_raw_member(obj, end, "storage", item->storage, sizeof(item->storage)) ||
        (strcmp(item->storage, "internal") && strcmp(item->storage, "usb"))) return 0;
    if (!spools_raw_member(obj, end, "path", item->path, sizeof(item->path)) || !safe_relative_path(item->path) ||
        !is_gcode_filename(item->path)) return 0;
    char mapping[QUEUE_MAPPING_MAX];
    if (!spools_raw_member(obj, end, "mapping", mapping, sizeof(mapping)) || !queue_mapping_tools(item, mapping)) return 0;
    if (!json_member_raw_string(obj, end, "side", &text, &length) || length != 1 || (text[0] != 'A' && text[0] != 'B')) return 0;
    item->side = text[0];
    if (!spools_raw_member(obj, end, "leveling", item->leveling, sizeof(item->leveling)) || !queue_valid_leveling(item->leveling)) return 0;
    if (!queue_json_bool(obj, end, "timelapse", &item->timelapse)) return 0;
    if (!spools_raw_member(obj, end, "measure", item->measure, sizeof(item->measure)) || !queue_valid_ref(item->measure) ||
        !spools_raw_member(obj, end, "nozzle", item->nozzle, sizeof(item->nozzle)) || !queue_valid_ref(item->nozzle)) return 0;
    if (!json_member_int(obj, end, "copies", &item->copies) || item->copies < 1 || item->copies > QUEUE_COPIES_MAX ||
        !json_member_int(obj, end, "done", &item->done) || item->done < 0 || item->done > item->copies) return 0;
    if (!plates_member_double(obj, end, "estimated_seconds", &number)) return 0;
    item->estimated_seconds = number < 0 || number > 1e9 ? -1 : (long)number;
    if (!plates_member_double(obj, end, "filament_grams", &number)) return 0;
    item->filament_grams = number < 0 || number > 1e7 ? -1 : number;
    if (!plates_member_double(obj, end, "added", &number) || number < 0) return 0;
    item->added = (long long)number;
    if (!spools_raw_member(obj, end, "state", item->state, sizeof(item->state)) ||
        (strcmp(item->state, "pending") && strcmp(item->state, "starting") && strcmp(item->state, "printing") &&
         strcmp(item->state, "done"))) return 0;
    if (!spools_raw_member(obj, end, "last_result", item->last_result, sizeof(item->last_result)) ||
        !queue_valid_result(item->last_result)) return 0;
    if (!spools_raw_member(obj, end, "print_name", item->print_name, sizeof(item->print_name)) ||
        (item->print_name[0] && (!safe_relative_path(item->print_name) || !is_gcode_filename(item->print_name)))) return 0;
    if (!queue_json_bool(obj, end, "native_seen", &item->native_seen)) return 0;
    if (!spools_raw_member(obj, end, "job_uuid", item->job_uuid, sizeof(item->job_uuid)) || !queue_plain_text(item->job_uuid)) return 0;
    if (json_member(obj, end, "request_uuid")) { /* absent in files written before it was stored */
        if (!spools_raw_member(obj, end, "request_uuid", item->request_uuid, sizeof(item->request_uuid)) ||
            !queue_plain_text(item->request_uuid)) return 0;
    }
    /* Absent in files written before filaments were recorded: the first Canvas report fills them in. */
    if (json_member(obj, end, "filaments") && !plates_parse_list(obj, end, "filaments", queue_load_filament, item)) return 0;
    if (!strcmp(item->state, "done") != (item->done >= item->copies)) {
        /* Keep the counts, let the state follow them. */
        snprintf(item->state, sizeof(item->state), "%s", item->done >= item->copies ? "done" : "pending");
    }
    item->duration_baseline = -1;
    if (!strcmp(item->state, "starting") || !strcmp(item->state, "printing")) {
        if (queue.active_id) return 0; /* one running copy at most */
        queue.active_id = item->id;
        item->started_ms = item->requested_ms = monotonic_ms(); /* the grace periods restart with the service */
    }
    queue.count++;
    return 1;
}

/* A missing file is an empty queue; anything unreadable keeps the queue closed. */
static void queue_load(void) {
    queue_defaults();
    queue_available = 0;
    size_t length = 0;
    char *text = plates_read_file(queue_path, QUEUE_FILE_MAX, &length);
    if (!text) {
        queue_available = errno == ENOENT;
        queue_error = queue_available ? "" : "The print queue file is unreadable";
        return;
    }
    queue_error = "The print queue file is unreadable";
    const char *end = text + length, *root = json_skip_space(text, end), *root_end, *policy_end;
    int version = 0, ok = 0, remove = 1;
    if (root < end && *root == '{' && (root_end = json_container_end(root, end)) &&
        json_member_int(root, root_end, "version", &version) && (version == 1 || version == 2) &&
        json_member_int(root, root_end, "next_id", &queue.next_id) && queue.next_id >= 1) {
        const char *policy = json_member_object(root, root_end, "policy", '{', &policy_end);
        ok = policy && spools_raw_member(policy, policy_end, "after", queue.after, sizeof(queue.after)) &&
             (!strcmp(queue.after, "wait") || !strcmp(queue.after, "auto")) &&
             json_member_int(policy, policy_end, "delay", &queue.delay) &&
             queue.delay >= QUEUE_DELAY_MIN && queue.delay <= QUEUE_DELAY_MAX &&
             queue_json_bool(policy, policy_end, "remove_done", &remove) &&
             plates_parse_list(root, root_end, "items", queue_load_item, NULL);
        if (ok) queue.remove_done = remove;
        for (int i = 0; ok && i < queue.count; ++i) if (queue.items[i].id >= queue.next_id) ok = 0;
    }
    free(text);
    if (!ok) { queue_defaults(); return; }
    queue_available = 1; queue_error = "";
}

/* ---- HTTP ------------------------------------------------------------------------------ */

static void queue_reply(int fd, int status, const char *body) {
    const char *text = status == 200 ? "OK" : status == 201 ? "Created" : status == 400 ? "Bad Request" :
                       status == 404 ? "Not Found" : status == 409 ? "Conflict" : status == 503 ? "Service Unavailable" :
                       "Internal Server Error";
    respond(fd, status, text, "application/json; charset=utf-8", body, strlen(body));
}

static void queue_fail(int fd, int status, const char *error) {
    char body[320], escaped[256];
    json_escape(escaped, sizeof(escaped), error);
    snprintf(body, sizeof(body), "{\"ok\":false,\"error\":\"%s\"}\n", escaped);
    queue_reply(fd, status, body);
}

static void queue_get_response(int fd, mqtt_client *mqtt) {
    char *data = malloc(QUEUE_FILE_MAX);
    if (!data) { queue_fail(fd, 500, "Out of memory"); return; }
    json_builder b = {data, 0, QUEUE_FILE_MAX, 0};
    const queue_item *next = queue_available ? queue_next() : NULL;
    spool_tray trays[4];
    int trays_known = queue_trays(mqtt, trays);
    /* A start is on offer and its slots come from the Canvas: whoever shows the queue is shown current trays. */
    if (next && next->tools && !queue.active_id && queue_mqtt_fresh(mqtt) && mqtt->machine_status == 1)
        mqtt->canvas_watch_until = time(NULL) + 15;
    json_builder_printf(&b, "{\"available\":%s,\"error\":", queue_available ? "true" : "false");
    json_builder_string(&b, queue_error);
    json_builder_printf(&b, ",\"revision\":%lu,\"printer_idle\":%s,\"policy\":{\"after\":\"%s\",\"delay\":%d,\"remove_done\":%s},\"active\":",
                        queue.revision, queue_mqtt_fresh(mqtt) && mqtt->machine_status == 1 ? "true" : "false",
                        queue.after, queue.delay, queue.remove_done ? "true" : "false");
    if (queue.active_id) json_builder_printf(&b, "%d", queue.active_id);
    else json_builder_printf(&b, "null");
    json_builder_printf(&b, ",\"next\":");
    if (next) json_builder_printf(&b, "%d", next->id);
    else json_builder_printf(&b, "null");
    /* Seconds until the automatic start, while one is pending: the operator can still hold it. */
    json_builder_printf(&b, ",\"auto_in\":");
    if (!strcmp(queue.after, "auto") && queue.complete_ms && !queue.active_id) {
        long long left = (long long)queue.delay * 1000 - (monotonic_ms() - queue.complete_ms);
        json_builder_printf(&b, "%lld", left > 0 ? (left + 999) / 1000 : 0);
    } else json_builder_printf(&b, "null");
    json_builder_printf(&b, ",\"items\":[");
    for (int i = 0; i < queue.count; ++i) {
        if (i) json_builder_printf(&b, ",");
        queue_item_json(&b, &queue.items[i], 0, trays_known ? trays : NULL);
    }
    json_builder_printf(&b, "]}\n");
    if (b.failed) queue_fail(fd, 500, "The print queue is too large to report");
    else respond(fd, 200, "OK", "application/json; charset=utf-8", data, b.length);
    free(data);
}

/* Splits a newline-separated body into at most `max` NUL-terminated lines (CR stripped). */
static int queue_lines(const char *body, size_t body_len, char *scratch, size_t scratch_cap, char **lines, int max) {
    if (body_len >= scratch_cap) return -1;
    memcpy(scratch, body, body_len); scratch[body_len] = 0;
    int n = 0; char *cursor = scratch;
    while (n < max) {
        lines[n++] = cursor;
        char *nl = strchr(cursor, '\n');
        if (!nl) break;
        *nl = 0; cursor = nl + 1;
    }
    for (int i = 0; i < n; ++i) { size_t l = strlen(lines[i]); while (l && lines[i][l - 1] == '\r') lines[i][--l] = 0; }
    for (int i = n; i < max; ++i) lines[i] = NULL;
    return n;
}

static int queue_parse_int(const char *text, int min, int max, int *out) {
    if (!text || !*text) return 0;
    char *end = NULL; long value = strtol(text, &end, 10);
    if (end == text || *end || value < min || value > max) return 0;
    *out = (int)value; return 1;
}

#define QUEUE_ADD_LINES 10 /* the lines of an add request before its filaments */

/* The Canvas part of a request, into item->tool[]: `mapping` ("tool:slot,...") for the tools that print from
 * a chosen slot, whose filament is the one the slot is labelled with, and one "tool<TAB>type<TAB>name<TAB>#RRGGBB"
 * line for each tool bound to a filament by itself, which need not be in the printer. Empty lines are skipped.
 * Returns NULL, or what is wrong with the request. */
static const char *queue_request_tools(queue_item *item, const char *mapping, char **lines, int count) {
    item->tools = 0;
    if (!queue_mapping_tools(item, mapping)) return "Invalid Canvas slot mapping";
    for (int i = 0; i < count; ++i) {
        char *field[4], *cursor = lines[i];
        int fields = 0;
        queue_tool fresh;
        if (!cursor || !*cursor) continue;
        while (fields < 4) {
            field[fields++] = cursor;
            if (!(cursor = strchr(cursor, '\t'))) break;
            *cursor++ = 0;
        }
        memset(&fresh, 0, sizeof(fresh));
        fresh.slot = -1;
        if (fields != 4 || cursor || !queue_parse_int(field[0], 0, GCODE_TOOLS_MAX - 1, &fresh.tool) ||
            !queue_filament_read(&fresh.filament, field[1], field[2], field[3])) return "Invalid filament";
        if (!queue_tool_add(item, &fresh)) return "A tool is named twice";
    }
    return NULL;
}

static void queue_add_response(int fd, const mqtt_client *mqtt, const char *body, size_t body_len) {
    if (!queue_available) { queue_fail(fd, 503, queue_error[0] ? queue_error : "The print queue is unavailable"); return; }
    if (queue.count >= QUEUE_MAX) { queue_fail(fd, 409, "The print queue is full"); return; }
    char storage[16], filename[PATH_MAX_LOCAL];
    const char *extra, *root, *media; size_t extra_len = 0;
    if (!gcode_request_file(body, body_len, storage, filename, &extra, &extra_len, &root, &media)) {
        queue_fail(fd, 400, "Invalid queue request"); return;
    }
    if (!gcode_file_is_printable(root, filename)) { queue_fail(fd, 404, "File is unavailable or unsafe"); return; }
    char scratch[4096]; char *line[QUEUE_ADD_LINES + GCODE_TOOLS_MAX];
    int n = queue_lines(extra, extra_len, scratch, sizeof(scratch), line, QUEUE_ADD_LINES + GCODE_TOOLS_MAX);
    if (n < 0) { queue_fail(fd, 400, "Invalid queue request"); return; }
    const char *mapping = line[0] ? line[0] : "", *side = line[1] ? line[1] : "A", *leveling = line[2] && *line[2] ? line[2] : "saved";
    const char *video = line[3] && *line[3] ? line[3] : "0", *measure = line[4] ? line[4] : "", *nozzle = line[5] ? line[5] : "";
    const char *copies_text = line[6], *position = line[7] && *line[7] ? line[7] : "end";
    const char *seconds_text = line[8] ? line[8] : "", *grams_text = line[9] ? line[9] : "";
    int copies = 0;
    queue_item fresh;
    memset(&fresh, 0, sizeof(fresh));
    const char *wrong = queue_request_tools(&fresh, mapping, line + QUEUE_ADD_LINES, GCODE_TOOLS_MAX);
    if (wrong) { queue_fail(fd, 400, wrong); return; }
    if (strlen(side) != 1 || (side[0] != 'A' && side[0] != 'B')) { queue_fail(fd, 400, "Invalid build plate side"); return; }
    if (!queue_valid_leveling(leveling)) { queue_fail(fd, 400, "Invalid bed leveling mode"); return; }
    if (strcmp(video, "0") && strcmp(video, "1")) { queue_fail(fd, 400, "Invalid timelapse option"); return; }
    if (!queue_valid_ref(measure) || !queue_valid_ref(nozzle)) { queue_fail(fd, 400, "Invalid build plate measurement"); return; }
    if (!queue_parse_int(copies_text, 1, QUEUE_COPIES_MAX, &copies)) { queue_fail(fd, 400, "Invalid number of copies"); return; }
    if (strcmp(position, "end") && strcmp(position, "next")) { queue_fail(fd, 400, "Invalid queue position"); return; }
    long estimated = -1; double grams = -1;
    if (*seconds_text) {
        char *end = NULL; long value = strtol(seconds_text, &end, 10);
        if (end == seconds_text || *end || value < 0 || value > 1000000000L) { queue_fail(fd, 400, "Invalid print time estimate"); return; }
        estimated = value;
    }
    if (*grams_text) {
        char *end = NULL; double value = strtod(grams_text, &end);
        if (end == grams_text || *end || !(value >= 0) || value > 1e7) { queue_fail(fd, 400, "Invalid filament estimate"); return; }
        grams = value;
    }
    fresh.id = queue.next_id;
    snprintf(fresh.storage, sizeof(fresh.storage), "%s", storage);
    snprintf(fresh.path, sizeof(fresh.path), "%s", filename);
    fresh.side = side[0];
    snprintf(fresh.leveling, sizeof(fresh.leveling), "%s", leveling);
    fresh.timelapse = video[0] == '1';
    snprintf(fresh.measure, sizeof(fresh.measure), "%s", measure);
    snprintf(fresh.nozzle, sizeof(fresh.nozzle), "%s", nozzle);
    fresh.copies = copies; fresh.done = 0;
    fresh.estimated_seconds = estimated; fresh.filament_grams = grams;
    fresh.added = (long long)time(NULL);
    fresh.duration_baseline = -1;
    snprintf(fresh.state, sizeof(fresh.state), "pending");
    /* A tool added with a slot is bound to the filament the Canvas labels that slot with now. */
    spool_tray trays[4];
    if (spools_trays(mqtt, trays)) (void)queue_note_filaments(&fresh, trays);
    /* "next" goes to the head, behind a copy that is already running. */
    int at = queue.count;
    if (!strcmp(position, "next")) {
        at = 0;
        while (at < queue.count && queue.items[at].id == queue.active_id) at++;
    }
    queue_state saved = queue;
    memmove(&queue.items[at + 1], &queue.items[at], (size_t)(queue.count - at) * sizeof(queue_item));
    queue.items[at] = fresh;
    queue.count++; queue.next_id++;
    if (queue_save() != 0) { queue = saved; queue_fail(fd, 500, "Cannot save the print queue"); return; }
    char reply[96];
    snprintf(reply, sizeof(reply), "{\"ok\":true,\"id\":%d,\"count\":%d}\n", fresh.id, queue.count);
    queue_reply(fd, 201, reply);
}

/* The copy is over, counted or not: the item forgets the running job. */
static void queue_clear_run(queue_item *item) {
    item->job_uuid[0] = 0; item->request_uuid[0] = 0; item->print_name[0] = 0; item->native_seen = 0;
    item->started_ms = item->requested_ms = item->mismatch_ms = 0;
    item->duration_baseline = -1; item->bed_baseline = item->nozzle_baseline = 0;
}

/* The mapping a copy starts with now: every tool on the slot that holds its filament (queue_resolve); empty
 * for the external feed. `trays`: the Canvas trays, or NULL when they are not known. Returns 0, or -1 with
 * `error` saying which filament is in no slot (or that the text does not fit). */
static int queue_start_mapping(const queue_item *item, const spool_tray *trays, char *out, size_t cap, char *error, size_t error_cap) {
    int use[GCODE_TOOLS_MAX], why[GCODE_TOOLS_MAX];
    size_t used = 0;
    out[0] = 0;
    queue_resolve(item, trays, use, why);
    for (int i = 0; i < item->tools; ++i) {
        const queue_tool *t = &item->tool[i];
        if (use[i] < 0) {
            if (why[i] == QUEUE_FEED_EMPTY)
                snprintf(error, error_cap, "Canvas slot %d is empty and no other slot holds the same filament", t->slot + 1);
            else if (why[i] == QUEUE_FEED_UNKNOWN)
                snprintf(error, error_cap, "The Canvas has not reported its slots, so the job's filament cannot be found");
            else
                snprintf(error, error_cap, "No Canvas slot holds the job's filament: %.64s %s",
                         t->filament.name[0] ? t->filament.name : t->filament.type, t->filament.color);
            return -1;
        }
        int n = snprintf(out + used, cap - used, "%s%d:%d", i ? "," : "", t->tool, use[i]);
        if (n < 0 || (size_t)n >= cap - used) { snprintf(error, error_cap, "Cannot build the print request"); return -1; }
        used += (size_t)n;
    }
    return 0;
}

/* Starts one copy of `item` through the protected print path. Returns the HTTP status sent. The
 * running state is saved before the request goes out, so a restart right after it still knows. */
static int queue_start_item(int fd, mqtt_client *mqtt, queue_item *item) {
    char body[PATH_MAX_LOCAL + 256], mapping[QUEUE_MAPPING_MAX], error[192];
    spool_tray trays[4];
    int trays_known = queue_trays(mqtt, trays);
    if (queue_start_mapping(item, trays_known ? trays : NULL, mapping, sizeof(mapping), error, sizeof(error)) != 0) {
        queue_fail(fd, 409, error);
        return 409;
    }
    int length = snprintf(body, sizeof(body), "%s\n%s\n%s\n%c\n%s\n%d\n%s\n%s", item->storage, item->path, mapping,
                          item->side, item->leveling, item->timelapse ? 1 : 0, item->measure, item->nozzle);
    if (length <= 0 || (size_t)length >= sizeof(body)) { queue_fail(fd, 500, "Cannot build the print request"); return 500; }
    queue_state saved = queue;
    double value;
    snprintf(item->state, sizeof(item->state), "starting");
    snprintf(item->print_name, sizeof(item->print_name), "%s", item->path);
    item->last_result[0] = 0; item->job_uuid[0] = 0; item->native_seen = 0; item->mismatch_ms = 0;
    snprintf(item->request_uuid, sizeof(item->request_uuid), "%s", mqtt->uuid);
    item->started_ms = item->requested_ms = monotonic_ms();
    item->duration_baseline = uds_value(&telemetry, U_TOTAL_DURATION, &value) ? value : -1;
    item->bed_baseline = uds_value(&telemetry, U_BG, &value) ? value : 0;
    item->nozzle_baseline = uds_value(&telemetry, U_EG, &value) ? value : 0;
    queue.active_id = item->id;
    queue.complete_ms = 0;
    if (queue_save() != 0) { queue = saved; queue_fail(fd, 500, "Cannot save the print queue"); return 500; }
    char started[PATH_MAX_LOCAL] = "";
    int status = gcode_start_response(fd, mqtt, body, (size_t)length, started);
    if (status != 202) {
        queue = saved;
        if (queue_save() != 0) fprintf(stderr, "Print queue: cannot save %s after a refused start\n", queue_path);
        return status;
    }
    if (started[0]) snprintf(item->print_name, sizeof(item->print_name), "%s", started);
    if (queue_save() != 0) fprintf(stderr, "Print queue: cannot save %s after a start\n", queue_path);
    return status;
}

static void queue_start_response(int fd, mqtt_client *mqtt, const char *body, size_t body_len) {
    if (!queue_available) { queue_fail(fd, 503, queue_error[0] ? queue_error : "The print queue is unavailable"); return; }
    char scratch[64]; char *line[2];
    if (queue_lines(body, body_len, scratch, sizeof(scratch), line, 2) < 0) { queue_fail(fd, 400, "Invalid queue request"); return; }
    queue_item *item = NULL;
    if (line[0] && *line[0]) {
        int id = 0;
        if (!queue_parse_int(line[0], 1, 1000000000, &id) || !(item = queue_find(id))) { queue_fail(fd, 404, "No such queued job"); return; }
    }
    if (queue.active_id) { queue_fail(fd, 409, "A queued copy is already running"); return; }
    if (!item && !(item = queue_next())) { queue_fail(fd, 409, "Nothing in the queue can start"); return; }
    if (strcmp(item->state, "pending")) { queue_fail(fd, 409, "This job has no copy left to print"); return; }
    const char *blocked = printer_change_blocked();
    if (blocked) { queue_fail(fd, 409, blocked); return; }
    if (plates_background_active()) { queue_fail(fd, 409, "Plate verification is still running"); return; }
    (void)queue_start_item(fd, mqtt, item);
}

static void queue_start_lost(queue_item *item);

/* The operator gives up a start the printer never ran: only while the printer is idle. */
static void queue_abandon_response(int fd, const mqtt_client *mqtt, const char *body, size_t body_len) {
    if (!queue_available) { queue_fail(fd, 503, "The print queue is unavailable"); return; }
    char scratch[64]; char *line[1];
    if (queue_lines(body, body_len, scratch, sizeof(scratch), line, 1) < 0) { queue_fail(fd, 400, "Invalid queue request"); return; }
    queue_item *item = queue_find(queue.active_id);
    if (!item || strcmp(item->state, "starting")) { queue_fail(fd, 409, "No start is waiting"); return; }
    if (line[0] && *line[0]) {
        int id = 0;
        if (!queue_parse_int(line[0], 1, 1000000000, &id) || id != item->id) { queue_fail(fd, 404, "No such queued job"); return; }
    }
    if (!queue_mqtt_fresh(mqtt) || mqtt->machine_status != 1) { queue_fail(fd, 409, "The printer is not idle"); return; }
    queue_start_lost(item);
    queue_reply(fd, 200, "{\"ok\":true}\n");
}

static void queue_remove_response(int fd, const char *body, size_t body_len) {
    if (!queue_available) { queue_fail(fd, 503, "The print queue is unavailable"); return; }
    char scratch[64]; char *line[1]; int id = 0;
    if (queue_lines(body, body_len, scratch, sizeof(scratch), line, 1) < 0 || !queue_parse_int(line[0], 1, 1000000000, &id)) {
        queue_fail(fd, 400, "Invalid queue request"); return;
    }
    queue_item *item = queue_find(id);
    if (!item) { queue_fail(fd, 404, "No such queued job"); return; }
    if (item->id == queue.active_id) { queue_fail(fd, 409, "A copy of this job is running"); return; }
    queue_state saved = queue;
    queue_remove_at((int)(item - queue.items));
    if (queue_save() != 0) { queue = saved; queue_fail(fd, 500, "Cannot save the print queue"); return; }
    queue_reply(fd, 200, "{\"ok\":true}\n");
}

static void queue_clear_done_response(int fd) {
    if (!queue_available) { queue_fail(fd, 503, "The print queue is unavailable"); return; }
    queue_state saved = queue;
    int kept = 0;
    for (int i = 0; i < queue.count; ++i) if (strcmp(queue.items[i].state, "done")) queue.items[kept++] = queue.items[i];
    queue.count = kept;
    if (queue_save() != 0) { queue = saved; queue_fail(fd, 500, "Cannot save the print queue"); return; }
    queue_reply(fd, 200, "{\"ok\":true}\n");
}

/* "id\nindex": moves the job to a position in the queue (0 is the head). */
static void queue_reorder_response(int fd, const char *body, size_t body_len) {
    if (!queue_available) { queue_fail(fd, 503, "The print queue is unavailable"); return; }
    char scratch[64]; char *line[2]; int id = 0, to = 0;
    if (queue_lines(body, body_len, scratch, sizeof(scratch), line, 2) < 2 || !queue_parse_int(line[0], 1, 1000000000, &id) ||
        !queue_parse_int(line[1], 0, QUEUE_MAX - 1, &to)) { queue_fail(fd, 400, "Invalid queue request"); return; }
    queue_item *item = queue_find(id);
    if (!item) { queue_fail(fd, 404, "No such queued job"); return; }
    if (to >= queue.count) to = queue.count - 1;
    int from = (int)(item - queue.items);
    if (from == to) { queue_reply(fd, 200, "{\"ok\":true}\n"); return; }
    queue_state saved = queue;
    queue_item moving = *item;
    if (from < to) memmove(&queue.items[from], &queue.items[from + 1], (size_t)(to - from) * sizeof(queue_item));
    else memmove(&queue.items[to + 1], &queue.items[to], (size_t)(from - to) * sizeof(queue_item));
    queue.items[to] = moving;
    if (queue_save() != 0) { queue = saved; queue_fail(fd, 500, "Cannot save the print queue"); return; }
    queue_reply(fd, 200, "{\"ok\":true}\n");
}

/* "id\ncopies": the total; never fewer than printed, and the running job keeps the copy it prints. */
static void queue_copies_response(int fd, const char *body, size_t body_len) {
    if (!queue_available) { queue_fail(fd, 503, "The print queue is unavailable"); return; }
    char scratch[64]; char *line[2]; int id = 0, copies = 0;
    if (queue_lines(body, body_len, scratch, sizeof(scratch), line, 2) < 2 || !queue_parse_int(line[0], 1, 1000000000, &id) ||
        !queue_parse_int(line[1], 1, QUEUE_COPIES_MAX, &copies)) { queue_fail(fd, 400, "Invalid number of copies"); return; }
    queue_item *item = queue_find(id);
    if (!item) { queue_fail(fd, 404, "No such queued job"); return; }
    if (copies < item->done) { queue_fail(fd, 409, "Fewer copies than already printed"); return; }
    if (copies == item->done && item->id == queue.active_id) {
        queue_fail(fd, 409, "A copy of this job is printing: keep one copy more than already printed"); return;
    }
    queue_state saved = queue;
    item->copies = copies;
    if (item->id != queue.active_id) {
        if (item->done >= copies) queue_finish_item(item);
        else snprintf(item->state, sizeof(item->state), "pending");
    }
    if (queue_save() != 0) { queue = saved; queue_fail(fd, 500, "Cannot save the print queue"); return; }
    queue_reply(fd, 200, "{\"ok\":true}\n");
}

/* "id\nmapping\nfilament lines": binds tools of a job to other filaments, in the words of an add request
 * (queue_request_tools): a tool named with a slot takes the filament that slot is labelled with now, a tool
 * named with a filament takes that one. Tools the request does not name keep theirs; which tools a job has
 * is the file's matter and does not change. A copy that is printing is not touched: it has its slots. */
static void queue_filament_response(int fd, const mqtt_client *mqtt, const char *body, size_t body_len) {
    if (!queue_available) { queue_fail(fd, 503, "The print queue is unavailable"); return; }
    char scratch[4096]; char *line[2 + GCODE_TOOLS_MAX]; int id = 0;
    if (queue_lines(body, body_len, scratch, sizeof(scratch), line, 2 + GCODE_TOOLS_MAX) < 2 ||
        !queue_parse_int(line[0], 1, 1000000000, &id)) { queue_fail(fd, 400, "Invalid queue request"); return; }
    queue_item *item = queue_find(id);
    if (!item) { queue_fail(fd, 404, "No such queued job"); return; }
    queue_item named;
    memset(&named, 0, sizeof(named));
    const char *wrong = queue_request_tools(&named, line[1], line + 2, GCODE_TOOLS_MAX);
    if (wrong) { queue_fail(fd, 400, wrong); return; }
    if (!named.tools) { queue_fail(fd, 400, "No filament to change"); return; }
    for (int i = 0; i < named.tools; ++i)
        if (!queue_tool_of(item, named.tool[i].tool)) { queue_fail(fd, 409, "This job has no such tool"); return; }
    queue_state saved = queue;
    for (int i = 0; i < named.tools; ++i) *queue_tool_of(item, named.tool[i].tool) = named.tool[i];
    spool_tray trays[4];
    if (spools_trays(mqtt, trays)) (void)queue_note_filaments(item, trays);
    if (queue_save() != 0) { queue = saved; queue_fail(fd, 500, "Cannot save the print queue"); return; }
    queue_reply(fd, 200, "{\"ok\":true}\n");
}

/* Cancels the pending automatic start; the policy itself stays. */
static void queue_hold_response(int fd) {
    if (!queue_available) { queue_fail(fd, 503, "The print queue is unavailable"); return; }
    queue.complete_ms = 0;
    queue_reply(fd, 200, "{\"ok\":true}\n");
}

/* "after\ndelay\nremove_done": what happens once a copy has finished. A change never starts anything
 * on its own: the automatic delay counts only from a completion seen while that policy was on. */
static void queue_policy_response(int fd, const char *body, size_t body_len) {
    if (!queue_available) { queue_fail(fd, 503, "The print queue is unavailable"); return; }
    char scratch[64]; char *line[3]; int delay = 0, remove = 0;
    if (queue_lines(body, body_len, scratch, sizeof(scratch), line, 3) < 3 || (strcmp(line[0], "wait") && strcmp(line[0], "auto")) ||
        !queue_parse_int(line[1], QUEUE_DELAY_MIN, QUEUE_DELAY_MAX, &delay) || !queue_parse_int(line[2], 0, 1, &remove)) {
        queue_fail(fd, 400, "Invalid queue policy"); return;
    }
    queue_state saved = queue;
    int was_auto = !strcmp(queue.after, "auto"), old_delay = queue.delay;
    snprintf(queue.after, sizeof(queue.after), "%s", line[0]);
    queue.delay = delay; queue.remove_done = remove;
    if (strcmp(queue.after, "auto") || !was_auto || delay != old_delay) queue.complete_ms = 0;
    if (queue_save() != 0) { queue = saved; queue_fail(fd, 500, "Cannot save the print queue"); return; }
    queue_reply(fd, 200, "{\"ok\":true}\n");
}

/* ---- following the printer ------------------------------------------------------------- */

static const char *queue_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* A reported file name (MQTT, or the printer service with its "local/" prefix) against the file the
 * printer was given for this copy; before a start, against the job's own file. */
static int queue_name_matches(const queue_item *item, const char *reported) {
    if (!strncmp(reported, "local/", 6)) reported += 6;
    return !strcmp(queue_basename(reported), queue_basename(item->print_name[0] ? item->print_name : item->path));
}

static int queue_native_running(void) {
    const char *native = uds_print_state(&telemetry);
    return native && (!strcmp(native, "printing") || !strcmp(native, "paused"));
}

/* Whether the reported file names name this copy's file. MQTT's name counts whenever it has one; the
 * printer service's name only while its print_stats run (it keeps the previous job's name after
 * "complete", and the two sockets do not deliver in step). An empty report is not evidence against it. */
static int queue_job_file_matches(const queue_item *item, const mqtt_client *mqtt) {
    if (mqtt->filename[0] && !queue_name_matches(item, mqtt->filename)) return 0;
    if (queue_native_running() && telemetry.have_filename && telemetry.filename[0] &&
        !queue_name_matches(item, telemetry.filename)) return 0;
    return 1;
}

/* The printer is doing something for the start: not idle, or heating beyond what it was heating when
 * the start was requested (a bed kept warm before the request is not the start script). */
static int queue_printer_active(const queue_item *item, const mqtt_client *mqtt) {
    double target;
    if (mqtt->machine_status != 1) return 1;
    if (uds_value(&telemetry, U_BG, &target) && target > item->bed_baseline + 0.5) return 1;
    if (uds_value(&telemetry, U_EG, &target) && target > item->nozzle_baseline + 0.5) return 1;
    return 0;
}

/* This copy is its own job: the printer service shows its file running, the printer's total print time
 * dropped below what it was when the start was requested (a new job resets it), or MQTT reports a job
 * uuid other than the one it had when the start was requested. */
static void queue_note_native(queue_item *item, const mqtt_client *mqtt) {
    int changed = 0;
    if (mqtt->uuid[0] && strcmp(mqtt->uuid, item->request_uuid) && !item->job_uuid[0]) {
        snprintf(item->job_uuid, sizeof(item->job_uuid), "%s", mqtt->uuid);
        changed = 1;
    }
    if (!item->native_seen) {
        double total;
        int running = queue_native_running() &&
                      (!telemetry.have_filename || !telemetry.filename[0] || queue_name_matches(item, telemetry.filename));
        int reset = item->duration_baseline >= 0 && uds_value(&telemetry, U_TOTAL_DURATION, &total) &&
                    total + 5 < item->duration_baseline;
        if (running || reset || item->job_uuid[0]) { item->native_seen = 1; changed = 1; }
    }
    if (changed && queue_save() != 0) fprintf(stderr, "Print queue: cannot save %s\n", queue_path);
}

static void queue_copy_over(queue_item *item, int counted, mqtt_client *mqtt) {
    if (counted) {
        if (item->done < item->copies) item->done++;
        item->last_result[0] = 0;
        queue.complete_ms = strcmp(queue.after, "auto") ? 0 : monotonic_ms();
        if (queue.complete_ms) {
            /* The automatic start will judge the Canvas trays: ask for them now, and take only a report that
             * arrives after this copy. */
            queue.complete_canvas = mqtt->canvas_revision;
            mqtt->canvas_refresh_due = 1;
        }
    } else {
        snprintf(item->last_result, sizeof(item->last_result), "stopped");
        queue.complete_ms = 0;
    }
    queue_clear_run(item);
    queue.active_id = 0;
    if (item->done >= item->copies) queue_finish_item(item);
    else snprintf(item->state, sizeof(item->state), "pending");
    if (queue_save() != 0) fprintf(stderr, "Print queue: cannot save %s after a copy ended\n", queue_path);
}

static void queue_start_lost(queue_item *item) {
    snprintf(item->state, sizeof(item->state), "pending");
    snprintf(item->last_result, sizeof(item->last_result), "not_started");
    queue_clear_run(item);
    queue.active_id = 0;
    if (queue_save() != 0) fprintf(stderr, "Print queue: cannot save %s\n", queue_path);
}

static void queue_lower(char *out, size_t cap, const char *text) {
    size_t n = 0;
    for (; text[n] && n + 1 < cap; ++n) out[n] = (char)tolower((unsigned char)text[n]);
    out[n] = 0;
}
/* The Queue page's rule for a slot: its type contains the file's material, or the file's material contains
 * the slot type's first word ("PLA Basic" for "PLA", "PLA" for "PLA Silk"). */
static int queue_material_matches(const char *slot_type, const char *wanted) {
    char slot[SPOOL_MATERIAL_MAX + 1], want[65];
    queue_lower(slot, sizeof(slot), slot_type);
    queue_lower(want, sizeof(want), wanted);
    if (strstr(slot, want)) return 1;
    char *space = strchr(slot, ' ');
    if (space) *space = 0;
    return *slot && strstr(want, slot) != NULL;
}
/* Before an automatic start, what the operator would check on the Queue page: every tool on a slot that
 * holds its filament (as at any start), that slot present in fresh Canvas telemetry and holding the file's
 * material. A filament that is in no slot, a slot that differs or is unknown, or a file that names no
 * material, holds the copy for the operator. The external feed (no tools) has nothing to verify. */
static int queue_material_verified(const queue_item *item, const mqtt_client *mqtt) {
    int use[GCODE_TOOLS_MAX], why[GCODE_TOOLS_MAX];
    if (!item->tools) return 1;
    spool_tray trays[4];
    if (!queue_trays(mqtt, trays)) return 0;
    if (queue_resolve(item, trays, use, why)) return 0; /* a filament of the job is in no slot */
    gcode_filament_info info[GCODE_TOOLS_MAX];
    if (gcode_read_filaments(queue_root(item), item->path, info) != 0) return 0;
    for (int i = 0; i < item->tools; ++i) {
        const spool_tray *tray = &trays[use[i]];
        const char *type = tray->type[0] ? tray->type : tray->name, *wanted = info[item->tool[i].tool].material;
        if (!tray->have || tray->status == 0 || !*type || !*wanted) return 0;
        if (!queue_material_matches(type, wanted)) return 0;
    }
    return 1;
}
/* Tools that know no filament for the slot they were added with take it from a new Canvas report
 * (queue_note_filaments). */
static void queue_learn_filaments(const mqtt_client *mqtt) {
    if (mqtt->canvas_revision == queue.learned_canvas) return;
    queue.learned_canvas = mqtt->canvas_revision;
    spool_tray trays[4];
    int noted = 0;
    if (!spools_trays(mqtt, trays)) return;
    for (int i = 0; i < queue.count; ++i) noted |= queue_note_filaments(&queue.items[i], trays);
    if (noted && queue_save() != 0) fprintf(stderr, "Print queue: cannot save %s\n", queue_path);
}

static void queue_tick(mqtt_client *mqtt) {
    if (!queue_available) return;
    if (!queue_mqtt_fresh(mqtt)) return; /* stale evidence decides nothing */
    queue_learn_filaments(mqtt);
    long long now = monotonic_ms();
    const char *native = uds_print_state(&telemetry);
    const char *word = native && *native ? native : mqtt->print_state;
    int running_word = !strcmp(word, "printing") || !strcmp(word, "paused") ||
                       !strcmp(mqtt->print_state, "printing") || !strcmp(mqtt->print_state, "paused");
    int busy = mqtt->machine_status == 2 && running_word;
    /* Loading, pausing, a power-loss recovery or a foreign-object check interrupt a print without ending it. */
    int held = mqtt->machine_status != 1 || running_word;
    queue_item *item = queue_find(queue.active_id);
    if (queue.active_id && !item) queue.active_id = 0;
    if (busy && !queue.active_id) queue.complete_ms = 0; /* another print ran: no automatic start after it */
    if (item && !strcmp(item->state, "starting")) {
        queue_note_native(item, mqtt);
        int foreign = busy && mqtt->filename[0] && !queue_name_matches(item, mqtt->filename);
        if (!foreign) item->mismatch_ms = 0;
        else if (!item->mismatch_ms) item->mismatch_ms = now;
        if (busy && queue_job_file_matches(item, mqtt)) {
            snprintf(item->state, sizeof(item->state), "printing");
            item->started_ms = now;
            if (queue_save() != 0) fprintf(stderr, "Print queue: cannot save %s\n", queue_path);
        } else if (foreign && now - item->mismatch_ms >= QUEUE_FOREIGN_MS) queue_start_lost(item); /* another file runs */
        else if (now - item->requested_ms >= QUEUE_START_CAP_MS) queue_start_lost(item);
        else if (queue_printer_active(item, mqtt)) item->started_ms = now; /* homing, bed mesh, heating: keep waiting */
        else if (now - item->started_ms >= QUEUE_START_GRACE_MS) queue_start_lost(item);
    } else if (item && !strcmp(item->state, "printing")) {
        queue_note_native(item, mqtt);
        if (busy && item->job_uuid[0] && mqtt->uuid[0] && strcmp(item->job_uuid, mqtt->uuid) &&
            strcmp(mqtt->uuid, item->request_uuid)) {
            queue_copy_over(item, 0, mqtt); /* another job took over without ours completing: not counted */
        } else if (held) item->started_ms = now;
        else if (!strcmp(word, "cancelled") || !strcmp(word, "error")) queue_copy_over(item, 0, mqtt);
        else if (!strcmp(word, "complete") && item->native_seen && queue_job_file_matches(item, mqtt)) queue_copy_over(item, 1, mqtt);
        else if (now - item->started_ms >= QUEUE_IDLE_GRACE_MS) queue_copy_over(item, 0, mqtt);
    }
    /* Automatic policy: only for a printer that clears its own bed; the delay lets the operator cancel. */
    int waiting = !strcmp(queue.after, "auto") && queue.complete_ms && !queue.active_id;
    /* Nobody may be showing the Canvas while the delay runs: keep its trays current for the check below. */
    if (waiting) mqtt->canvas_watch_until = time(NULL) + 15;
    if (waiting && now - queue.complete_ms >= (long long)queue.delay * 1000) {
        queue.complete_ms = 0;
        queue_item *next = queue_next();
        if (!next) return;
        /* A job fed from the Canvas needs a report received after the copy before it: an older one checks nothing. */
        int reported = !next->tools || mqtt->canvas_revision != queue.complete_canvas;
        if (!reported || !queue_material_verified(next, mqtt)) {
            /* The Queue page would show the warning; nobody is there to read it, so the copy waits for the button. */
            snprintf(next->last_result, sizeof(next->last_result), "auto_material");
            if (queue_save() != 0) fprintf(stderr, "Print queue: cannot save %s\n", queue_path);
            return;
        }
        int status = 503;
        if (mqtt->machine_status == 1 && !printer_change_blocked() && !plates_background_active()) {
            int sink = open("/dev/null", O_WRONLY);
            status = sink >= 0 ? queue_start_item(sink, mqtt, next) : 500;
            if (sink >= 0) close(sink);
        }
        if (status != 202) {
            snprintf(next->last_result, sizeof(next->last_result), "auto_failed");
            if (queue_save() != 0) fprintf(stderr, "Print queue: cannot save %s\n", queue_path);
        }
    }
}
