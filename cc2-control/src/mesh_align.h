/* Mesh-guided screw alignment: a session of bed mesh calibrations around turning the bed
 * screws. Each round is the calibration job of plates.h (home when needed, heat, hold the
 * bed for the soak time, BED_MESH_CALIBRATE PROFILE=<slot> BED_TEMP=<t>); the page fits a
 * plane through the mesh and tells what each screw has to do. No shorter probing command
 * is known for this firmware, so every round measures the full mesh, and the last round's
 * mesh is the one saved to the plate library at the end.
 *
 * Nothing is sent without the caller's confirmation: every request first answers with the
 * commands it would send ("confirm_required") and acts only when its last line is CONFIRM.
 *
 * The session owns the heaters from its first measurement on. However it ends without a
 * print taking over (explicit end, saved, a failed or cancelled measurement, emergency
 * stop, no request for SCREWS_SESSION_TIMEOUT_S), the deferred TURN_OFF_HEATERS of the
 * screw-tramming session is requested, which the main loop retries until it is delivered
 * to an idle printer. A print that starts meanwhile, or a bed target someone else set,
 * keeps its heat. Everything here runs in the main loop. */

#define ALIGN_ROUNDS_MAX 10
#define ALIGN_CONFIRM "CONFIRM"

static struct {
    int active;
    int round, rounds; /* measurements completed, and how many the session may run */
    int measuring;     /* the calibration job of this session runs */
    int measured;      /* the side slot holds the mesh of this session's last measurement */
    char side;
    int temp, soak;    /* soak in minutes */
    char nozzle[PLATE_ID_LEN + 1];
    time_t last;       /* the last accepted request or completed measurement */
    const char *ended; /* how the last session ended: "", "ended", "saved", "failed", "cancelled",
                        * "timeout", "print" or "estop" */
} align = {.ended = ""};

static int align_active(void) { return align.active; }

/* The bed target is someone else's: telemetry shows a target that is neither off nor the session's. */
static int align_bed_taken(void) {
    double target;
    return uds_value(&telemetry, U_BG, &target) && target > 0.5 && fabs(target - align.temp) > 0.5;
}

static void align_finish(const char *why, int heaters_off) {
    if (!align.active) return;
    if (heaters_off && !align_bed_taken()) control_screws_request_heaters_off();
    fprintf(stderr, "Mesh-guided screw alignment: %s after %d of %d measurement(s)\n", why, align.round, align.rounds);
    align.active = align.measuring = align.measured = 0;
    align.ended = why;
}

/* Main loop, after the calibration job's own tick. */
static void align_tick(const mqtt_client *mqtt, int emergency, time_t now) {
    if (!align.active) return;
    if (emergency) { align_finish("estop", 1); return; }
    /* A print owns the heaters now; control_screws_tick drops a pending shutdown as well. */
    if (mqtt->have_machine_status && mqtt->machine_status == 2) { align_finish("print", 0); return; }
    if (align.measuring) {
        if (plates_cal.stage) return; /* the job has its own deadlines */
        align.measuring = 0;
        if (!strcmp(plates_cal.result, "done")) {
            align.round++; align.measured = 1; align.last = now;
            return;
        }
        /* The job switched the bed off where it still owned it; the nozzle may still hold the
         * homing temperature. A printer that is busy with something else is left to it until idle. */
        align_finish(!strcmp(plates_cal.result, "cancelled") ? "cancelled" : "failed", 1);
        return;
    }
    if (now >= align.last && now - align.last > SCREWS_SESSION_TIMEOUT_S) align_finish("timeout", 1);
}

static void align_commands_begin(json_builder *b) { json_builder_printf(b, "{\"confirm_required\":true,\"commands\":["); }
static void align_command(json_builder *b, int *count, const char *format, ...) {
    char command[128]; va_list args;
    va_start(args, format); vsnprintf(command, sizeof(command), format, args); va_end(args);
    json_builder_printf(b, "%s", (*count)++ ? "," : "");
    json_builder_string(b, command);
}
static void align_reply_built(int fd, json_builder *b) {
    if (b->failed) plates_fail(fd, 500, "Alignment reply is too large");
    else respond(fd, 200, "OK", "application/json; charset=utf-8", b->data, b->length);
    free(b->data);
}

static int align_confirmed(char fields[][PLATE_NAME_MAX + 1], int count, int at) {
    return count == at + 1 && !strcmp(fields[at], ALIGN_CONFIRM);
}

/* GET /api/screws/align */
static void align_get_response(int fd) {
    static const char *stages[] = {"off", "homing", "heating", "soaking", "probing", "saving"};
    char body[640], detail[2 * sizeof(plates_cal.detail) + 8];
    double bed = 0; int have_bed = uds_value(&telemetry, U_BT, &bed);
    long long left = plates_cal.stage == CAL_SOAKING ? (plates_cal.soak_until - monotonic_ms() + 999) / 1000 : 0;
    long idle = align.active && !align.measuring ? (long)(align.last + SCREWS_SESSION_TIMEOUT_S - time(NULL)) : 0;
    char bed_text[24];
    if (have_bed) snprintf(bed_text, sizeof(bed_text), "%.1f", bed); else snprintf(bed_text, sizeof(bed_text), "null");
    json_escape(detail, sizeof(detail), plates_cal.detail);
    int n = snprintf(body, sizeof(body),
        "{\"active\":%s,\"round\":%d,\"rounds\":%d,\"measuring\":%s,\"measured\":%s,\"side\":\"%c\",\"temp\":%d,"
        "\"soak\":%d,\"nozzle\":\"%s\",\"idle_left\":%ld,\"ended\":\"%s\",\"heaters_off_pending\":%s,"
        "\"calibration\":{\"stage\":\"%s\",\"remaining\":%lld,\"bed\":%s,\"result\":\"%s\",\"error\":\"%s\",\"detail\":\"%s\"}}\n",
        align.active ? "true" : "false", align.round, align.rounds, align.measuring ? "true" : "false",
        align.measured ? "true" : "false", align.side ? align.side : 'A', align.temp, align.soak, align.nozzle,
        idle > 0 ? idle : 0, align.ended, control_screws_heaters_off_pending() ? "true" : "false",
        stages[plates_cal.stage], left > 0 ? left : 0, bed_text, plates_cal.result, plates_cal.error, detail);
    if (n < 0 || (size_t)n >= sizeof(body)) { plates_fail(fd, 500, "Alignment state is too large"); return; }
    plates_reply(fd, 200, body);
}

/* POST /api/screws/align/measure: side \n temp \n soak minutes \n nozzle \n rounds [\n CONFIRM].
 * Starts a session with its first measurement, or runs the next one of the running session, whose
 * parameters the request must repeat. Without CONFIRM it only lists the commands. */
static void align_measure_response(int fd, const mqtt_client *mqtt, console_state *console, const char *body,
                                   size_t length) {
    char fields[6][PLATE_NAME_MAX + 1] = {{0}}; int temp, soak, status; const char *reason;
    int count = plates_fields(body, length, fields, 6);
    size_t digits = strlen(fields[4]);
    int rounds = digits && digits <= 2 && strspn(fields[4], "0123456789") == digits ? atoi(fields[4]) : 0;
    if (count < 5 || !plates_calibrate_fields(fields, 4, &temp, &soak) || rounds < 1 || rounds > ALIGN_ROUNDS_MAX ||
        (count == 6 && strcmp(fields[5], ALIGN_CONFIRM))) {
        plates_fail(fd, 400, "Invalid alignment measurement"); return;
    }
    char side = fields[0][0];
    if (control_screws_session_active()) { plates_fail(fd, 409, "End the screw-tramming session first"); return; }
    if (atomic_load(&console->emergency_sent)) {
        plates_fail(fd, 409, "An emergency stop was sent; restart the printer first"); return;
    }
    if (align.active) {
        if (align.measuring) { plates_fail(fd, 409, "The measurement of this session is still running"); return; }
        if (side != align.side || temp != align.temp || soak != align.soak || strcmp(fields[3], align.nozzle)) {
            plates_fail(fd, 409, "The running alignment session uses other settings; end it first"); return;
        }
        if (align.round >= align.rounds) {
            plates_fail(fd, 409, "The session has used all its measurements; save the mesh or end the session"); return;
        }
    }
    if ((reason = plates_calibrate_check(mqtt, side, "", &status))) { plates_fail(fd, status, reason); return; }
    if (!align_confirmed(fields, count, 5)) {
        json_builder b = {malloc(1024), 0, 1024, 0}; int listed = 0;
        if (!b.data) { plates_fail(fd, 500, "Out of memory"); return; }
        align_commands_begin(&b);
        /* What plates_calibrate_start sends first: the profile of the measurement the slot holds now. */
        const plate_measure *held = plates_available && !plates.pending[0] ? plates_slot_measure(side) : NULL;
        if (held && !plates_has_profile(held)) {
            char name[32]; plate_profile_name(name, sizeof(name), held);
            align_command(&b, &listed, "BED_MESH_PROFILE LOAD=%s", plate_slot(side));
            align_command(&b, &listed, "BED_MESH_PROFILE SAVE=%s", name);
        }
        if (!plates_homed(mqtt)) align_command(&b, &listed, "G28");
        align_command(&b, &listed, "M140 S%d", temp);
        align_command(&b, &listed, "BED_MESH_CALIBRATE PROFILE=%s BED_TEMP=%d", plate_slot(side), temp);
        json_builder_printf(&b, "],\"round\":%d,\"rounds\":%d,\"soak\":%d}\n", align.active ? align.round + 1 : 1,
                            align.active ? align.rounds : rounds, soak);
        align_reply_built(fd, &b);
        return;
    }
    if ((reason = plates_calibrate_start(mqtt, console, side, temp, soak, fields[3], ""))) {
        plates_fail(fd, 409, reason); return; /* nothing was queued: the session, if any, waits as before */
    }
    if (!align.active) {
        memset(&align, 0, sizeof(align));
        align.active = 1; align.rounds = rounds; align.side = side; align.temp = temp; align.soak = soak;
        memcpy(align.nozzle, fields[3], sizeof(align.nozzle)); /* "" or a checked id */
    }
    align.ended = ""; align.measuring = 1; align.measured = 0; align.last = time(NULL);
    char reply[96];
    snprintf(reply, sizeof(reply), "{\"started\":true,\"round\":%d,\"rounds\":%d}\n", align.round + 1, align.rounds);
    plates_reply(fd, 202, reply);
}

/* POST /api/screws/align/save: plate [\n CONFIRM]. The mesh of the last measurement becomes the
 * plate's measurement through the plate library, and the session ends. */
static void align_save_response(int fd, const mqtt_client *mqtt, const char *body, size_t length) {
    char fields[2][PLATE_NAME_MAX + 1] = {{0}};
    int count = plates_fields(body, length, fields, 2), status = 409;
    if (count < 1 || !plate_id_valid(fields[0]) || (count == 2 && strcmp(fields[1], ALIGN_CONFIRM))) {
        plates_fail(fd, 400, "Invalid alignment save"); return;
    }
    if (!align.active) { plates_fail(fd, 409, "No alignment session is active"); return; }
    if (align.measuring || !align.measured) {
        plates_fail(fd, 409, "The session has no completed measurement to save"); return;
    }
    const char *reason = plates_closed(&status);
    if (reason) { plates_fail(fd, status, reason); return; }
    const plate_entry *p = plate_find(fields[0]);
    if (!p) { plates_fail(fd, 404, "Unknown plate"); return; }
    if (p->side != align.side) { plates_fail(fd, 409, "The plate is on the other side"); return; }
    if (!align_confirmed(fields, count, 1)) {
        json_builder b = {malloc(1024), 0, 1024, 0}; int listed = 0;
        if (!b.data) { plates_fail(fd, 500, "Out of memory"); return; }
        align_commands_begin(&b);
        const plate_measure *same = NULL;
        for (int i = 0; i < plates.measure_count && !same; ++i) {
            const plate_measure *o = &plates.measures[i];
            if (!strcmp(o->plate, p->id) && o->temp == align.temp && !strcmp(o->nozzle, align.nozzle)) same = o;
        }
        align_command(&b, &listed, "BED_MESH_PROFILE LOAD=%s", plate_slot(align.side));
        /* A new measurement gets its id when it is created. */
        align_command(&b, &listed, "BED_MESH_PROFILE SAVE=" PLATE_PROFILE_PREFIX "%s", same ? same->id : "<id>");
        align_command(&b, &listed, "SET_GCODE_OFFSET Z=%.3f", plate_z_now(p));
        align_command(&b, &listed, "TURN_OFF_HEATERS");
        json_builder_printf(&b, "],\"replaces\":%s,\"temp\":%d}\n", same ? "true" : "false", align.temp);
        align_reply_built(fd, &b);
        return;
    }
    int profile = 0, applied = 0; plate_measure *m = NULL;
    reason = plates_take_measure(mqtt, p->id, align.temp, align.nozzle, 0, &status, &m, &profile, &applied);
    if (reason) { plates_fail(fd, status, reason); return; } /* the session stays, so the save can be repeated */
    char reply[160];
    snprintf(reply, sizeof(reply), "{\"saved\":true,\"measure\":\"%s\",\"profile\":%s,\"applied\":%s}\n", m->id,
             profile ? "true" : "false", applied ? "true" : "false");
    align_finish("saved", 1);
    plates_reply(fd, 200, reply);
}

/* POST /api/screws/align/end: [CONFIRM]. Ends the session, a measurement that has not begun probing
 * included, and asks for the heaters to be switched off. */
static void align_end_response(int fd, const mqtt_client *mqtt, const char *body, size_t length) {
    char fields[1][PLATE_NAME_MAX + 1] = {{0}};
    int count = plates_fields(body, length, fields, 1);
    if (count < 0 || (count == 1 && strcmp(fields[0], ALIGN_CONFIRM))) { plates_fail(fd, 400, "Invalid alignment end"); return; }
    if (!align.active) { plates_reply(fd, 200, "{\"ended\":true,\"active\":false}\n"); return; }
    if (align.measuring && (plates_cal.stage == CAL_PROBING || plates_cal.stage == CAL_SAVING)) {
        plates_fail(fd, 409, "The probing has started; use the emergency stop to interrupt it"); return;
    }
    if (count != 1) {
        json_builder b = {malloc(512), 0, 512, 0}; int listed = 0;
        if (!b.data) { plates_fail(fd, 500, "Out of memory"); return; }
        align_commands_begin(&b);
        if (align.measuring && plates_cal.stage && plates_cal_owns_bed(mqtt)) align_command(&b, &listed, "M140 S0");
        align_command(&b, &listed, "TURN_OFF_HEATERS");
        json_builder_printf(&b, "],\"measuring\":%s}\n", align.measuring ? "true" : "false");
        align_reply_built(fd, &b);
        return;
    }
    if (align.measuring && plates_cal.stage) plates_cal_end(mqtt, "cancelled", "", "", 1);
    align_finish("ended", 1);
    plates_reply(fd, 200, "{\"ended\":true,\"active\":false}\n");
}

/* POST /api/plates/calibrate while a session is active: its measurements go through the session, so a
 * calibration started beside it would replace the mesh the session is about to save. */
static void align_plates_calibrate_response(int fd, const mqtt_client *mqtt, console_state *console,
                                            const char *body, size_t length) {
    if (align.active) plates_fail(fd, 409, "End the mesh-guided alignment session first");
    else plates_calibrate_response(fd, mqtt, console, body, length);
}
