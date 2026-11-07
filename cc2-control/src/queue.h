/* ---- Print queue --------------------------------------------------------------------
 * Jobs prepared ahead of time: a G-code file, the print dialog's choices (Canvas mapping,
 * build plate side, bed leveling mode, timelapse, plate measurement and nozzle) and a
 * number of copies. Each copy is one protected print start through gcode_start_response(),
 * with every guard that applies to a print started from the Files page. Nothing starts on
 * its own: the operator presses Start for every copy, unless the "auto" policy was chosen
 * for a printer that clears its own bed.
 *
 * The queue is kept in queue_path (--queue FILE) as JSON and survives restarts. It is
 * included part-way through main.c: it uses the static state above it (the MQTT cache,
 * the UDS telemetry, the plate library's file helpers and the print start itself). */

#define QUEUE_MAX 32
#define QUEUE_FILE_MAX (96 * 1024)
#define QUEUE_MAPPING_MAX 96
#define QUEUE_COPIES_MAX 99
#define QUEUE_DELAY_MIN 10
#define QUEUE_DELAY_MAX 3600
#define QUEUE_START_GRACE_MS 180000   /* a start the printer never reported running */
#define QUEUE_IDLE_GRACE_MS 60000     /* idle again without "complete": the copy ended otherwise */

typedef struct {
    int id;
    char storage[16];
    char path[PATH_MAX_LOCAL];
    char mapping[QUEUE_MAPPING_MAX];
    char side;
    char leveling[16];
    int timelapse;
    char measure[24], nozzle[24];
    int copies, done;
    long estimated_seconds;     /* -1 unknown; the slicer's estimate, per copy */
    double filament_grams;      /* <0 unknown; per copy */
    long long added;            /* wall-clock seconds */
    char state[12];             /* pending | starting | printing | done */
    char last_result[16];       /* "" | stopped | not_started | auto_failed */
    long long started_ms;       /* monotonic, while starting or printing */
    char job_uuid[128];         /* the running job as MQTT reports it */
} queue_item;

typedef struct {
    int count, next_id;
    queue_item items[QUEUE_MAX];
    char after[8];              /* wait | auto */
    int delay;                  /* seconds before an automatic start */
    int remove_done;
    int active_id;              /* the item whose copy is starting or printing, 0 none */
    long long complete_ms;      /* when the last queued copy was seen complete (auto policy) */
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

/* ---- persistence --------------------------------------------------------------------- */

static int queue_save(void) {
    char *data = malloc(QUEUE_FILE_MAX);
    if (!data) return -1;
    json_builder b = {data, 0, QUEUE_FILE_MAX, 0};
    json_builder_printf(&b, "{\"version\":1,\"next_id\":%d,\"policy\":{\"after\":\"%s\",\"delay\":%d,\"remove_done\":%s},\"items\":[",
                        queue.next_id, queue.after, queue.delay, queue.remove_done ? "true" : "false");
    for (int i = 0; i < queue.count; ++i) {
        const queue_item *item = &queue.items[i];
        json_builder_printf(&b, "%s{\"id\":%d,\"storage\":\"%s\",\"path\":", i ? "," : "", item->id, item->storage);
        json_builder_string(&b, item->path);
        json_builder_printf(&b, ",\"mapping\":");
        json_builder_string(&b, item->mapping);
        json_builder_printf(&b, ",\"side\":\"%c\",\"leveling\":\"%s\",\"timelapse\":%s,\"measure\":\"%s\",\"nozzle\":\"%s\","
                            "\"copies\":%d,\"done\":%d,\"estimated_seconds\":%ld,\"filament_grams\":%.3f,\"added\":%lld,"
                            "\"state\":\"%s\",\"last_result\":\"%s\",\"job_uuid\":",
                            item->side, item->leveling, item->timelapse ? "true" : "false", item->measure, item->nozzle,
                            item->copies, item->done, item->estimated_seconds, item->filament_grams, item->added,
                            item->state, item->last_result);
        json_builder_string(&b, item->job_uuid);
        json_builder_printf(&b, "}");
    }
    json_builder_printf(&b, "]}\n");
    int result = b.failed ? -1 : plates_replace_file(queue_path, NULL, data, b.length, 0644);
    free(data);
    if (result == 0) queue.revision++;
    return result;
}

static int queue_plain_text(const char *text, size_t max) {
    size_t n = strlen(text);
    if (n > max) return 0;
    for (size_t i = 0; i < n; ++i) if ((unsigned char)text[i] < 32 || text[i] == 127) return 0;
    return 1;
}

static int queue_valid_leveling(const char *leveling) {
    return !strcmp(leveling, "saved") || !strcmp(leveling, "calibrate") ||
           !strcmp(leveling, "adaptive") || !strcmp(leveling, "full");
}

static int queue_valid_mapping(const char *mapping) {
    int tools[GCODE_TOOLS_MAX], trays[GCODE_TOOLS_MAX]; size_t count = 0;
    return queue_plain_text(mapping, QUEUE_MAPPING_MAX - 1) && parse_slot_map(mapping, strlen(mapping), tools, trays, &count);
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
    if (!spools_raw_member(obj, end, "mapping", item->mapping, sizeof(item->mapping)) || !queue_valid_mapping(item->mapping)) return 0;
    if (!json_member_raw_string(obj, end, "side", &text, &length) || length != 1 || (text[0] != 'A' && text[0] != 'B')) return 0;
    item->side = text[0];
    if (!spools_raw_member(obj, end, "leveling", item->leveling, sizeof(item->leveling)) || !queue_valid_leveling(item->leveling)) return 0;
    const char *flag = json_member(obj, end, "timelapse");
    if (!flag || (strncmp(flag, "true", 4) && strncmp(flag, "false", 5))) return 0;
    item->timelapse = !strncmp(flag, "true", 4);
    if (!spools_raw_member(obj, end, "measure", item->measure, sizeof(item->measure)) || !queue_plain_text(item->measure, 23) ||
        !spools_raw_member(obj, end, "nozzle", item->nozzle, sizeof(item->nozzle)) || !queue_plain_text(item->nozzle, 23)) return 0;
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
        !queue_plain_text(item->last_result, 15)) return 0;
    if (!spools_raw_member(obj, end, "job_uuid", item->job_uuid, sizeof(item->job_uuid)) || !queue_plain_text(item->job_uuid, 127)) return 0;
    if (!strcmp(item->state, "done") != (item->done >= item->copies)) {
        /* Keep the counts, let the state follow them. */
        snprintf(item->state, sizeof(item->state), "%s", item->done >= item->copies ? "done" : "pending");
    }
    if (!strcmp(item->state, "starting") || !strcmp(item->state, "printing")) {
        if (queue.active_id) return 0; /* one running copy at most */
        queue.active_id = item->id;
        item->started_ms = monotonic_ms(); /* the grace periods restart with the service */
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
    int version = 0, ok = 0;
    if (root < end && *root == '{' && (root_end = json_container_end(root, end)) &&
        json_member_int(root, root_end, "version", &version) && version == 1 &&
        json_member_int(root, root_end, "next_id", &queue.next_id) && queue.next_id >= 1) {
        const char *policy = json_member_object(root, root_end, "policy", '{', &policy_end);
        const char *remove = policy ? json_member(policy, policy_end, "remove_done") : NULL;
        ok = policy && spools_raw_member(policy, policy_end, "after", queue.after, sizeof(queue.after)) &&
             (!strcmp(queue.after, "wait") || !strcmp(queue.after, "auto")) &&
             json_member_int(policy, policy_end, "delay", &queue.delay) &&
             queue.delay >= QUEUE_DELAY_MIN && queue.delay <= QUEUE_DELAY_MAX &&
             remove && (!strncmp(remove, "true", 4) || !strncmp(remove, "false", 5)) &&
             plates_parse_list(root, root_end, "items", queue_load_item, NULL);
        if (ok) queue.remove_done = !strncmp(remove, "true", 4);
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

static void queue_get_response(int fd, const mqtt_client *mqtt) {
    char *data = malloc(QUEUE_FILE_MAX);
    if (!data) { queue_fail(fd, 500, "Out of memory"); return; }
    json_builder b = {data, 0, QUEUE_FILE_MAX, 0};
    const queue_item *next = queue_available ? queue_next() : NULL;
    time_t wall = time(NULL);
    int fresh = mqtt->connected && mqtt->have_machine_status && mqtt->last_message > 0 &&
                wall >= mqtt->last_message && wall - mqtt->last_message <= 15;
    json_builder_printf(&b, "{\"available\":%s,\"error\":", queue_available ? "true" : "false");
    json_builder_string(&b, queue_error);
    json_builder_printf(&b, ",\"revision\":%lu,\"printer_idle\":%s,\"policy\":{\"after\":\"%s\",\"delay\":%d,\"remove_done\":%s},\"active\":",
                        queue.revision, fresh && mqtt->machine_status == 1 ? "true" : "false",
                        queue.after, queue.delay, queue.remove_done ? "true" : "false");
    if (queue.active_id) json_builder_printf(&b, "%d", queue.active_id);
    else json_builder_printf(&b, "null");
    json_builder_printf(&b, ",\"next\":");
    if (next) json_builder_printf(&b, "%d", next->id);
    else json_builder_printf(&b, "null");
    json_builder_printf(&b, ",\"items\":[");
    for (int i = 0; i < queue.count; ++i) {
        const queue_item *item = &queue.items[i];
        json_builder_printf(&b, "%s{\"id\":%d,\"storage\":\"%s\",\"path\":", i ? "," : "", item->id, item->storage);
        json_builder_string(&b, item->path);
        json_builder_printf(&b, ",\"mapping\":");
        json_builder_string(&b, item->mapping);
        json_builder_printf(&b, ",\"side\":\"%c\",\"leveling\":\"%s\",\"timelapse\":%s,\"measure\":\"%s\",\"nozzle\":\"%s\","
                            "\"copies\":%d,\"done\":%d,\"estimated_seconds\":",
                            item->side, item->leveling, item->timelapse ? "true" : "false", item->measure, item->nozzle,
                            item->copies, item->done);
        if (item->estimated_seconds >= 0) json_builder_printf(&b, "%ld", item->estimated_seconds);
        else json_builder_printf(&b, "null");
        json_builder_printf(&b, ",\"filament_grams\":");
        if (item->filament_grams >= 0) json_builder_printf(&b, "%.1f", item->filament_grams);
        else json_builder_printf(&b, "null");
        json_builder_printf(&b, ",\"added\":%lld,\"state\":\"%s\",\"last_result\":\"%s\",\"present\":%s}",
                            item->added, item->state, item->last_result, queue_present(item) ? "true" : "false");
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

static void queue_add_response(int fd, const char *body, size_t body_len) {
    if (!queue_available) { queue_fail(fd, 503, queue_error[0] ? queue_error : "The print queue is unavailable"); return; }
    if (queue.count >= QUEUE_MAX) { queue_fail(fd, 409, "The print queue is full"); return; }
    char storage[16], filename[PATH_MAX_LOCAL];
    const char *extra, *root, *media; size_t extra_len = 0;
    if (!gcode_request_file(body, body_len, storage, filename, &extra, &extra_len, &root, &media)) {
        queue_fail(fd, 400, "Invalid queue request"); return;
    }
    if (!gcode_file_is_printable(root, filename)) { queue_fail(fd, 404, "File is unavailable or unsafe"); return; }
    char scratch[1024]; char *line[10];
    int n = queue_lines(extra, extra_len, scratch, sizeof(scratch), line, 10);
    if (n < 0) { queue_fail(fd, 400, "Invalid queue request"); return; }
    const char *mapping = line[0] ? line[0] : "", *side = line[1] ? line[1] : "A", *leveling = line[2] && *line[2] ? line[2] : "saved";
    const char *video = line[3] && *line[3] ? line[3] : "0", *measure = line[4] ? line[4] : "", *nozzle = line[5] ? line[5] : "";
    const char *copies_text = line[6], *position = line[7] && *line[7] ? line[7] : "end";
    const char *seconds_text = line[8] ? line[8] : "", *grams_text = line[9] ? line[9] : "";
    int copies = 0;
    if (!queue_valid_mapping(mapping)) { queue_fail(fd, 400, "Invalid Canvas slot mapping"); return; }
    if (strlen(side) != 1 || (side[0] != 'A' && side[0] != 'B')) { queue_fail(fd, 400, "Invalid build plate side"); return; }
    if (!queue_valid_leveling(leveling)) { queue_fail(fd, 400, "Invalid bed leveling mode"); return; }
    if (strcmp(video, "0") && strcmp(video, "1")) { queue_fail(fd, 400, "Invalid timelapse option"); return; }
    if (!queue_plain_text(measure, 23) || !queue_plain_text(nozzle, 23)) { queue_fail(fd, 400, "Invalid build plate measurement"); return; }
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
    queue_item fresh;
    memset(&fresh, 0, sizeof(fresh));
    fresh.id = queue.next_id;
    snprintf(fresh.storage, sizeof(fresh.storage), "%s", storage);
    snprintf(fresh.path, sizeof(fresh.path), "%s", filename);
    snprintf(fresh.mapping, sizeof(fresh.mapping), "%s", mapping);
    fresh.side = side[0];
    snprintf(fresh.leveling, sizeof(fresh.leveling), "%s", leveling);
    fresh.timelapse = video[0] == '1';
    snprintf(fresh.measure, sizeof(fresh.measure), "%s", measure);
    snprintf(fresh.nozzle, sizeof(fresh.nozzle), "%s", nozzle);
    fresh.copies = copies; fresh.done = 0;
    fresh.estimated_seconds = estimated; fresh.filament_grams = grams;
    fresh.added = (long long)time(NULL);
    snprintf(fresh.state, sizeof(fresh.state), "pending");
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

/* Starts one copy of `item` through the protected print path. Returns the HTTP status sent. */
static int queue_start_item(int fd, mqtt_client *mqtt, queue_item *item) {
    char body[PATH_MAX_LOCAL + 256];
    int length = snprintf(body, sizeof(body), "%s\n%s\n%s\n%c\n%s\n%d\n%s\n%s", item->storage, item->path, item->mapping,
                          item->side, item->leveling, item->timelapse ? 1 : 0, item->measure, item->nozzle);
    if (length <= 0 || (size_t)length >= sizeof(body)) { queue_fail(fd, 500, "Cannot build the print request"); return 500; }
    int status = gcode_start_response(fd, mqtt, body, (size_t)length);
    if (status == 202) {
        snprintf(item->state, sizeof(item->state), "starting");
        item->last_result[0] = 0; item->job_uuid[0] = 0;
        item->started_ms = monotonic_ms();
        queue.active_id = item->id;
        queue.complete_ms = 0;
        if (queue_save() != 0) fprintf(stderr, "Print queue: cannot save %s after a start\n", queue_path);
    }
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
    } else if (!(item = queue_next())) { queue_fail(fd, 409, "Nothing in the queue can start"); return; }
    if (queue.active_id) { queue_fail(fd, 409, "A queued copy is already running"); return; }
    if (strcmp(item->state, "pending")) { queue_fail(fd, 409, "This job has no copy left to print"); return; }
    if (plates_background_active()) { queue_fail(fd, 409, "Plate verification is still running"); return; }
    (void)queue_start_item(fd, mqtt, item);
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
    int at = (int)(item - queue.items);
    memmove(&queue.items[at], &queue.items[at + 1], (size_t)(queue.count - at - 1) * sizeof(queue_item));
    queue.count--;
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

/* "id\ncopies": the total; it cannot drop below the copies already printed. */
static void queue_copies_response(int fd, const char *body, size_t body_len) {
    if (!queue_available) { queue_fail(fd, 503, "The print queue is unavailable"); return; }
    char scratch[64]; char *line[2]; int id = 0, copies = 0;
    if (queue_lines(body, body_len, scratch, sizeof(scratch), line, 2) < 2 || !queue_parse_int(line[0], 1, 1000000000, &id) ||
        !queue_parse_int(line[1], 1, QUEUE_COPIES_MAX, &copies)) { queue_fail(fd, 400, "Invalid number of copies"); return; }
    queue_item *item = queue_find(id);
    if (!item) { queue_fail(fd, 404, "No such queued job"); return; }
    if (copies < item->done || (copies == item->done && item->id == queue.active_id)) {
        queue_fail(fd, 409, "Fewer copies than already printed"); return;
    }
    queue_state saved = queue;
    item->copies = copies;
    if (item->id != queue.active_id) {
        snprintf(item->state, sizeof(item->state), "%s", item->done >= copies ? "done" : "pending");
    }
    if (queue_save() != 0) { queue = saved; queue_fail(fd, 500, "Cannot save the print queue"); return; }
    queue_reply(fd, 200, "{\"ok\":true}\n");
}

/* "after\ndelay\nremove_done": what happens once a copy has finished. */
static void queue_policy_response(int fd, const char *body, size_t body_len) {
    if (!queue_available) { queue_fail(fd, 503, "The print queue is unavailable"); return; }
    char scratch[64]; char *line[3]; int delay = 0, remove = 0;
    if (queue_lines(body, body_len, scratch, sizeof(scratch), line, 3) < 3 || (strcmp(line[0], "wait") && strcmp(line[0], "auto")) ||
        !queue_parse_int(line[1], QUEUE_DELAY_MIN, QUEUE_DELAY_MAX, &delay) || !queue_parse_int(line[2], 0, 1, &remove)) {
        queue_fail(fd, 400, "Invalid queue policy"); return;
    }
    queue_state saved = queue;
    snprintf(queue.after, sizeof(queue.after), "%s", line[0]);
    queue.delay = delay; queue.remove_done = remove;
    if (strcmp(queue.after, "auto")) queue.complete_ms = 0;
    if (queue_save() != 0) { queue = saved; queue_fail(fd, 500, "Cannot save the print queue"); return; }
    queue_reply(fd, 200, "{\"ok\":true}\n");
}

/* ---- following the printer ------------------------------------------------------------- */

static const char *queue_basename(const char *path) {
    const char *slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

/* Whether a reported file name (MQTT or the printer service) names this job's file. An empty
 * report is not evidence against it: the firmware clears the name once a job is over. */
static int queue_job_file_matches(const queue_item *item, const mqtt_client *mqtt) {
    const char *want = queue_basename(item->path);
    if (mqtt->filename[0] && strcmp(queue_basename(mqtt->filename), want)) return 0;
    if (telemetry.have_filename && telemetry.filename[0]) {
        const char *file = telemetry.filename;
        if (!strncmp(file, "local/", 6)) file += 6;
        if (strcmp(queue_basename(file), want)) return 0;
    }
    return 1;
}

static void queue_copy_over(queue_item *item, int counted) {
    if (counted) {
        if (item->done < item->copies) item->done++;
        item->last_result[0] = 0;
        queue.complete_ms = monotonic_ms();
    } else {
        snprintf(item->last_result, sizeof(item->last_result), "stopped");
        queue.complete_ms = 0;
    }
    item->job_uuid[0] = 0; item->started_ms = 0;
    queue.active_id = 0;
    if (item->done >= item->copies) {
        snprintf(item->state, sizeof(item->state), "done");
        if (queue.remove_done) {
            int at = (int)(item - queue.items);
            memmove(&queue.items[at], &queue.items[at + 1], (size_t)(queue.count - at - 1) * sizeof(queue_item));
            queue.count--;
        }
    } else snprintf(item->state, sizeof(item->state), "pending");
    if (queue_save() != 0) fprintf(stderr, "Print queue: cannot save %s after a copy ended\n", queue_path);
}

static void queue_tick(mqtt_client *mqtt) {
    if (!queue_available) return;
    time_t wall = time(NULL);
    int fresh = mqtt->connected && mqtt->have_machine_status && mqtt->last_message > 0 &&
                wall >= mqtt->last_message && wall - mqtt->last_message <= 15;
    if (!fresh) return; /* stale evidence decides nothing */
    long long now = monotonic_ms();
    const char *native = uds_print_state(&telemetry);
    const char *word = native && *native ? native : mqtt->print_state;
    int busy = mqtt->machine_status == 2 && (!strcmp(word, "printing") || !strcmp(word, "paused") ||
                                             !strcmp(mqtt->print_state, "printing") || !strcmp(mqtt->print_state, "paused"));
    queue_item *item = queue_find(queue.active_id);
    if (queue.active_id && !item) queue.active_id = 0;
    if (item && !strcmp(item->state, "starting")) {
        if (busy && queue_job_file_matches(item, mqtt)) {
            snprintf(item->state, sizeof(item->state), "printing");
            snprintf(item->job_uuid, sizeof(item->job_uuid), "%s", mqtt->uuid);
            item->started_ms = now;
            if (queue_save() != 0) fprintf(stderr, "Print queue: cannot save %s\n", queue_path);
        } else if (now - item->started_ms >= QUEUE_START_GRACE_MS) {
            snprintf(item->state, sizeof(item->state), "pending");
            snprintf(item->last_result, sizeof(item->last_result), "not_started");
            item->job_uuid[0] = 0; item->started_ms = 0;
            queue.active_id = 0;
            if (queue_save() != 0) fprintf(stderr, "Print queue: cannot save %s\n", queue_path);
        }
    } else if (item && !strcmp(item->state, "printing")) {
        if (busy) {
            /* Another job took over without ours completing: not counted. */
            if (item->job_uuid[0] && mqtt->uuid[0] && strcmp(item->job_uuid, mqtt->uuid)) queue_copy_over(item, 0);
            else item->started_ms = now;
        } else if (!strcmp(word, "cancelled") || !strcmp(word, "error")) queue_copy_over(item, 0);
        else if (mqtt->machine_status == 1 && !strcmp(word, "complete")) queue_copy_over(item, queue_job_file_matches(item, mqtt));
        else if (mqtt->machine_status != 2 && now - item->started_ms >= QUEUE_IDLE_GRACE_MS) queue_copy_over(item, 0);
    }
    /* Automatic policy: only for a printer that clears its own bed; the delay lets the operator cancel. */
    if (!strcmp(queue.after, "auto") && queue.complete_ms && !queue.active_id &&
        now - queue.complete_ms >= (long long)queue.delay * 1000) {
        queue.complete_ms = 0;
        queue_item *next = queue_next();
        if (next && mqtt->machine_status == 1 && !plates_background_active()) {
            int sink = open("/dev/null", O_WRONLY);
            int status = sink >= 0 ? queue_start_item(sink, mqtt, next) : 500;
            if (sink >= 0) close(sink);
            if (status != 202) {
                snprintf(next->last_result, sizeof(next->last_result), "auto_failed");
                if (queue_save() != 0) fprintf(stderr, "Print queue: cannot save %s\n", queue_path);
            }
        }
    }
}
