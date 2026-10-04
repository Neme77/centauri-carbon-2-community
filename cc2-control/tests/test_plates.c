#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

/* The printer as plates.h sees it: autosave.cfg on disk, profiles in memory, G-code it ran. */
static plate_mesh memory_a, memory_b;
static int memory_has_b = 1, uds_down;
static char ran[256];
static int fake_uds(const char *query, char **reply, size_t *length) {
    if (uds_down) return -1;
    json_builder b = {malloc(65536), 0, 65536, 0};
    if (strstr(query, "gcode/script")) {
        const char *script = strstr(query, "\"script\":\"") + 10;
        snprintf(ran, sizeof(ran), "%.*s", (int)(strchr(script, '"') - script), script);
        json_builder_printf(&b, "{\"id\":7,\"result\":{}}");
    } else {
        assert(strstr(query, "\"bed_mesh\":[\"profiles\"]"));
        json_builder_printf(&b, "{\"id\":7,\"result\":{\"eventtime\":12.5,\"status\":{\"bed_mesh\":{\"profiles\":{");
        for (int side = 0; side < 2; ++side) {
            const plate_mesh *m = side ? &memory_b : &memory_a;
            if (side && !memory_has_b) break;
            json_builder_printf(&b, "%s\"%s\":{\"points\":[", side ? "," : "", side ? "default1" : "default");
            for (int r = 0; r < m->y_count; ++r) {
                json_builder_printf(&b, "%s[", r ? "," : "");
                for (int c = 0; c < m->x_count; ++c)
                    json_builder_printf(&b, "%s%.17g", c ? "," : "", m->points[r * m->x_count + c]);
                json_builder_printf(&b, "]");
            }
            json_builder_printf(&b, "],\"mesh_params\":{\"min_x\":%.1f,\"max_x\":%.1f,\"min_y\":%.1f,\"max_y\":%.1f,"
                "\"x_count\":%d,\"y_count\":%d,\"mesh_x_pps\":%d,\"mesh_y_pps\":%d,\"algo\":\"%s\",\"tension\":%.1f}}",
                m->min_x, m->max_x, m->min_y, m->max_y, m->x_count, m->y_count, m->x_pps, m->y_pps, m->algo, m->tension);
        }
        json_builder_printf(&b, "}}}}}");
    }
    assert(!b.failed);
    *reply = b.data; *length = b.length;
    return 0;
}

static plate_mesh grid(double base) {
    plate_mesh m; memset(&m, 0, sizeof(m));
    m.x_count = m.y_count = 11; m.x_pps = m.y_pps = 3;
    m.min_x = m.min_y = 6; m.max_x = m.max_y = 246; m.tension = 0.2;
    snprintf(m.algo, sizeof(m.algo), "bicubic");
    for (int i = 0; i < 121; ++i) m.points[i] = base + (double)((i * 37) % 101) / 1000.0 - 0.05;
    for (int i = 0; i < 121; ++i) m.points[i] = round(m.points[i] * 1e6) / 1e6;
    return m;
}

static const char *section_text(char *out, size_t cap, const char *slot, const plate_mesh *m) {
    assert(autosave_format(out, cap, slot, m) > 0);
    return out;
}

/* The vendor file: header, other sections, both mesh slots (optionally without default1). */
static void write_autosave(const plate_mesh *a, const plate_mesh *b) {
    char sa[4096], sb[4096], text[16384];
    snprintf(text, sizeof(text),
        "\n\n" AUTOSAVE_MARKER "\n#*# DO NOT EDIT THIS BLOCK OR BELOW. The contents are auto-generated.\n#*#\n"
        "#*# [input_shaper]\n#*# shaper_type_x = zv\n#*# shaper_freq_x = 51.400000\n#*#\n#*#\n"
        "#*# [stepper_z]\n#*# position_endstop = 6.394592\n#*#\n#*#\n"
        "%s#*#\n#*#\n#*# [bed_mesh ADAPTIVE]\n#*# version = 1\n#*# points = 0.1, 0.2, 0.3, 0.1, 0.2, 0.3, 0.1, 0.2, 0.3\n"
        "#*# offset = 0.000000\n#*# algo = lagrange\n#*# max_x = 236.219400\n#*# max_y = 236.220300\n"
        "#*# mesh_x_pps = 0\n#*# mesh_y_pps = 0\n#*# min_x = 19.719400\n#*# min_y = 19.720300\n"
        "#*# tension = 0.200000\n#*# x_count = 3\n#*# y_count = 3\n#*#\n#*#\n"
        "%s%s#*# [extruder]\n#*# control = pid\n#*# pid_Kd = 61.500269\n\n",
        section_text(sa, sizeof(sa), "default", a), b ? section_text(sb, sizeof(sb), "default1", b) : "",
        b ? "#*#\n#*#\n" : "");
    FILE *f = fopen(printer_autosave_path, "wb"); assert(f);
    assert(fwrite(text, 1, strlen(text), f) == strlen(text)); fclose(f);
    assert(chmod(printer_autosave_path, 0777) == 0);
}

static char *slurp(const char *path, size_t *length) {
    char *text = plates_read_file(path, AUTOSAVE_FILE_MAX, length); assert(text); return text;
}

static int status_of(const char *response) { return atoi(strchr(response, ' ') + 1); }
static char response[131072];
static int call(int handler, const mqtt_client *mqtt, const char *body) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    size_t length = body ? strlen(body) : 0;
    switch (handler) {
        case 0: plates_get_response(pair[0]); break;
        case 1: plates_save_response(pair[0], mqtt, body, length); break;
        case 2: plates_mount_response(pair[0], mqtt, body, length); break;
        case 3: plates_edit_response(pair[0], mqtt, body, length); break;
        case 4: plates_delete_response(pair[0], body, length); break;
        case 5: plates_recapture_response(pair[0], mqtt, body, length); break;
        case 6: plates_unmount_response(pair[0]); break;
    }
    close(pair[0]);
    size_t used = 0; ssize_t n;
    while ((n = recv(pair[1], response + used, sizeof(response) - 1 - used, 0)) > 0) used += (size_t)n;
    response[used] = 0; close(pair[1]);
    return status_of(response);
}
#define GET 0
#define SAVE 1
#define MOUNT 2
#define EDIT 3
#define DELETE 4
#define RECAPTURE 5
#define UNMOUNT 6

static void fresh(mqtt_client *mqtt) {
    mqtt->connected = mqtt->registered = 1; mqtt->have_machine_status = 1; mqtt->machine_status = 1;
    mqtt->last_message = time(NULL);
    telemetry.fd = 0; telemetry.ready = 1; clock_gettime(CLOCK_MONOTONIC, &telemetry.last_rx);
    plates_next_tick_ms = 0;
}

static pid_t refuse_launch(void) { assert(!"the guard must stop this reboot"); return -1; }

/* The printer service binds a new socket file when it starts. Creating the new file
 * before the old one goes guarantees a different inode. */
static char service_socket[160];
static void restart_service(void) {
    char next[176]; snprintf(next, sizeof(next), "%s.new", service_socket);
    FILE *f = fopen(next, "wb"); assert(f); fclose(f);
    assert(rename(next, service_socket) == 0);
}

static void new_process(void) { /* what a CC2 Control restart forgets */
    plates_reboot_requested = 0; plates_z_valid = 0; plates_result = ""; reboot_pending = 0; reboot_guard = NULL;
    plates_seen_connections = ULONG_MAX; plates_next_tick_ms = 0; ran[0] = 0;
    plates_load();
}

static void test_names_and_numbers(void) {
    assert(plate_name_valid("Textured PEI") && plate_name_valid("Текстурная PEI (B)") && plate_name_valid("板 1"));
    assert(!plate_name_valid("") && !plate_name_valid(" lead") && !plate_name_valid("trail ") &&
           !plate_name_valid("quo\"te") && !plate_name_valid("back\\slash") && !plate_name_valid("tab\there") &&
           !plate_name_valid("\xc0\xaf") && !plate_name_valid("\xe2\x80\xa8") && !plate_name_valid("\xed\xa0\x80"));
    char long_name[80]; memset(long_name, 'x', 65); long_name[65] = 0;
    assert(!plate_name_valid(long_name)); long_name[64] = 0; assert(plate_name_valid(long_name));
    double z;
    assert(plate_z_parse("-0.02", &z) && z == -0.02 && plate_z_parse("0.0005", &z) && fabs(z - 0.001) < 1e-12);
    assert(plate_z_parse("-0", &z) && !signbit(z) && plate_z_parse("1.000", &z) && z == 1.0);
    assert(!plate_z_parse("1.01", &z) && !plate_z_parse("1e-3", &z) && !plate_z_parse("nan", &z) &&
           !plate_z_parse("", &z) && !plate_z_parse(" 0.1", &z) && !plate_z_parse("0x1", &z));
}

static void test_autosave(void) {
    plate_mesh a = grid(0.6), b = grid(0.3), c = grid(-0.2), read;
    write_autosave(&a, &b);
    assert(autosave_slot('A', &read) == 1 && plate_mesh_equal(&read, &a, 1));
    assert(autosave_slot('B', &read) == 1 && plate_mesh_equal(&read, &b, 1));
    size_t length, updated_length, start, end;
    char *text = slurp(printer_autosave_path, &length);
    /* Replacing a slot changes exactly that section. */
    char *updated = autosave_with_mesh(text, length, "default", &c, &updated_length);
    assert(updated && autosave_section(text, length, "default", &start, &end) == 1);
    char section[4096]; int section_len = autosave_format(section, sizeof(section), "default", &c);
    assert(updated_length == length - (end - start) + (size_t)section_len);
    assert(!memcmp(updated, text, start) && !memcmp(updated + start, section, (size_t)section_len));
    assert(!memcmp(updated + start + section_len, text + end, length - end));
    free(updated);
    /* The vendor's own spelling round-trips byte for byte. */
    assert(autosave_section(text, length, "default1", &start, &end) == 1);
    section_len = autosave_format(section, sizeof(section), "default1", &b);
    assert((size_t)section_len == end - start && !memcmp(section, text + start, end - start));
    free(text);
    /* A missing Side B is appended after the last section, keeping the final blank line. */
    write_autosave(&a, NULL);
    assert(autosave_slot('B', &read) == 0);
    text = slurp(printer_autosave_path, &length);
    updated = autosave_with_mesh(text, length, "default1", &c, &updated_length);
    assert(updated && !memcmp(updated, text, length - 2));
    assert(strstr(updated, "#*# pid_Kd = 61.500269\n#*#\n#*#\n#*# [bed_mesh default1]\n#*# version = 1\n"));
    const char *tail = "#*# y_count = 11\n\n";
    assert(updated_length > strlen(tail) && !strcmp(updated + updated_length - strlen(tail), tail));
    assert(autosave_section(updated, updated_length, "default", &start, &end) == 1);
    free(updated);
    /* Anything unexpected keeps the file untouched. */
    char *odd = malloc(length + 64); assert(odd);
    const char *slot_line = strstr(text, "#*# algo = bicubic");
    size_t at = (size_t)(slot_line - text);
    memcpy(odd, text, at); strcpy(odd + at, "#*# future_key = 1\n"); strcat(odd, text + at);
    assert(!autosave_with_mesh(odd, strlen(odd), "default", &c, &updated_length));
    strcpy(odd, text); odd[strstr(odd, "#*# version") - odd + 2] = '\r';
    assert(!autosave_with_mesh(odd, strlen(odd), "default", &c, &updated_length));
    assert(!autosave_with_mesh("[printer]\n", 10, "default", &c, &updated_length));
    snprintf(odd, length + 64, "%s", text);
    char *points = strstr(odd, "#*# points = ") + 13; *strchr(points, ',') = ';';
    assert(!autosave_with_mesh(odd, strlen(odd), "default", &c, &updated_length));
    free(odd); free(text);
}

static void test_store(void) {
    plate_mesh a = grid(0.6);
    memset(&plates, 0, sizeof(plates)); plates_available = 1;
    plate_entry *p = &plates.plates[0];
    snprintf(p->id, sizeof(p->id), "0123456789abcdef"); snprintf(p->name, sizeof(p->name), "Гладкая A");
    p->side = 'A'; p->z = -0.025; p->measured = 1790000000; p->mesh = a;
    plates.count = 1; memcpy(plates.current, p->id, sizeof(plates.current));
    assert(plates_save() == 0);
    plates_load();
    assert(plates_available && plates.count == 1 && !strcmp(plates.current, "0123456789abcdef"));
    assert(!strcmp(plates.plates[0].name, "Гладкая A") && plates.plates[0].z == -0.025);
    assert(plate_mesh_equal(&plates.plates[0].mesh, &a, 1));
    /* A damaged library stays closed instead of being overwritten. */
    FILE *f = fopen(plates_path, "ab"); assert(f); fputs("{", f); fclose(f);
    size_t length; char *text = slurp(plates_path, &length);
    text[1] = 'X'; f = fopen(plates_path, "wb"); fwrite(text, 1, length, f); fclose(f); free(text);
    plates_load();
    assert(!plates_available && plates.count == 0);
    mqtt_client mqtt = {0}; fresh(&mqtt);
    assert(call(SAVE, &mqtt, "A\nNew\n0") == 503 && strstr(response, "unreadable"));
    unlink(plates_path);
    plates_load();
    assert(plates_available && plates.count == 0);
}

static void test_http(void) {
    mqtt_client mqtt = {0};
    plate_mesh a = grid(0.6), b = grid(0.3), other = grid(0.1);
    write_autosave(&a, &b);
    memory_a = a; memory_b = b; memory_has_b = 1;
    unlink(plates_path); new_process(); fresh(&mqtt);

    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"available\":true") &&
           strstr(response, "\"slots\":{\"A\":\"mesh\",\"B\":\"mesh\"},\"plates\":[]"));
    assert(call(SAVE, &mqtt, "C\nBad side\n0") == 400 && call(SAVE, &mqtt, "A\nZ too big\n1.5") == 400);
    mqtt.machine_status = 2; assert(call(SAVE, &mqtt, "A\nSmooth\n-0.02") == 409 && strstr(response, "idle"));
    fresh(&mqtt);
    memory_a.points[5] += 0.01; /* the file and memory disagree until the printer restarts */
    assert(call(SAVE, &mqtt, "A\nSmooth\n-0.02") == 409 && strstr(response, "restart the printer"));
    memory_a = a;
    assert(call(SAVE, &mqtt, "A\nSmooth\n-0.02") == 201 && strstr(response, "\"saved\":true"));
    assert(call(SAVE, &mqtt, "B\nSmooth\n0") == 409 && strstr(response, "already exists"));
    assert(call(SAVE, &mqtt, "B\nTextured\n0.01\n") == 201);
    char smooth[17], textured[17];
    snprintf(smooth, sizeof(smooth), "%s", plates.plates[0].id); snprintf(textured, sizeof(textured), "%s", plates.plates[1].id);
    assert(plates.plates[0].side == 'A' && plate_mesh_equal(&plates.plates[0].mesh, &a, 1));
    assert(plates.plates[1].side == 'B' && plate_mesh_equal(&plates.plates[1].mesh, &b, 1));
    assert(!plates.current[0] && !ran[0]);
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"name\":\"Smooth\",\"side\":\"A\",\"z_offset\":-0.020") &&
           strstr(response, "\"in_printer\":true"));

    /* A plate already in its slot mounts at once: only the offset changes. */
    char body[64]; snprintf(body, sizeof(body), "%s", smooth);
    assert(call(MOUNT, &mqtt, body) == 200 && strstr(response, "\"reboot\":false"));
    assert(!strcmp(ran, "SET_GCODE_OFFSET Z=-0.020") && !strcmp(plates.current, smooth) && !reboot_pending);
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"z_applied\":true"));
    z_offset_session = 1;
    snprintf(body, sizeof(body), "%s\nSmooth\n-0.035", smooth); ran[0] = 0;
    assert(call(EDIT, &mqtt, body) == 200 && strstr(response, "\"applied\":true") &&
           !strcmp(ran, "SET_GCODE_OFFSET Z=-0.035") && !z_offset_session);
    snprintf(body, sizeof(body), "%s\nTextured\n0", smooth);
    assert(call(EDIT, &mqtt, body) == 409);
    snprintf(body, sizeof(body), "%s\nGlass\n0.02", textured); ran[0] = 0;
    assert(call(EDIT, &mqtt, body) == 200 && strstr(response, "\"applied\":false") && !ran[0]);
    snprintf(body, sizeof(body), "%s", textured);
    assert(call(RECAPTURE, &mqtt, body) == 409 && strstr(response, "mounted plate"));

    /* After a recalibration the mounted plate can take the new mesh. */
    plate_mesh recalibrated = grid(0.62);
    write_autosave(&recalibrated, &b); memory_a = recalibrated;
    snprintf(body, sizeof(body), "%s", smooth);
    assert(call(RECAPTURE, &mqtt, body) == 200 && plate_mesh_equal(&plate_find(smooth)->mesh, &recalibrated, 1));

    /* A different plate on Side A needs the file and a restart. */
    plates.plates[0].mesh = other; assert(plates_save() == 0);
    assert(call(MOUNT, &mqtt, body) == 409 && strstr(response, "\"reboot_required\":true"));
    assert(call(MOUNT, &mqtt, "0123456789abcdef\nREBOOT") == 404);
    snprintf(body, sizeof(body), "%s\nYES", smooth);
    assert(call(MOUNT, &mqtt, body) == 400);
    size_t before_length, length; char *before = slurp(printer_autosave_path, &before_length);
    snprintf(body, sizeof(body), "%s\nREBOOT\n", smooth);
    assert(call(MOUNT, &mqtt, body) == 202 && strstr(response, "\"reboot\":true"));
    assert(reboot_pending && plates_reboot_requested && !strcmp(plates.pending, smooth) && !strcmp(plates.current, smooth));
    plate_mesh read;
    assert(autosave_slot('A', &read) == 1 && plate_mesh_equal(&read, &other, 1));
    assert(autosave_slot('B', &read) == 1 && plate_mesh_equal(&read, &b, 1));
    char backup[PATH_MAX_LOCAL], copy[PATH_MAX_LOCAL]; struct stat info;
    assert(autosave_backup_path(backup, sizeof(backup)));
    char *kept = slurp(backup, &length); assert(length == before_length && !memcmp(kept, before, length)); free(kept);
    plates_autosave_copy_path(copy, sizeof(copy));
    kept = slurp(copy, &length); assert(length == before_length && !memcmp(kept, before, length)); free(kept);
    assert(stat(printer_autosave_path, &info) == 0 && (info.st_mode & 0777) == 0777);
    assert(call(SAVE, &mqtt, "A\nLater\n0") == 409 && strstr(response, "waiting for the printer restart"));

    /* The restart utility failed: the old mesh goes back to the file. */
    reboot_pending = 0; reboot_error = "launch_failed";
    plates_tick(&mqtt);
    assert(!plates_reboot_requested && !plates.pending[0] && !strcmp(plates_result, "reboot_failed"));
    char *now = slurp(printer_autosave_path, &length);
    assert(length == before_length && !memcmp(now, before, length)); free(now);
    assert(!strcmp(plates.current, smooth)); /* it was mounted before the attempt */

    /* A print started on the screen while the reboot waited: the guard cancels it. */
    snprintf(body, sizeof(body), "%s\nREBOOT", smooth);
    assert(call(MOUNT, &mqtt, body) == 202 && reboot_guard == plates_reboot_guard);
    reboot_launcher = refuse_launch; mqtt.machine_status = 2; reboot_due = recovery_clock() - 1;
    recovery_tick();
    assert(!reboot_pending && !reboot_launched && !strcmp(reboot_error, "printer_busy") && !reboot_guard);
    plates_tick(&mqtt);
    assert(!plates_reboot_requested && !plates.pending[0] && !strcmp(plates_result, "reboot_failed"));
    now = slurp(printer_autosave_path, &length);
    assert(length == before_length && !memcmp(now, before, length)); free(now);
    fresh(&mqtt);

    /* The restart happens; the next process finds the mesh in the file and in memory. */
    snprintf(body, sizeof(body), "%s\nREBOOT", smooth);
    assert(call(MOUNT, &mqtt, body) == 202);
    memory_a = other;
    new_process(); fresh(&mqtt);
    assert(plates_available && !strcmp(plates.pending, smooth));
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"pending\":\"") && strstr(response, "\"result\":\"\""));
    uds_down = 1; plates_tick(&mqtt); assert(plates.pending[0]); /* memory not readable yet */
    uds_down = 0; plates_next_tick_ms = 0;
    plates_tick(&mqtt);
    assert(!plates.pending[0] && !strcmp(plates_result, "mounted") && !strcmp(plates.current, smooth));
    assert(!strcmp(ran, "SET_GCODE_OFFSET Z=-0.035"));
    ran[0] = 0; plates_next_tick_ms = 0; plates_tick(&mqtt); assert(!ran[0]); /* applied once */
    /* A reconnect after a receive timeout during a print start keeps the offset. */
    mqtt.machine_status = 2; telemetry.connections++; plates_next_tick_ms = 0;
    plates_tick(&mqtt); assert(!ran[0]);
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"z_applied\":true"));
    fresh(&mqtt); plates_tick(&mqtt); assert(!ran[0]);
    /* A restarted printer service has a new socket and has lost it. */
    restart_service(); telemetry.connections++; mqtt.machine_status = 2; plates_next_tick_ms = 0;
    plates_tick(&mqtt); assert(!ran[0]);
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"z_applied\":false"));
    fresh(&mqtt); plates_tick(&mqtt); assert(!strcmp(ran, "SET_GCODE_OFFSET Z=-0.035"));
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"z_applied\":true"));

    /* A restart that did not load the mesh is reported and the plate is not mounted. */
    plates.plates[0].mesh = grid(0.9); assert(plates_save() == 0);
    snprintf(body, sizeof(body), "%s\nREBOOT", smooth);
    assert(call(MOUNT, &mqtt, body) == 202);
    new_process(); fresh(&mqtt);
    plates_tick(&mqtt);
    assert(!plates.pending[0] && !plates.current[0] && !strcmp(plates_result, "verify_failed"));

    /* Side B without a saved mesh gets its section appended. */
    write_autosave(&other, NULL); memory_has_b = 0; memory_a = other;
    snprintf(body, sizeof(body), "%s\nREBOOT", textured);
    assert(call(MOUNT, &mqtt, body) == 202);
    assert(autosave_slot('B', &read) == 1 && plate_mesh_equal(&read, &b, 1));
    assert(autosave_slot('A', &read) == 1 && plate_mesh_equal(&read, &other, 1));
    memory_b = b; memory_has_b = 1;
    new_process(); fresh(&mqtt); plates_tick(&mqtt);
    assert(!strcmp(plates_result, "mounted") && !strcmp(plates.current, textured) && !strcmp(ran, "SET_GCODE_OFFSET Z=0.020"));

    assert(call(UNMOUNT, &mqtt, NULL) == 200 && !plates.current[0]);
    snprintf(body, sizeof(body), "%s", textured);
    assert(call(DELETE, &mqtt, body) == 200 && plates.count == 1 && !plate_find(textured));
    assert(call(DELETE, &mqtt, body) == 404);
    new_process(); assert(plates.count == 1 && !strcmp(plates.plates[0].id, smooth));
    free(before);
}

int main(void) {
    char directory[] = "/tmp/cc2-plates-XXXXXX";
    assert(mkdtemp(directory));
    static char autosave[128], library[128];
    snprintf(autosave, sizeof(autosave), "%s/autosave.cfg", directory);
    snprintf(library, sizeof(library), "%s/bed-plates.json", directory);
    printer_autosave_path = autosave; plates_path = library;
    snprintf(service_socket, sizeof(service_socket), "%s/elegoo_uds", directory);
    FILE *socket_file = fopen(service_socket, "wb"); assert(socket_file); fclose(socket_file);
    object_query_path = service_socket;
    plates_uds = fake_uds;
    uds_init(&telemetry);
    test_names_and_numbers();
    test_autosave();
    test_store();
    test_http();
    printf("PASS: plate library\n");
    return 0;
}
