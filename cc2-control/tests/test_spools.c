#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

/* The printer as spools.h sees it: MQTT job state and Canvas trays, the printer
 * service's print_stats through the UDS cache, and two clocks the test moves. */
static long long fake_ms = 1000000;
static time_t fake_now = 1790000000;
static long long clock_fake(void) { return fake_ms; }
static time_t wall_fake(time_t *t) { if (t) *t = fake_now; return fake_now; }
static void advance(long long ms) { fake_ms += ms; fake_now += (time_t)(ms / 1000); }

static mqtt_client mqtt;

static void printer(const char *state, const char *file, int tray) {
    mqtt.connected = mqtt.registered = 1;
    mqtt.have_machine_status = 1;
    mqtt.machine_status = !strcmp(state, "printing") || !strcmp(state, "paused") ? 2 : 1;
    snprintf(mqtt.print_state, sizeof(mqtt.print_state), "%s", state);
    snprintf(mqtt.filename, sizeof(mqtt.filename), "%s", file);
    mqtt.last_message = fake_now;
    mqtt.have_canvas_active_tray = 1;
    mqtt.canvas_active_tray_id = tray;
}

static void service(const char *file, double used, double duration) {
    telemetry.fd = 0; telemetry.ready = 1;
    clock_gettime(CLOCK_MONOTONIC, &telemetry.last_rx);
    snprintf(telemetry.filename, sizeof(telemetry.filename), "%s", file);
    telemetry.have_filename = file[0] != 0;
    telemetry.values[U_FILAMENT_USED] = used;
    telemetry.values[U_TOTAL_DURATION] = duration;
    telemetry.present |= (UINT32_C(1) << U_FILAMENT_USED) | (UINT32_C(1) << U_TOTAL_DURATION);
}

/* The printer service's Canvas channel, which arrives with filament_used. */
static void channel(int cid) {
    telemetry.values[U_CANVAS_CHANNEL] = cid;
    telemetry.present |= UINT32_C(1) << U_CANVAS_CHANNEL;
}

/* Four trays: status and filament of each, as the printer publishes them. */
static int tray_status[4] = {1, 1, 1, 1};
static const char *tray_type[4] = {"PLA", "PLA", "PETG", "PLA"};
static const char *tray_color[4] = {"#2850DF", "#09CC3A", "#FFFFFF", "#F72221"};
static void publish_trays(void) {
    char list[2048]; size_t used = 0;
    for (int i = 0; i < 4; ++i)
        used += (size_t)snprintf(list + used, sizeof(list) - used,
            "%s{\"brand\":\"ELEGOO\",\"filament_code\":\"0x0006\",\"filament_color\":\"%s\",\"filament_name\":\"%s Matte\","
            "\"filament_type\":\"%s\",\"max_nozzle_temp\":230,\"min_nozzle_temp\":190,\"status\":%d,\"tray_id\":%d}",
            i ? "," : "", tray_color[i], tray_type[i], tray_type[i], tray_status[i], i);
    snprintf(mqtt.canvas_snapshot, sizeof(mqtt.canvas_snapshot),
        "{\"id\":1,\"method\":6000,\"result\":{\"canvas_info\":{\"canvas_list\":[{\"canvas_id\":0,\"connected\":1,"
        "\"tray_list\":[%s]}]}}}", list);
    mqtt.canvas_snapshot_len = strlen(mqtt.canvas_snapshot);
    mqtt.canvas_revision++;
}

static void tick(void) { spools_tick(&mqtt); }

static char response[300000];
static int call(const char *route, const char *body) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    size_t length = body ? strlen(body) : 0;
    if (!strcmp(route, "get")) spools_get_response(pair[0], &mqtt);
    else if (!strcmp(route, "enable")) spools_enable_response(pair[0], &mqtt, body, length);
    else if (!strcmp(route, "save")) spools_save_response(pair[0], body, length);
    else if (!strcmp(route, "delete")) spools_delete_response(pair[0], body, length);
    else if (!strcmp(route, "assign")) spools_assign_response(pair[0], body, length);
    else if (!strcmp(route, "dismiss")) spools_dismiss_response(pair[0], body, length);
    else if (!strcmp(route, "adjust")) spools_adjust_response(pair[0], body, length);
    else assert(!"unknown route");
    close(pair[0]);
    size_t used = 0; ssize_t n;
    while ((n = recv(pair[1], response + used, sizeof(response) - 1 - used, 0)) > 0) used += (size_t)n;
    response[used] = 0; close(pair[1]);
    return atoi(strchr(response, ' ') + 1);
}

static const char *body_of(void) { const char *b = strstr(response, "\r\n\r\n"); assert(b); return b + 4; }

/* Creates a spool and returns its id. */
static const char *create(const char *fields) {
    static char ids[8][SPOOL_ID_LEN + 1]; static int next;
    assert(call("save", fields) == 201);
    const char *id = strstr(body_of(), "\"id\":\"") + 6;
    char *out = ids[next++ % 8];
    memcpy(out, id, SPOOL_ID_LEN); out[SPOOL_ID_LEN] = 0;
    assert(spool_find(out));
    return out;
}

static void assign(int slot, const char *id) {
    char body[96]; snprintf(body, sizeof(body), "slot=%d\nspool=%s", slot, id);
    assert(call("assign", body) == 200);
}

static double grams(const char *id, double mm) { return spool_grams(spool_find(id), mm); }
static int near(double a, double b) { return fabs(a - b) < 1e-6; }
static int saved_near(double a, double b) { return fabs(a - b) < 0.001; } /* the file keeps milligrams */

static void reset(void) {
    unlink(spools_path);
    memset(&mqtt, 0, sizeof(mqtt));
    memset(&telemetry, 0, sizeof(telemetry));
    telemetry.fd = -1;
    for (int i = 0; i < 4; ++i) tray_status[i] = 1;
    tray_type[0] = "PLA"; tray_color[0] = "#2850DF"; tray_type[1] = "PLA"; tray_color[1] = "#09CC3A";
    spools_canvas_seen = 0;
    spools_load();
    assert(spools_available && !spools.enabled && !spools.count);
    printer("complete", "", -1);
    publish_trays();
}

static void enable(void) {
    assert(call("enable", "on") == 200 && spools.enabled);
    tick();
}

static void test_values(void) {
    assert(spool_text_ok("Elegoo PLA Matte", 96, 0) && spool_text_ok("Синий PLA №2", 96, 0) && spool_text_ok("", 96, 1));
    assert(!spool_text_ok("", 96, 0) && !spool_text_ok(" lead", 96, 0) && !spool_text_ok("quo\"te", 96, 0) &&
           !spool_text_ok("back\\slash", 96, 0) && !spool_text_ok("tab\there", 96, 0) && !spool_text_ok("\xc0\xaf", 96, 0));
    char out[32];
    const char *raw = "  a\"b\\c\x01\xd0\x96 ";
    spool_clean(out, sizeof(out), raw, strlen(raw));
    assert(!strcmp(out, "a_b_c_\xd0\x96") && spool_text_ok(out, 96, 0));
    spool_clean(out, 6, "abcd\xd0\x96", 6); /* cut before a character that does not fit */
    assert(!strcmp(out, "abcd"));
    char color[8];
    assert(spool_color(color, "#2850df", 7) && !strcmp(color, "#2850DF"));
    assert(spool_color(color, "#2850DFFF", 9) && !strcmp(color, "#2850DF"));
    assert(!spool_color(color, "2850DF", 6) && !spool_color(color, "#2850DG", 7));
    double v;
    assert(spool_number("1000", 1, 20000, &v) && v == 1000 && !spool_number("1e3", 1, 20000, &v) &&
           !spool_number("", 0, 1, &v) && !spool_number("-1", 0, 1, &v) && !spool_number("nan", 0, 1, &v));
    /* One metre of 1.75 mm PLA weighs 2.98 g. */
    assert(fabs(spool_grams_at(1000, 1.75, 1.24) - 2.9826) < 0.0001);
    /* A product line fits a tray that reports only its base type, or its own name. */
    spool_entry line = {.material = "PLA Basic", .color = "#262626"};
    spool_tray plain = {.have = 1, .status = 1, .type = "PLA", .name = "PLA", .color = "#262626"};
    assert(spool_fits(&line, &plain));
    snprintf(line.material, sizeof(line.material), "PLA+");
    assert(spool_fits(&line, &plain));
    snprintf(line.material, sizeof(line.material), "Rapid PLA+");
    snprintf(plain.name, sizeof(plain.name), "Rapid PLA+");
    assert(spool_fits(&line, &plain));
    snprintf(line.material, sizeof(line.material), "PLAX");
    assert(!spool_fits(&line, &plain));
    snprintf(line.material, sizeof(line.material), "PETG Basic");
    assert(!spool_fits(&line, &plain));
    snprintf(line.material, sizeof(line.material), "PLA Matte");
    snprintf(plain.color, sizeof(plain.color), "#FFFFFF");
    assert(!spool_fits(&line, &plain));
    puts("PASS: spool text, colours, numbers and grams");
}

static void test_library(void) {
    reset();
    assert(call("get", NULL) == 200 && strstr(body_of(), "\"enabled\":false") && strstr(body_of(), "\"spools\":[]"));
    const char *id = create("name=Blue\nbrand=ELEGOO\nmaterial=PLA\ncolor=#2850df\nnet=1000\ndensity=1.24\ntare=150");
    spool_entry *s = spool_find(id);
    assert(near(s->remaining, 1000) && !strcmp(s->color, "#2850DF") && near(s->diameter, 1.75) && near(s->tare, 150));
    assert(spools.log_count == 1 && spools.log[0].kind == 'n' && near(spools.log[0].grams, 1000));
    assert(call("save", "material=PLA\ncolor=#123456\nnet=750\nremaining=200") == 201);
    assert(call("save", "material=PLA\ncolor=123456") == 400);
    assert(call("save", "material=PLA\ncolor=#123456\nname=bad\"name") == 400);
    assert(call("save", "material=PLA\ncolor=#123456\nnet=50000") == 400);
    assert(call("save", "color=#123456") == 400);
    assert(call("save", "material=PLA\ncolor=#123456\nnet") == 400);
    assert(call("save", "material=PLA\nmaterial=PETG\ncolor=#123456") == 400);
    char body[160];
    snprintf(body, sizeof(body), "id=%s\nname=Blue spool\nlow=120", id);
    assert(call("save", body) == 200 && !strcmp(s->name, "Blue spool") && near(s->low, 120) && near(s->remaining, 1000));
    snprintf(body, sizeof(body), "id=%s\nremaining=10", id);
    assert(call("save", body) == 400 && near(s->remaining, 1000));
    snprintf(body, sizeof(body), "id=%s\ngross=790", id);
    assert(call("adjust", body) == 200 && near(s->remaining, 640));
    assert(spools.log[spools.log_count - 1].kind == 'c' && near(spools.log[spools.log_count - 1].grams, -360));
    snprintf(body, sizeof(body), "id=%s\nremaining=600", id);
    assert(call("adjust", body) == 200 && near(s->remaining, 600));
    snprintf(body, sizeof(body), "id=%s\ngross=100", id);
    assert(call("adjust", body) == 400);
    assert(call("save", "id=0123456789abcdef\nname=x") == 404);
    /* The file holds it all. */
    char saved_id[SPOOL_ID_LEN + 1]; snprintf(saved_id, sizeof(saved_id), "%s", id);
    spools_load();
    assert(spools_available && spools.count == 2 && (s = spool_find(saved_id)) && near(s->remaining, 600) &&
           !strcmp(s->name, "Blue spool") && spools.log_count == 4);
    puts("PASS: spool library create, edit, weigh-in and reload");
}

static void test_print(void) {
    reset();
    const char *a = create("material=PLA\ncolor=#2850DF\nnet=1000");
    const char *b = create("material=PLA\ncolor=#09CC3A\nnet=1000\ndensity=1.30");
    assign(0, a); assign(1, b);
    /* Disabled: a print changes nothing. */
    printer("printing", "cube.gcode", 0); service("local/cube.gcode", 0, 1); tick();
    service("local/cube.gcode", 500, 30); tick();
    assert(near(spool_find(a)->remaining, 1000) && !spools.job.active);
    printer("complete", "cube.gcode", -1); tick();
    enable();
    assert(!spools.slots[0].question && !strcmp(spools.slots[0].spool, a));
    /* A fresh print counts from its start, on the tray that feeds. */
    printer("printing", "cube.gcode", 0); service("local/cube.gcode", 120, 10); tick();
    assert(spools.job.active && near(spool_find(a)->remaining, 1000 - grams(a, 120)));
    service("local/cube.gcode", 1000, 60); tick();
    assert(near(spool_find(a)->remaining, 1000 - grams(a, 1000)));
    printer("printing", "cube.gcode", 1);
    service("local/cube.gcode", 1500, 90); tick();
    assert(near(spool_find(b)->remaining, 1000 - grams(b, 500)));
    service("local/cube.gcode", 1490, 91); tick(); /* a retraction gives filament back */
    assert(near(spool_find(b)->remaining, 1000 - grams(b, 490)));
    /* Between trays the extruder pulls in the next filament: that waits for the tray named next. */
    printer("printing", "cube.gcode", -1); service("local/cube.gcode", 1500, 95); tick();
    assert(near(spool_find(b)->remaining, 1000 - grams(b, 490)) && near(spools.job.between, 10));
    /* The flow override scales what really leaves the spool. */
    telemetry.values[U_FLOW_FACTOR] = 0.5; telemetry.present |= UINT32_C(1) << U_FLOW_FACTOR;
    printer("printing", "cube.gcode", 1); service("local/cube.gcode", 1700, 100); tick();
    assert(near(spool_find(b)->remaining, 1000 - grams(b, 600)));
    telemetry.present &= ~(UINT32_C(1) << U_FLOW_FACTOR);
    /* Paused: print_stats stop, nothing ends. */
    printer("paused", "cube.gcode", 1); tick(); advance(60000); tick();
    assert(spools.job.active);
    /* The end is logged after a grace period that lets the last reading in; with no tray named it waits,
     * and the end gives it to the last tray. */
    printer("complete", "cube.gcode", -1); tick();
    service("local/cube.gcode", 1710, 120); tick();
    assert(spools.job.active && near(spools.job.between, 10) && near(spool_find(b)->remaining, 1000 - grams(b, 600)));
    advance(9000); tick();
    assert(!spools.job.active);
    const spool_event *e = &spools.log[spools.log_count - 2], *f = &spools.log[spools.log_count - 1];
    assert(e->kind == 'p' && !strcmp(e->spool, a) && e->slot == 0 && near(e->mm, 1000) && !strcmp(e->result, "complete") &&
           !strcmp(e->job, "cube.gcode") && near(e->grams, -grams(a, 1000)));
    assert(f->kind == 'p' && !strcmp(f->spool, b) && f->slot == 1 && near(f->mm, 610) && near(f->grams, -grams(b, 610)));
    /* Saved at the end. */
    spools_load();
    assert(saved_near(spool_find(b)->remaining, 1000 - grams(b, 610)) && !spools.job.active);
    puts("PASS: a print is charged per feeding tray, with retractions, flow and a clean end");
}

static void test_first_tray(void) {
    reset();
    const char *w = create("material=PETG\ncolor=#FFFFFF\nnet=1000\ndensity=1.27");
    const char *x = create("material=TPU\ncolor=#000000\nnet=1000\ndensity=1.21");
    assign(2, w); assign(4, x);
    enable();
    /* The printer names the first tray once it has loaded it: the load's extrusion moves to that tray. */
    printer("printing", "start.gcode", -1); service("local/start.gcode", 0, 1); tick();
    service("local/start.gcode", 18, 40); tick();
    assert(near(spool_find(x)->remaining, 1000 - grams(x, 18)));
    printer("printing", "start.gcode", 2); service("local/start.gcode", 198, 70); tick();
    assert(near(spool_find(x)->remaining, 1000) && near(spool_find(w)->remaining, 1000 - grams(w, 198)));
    assert(near(spools.job.mm[SPOOL_EXTERNAL], 0) && near(spools.job.mm[2], 198));
    /* Later, between trays, the moves wait for the next tray; the print ends first, so the last tray gets them. */
    printer("printing", "start.gcode", -1); service("local/start.gcode", 250, 80); tick();
    assert(near(spools.job.mm[2], 198) && near(spools.job.between, 52) && near(spools.job.mm[SPOOL_EXTERNAL], 0));
    /* A restart keeps what waits. */
    assert(spools_save() == 0);
    spools_load(); spools_canvas_seen = 0;
    assert(spools.job.active && saved_near(spools.job.between, 52) && spools.job.last_tray == 2);
    printer("complete", "start.gcode", -1); tick(); advance(9000); tick();
    const spool_event *e = &spools.log[spools.log_count - 1];
    assert(e->kind == 'p' && !strcmp(e->spool, w) && e->slot == 2 && near(e->mm, 250));
    assert(!strcmp(spools.log[spools.log_count - 2].spool, x) && spools.log[spools.log_count - 2].kind == 'n');
    puts("PASS: what a print extrudes before the printer names its first tray is that tray's");
}

static void test_service_channel(void) {
    reset();
    const char *a = create("material=PLA\ncolor=#2850DF\nnet=1000");
    const char *r = create("material=PLA\ncolor=#F72221\nnet=1000");
    assign(0, a); assign(3, r);
    enable();
    /* MQTT still names tray 0 when the printer service has switched to tray 3: the purge is tray 3's. */
    printer("printing", "two.gcode", 0); service("local/two.gcode", 0, 1); channel(0); tick();
    service("local/two.gcode", 100, 20); tick();
    channel(3); service("local/two.gcode", 250, 40); tick();
    assert(near(spools.job.mm[0], 100) && near(spools.job.mm[3], 150));
    /* Tray 3 is cut and pulled back unseen; the extruder pulls in tray 0's filament before the printer names it. */
    channel(-1); service("local/two.gcode", 267.5, 45); tick();
    assert(near(spools.job.mm[3], 150) && near(spools.job.between, 17.5));
    channel(0); service("local/two.gcode", 300, 50); tick();
    assert(near(spools.job.mm[0], 150) && near(spools.job.between, 0) && near(spools.job.mm[3], 150));
    /* At the end MQTT drops the file name while it still says printing; the printer service says complete. */
    printer("printing", "", -1);
    snprintf(telemetry.print_state, sizeof(telemetry.print_state), "complete");
    telemetry.have_print_state = 1;
    tick(); advance(9000); tick();
    assert(!spools.job.active);
    const spool_event *e = &spools.log[spools.log_count - 1];
    assert(e->kind == 'p' && !strcmp(e->spool, r) && e->slot == 3 && near(e->mm, 150) && !strcmp(e->result, "complete"));
    assert(near(spool_find(a)->remaining, 1000 - grams(a, 150)));
    puts("PASS: the printer service's channel and end state win over MQTT's");
}

static void test_restart_and_reprint(void) {
    reset();
    const char *a = create("material=PLA\ncolor=#2850DF\nnet=1000");
    assign(0, a);
    enable();
    printer("printing", "box.gcode", 0); service("local/box.gcode", 0, 2); tick();
    service("local/box.gcode", 2000, 400); tick();
    assert(spools_save() == 0);
    double saved = spool_find(a)->remaining;
    /* CC2 Control restarts; the print went on meanwhile. */
    spools_load(); spools_canvas_seen = 0;
    assert(spools.job.active && near(spools.job.used, 2000));
    service("local/box.gcode", 2600, 700); tick();
    assert(saved_near(spool_find(a)->remaining, saved - grams(a, 600)));
    /* The same file again: print_stats restart, a new print is counted from zero. */
    service("local/box.gcode", 30, 3); tick();
    assert(spools.job.active && near(spools.job.used, 30));
    assert(saved_near(spool_find(a)->remaining, saved - grams(a, 630)));
    const spool_event *e = &spools.log[spools.log_count - 1];
    assert(e->kind == 'p' && near(e->mm, 2600));
    /* Joining a print that has long been running counts from now. */
    printer("complete", "box.gcode", -1); tick(); advance(9000); tick();
    assert(!spools.job.active);
    double before = spool_find(a)->remaining;
    printer("printing", "late.gcode", 0); service("local/late.gcode", 5000, 3600); tick();
    service("local/late.gcode", 5100, 3610); tick();
    assert(near(spool_find(a)->remaining, before - grams(a, 100)));
    puts("PASS: restarts mid-print resume the count, reprints and late joins are not double counted");
}

static void test_questions(void) {
    reset();
    const char *a = create("material=PLA\ncolor=#09CC3A\nnet=1000");
    const char *c = create("material=PETG\ncolor=#FFAA00\nnet=1000\ndensity=1.27");
    assign(1, a);
    enable();
    /* Taken out and put back within seconds still asks: another spool can go in that fast, and the tray keeps
     * reporting the filament it had. The spool that was there is remembered for the answer. */
    tray_status[1] = 0; publish_trays(); tick();
    advance(5000); tray_status[1] = 1; publish_trays(); tick();
    assert(!spools.slots[1].spool[0] && spools.slots[1].question == 1 && !strcmp(spools.slots[1].last, a));
    assign(1, a);
    /* Taken out while idle: back to storage after 15 s. */
    tray_status[1] = 0; publish_trays(); tick(); advance(16000); tick();
    assert(!spools.slots[1].spool[0] && !strcmp(spools.slots[1].last, a) && near(spool_find(a)->remaining, 1000));
    /* New filament: a question, shown to every page through /api/printer. */
    tray_status[1] = 1; tray_type[1] = "PETG"; tray_color[1] = "#FFAA00"; publish_trays(); tick();
    assert(spools.slots[1].question == 1);
    char summary[128]; spools_summary(summary, sizeof(summary));
    assert(strstr(summary, "\"enabled\":true") && strstr(summary, "\"questions\":[1]"));
    /* Unanswered, the tray's usage is kept and charged to the spool chosen later. */
    printer("printing", "vase.gcode", 1); service("local/vase.gcode", 0, 1); tick();
    service("local/vase.gcode", 800, 60); tick();
    assert(near(spools.slots[1].question_mm, 800) && near(spool_find(c)->remaining, 1000));
    assign(1, c);
    assert(!spools.slots[1].question && near(spool_find(c)->remaining, 1000 - grams(c, 800)));
    service("local/vase.gcode", 1000, 80); tick();
    printer("complete", "vase.gcode", -1); tick(); advance(9000); tick();
    const spool_event *e = &spools.log[spools.log_count - 1];
    assert(e->kind == 'p' && !strcmp(e->spool, c) && near(e->mm, 1000) && near(e->grams, -grams(c, 1000)));
    /* Edited on the touchscreen right after an assignment: the spool's own data. */
    tray_color[1] = "#FFAB00"; publish_trays(); tick();
    assert(!strcmp(spools.slots[1].spool, c) && !spools.slots[1].question);
    /* Later, filament that does not fit the spool asks again, and the spool leaves the tray. */
    advance(31000);
    tray_type[1] = "ABS"; tray_color[1] = "#000000"; publish_trays(); tick();
    assert(spools.slots[1].question == 2 && !spools.slots[1].spool[0] && !strcmp(spools.slots[1].last, c));
    assert(call("dismiss", "slot=1") == 200 && !spools.slots[1].question);
    assert(call("dismiss", "slot=4") == 400);
    /* Turning tracking on checks every binding against the trays. */
    assign(1, c);
    assert(call("enable", "off") == 200);
    tray_type[1] = "PLA"; tray_color[1] = "#09CC3A"; publish_trays();
    assert(call("enable", "on") == 200 && spools.slots[1].question == 2 && !spools.slots[1].spool[0]);
    /* A spool sits in one place only. */
    assign(2, a); assign(3, a);
    assert(!spools.slots[2].spool[0] && !strcmp(spools.slots[3].spool, a));
    puts("PASS: inserted and changed filament asks which spool, usage waits for the answer");
}

static void test_runout(void) {
    reset();
    const char *a = create("material=PLA\ncolor=#2850DF\nnet=1000\nremaining=40");
    const char *b = create("material=PLA\ncolor=#09CC3A\nnet=1000");
    assign(0, a); assign(1, b);
    enable();
    tray_status[0] = 2; publish_trays();
    printer("printing", "long.gcode", 0); service("local/long.gcode", 0, 1); tick();
    service("local/long.gcode", 9000, 900); tick();
    /* The feeding tray empties: the spool ran out. The tail is still printed. */
    tray_status[0] = 0; publish_trays(); tick();
    assert(spools.slots[0].runout_ms && !strcmp(spools.slots[0].spool, a));
    service("local/long.gcode", 9500, 950); tick();
    advance(16000); tick();
    assert(!strcmp(spools.slots[0].spool, a)); /* still feeding */
    printer("printing", "long.gcode", 1); tray_status[1] = 2; publish_trays(); tick();
    assert(!spools.slots[0].spool[0] && near(spool_find(a)->remaining, 0) && !strcmp(spools.slots[0].last, a));
    const spool_event *r = &spools.log[spools.log_count - 1], *p = &spools.log[spools.log_count - 2];
    assert(r->kind == 'r' && near(r->grams, -(40 - grams(a, 9500))) && p->kind == 'p' && near(p->mm, 9500) &&
           !strcmp(p->result, "runout"));
    /* A new spool in the tray that ran out, before the printer moved on: the old one is still empty, and the
     * new one is asked about although the tray reports the same filament. */
    tray_status[0] = 1; publish_trays(); tick();
    assert(spools.slots[0].question == 1 && !strcmp(spools.slots[0].last, a));
    assign(0, b);
    tray_status[0] = 2; publish_trays();
    printer("printing", "long.gcode", 0); service("local/long.gcode", 9600, 960); tick();
    tray_status[0] = 0; publish_trays(); tick();
    assert(spools.slots[0].runout_ms && !strcmp(spools.slots[0].spool, b));
    advance(5000); tray_status[0] = 1; publish_trays(); tick();
    assert(near(spool_find(b)->remaining, 0) && !spools.slots[0].spool[0] && spools.slots[0].question == 1 &&
           !strcmp(spools.slots[0].last, b) && spools.log[spools.log_count - 1].kind == 'r');
    b = create("material=PLA\ncolor=#09CC3A\nnet=1000");
    assign(1, b);
    printer("printing", "long.gcode", 1);
    /* Unloaded first and then taken out is not a run-out. */
    tray_status[1] = 1; publish_trays(); tick();
    tray_status[1] = 0; publish_trays(); tick(); advance(16000); tick();
    assert(!spools.slots[1].spool[0] && spool_find(b)->remaining > 900);
    /* A restart while a spool's tray is empty still gives the spool back. */
    assign(1, b);
    assert(spools_save() == 0);
    spools_load(); spools_canvas_seen = 0;
    assert(!strcmp(spools.slots[1].spool, b) && spools.slots[1].seen.status == 0);
    publish_trays(); tick();
    assert(!strcmp(spools.slots[1].spool, b));
    advance(16000); tick();
    assert(!spools.slots[1].spool[0] && !strcmp(spools.slots[1].last, b));
    puts("PASS: a run-out empties the spool once the printer moves on; an unload does not");
}

static void test_external_and_removal(void) {
    reset();
    const char *a = create("material=PLA\ncolor=#2850DF\nnet=1000");
    const char *x = create("material=PETG\ncolor=#000000\nnet=1000");
    assign(4, x); assign(0, a);
    enable();
    /* No Canvas tray feeds: the external spool holder does. */
    printer("printing", "ext.gcode", -1); service("local/ext.gcode", 0, 1); tick();
    service("local/ext.gcode", 300, 30); tick();
    assert(near(spool_find(x)->remaining, 1000 - grams(x, 300)) && near(spool_find(a)->remaining, 1000));
    /* Archiving or deleting a loaded spool empties its tray. */
    char body[96]; snprintf(body, sizeof(body), "id=%s\narchived=1", x);
    assert(call("save", body) == 200 && !spools.slots[4].spool[0]);
    snprintf(body, sizeof(body), "slot=4\nspool=%s", x);
    assert(call("assign", body) == 409);
    snprintf(body, sizeof(body), "id=%s", a);
    assert(call("delete", body) == 200 && !spools.slots[0].spool[0] && !spool_find(a) && spools.count == 1);
    assert(call("delete", body) == 404);
    /* Usage without a spool is logged too; disabling during a print closes its count. */
    service("local/ext.gcode", 400, 40); tick();
    assert(call("enable", "off") == 200 && !spools.job.active);
    const spool_event *e = &spools.log[spools.log_count - 1];
    assert(!strcmp(e->result, "tracking off") && !e->spool[0] && e->slot == 4 && near(e->mm, 100) &&
           near(e->grams, -spool_grams_at(100, 1.75, SPOOL_DENSITY)));
    puts("PASS: external holder, archive, delete and switching off mid-print");
}

static void test_damaged_file(void) {
    reset();
    FILE *f = fopen(spools_path, "wb"); assert(f); fputs("{\"version\":1,\"enabled\":tru", f); fclose(f);
    spools_load();
    assert(!spools_available);
    assert(call("save", "material=PLA\ncolor=#123456") == 503 && call("enable", "on") == 503);
    char summary[64]; spools_summary(summary, sizeof(summary)); assert(!strcmp(summary, "null"));
    size_t length; char *text = plates_read_file(spools_path, SPOOLS_FILE_MAX, &length);
    assert(text && !strcmp(text, "{\"version\":1,\"enabled\":tru")); /* never overwritten */
    free(text);
    tick();
    puts("PASS: a damaged library file stays untouched and the library closed");
}

int main(void) {
    static char path[64];
    snprintf(path, sizeof(path), "spools-test-%ld.json", (long)getpid());
    spools_path = path;
    spools_clock = clock_fake;
    spools_wall = wall_fake;
    test_values();
    test_library();
    test_print();
    test_first_tray();
    test_service_channel();
    test_restart_and_reprint();
    test_questions();
    test_runout();
    test_external_and_removal();
    test_damaged_file();
    unlink(spools_path);
    return 0;
}
