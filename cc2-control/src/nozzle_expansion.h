#ifndef CC2_NOZZLE_EXPANSION_H
#define CC2_NOZZLE_EXPANSION_H
/* Nozzle thermal-expansion calibration. HTTP-loop owned.
 *
 * The firmware's [z_compensation] raises Z by the nozzle's growth since the temperature it
 * homed at (fix_ref_temperature), read from a table probed every `diff` degrees from
 * `nozzle_calibrate_start`; above the table it extrapolates. The stock table ends at 250 C.
 * NOZZLE_TEMP_CALIBRATE measures a new one: it homes, cleans the nozzle, holds it over the bed
 * centre and probes after each step with the bed unheated, then leaves the nozzle at the last
 * temperature and the table in memory. CC2 Control runs it as one long script, always switches
 * the nozzle off afterwards, saves the table with SAVE_CONFIG and keeps it only if it is
 * plausible; otherwise the previous [z_compensation] lines of autosave.cfg are put back. The
 * firmware reads the table when it starts, so a saved table waits for a printer restart.
 * A session journal makes a restarted CC2 Control switch the nozzle off once the printer is idle. */
enum { NX_IDLE, NX_RUNNING, NX_COOLING, NX_SAVING, NX_RECOVERY };
#define NX_POINTS_MAX 48
#define NX_DELAY_S 60       /* hold after each step, so the heat break and nozzle settle */
#define NX_SAMPLES 5        /* probes per step */
/* The run is one request that replies when the firmware has finished: homing and the nozzle clean, then per step
 * heating, NX_DELAY_S and NX_SAMPLES probes (~3 min each on a CC2). Generous, so a slow printer is not abandoned. */
#define NX_SETUP_MS (15LL * 60 * 1000)
#define NX_STEP_MS (8LL * 60 * 1000)
#define NX_OFF_GRACE_MS (10LL * 60 * 1000)   /* after the run's budget, to see the nozzle switched off */
#define NX_RECOVERY_WAIT_MS (15LL * 60 * 1000)
#define NX_SECTION "[z_compensation]"
/* What a plausible table looks like: the stock table rises 0.74 um/C and stays within
 * 2.3 um of a straight line; a drop of ooze under the nozzle shows as a step of tens of um. */
#define NX_SLOPE_MIN 0.0002 /* mm/C */
#define NX_SLOPE_MAX 0.0020
#define NX_RESIDUAL_MAX 0.015
#define NX_DROP_MAX 0.006
typedef struct { int known; double start, diff, ref; int count; double values[NX_POINTS_MAX]; } nx_table;
static struct {
    int stage, start, end, step, ok, off_acked;
    long long since, retry_at, cooling_since, deadline; /* deadline: monotonic, end of the run's budget */
    unsigned long connections;                         /* printer-service connection the run was sent on */
    const char *result; /* "", "saved", "rejected", "failed" or "interrupted" */
    char detail[160];
    nx_table measured;
    double slope, residual;
    plates_exchange exchange;
    char query[256];
} nx = {.exchange = {.fd = -1}, .result = ""};
static const char *nx_journal = "/opt/usr/cc2-nozzle-expansion.session";
static const char *nx_pending_path = "/tmp/cc2-control-nozzle-expansion-pending";
static long long (*nx_clock)(void) = monotonic_ms;
static int nx_restart_requested;
static char *nx_before; /* autosave.cfg as the run found it */
static size_t nx_before_len;

static int nx_active(void) { return nx.stage != NX_IDLE; }

/* ---- tables ------------------------------------------------------------------------------ */

static int nx_number(const char *text, size_t length, double *out) { return canvas_unload_number(text, length, out); }

static int nx_list(const char *text, size_t length, nx_table *t) {
    t->count = 0;
    for (size_t pos = 0; pos <= length;) {
        const char *comma = memchr(text + pos, ',', length - pos);
        size_t end = comma ? (size_t)(comma - text) : length;
        if (t->count == NX_POINTS_MAX || !nx_number(text + pos, end - pos, &t->values[t->count])) return 0;
        t->count++;
        if (!comma) break;
        pos = end + 1;
    }
    return t->count >= 2;
}

/* key = value inside a section line (already stripped of the "#*# " prefix or a comment). */
static void nx_key(const char *line, size_t n, nx_table *t, int *have) {
    static const char *keys[] = {"nozzle_calibrate_start", "nozzle_calibrate_diff", "nozzle_calibrate_datas",
                                 "fix_ref_temperature"};
    for (int k = 0; k < 4; k++) {
        size_t key_len = strlen(keys[k]), rest = key_len;
        if (n <= key_len || memcmp(line, keys[k], key_len)) continue;
        while (rest < n && isspace((unsigned char)line[rest])) rest++;
        if (rest == n || (line[rest] != '=' && line[rest] != ':')) continue;
        const char *value = line + rest + 1; size_t value_len = n - rest - 1;
        int ok = k == 0 ? nx_number(value, value_len, &t->start) : k == 1 ? nx_number(value, value_len, &t->diff) :
                 k == 2 ? nx_list(value, value_len, t) : nx_number(value, value_len, &t->ref);
        if (ok) *have |= 1 << k; else *have |= 1 << 8;
    }
}

static int nx_table_complete(nx_table *t, int have) {
    t->known = !(have & (1 << 8)) && (have & 7) == 7 && isfinite(t->start) && isfinite(t->diff) && t->diff > 0;
    return t->known;
}

/* The vendor configuration's table, as the firmware's INI loader reads it ('#' starts a comment);
 * `*section` reports whether a [z_compensation] section exists at all. */
static void nx_configured(const char *text, size_t length, nx_table *t, int *section) {
    int inside = 0, have = 0;
    memset(t, 0, sizeof(*t)); *section = 0;
    for (size_t pos = 0; pos < length;) {
        size_t eol = autosave_line_end(text, length, pos), next = eol < length ? eol + 1 : length;
        const char *line = text + pos, *hash = memchr(line, '#', eol - pos);
        size_t n = hash ? (size_t)(hash - line) : eol - pos;
        while (n && isspace((unsigned char)*line)) { line++; n--; }
        while (n && isspace((unsigned char)line[n - 1])) n--;
        if (n && line[0] == '[') {
            inside = n == strlen(NX_SECTION) && !memcmp(line, NX_SECTION, n);
            if (inside) *section = 1;
        } else if (inside && n) nx_key(line, n, t, &have);
        pos = next;
    }
    nx_table_complete(t, have);
}

/* The SAVE_CONFIG section in autosave.cfg: 1 parsed (it may be absent), -1 a layout this code
 * does not edit (no marker, a carriage return, a duplicate or noncanonical header). [start, end)
 * spans the header and its key lines. */
static int nx_autosave(const char *text, size_t length, nx_table *t, size_t *start, size_t *end, int *found) {
    static const char header[] = "#*# " NX_SECTION;
    const size_t header_len = sizeof(header) - 1;
    int have = 0;
    memset(t, 0, sizeof(*t)); *found = 0; *start = *end = 0;
    if (memchr(text, '\r', length) || memchr(text, '\0', length)) return -1;
    const char *marker = strstr(text, AUTOSAVE_MARKER);
    if (!marker) return -1;
    for (size_t pos = (size_t)(marker - text); pos < length;) {
        size_t eol = autosave_line_end(text, length, pos), next = eol < length ? eol + 1 : length;
        if (eol - pos >= 3 && !memcmp(text + pos, "#*#", 3)) {
            const char *section = text + pos + 3; size_t n = eol - pos - 3;
            while (n && isspace((unsigned char)*section)) { section++; n--; }
            while (n && isspace((unsigned char)section[n - 1])) n--;
            if (n == strlen(NX_SECTION) && !memcmp(section, NX_SECTION, n) &&
                (eol - pos != header_len || memcmp(text + pos, header, header_len))) return -1;
        }
        if (eol - pos != header_len || memcmp(text + pos, header, header_len)) { pos = next; continue; }
        if (*found) return -1;
        *found = 1; *start = pos;
        size_t line = next;
        while (line < length) {
            size_t line_eol = autosave_line_end(text, length, line), after = line_eol < length ? line_eol + 1 : length;
            if (line_eol - line < 5 || memcmp(text + line, "#*# ", 4) || text[line + 4] == '[') break;
            nx_key(text + line + 4, line_eol - line - 4, t, &have);
            line = after;
        }
        *end = line; pos = line;
    }
    if (*found) nx_table_complete(t, have);
    return 1;
}

/* A copy of autosave.cfg whose [z_compensation] section is replaced by `section` (its exact
 * text, header included), or removed with the separator lines before it when `section` is NULL.
 * Every byte outside the section stays as it was. */
static char *nx_autosave_replace(const char *text, size_t length, const char *section, size_t section_len,
                                 size_t *out_length) {
    static const char separator[] = "#*#\n";
    nx_table t; size_t start, end; int found;
    if (nx_autosave(text, length, &t, &start, &end, &found) != 1) return NULL;
    size_t cut_start = start, cut_end = end;
    if (!found && !section) cut_start = cut_end = 0;
    else if (!found) { /* a new section after the last non-empty line, like SAVE_CONFIG lays them out */
        cut_start = cut_end = length;
        while (cut_start && text[cut_start - 1] == '\n') cut_start = --cut_end;
    } else if (!section) {
        while (cut_start >= 4 && !memcmp(text + cut_start - 4, separator, 4) &&
               (cut_start == 4 || text[cut_start - 5] == '\n')) cut_start -= 4;
    }
    /* A new section goes in without its last newline: the file's own ending follows it. */
    const char *prefix = section && !found ? "\n#*#\n#*#\n" : "";
    size_t prefix_len = strlen(prefix), insert_len = section ? section_len : 0;
    if (section && !found && insert_len && section[insert_len - 1] == '\n') insert_len--;
    const char *suffix = section && !found && cut_end == length ? "\n" : "";
    size_t suffix_len = strlen(suffix);
    size_t used = cut_start + prefix_len + insert_len + suffix_len + (length - cut_end);
    char *copy = malloc(used + 1);
    if (!copy) return NULL;
    memcpy(copy, text, cut_start);
    memcpy(copy + cut_start, prefix, prefix_len);
    if (insert_len) memcpy(copy + cut_start + prefix_len, section, insert_len);
    memcpy(copy + cut_start + prefix_len + insert_len, suffix, suffix_len);
    memcpy(copy + cut_start + prefix_len + insert_len + suffix_len, text + cut_end, length - cut_end);
    copy[used] = 0;
    nx_table check; size_t s, e; int again;
    if (nx_autosave(copy, used, &check, &s, &e, &again) != 1 || again != (section != NULL)) { free(copy); return NULL; }
    *out_length = used;
    return copy;
}

/* NULL when the measured table is plausible; sets the fitted slope and the largest residual. */
static const char *nx_validate(const nx_table *t, int start, int step, int count, double *slope, double *residual) {
    *slope = *residual = 0;
    if (!t->known) return "The printer did not save a table";
    if (fabs(t->start - start) > 0.001 || fabs(t->diff - step) > 0.001 || t->count != count)
        return "The saved table does not match the requested temperatures";
    double mx = 0, my = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < count; i++) {
        if (!isfinite(t->values[i]) || fabs(t->values[i]) > 2.0) return "A reading is out of range";
        mx += start + i * step; my += t->values[i];
    }
    mx /= count; my /= count;
    for (int i = 0; i < count; i++) {
        double dx = start + i * step - mx;
        sxx += dx * dx; sxy += dx * (t->values[i] - my);
    }
    *slope = sxy / sxx;
    for (int i = 0; i < count; i++) {
        double r = fabs(t->values[i] - (my + *slope * (start + i * step - mx)));
        if (r > *residual) *residual = r;
        if (i && t->values[i] - t->values[i - 1] < -NX_DROP_MAX) return "A reading drops as the nozzle heats up";
    }
    if (*slope < NX_SLOPE_MIN || *slope > NX_SLOPE_MAX) return "The nozzle growth is outside the expected range";
    if (*residual > NX_RESIDUAL_MAX) return "A reading is far from the trend; the nozzle may have oozed";
    return NULL;
}

/* ---- session ----------------------------------------------------------------------------- */

static int nx_pending(void) {
    char now[64], saved[64]; size_t length = 0;
    char *text = plates_read_file(nx_pending_path, sizeof(saved) - 1, &length);
    if (!text) return 0;
    snprintf(saved, sizeof(saved), "%.*s", (int)length, text);
    free(text);
    if (canvas_unload_boot_id(now, sizeof(now)) && !strcmp(now, saved)) return 1;
    unlink(nx_pending_path);
    return 0;
}

static int nx_mark_pending(void) {
    char boot[64];
    return canvas_unload_boot_id(boot, sizeof(boot)) &&
           plates_replace_file(nx_pending_path, NULL, boot, strlen(boot), 0644) == 0;
}

static int nx_journal_write(void) {
    char text[96]; int n = snprintf(text, sizeof(text), "CC2NX2 %d %d %d %lld\n", nx.start, nx.end, nx.step, (long long)time(NULL));
    int fd = open(nx_journal, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return 0;
    int ok = write(fd, text, (size_t)n) == n;
    if (fsync(fd)) ok = 0;
    if (close(fd)) ok = 0;
    if (!ok) unlink(nx_journal);
    return ok;
}

static long long nx_budget_ms(int start, int end, int step) {
    return NX_SETUP_MS + (step > 0 ? (end - start) / step + 1 : NX_POINTS_MAX) * NX_STEP_MS;
}

static void nx_load(void) {
    int fd = open(nx_journal, O_RDONLY | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return;
    char text[96] = {0}; ssize_t n = read(fd, text, sizeof(text) - 1);
    close(fd);
    /* A run whose CC2 Control stopped: the firmware may still be running it and heating. */
    int start = 0, end = 0, step = 0; long long began = 0;
    if (n > 0 && sscanf(text, "CC2NX2 %d %d %d %lld", &start, &end, &step, &began) == 4 && step > 0 && end > start) {
        nx.start = start; nx.end = end; nx.step = step;
    } else began = 0;
    long long left = began ? nx_budget_ms(nx.start, nx.end, nx.step) - (time(NULL) - began) * 1000LL
                           : nx_budget_ms(100, 340, 5);   /* unknown run: assume the longest */
    nx.since = nx_clock(); nx.deadline = nx.since + (left > 0 ? left : 0);
    nx.stage = NX_RECOVERY; nx.result = "interrupted";
    snprintf(nx.detail, sizeof(nx.detail), "CC2 Control restarted during the calibration");
}

static void nx_end(const char *result, const char *detail) {
    plates_exchange_close(&nx.exchange); nx.query[0] = 0;
    if (unlink(nx_journal) && errno != ENOENT) fprintf(stderr, "Nozzle expansion: cannot remove %s\n", nx_journal);
    if (nx.stage) fprintf(stderr, "Nozzle expansion calibration %d-%d/%d: %s %s\n", nx.start, nx.end, nx.step, result, detail);
    nx.stage = NX_IDLE; nx.result = result;
    if (detail != nx.detail) snprintf(nx.detail, sizeof(nx.detail), "%s", detail);
    free(nx_before); nx_before = NULL; nx_before_len = 0;
}

/* The run set the measured table in the firmware's memory (configfile set). Any later SAVE_CONFIG, also from
 * the touchscreen, writes that memory back over autosave.cfg, so a table that was not kept stays a risk until
 * the printer restarts: require that restart. */
static void nx_end_discard(const char *result, const char *detail) {
    static const char note[] = " (could not record the required restart)";
    char text[sizeof(nx.detail)];
    size_t n = strnlen(detail, sizeof(text) - 1);
    memcpy(text, detail, n); text[n] = 0;
    if (!nx_mark_pending() && n + sizeof(note) <= sizeof(text)) memcpy(text + n, note, sizeof(note));
    nx_end(result, text);
}

static void nx_command(const char *script) {
    plates_exchange_close(&nx.exchange);
    snprintf(nx.query, sizeof(nx.query), "{\"id\":204,\"method\":\"gcode/script\",\"params\":{\"script\":\"%s\"}}\003", script);
}

/* Exact correlated success: the reply to our request id with a result and no error. */
static int nx_reply_ok(const char *text, size_t length) {
    const char *end = text + length, *root = json_skip_space(text, end);
    const char *root_end = root < end && *root == '{' ? json_container_end(root, end) : NULL;
    int id = -1;
    return root_end && json_skip_space(root_end, end) == end && !json_member(root, root_end, "error") &&
           json_member_int(root, root_end, "id", &id) && id == 204 && json_member(root, root_end, "result");
}

/* The nozzle heater may be switched off by us only while no print owns it. The printer's own print state
 * decides: the vendor print.enabled flag stays set after a finished or cancelled print until a reboot. */
static int nx_no_print(const mqtt_client *mqtt) {
    const char *state = uds_print_state(&telemetry);
    return mqtt->connected && mqtt->registered && state &&
           (!strcmp(state, "standby") || !strcmp(state, "complete") || !strcmp(state, "cancelled"));
}

/* Reads the table SAVE_CONFIG wrote; keeps it when plausible, otherwise puts the previous section back. */
static void nx_check_saved(const char *before, size_t before_len) {
    size_t length = 0, start, end, b_start, b_end; int found, b_found; nx_table previous;
    char *text = plates_read_file(printer_autosave_path, AUTOSAVE_FILE_MAX, &length);
    if (!text || nx_autosave(text, length, &nx.measured, &start, &end, &found) != 1) {
        int back = before && plates_replace_file(printer_autosave_path, NULL, before, before_len, autosave_mode()) == 0;
        free(text);
        nx_end_discard("failed", back ? "Cannot read the saved table; autosave.cfg was put back as the run found it"
                                      : "Cannot read the saved table; CHECK autosave.cfg");
        return;
    }
    int count = (nx.end - nx.start) / nx.step + 1;
    const char *problem = nx_validate(&nx.measured, nx.start, nx.step, count, &nx.slope, &nx.residual);
    if (!problem) {
        free(text);
        if (!nx_mark_pending()) { nx_end("saved", "Saved; restart the printer to use the new table"); return; }
        nx_end("saved", ""); return;
    }
    /* Put back the section the run started with (none: remove it), leaving SAVE_CONFIG's other lines. */
    /* No backup: SAVE_CONFIG's own autosave_backup.cfg is the file as the run found it; keep that. */
    size_t new_length = 0; char *restored = NULL;
    if (before && nx_autosave(before, before_len, &previous, &b_start, &b_end, &b_found) == 1)
        restored = nx_autosave_replace(text, length, b_found ? before + b_start : NULL, b_found ? b_end - b_start : 0,
                                       &new_length);
    int written = restored && plates_replace_file(printer_autosave_path, NULL, restored, new_length, autosave_mode()) == 0;
    free(restored); free(text);
    char detail[160];
    snprintf(detail, sizeof(detail), "%s%s", problem,
             written ? "; the previous table was kept" : "; CHECK autosave.cfg, the previous table could not be restored");
    nx_end_discard("rejected", detail);
}

/* Switch the nozzle off: M104 S0 waits in the firmware's queue behind a calibration that may still be running,
 * so it is given the rest of the run's budget; the heater counts as off only after its reply and a zero target. */
static void nx_cool(long long now, int ok) {
    plates_exchange_close(&nx.exchange); nx.query[0] = 0;
    nx.stage = NX_COOLING; nx.ok = ok; nx.off_acked = 0; nx.cooling_since = now; nx.retry_at = 0;
}

static void nx_tick(const mqtt_client *mqtt) {
    if (!nx_active()) return;
    long long now = nx_clock();
    double target = 0;
    int have_target = uds_value(&telemetry, U_EG, &target);
    if (nx.stage == NX_RECOVERY) {
        int fresh = mqtt->connected && mqtt->registered && uds_fresh(&telemetry) && uds_print_state(&telemetry);
        if (!fresh) {
            if (now - nx.since > NX_RECOVERY_WAIT_MS)
                nx_end_discard("interrupted", "CC2 Control restarted during the calibration and could not reach the printer; check that the nozzle is off");
            return;
        }
        /* A print owns the heater now: never switch it off under the print. */
        if (!nx_no_print(mqtt)) { nx_end_discard("interrupted", "CC2 Control restarted during the calibration; a print is running now"); return; }
        nx_cool(now, 0); nx.connections = telemetry.connections;
    }
    if (nx.stage == NX_RUNNING && telemetry.connections != nx.connections) {
        /* The printer service restarted (emergency stop, firmware restart): the run is gone with it. */
        nx.result = "failed"; snprintf(nx.detail, sizeof(nx.detail), "The printer restarted during the calibration");
        nx_cool(now, 0);
    }
    if (nx.stage == NX_COOLING && !nx.query[0]) {
        int firmware_restarted = telemetry.connections != nx.connections;
        if (have_target && uds_fresh(&telemetry) && target == 0 && now - nx.cooling_since > 1000 &&
            (nx.off_acked || firmware_restarted)) {
            if (!nx.ok) { nx_end_discard(nx.result[0] ? nx.result : "failed", nx.detail); return; }
            nx.stage = NX_SAVING; nx_command("SAVE_CONFIG"); return;
        }
        long long limit = (nx.deadline > nx.cooling_since ? nx.deadline : nx.cooling_since) + NX_OFF_GRACE_MS;
        if (now > limit) {
            nx_end_discard("failed", "Could not confirm the nozzle heater is off; check the printer (its idle timeout switches the heater off after 5 minutes)");
            return;
        }
        /* Acknowledged but still heating after a while (something set it again): send it again. */
        if (nx.off_acked && now >= nx.retry_at && have_target && uds_fresh(&telemetry) && target != 0) nx.off_acked = 0;
        if (now < nx.retry_at || nx.off_acked) return;
        /* No print state: the firmware is not answering (an emergency stop, a restart); keep trying until the limit. */
        if (!uds_print_state(&telemetry)) return;
        if (!nx_no_print(mqtt)) { nx_end_discard("failed", "A print started before the nozzle was switched off"); return; }
        nx_command("M104 S0");
    }
    if (!nx.query[0]) return;
    char *reply = NULL; size_t length = 0;
    int fresh_exchange = nx.exchange.fd < 0;
    int result = plates_exchange_step(&nx.exchange, nx.query, &reply, &length);
    /* The calibration replies when it finishes; M104 S0 may wait behind it; SAVE_CONFIG writes a file. */
    if (!result && fresh_exchange && nx.exchange.fd >= 0) {
        long long until = nx.deadline > now ? nx.deadline : now;
        nx.exchange.deadline = nx.stage == NX_SAVING ? now + 20000 : nx.stage == NX_COOLING ? until + NX_OFF_GRACE_MS : until;
    }
    if (!result) return;
    int accepted = result > 0 && nx_reply_ok(reply, (size_t)length);
    free(reply); nx.query[0] = 0;
    if (nx.stage == NX_RUNNING) {
        nx_cool(now, accepted);
        if (!accepted) {
            nx.result = "failed";
            snprintf(nx.detail, sizeof(nx.detail), "%s", result > 0 ? "The printer stopped the calibration"
                                                                     : "Lost the connection to the printer during the calibration");
        }
    } else if (nx.stage == NX_COOLING) {
        nx.off_acked = accepted; nx.retry_at = now + 5000;
    } else if (nx.stage == NX_SAVING) {
        if (!accepted) { nx_end_discard("failed", "SAVE_CONFIG failed; the table was not saved"); return; }
        nx_check_saved(nx_before, nx_before_len);
    }
}

static void nx_table_json(json_builder *b, const char *name, const nx_table *t) {
    if (!t->known) { json_builder_printf(b, ",\"%s\":null", name); return; }
    json_builder_printf(b, ",\"%s\":{\"start\":%.6g,\"diff\":%.6g,\"values\":[", name, t->start, t->diff);
    for (int i = 0; i < t->count; i++) json_builder_printf(b, "%s%.6f", i ? "," : "", t->values[i]);
    json_builder_printf(b, "]}");
}

static void nx_state(int fd, int status) {
    static const char *stages[] = {"idle", "running", "cooling", "saving", "recovery"};
    size_t length = 0, start, end; int section = 0, found = 0;
    nx_table factory, override;
    char *text = plates_read_file(printer_config_path, CANVAS_UNLOAD_CONFIG_MAX, &length);
    if (text) nx_configured(text, length, &factory, &section); else memset(&factory, 0, sizeof(factory));
    free(text);
    text = plates_read_file(printer_autosave_path, AUTOSAVE_FILE_MAX, &length);
    int editable = text && nx_autosave(text, length, &override, &start, &end, &found) == 1;
    if (!editable || !found) memset(&override, 0, sizeof(override));
    free(text);
    const char *restart = "none";
    if (nx_restart_requested) {
        if (reboot_pending) restart = "restarting";
        else if (strcmp(reboot_error, "none")) { restart = reboot_error; nx_restart_requested = 0; }
    }
    double nozzle = 0, target = 0;
    int have = uds_value(&telemetry, U_ET, &nozzle) && uds_value(&telemetry, U_EG, &target);
    long long elapsed = nx_active() && nx.stage != NX_RECOVERY ? (nx_clock() - nx.since) / 1000 : 0;
    char body[4096];
    json_builder b = {body, 0, sizeof(body), 0};
    json_builder_printf(&b, "{\"ok\":true,\"available\":%s,\"editable\":%s,\"state\":\"%s\",\"result\":\"%s\",\"detail\":",
                        section ? "true" : "false", editable ? "true" : "false", stages[nx.stage], nx.result);
    json_builder_string(&b, nx.detail);
    json_builder_printf(&b, ",\"start\":%d,\"end\":%d,\"step\":%d,\"delay\":%d,\"samples\":%d,\"elapsed\":%lld",
                        nx.start, nx.end, nx.step, NX_DELAY_S, NX_SAMPLES, elapsed);
    if (have) json_builder_printf(&b, ",\"nozzle\":%.1f,\"target\":%.1f", nozzle, target);
    else json_builder_printf(&b, ",\"nozzle\":null,\"target\":null");
    json_builder_printf(&b, ",\"reference\":%.6g", factory.ref > 0 ? factory.ref : 0);
    nx_table_json(&b, "factory", &factory);
    nx_table_json(&b, "override", &override);
    nx_table_json(&b, "measured", &nx.measured);
    json_builder_printf(&b, ",\"slope_um\":%.3f,\"residual_um\":%.1f,\"pending_restart\":%s,\"restart\":\"%s\"}\n",
                        nx.slope * 1000, nx.residual * 1000, nx_pending() ? "true" : "false", restart);
    plates_reply(fd, b.failed ? 500 : status, b.failed ? "{\"ok\":false,\"error\":\"Reply too large\"}\n" : body);
}

static void nx_response(int fd) { nx_state(fd, 200); }

/* Body start:end:step, e.g. 130:320:10. */
static void nx_start_response(int fd, const char *request, const char *body, size_t length, mqtt_client *mqtt) {
    if (!app_update_authorized(request, mqtt)) { app_update_fail(fd, 401, "LAN code confirmation required or temporarily locked"); return; }
    int start = 0, end = 0, step = 0; char input[32] = {0}, canonical[32];
    if (length < 8 || length > 11) { plates_fail(fd, 400, "Invalid calibration range"); return; }
    memcpy(input, body, length);
    if (sscanf(input, "%d:%d:%d", &start, &end, &step) != 3 ||
        snprintf(canonical, sizeof(canonical), "%d:%d:%d", start, end, step) != (int)length || memcmp(canonical, body, length) ||
        start < 100 || start > 200 || (step != 5 && step != 10) || end > 340 || end < start + 3 * step ||
        (end - start) % step || (end - start) / step + 1 > NX_POINTS_MAX) {
        plates_fail(fd, 400, "Use a start of 100-200 C, an end up to 340 C and steps of 5 or 10 C"); return;
    }
    if (nx_active()) { plates_fail(fd, 409, "A nozzle expansion calibration is already running"); return; }
    const char *busy = plates_printer_ready(mqtt);
    if (busy) { plates_fail(fd, 409, busy); return; }
    if (z_offset_pending || drying_active()) { plates_fail(fd, 409, "Another printer operation is still running"); return; }
    if (!nx_no_print(mqtt)) { plates_fail(fd, 409, "A print is active"); return; }
    /* The run ends with SAVE_CONFIG, which writes the settings the firmware loaded at start-up (plus its own
     * changes) over autosave.cfg: a saved change still waiting for a restart would be lost. */
    const char *pending = pending_autosave_guard();
    if (pending || plates_reboot_requested) {
        plates_fail(fd, 409, pending ? pending : "Restart the printer first: a saved change is waiting for a restart and the calibration would discard it");
        return;
    }
    double nozzle_target, bed, bed_target;
    if (!uds_value(&telemetry, U_EG, &nozzle_target) || !uds_value(&telemetry, U_BT, &bed) ||
        !uds_value(&telemetry, U_BG, &bed_target) || nozzle_target != 0 || bed_target != 0 || bed > 40) {
        plates_fail(fd, 409, "Switch the heaters off and let the bed cool to 40 C or less"); return;
    }
    size_t config_len = 0, before_len = 0, s, e; int section = 0, found = 0; nx_table t;
    char *config = plates_read_file(printer_config_path, CANVAS_UNLOAD_CONFIG_MAX, &config_len);
    if (config) nx_configured(config, config_len, &t, &section);
    free(config);
    if (!section) { plates_fail(fd, 409, "This printer's configuration has no nozzle temperature compensation"); return; }
    /* The firmware compensates against its reference temperature (140 C, where the start G-code homes and probes)
     * and uses a wrong constant at and outside the table's ends, so the reference must lie well inside the table. */
    if (t.ref > 0 && (start > t.ref - 5 || end < t.ref + 5)) {
        char message[160];
        snprintf(message, sizeof(message), "Start at %d C or lower: the printer measures nozzle growth from %.0f C, which must lie inside the table",
                 (int)(t.ref - 5) / 5 * 5, t.ref);
        plates_fail(fd, 400, message); return;
    }
    char *before = plates_read_file(printer_autosave_path, AUTOSAVE_FILE_MAX, &before_len);
    if (!before || nx_autosave(before, before_len, &t, &s, &e, &found) != 1) {
        free(before); plates_fail(fd, 409, "The printer's autosave.cfg has a layout CC2 Control does not edit"); return;
    }
    nx.start = start; nx.end = end; nx.step = step;
    if (!nx_journal_write()) { free(before); plates_fail(fd, 503, "Cannot record the calibration session; nothing was started"); return; }
    free(nx_before); nx_before = before; nx_before_len = before_len;
    memset(&nx.measured, 0, sizeof(nx.measured)); nx.slope = nx.residual = 0;
    nx.result = ""; nx.detail[0] = 0; nx.ok = 0;
    nx.stage = NX_RUNNING; nx.since = nx_clock(); nx.connections = telemetry.connections;
    nx.deadline = nx.since + nx_budget_ms(start, end, step);
    char command[128];
    snprintf(command, sizeof(command), "NOZZLE_TEMP_CALIBRATE START=%d END=%d DIFF=%d DELAY=%d SAMPLES=%d",
             start, end, step, NX_DELAY_S, NX_SAMPLES);
    nx_command(command);
    nx_state(fd, 202);
}

/* Body FACTORY: remove the saved table, so the vendor configuration's applies after a restart. */
static void nx_factory_response(int fd, const mqtt_client *mqtt, const char *body, size_t length) {
    if (length != 7 || memcmp(body, "FACTORY", 7)) { plates_fail(fd, 400, "Explicit confirmation required"); return; }
    if (nx_active()) { plates_fail(fd, 409, "A nozzle expansion calibration is running"); return; }
    const char *busy = plates_printer_ready(mqtt);
    if (busy) { plates_fail(fd, 409, busy); return; }
    size_t len = 0, new_len = 0, again_len = 0; char backup[PATH_MAX_LOCAL];
    char *file = plates_read_file(printer_autosave_path, AUTOSAVE_FILE_MAX, &len);
    char *updated = file ? nx_autosave_replace(file, len, NULL, 0, &new_len) : NULL;
    if (!updated) { free(file); plates_fail(fd, 409, "The printer's autosave.cfg has a layout CC2 Control does not edit"); return; }
    if (new_len == len && !memcmp(updated, file, len)) { free(file); free(updated); nx_state(fd, 200); return; }
    char *again = plates_read_file(printer_autosave_path, AUTOSAVE_FILE_MAX, &again_len);
    int unchanged = again && again_len == len && !memcmp(again, file, len);
    free(again); free(file);
    if (!unchanged) { free(updated); plates_fail(fd, 409, "The printer changed its configuration meanwhile; try again"); return; }
    int already = nx_pending();
    if (!nx_mark_pending()) { free(updated); plates_fail(fd, 500, "Cannot record the required printer restart; configuration unchanged"); return; }
    if (!autosave_backup_path(backup, sizeof(backup)) ||
        plates_replace_file(printer_autosave_path, backup, updated, new_len, autosave_mode()) != 0) {
        if (!already) unlink(nx_pending_path);
        free(updated); plates_fail(fd, 500, "Cannot write the printer configuration"); return;
    }
    free(updated);
    nx.result = ""; nx.detail[0] = 0; memset(&nx.measured, 0, sizeof(nx.measured));
    nx_state(fd, 200);
}

/* Body RESTART: reboot the printer so it reads the saved table, once it is still idle. */
static void nx_restart_response(int fd, mqtt_client *mqtt, const char *body, size_t length) {
    if (length != 7 || memcmp(body, "RESTART", 7)) { plates_fail(fd, 400, "Explicit restart confirmation required"); return; }
    if (!nx_pending()) { plates_fail(fd, 409, "No saved nozzle table is waiting for a restart"); return; }
    if (nx_active()) { plates_fail(fd, 409, "A nozzle expansion calibration is running"); return; }
    const char *busy = plates_printer_ready(mqtt);
    if (busy) { plates_fail(fd, 409, busy); return; }
    nx_restart_requested = 1; plates_mqtt = mqtt;
    reboot_guard = plates_reboot_guard;
    reboot_pending = 1; reboot_launched = 0; reboot_due = recovery_clock() + 2; reboot_error = "none";
    plates_reply(fd, 202, "{\"ok\":true,\"restarting\":true}\n");
}
#endif
