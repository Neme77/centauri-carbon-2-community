#define main cc2_original_main
#include "../src/main.c"
#undef main
#include <assert.h>
static mqtt_client machine;
static console_state commands;
static long long clock_ms = 1000;
static long long fake_clock(void) { return clock_ms; }
static int calibrations, offs, saves, reject_calibration, bad_table;
static char autosave[300], config[300], journal[300], pending[300], unload_pending[300];
static const char *before_autosave =
    "[include printer_dsp.cfg]\n\n"
    "#*# <---------------------- SAVE_CONFIG ---------------------->\n"
    "#*# DO NOT EDIT THIS BLOCK OR BELOW. The contents are auto-generated.\n"
    "#*#\n"
    "#*# [canvas_dev]\n"
    "#*# unload_fila_dist = 560\n";

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "w"); assert(f); fputs(text, f); fclose(f);
}
static char *read_all(const char *path) {
    size_t length = 0; char *text = plates_read_file(path, AUTOSAVE_FILE_MAX, &length); assert(text); return text;
}
static void snapshot(double nozzle, double target, double bed, double bed_target) {
    telemetry.values[U_ET] = nozzle; telemetry.values[U_EG] = target;
    telemetry.values[U_BT] = bed; telemetry.values[U_BG] = bed_target; telemetry.messages++;
    clock_gettime(CLOCK_MONOTONIC, &telemetry.last_rx);
    machine.last_message = time(NULL);
}
/* The firmware's SAVE_CONFIG writes the part of autosave.cfg before the marker plus the block it loaded at boot
 * (here unload_fila_dist 560) with its own later changes (the table NOZZLE_TEMP_CALIBRATE set): edits made to the
 * file since boot are lost, as on the printer. */
static int memory_table;
static void firmware_save(void) {
    char table[1024]; size_t used = 0;
    FILE *f = fopen(autosave, "w"); assert(f);
    fprintf(f, "%s", before_autosave);
    if (memory_table) {
        for (int t = 130; t <= 320; t += 10) {
            double v = 0.00075 * (t - 140) - 0.01;
            if (bad_table && t == 250) v += 0.05; /* a blob of ooze */
            used += (size_t)snprintf(table + used, sizeof(table) - used, "%s%.6f", t == 130 ? "" : ", ", v);
        }
        fprintf(f, "#*#\n#*# [z_compensation]\n#*# nozzle_calibrate_start = 130.000000\n#*# nozzle_calibrate_diff = 10.000000\n"
                   "#*# nozzle_calibrate_datas = %s\n", table);
    }
    fclose(f);
}
static int fake_rpc(const char *query, char **reply, size_t *length) {
    const char *text = "{\"id\":204,\"result\":{}}";
    if (strstr(query, "NOZZLE_TEMP_CALIBRATE START=130 END=320 DIFF=10 DELAY=60 SAMPLES=5")) {
        calibrations++; snapshot(320, 320, 25, 0);
        if (reject_calibration) text = "{\"id\":204,\"error\":{\"message\":\"Probe triggered prior to movement\"}}";
        else memory_table = 1;
    } else if (strstr(query, "M104 S0")) { offs++; snapshot(300, 0, 25, 0); }
    else if (strstr(query, "SAVE_CONFIG")) { saves++; firmware_save(); }
    else assert(0);
    *reply = strdup(text); assert(*reply); *length = strlen(text); return 0;
}
static const char *auth = "POST /api/nozzle-expansion/start HTTP/1.1\r\nX-CC2-LAN: fixture-confirmation\r\n\r\n";
static const char *no_auth = "POST /api/nozzle-expansion/start HTTP/1.1\r\n\r\n";
static void call(int which, const char *request, const char *body, int expected) {
    int pair[2]; char response[8192], status[32]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    if (which == 0) nx_start_response(pair[0], request, body, strlen(body), &machine);
    else if (which == 1) nx_factory_response(pair[0], &machine, body, strlen(body));
    else if (which == 2) nx_restart_response(pair[0], &machine, body, strlen(body));
    else nx_response(pair[0]);
    close(pair[0]); ssize_t n = read(pair[1], response, sizeof(response) - 1); assert(n > 0); response[n] = 0; close(pair[1]);
    snprintf(status, sizeof(status), "HTTP/1.1 %d", expected);
    if (!strstr(response, status)) { fprintf(stderr, "expected %d for '%s':\n%s\n", expected, body, response); assert(0); }
}
static void start(const char *body, int expected) { call(0, auth, body, expected); }
static void reset(void) {
    nx_end("", ""); nx.result = ""; nx.detail[0] = 0; memset(&nx.measured, 0, sizeof(nx.measured));
    calibrations = offs = saves = reject_calibration = bad_table = 0; clock_ms = 1000;
    machine.machine_status = 1; machine.print_enabled = 0; strcpy(telemetry.print_state, "standby");
    snapshot(25, 0, 25, 0); write_file(autosave, before_autosave); unlink(pending); unlink(journal); unlink(unload_pending);
    memory_table = 0; telemetry.connections = 1;
}
static void tick(void) { clock_ms += 1500; nx_tick(&machine); }
/* An exchange the real nonblocking transport has opened for the queued request but not finished sending: `sent`
 * bytes already reached the firmware's end (`peer`); the rest waits on a pending connect or a full socket. */
static int peer = -1;
static void open_exchange(int connecting, size_t sent, int full) {
    int sv[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, sv));
    assert(!fcntl(sv[0], F_SETFL, O_NONBLOCK) && !fcntl(sv[1], F_SETFL, O_NONBLOCK));
    plates_exchange *x = &nx.exchange; plates_exchange_close(x);
    x->fd = sv[0]; x->connecting = connecting; x->id = 204; x->deadline = monotonic_ms() + 60000;
    x->buffer = malloc(PLATES_REPLY_MAX + 1); assert(x->buffer);
    snprintf(x->request, sizeof(x->request), "%s", nx.query);
    if (sent) { assert(send(sv[0], nx.query, sent, 0) == (ssize_t)sent); x->sent = sent; }
    if (full) { char fill[4096]; memset(fill, 'x', sizeof(fill)); while (send(sv[0], fill, sizeof(fill), MSG_DONTWAIT) > 0) {} }
    peer = sv[1];
}
/* Everything that reached the firmware once the connection is closed, filler left out; fails if it is still open. */
static size_t drain(char *out, size_t capacity) {
    size_t used = 0; char chunk[4096]; ssize_t n;
    while ((n = read(peer, chunk, sizeof(chunk))) > 0)
        for (ssize_t i = 0; i < n; i++) if (chunk[i] != 'x') { assert(used < capacity); out[used++] = chunk[i]; }
    assert(n == 0); close(peer); peer = -1; return used;
}
static void run(int ticks) { for (int i = 0; i < ticks && nx_active(); i++) { clock_ms += 1500; nx_tick(&machine); } }

int main(void) {
    char temporary[] = "/tmp/cc2-nx-XXXXXX"; assert(mkdtemp(temporary));
    snprintf(autosave, sizeof(autosave), "%s/autosave.cfg", temporary);
    snprintf(config, sizeof(config), "%s/printer_dsp.cfg", temporary);
    snprintf(journal, sizeof(journal), "%s/session", temporary);
    snprintf(pending, sizeof(pending), "%s/pending", temporary);
    snprintf(unload_pending, sizeof(unload_pending), "%s/unload-pending", temporary); canvas_unload_pending_path = unload_pending;
    printer_autosave_path = autosave; printer_config_path = config; nx_journal = journal; nx_pending_path = pending;
    nx_clock = fake_clock;
    write_file(config, "[extruder]\nmax_temp=365\n[z_compensation]\nnozzle_temp_start=100\n"
                       "nozzle_calibrate_start = 100.000000\nnozzle_calibrate_diff = 10.000000\n"
                       "# nozzle_calibrate_datas = 9, 9, 9\n"
                       "nozzle_calibrate_datas = -0.039173, -0.033207, -0.027035, -0.017856\n"
                       "fix_ref_temperature=140.0\n[probe]\nz_offset=0\n");

    /* Vendor configuration: commented lines ignored, the reference temperature read. */
    { size_t n = 0; char *text = read_all(config); nx_table t; int section;
      n = strlen(text); nx_configured(text, n, &t, &section);
      assert(section && t.known && t.start == 100 && t.diff == 10 && t.count == 4 && t.ref == 140 && fabs(t.values[0] + 0.039173) < 1e-9);
      free(text); }

    /* autosave.cfg: add, replace and remove the section; every other byte stays. */
    { nx_table t; size_t s, e, n = 0, m = 0; int found;
      const char *section = "#*# [z_compensation]\n#*# nozzle_calibrate_start = 130\n#*# nozzle_calibrate_diff = 10\n#*# nozzle_calibrate_datas = 1, 2, 3\n";
      char *added = nx_autosave_replace(before_autosave, strlen(before_autosave), section, strlen(section), &n);
      assert(added && nx_autosave(added, n, &t, &s, &e, &found) == 1 && found && t.known && t.count == 3 && t.start == 130);
      assert(!strncmp(added, before_autosave, strlen(before_autosave)));
      char *removed = nx_autosave_replace(added, n, NULL, 0, &m);
      assert(removed && m == strlen(before_autosave) && !memcmp(removed, before_autosave, m));
      free(added); free(removed);
      const char *odd = "#*# <---------------------- SAVE_CONFIG ---------------------->\n#*#  [z_compensation]\n#*# a = 1\n";
      assert(nx_autosave(odd, strlen(odd), &t, &s, &e, &found) == -1);
      assert(!nx_autosave_replace("no marker\n", 10, NULL, 0, &n)); }

    /* Plausibility: the stock table's shape passes; ooze, a falling table or a wrong grid do not. */
    { nx_table t = {.known = 1, .start = 130, .diff = 10, .count = 20}; double slope, residual;
      for (int i = 0; i < 20; i++) t.values[i] = 0.00074 * (130 + 10 * i) + (i % 2 ? 0.002 : -0.002);
      assert(!nx_validate(&t, 130, 10, 20, &slope, &residual) && fabs(slope - 0.00074) < 0.00005 && residual < 0.004);
      t.values[12] += 0.03; assert(nx_validate(&t, 130, 10, 20, &slope, &residual));
      t.values[12] -= 0.03; assert(nx_validate(&t, 140, 10, 20, &slope, &residual));
      for (int i = 0; i < 20; i++) t.values[i] = -0.0007 * i;
      assert(nx_validate(&t, 130, 10, 20, &slope, &residual)); }

    assert(!pthread_mutex_init(&commands.lock, NULL));
    machine.connected = machine.registered = machine.have_machine_status = 1; strcpy(machine.password, "fixture-confirmation");
    uds_init(&telemetry); int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair)); telemetry.fd = pair[0]; telemetry.ready = 1;
    telemetry.have_print_state = 1; telemetry.connections = 1;
    telemetry.present = (1u << U_ET) | (1u << U_BT) | (1u << U_BG) | (1u << U_EG) | (1u << U_LIVE_SPEED);
    plates_uds = fake_rpc;

    /* Off by default, enforced at mutation endpoints as well as in the UI. */
    reset();experiments.nozzle_expansion=0;start("130:320:10",409);call(1,auth,"FACTORY",409);
    assert(!calibrations&&!saves&&!nx_active());experiments.nozzle_expansion=1;
    /* Requests that must not start anything. */
    reset();
    call(0, no_auth, "130:320:10", 401);
    start("130:320:7", 400); start("130:120:10", 400); start("130:350:10", 400); start("90:320:10", 400);
    start("130:325:10", 400); start("0130:320:10", 400); start("130:320:10 ", 400);
    start("140:320:10", 400); start("136:320:5", 400); /* the 140 C reference must lie inside the table */
    snapshot(25, 0, 60, 0); start("130:320:10", 409); snapshot(25, 0, 25, 0);
    snapshot(200, 200, 25, 0); start("130:320:10", 409); snapshot(25, 0, 25, 0);
    strcpy(telemetry.print_state, "printing"); start("130:320:10", 409);
    strcpy(telemetry.print_state, "paused"); start("130:320:10", 409); strcpy(telemetry.print_state, "standby");
    machine.machine_status = 2; start("130:320:10", 409); machine.machine_status = 1;
    strcpy(telemetry.print_state, "error"); start("130:320:10", 409); strcpy(telemetry.print_state, "standby"); /* allow-list */
    /* A saved change still waiting for a restart (here the Canvas pull-back) would be lost to SAVE_CONFIG: refused. */
    { char boot[64]; assert(canvas_unload_boot_id(boot, sizeof(boot))); write_file(unload_pending, boot);
      start("130:320:10", 409); unlink(unload_pending); }
    assert(!calibrations && !nx_active() && access(journal, F_OK));
    assert(nx_budget_ms(130, 330, 10) == (15 + 21 * 8) * 60 * 1000LL);
    /* The vendor flag left set by a finished or cancelled print does not block a start. */
    reset(); machine.print_enabled = 1; strcpy(telemetry.print_state, "cancelled"); start("130:320:10", 202);
    assert(nx.stage == NX_RUNNING); run(10); assert(!nx_active() && !strcmp(nx.result, "saved"));

    /* A good run: calibrate, nozzle off, SAVE_CONFIG, the table kept, a restart pending. */
    reset(); start("130:320:10", 202); assert(nx.stage == NX_RUNNING && !access(journal, F_OK));
    call(2, auth, "RESTART", 409); /* nothing saved yet */
    run(10);
    assert(calibrations == 1 && offs == 1 && saves == 1 && !nx_active());
    assert(!strcmp(nx.result, "saved") && nx.measured.known && nx.measured.count == 20 && fabs(nx.slope - 0.00075) < 1e-6);
    assert(access(journal, F_OK) && nx_pending());
    { char *text = read_all(autosave); assert(strstr(text, "unload_fila_dist = 560") && strstr(text, "[z_compensation]")); free(text); }
    call(3, auth, "", 200);

    /* An implausible table is not kept: autosave.cfg goes back to how the run found it. */
    reset(); bad_table = 1; start("130:320:10", 202); run(10);
    /* ...and the firmware still holds the rejected table in memory, so a restart is required. */
    assert(saves == 1 && !strcmp(nx.result, "rejected") && strstr(nx.detail, "previous table was kept") && nx_pending());
    { char *text = read_all(autosave); assert(!strcmp(text, before_autosave)); free(text); }

    /* A calibration the printer stops still gets its nozzle switched off, and nothing is saved. */
    reset(); reject_calibration = 1; start("130:320:10", 202); run(10);
    assert(calibrations == 1 && offs == 1 && saves == 0 && !strcmp(nx.result, "failed") && nx_pending());

    /* The printer service restarts mid-run (emergency stop): the run is over; the nozzle is confirmed off, nothing saved. */
    reset(); start("130:320:10", 202); telemetry.connections++; run(10);
    assert(!nx_active() && !saves && !strcmp(nx.result, "failed") && strstr(nx.detail, "restarted"));

    /* A print can start while a request is queued but not yet with the firmware. Ownership is checked again at the
     * dispatch boundary of the real nonblocking transport: an unsent request is cancelled, a part-sent one is cut
     * off before its 0x03 terminator (the firmware drops it with the connection), and SAVE_CONFIG never follows. */
    { char got[256]; size_t n;
      /* The calibration itself, its connect still pending: nothing ran, so nothing to restart for. */
      reset(); start("130:320:10", 202); plates_uds = uds_query_json; open_exchange(1, 0, 0);
      strcpy(telemetry.print_state, "printing"); snapshot(25, 0, 25, 0); tick();
      assert(!nx_active() && !calibrations && !strcmp(nx.result, "failed") && strstr(nx.detail, "before the calibration was sent"));
      assert(!nx_pending() && access(journal, F_OK) && nx.exchange.fd < 0 && drain(got, sizeof(got)) == 0);
      /* M104 S0 with zero bytes sent (EAGAIN on the real transport), then a print starts. */
      reset(); plates_uds = fake_rpc; start("130:320:10", 202); tick(); assert(calibrations == 1 && nx.stage == NX_COOLING && !nx.query[0]);
      plates_uds = uds_query_json; nx_command("M104 S0"); open_exchange(0, 0, 1);
      tick(); assert(nx.stage == NX_COOLING && nx.exchange.fd >= 0 && nx.exchange.sent == 0); /* EAGAIN: waits */
      strcpy(telemetry.print_state, "printing"); snapshot(200, 220, 60, 60); tick();
      assert(!nx_active() && !offs && !saves && !strcmp(nx.result, "failed") && strstr(nx.detail, "before the nozzle was switched off"));
      assert(nx_pending() && drain(got, sizeof(got)) == 0);
      /* Part of M104 S0 already sent: the rest and the terminator are never sent. */
      reset(); plates_uds = fake_rpc; start("130:320:10", 202); tick();
      plates_uds = uds_query_json; nx_command("M104 S0"); open_exchange(0, 10, 0);
      strcpy(telemetry.print_state, "printing"); snapshot(200, 220, 60, 60); tick();
      assert(!nx_active() && !offs && nx_pending() && (n = drain(got, sizeof(got))) == 10 && !memcmp(got, "{\"id\":204,", 10) && !memchr(got, 3, n));
      /* A print starting between the nozzle-off acknowledgement and the save stage: SAVE_CONFIG is never queued. */
      reset(); plates_uds = fake_rpc; start("130:320:10", 202); tick(); tick(); assert(offs == 1 && nx.stage == NX_COOLING && nx.off_acked);
      strcpy(telemetry.print_state, "printing"); snapshot(200, 0, 25, 0); tick();
      assert(!nx_active() && !saves && !strcmp(nx.result, "failed") && strstr(nx.detail, "before the table was saved") && nx_pending());
      { char *text = read_all(autosave); assert(!strcmp(text, before_autosave)); free(text); }
      /* SAVE_CONFIG queued but unsent when the print starts: cancelled. */
      reset(); start("130:320:10", 202); tick(); tick(); tick(); assert(nx.stage == NX_SAVING && nx.query[0] && !saves);
      plates_uds = uds_query_json; open_exchange(0, 0, 1);
      strcpy(telemetry.print_state, "printing"); snapshot(200, 0, 25, 0); tick();
      assert(!nx_active() && !saves && strstr(nx.detail, "before the table was saved") && nx_pending() && drain(got, sizeof(got)) == 0);
      /* ... and with no fresh print state it is not sent either: decided again once the state is back. */
      reset(); plates_uds = fake_rpc; start("130:320:10", 202); tick(); tick(); tick(); assert(nx.stage == NX_SAVING);
      plates_uds = uds_query_json; open_exchange(1, 0, 0); telemetry.last_rx.tv_sec = 0; tick();
      assert(nx.stage == NX_COOLING && !nx.query[0] && nx.exchange.fd < 0 && drain(got, sizeof(got)) == 0);
      plates_uds = fake_rpc; snapshot(25, 0, 25, 0); run(5); assert(saves == 1 && !strcmp(nx.result, "saved")); }
    strcpy(telemetry.print_state, "standby");

    /* Other operations wait while it runs. */
    reset(); plates_uds = NULL; start("130:320:10", 202);
    assert(plates_printer_ready(&machine) && !strcmp(plates_printer_ready(&machine), "A nozzle expansion calibration is running"));
    call(1, auth, "FACTORY", 409);
    plates_uds = fake_rpc; nx_end("", ""); unlink(journal);

    /* CC2 Control restarted mid-run: the nozzle is switched off once the printer is idle. */
    { char j[96]; snprintf(j, sizeof(j), "CC2NX2 130 320 10 %lld\n", (long long)time(NULL) - 600);
      reset(); write_file(journal, j); nx_load(); }
    assert(nx.stage == NX_RECOVERY && nx.start == 130 && nx.end == 320 && nx.step == 10 && nx.deadline > clock_ms);
    /* No printer yet: it waits. Then it queues M104 S0 (behind the run, if still going) and needs its reply. */
    telemetry.last_rx.tv_sec = 0; run(3); assert(nx.stage == NX_RECOVERY && !offs);
    experiments.nozzle_expansion=0; /* Recovery must not require the user to re-enable the experiment. */
    snapshot(310, 320, 25, 0); run(10);
    experiments.nozzle_expansion=1;
    assert(offs == 1 && !saves && !nx_active() && !strcmp(nx.result, "interrupted") && access(journal, F_OK) && nx_pending());
    /* A print running by then owns the heater: left alone. */
    { char j[96]; snprintf(j, sizeof(j), "CC2NX2 130 320 10 %lld\n", (long long)time(NULL));
      reset(); write_file(journal, j); nx_load(); }
    strcpy(telemetry.print_state, "printing"); snapshot(250, 250, 100, 100); run(3);
    assert(!offs && !nx_active() && !strcmp(nx.result, "interrupted") && strstr(nx.detail, "print is running"));
    strcpy(telemetry.print_state, "standby");

    /* Back to the vendor table: the saved section goes, a restart is pending. */
    reset(); memory_table = 1; firmware_save(); call(1, auth, "FACTORY", 200);
    { char *text = read_all(autosave); assert(!strcmp(text, before_autosave)); free(text); }
    assert(nx_pending());
    call(1, auth, "nope", 400);
    /* While a nozzle table waits for its restart, console commands and PID saves are refused too:
     * their SAVE_CONFIG would write the firmware's in-memory table over it. */
    { console_state c; console_init(&c, "/tmp/cc2-nx-fixture-no-printer");
      int s = saves;
      const char *blocked[][2] = {{"console", "SAVE_CONFIG"}, {"control", "pid:save"}};
      for (size_t i = 0; i < sizeof(blocked) / sizeof(blocked[0]); i++) {
          int pair[2]; char response[2048]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
          if (!strcmp(blocked[i][0], "console")) console_command_response(pair[0], &c, &machine, blocked[i][1], strlen(blocked[i][1]));
          else control_response(pair[0], &c, &machine, blocked[i][1], strlen(blocked[i][1]));
          close(pair[0]); ssize_t n = read(pair[1], response, sizeof(response) - 1); assert(n > 0); response[n] = 0; close(pair[1]);
          assert(strstr(response, "HTTP/1.1 409") && strstr(response, "nozzle expansion change needs a restart"));
      }
      assert(saves == s); pthread_mutex_destroy(&c.lock); }
    puts("nozzle expansion tests passed");
    return 0;
}
