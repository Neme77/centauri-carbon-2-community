#define main cc2_original_main
#include "../src/main.c"
#undef main
#include <assert.h>
/* Continue from another Canvas slot after a runout (issue #284). The log lines are copied from a
 * CC2 (firmware 02.01.01.07) that ran out on slot 3 and was continued from slot 2 on 2026-11-02. */
static mqtt_client machine;
static char sent[1024];
static int sends;
static const char *reply_text = "{\"id\":204,\"result\":{}}";
static int fake_rpc(const char *query, char **reply, size_t *length) {
    snprintf(sent, sizeof(sent), "%s", query); sends++;
    *reply = strdup(reply_text); assert(*reply); *length = strlen(*reply); return 0;
}

#define L(text) "[2026-11-03 03:15:34.487] [info] " text "\n"
#define LT(stamp, text) "[2026-11-03 " stamp "] [info] " text "\n"
static const char *PREVIOUS_PRINT =
    L("[mmu.cpp:4520] [2024] CMD_canvas_set_color_table #1 T_vec.size 1 table_T 0 table_color 0xFF3700 table_channel 1")
    L("[virtual_sdcard.cpp:1095] [2024] cmd_SDCARD_PRINT_FILE slice_cfg_model:false auto_det:false filename:local/old.gcode")
    L("[mmu.cpp:3640] [2024] canvas check filament break, enter secondary confirmation!  channel 1");
static const char *THIS_PRINT =
    L("[mmu.cpp:4520] [2024] CMD_canvas_set_color_table #1 T_vec.size 2 table_T 0 table_color 0x696357 table_channel 2")
    L("[mmu.cpp:4520] [2024] CMD_canvas_set_color_table #1 T_vec.size 2 table_T 1 table_color 0x735DF9 table_channel 0")
    L("[virtual_sdcard.cpp:1095] [2024] cmd_SDCARD_PRINT_FILE slice_cfg_model:false auto_det:false filename:local/test.gcode")
    L("[mmu.cpp:4664] [2024] M6211 printable_height 256 T 0 A true K 75 L 200 M 600 N 300")
    /* the firmware's own auto refill moved T1 from slot 1 (channel 0) to slot 2 (channel 1) */
    L("[mmu.cpp:2939] [2024] exec auto refill filament start, befor_channel 0 ex_fila_status 0 canvas_status 0")
    L("[mmu.cpp:2991] [2024] exec auto refill filament over, after_channel 1 ex_fila_status 0 canvas_status 1")
    /* an ordinary load reports the same channel before and after */
    L("[mmu.cpp:2939] [2024] exec auto refill filament start, befor_channel 2 ex_fila_status 0 canvas_status 1")
    L("[mmu.cpp:2991] [2024] exec auto refill filament over, after_channel 2 ex_fila_status 0 canvas_status 1")
    L("[mmu.cpp:3640] [2024] canvas check filament break, enter secondary confirmation!  channel 2")
    L("[mmu.cpp:4165] [2024] Failed to execute filament switch process, reason [error_code 803] MMUError: Filament run out  channel : 2");

static void write_file(const char *path, const char *text, const char *mode) {
    FILE *f = fopen(path, mode); assert(f); assert(fputs(text, f) >= 0); fclose(f);
}

/* Canvas: slot 1 PA, slot 2 ASA-CF black, slot 3 ASA-CF sand (empty: the runout), slot 4 PETG-CF. */
static void canvas(int s0, int s1, int s2, int s3) {
    int n = snprintf(machine.canvas_snapshot, sizeof(machine.canvas_snapshot),
        "{\"result\":{\"canvas_info\":{\"active_tray_id\":2,\"canvas_list\":[{\"canvas_id\":0,\"connected\":1,\"tray_list\":["
        "{\"tray_id\":0,\"status\":%d,\"filament_type\":\"PA\",\"filament_color\":\"#FFFFFF\",\"min_nozzle_temp\":245,\"max_nozzle_temp\":270},"
        "{\"tray_id\":1,\"status\":%d,\"filament_type\":\"ASA\",\"filament_color\":\"#000000\",\"min_nozzle_temp\":240,\"max_nozzle_temp\":280},"
        "{\"tray_id\":2,\"status\":%d,\"filament_type\":\"ASA\",\"filament_color\":\"#696357\",\"min_nozzle_temp\":240,\"max_nozzle_temp\":280},"
        "{\"tray_id\":3,\"status\":%d,\"filament_type\":\"PETG\",\"filament_color\":\"#000000\",\"min_nozzle_temp\":200,\"max_nozzle_temp\":260}]}]}}}",
        s0, s1, s2, s3);
    assert(n > 0 && (size_t)n < sizeof(machine.canvas_snapshot)); machine.canvas_snapshot_len = (size_t)n;
    machine.last_message = time(NULL);
}
static void event(int code, int level) {
    telemetry.report_code = code; telemetry.report_level = level; telemetry.report_sequence++;
    clock_gettime(CLOCK_REALTIME, &telemetry.report_identity); telemetry.report_identity.tv_nsec += telemetry.report_sequence % 1000;
}
static void fresh(void) { clock_gettime(CLOCK_MONOTONIC, &telemetry.last_rx); machine.last_message = time(NULL); }

static char response[8192];
static int call(int which, const char *body) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    if (which == 0) runout_response(pair[0], &machine);
    else if (which == 1) runout_continue_response(pair[0], &machine, body, strlen(body));
    else runout_setting_response(pair[0], body, strlen(body));
    close(pair[0]);
    ssize_t n = read(pair[1], response, sizeof(response) - 1); assert(n > 0); response[n] = 0; close(pair[1]);
    int status = 0; sscanf(response, "HTTP/1.1 %d", &status); return status;
}
static int available(void) { assert(call(0, NULL) == 200); return strstr(response, "\"available\":true") != NULL; }
static int reason(const char *expected) { assert(call(0, NULL) == 200); char key[64]; snprintf(key, sizeof(key), "\"reason\":\"%s\"", expected); return strstr(response, key) != NULL; }
static void reset_job(void) { plates_exchange_close(&runout.x); runout.stage = RO_IDLE; runout.automatic = 0; runout.message[0] = runout.auto_note[0] = 0; }

/* Feeds a whole log in one go and returns whether the latest print has a map. */
static int map_of(const char *text, runout_table *t) {
    runout_log s; runout_log_reset(&s); runout_log_feed(&s, text, strlen(text)); return runout_log_table(&s, t);
}

/* A printer socket that accepts and then says only what the test writes. */
static int listener = -1, peer = -1;
static void printer_socket(const char *path) {
    struct sockaddr_un a; memset(&a, 0, sizeof(a)); a.sun_family = AF_UNIX;
    assert(strlen(path) < sizeof(a.sun_path)); memcpy(a.sun_path, path, strlen(path) + 1);
    unlink(path); listener = socket(AF_UNIX, SOCK_STREAM, 0); assert(listener >= 0);
    assert(!bind(listener, (struct sockaddr *)&a, sizeof(a)) && !listen(listener, 4));
}
static void accept_peer(void) {
    if (peer >= 0) close(peer);
    peer = accept(listener, NULL, NULL); assert(peer >= 0);
}
static size_t drain(void) { /* what the printer has received so far */
    char buffer[2048]; ssize_t n = recv(peer, buffer, sizeof(buffer), MSG_DONTWAIT); return n > 0 ? (size_t)n : 0;
}

int main(void) {
    char dir[] = "/tmp/cc2-runout-XXXXXX", older[300], current[300], settings[300];
    assert(mkdtemp(dir));
    snprintf(older, sizeof(older), "%s/elegoo.1.log", dir); snprintf(current, sizeof(current), "%s/elegoo.log", dir);
    snprintf(settings, sizeof(settings), "%s/runout.conf", dir);
    runout_log_paths[0] = older; runout_log_paths[1] = current; runout_settings_path = settings;

    /* ---- parsing, fed in uneven pieces: the map is this print's, with the refill applied */
    {
        runout_log s; runout_log_reset(&s);
        char text[8192]; snprintf(text, sizeof(text), "%s%s", PREVIOUS_PRINT, THIS_PRINT);
        size_t length = strlen(text), done = 0, carry = 0; char buffer[8192];
        for (size_t piece = 7; done < length; piece = piece * 3 % 211 + 13) {
            size_t n = length - done < piece ? length - done : piece;
            memcpy(buffer + carry, text + done, n); done += n; carry += n;
            size_t used = runout_log_feed(&s, buffer, carry);
            memmove(buffer, buffer + used, carry - used); carry -= used;
        }
        assert(carry == 0);
        runout_table t; assert(runout_log_table(&s, &t) && t.count == 2);
        assert(t.e[0].tool == 0 && t.e[0].channel == 2 && !strcmp(t.e[0].color, "0x696357"));
        assert(t.e[1].tool == 1 && t.e[1].channel == 1 && !strcmp(t.e[1].color, "0x735DF9"));
        assert(runout_log_channel(&s) == 2);
        char script[RUNOUT_SCRIPT_MAX];
        assert(!runout_script(&t, 2, 1, 270, script, sizeof(script)));
        assert(!strcmp(script, "CANVAS_SET_COLOR_TABLE T=0,1 ID=0,0 CHANNEL=1,1 COLOR=0x696357,0x735DF9\\nM109 S270\\nRESUME"));
        /* The previous print's map and runout never count for a new print without its own. */
        runout_log_reset(&s);
        snprintf(text, sizeof(text), "%s%s", PREVIOUS_PRINT, L("[virtual_sdcard.cpp:1095] [2024] cmd_SDCARD_PRINT_FILE filename:local/new.gcode"));
        runout_log_feed(&s, text, strlen(text));
        assert(!runout_log_table(&s, &t) && runout_log_channel(&s) == -1);
        /* A group cut short by a malformed entry is not a map. */
        runout_log_reset(&s);
        const char *broken = L("[mmu.cpp:4520] CMD_canvas_set_color_table #1 T_vec.size 2 table_T 0 table_color 0x696357 table_channel 2")
                             L("[mmu.cpp:4520] CMD_canvas_set_color_table #1 T_vec.size 2 table_T 1 table_color red table_channel 0")
                             L("[virtual_sdcard.cpp:1095] cmd_SDCARD_PRINT_FILE filename:local/x.gcode");
        runout_log_feed(&s, broken, strlen(broken));
        assert(!runout_log_table(&s, &t));
    }

    /* ---- the reader: the older file once, the current one incrementally, again after rotation */
    write_file(older, PREVIOUS_PRINT, "w");
    write_file(current, "", "w");
    runout_reader.ready = 0;
    assert(runout_refresh());
    runout_table t;   /* the latest print is still the old one: its own map */
    assert(runout_log_table(&runout_reader.log, &t) && t.count == 1 && t.e[0].channel == 1);
    write_file(current, THIS_PRINT, "a");
    write_file(current, "[2026-11-03 03:35:46.689] [info] half a li", "a");
    assert(runout_refresh() && runout_log_table(&runout_reader.log, &t) && t.e[0].channel == 2);
    long lines = runout_reader.log.line;
    assert(runout_refresh() && runout_reader.log.line == lines);   /* nothing new: nothing re-read */
    write_file(current, "ne\n", "a");
    assert(runout_refresh() && runout_reader.log.line == lines + 1);
    assert(!rename(current, older)); write_file(current, "", "w");   /* rotation */
    assert(runout_refresh() && runout_log_table(&runout_reader.log, &t) && runout_log_channel(&runout_reader.log) == 2);

    /* ---- whose map is it: the previous print's mid-print table is never the next print's */
    {
        const char *old_start =
            LT("10:00:00.000", "[mmu.cpp:4520] [2024] CMD_canvas_set_color_table #1 T_vec.size 1 table_T 0 table_color 0xFF3700 table_channel 1")
            LT("10:00:00.030", "[virtual_sdcard.cpp:1095] [2024] cmd_SDCARD_PRINT_FILE slice_cfg_model:false auto_det:false filename:local/a.gcode");
        const char *old_continue = /* e.g. our own continue during print A */
            LT("10:20:00.000", "[mmu.cpp:4520] [2024] CMD_canvas_set_color_table #1 T_vec.size 1 table_T 0 table_color 0xFF3700 table_channel 3");
        const char *new_start_no_table =
            LT("11:00:00.000", "[virtual_sdcard.cpp:1095] [2024] cmd_SDCARD_PRINT_FILE slice_cfg_model:false auto_det:false filename:local/b.gcode")
            LT("11:10:00.000", "[mmu.cpp:3640] [2024] canvas check filament break, enter secondary confirmation!  channel 3");
        char text[4096];
        snprintf(text, sizeof(text), "%s", old_start); assert(map_of(text, &t) && t.e[0].channel == 1);   /* A's start table */
        snprintf(text, sizeof(text), "%s%s", old_start, old_continue); assert(map_of(text, &t) && t.e[0].channel == 3);
        snprintf(text, sizeof(text), "%s%s%s", old_start, old_continue, new_start_no_table);
        assert(!map_of(text, &t));                                        /* B set no table: refused, not A's */
        /* B's own table just before its start line is B's, whatever came before */
        snprintf(text, sizeof(text), "%s%s%s%s", old_start, old_continue,
                 LT("10:59:59.970", "[mmu.cpp:4520] [2024] CMD_canvas_set_color_table #1 T_vec.size 1 table_T 0 table_color 0x112233 table_channel 2"),
                 new_start_no_table);
        assert(map_of(text, &t) && t.e[0].channel == 2 && !strcmp(t.e[0].color, "0x112233"));
        /* a table well before the start is not the start's own */
        snprintf(text, sizeof(text), "%s%s%s", old_start,
                 LT("10:58:00.000", "[mmu.cpp:4520] [2024] CMD_canvas_set_color_table #1 T_vec.size 1 table_T 0 table_color 0x112233 table_channel 2"),
                 new_start_no_table);
        assert(!map_of(text, &t));
        /* nor one whose time is unknown */
        snprintf(text, sizeof(text), "%s%s%s", old_start,
                 "[mmu.cpp:4520] [2024] CMD_canvas_set_color_table #1 T_vec.size 1 table_T 0 table_color 0x112233 table_channel 2\n",
                 new_start_no_table);
        assert(!map_of(text, &t));
        /* a table set during B (a continue) is B's */
        snprintf(text, sizeof(text), "%s%s%s%s", old_start, old_continue, new_start_no_table,
                 LT("11:11:00.000", "[mmu.cpp:4520] [2024] CMD_canvas_set_color_table #1 T_vec.size 1 table_T 0 table_color 0xFF3700 table_channel 0"));
        assert(map_of(text, &t) && t.e[0].channel == 0);
        assert(runout_line_ms("[2026-03-01 00:00:00.000] x") - runout_line_ms("[2026-02-28 23:59:59.970] x") == 30);
        assert(runout_line_ms("[2026-11-03 10:00] x") == -1 && runout_line_ms("no stamp") == -1);
    }

    /* ---- the API */
    plates_uds = fake_rpc;
    machine.connected = machine.registered = machine.canvas_direct_known = 1;
    uds_init(&telemetry); int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    telemetry.fd = pair[0]; telemetry.ready = 1; telemetry.have_print_state = 1; telemetry.connections = 1;
    telemetry.present = 1u << U_EG; telemetry.values[U_EG] = 270;
    canvas(1, 1, 0, 1); fresh();
    strcpy(telemetry.print_state, "printing"); event(1102, 1);
    assert(reason("not_paused"));
    strcpy(telemetry.print_state, "paused");
    assert(reason("not_runout"));
    event(1211, 3); assert(reason("not_runout"));                       /* cleared */
    event(1211, 1); assert(available());
    assert(strstr(response, "\"slot\":2,\"tools\":[0],\"material\":\"ASA\""));
    assert(strstr(response, "{\"slot\":1,\"status\":1,\"type\":\"ASA\",\"color\":\"#000000\",\"eligible\":true,\"same_material\":true}"));
    assert(strstr(response, "{\"slot\":0,\"status\":1,\"type\":\"PA\",\"color\":\"#FFFFFF\",\"eligible\":true,\"same_material\":false}"));
    assert(strstr(response, "{\"slot\":2,\"status\":0,\"type\":\"ASA\",\"color\":\"#696357\",\"eligible\":false,\"same_material\":false}"));
    telemetry.last_rx.tv_sec -= 60; assert(reason("printer_unavailable")); fresh();
    machine.canvas_snapshot_len = 0; assert(reason("canvas_unavailable")); canvas(1, 1, 0, 1);

    /* refusals */
    assert(call(1, "slot=9") == 400 && call(1, "slot=") == 400 && call(1, "slot=12") == 400 && call(1, "") == 400);
    assert(call(1, "slot=2") == 409 && strstr(response, "slot_not_loaded"));   /* the empty slot itself */
    canvas(0, 1, 0, 1); assert(call(1, "slot=0") == 409 && strstr(response, "slot_not_loaded")); canvas(1, 1, 0, 1);
    canvas(1, 2, 0, 1); assert(call(1, "slot=1") == 409); canvas(1, 1, 0, 1);  /* loaded in the head: not a runout */
    assert(sends == 0);

    /* continue from slot 2 (channel 1): T0 moves, T1 (already on channel 1 after its refill) stays */
    assert(call(1, "slot=1") == 202 && strstr(response, "\"accepted\":true"));
    assert(call(1, "slot=1") == 409 && strstr(response, "busy"));
    runout_tick(&machine);
    assert(sends == 1 && strstr(sent, "\"id\":204,\"method\":\"gcode/script\""));
    assert(strstr(sent, "\"script\":\"CANVAS_SET_COLOR_TABLE T=0,1 ID=0,0 CHANNEL=1,1 COLOR=0x696357,0x735DF9\\nM109 S270\\nRESUME\""));
    assert(runout.stage == RO_DONE);
    assert(call(0, NULL) == 200 && strstr(response, "\"job\":{\"state\":\"done\",\"slot\":1,\"from\":2,\"automatic\":false"));
    reset_job();

    /* a refused script is reported */
    reply_text = "{\"id\":204,\"error\":{\"message\":\"no\"}}";
    assert(call(1, "slot=0") == 202); runout_tick(&machine);
    assert(runout.stage == RO_FAILED && strstr(runout.message, "refused"));
    reply_text = "{\"id\":204,\"result\":{}}"; reset_job();

    /* the nozzle target is gone: the new slot's range, RESUME restores the print's own */
    telemetry.values[U_EG] = 0;
    assert(call(1, "slot=3") == 202); runout_tick(&machine);
    assert(strstr(sent, "M109 S230\\nRESUME")); reset_job();
    telemetry.values[U_EG] = 270;

    /* ---- a queued continue is checked again before its first byte, and dropped if the runout moved on */
    sends = 0;
    assert(call(1, "slot=1") == 202); strcpy(telemetry.print_state, "printing"); fresh();   /* resumed elsewhere */
    runout_tick(&machine); assert(sends == 0 && runout.stage == RO_FAILED && strstr(runout.message, "Not sent") && strstr(runout.message, "not_paused"));
    strcpy(telemetry.print_state, "paused"); reset_job();
    assert(call(1, "slot=1") == 202); event(1211, 1);                                       /* a new runout event */
    runout_tick(&machine); assert(sends == 0 && runout.stage == RO_FAILED && strstr(runout.message, "runout_changed"));
    reset_job();
    assert(call(1, "slot=1") == 202); canvas(1, 0, 0, 1);                                   /* the chosen slot was pulled */
    runout_tick(&machine); assert(sends == 0 && runout.stage == RO_FAILED && strstr(runout.message, "slot_not_loaded"));
    canvas(1, 1, 0, 1); reset_job();
    assert(call(1, "slot=1") == 202);                                                       /* another runout line for this print */
    write_file(current, L("[mmu.cpp:3640] [2024] canvas check filament break, enter secondary confirmation!  channel 2"), "a");
    runout_tick(&machine); assert(sends == 0 && runout.stage == RO_FAILED && strstr(runout.message, "runout_changed"));
    reset_job(); event(1211, 1);
    assert(call(1, "slot=1") == 202); runout_tick(&machine); assert(sends == 1 && runout.stage == RO_DONE); /* unchanged: sent */
    reset_job(); sends = 0;

    /* ---- the real transport: one absolute reply deadline, fixed when the request is written */
    {
        char sock[320]; snprintf(sock, sizeof(sock), "%s/native.sock", dir);
        printer_socket(sock); const char *saved = object_query_path; object_query_path = sock; plates_uds = uds_query_json;
        /* silence */
        assert(call(1, "slot=1") == 202);
        runout_tick(&machine); accept_peer();
        for (int i = 0; i < 50 && !runout.reply_deadline_set; ++i) { runout_tick(&machine); usleep(2000); }
        assert(runout.reply_deadline_set && drain() > 0);
        long long deadline = runout.x.deadline, now = monotonic_ms();
        assert(deadline > now + RUNOUT_REPLY_MS - 5000 && deadline <= now + RUNOUT_REPLY_MS);
        usleep(20000); for (int i = 0; i < 20; ++i) runout_tick(&machine);
        assert(runout.stage == RO_RUNNING && runout.x.deadline == deadline);                  /* never moved */
        /* trickling bytes that never finish a frame do not move it either */
        for (int i = 0; i < 5; ++i) { assert(send(peer, "{\"id\":7,", 8, 0) == 8); usleep(2000); runout_tick(&machine); }
        assert(runout.stage == RO_RUNNING && runout.x.deadline == deadline);
        /* nine minutes and 59 seconds later, still silent: a tick must not push the deadline out again */
        long long almost = monotonic_ms() + 1000; runout.x.deadline = almost;
        for (int i = 0; i < 5; ++i) runout_tick(&machine);
        assert(runout.stage == RO_RUNNING && runout.x.deadline == almost);
        runout.x.deadline = monotonic_ms() - 1;                                               /* ten minutes later */
        runout_tick(&machine); assert(runout.stage == RO_FAILED && strstr(runout.message, "within 10 minutes"));
        reset_job(); event(1211, 1);
        /* a reply that arrives in fragments, after other frames, completes the job */
        assert(call(1, "slot=1") == 202);
        runout_tick(&machine); accept_peer();
        for (int i = 0; i < 50 && !runout.reply_deadline_set; ++i) { runout_tick(&machine); usleep(2000); }
        assert(drain() > 0);
        const char *parts[] = {"{\"id\":203,\"result\":{}}\003{\"id\":2", "04,\"res", "ult\":{}}", "\003"};
        for (int i = 0; i < 4; ++i) { assert(send(peer, parts[i], strlen(parts[i]), 0) == (ssize_t)strlen(parts[i])); usleep(2000); runout_tick(&machine); }
        assert(runout.stage == RO_DONE && !runout.message[0]);
        reset_job(); event(1211, 1);
        close(peer); peer = -1; close(listener); unlink(sock);
        object_query_path = saved; plates_uds = fake_rpc;
    }

    /* ---- automatic, "match material only": once per runout, only with the firmware's auto refill on */
    assert(call(2, "maybe") == 400);
    assert(call(2, "on") == 200 && runout_setting());
    runout_match_material = -1; assert(runout_setting());              /* persisted */
    sends = 0; machine.have_auto_refill = 1; machine.auto_refill = 0;
    runout_tick(&machine); assert(sends == 0);
    machine.auto_refill = 1;
    runout_tick(&machine); assert(runout.stage == RO_RUNNING && runout.automatic && runout.slot == 1);
    runout_tick(&machine); assert(sends == 1 && strstr(sent, "CHANNEL=1,1") && runout.stage == RO_DONE);
    runout_tick(&machine); runout_tick(&machine); assert(sends == 1);  /* the same event is not handled twice */
    reset_job(); event(1211, 1);
    canvas(1, 0, 0, 1);                                                 /* no other ASA loaded */
    runout_tick(&machine); assert(sends == 1 && runout.stage == RO_IDLE && !strcmp(runout.auto_note, "no_matching_slot"));
    assert(available());                                                /* the page still offers a choice */
    assert(call(2, "off") == 200 && !runout_setting());

    unlink(older); unlink(current); unlink(settings); rmdir(dir);
    puts("PASS: runout continue: log map and refills, map provenance per print start, incremental reader across rotation, guards, exact script, temperature fallback, unsent job dropped when the runout changes, absolute reply deadline (silence, trickle, fragments), automatic once per runout");
    return 0;
}
