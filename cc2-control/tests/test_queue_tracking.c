#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

/* How the print queue follows a copy from the start request to its end, from the MQTT
 * cache and the UDS telemetry alone, and what it decides on stale or foreign evidence. */
static mqtt_client machine;

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
    clock_gettime(CLOCK_MONOTONIC, &telemetry.last_rx);
}

static queue_item *job(int id, int copies, int done) {
    queue_item *it = &queue.items[queue.count++];
    memset(it, 0, sizeof(*it));
    it->id = id; snprintf(it->storage, sizeof(it->storage), "internal");
    snprintf(it->path, sizeof(it->path), "part.gcode"); it->side = 'A';
    snprintf(it->leveling, sizeof(it->leveling), "saved");
    it->copies = copies; it->done = done; it->estimated_seconds = -1; it->filament_grams = -1;
    snprintf(it->state, sizeof(it->state), "pending");
    if (queue.next_id <= id) queue.next_id = id + 1;
    return it;
}

static void copy_starts(queue_item *it, const char *uuid) {
    snprintf(it->state, sizeof(it->state), "starting");
    it->started_ms = monotonic_ms(); it->job_uuid[0] = 0; queue.active_id = it->id;
    printer_says(2, "printing", "part.gcode", uuid); telemetry_says("printing", "local/part.gcode");
    queue_tick(&machine);
    assert(!strcmp(it->state, "printing") && !strcmp(it->job_uuid, uuid));
}

int main(void) {
    char dir[] = "/tmp/cc2-queue-XXXXXX";
    assert(mkdtemp(dir));
    static char file[640], internal[640], gcode[700];
    snprintf(file, sizeof(file), "%s/print-queue.json", dir); queue_path = file;
    snprintf(internal, sizeof(internal), "%s/internal", dir); assert(mkdir(internal, 0755) == 0); gcode_internal_root = internal;
    snprintf(gcode, sizeof(gcode), "%s/part.gcode", internal);
    FILE *out = fopen(gcode, "w"); assert(out && fputs("G28\n", out) >= 0 && fclose(out) == 0);
    memset(&machine, 0, sizeof(machine)); uds_init(&telemetry);
    queue_load();
    assert(queue_available && queue.count == 0 && !strcmp(queue.after, "wait") && queue.remove_done);
    queue.remove_done = 0; /* finished jobs stay listed until the last section below */

    /* A copy: start request, the printer runs the file, the printer finishes it. */
    queue_item *it = job(1, 2, 0);
    assert(queue_save() == 0);
    copy_starts(it, "u1");
    printer_says(1, "complete", "", "u1"); telemetry_says("complete", "local/part.gcode");
    queue_tick(&machine);
    assert(it->done == 1 && !strcmp(it->state, "pending") && queue.active_id == 0 && queue.complete_ms > 0 && !it->last_result[0]);
    queue_load(); it = &queue.items[0]; /* persisted as counted */
    assert(queue.count == 1 && it->done == 1 && !strcmp(it->state, "pending") && queue.active_id == 0);

    /* A cancelled copy is not counted; the job stays first with a note. */
    copy_starts(it, "u2");
    printer_says(1, "cancelled", "", "u2"); telemetry_says("cancelled", "local/part.gcode");
    queue_tick(&machine);
    assert(it->done == 1 && !strcmp(it->state, "pending") && !strcmp(it->last_result, "stopped") && queue.active_id == 0 && queue.complete_ms == 0);

    /* A start the printer never reported running. */
    snprintf(it->state, sizeof(it->state), "starting"); queue.active_id = 1; it->started_ms = monotonic_ms() - QUEUE_START_GRACE_MS - 1;
    printer_says(1, "standby", "", ""); telemetry_says("standby", NULL);
    queue_tick(&machine);
    assert(!strcmp(it->state, "pending") && !strcmp(it->last_result, "not_started") && queue.active_id == 0);

    /* Another file completing is not this job's copy. */
    copy_starts(it, "u3");
    printer_says(1, "complete", "other.gcode", "u3"); telemetry_says("complete", "local/other.gcode");
    queue_tick(&machine);
    assert(it->done == 1 && !strcmp(it->last_result, "stopped") && queue.active_id == 0);

    /* Idle again without "complete" for a minute: the copy ended otherwise. */
    copy_starts(it, "u4"); it->started_ms = monotonic_ms() - QUEUE_IDLE_GRACE_MS - 1;
    printer_says(1, "standby", "", ""); telemetry_says("standby", NULL);
    queue_tick(&machine);
    assert(!strcmp(it->state, "pending") && !strcmp(it->last_result, "stopped") && it->done == 1);

    /* Stale evidence decides nothing, in either direction. */
    copy_starts(it, "u5");
    printer_says(1, "complete", "", "u5"); machine.last_message = time(NULL) - 60; telemetry_says("complete", "local/part.gcode");
    queue_tick(&machine);
    assert(!strcmp(it->state, "printing") && queue.active_id == 1 && it->done == 1);
    machine.last_message = time(NULL);
    queue_tick(&machine);
    assert(it->done == 2 && !strcmp(it->state, "done") && queue.count == 1); /* remove_done is off: the job stays, finished */
    assert(queue.active_id == 0);

    /* The last copy with remove_done drops the job. */
    queue.count = 0; it = job(2, 1, 0); queue.remove_done = 1;
    copy_starts(it, "u6");
    printer_says(1, "complete", "part.gcode", "u6"); telemetry_says("complete", NULL);
    queue_tick(&machine);
    assert(queue.count == 0 && queue.active_id == 0);

    /* The automatic policy starts the next job through the protected print path after the delay;
     * without a printer link that path refuses, and the refusal is recorded instead of retried. */
    it = job(3, 2, 0); assert(queue_save() == 0);
    snprintf(queue.after, sizeof(queue.after), "auto"); queue.delay = 10;
    queue.complete_ms = monotonic_ms() - 11000;
    printer_says(1, "complete", "", ""); machine.registered = 0; telemetry_says("complete", NULL);
    queue_tick(&machine);
    assert(queue.complete_ms == 0 && !strcmp(it->last_result, "auto_failed") && queue.active_id == 0 && !strcmp(it->state, "pending"));
    /* Not before the delay, and never with the wait policy. */
    queue.complete_ms = monotonic_ms() - 5000; it->last_result[0] = 0;
    queue_tick(&machine);
    assert(queue.complete_ms != 0 && !it->last_result[0]);
    snprintf(queue.after, sizeof(queue.after), "wait"); queue.complete_ms = monotonic_ms() - 11000;
    queue_tick(&machine);
    assert(!it->last_result[0] && queue.active_id == 0);

    /* A loaded file with a running copy keeps following it; two running copies are refused. */
    snprintf(it->state, sizeof(it->state), "printing"); queue.active_id = 3; assert(queue_save() == 0);
    queue_load();
    assert(queue.count == 1 && queue.active_id == 3 && !strcmp(queue.items[0].state, "printing"));
    job(4, 1, 0); snprintf(queue.items[1].state, sizeof(queue.items[1].state), "printing"); assert(queue_save() == 0);
    queue_load();
    assert(!queue_available && queue.count == 0);

    unlink(gcode); unlink(file); rmdir(internal); rmdir(dir);
    puts("PASS copies counted only from fresh evidence of this job's completion; stops, foreign jobs, lost starts and the automatic policy handled");
    return 0;
}
