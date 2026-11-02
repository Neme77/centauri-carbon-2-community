#ifndef CC2_RUNOUT_H
#define CC2_RUNOUT_H
/* Continue a print from another Canvas slot after a runout (issue #284). HTTP-loop owned.
 *
 * On a Canvas runout the firmware prints out what is left in the tube, pauses at the waste
 * box with the nozzle hot, and once the toolhead sensor clears it runs RESUME by itself,
 * which tries the same (now empty) slot again. Only that failed retry reports 1211 at level 1;
 * a load that succeeds clears it (level 3). So a paused print whose latest vendor event is an
 * active 1211 is a settled runout: nothing of the firmware's own routine is still queued.
 *
 * Continuing points every tool that used the empty slot at another one and resumes:
 * CANVAS_SET_COLOR_TABLE (it replaces the whole table, so the whole map is sent with only the
 * runout slot changed), M109 to the print temperature, RESUME. RESUME loads the newly mapped
 * slot, purges at the waste box and carries on, as the firmware's own refill does. This works
 * whether or not the firmware's auto refill is enabled. Verified on a CC2 on 2026-11-02.
 *
 * The firmware does not report its tool-to-slot table, and saves it to
 * /opt/usr/canvas_record_info.json only while idle. Its log is the authoritative trace: every
 * entry of every CANVAS_SET_COLOR_TABLE (the print start's, and later ones such as ours), each
 * auto refill that moved a tool to another slot, the runout's slot and the print starts. The
 * log is read incrementally, so a poll only reads what was appended since the last one. A map
 * is only used when it was set after the previous print started, i.e. for this print.
 *
 * "Match material only" (off unless chosen) extends the firmware's auto refill, which only
 * switches to a slot with the same type and colour: when it gives up, the first loaded slot
 * with the same material type continues the print. It trusts the Canvas material labels. */
#define RUNOUT_TOOLS_MAX 32
#define RUNOUT_SCRIPT_MAX 500           /* plates_exchange request[512], with the JSON around it */
#define RUNOUT_REPLY_MS (10 * 60 * 1000) /* heat-up, the Canvas load and the purge */
#define RUNOUT_TEMP_MIN 170
#define RUNOUT_TEMP_MAX 320

typedef struct { int tool, channel; char color[9]; } runout_entry; /* color: "0xRRGGBB" */
typedef struct { int count; runout_entry e[RUNOUT_TOOLS_MAX]; } runout_table;
typedef struct {
    long line;                          /* lines parsed so far */
    long start_last, start_prev;        /* line of the latest print start, and of the one before */
    long table_line;                    /* line where `table` was completed */
    runout_table table, pending;
    int pending_size;                   /* T_vec.size of the group being read, 0 for none */
    int refill_before;                  /* befor_channel of an auto refill in progress, -1 */
    int channel; long channel_line;     /* last runout slot (0-3) and its line */
} runout_log;

static const char *runout_log_paths[2] = {"/opt/usr/logs/elegoo.1.log", "/opt/usr/logs/elegoo.log"}; /* older first */
static const char *runout_settings_path = "./runout.conf";

static void runout_log_reset(runout_log *s) {
    memset(s, 0, sizeof(*s));
    s->start_last = s->start_prev = s->table_line = s->channel_line = -1;
    s->refill_before = s->channel = -1;
}

/* The integer after `key` in a line, or -1. */
static int runout_field(const char *line, const char *key, int low, int high) {
    const char *p = strstr(line, key);
    if (!p) return -1;
    p += strlen(key);
    while (*p == ' ' || *p == ':') p++;
    char *end; errno = 0; long v = strtol(p, &end, 10);
    if (end == p || errno || v < low || v > high) return -1;
    return (int)v;
}

static void runout_log_line(runout_log *s, const char *line) {
    s->line++;
    const char *p;
    if (strstr(line, "cmd_SDCARD_PRINT_FILE")) {
        s->start_prev = s->start_last; s->start_last = s->line;
    } else if ((p = strstr(line, "CMD_canvas_set_color_table #1 T_vec.size"))) {
        /* One line per entry: T_vec.size N table_T t table_color 0xRRGGBB table_channel c */
        int size = runout_field(p, "T_vec.size", 1, RUNOUT_TOOLS_MAX);
        int tool = runout_field(p, "table_T", 0, 255), channel = runout_field(p, "table_channel", 0, 3);
        const char *c = strstr(p, "table_color ");
        char color[9] = "";
        if (c) {
            c += 12;
            if (c[0] == '0' && (c[1] == 'x' || c[1] == 'X')) {
                int ok = 1;
                for (int i = 2; i < 8; ++i) if (!isxdigit((unsigned char)c[i])) ok = 0;
                if (ok && (c[8] == ' ' || c[8] == '\n' || c[8] == '\r' || !c[8])) { memcpy(color, c, 8); color[8] = 0; color[1] = 'x'; }
            }
        }
        if (size < 0 || tool < 0 || channel < 0 || !color[0]) { s->pending_size = 0; return; }
        if (!s->pending_size || size != s->pending_size || s->pending.count >= size) {
            s->pending_size = size; s->pending.count = 0;
        }
        runout_entry *e = &s->pending.e[s->pending.count++];
        e->tool = tool; e->channel = channel; memcpy(e->color, color, sizeof(color));
        if (s->pending.count == size) {
            s->table = s->pending; s->table_line = s->line; s->pending_size = 0;
        }
    } else if ((p = strstr(line, "exec auto refill filament start"))) {
        s->refill_before = runout_field(p, "befor_channel", 0, 3);
    } else if ((p = strstr(line, "exec auto refill filament over"))) {
        int after = runout_field(p, "after_channel", 0, 3);
        if (s->refill_before >= 0 && after >= 0 && after != s->refill_before)
            for (int i = 0; i < s->table.count; ++i)
                if (s->table.e[i].channel == s->refill_before) s->table.e[i].channel = after;
        s->refill_before = -1;
    } else if ((p = strstr(line, "canvas check filament break"))) {
        int channel = runout_field(p, "channel", 0, 3);
        if (channel >= 0) { s->channel = channel; s->channel_line = s->line; }
    }
}

/* Feeds whole lines from text; returns the bytes consumed (up to the last newline). */
static size_t runout_log_feed(runout_log *s, const char *text, size_t length) {
    size_t used = 0;
    char line[1024];
    while (used < length) {
        const char *eol = memchr(text + used, '\n', length - used);
        if (!eol) break;
        size_t n = (size_t)(eol - (text + used));
        /* Longer lines are vendor JSON dumps: none of the lines above is that long. */
        if (n < sizeof(line)) { memcpy(line, text + used, n); line[n] = 0; runout_log_line(s, line); }
        else s->line++;
        used += n + 1;
    }
    return used;
}

/* The current print's map, or 0. */
static int runout_log_table(const runout_log *s, runout_table *out) {
    if (s->start_last < 0 || s->table_line < 0 || s->table_line <= s->start_prev || !s->table.count) return 0;
    *out = s->table; return 1;
}
/* The runout slot of the current print, or -1. */
static int runout_log_channel(const runout_log *s) {
    return s->start_last >= 0 && s->channel_line > s->start_last ? s->channel : -1;
}

/* Incremental reader: the older file once, then whatever the current file has gained. */
static struct {
    runout_log log;
    int ready;
    dev_t dev; ino_t ino; off_t offset;
} runout_reader;

static int runout_read_from(runout_log *s, const char *path, off_t offset, off_t *consumed) {
    FILE *f = fopen(path, "rb");
    if (!f) return errno == ENOENT ? 0 : -1;
    if (offset && fseeko(f, offset, SEEK_SET) != 0) { fclose(f); return -1; }
    static char chunk[65536 + 1024];
    size_t carry = 0, n;
    off_t done = offset;
    while ((n = fread(chunk + carry, 1, sizeof(chunk) - carry, f)) > 0) {
        size_t have = carry + n, used = runout_log_feed(s, chunk, have);
        if (!used && have == sizeof(chunk)) used = have;  /* an endless line: skip it */
        done += (off_t)used;
        carry = have - used;
        memmove(chunk, chunk + used, carry);
    }
    int ok = !ferror(f);
    fclose(f);
    if (consumed) *consumed = done;
    return ok ? 1 : -1;
}

static int runout_refresh(void) {
    struct stat st;
    if (stat(runout_log_paths[1], &st) != 0) return 0;
    if (runout_reader.ready && (st.st_dev != runout_reader.dev || st.st_ino != runout_reader.ino ||
                                st.st_size < runout_reader.offset))
        runout_reader.ready = 0;  /* rotated or truncated: start again */
    if (!runout_reader.ready) {
        runout_log_reset(&runout_reader.log);
        if (runout_read_from(&runout_reader.log, runout_log_paths[0], 0, NULL) < 0) return 0;
        runout_reader.offset = 0;
        runout_reader.dev = st.st_dev; runout_reader.ino = st.st_ino; runout_reader.ready = 1;
    }
    off_t consumed = runout_reader.offset;
    if (runout_read_from(&runout_reader.log, runout_log_paths[1], runout_reader.offset, &consumed) < 0) {
        runout_reader.ready = 0; return 0;
    }
    runout_reader.offset = consumed;
    return 1;
}

/* ---- Canvas slots, from the snapshot the Canvas page uses (MQTT or the UDS variant). */
typedef struct { int known, status; char type[32], color[8]; double min_temp, max_temp; } runout_slot;

static int runout_text(const char *o, const char *e, const char *key, char *out, size_t cap) {
    const char *v = ccv_json_member(o, e, key);
    if (!v || *v != '"') return 0;
    const char *end = ccv_json_string_end(v, e);
    if (!end) return 0;
    size_t n = (size_t)(end - v) - 2;
    if (n >= cap) return 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)v[1 + i];
        if (c < 32 || c == '\\' || c == '"' || c == '<' || c == '>' || c == '&') return 0;
        out[i] = (char)c;
    }
    out[n] = 0; return 1;
}

static int runout_slots(const mqtt_client *m, runout_slot slots[4]) {
    memset(slots, 0, 4 * sizeof(*slots));
    if (!canvas_source_valid(m)) return 0;
    size_t length; const char *p = mqtt_canvas_snapshot(m, &length), *end = p + length;
    const char *re = end, *ie = end, *le = end;
    const char *result = canvas_child(p, end, "result", &re);
    const char *info = canvas_child(result, re, "canvas_info", &ie);
    const char *list = info ? ccv_json_member_object(info, ie, "canvas_list", '[', &le) : NULL;
    if (!list) return 0;
    int modules = 0;
    for (p = list + 1; p < le;) {
        p = ccv_json_skip_space(p, le); if (p >= le || *p == ']') break;
        if (*p != '{') return 0;
        const char *me = ccv_json_container_end(p, le); if (!me) return 0;
        double connected;
        if (ccv_numeric(p, me, "connected", &connected) && connected == 1) {
            if (++modules != 1) return 0;
            const char *te, *trays = ccv_json_member_object(p, me, "tray_list", '[', &te);
            if (!trays) return 0;
            for (const char *t = trays + 1; t < te;) {
                t = ccv_json_skip_space(t, te); if (t >= te || *t == ']') break;
                if (*t != '{') return 0;
                const char *se = ccv_json_container_end(t, te); if (!se) return 0;
                double id, status, low = 0, high = 0;
                char type[32], color[16];
                if (ccv_numeric(t, se, "tray_id", &id) && id >= 0 && id <= 3 && id == floor(id) &&
                    ccv_numeric(t, se, "status", &status) && runout_text(t, se, "filament_type", type, sizeof(type)) &&
                    runout_text(t, se, "filament_color", color, sizeof(color))) {
                    runout_slot *s = &slots[(int)id];
                    s->known = 1; s->status = (int)status;
                    snprintf(s->type, sizeof(s->type), "%s", type);
                    if (color[0] == '#' && strlen(color) == 7) memcpy(s->color, color, sizeof(s->color));
                    if (ccv_numeric(t, se, "min_nozzle_temp", &low)) s->min_temp = low;
                    if (ccv_numeric(t, se, "max_nozzle_temp", &high)) s->max_temp = high;
                }
                t = ccv_json_skip_space(se, te); if (t < te && *t == ',') t++; else if (t >= te || *t != ']') return 0;
            }
        }
        p = ccv_json_skip_space(me, le); if (p < le && *p == ',') p++; else if (p >= le || *p != ']') return 0;
    }
    return modules == 1;
}

static int runout_same_material(const runout_slot *a, const runout_slot *b) {
    return a->type[0] && b->type[0] && !strcasecmp(a->type, b->type);
}

/* ---- the continue job */
enum { RO_IDLE, RO_RUNNING, RO_DONE, RO_FAILED };
static struct {
    int stage, slot, from, automatic;
    char query[RUNOUT_SCRIPT_MAX + 64], message[160];
    plates_exchange x;
    struct timespec auto_event;     /* the 1211 event an automatic continue already handled */
    char auto_note[96];
} runout = {.x = {.fd = -1}};
static int runout_match_material = -1;  /* setting, read on first use */

static int runout_setting(void) {
    if (runout_match_material < 0) {
        runout_match_material = 0;
        FILE *f = fopen(runout_settings_path, "r");
        if (f) { char line[64]; while (fgets(line, sizeof(line), f)) if (!strncmp(line, "match_material=1", 16)) runout_match_material = 1; fclose(f); }
    }
    return runout_match_material;
}
static int runout_save_setting(int on) {
    char temp[256]; snprintf(temp, sizeof(temp), "%s.tmp", runout_settings_path);
    FILE *f = fopen(temp, "w");
    if (!f) return -1;
    int ok = fprintf(f, "match_material=%d\n", on ? 1 : 0) > 0;
    ok = !fclose(f) && ok;
    if (!ok || rename(temp, runout_settings_path) != 0) { unlink(temp); return -1; }
    runout_match_material = on ? 1 : 0;
    return 0;
}

/* Why no continue is possible now, or NULL with the map and runout slot filled in. */
static const char *runout_blocked(const mqtt_client *m, runout_table *table, int *channel, runout_slot slots[4]) {
    if (!uds_fresh(&telemetry)) return "printer_unavailable";
    const char *state = uds_print_state(&telemetry);
    if (!state || strcmp(state, "paused")) return "not_paused";
    if (!telemetry.report_sequence || telemetry.report_code != 1211 || telemetry.report_level != 1) return "not_runout";
    if (runout.stage == RO_RUNNING) return "busy";
    if (!runout_refresh()) return "log_unavailable";
    if (!runout_log_table(&runout_reader.log, table)) return "map_unknown";
    *channel = runout_log_channel(&runout_reader.log);
    if (*channel < 0) return "slot_unknown";
    int used = 0;
    for (int i = 0; i < table->count; ++i) if (table->e[i].channel == *channel) used = 1;
    if (!used) return "slot_unknown";
    if (!runout_slots(m, slots)) return "canvas_unavailable";
    return NULL;
}
/* A slot that can take over: loaded in the Canvas (present, not in the head) and not the empty one. */
static int runout_slot_eligible(const runout_slot slots[4], int channel, int slot) {
    return slot >= 0 && slot < 4 && slot != channel && slots[slot].known && slots[slot].status == 1;
}

static int runout_script(const runout_table *table, int from, int to, int temp, char *out, size_t cap) {
    static const char *const keys[] = {"CANVAS_SET_COLOR_TABLE T=", " ID=", " CHANNEL=", " COLOR="};
    size_t used = 0;
    for (int key = 0; key < 4; ++key)
        for (int i = 0; i < table->count; ++i) {
            const runout_entry *e = &table->e[i];
            int n = key == 3 ? snprintf(out + used, cap - used, "%s%s", i ? "," : keys[key], e->color)
                             : snprintf(out + used, cap - used, "%s%d", i ? "," : keys[key],
                                        key == 0 ? e->tool : key == 1 ? 0 : e->channel == from ? to : e->channel);
            if (n < 0 || (size_t)n >= cap - used) return -1;
            used += (size_t)n;
        }
    int n = snprintf(out + used, cap - used, "\\nM109 S%d\\nRESUME", temp);
    return n < 0 || (size_t)n >= cap - used ? -1 : 0;
}

/* The nozzle temperature for the reload: the print's target while the firmware holds it, else
 * the new slot's range. RESUME restores the print's own temperatures afterwards. */
static int runout_temperature(const runout_slot *s) {
    double target = 0;
    if (uds_value(&telemetry, U_EG, &target) && target >= RUNOUT_TEMP_MIN && target <= RUNOUT_TEMP_MAX) return (int)lround(target);
    if (s->min_temp >= RUNOUT_TEMP_MIN && s->max_temp >= s->min_temp && s->max_temp <= RUNOUT_TEMP_MAX)
        return (int)lround((s->min_temp + s->max_temp) / 2);
    return -1;
}

static const char *runout_start(const mqtt_client *m, int slot, int automatic) {
    runout_table table; runout_slot slots[4]; int channel = -1;
    const char *why = runout_blocked(m, &table, &channel, slots);
    if (why) return why;
    if (!runout_slot_eligible(slots, channel, slot)) return "slot_not_loaded";
    int temp = runout_temperature(&slots[slot]);
    if (temp < 0) return "temperature_unknown";
    char script[RUNOUT_SCRIPT_MAX];
    if (runout_script(&table, channel, slot, temp, script, sizeof(script)) != 0) return "map_too_large";
    int n = snprintf(runout.query, sizeof(runout.query),
                     "{\"id\":204,\"method\":\"gcode/script\",\"params\":{\"script\":\"%s\"}}\003", script);
    if (n < 0 || (size_t)n >= sizeof(runout.x.request)) return "map_too_large";
    plates_exchange_close(&runout.x);
    runout.stage = RO_RUNNING; runout.slot = slot; runout.from = channel; runout.automatic = automatic;
    snprintf(runout.message, sizeof(runout.message), "%s", "");
    return NULL;
}

static void runout_tick(const mqtt_client *m) {
    if (runout.stage == RO_RUNNING) {
        char *reply = NULL; size_t length = 0;
        int result = plates_exchange_step(&runout.x, runout.query, &reply, &length);
        if (result == 0) {
            /* Sent: the reply comes once RESUME has loaded the slot and the print runs again. */
            if (runout.x.fd >= 0 && runout.x.sent == strlen(runout.query) && runout.x.deadline < monotonic_ms() + RUNOUT_REPLY_MS - 5000)
                runout.x.deadline = monotonic_ms() + RUNOUT_REPLY_MS;
            return;
        }
        const char *end = reply ? reply + length : NULL;
        const char *error = reply ? json_member(reply, end, "error") : NULL;
        runout.stage = result > 0 && !error ? RO_DONE : RO_FAILED;
        snprintf(runout.message, sizeof(runout.message), "%s",
                 result < 0 ? "No reply from the printer" : error ? "The printer refused the command" : "");
        free(reply);
        return;
    }
    /* Automatic: once per runout event, only when the firmware's own auto refill is on and gave up. */
    if (!runout_setting()) return;
    int refill = m->canvas_uds_ready ? telemetry.canvas.auto_refill : m->have_auto_refill && m->auto_refill;
    if (!refill || !telemetry.report_sequence || telemetry.report_code != 1211 || telemetry.report_level != 1) return;
    if (runout.auto_event.tv_sec == telemetry.report_identity.tv_sec &&
        runout.auto_event.tv_nsec == telemetry.report_identity.tv_nsec) return;
    runout_table table; runout_slot slots[4]; int channel = -1;
    const char *why = runout_blocked(m, &table, &channel, slots);
    if (why) return;  /* not yet settled, or no map: the page offers the choice */
    runout.auto_event = telemetry.report_identity;
    for (int i = 0; i < 4; ++i)
        if (runout_slot_eligible(slots, channel, i) && runout_same_material(&slots[channel], &slots[i])) {
            why = runout_start(m, i, 1);
            snprintf(runout.auto_note, sizeof(runout.auto_note), "%s", why ? why : "");
            return;
        }
    snprintf(runout.auto_note, sizeof(runout.auto_note), "%s", "no_matching_slot");
}

static void runout_response(int fd, const mqtt_client *m) {
    runout_table table; runout_slot slots[4]; int channel = -1;
    const char *why = runout_blocked(m, &table, &channel, slots);
    char body[4096];
    json_builder b = {body, 0, sizeof(body), 0};
    static const char *const stages[] = {"idle", "running", "done", "failed"};
    json_builder_printf(&b, "{\"available\":%s,\"reason\":\"%s\",\"match_material\":%s,\"job\":{\"state\":\"%s\",\"slot\":%d,\"from\":%d,\"automatic\":%s,\"message\":",
        why ? "false" : "true", why ? why : "none", runout_setting() ? "true" : "false", stages[runout.stage],
        runout.stage == RO_IDLE ? -1 : runout.slot, runout.stage == RO_IDLE ? -1 : runout.from, runout.automatic ? "true" : "false");
    json_builder_string(&b, runout.message);
    json_builder_printf(&b, ",\"auto_note\":");
    json_builder_string(&b, runout.auto_note);
    json_builder_printf(&b, "}");
    if (!why) {
        json_builder_printf(&b, ",\"slot\":%d,\"tools\":[", channel);
        int first = 1;
        for (int i = 0; i < table.count; ++i)
            if (table.e[i].channel == channel) { json_builder_printf(&b, "%s%d", first ? "" : ",", table.e[i].tool); first = 0; }
        json_builder_printf(&b, "],\"material\":");
        json_builder_string(&b, slots[channel].type);
        json_builder_printf(&b, ",\"slots\":[");
        for (int i = 0; i < 4; ++i) {
            json_builder_printf(&b, "%s{\"slot\":%d,\"status\":%d,\"type\":", i ? "," : "", i, slots[i].known ? slots[i].status : -1);
            json_builder_string(&b, slots[i].type);
            json_builder_printf(&b, ",\"color\":");
            json_builder_string(&b, slots[i].color);
            json_builder_printf(&b, ",\"eligible\":%s,\"same_material\":%s}",
                runout_slot_eligible(slots, channel, i) ? "true" : "false",
                i != channel && runout_same_material(&slots[channel], &slots[i]) ? "true" : "false");
        }
        json_builder_printf(&b, "]");
    }
    json_builder_printf(&b, "}\n");
    if (b.failed) respond(fd, 500, "Internal Server Error", "application/json; charset=utf-8", "{\"available\":false}\n", 19);
    else respond(fd, 200, "OK", "application/json; charset=utf-8", body, b.length);
}

/* POST /api/runout/continue, body "slot=N" (0-3). */
static void runout_continue_response(int fd, const mqtt_client *m, const char *body, size_t length) {
    int slot = -1;
    if (length == 6 && !memcmp(body, "slot=", 5) && body[5] >= '0' && body[5] <= '3') slot = body[5] - '0';
    const char *why = slot < 0 ? "bad_request" : runout_start(m, slot, 0);
    char reply[160];
    int n = snprintf(reply, sizeof(reply), "{\"accepted\":%s,\"reason\":\"%s\"}\n", why ? "false" : "true", why ? why : "none");
    respond(fd, why ? (slot < 0 ? 400 : 409) : 202, why ? (slot < 0 ? "Bad Request" : "Conflict") : "Accepted",
            "application/json; charset=utf-8", reply, (size_t)n);
}

/* POST /api/runout/match-material, body "on" or "off". */
static void runout_setting_response(int fd, const char *body, size_t length) {
    int on = length == 2 && !memcmp(body, "on", 2) ? 1 : length == 3 && !memcmp(body, "off", 3) ? 0 : -1;
    const char *reply = on < 0 ? "{\"accepted\":false,\"reason\":\"bad_request\"}\n" :
                        runout_save_setting(on) ? "{\"accepted\":false,\"reason\":\"save_failed\"}\n" : "{\"accepted\":true}\n";
    int status = on < 0 ? 400 : strstr(reply, "true") ? 200 : 500;
    respond(fd, status, status == 200 ? "OK" : status == 400 ? "Bad Request" : "Internal Server Error",
            "application/json; charset=utf-8", reply, strlen(reply));
}
#endif
