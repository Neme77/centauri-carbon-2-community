#define main cc2_main_original
#define mqtt_start_print mock_mqtt_start_print
#define mqtt_prepare_timelapse mock_mqtt_prepare_timelapse
#include "../src/main.c"
#undef main
#undef mqtt_start_print
#undef mqtt_prepare_timelapse
#include <assert.h>

/* How the print queue follows a copy from the start request to its end, from the MQTT cache and
 * the UDS telemetry alone; what it decides on stale, foreign or missing evidence; the guards of its
 * HTTP handlers while a copy runs; and the automatic policy, with the MQTT start mocked away. */
static mqtt_client machine;
static int starts_sent;
static char start_file[PATH_MAX_LOCAL];
static int start_trays[GCODE_TOOLS_MAX]; /* the Canvas tray of each mapped tool in the last start */
static size_t start_count;

int mock_mqtt_start_print(mqtt_client *c, const char *media, const char *filename, const int *tools, const int *trays,
                          size_t count, char layout, int bedlevel_force, int timelapse) {
    (void)c; (void)media; (void)tools; (void)layout; (void)bedlevel_force; (void)timelapse;
    starts_sent++;
    snprintf(start_file, sizeof(start_file), "%s", filename);
    start_count = count;
    for (size_t i = 0; i < count && i < GCODE_TOOLS_MAX; ++i) start_trays[i] = trays[i];
    return 0;
}
int mock_mqtt_prepare_timelapse(mqtt_client *c, int enabled) { (void)c; (void)enabled; return -1; }
static int fake_uds(const char *request, char **reply, size_t *length) {
    (void)request;
    *reply = strdup("{\"id\":1001,\"result\":{}}"); *length = strlen(*reply);
    return *reply ? 0 : -1;
}

static void printer_says(int status, const char *mqtt_state, const char *file, const char *uuid) {
    machine.connected = machine.registered = machine.have_machine_status = 1;
    machine.machine_status = status;
    snprintf(machine.print_state, sizeof(machine.print_state), "%s", mqtt_state);
    snprintf(machine.filename, sizeof(machine.filename), "%s", file);
    snprintf(machine.uuid, sizeof(machine.uuid), "%s", uuid);
    machine.last_message = time(NULL);
}

static void telemetry_says(const char *state, const char *file) {
    telemetry.fd = 99; telemetry.ready = 1; telemetry.have_print_state = 1;
    snprintf(telemetry.print_state, sizeof(telemetry.print_state), "%s", state);
    telemetry.have_filename = file != NULL;
    if (file) snprintf(telemetry.filename, sizeof(telemetry.filename), "%s", file);
    telemetry.present = 0;
    clock_gettime(CLOCK_MONOTONIC, &telemetry.last_rx);
}
static void telemetry_stale(void) { telemetry.last_rx.tv_sec -= 30; }
static void telemetry_heating(double bed_target) { telemetry.present |= 1u << U_BG; telemetry.values[U_BG] = bed_target; }
/* The Canvas telemetry in the MQTT cache: one tray, as the printer's method 2005 reports it. Each call is a
 * new report, as mqtt.c counts them. */
static void canvas_says(int tray, const char *type, int status) {
    machine.canvas_uds_ready = 0; machine.canvas_revision++;
    machine.canvas_snapshot_len = (size_t)snprintf(machine.canvas_snapshot, sizeof(machine.canvas_snapshot),
        "{\"id\":2005,\"result\":{\"canvas_info\":{\"canvas_list\":[{\"canvas_id\":0,\"connected\":1,\"tray_list\":["
        "{\"tray_id\":%d,\"status\":%d,\"filament_type\":\"%s\"}]}]}}}", tray, status, type);
}
/* Several trays with their labels: status 0 is an empty tray, which keeps the label of the filament it held. */
#define TRAY_NAMED(id, status, type, name, color) \
    "{\"tray_id\":" #id ",\"status\":" #status ",\"filament_type\":\"" type "\",\"filament_name\":\"" name "\",\"filament_color\":\"" color "\"}"
#define TRAY(id, status, type, color) TRAY_NAMED(id, status, type, type, color)
static void canvas_list(const char *trays) {
    machine.canvas_uds_ready = 0; machine.canvas_revision++;
    machine.canvas_snapshot_len = (size_t)snprintf(machine.canvas_snapshot, sizeof(machine.canvas_snapshot),
        "{\"id\":2005,\"result\":{\"canvas_info\":{\"canvas_list\":[{\"canvas_id\":0,\"connected\":1,\"tray_list\":[%s]}]}}}", trays);
}

static queue_item *job(int id, int copies, int done) {
    queue_item *it = &queue.items[queue.count++];
    memset(it, 0, sizeof(*it));
    it->id = id; snprintf(it->storage, sizeof(it->storage), "internal");
    snprintf(it->path, sizeof(it->path), "part.gcode"); it->side = 'A';
    snprintf(it->leveling, sizeof(it->leveling), "saved");
    it->copies = copies; it->done = done; it->estimated_seconds = -1; it->filament_grams = -1; it->duration_baseline = -1;
    snprintf(it->state, sizeof(it->state), "pending");
    if (queue.next_id <= id) queue.next_id = id + 1;
    return it;
}

/* What queue_start_item records when the printer accepts a start. */
static void requested(queue_item *it) {
    double value;
    snprintf(it->state, sizeof(it->state), "starting");
    snprintf(it->print_name, sizeof(it->print_name), "%s", it->path);
    snprintf(it->request_uuid, sizeof(it->request_uuid), "%s", machine.uuid);
    it->started_ms = it->requested_ms = monotonic_ms(); it->mismatch_ms = 0;
    it->job_uuid[0] = 0; it->native_seen = 0;
    it->duration_baseline = uds_value(&telemetry, U_TOTAL_DURATION, &value) ? value : -1;
    it->bed_baseline = uds_value(&telemetry, U_BG, &value) ? value : 0;
    it->nozzle_baseline = uds_value(&telemetry, U_EG, &value) ? value : 0;
    queue.active_id = it->id;
}

/* The printer runs the copy: MQTT and the printer service both show the file printing, as a new job. */
static void copy_starts(queue_item *it, const char *uuid) {
    requested(it);
    printer_says(2, "printing", "part.gcode", uuid); telemetry_says("printing", "local/part.gcode");
    queue_tick(&machine);
    assert(!strcmp(it->state, "printing") && !strcmp(it->job_uuid, uuid) && it->native_seen);
}

/* HTTP handlers through a socket pair: the status line of the reply, and its body. */
static int pair_[2];
static char reply_body[16384];
static int begin(void) { assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair_)); return pair_[0]; }
static int finish(void) {
    close(pair_[0]);
    size_t used = 0; ssize_t n;
    while (used + 1 < sizeof(reply_body) && (n = recv(pair_[1], reply_body + used, sizeof(reply_body) - 1 - used, 0)) > 0) used += (size_t)n;
    close(pair_[1]); reply_body[used] = 0;
    assert(used > 0);
    return atoi(strchr(reply_body, ' ') + 1);
}
/* What GET /api/queue reports for one tool of a job: the filament it is bound to (empty texts: not known yet),
 * the slot the job was added with (null: added for the filament alone), the slot a start would take (null: no
 * slot holds the filament) and what was found ("", "empty", "other", "absent" or "unknown"). */
#define FEED(tool, type, name, color, slot, use, reason) \
    "{\"tool\":" #tool ",\"type\":\"" type "\",\"name\":\"" name "\",\"color\":\"" color "\",\"slot\":" #slot ",\"use\":" #use ",\"reason\":\"" reason "\"}"
#define BLIND(tool, slot, use, reason) FEED(tool, "", "", "", slot, use, reason)
#define RED(tool, slot, use, reason) FEED(tool, "PLA", "PLA", "#FF0000", slot, use, reason)
#define PRO(tool, slot, use, reason) FEED(tool, "PETG", "PETG Pro", "#AEADAC", slot, use, reason)
#define MATTE(tool, slot, use, reason) FEED(tool, "PLA", "PLA Matte", "#F72221", slot, use, reason)
static int queue_reports(const char *part) {
    queue_get_response(begin(), &machine);
    return finish() == 200 && strstr(reply_body, part) != NULL;
}
/* A job's tools on Canvas slots, as an add request maps them ("tool:slot,..."): no filament known yet. */
static void feed(queue_item *it, const char *mapping) { it->tools = 0; assert(queue_mapping_tools(it, mapping)); }
static int holds(queue_item *it, int tool, const char *type, const char *name, const char *color) {
    const queue_tool *t = queue_tool_of(it, tool);
    return t && !strcmp(t->filament.type, type) && !strcmp(t->filament.name, name) && !strcmp(t->filament.color, color);
}
/* POST /api/queue/filament for a job: the status of the reply. */
static int change(int id, const char *rest) {
    char body[256];
    snprintf(body, sizeof(body), "%d\n%s", id, rest);
    queue_filament_response(begin(), &machine, body, strlen(body));
    return finish();
}
static int add(const char *body) {
    queue_add_response(begin(), &machine, body, strlen(body));
    return finish();
}
static int stored_has(const char *needle) {
    static char text[65536];
    FILE *in = fopen(queue_path, "r");
    assert(in);
    size_t n = fread(text, 1, sizeof(text) - 1, in);
    fclose(in); text[n] = 0;
    return strstr(text, needle) != NULL;
}
/* A stored queue with one job, as a build of some version left it: whether it loads. */
static int loads(int version, const char *mapping, const char *filaments) {
    FILE *out = fopen(queue_path, "w");
    assert(out);
    fprintf(out, "{\"version\":%d,\"next_id\":41,\"policy\":{\"after\":\"wait\",\"delay\":60,\"remove_done\":false},\"items\":[{\"id\":40,"
        "\"storage\":\"internal\",\"path\":\"part.gcode\",\"mapping\":\"%s\",\"side\":\"A\",\"leveling\":\"saved\",\"timelapse\":false,"
        "\"measure\":\"\",\"nozzle\":\"\",\"copies\":2,\"done\":1,\"estimated_seconds\":-1,\"filament_grams\":-1,\"added\":1,"
        "\"state\":\"pending\",\"last_result\":\"\",\"filaments\":[%s],"
        "\"print_name\":\"\",\"native_seen\":false,\"job_uuid\":\"\",\"request_uuid\":\"\"}]}\n", version, mapping, filaments);
    assert(fclose(out) == 0);
    queue_load();
    return queue_available && queue.count == 1;
}

int main(void) {
    char dir[] = "/tmp/cc2-queue-XXXXXX";
    assert(mkdtemp(dir));
    static char file[640], internal[640], gcode[700], plain[700], duo[700], autosave[640];
    snprintf(file, sizeof(file), "%s/print-queue.json", dir); queue_path = file;
    snprintf(internal, sizeof(internal), "%s/internal", dir); assert(mkdir(internal, 0755) == 0); gcode_internal_root = internal;
    snprintf(gcode, sizeof(gcode), "%s/part.gcode", internal);
    FILE *out = fopen(gcode, "w"); assert(out && fputs("; filament_type = PLA\nG28\nG1 X10 Y10 E1\n", out) >= 0 && fclose(out) == 0);
    snprintf(plain, sizeof(plain), "%s/plain.gcode", internal); /* a file that names no material */
    out = fopen(plain, "w"); assert(out && fputs("G28\nG1 X10 Y10 E1\n", out) >= 0 && fclose(out) == 0);
    snprintf(duo, sizeof(duo), "%s/duo.gcode", internal); /* a file for two tools */
    out = fopen(duo, "w");
    assert(out && fputs("; filament_type = PLA;PLA\nG28\nT0\nG1 X10 Y10 E1\nT1\nG1 X20 Y20 E1\n", out) >= 0 && fclose(out) == 0);
    snprintf(autosave, sizeof(autosave), "%s/autosave.cfg", dir); printer_autosave_path = autosave;
    out = fopen(autosave, "w"); assert(out && fputs("[bed_mesh default]\nx_count = 11\ny_count = 11\npoints =\n  0, 0\n", out) >= 0 && fclose(out) == 0);
    canvas_color_table_uds = fake_uds;
    memset(&machine, 0, sizeof(machine)); uds_init(&telemetry);
    queue_load();
    assert(queue_available && queue.count == 0 && !strcmp(queue.after, "wait") && queue.remove_done);
    queue.remove_done = 0; /* finished jobs stay listed until the section that tests removal */

    /* A copy: start request, the printer runs the file as a new job, the printer finishes it. */
    queue_item *it = job(1, 2, 0);
    assert(queue_save() == 0);
    copy_starts(it, "u1");
    printer_says(1, "complete", "", "u1"); telemetry_says("complete", "local/part.gcode");
    queue_tick(&machine);
    assert(it->done == 1 && !strcmp(it->state, "pending") && queue.active_id == 0 && !it->last_result[0]);
    assert(queue.complete_ms == 0); /* counted under "wait": no automatic start pending */
    queue_load(); it = &queue.items[0]; /* persisted as counted, with its unknown estimates */
    assert(queue_available && queue.count == 1 && it->done == 1 && !strcmp(it->state, "pending") && queue.active_id == 0 && !queue.remove_done);
    assert(it->estimated_seconds == -1 && it->filament_grams < 0);

    /* A cancelled copy is not counted; the job stays first with a note. */
    copy_starts(it, "u2");
    printer_says(1, "cancelled", "", "u2"); telemetry_says("cancelled", "local/part.gcode");
    queue_tick(&machine);
    assert(it->done == 1 && !strcmp(it->state, "pending") && !strcmp(it->last_result, "stopped") && queue.active_id == 0);

    /* A start the printer never reported running: idle for the grace period. */
    requested(it); it->started_ms = monotonic_ms() - QUEUE_START_GRACE_MS - 1;
    printer_says(1, "standby", "", "u2"); telemetry_says("standby", NULL);
    queue_tick(&machine);
    assert(!strcmp(it->state, "pending") && !strcmp(it->last_result, "not_started") && queue.active_id == 0);

    /* A calibrated start keeps the printer busy for minutes before the file prints: the wait goes on
     * while the printer is not idle or heats beyond what it was heating, and only idle time counts. */
    requested(it); it->started_ms = monotonic_ms() - QUEUE_START_GRACE_MS - 1;
    printer_says(5, "standby", "", "u2"); telemetry_says("standby", NULL);
    queue_tick(&machine);
    assert(!strcmp(it->state, "starting") && monotonic_ms() - it->started_ms < 1000);
    it->started_ms = monotonic_ms() - QUEUE_START_GRACE_MS - 1;
    printer_says(1, "standby", "", "u2"); telemetry_says("standby", NULL); telemetry_heating(60);
    queue_tick(&machine);
    assert(!strcmp(it->state, "starting") && monotonic_ms() - it->started_ms < 1000);
    printer_says(2, "printing", "part.gcode", "u3"); telemetry_says("printing", "local/part.gcode");
    queue_tick(&machine);
    assert(!strcmp(it->state, "printing") && it->native_seen && !strcmp(it->job_uuid, "u3"));
    printer_says(1, "complete", "", "u3"); telemetry_says("complete", "local/part.gcode");
    queue_tick(&machine);
    assert(it->done == 2 && !strcmp(it->state, "done") && queue.count == 1 && queue.active_id == 0);
    it->copies = 5; snprintf(it->state, sizeof(it->state), "pending");
    /* A bed kept warm before the request is not the start script: the idle grace still runs out. */
    printer_says(1, "complete", "", "u3"); telemetry_says("complete", NULL); telemetry_heating(60);
    requested(it); it->started_ms = monotonic_ms() - QUEUE_START_GRACE_MS - 1;
    queue_tick(&machine);
    assert(!strcmp(it->state, "pending") && !strcmp(it->last_result, "not_started") && queue.active_id == 0);
    /* ...but a start request this old is given up whatever the printer shows. */
    requested(it); it->requested_ms = monotonic_ms() - QUEUE_START_CAP_MS - 1;
    printer_says(5, "standby", "", "u3"); telemetry_says("standby", NULL);
    queue_tick(&machine);
    assert(!strcmp(it->state, "pending") && !strcmp(it->last_result, "not_started") && queue.active_id == 0);
    /* The printer running another file for a while means our start did not happen; one tick does not. */
    requested(it);
    printer_says(2, "printing", "other.gcode", "u4"); telemetry_says("printing", "local/other.gcode");
    queue_tick(&machine);
    assert(!strcmp(it->state, "starting") && it->mismatch_ms > 0);
    it->mismatch_ms -= QUEUE_FOREIGN_MS + 1;
    queue_tick(&machine);
    assert(!strcmp(it->state, "pending") && !strcmp(it->last_result, "not_started") && queue.active_id == 0);
    /* MQTT reports our file before the printer service has moved on from the previous one: the copy starts. */
    requested(it);
    printer_says(2, "printing", "part.gcode", "u5"); telemetry_says("complete", "local/previous.gcode");
    queue_tick(&machine);
    assert(!strcmp(it->state, "printing") && it->native_seen); /* the new MQTT job uuid is proof enough */

    /* Another file completing is not this job's copy; it ends uncounted once the printer stays idle. */
    it->started_ms = monotonic_ms() - QUEUE_IDLE_GRACE_MS - 1;
    printer_says(1, "complete", "other.gcode", "u5"); telemetry_says("complete", "local/other.gcode");
    queue_tick(&machine);
    assert(it->done == 2 && !strcmp(it->last_result, "stopped") && queue.active_id == 0);

    /* Idle again without "complete" for a minute: the copy ended otherwise. */
    copy_starts(it, "u6"); it->started_ms = monotonic_ms() - QUEUE_IDLE_GRACE_MS - 1;
    printer_says(1, "standby", "", "u6"); telemetry_says("standby", NULL);
    queue_tick(&machine);
    assert(!strcmp(it->state, "pending") && !strcmp(it->last_result, "stopped") && it->done == 2);

    /* A "complete" left over from the previous copy of the same file, with no new job: MQTT still carries
     * the previous job's uuid and the printer service is silent, so the copy is never seen running. */
    requested(it); /* request_uuid is u6, the job before */
    printer_says(2, "printing", "part.gcode", "u6"); telemetry_says("complete", "local/part.gcode"); telemetry_stale();
    queue_tick(&machine);
    assert(!strcmp(it->state, "printing") && !it->native_seen && !it->job_uuid[0]);
    printer_says(1, "complete", "", "u6"); telemetry_stale();
    queue_tick(&machine);
    assert(!strcmp(it->state, "printing") && it->done == 2);
    it->started_ms = monotonic_ms() - QUEUE_IDLE_GRACE_MS - 1;
    queue_tick(&machine);
    assert(!strcmp(it->state, "pending") && !strcmp(it->last_result, "stopped") && it->done == 2);
    /* The printer service down for the whole copy: MQTT's new job uuid is this copy's proof. */
    requested(it);
    printer_says(2, "printing", "part.gcode", "u7"); telemetry_stale();
    queue_tick(&machine);
    assert(!strcmp(it->state, "printing") && it->native_seen && !strcmp(it->job_uuid, "u7"));
    printer_says(1, "complete", "", "u7"); telemetry_stale();
    queue_tick(&machine);
    assert(it->done == 3 && !strcmp(it->state, "pending") && !it->last_result[0]);

    /* A pause, a filament load or a recovery is not the end of the copy, whatever the machine status. */
    copy_starts(it, "u8"); it->started_ms = monotonic_ms() - QUEUE_IDLE_GRACE_MS - 1;
    printer_says(3, "paused", "part.gcode", "u8"); telemetry_says("paused", "local/part.gcode");
    queue_tick(&machine);
    assert(!strcmp(it->state, "printing") && monotonic_ms() - it->started_ms < 1000);
    printer_says(1, "complete", "", "u8"); telemetry_says("complete", "local/part.gcode");
    queue_tick(&machine);
    assert(it->done == 4 && !strcmp(it->state, "pending"));
    /* Another job taking over while ours runs: ours is over, not counted. */
    copy_starts(it, "u9");
    printer_says(2, "printing", "part.gcode", "u10"); telemetry_says("printing", "local/part.gcode");
    queue_tick(&machine);
    assert(!strcmp(it->state, "pending") && !strcmp(it->last_result, "stopped") && it->done == 4 && queue.active_id == 0);

    /* Stale MQTT decides nothing, in either direction. */
    copy_starts(it, "u11");
    printer_says(1, "complete", "", "u11"); machine.last_message = time(NULL) - 60; telemetry_says("complete", "local/part.gcode");
    queue_tick(&machine);
    assert(!strcmp(it->state, "printing") && queue.active_id == 1 && it->done == 4);
    machine.last_message = time(NULL);
    queue_tick(&machine);
    assert(it->done == 5 && !strcmp(it->state, "done") && queue.count == 1 && queue.active_id == 0);

    /* The last copy with remove_done drops the job; so does lowering the copies to the printed count. */
    queue.count = 0; it = job(2, 1, 0); queue.remove_done = 1;
    copy_starts(it, "u12");
    printer_says(1, "complete", "part.gcode", "u12"); telemetry_says("complete", NULL);
    queue_tick(&machine);
    assert(queue.count == 0 && queue.active_id == 0);
    it = job(3, 3, 1); queue_available = 1;
    queue_copies_response(begin(), "3\n1", 3); assert(finish() == 200);
    assert(queue.count == 0);

    /* A USB job prints as the copy gcode_import_usb() made in internal memory; the start records that name. */
    it = job(4, 1, 0); snprintf(it->storage, sizeof(it->storage), "usb"); snprintf(it->path, sizeof(it->path), "stick/part.gcode");
    requested(it); snprintf(it->print_name, sizeof(it->print_name), "CC2_USB_0badf00d_part.gcode");
    printer_says(2, "printing", "CC2_USB_0badf00d_part.gcode", "u13"); telemetry_says("printing", "local/CC2_USB_0badf00d_part.gcode");
    queue_tick(&machine);
    assert(!strcmp(it->state, "printing") && it->native_seen);
    printer_says(1, "complete", "", "u13"); telemetry_says("complete", "local/CC2_USB_0badf00d_part.gcode");
    queue_tick(&machine);
    assert(queue.count == 0 && queue.active_id == 0); /* counted: the last copy, removed */
    it = job(5, 1, 0); requested(it);
    printer_says(2, "printing", "CC2_USB_0badf00d_part.gcode", "u14"); telemetry_says("printing", "local/CC2_USB_0badf00d_part.gcode");
    queue_tick(&machine); it->mismatch_ms -= QUEUE_FOREIGN_MS + 1; queue_tick(&machine);
    assert(!strcmp(it->state, "pending") && !strcmp(it->last_result, "not_started")); /* an internal job is not the import */
    queue.count = 0;

    /* Changing the policy never starts anything: a copy counted under "wait" leaves no pending delay,
     * and switching to "auto" afterwards does not create one. */
    it = job(6, 3, 0); queue.remove_done = 0;
    copy_starts(it, "u15");
    printer_says(1, "complete", "", "u15"); telemetry_says("complete", "local/part.gcode");
    queue_tick(&machine);
    assert(it->done == 1 && queue.complete_ms == 0);
    queue_policy_response(begin(), "auto\n10\n0", 10); assert(finish() == 200);
    assert(!strcmp(queue.after, "auto") && queue.complete_ms == 0);
    queue_tick(&machine);
    assert(queue.active_id == 0 && starts_sent == 0 && !it->last_result[0]);
    /* Under "auto" a counted copy arms the delay; a print the operator starts by hand disarms it. The
     * automatic start will judge the Canvas trays with nobody watching them: the queue asks for a report at
     * once, notes the ones it had by then, and keeps the Canvas watched while the delay runs. */
    copy_starts(it, "u16");
    machine.canvas_revision = 7; machine.canvas_refresh_due = 0; machine.canvas_watch_until = 0;
    printer_says(1, "complete", "", "u16"); telemetry_says("complete", "local/part.gcode");
    queue_tick(&machine);
    assert(it->done == 2 && queue.complete_ms > 0);
    assert(machine.canvas_refresh_due == 1 && queue.complete_canvas == 7 && machine.canvas_watch_until > time(NULL));
    machine.canvas_refresh_due = 0;
    printer_says(2, "printing", "manual.gcode", "m1"); telemetry_says("printing", "local/manual.gcode");
    queue_tick(&machine);
    assert(queue.complete_ms == 0 && queue.active_id == 0 && !strcmp(it->state, "pending"));
    /* While the delay runs, the queue reports the seconds left and a hold cancels the start. */
    queue.complete_ms = monotonic_ms();
    queue_get_response(begin(), &machine); assert(finish() == 200 && strstr(reply_body, "\"auto_in\":10"));
    queue_hold_response(begin()); assert(finish() == 200 && queue.complete_ms == 0);
    queue_get_response(begin(), &machine); assert(finish() == 200 && strstr(reply_body, "\"auto_in\":null"));
    /* A changed delay under "auto" also disarms it. */
    queue.complete_ms = monotonic_ms();
    queue_policy_response(begin(), "auto\n20\n0", 10); assert(finish() == 200);
    assert(queue.complete_ms == 0);
    queue_policy_response(begin(), "auto\n20\n1", 10); assert(finish() == 200); /* only remove_done: untouched */
    queue.remove_done = 0;

    /* The automatic start goes through the protected print path after the delay, once; the running
     * copy is then followed like any other. */
    queue.delay = 10; queue.complete_ms = monotonic_ms() - 5000;
    printer_says(1, "complete", "", "u16"); telemetry_says("complete", NULL);
    queue_tick(&machine);
    assert(queue.complete_ms != 0 && starts_sent == 0); /* not before the delay */
    queue.complete_ms = monotonic_ms() - 11000;
    queue_tick(&machine);
    assert(starts_sent == 1 && !strcmp(start_file, "part.gcode") && !strcmp(it->state, "starting") && queue.active_id == 6);
    assert(!strcmp(it->print_name, "part.gcode") && !strcmp(it->request_uuid, "u16") && queue.complete_ms == 0 && !it->last_result[0]);
    queue_load(); it = &queue.items[0]; /* the start was saved before the request went out */
    assert(queue.count == 1 && !strcmp(it->state, "starting") && queue.active_id == 6);
    queue_tick(&machine);
    assert(starts_sent == 1 && !strcmp(it->state, "starting")); /* no second start while it waits */
    printer_says(2, "printing", "part.gcode", "u17"); telemetry_says("printing", "local/part.gcode");
    queue_tick(&machine);
    assert(!strcmp(it->state, "printing"));
    printer_says(1, "complete", "", "u17"); telemetry_says("complete", "local/part.gcode");
    queue_tick(&machine);
    assert(it->done == 3 && !strcmp(it->state, "done") && queue.active_id == 0 && queue.count == 1);
    /* An exclusive session (here a native calibration tool) refuses the automatic start; the refusal is recorded. */
    queue.count = 0; it = job(7, 2, 0);
    queue.complete_ms = monotonic_ms() - 11000; tools.busy = 1;
    printer_says(1, "complete", "", "u17"); telemetry_says("complete", NULL);
    queue_tick(&machine);
    assert(queue.complete_ms == 0 && !strcmp(it->last_result, "auto_failed") && queue.active_id == 0 && starts_sent == 1);
    tools.busy = 0; it->last_result[0] = 0;
    /* Without a printer link the print path refuses; the refusal is recorded, not retried. */
    queue.complete_ms = monotonic_ms() - 11000;
    machine.registered = 0;
    queue_tick(&machine);
    assert(queue.complete_ms == 0 && !strcmp(it->last_result, "auto_failed") && queue.active_id == 0 && starts_sent == 1);
    assert(!strcmp(it->state, "pending") && !it->print_name[0]); /* the refused start left no trace */
    machine.registered = 1;
    /* The "wait" policy never starts. */
    snprintf(queue.after, sizeof(queue.after), "wait"); queue.complete_ms = monotonic_ms() - 11000; it->last_result[0] = 0;
    queue_tick(&machine);
    assert(!it->last_result[0] && queue.active_id == 0 && starts_sent == 1);

    /* The operator can give up a start the printer never ran, while it is idle; not a running copy. */
    requested(it);
    printer_says(2, "printing", "", "u17"); telemetry_says("printing", NULL);
    queue_abandon_response(begin(), &machine, "", 0); assert(finish() == 409);
    printer_says(1, "standby", "", "u17"); telemetry_says("standby", NULL);
    queue_abandon_response(begin(), &machine, "99", 2); assert(finish() == 404);
    queue_abandon_response(begin(), &machine, "", 0); assert(finish() == 200);
    assert(!strcmp(it->state, "pending") && !strcmp(it->last_result, "not_started") && queue.active_id == 0);
    queue_abandon_response(begin(), &machine, "", 0); assert(finish() == 409);

    /* The HTTP handlers while a copy runs: nothing else starts, the running job stays, its copies keep one
     * for the running print, and "next" queues behind it. */
    copy_starts(it, "u18"); /* job 7, copies 2, done 0, printing */
    assert(queue.active_id == 7);
    queue_start_response(begin(), &machine, "", 0); assert(finish() == 409 && strstr(reply_body, "already running"));
    queue_start_response(begin(), &machine, "7", 1); assert(finish() == 409);
    queue_remove_response(begin(), "7", 1); assert(finish() == 409 && queue.count == 1);
    queue_copies_response(begin(), "7\n0", 3); assert(finish() == 400);
    it->done = 1;
    queue_copies_response(begin(), "7\n1", 3); assert(finish() == 409 && strstr(reply_body, "keep one copy more"));
    queue_copies_response(begin(), "7\n2", 3); assert(finish() == 200 && !strcmp(it->state, "printing") && it->copies == 2);
    static const char add_next[] = "internal\npart.gcode\n\nA\nsaved\n0\n\n\n1\nnext";
    queue_add_response(begin(), &machine, add_next, strlen(add_next)); assert(finish() == 201);
    assert(queue.count == 2 && queue.items[0].id == 7 && queue.items[1].id == 8 && !strcmp(queue.items[1].state, "pending"));
    static const char add_bad[] = "internal\npart.gcode\n\nA\nsaved\n0\n\nx\"y\n1";
    queue_add_response(begin(), &machine, add_bad, strlen(add_bad)); assert(finish() == 400); /* not a nozzle id */
    static const char add_measure[] = "internal\npart.gcode\n\nA\nsaved\n0\n0123456789abcdef\n\n1";
    queue_add_response(begin(), &machine, add_measure, strlen(add_measure)); assert(finish() == 201);
    queue_get_response(begin(), &machine); assert(finish() == 200);
    /* `next` names the first job that could start; the browser hides it behind `active`. */
    assert(strstr(reply_body, "\"active\":7") && strstr(reply_body, "\"measure\":\"0123456789abcdef\"") && strstr(reply_body, "\"next\":8"));
    assert(queue_save() == 0);

    /* A loaded file with a running copy keeps following it, with the uuids of its run; two running copies are refused. */
    queue_load();
    assert(queue_available && queue.count == 3 && queue.active_id == 7 && !strcmp(queue.items[0].state, "printing"));
    assert(!strcmp(queue.items[0].request_uuid, "u17") && !strcmp(queue.items[0].job_uuid, "u18") && queue.items[0].native_seen);
    assert(!strcmp(queue.items[2].measure, "0123456789abcdef"));
    snprintf(queue.items[1].state, sizeof(queue.items[1].state), "printing"); assert(queue_save() == 0);
    queue_load();
    assert(!queue_available && queue.count == 0);

    /* A restart while a start waits: the printer still reports the previous job's uuid, which must not pass for
     * this copy's own after the reload; the copy is recognised by the uuid that follows and counted. */
    queue_available = 1; queue.active_id = 0; queue.count = 0; queue.remove_done = 0;
    it = job(9, 1, 0);
    printer_says(1, "complete", "", "u18"); telemetry_says("complete", "local/part.gcode");
    requested(it); assert(queue_save() == 0);
    queue_load(); it = &queue.items[0];
    assert(queue_available && queue.count == 1 && !strcmp(it->state, "starting") && queue.active_id == 9);
    assert(!strcmp(it->request_uuid, "u18") && !it->job_uuid[0] && !it->native_seen);
    printer_says(2, "printing", "part.gcode", "u18"); telemetry_stale();
    queue_tick(&machine);
    assert(!strcmp(it->state, "printing") && !it->job_uuid[0] && !it->native_seen); /* the old uuid proves nothing */
    printer_says(2, "printing", "part.gcode", "u19"); telemetry_stale();
    queue_tick(&machine);
    assert(!strcmp(it->state, "printing") && !strcmp(it->job_uuid, "u19") && it->native_seen); /* not a takeover */
    printer_says(1, "complete", "", "u19"); telemetry_stale();
    queue_tick(&machine);
    assert(it->done == 1 && !strcmp(it->state, "done") && queue.active_id == 0 && !it->last_result[0]);
    /* A record written before the uuid was stored still loads. */
    FILE *old = fopen(file, "w");
    assert(old && fputs("{\"version\":1,\"next_id\":11,\"policy\":{\"after\":\"wait\",\"delay\":60,\"remove_done\":false},\"items\":[{\"id\":10,"
        "\"storage\":\"internal\",\"path\":\"part.gcode\",\"mapping\":\"\",\"side\":\"A\",\"leveling\":\"saved\",\"timelapse\":false,"
        "\"measure\":\"\",\"nozzle\":\"\",\"copies\":2,\"done\":1,\"estimated_seconds\":-1,\"filament_grams\":-1,\"added\":1,"
        "\"state\":\"pending\",\"last_result\":\"\",\"print_name\":\"\",\"native_seen\":false,\"job_uuid\":\"\"}]}\n", old) >= 0 &&
        fclose(old) == 0);
    queue_load();
    assert(queue_available && queue.count == 1 && queue.items[0].id == 10 && !queue.items[0].request_uuid[0]);

    /* The automatic start checks the Canvas material at the moment it would start, as the Queue page shows it:
     * a spool swapped during the countdown, an empty slot, missing telemetry or a file that names no material
     * hold the copy for the operator; a slot holding the material starts. (The external feed, checked above
     * with no tools, has nothing to verify.) These slots report no colour, so the job knows no filament and
     * prints with whatever its slot holds. */
    queue.count = 0; queue.active_id = 0;
    it = job(12, 2, 0); feed(it, "0:2");
    snprintf(queue.after, sizeof(queue.after), "auto"); queue.delay = 10;
    printer_says(1, "complete", "", "u19"); telemetry_says("complete", NULL);
    int sent = starts_sent;
    canvas_says(2, "PETG", 1); queue.complete_ms = monotonic_ms() - 11000; queue_tick(&machine); /* swapped during the countdown */
    assert(starts_sent == sent && !strcmp(it->state, "pending") && !strcmp(it->last_result, "auto_material") && queue.complete_ms == 0);
    it->last_result[0] = 0; canvas_says(2, "PLA", 0); queue.complete_ms = monotonic_ms() - 11000; queue_tick(&machine); /* slot empty */
    assert(starts_sent == sent && !strcmp(it->last_result, "auto_material"));
    it->last_result[0] = 0; machine.canvas_snapshot_len = 0; queue.complete_ms = monotonic_ms() - 11000; queue_tick(&machine); /* no telemetry */
    assert(starts_sent == sent && !strcmp(it->last_result, "auto_material"));
    queue_load(); it = &queue.items[0]; /* the hold is saved with the job */
    assert(queue.count == 1 && it->id == 12 && !strcmp(it->last_result, "auto_material") && !strcmp(it->state, "pending"));
    assert(it->tools == 1 && it->tool[0].tool == 0 && it->tool[0].slot == 2 && !queue_filament_known(&it->tool[0].filament));
    it->last_result[0] = 0; canvas_says(2, "Elegoo PLA Basic", 1); queue.complete_ms = monotonic_ms() - 11000; queue_tick(&machine);
    assert(starts_sent == sent + 1 && !strcmp(it->state, "starting") && !it->last_result[0]); /* the slot's type contains the material */
    queue.count = 0; queue.active_id = 0;
    it = job(13, 1, 0); snprintf(it->path, sizeof(it->path), "plain.gcode"); feed(it, "0:2");
    canvas_says(2, "PLA", 1); queue.complete_ms = monotonic_ms() - 11000; queue_tick(&machine); /* the file names no material */
    assert(starts_sent == sent + 1 && !strcmp(it->state, "pending") && !strcmp(it->last_result, "auto_material"));

    /* A tool whose slot is empty takes the lowest other slot that holds the same filament: the same type, name
     * and colour. A job that knows no filament for the tool yet judges the empty slot by its own label. The
     * queue reports what a start would take, and the start sends it; the job itself does not change. */
    queue.count = 0; queue.active_id = 0; queue.complete_ms = 0; snprintf(queue.after, sizeof(queue.after), "wait");
    it = job(14, 3, 0); feed(it, "0:3");
    printer_says(1, "complete", "", "u19"); telemetry_says("complete", NULL);
    canvas_list(TRAY(0, 1, "PETG", "#2B5DC4") "," TRAY(1, 1, "PETG", "#aeadac") "," TRAY(2, 1, "PETG", "#AEADAC") ","
                TRAY(3, 0, "PETG", "#AEADAC"));
    machine.canvas_watch_until = 0;
    assert(queue_reports("\"filaments\":[" BLIND(0, 3, 1, "empty") "]")); /* tray 0 has another colour; tray 1 comes before tray 2 */
    /* The printer answers a Canvas request and may say nothing by itself: while a start fed from the Canvas is
     * on offer, whoever reads the queue keeps the Canvas watched. Not a busy printer, nor a running copy. */
    assert(machine.canvas_watch_until > time(NULL));
    machine.canvas_watch_until = 0;
    printer_says(2, "printing", "other.gcode", "u19");
    assert(queue_reports("\"filaments\"") && machine.canvas_watch_until == 0);
    printer_says(1, "complete", "", "u19");
    sent = starts_sent;
    queue_start_response(begin(), &machine, "14", 2); assert(finish() == 202);
    assert(starts_sent == sent + 1 && start_count == 1 && start_trays[0] == 1);
    assert(!strcmp(it->state, "starting") && queue.active_id == 14 && it->tool[0].slot == 3);
    assert(queue_reports("\"active\":14") && machine.canvas_watch_until == 0);
    queue_abandon_response(begin(), &machine, "", 0); assert(finish() == 200 && !strcmp(it->state, "pending"));
    /* A slot loaded into the head feeds a start as well; a refilled slot is used again. */
    canvas_list(TRAY(1, 2, "PETG", "#AEADAC") "," TRAY(3, 0, "PETG", "#AEADAC"));
    assert(queue_reports("\"filaments\":[" BLIND(0, 3, 1, "empty") "]"));
    canvas_list(TRAY(1, 1, "PETG", "#AEADAC") "," TRAY(3, 1, "PETG", "#AEADAC"));
    assert(queue_reports("\"filaments\":[" BLIND(0, 3, 3, "") "]"));
    queue_start_response(begin(), &machine, "14", 2); assert(finish() == 202 && start_trays[0] == 3);
    queue_abandon_response(begin(), &machine, "", 0); assert(finish() == 200);
    /* Another colour, another material, another name, an empty slot or a slot without a label is not the same
     * filament: the start is refused and nothing changes. */
    canvas_list(TRAY(0, 1, "PETG", "#2B5DC4") "," TRAY(1, 0, "PETG", "#AEADAC") "," TRAY(2, 1, "PLA", "#AEADAC") ","
                TRAY(3, 0, "PETG", "#AEADAC"));
    assert(queue_reports("\"filaments\":[" BLIND(0, 3, null, "empty") "]"));
    sent = starts_sent; it->last_result[0] = 0;
    queue_start_response(begin(), &machine, "14", 2);
    assert(finish() == 409 && strstr(reply_body, "Canvas slot 4 is empty and no other slot holds the same filament"));
    assert(starts_sent == sent && !strcmp(it->state, "pending") && queue.active_id == 0 && !it->last_result[0] && !it->print_name[0]);
    canvas_list(TRAY_NAMED(0, 1, "PETG", "PETG Pro", "#AEADAC") "," TRAY(3, 0, "PETG", "#AEADAC"));
    assert(queue_reports("\"use\":null"));
    canvas_list(TRAY_NAMED(0, 1, "PLA", "Basic", "#AEADAC") "," TRAY_NAMED(3, 0, "PETG", "Basic", "#AEADAC"));
    assert(queue_reports("\"use\":null")); /* one name and colour for two materials */
    canvas_list("{\"tray_id\":0,\"status\":1},{\"tray_id\":3,\"status\":0}");
    assert(queue_reports("\"use\":null"));
    /* A tray the printer did not report, or telemetry that is not fresh: nothing is known to be empty. */
    canvas_list(TRAY(1, 1, "PETG", "#AEADAC"));
    assert(queue_reports("\"filaments\":[" BLIND(0, 3, 3, "") "]"));
    canvas_list(TRAY(1, 1, "PETG", "#AEADAC") "," TRAY(3, 0, "PETG", "#AEADAC"));
    machine.last_message = time(NULL) - 60;
    assert(queue_reports("\"filaments\":[" BLIND(0, 3, 3, "") "]"));
    machine.last_message = time(NULL);
    assert(queue_reports("\"filaments\":[" BLIND(0, 3, 1, "empty") "]"));
    /* Several tools: each takes the lowest slot that holds its filament, so tools with one filament share it. */
    feed(it, "0:3,1:1");
    canvas_list(TRAY(0, 1, "PETG", "#AEADAC") "," TRAY(1, 1, "PETG", "#AEADAC") "," TRAY(3, 0, "PETG", "#AEADAC"));
    assert(queue_reports("\"filaments\":[" BLIND(0, 3, 0, "empty") "," BLIND(1, 1, 1, "") "]"));
    canvas_list(TRAY(1, 1, "PETG", "#AEADAC") "," TRAY(3, 0, "PETG", "#AEADAC"));
    assert(queue_reports("\"filaments\":[" BLIND(0, 3, 1, "empty") "," BLIND(1, 1, 1, "") "]"));
    feed(it, "2:0,0:3,1:3"); /* the tools are kept in their own order */
    canvas_list(TRAY(0, 0, "PETG", "#AEADAC") "," TRAY(1, 1, "PETG", "#AEADAC") "," TRAY(2, 1, "PETG", "#AEADAC") ","
                TRAY(3, 0, "PETG", "#AEADAC"));
    assert(queue_reports("\"mapping\":\"0:3,1:3,2:0\""));
    assert(queue_reports("\"filaments\":[" BLIND(0, 3, 1, "empty") "," BLIND(1, 3, 1, "empty") "," BLIND(2, 0, 1, "empty") "]"));
    sent = starts_sent;
    queue_start_response(begin(), &machine, "14", 2); /* part.gcode has one tool: the print path refuses three */
    assert(finish() == 409 && strstr(reply_body, "Canvas mapping does not match this G-code") && starts_sent == sent);
    /* The external feed has no tools, and nothing to ask the Canvas. */
    it->tools = 0; machine.canvas_watch_until = 0;
    assert(queue_reports("\"filaments\":[]") && queue_reports("\"mapping\":\"\"") && machine.canvas_watch_until == 0);

    /* A tool added with a slot is bound to the filament the Canvas labels that slot with (an empty slot keeps
     * its label). The filament travels with the job, and the queue reports it with the slot a start would take. */
    queue.count = 0; queue.active_id = 0;
    printer_says(1, "complete", "", "u19"); telemetry_says("complete", NULL);
    canvas_list(TRAY(0, 1, "PETG", "#AEADAC") "," TRAY(1, 1, "PLA", "#FF0000") "," TRAY_NAMED(3, 0, "PETG", "PETG Pro", "#aeadac"));
    assert(add("internal\nduo.gcode\n0:3,1:1\nA\nsaved\n0\n\n\n3") == 201);
    it = &queue.items[0];
    assert(it->tools == 2 && it->tool[0].tool == 0 && it->tool[0].slot == 3 && it->tool[1].tool == 1 && it->tool[1].slot == 1);
    assert(holds(it, 0, "PETG", "PETG Pro", "#AEADAC") && holds(it, 1, "PLA", "PLA", "#FF0000"));
    /* Slot 4 is empty and nothing holds its filament yet: slot 1 has that colour under another name. */
    assert(queue_reports("\"mapping\":\"0:3,1:1\"") && queue_reports("\"filaments\":[" PRO(0, 3, null, "empty") "," RED(1, 1, 1, "") "]"));
    assert(stored_has("\"version\":1") && stored_has("\"mapping\":\"0:3,1:1\"") &&
           stored_has("\"filaments\":[{\"tool\":0,\"type\":\"PETG\",\"name\":\"PETG Pro\",\"color\":\"#AEADAC\"},"
                      "{\"tool\":1,\"type\":\"PLA\",\"name\":\"PLA\",\"color\":\"#FF0000\"}]"));
    queue_load(); it = &queue.items[0];
    assert(queue_available && queue.count == 1 && it->tools == 2 && it->tool[0].slot == 3 && it->tool[1].slot == 1);
    assert(holds(it, 0, "PETG", "PETG Pro", "#AEADAC") && holds(it, 1, "PLA", "PLA", "#FF0000"));

    /* The slot the job was added with holds another filament now, and another slot holds the job's: a start
     * takes that slot. Nothing about the job changes, and its own slot comes first when the filament is back. */
    canvas_list(TRAY_NAMED(0, 1, "PETG", "PETG Pro", "#AEADAC") "," TRAY(1, 1, "PLA", "#FF0000") "," TRAY(3, 1, "PLA", "#0000FF"));
    assert(queue_reports("\"filaments\":[" PRO(0, 3, 0, "other") "," RED(1, 1, 1, "") "]"));
    sent = starts_sent;
    queue_start_response(begin(), &machine, "", 0); assert(finish() == 202);
    assert(starts_sent == sent + 1 && start_count == 2 && start_trays[0] == 0 && start_trays[1] == 1);
    assert(it->tool[0].slot == 3 && holds(it, 0, "PETG", "PETG Pro", "#AEADAC"));
    queue_abandon_response(begin(), &machine, "", 0); assert(finish() == 200);
    canvas_list(TRAY_NAMED(0, 1, "PETG", "PETG Pro", "#AEADAC") "," TRAY(1, 1, "PLA", "#FF0000") ","
                TRAY_NAMED(3, 1, "petg", "petg pro", "#AEADAC")); /* the labels compare without case */
    assert(queue_reports("\"filaments\":[" PRO(0, 3, 3, "") "," RED(1, 1, 1, "") "]"));

    /* No slot holds the job's filament: the copy cannot start, whatever its old slot holds now, and the
     * refusal changes nothing. */
    canvas_list(TRAY(1, 1, "PLA", "#FF0000") "," TRAY(3, 1, "PLA", "#0000FF"));
    assert(queue_reports("\"filaments\":[" PRO(0, 3, null, "other") "," RED(1, 1, 1, "") "]"));
    sent = starts_sent;
    queue_start_response(begin(), &machine, "", 0);
    assert(finish() == 409 && strstr(reply_body, "No Canvas slot holds the job's filament: PETG Pro #AEADAC"));
    assert(starts_sent == sent && !strcmp(it->state, "pending") && queue.active_id == 0 && !it->print_name[0]);
    assert(it->tool[0].slot == 3 && holds(it, 0, "PETG", "PETG Pro", "#AEADAC"));
    /* The operator gives the tool another filament: the one a slot holds now. The other tools keep theirs, and
     * the change is saved. */
    int id = it->id;
    assert(change(id, "0:3") == 200);
    assert(it->tool[0].slot == 3 && holds(it, 0, "PLA", "PLA", "#0000FF") && it->tool[1].slot == 1 && holds(it, 1, "PLA", "PLA", "#FF0000"));
    assert(queue_reports("\"filaments\":[" FEED(0, "PLA", "PLA", "#0000FF", 3, 3, "") "," RED(1, 1, 1, "") "]"));
    queue_load(); it = &queue.items[0];
    assert(queue.count == 1 && it->id == id && it->tool[0].slot == 3 && holds(it, 0, "PLA", "PLA", "#0000FF"));
    queue_start_response(begin(), &machine, "", 0); assert(finish() == 202 && start_trays[0] == 3 && start_trays[1] == 1);
    /* Or a filament that is in no slot, by itself; also while a copy runs, which has its slots already. */
    assert(change(id, "\n0\tPETG\tPETG Pro\t#aeadac") == 200);
    assert(it->tool[0].slot == -1 && holds(it, 0, "PETG", "PETG Pro", "#AEADAC") && !strcmp(it->state, "starting") && it->tool[1].slot == 1);
    queue_abandon_response(begin(), &machine, "", 0); assert(finish() == 200);
    assert(queue_reports("\"mapping\":\"1:1\"") && queue_reports("\"filaments\":[" PRO(0, null, null, "absent") "," RED(1, 1, 1, "") "]"));
    /* What such a request cannot say: another job or a tool the job has not, a slot or a filament that is none,
     * one tool twice, or nothing at all. */
    assert(change(99, "0:3") == 404 && change(id, "5:1") == 409 && strstr(reply_body, "This job has no such tool"));
    assert(change(id, "0:9") == 400 && change(id, "") == 400 && strstr(reply_body, "No filament to change"));
    queue_filament_response(begin(), &machine, "1", 1); assert(finish() == 400);
    assert(change(id, "\n0\tPLA\tPLA\tred") == 400 && strstr(reply_body, "Invalid filament"));
    assert(change(id, "\n0\t\t\t#FF0000") == 400 && change(id, "\n0\tPLA\tPLA") == 400 && change(id, "\n0\tPLA\tPLA\t#FF0000\tx") == 400);
    assert(change(id, "\nx\tPLA\tPLA\t#FF0000") == 400 && change(id, "\n16\tPLA\tPLA\t#FF0000") == 400);
    assert(change(id, "\n0\tPL\"A\tPLA\t#FF0000") == 400 && change(id, "\n0\tPLA\t PLA\t#FF0000") == 400);
    assert(change(id, "0:1\n0\tPLA\tPLA\t#FF0000") == 400 && strstr(reply_body, "A tool is named twice"));
    assert(it->tool[0].slot == -1 && holds(it, 0, "PETG", "PETG Pro", "#AEADAC") && it->tool[1].slot == 1 && holds(it, 1, "PLA", "PLA", "#FF0000"));

    /* An empty slot gives way to the slot that holds the tool's filament, whatever label the empty slot
     * carries now; with that filament in no slot the start is refused. */
    it->tool[0].slot = 3; /* as a job added with slot 4 */
    canvas_list(TRAY_NAMED(0, 1, "PETG", "PETG Pro", "#AEADAC") "," TRAY(1, 1, "PLA", "#FF0000") "," TRAY(2, 1, "PETG", "#00FF00") ","
                TRAY(3, 0, "PETG", "#00FF00"));
    assert(queue_reports("\"filaments\":[" PRO(0, 3, 0, "empty") "," RED(1, 1, 1, "") "]"));
    canvas_list(TRAY(1, 1, "PLA", "#FF0000") "," TRAY(2, 1, "PETG", "#00FF00") "," TRAY(3, 0, "PETG", "#00FF00"));
    assert(queue_reports("\"filaments\":[" PRO(0, 3, null, "empty") "," RED(1, 1, 1, "") "]"));
    sent = starts_sent;
    queue_start_response(begin(), &machine, "", 0);
    assert(finish() == 409 && strstr(reply_body, "Canvas slot 4 is empty and no other slot holds the same filament"));
    assert(starts_sent == sent && !strcmp(it->state, "pending"));

    /* Two spools that changed places: each tool follows its filament. */
    canvas_list(TRAY_NAMED(1, 1, "PETG", "PETG Pro", "#AEADAC") "," TRAY(3, 1, "PLA", "#FF0000"));
    assert(queue_reports("\"filaments\":[" PRO(0, 3, 1, "other") "," RED(1, 1, 3, "other") "]"));
    /* Tools that print with one filament share the slot that holds it: the lowest one, unless a tool's own slot
     * holds it. A slot that reports no label holds no filament of the job. */
    it->tool[0].filament = it->tool[1].filament; /* as a job added with the same filament in both slots */
    canvas_list(TRAY(1, 1, "PLA", "#FF0000") "," TRAY(3, 1, "PETG", "#00FF00"));
    assert(queue_reports("\"filaments\":[" RED(0, 3, 1, "other") "," RED(1, 1, 1, "") "]"));
    canvas_list(TRAY(0, 1, "PLA", "#FF0000") "," TRAY(1, 1, "PLA", "#FF0000") "," TRAY(3, 1, "PETG", "#00FF00"));
    assert(queue_reports("\"filaments\":[" RED(0, 3, 0, "other") "," RED(1, 1, 1, "") "]"));
    sent = starts_sent;
    queue_start_response(begin(), &machine, "", 0);
    assert(finish() == 202 && starts_sent == sent + 1 && start_count == 2 && start_trays[0] == 0 && start_trays[1] == 1);
    queue_abandon_response(begin(), &machine, "", 0); assert(finish() == 200);
    canvas_list("{\"tray_id\":1,\"status\":1},{\"tray_id\":3,\"status\":1}");
    assert(queue_reports("\"filaments\":[" RED(0, 3, null, "other") "," RED(1, 1, null, "other") "]"));

    /* A job added while the Canvas had reported no label for its slot takes the filament from the first report
     * that has one (a label without a colour names no filament), and keeps it when the label changes. */
    queue.count = 0; machine.canvas_snapshot_len = 0;
    assert(add("internal\npart.gcode\n0:2\nA\nsaved\n0\n\n\n2") == 201);
    it = &queue.items[0];
    assert(it->tools == 1 && it->tool[0].slot == 2 && !queue_filament_known(&it->tool[0].filament));
    assert(queue_reports("\"filaments\":[" BLIND(0, 2, 2, "") "]") && stored_has("\"filaments\":[]"));
    canvas_list("{\"tray_id\":2,\"status\":1,\"filament_type\":\"PLA\"}"); queue_tick(&machine);
    assert(!queue_filament_known(&it->tool[0].filament));
    assert(queue_reports("\"filaments\":[" BLIND(0, 2, 2, "") "]")); /* nothing known: the slot is used as it is */
    canvas_list(TRAY(2, 0, "PLA", "#112233")); queue_tick(&machine);
    assert(holds(it, 0, "PLA", "PLA", "#112233"));
    queue_load(); it = &queue.items[0]; /* saved when it was learnt */
    assert(queue.count == 1 && it->tool[0].slot == 2 && holds(it, 0, "PLA", "PLA", "#112233"));
    canvas_list(TRAY(2, 1, "PETG", "#445566")); queue_tick(&machine);
    assert(holds(it, 0, "PLA", "PLA", "#112233"));
    assert(queue_reports("\"filaments\":[" FEED(0, "PLA", "PLA", "#112233", 2, null, "other") "]"));
    /* What the stored file may say about filaments. A filament of a tool of the mapping, or of a tool that is
     * not in it: added for the filament alone. Such a queue is saved as version 2, which a build that knows
     * only version 1 refuses rather than take the job for one that prints from the external feed. A record the
     * queue could not have written keeps the queue closed. */
    assert(loads(1, "0:1", "") && queue.items[0].tools == 1 && queue.items[0].tool[0].slot == 1);
    assert(loads(1, "0:1", "{\"tool\":0,\"type\":\"PLA\",\"name\":\"\",\"color\":\"#FF0000\"}"));
    assert(queue.items[0].tool[0].slot == 1 && holds(&queue.items[0], 0, "PLA", "", "#FF0000"));
    assert(loads(2, "1:0", "{\"tool\":0,\"type\":\"\",\"name\":\"PLA Matte\",\"color\":\"#F72221\"}"));
    assert(queue.items[0].tools == 2 && queue.items[0].tool[0].slot == -1 && holds(&queue.items[0], 0, "", "PLA Matte", "#F72221") &&
           queue.items[0].tool[1].tool == 1 && queue.items[0].tool[1].slot == 0);
    assert(queue_save() == 0 && stored_has("\"version\":2") && stored_has("\"mapping\":\"1:0\""));
    assert(!loads(3, "0:1", "") && !queue_available);
    assert(!loads(1, "0:1", "{\"tool\":0,\"type\":\"PLA\",\"name\":\"PLA\",\"color\":\"red\"}") && !queue_available);
    assert(!loads(1, "0:1", "{\"tool\":0,\"type\":\"\",\"name\":\"\",\"color\":\"#FF0000\"}") && !queue_available);
    assert(!loads(1, "0:1", "{\"tool\":16,\"type\":\"PLA\",\"name\":\"PLA\",\"color\":\"#FF0000\"}") && !queue_available);
    assert(!loads(1, "0:1", "{\"type\":\"PLA\",\"name\":\"PLA\",\"color\":\"#FF0000\"}") && !queue_available);
    assert(!loads(1, "0:1", "{\"tool\":0,\"type\":\"PLA\",\"name\":\"PLA\",\"color\":\"#FF0000\"},"
                           "{\"tool\":0,\"type\":\"PLA\",\"name\":\"PLA\",\"color\":\"#00FF00\"}") && !queue_available);
    assert(!loads(1, "", "{\"tool\":2,\"type\":\"PLA\",\"name\":\"PLA\",\"color\":\"#FF0000\"},"
                        "{\"tool\":2,\"type\":\"PLA\",\"name\":\"PLA\",\"color\":\"#00FF00\"}") && !queue_available);
    queue_available = 1; queue_error = ""; queue.remove_done = 0;

    /* The automatic start takes the slot that holds the filament when that slot holds the file's material,
     * and holds the copy when it does not. */
    queue.count = 0; queue.active_id = 0;
    it = job(15, 2, 0); feed(it, "0:2");
    snprintf(queue.after, sizeof(queue.after), "auto"); queue.delay = 10;
    printer_says(1, "complete", "", "u19"); telemetry_says("complete", NULL);
    canvas_list(TRAY(0, 1, "PETG", "#FF0000") "," TRAY(2, 0, "PETG", "#FF0000")); /* the file is PLA */
    sent = starts_sent; queue.complete_ms = monotonic_ms() - 11000; queue_tick(&machine);
    assert(starts_sent == sent && !strcmp(it->state, "pending") && !strcmp(it->last_result, "auto_material"));
    /* A Canvas report older than the copy that just completed checks nothing: the start waits for the answer
     * to the request made at the completion, and the copy is held when none came. */
    queue.count = 0; it = job(16, 2, 0); feed(it, "0:2");
    canvas_list(TRAY(0, 1, "PLA", "#FF0000") "," TRAY(2, 0, "PLA", "#FF0000"));
    queue.complete_canvas = machine.canvas_revision; queue.complete_ms = monotonic_ms() - 11000; queue_tick(&machine);
    assert(starts_sent == sent && !strcmp(it->state, "pending") && !strcmp(it->last_result, "auto_material"));
    it->last_result[0] = 0;
    canvas_list(TRAY(0, 1, "PLA", "#FF0000") "," TRAY(2, 0, "PLA", "#FF0000")); /* the printer's answer */
    queue.complete_ms = monotonic_ms() - 11000; queue_tick(&machine);
    assert(starts_sent == sent + 1 && start_count == 1 && start_trays[0] == 0);
    assert(!strcmp(it->state, "starting") && !it->last_result[0] && it->tool[0].slot == 2);
    /* Nor does it print with another filament than the job's: with the job's filament in no slot the copy is
     * held, although its old slot holds the file's material. When a slot holds it, the start takes that slot. */
    queue_abandon_response(begin(), &machine, "", 0); assert(finish() == 200);
    assert(holds(it, 0, "PLA", "PLA", "#FF0000")); /* learnt from the first report */
    it->last_result[0] = 0;
    canvas_list(TRAY(0, 1, "PLA", "#00FF00") "," TRAY(2, 1, "PLA", "#0000FF"));
    sent = starts_sent; queue.complete_ms = monotonic_ms() - 11000; queue_tick(&machine);
    assert(starts_sent == sent && !strcmp(it->state, "pending") && !strcmp(it->last_result, "auto_material"));
    assert(holds(it, 0, "PLA", "PLA", "#FF0000"));
    it->last_result[0] = 0;
    canvas_list(TRAY(0, 1, "PLA", "#00FF00") "," TRAY(1, 1, "PLA", "#FF0000") "," TRAY(2, 1, "PLA", "#0000FF"));
    queue.complete_ms = monotonic_ms() - 11000; queue_tick(&machine);
    assert(starts_sent == sent + 1 && start_count == 1 && start_trays[0] == 1);
    assert(!strcmp(it->state, "starting") && !it->last_result[0] && holds(it, 0, "PLA", "PLA", "#FF0000"));

    /* A job added for a filament alone, while the printer holds nothing of the kind: it waits until a slot
     * holds that filament and then starts from it, whichever slot it is. */
    queue.count = 0; queue.active_id = 0; queue.complete_ms = 0; queue.next_id = 50; snprintf(queue.after, sizeof(queue.after), "wait");
    printer_says(1, "complete", "", "u19"); telemetry_says("complete", NULL);
    canvas_list(TRAY(0, 1, "PETG", "#AEADAC") "," TRAY(1, 1, "PETG", "#2B5DC4"));
    assert(add("internal\npart.gcode\n\nA\nsaved\n0\n\n\n2\nend\n\n\n0\tPLA\tPLA Matte\t#f72221") == 201);
    it = &queue.items[0];
    assert(it->id == 50 && it->tools == 1 && it->tool[0].tool == 0 && it->tool[0].slot == -1 && holds(it, 0, "PLA", "PLA Matte", "#F72221"));
    machine.canvas_watch_until = 0;
    assert(queue_reports("\"mapping\":\"\"") && queue_reports("\"filaments\":[" MATTE(0, null, null, "absent") "]"));
    assert(machine.canvas_watch_until > time(NULL)); /* a start that looks for a filament keeps the Canvas watched too */
    sent = starts_sent;
    queue_start_response(begin(), &machine, "50", 2);
    assert(finish() == 409 && strstr(reply_body, "No Canvas slot holds the job's filament: PLA Matte #F72221"));
    assert(starts_sent == sent && !strcmp(it->state, "pending") && queue.active_id == 0);
    assert(stored_has("\"version\":2") && stored_has("\"mapping\":\"\"") &&
           stored_has("\"filaments\":[{\"tool\":0,\"type\":\"PLA\",\"name\":\"PLA Matte\",\"color\":\"#F72221\"}]"));
    queue_load(); it = &queue.items[0];
    assert(queue_available && queue.count == 1 && it->tools == 1 && it->tool[0].slot == -1 && holds(it, 0, "PLA", "PLA Matte", "#F72221"));
    /* Another line or another colour of the material is not it, nor is an empty slot that is labelled with it. */
    canvas_list(TRAY_NAMED(0, 1, "PLA", "PLA Silk", "#F72221") "," TRAY_NAMED(1, 1, "PLA", "PLA Matte", "#F88946") ","
                TRAY_NAMED(2, 0, "PLA", "PLA Matte", "#F72221"));
    assert(queue_reports("\"filaments\":[" MATTE(0, null, null, "absent") "]"));
    /* The spool is loaded: the lowest slot that holds it, present or already in the head. */
    canvas_list(TRAY(0, 1, "PETG", "#AEADAC") "," TRAY_NAMED(2, 2, "pla", "pla matte", "#F72221") ","
                TRAY_NAMED(3, 1, "PLA", "PLA Matte", "#F72221"));
    assert(queue_reports("\"filaments\":[" MATTE(0, null, 2, "") "]"));
    queue_start_response(begin(), &machine, "50", 2); assert(finish() == 202);
    assert(starts_sent == sent + 1 && start_count == 1 && start_trays[0] == 2 && it->tool[0].slot == -1);
    queue_abandon_response(begin(), &machine, "", 0); assert(finish() == 200);
    /* Without a fresh Canvas report there is nowhere to look. */
    machine.last_message = time(NULL) - 60;
    assert(queue_reports("\"filaments\":[" MATTE(0, null, null, "unknown") "]"));
    machine.last_message = time(NULL); machine.canvas_snapshot_len = 0;
    assert(queue_reports("\"filaments\":[" MATTE(0, null, null, "unknown") "]"));
    sent = starts_sent;
    queue_start_response(begin(), &machine, "50", 2);
    assert(finish() == 409 && strstr(reply_body, "The Canvas has not reported its slots") && starts_sent == sent);
    /* A filament stated without a product line is any line of that material in that colour; one stated by its
     * line alone is that line, whatever type the slot gives it. */
    canvas_list(TRAY_NAMED(1, 1, "PLA", "PLA Silk", "#F72221") "," TRAY_NAMED(2, 1, "PETG", "PLA", "#F72221"));
    assert(change(50, "\n0\tPLA\t\t#F72221") == 200 && holds(it, 0, "PLA", "", "#F72221"));
    assert(queue_reports("\"filaments\":[" FEED(0, "PLA", "", "#F72221", null, 1, "") "]"));
    assert(change(50, "\n0\t\tPLA\t#F72221") == 200 && queue_reports("\"filaments\":[" FEED(0, "", "PLA", "#F72221", null, 2, "") "]"));
    assert(change(50, "\n0\t\tPLA Matte\t#F72221") == 200 &&
           queue_reports("\"filaments\":[" FEED(0, "", "PLA Matte", "#F72221", null, null, "absent") "]"));
    /* One tool on a slot and one waiting for its filament; the filament lines follow the estimates of an add
     * request, and empty lines are skipped. A request that names a tool twice or a filament that is none is
     * refused whole. */
    canvas_list(TRAY(0, 1, "PETG", "#AEADAC"));
    assert(add("internal\nduo.gcode\n0:0\nA\nsaved\n0\n\n\n1\nend\n600\n12.5\n\n1\tPLA\tPLA Matte\t#F72221\n") == 201 && queue.count == 2);
    queue_item *mixed = &queue.items[1];
    assert(mixed->tools == 2 && mixed->tool[0].slot == 0 && holds(mixed, 0, "PETG", "PETG", "#AEADAC") &&
           mixed->tool[1].slot == -1 && holds(mixed, 1, "PLA", "PLA Matte", "#F72221") && mixed->estimated_seconds == 600);
    assert(queue_reports("\"filaments\":[" FEED(0, "PETG", "PETG", "#AEADAC", 0, 0, "") "," MATTE(1, null, null, "absent") "]"));
    assert(add("internal\nduo.gcode\n0:0\nA\nsaved\n0\n\n\n1\nend\n\n\n0\tPLA\tPLA Matte\t#F72221") == 400 && strstr(reply_body, "A tool is named twice"));
    assert(add("internal\nduo.gcode\n0:0\nA\nsaved\n0\n\n\n1\nend\n\n\n1\tPLA\tPLA Matte\tF72221") == 400 && strstr(reply_body, "Invalid filament"));
    assert(add("internal\nduo.gcode\n\nA\nsaved\n0\n\n\n1\nend\n\n\n1\tPLA\tPLA Matte\t#F72221\n1\tPLA\tPLA Matte\t#F88946") == 400);
    assert(queue.count == 2);
    /* The automatic start holds a copy whose filament is in no slot, and takes the slot once one holds it. */
    assert(change(50, "\n0\tPLA\tPLA Matte\t#F72221") == 200);
    snprintf(queue.after, sizeof(queue.after), "auto"); queue.delay = 10;
    canvas_list(TRAY(0, 1, "PETG", "#AEADAC"));
    sent = starts_sent; queue.complete_ms = monotonic_ms() - 11000; queue_tick(&machine);
    assert(starts_sent == sent && !strcmp(it->state, "pending") && !strcmp(it->last_result, "auto_material"));
    it->last_result[0] = 0;
    canvas_list(TRAY(0, 1, "PETG", "#AEADAC") "," TRAY_NAMED(3, 1, "PLA", "PLA Matte", "#F72221"));
    queue.complete_ms = monotonic_ms() - 11000; queue_tick(&machine);
    assert(starts_sent == sent + 1 && start_count == 1 && start_trays[0] == 3 && !strcmp(it->state, "starting") && !it->last_result[0]);

    unlink(gcode); unlink(plain); unlink(duo); unlink(file); unlink(autosave); rmdir(internal); rmdir(dir);
    puts("PASS copies counted only from this copy's own completion seen fresh; stops, foreign jobs, long starts, pauses, "
         "USB imports, policy changes, the automatic start with its guards, the running-copy handlers, jobs bound to "
         "filaments (the slot that holds one, a job added for a filament that is in no slot, another filament for a "
         "tool) and their stored form covered");
    return 0;
}
