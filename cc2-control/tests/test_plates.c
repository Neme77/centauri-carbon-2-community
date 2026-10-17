#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

/* The printer as plates.h sees it: autosave.cfg on disk, profiles in memory, G-code it ran. */
static plate_mesh memory_a, memory_b;
static int memory_has_b = 1, uds_down;
static plate_mesh active_mesh;
static char active_profile[32]="default";
static int refuse_mesh_load, ignore_mesh_load, corrupt_active_matrix, missing_active_matrix;
static char ran[256];            /* the last SET_GCODE_OFFSET */
static char profile_log[1024];   /* BED_MESH_PROFILE scripts, separated by '|' */
static int fake_uds(const char *query, char **reply, size_t *length) {
    if (uds_down) return -1;
    json_builder b = {malloc(65536), 0, 65536, 0};
    if (strstr(query, "gcode/script")) {
        const char *script = strstr(query, "\"script\":\"") + 10;
        int n = (int)(strchr(script, '"') - script);
        if (!strncmp(script, "SET_GCODE_OFFSET", 16)) snprintf(ran, sizeof(ran), "%.*s", n, script);
        else {
            size_t used = strlen(profile_log);
            snprintf(profile_log + used, sizeof(profile_log) - used, "%s%.*s", used ? "|" : "", n, script);
        }
        if (!strncmp(script,"BED_MESH_PROFILE LOAD=",22)) {
            if (refuse_mesh_load) {
                free(b.data); *reply=strdup("{\"id\":204,\"error\":{\"message\":\"refused\"}}");
                *length=strlen(*reply); return 0;
            }
            if (!ignore_mesh_load) {
                const char *name=script+22;
                if (!strncmp(name,"default1",8)) { active_mesh=memory_b; snprintf(active_profile,sizeof(active_profile),"default1"); }
                else if (!strncmp(name,"default",7)) { active_mesh=memory_a; snprintf(active_profile,sizeof(active_profile),"default"); }
            }
        }
        /* SAVE copies the loaded slot to a profile and writes autosave.cfg at once. */
        const char *load = strstr(script, "LOAD="), *save = strstr(script, "SAVE=");
        if (load && save && save < script + n) {
            char name[40]; snprintf(name, sizeof(name), "%.*s", (int)(script + n - save - 5), save + 5);
            const plate_mesh *from = !strncmp(load + 5, "default1", 8) ? &memory_b : &memory_a;
            size_t file_length, new_length;
            char *file = plates_read_file(printer_autosave_path, AUTOSAVE_FILE_MAX, &file_length);
            assert(file);
            char *updated = autosave_with_mesh(file, file_length, name, from, &new_length);
            assert(updated);
            FILE *f = fopen(printer_autosave_path, "wb"); assert(f);
            assert(fwrite(updated, 1, new_length, f) == new_length); fclose(f);
            free(file); free(updated);
        }
        json_builder_printf(&b, "{\"id\":7,\"result\":{}}");
    } else if (strstr(query,"\"profile_name\"")) {
        json_builder_printf(&b,"{\"id\":203,\"result\":{\"status\":{\"bed_mesh\":{\"profile_name\":\"%s\",\"mesh_min\":[%.9g,%.9g],\"mesh_max\":[%.9g,%.9g]",
            active_profile,active_mesh.min_x,active_mesh.min_y,active_mesh.max_x,active_mesh.max_y);
        if (!missing_active_matrix) {
            json_builder_printf(&b,",\"probed_matrix\":[");
            for(int r=0;r<active_mesh.y_count;r++) {
                json_builder_printf(&b,"%s[",r?",":"");
                for(int c=0;c<active_mesh.x_count;c++)
                    json_builder_printf(&b,"%s%.17g",c?",":"",active_mesh.points[r*active_mesh.x_count+c]+(corrupt_active_matrix && !r && !c?0.01:0));
                json_builder_printf(&b,"]");
            }
            json_builder_printf(&b,"]");
        }
        json_builder_printf(&b,"}}}}");
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
static char response[262144];
static int call(int handler, const mqtt_client *mqtt, const char *body) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    size_t length = body ? strlen(body) : 0;
    int size = 1 << 20; setsockopt(pair[0], SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
    setsockopt(pair[1], SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
    switch (handler) {
        case 0: plates_get_response(pair[0]); break;
        case 1: plates_save_response(pair[0], mqtt, body, length); break;
        case 2: plates_mount_response(pair[0], mqtt, body, length); break;
        case 3: plates_edit_response(pair[0], mqtt, body, length); break;
        case 4: plates_delete_response(pair[0], mqtt, body, length); break;
        case 5: plates_recapture_response(pair[0], mqtt, body, length); break;
        case 6: plates_unmount_response(pair[0]); break;
        case 7: plates_measure_response(pair[0], mqtt, body, length); break;
        case 8: plates_measure_delete_response(pair[0], mqtt, body, length); break;
        case 9: plates_keep_response(pair[0], mqtt, body, length); break;
        case 10: plates_nozzle_save_response(pair[0], mqtt, body, length); break;
        case 11: plates_nozzle_delete_response(pair[0], mqtt, body, length); break;
        case 12: plates_nozzle_select_response(pair[0], mqtt, body, length); break;
        case 13: plates_create_response(pair[0], mqtt, body, length); break;
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
#define MEASURE 7
#define MEASURE_DELETE 8
#define KEEP 9
#define NOZZLE 10
#define NOZZLE_DELETE 11
#define NOZZLE_SELECT 12

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
    plates_background_close(); plates_attempts=0; plates_attempt_id[0]=0; plates_attempt_service_known=0;
    plates_late.state = 0; plates_late_result = "";
    plates_load();
}

static plate_mesh *base_mesh(int index) { return &plate_base(&plates.plates[index])->mesh; }

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
    int temp;
    assert(plate_temp_parse("60", &temp) && temp == 60 && plate_temp_parse("110", &temp) && plate_temp_parse("40", &temp));
    assert(plate_temp_parse("30", &temp) && temp == 30);
    assert(!plate_temp_parse("29", &temp) && !plate_temp_parse("111", &temp) && !plate_temp_parse("6", &temp) &&
           !plate_temp_parse("60.5", &temp) && !plate_temp_parse("-60", &temp) && !plate_temp_parse("", &temp));
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
    plate_measure *m = &plates.measures[0];
    plate_nozzle *n = &plates.nozzles[0];
    snprintf(p->id, sizeof(p->id), "0123456789abcdef"); snprintf(p->name, sizeof(p->name), "Гладкая A");
    p->side = 'A'; p->z = -0.025; snprintf(p->measure, sizeof(p->measure), "00000000000000aa");
    snprintf(m->id, sizeof(m->id), "00000000000000aa"); memcpy(m->plate, p->id, sizeof(m->plate));
    m->temp = 80; m->measured = 1790000000; m->mesh = a;
    snprintf(n->id, sizeof(n->id), "00000000000000bb"); snprintf(n->name, sizeof(n->name), "0.4 латунь");
    n->diameter = 0.4; n->z = 0.015; memcpy(m->nozzle, n->id, sizeof(m->nozzle));
    plates.count = plates.measure_count = plates.nozzle_count = 1;
    memcpy(plates.current, p->id, sizeof(plates.current)); memcpy(plates.nozzle, n->id, sizeof(plates.nozzle));
    assert(plates_save() == 0);
    plates_load();
    assert(plates_available && plates.count == 1 && plates.measure_count == 1 && plates.nozzle_count == 1);
    assert(!strcmp(plates.current, "0123456789abcdef") && !strcmp(plates.nozzle, "00000000000000bb"));
    assert(!strcmp(plates.plates[0].name, "Гладкая A") && plates.plates[0].z == -0.025);
    assert(plates.measures[0].temp == 80 && !strcmp(plates.measures[0].nozzle, "00000000000000bb"));
    assert(plate_mesh_equal(base_mesh(0), &a, 1) && !strcmp(plates.nozzles[0].name, "0.4 латунь"));
    assert(fabs(plate_z_now(&plates.plates[0]) - (-0.010)) < 1e-9);
    /* A damaged library stays closed instead of being overwritten. */
    FILE *f = fopen(plates_path, "ab"); assert(f); fputs("{", f); fclose(f);
    size_t length; char *text = slurp(plates_path, &length);
    text[1] = 'X'; f = fopen(plates_path, "wb"); fwrite(text, 1, length, f); fclose(f); free(text);
    plates_load();
    assert(!plates_available && plates.count == 0);
    mqtt_client mqtt = {0}; fresh(&mqtt);
    assert(call(SAVE, &mqtt, "A\nNew\n0") == 503 && strstr(response, "unreadable"));
    /* A measurement must belong to a known plate. */
    f = fopen(plates_path, "wb"); assert(f);
    fputs("{\"version\":2,\"current\":\"\",\"pending\":\"\",\"nozzle\":\"\",\"nozzles\":[],\"plates\":[],"
          "\"measures\":[{\"id\":\"00000000000000aa\",\"plate\":\"0123456789abcdef\",\"temp\":60,\"nozzle\":\"\","
          "\"measured\":0,\"mesh\":{}}]}\n", f);
    fclose(f);
    plates_load();
    assert(!plates_available);
    unlink(plates_path);
    plates_load();
    assert(plates_available && plates.count == 0);
}

/* A library written before measurements existed: each plate's mesh becomes a 60 degree measurement. */
static void test_version_1(void) {
    plate_mesh a = grid(0.6);
    char mesh[8192]; json_builder b = {mesh, 0, sizeof(mesh), 0};
    plates_mesh_json(&b, &a); assert(!b.failed);
    char text[20000];
    snprintf(text, sizeof(text), "{\"version\":1,\"current\":\"0123456789abcdef\",\"pending\":\"\",\"plates\":["
        "{\"id\":\"0123456789abcdef\",\"name\":\"Карбон\",\"side\":\"A\",\"z_offset\":0.045,\"measured\":1790000000,\"mesh\":%s},"
        "{\"id\":\"fedcba9876543210\",\"name\":\"Штатный лист B\",\"side\":\"B\",\"z_offset\":0.000,\"measured\":1790000100,\"mesh\":%s}]}\n",
        mesh, mesh);
    FILE *f = fopen(plates_path, "wb"); assert(f); fputs(text, f); fclose(f);
    plates_load();
    assert(plates_available && plates.count == 2 && plates.measure_count == 2 && !plates.nozzle_count);
    for (int i = 0; i < 2; ++i) {
        const plate_measure *m = plate_base(&plates.plates[i]);
        assert(m && m->temp == 60 && !m->nozzle[0] && plate_mesh_equal(&m->mesh, &a, 1));
        assert(plate_id_valid(m->id) && strcmp(m->id, plates.plates[0].id) && strcmp(m->id, plates.plates[1].id));
    }
    assert(plates.measures[1].measured == 1790000100 && !strcmp(plates.current, "0123456789abcdef"));
    size_t length; char *saved = slurp(plates_path, &length);
    assert(strstr(saved, "\"version\":2") && strstr(saved, "\"measures\":[{"));
    free(saved);
    char kept[PLATE_ID_LEN + 1]; memcpy(kept, plates.measures[0].id, sizeof(kept));
    plates_load(); /* the ids given at the conversion stay */
    assert(plates_available && !strcmp(plates.measures[0].id, kept));
    unlink(plates_path);
}

static void test_http(void) {
    mqtt_client mqtt = {0};
    plate_mesh a = grid(0.6), b = grid(0.3), other = grid(0.1);
    write_autosave(&a, &b);
    memory_a = a; memory_b = b; memory_has_b = 1;
    unlink(plates_path); new_process(); fresh(&mqtt);

    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"available\":true") &&
           strstr(response, "\"slots\":{\"A\":\"mesh\",\"B\":\"mesh\"},\"nozzles\":[],\"plates\":[]"));
    assert(call(SAVE, &mqtt, "C\nBad side\n0") == 400 && call(SAVE, &mqtt, "A\nZ too big\n1.5") == 400);
    assert(call(SAVE, &mqtt, "A\nHot\n0\n120") == 400 && call(SAVE, &mqtt, "A\nNo nozzle\n0\n60\n0123456789abcdef") == 400);
    mqtt.machine_status = 2; assert(call(SAVE, &mqtt, "A\nSmooth\n-0.02") == 409 && strstr(response, "idle"));
    fresh(&mqtt);
    memory_a.points[5] += 0.01; /* the file and memory disagree until the printer restarts */
    assert(call(SAVE, &mqtt, "A\nSmooth\n-0.02") == 409 && strstr(response, "restart the printer"));
    memory_a = a;
    assert(call(SAVE, &mqtt, "A\nSmooth\n-0.02") == 201 && strstr(response, "\"saved\":true") &&
           strstr(response, "\"profile\":true"));
    assert(call(SAVE, &mqtt, "B\nSmooth\n0") == 409 && strstr(response, "already exists"));
    assert(call(SAVE, &mqtt, "B\nTextured\n0.01\n70\n") == 201);
    char smooth[17], textured[17];
    snprintf(smooth, sizeof(smooth), "%s", plates.plates[0].id); snprintf(textured, sizeof(textured), "%s", plates.plates[1].id);
    assert(plates.plates[0].side == 'A' && plate_mesh_equal(base_mesh(0), &a, 1) && plate_base(&plates.plates[0])->temp == 60);
    assert(plates.plates[1].side == 'B' && plate_mesh_equal(base_mesh(1), &b, 1) && plate_base(&plates.plates[1])->temp == 70);
    assert(!plates.current[0] && !ran[0]);
    /* Each new measurement got its own printer profile from its slot. */
    assert(plates_has_profile(plate_base(&plates.plates[0])) && plates_has_profile(plate_base(&plates.plates[1])));
    assert(strstr(profile_log, "BED_MESH_PROFILE LOAD=default\\nBED_MESH_PROFILE SAVE=cc2_"));
    assert(strstr(profile_log, "BED_MESH_PROFILE LOAD=default1\\nBED_MESH_PROFILE SAVE=cc2_"));
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"name\":\"Smooth\",\"side\":\"A\",\"z_offset\":-0.020") &&
           strstr(response, "\"in_printer\":true") && strstr(response, "\"temp\":60,\"nozzle\":\"\"") &&
           strstr(response, "\"slot\":true,\"profile\":true"));

    /* A plate already in its slot mounts at once: only the offset changes. */
    char body[96]; snprintf(body, sizeof(body), "%s", smooth);
    memory_a.points[0]+=0.01;
    assert(call(MOUNT,&mqtt,body)==409 && strstr(response,"reboot_required") && !ran[0]);
    memory_a=a;
    assert(call(MOUNT, &mqtt, body) == 200 && strstr(response, "\"reboot\":false"));
    assert(!strcmp(ran, "SET_GCODE_OFFSET Z=-0.020") && !strcmp(plates.current, smooth) && !reboot_pending);
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"z_applied\":true") && strstr(response, "\"z_effective\":-0.020"));
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
    assert(call(RECAPTURE, &mqtt, body) == 200 && plate_mesh_equal(base_mesh(0), &recalibrated, 1));
    assert(plate_measure_count(smooth) == 1 && plates_has_profile(plate_base(&plates.plates[0])));

    /* A different plate on Side A needs the file and a restart. */
    *base_mesh(0) = other; assert(plates_save() == 0);
    assert(call(MOUNT, &mqtt, body) == 409 && strstr(response, "\"reboot_required\":true"));
    assert(call(MOUNT, &mqtt, "0123456789abcdef\nREBOOT") == 404);
    snprintf(body, sizeof(body), "%s\nYES", smooth);
    assert(call(MOUNT, &mqtt, body) == 400);
    snprintf(body, sizeof(body), "%s\nREBOOT\nREBOOT", smooth);
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

    /* Unrelated firmware configuration updates survive a failed reboot. */
    snprintf(body,sizeof(body),"%s\nREBOOT",smooth);
    assert(call(MOUNT,&mqtt,body)==202);
    FILE *concurrent=fopen(printer_autosave_path,"ab");assert(concurrent);
    fputs("#*# [heater_bed]\n#*# pid_Kp = 123.45\n",concurrent);fclose(concurrent);
    assert(plates_reboot_guard());
    mqtt.connected=0;assert(!plates_reboot_guard());fresh(&mqtt);
    mqtt.last_message-=16;assert(!plates_reboot_guard());fresh(&mqtt);
    telemetry.last_rx.tv_sec-=20;assert(!plates_reboot_guard());fresh(&mqtt);
    reboot_pending=0;reboot_error="launch_failed";plates_tick(&mqtt);
    now=slurp(printer_autosave_path,&length);
    assert(strstr(now,"pid_Kp = 123.45") && autosave_slot('A',&read)==1 && plate_mesh_equal(&read,&recalibrated,1));free(now);
    write_autosave(&recalibrated,&b);

    /* Failed rollback keeps both recovery metadata and the backup. */
    assert(call(MOUNT,&mqtt,body)==202);
    char blocked[PATH_MAX_LOCAL];snprintf(blocked,sizeof(blocked),"%s.cc2-new",printer_autosave_path);
    assert(mkdir(blocked,0700)==0);
    reboot_pending=0;reboot_error="launch_failed";plates_tick(&mqtt);
    assert(!plates_available && plates.pending[0] && access(copy,F_OK)==0 && strstr(plates_error,"rollback failed"));
    assert(rmdir(blocked)==0);plates_available=1;
    assert(plates_restore_mesh()==0);plates.pending[0]=0;assert(plates_save()==0);
    fresh(&mqtt);

    /* A print started on the screen while the reboot waited: the guard cancels it. */
    free(before); before = slurp(printer_autosave_path, &before_length);
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
    *base_mesh(0) = grid(0.9); assert(plates_save() == 0);
    snprintf(body, sizeof(body), "%s\nREBOOT", smooth);
    assert(call(MOUNT, &mqtt, body) == 202);
    new_process(); fresh(&mqtt);
    plates_tick(&mqtt);
    assert(!plates.pending[0] && !plates.current[0] && !strcmp(plates_result, "verify_failed"));

    /* Repeated native query failures stop after three attempts. */
    snprintf(plates.current,sizeof(plates.current),"%s",smooth);plates_z_valid=0;
    plates_attempts=0;uds_down=1;
    for(int attempt=0;attempt<3;attempt++){fresh(&mqtt);plates_tick(&mqtt);}
    assert(plates_attempts==3 && !strcmp(plates_result,"verify_failed"));
    fresh(&mqtt);telemetry.connections++;plates_tick(&mqtt);assert(plates_attempts==3);
    uds_down=0;plates_attempts=0;

    /* Side B without a saved mesh gets its section appended. */
    write_autosave(&other, NULL); memory_has_b = 0; memory_a = other;
    snprintf(body, sizeof(body), "%s\nREBOOT", textured);
    assert(call(MOUNT, &mqtt, body) == 202);
    assert(autosave_slot('B', &read) == 1 && plate_mesh_equal(&read, &b, 1));
    assert(autosave_slot('A', &read) == 1 && plate_mesh_equal(&read, &other, 1));
    memory_b = b; memory_has_b = 1;
    active_mesh=memory_a; snprintf(active_profile,sizeof(active_profile),"default");
    new_process(); fresh(&mqtt); plates_tick(&mqtt);
    assert(!strcmp(active_profile,"default1") && plate_mesh_equal(&active_mesh,&memory_b,0));
    assert(!strcmp(plates_result, "mounted") && !strcmp(plates.current, textured) && !strcmp(ran, "SET_GCODE_OFFSET Z=0.020"));

    assert(call(UNMOUNT, &mqtt, NULL) == 200 && !plates.current[0]);
    snprintf(body, sizeof(body), "%s", textured);
    profile_log[0] = 0;
    assert(call(DELETE, &mqtt, body) == 200 && plates.count == 1 && !plate_find(textured) && plate_measure_count(textured) == 0);
    assert(strstr(profile_log, "BED_MESH_PROFILE REMOVE=cc2_"));
    assert(call(DELETE, &mqtt, body) == 404);
    new_process(); assert(plates.count == 1 && !strcmp(plates.plates[0].id, smooth) && plates.measure_count == 1);
    free(before);
}

/* Stored A/B profiles and the active mesh are independent firmware state. */
static void test_active_mount(void) {
    mqtt_client mqtt={0}; unlink(plates_path); new_process(); fresh(&mqtt);
    memory_a=grid(0.6); memory_b=grid(-0.3); memory_has_b=1; write_autosave(&memory_a,&memory_b);
    assert(call(SAVE,&mqtt,"A\nPlate A\n0\n60\n")==201);
    char a[17]; snprintf(a,sizeof(a),"%s",plates.plates[0].id);
    assert(call(SAVE,&mqtt,"B\nPlate B\n0\n60\n")==201);
    char b[17]; snprintf(b,sizeof(b),"%s",plates.plates[1].id);
    assert(plates_has_profile(plate_base(plate_find(a))) && plates_has_profile(plate_base(plate_find(b))));
    active_mesh=memory_a; snprintf(active_profile,sizeof(active_profile),"default"); profile_log[0]=0;
    assert(call(MOUNT,&mqtt,b)==200 && !strcmp(active_profile,"default1"));
    assert(plate_mesh_equal(&active_mesh,&memory_b,0) && !strcmp(plates.current,b) && !reboot_pending);
    assert(strstr(profile_log,"BED_MESH_PROFILE LOAD=default1"));
    size_t length; char *before=slurp(printer_autosave_path,&length);
    /* Acknowledged-but-ignored loads, missing data and a wrong matrix cannot mount. */
    ignore_mesh_load=1;
    assert(call(MOUNT,&mqtt,a)==503 && !plates.current[0] && !strcmp(plates_result,"verify_failed"));
    ignore_mesh_load=0; corrupt_active_matrix=1;
    assert(call(MOUNT,&mqtt,a)==503 && !plates.current[0]);
    corrupt_active_matrix=0; missing_active_matrix=1;
    assert(call(MOUNT,&mqtt,a)==503 && !plates.current[0]);
    missing_active_matrix=0; refuse_mesh_load=1;
    assert(call(MOUNT,&mqtt,a)==503 && !plates.current[0]);
    refuse_mesh_load=0;
    assert(call(MOUNT,&mqtt,a)==200 && !strcmp(active_profile,"default"));
    size_t after_length; char *after=slurp(printer_autosave_path,&after_length);
    assert(length==after_length && !memcmp(before,after,length)); free(before); free(after);
    profile_log[0]=0; mqtt.machine_status=2;
    assert(call(MOUNT,&mqtt,b)==409 && !profile_log[0]); fresh(&mqtt);
    mqtt.last_message-=16;
    assert(call(MOUNT,&mqtt,b)==409 && !profile_log[0]); fresh(&mqtt);
    printf("PASS: mounting stored B from active A loads and verifies the actual matrix; refusal/mismatch/missing data fail closed; busy/stale printer untouched\n");
}

/* Measurements at several bed temperatures, nozzles and the choice a print makes. */
static void test_measures(void) {
    mqtt_client mqtt = {0};
    plate_mesh at60 = grid(0.6), at80 = grid(0.66), at100 = grid(0.71), b = grid(0.3);
    write_autosave(&at60, &b); memory_a = at60; memory_b = b; memory_has_b = 1;
    unlink(plates_path); new_process(); fresh(&mqtt);
    assert(call(SAVE, &mqtt, "A\nCarbon\n0.045\n60") == 201);
    char carbon[17], first[17], body[128];
    snprintf(carbon, sizeof(carbon), "%s", plates.plates[0].id); snprintf(first, sizeof(first), "%s", plates.plates[0].measure);

    /* Nozzles: the selected one adds its correction to the mounted plate's offset. */
    assert(call(NOZZLE, &mqtt, "\n0.4 brass\n0.4\n0") == 200);
    char brass[17]; snprintf(brass, sizeof(brass), "%s", plates.nozzles[0].id);
    assert(call(NOZZLE, &mqtt, "\n0.6 hardened\n0.6\n0.02") == 200);
    char hardened[17]; snprintf(hardened, sizeof(hardened), "%s", plates.nozzles[1].id);
    assert(call(NOZZLE, &mqtt, "\n0.4 brass\n0.4\n0") == 409 && strstr(response, "already exists"));
    assert(call(NOZZLE, &mqtt, "\nHuge\n3\n0") == 400 && call(NOZZLE, &mqtt, "\nFar\n0.4\n0.6") == 400);
    snprintf(body, sizeof(body), "%s", carbon);
    assert(call(MOUNT, &mqtt, body) == 200 && !strcmp(ran, "SET_GCODE_OFFSET Z=0.045"));
    snprintf(body, sizeof(body), "%s", hardened);
    assert(call(NOZZLE_SELECT, &mqtt, body) == 200 && strstr(response, "\"applied\":true") &&
           !strcmp(ran, "SET_GCODE_OFFSET Z=0.065") && !strcmp(plates.nozzle, hardened));
    snprintf(body, sizeof(body), "%s\n0.6 hardened\n0.6\n-0.01", hardened);
    assert(call(NOZZLE, &mqtt, body) == 200 && !strcmp(ran, "SET_GCODE_OFFSET Z=0.035"));
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"z_effective\":0.035") &&
           strstr(response, "\"name\":\"0.6 hardened\",\"diameter\":0.60,\"z_offset\":-0.010"));
    /* The offset is kept with the nozzle while the printer is idle. */
    plates_z_valid = 0; ran[0] = 0; plates_tick(&mqtt); assert(!strcmp(ran, "SET_GCODE_OFFSET Z=0.035"));
    assert(call(NOZZLE_SELECT, &mqtt, "") == 200 && !strcmp(ran, "SET_GCODE_OFFSET Z=0.045") && !plates.nozzle[0]);

    /* Calibrations at other temperatures become separate measurements of the plate. */
    write_autosave(&at80, &b); memory_a = at80;
    assert(call(MEASURE, &mqtt, "0123456789abcdef\n80") == 404);
    snprintf(body, sizeof(body), "%s\n80\n%s", carbon, brass);
    assert(call(MEASURE, &mqtt, body) == 200 && strstr(response, "\"profile\":true"));
    assert(plate_measure_count(carbon) == 2 && plate_base(&plates.plates[0])->temp == 80);
    char m80[17]; snprintf(m80, sizeof(m80), "%s", plates.plates[0].measure);
    write_autosave(&at100, &b); memory_a = at100;
    /* The library file still has its earlier profiles; a calibration keeps them in the file. */
    char name[32]; plate_profile_name(name, sizeof(name), measure_find(first));
    assert(!plates_has_profile(measure_find(first)));
    write_autosave(&at60, &b); memory_a = at60;
    assert(call(KEEP, &mqtt, "A") == 200 && strstr(response, first) && plates_has_profile(measure_find(first)));
    snprintf(body, sizeof(body), "%s\n80\n%s", carbon, brass);
    assert(call(MEASURE, &mqtt, body) == 200 && plate_measure_count(carbon) == 2 && !strcmp(plates.plates[0].measure, m80));
    assert(plate_mesh_equal(&measure_find(m80)->mesh, &at60, 1)); /* the same temperature and nozzle are replaced */
    {   /* restore the 80 degree mesh and add 100 */
        size_t file_length, new_length; char *file = slurp(printer_autosave_path, &file_length);
        char *updated = autosave_with_mesh(file, file_length, "default", &at80, &new_length); assert(updated);
        FILE *f = fopen(printer_autosave_path, "wb"); fwrite(updated, 1, new_length, f); fclose(f); free(file); free(updated);
        memory_a = at80;
        assert(call(MEASURE, &mqtt, body) == 200);
        updated = NULL; file = slurp(printer_autosave_path, &file_length);
        updated = autosave_with_mesh(file, file_length, "default", &at100, &new_length); assert(updated);
        f = fopen(printer_autosave_path, "wb"); fwrite(updated, 1, new_length, f); fclose(f); free(file); free(updated);
        memory_a = at100;
        snprintf(body, sizeof(body), "%s\n100", carbon);
        assert(call(MEASURE, &mqtt, body) == 200);
    }
    char m100[17]; snprintf(m100, sizeof(m100), "%s", plates.plates[0].measure);
    assert(plate_measure_count(carbon) == 3 && !strcmp(plates.current, carbon));
    for (int i = 0; i < plates.measure_count; ++i) assert(plates_has_profile(&plates.measures[i]));
    /* Coolest first, with where each mesh is. */
    assert(call(GET, &mqtt, NULL) == 200);
    const char *t60 = strstr(response, "\"temp\":60"), *t80 = strstr(response, "\"temp\":80"), *t100 = strstr(response, "\"temp\":100");
    assert(t60 && t80 && t100 && t60 < t80 && t80 < t100);
    assert(strstr(t100, "\"slot\":true,\"profile\":true") && strstr(t60, "\"slot\":false,\"profile\":true"));

    /* The print dialog's choice. */
    char late[24];
    assert(!plates_print_prepare(&mqtt, 'A', 1, "", "", late) && !late[0]);
    assert(!plates_print_prepare(&mqtt, 'A', 1, m100, "", late) && !late[0]);       /* already in the slot */
    assert(!plates_print_prepare(&mqtt, 'A', 1, first, "", late) && !strcmp(late, first));
    assert(!plates_print_prepare(&mqtt, 'A', 0, first, "", late) && !late[0]);      /* the print probes */
    assert(plates_print_prepare(&mqtt, 'B', 1, first, "", late) && !late[0]);
    assert(plates_print_prepare(&mqtt, 'A', 1, "0123456789abcdef", "", late));
    assert(plates_print_prepare(&mqtt, 'A', 1, first, "0123456789abcdef", late));
    snprintf(body, sizeof(body), "%s", first);
    plate_mesh lost; plate_profile_name(name, sizeof(name), measure_find(first));
    {   /* a profile that no longer holds the mesh is not loaded */
        size_t file_length, new_length; char *file = slurp(printer_autosave_path, &file_length);
        lost = grid(0.2);
        char *updated = autosave_with_mesh(file, file_length, name, &lost, &new_length); assert(updated);
        FILE *f = fopen(printer_autosave_path, "wb"); fwrite(updated, 1, new_length, f); fclose(f); free(file);
        const char *problem = plates_print_prepare(&mqtt, 'A', 1, first, "", late);
        assert(problem && strstr(problem, "not stored") && !late[0]);
        file = slurp(printer_autosave_path, &file_length); free(updated);
        updated = autosave_with_mesh(file, file_length, name, &measure_find(first)->mesh, &new_length); assert(updated);
        f = fopen(printer_autosave_path, "wb"); fwrite(updated, 1, new_length, f); fclose(f); free(file); free(updated);
    }
    /* A nozzle chosen for the print is selected and its offset applied before the start. */
    ran[0] = 0;
    assert(!plates_print_prepare(&mqtt, 'A', 1, first, hardened, late) && !strcmp(late, first) &&
           !strcmp(plates.nozzle, hardened) && !strcmp(ran, "SET_GCODE_OFFSET Z=0.035"));

    /* The print start: the side slot is loaded, then the chosen profile replaces it. */
    plates_print_arm(late, 'A');
    profile_log[0] = 0;
    telemetry.have_print_state = 1; strcpy(telemetry.print_state, "complete");
    telemetry.have_mesh_profile = 1; strcpy(telemetry.mesh_profile, "default");
    telemetry.values[U_LAYER] = 67; telemetry.present |= UINT32_C(1) << U_LAYER;
    plates_tick(&mqtt); assert(plates_late.state == 1 && !profile_log[0]); /* not started yet */
    strcpy(telemetry.print_state, "printing");
    /* A stale layer count from the last print is neither the first layer nor proof of the start. */
    plates_tick(&mqtt); assert(plates_late.state == 2 && !plates_late.loads && !profile_log[0]);
    telemetry.values[U_LAYER] = 0;
    plates_tick(&mqtt); assert(plates_late.layer_reset && plates_late.loads == 1);
    assert(!strncmp(profile_log, "BED_MESH_PROFILE LOAD=cc2_", 26) && strstr(profile_log, first));
    plates_tick(&mqtt); assert(plates_late.loads == 1); /* waits for the push before loading again */
    plates_late.sent -= 2000;
    plates_tick(&mqtt); assert(plates_late.loads == 2);
    snprintf(telemetry.mesh_profile, sizeof(telemetry.mesh_profile), "cc2_%s", first);
    plates_late.sent -= 2000; plates_tick(&mqtt); assert(plates_late.loads == 2);
    strcpy(telemetry.mesh_profile, "default"); plates_late.sent -= 2000; /* G180 S7 */
    plates_tick(&mqtt); assert(plates_late.loads == 3);
    snprintf(telemetry.mesh_profile, sizeof(telemetry.mesh_profile), "cc2_%s", first);
    telemetry.values[U_LAYER] = 1;
    plates_tick(&mqtt); assert(!plates_late.state && !strcmp(plates_late_result, "loaded"));
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"print_mesh\":{\"state\":\"off\"") &&
           strstr(response, "\"result\":\"loaded\"") && strstr(response, "\"mesh_profile\":\"cc2_"));
    /* Too late: the first layer began with the slot mesh. */
    plates_print_arm(first, 'A'); strcpy(telemetry.mesh_profile, "default"); telemetry.values[U_LAYER] = 0;
    plates_tick(&mqtt); telemetry.values[U_LAYER] = 1; plates_tick(&mqtt);
    assert(!plates_late.state && !strcmp(plates_late_result, "missed"));
    /* Telemetry that reconnects after the first layer began never loads: the stale count
     * from the last print would have kept layer_reset false, and layer 0 is never read. */
    plates_print_arm(first, 'A'); telemetry.values[U_LAYER] = 120; strcpy(telemetry.mesh_profile, "default");
    plates_tick(&mqtt); assert(plates_late.state == 2);
    telemetry.present &= ~(UINT32_C(1) << U_LAYER); /* the stream reconnects */
    plates_late.sent = 0; plates_tick(&mqtt);
    telemetry.values[U_LAYER] = 3; telemetry.present |= UINT32_C(1) << U_LAYER; /* the first snapshot */
    for (int i = 0; i < 4; ++i) { plates_late.sent = 0; plates_tick(&mqtt); }
    assert(plates_late.state == 2 && !plates_late.loads);
    strcpy(telemetry.print_state, "complete"); plates_tick(&mqtt);
    assert(!plates_late.state && !strcmp(plates_late_result, "ended"));
    strcpy(telemetry.print_state, "printing");
    /* Nor while the print is paused before its first layer. */
    plates_print_arm(first, 'A'); telemetry.values[U_LAYER] = 0; strcpy(telemetry.print_state, "paused");
    plates_tick(&mqtt); assert(plates_late.state == 2 && !plates_late.loads);
    strcpy(telemetry.print_state, "printing"); plates_tick(&mqtt); assert(plates_late.loads == 1);
    /* A print that probes its own mesh is left alone. */
    plates_print_arm(first, 'A'); telemetry.values[U_LAYER] = 0; strcpy(telemetry.mesh_profile, "ADAPTIVE");
    plates_tick(&mqtt); assert(!plates_late.state && !strcmp(plates_late_result, "adaptive"));
    /* A cancelled start ends the watch; a refused or lost start times out. */
    plates_print_arm(first, 'A'); strcpy(telemetry.print_state, "cancelled");
    plates_tick(&mqtt); assert(plates_late.state == 1);
    plates_late.armed -= 181000; plates_tick(&mqtt); assert(!plates_late.state && !strcmp(plates_late_result, "not_started"));
    plates_print_arm("", 'A'); assert(!plates_late.state && !plates_late_result[0]);

    /* Measurements leave one by one; a plate keeps its last one. */
    snprintf(body, sizeof(body), "%s", m100);
    profile_log[0] = 0; measure_find(m80)->measured = 2000000000;
    assert(call(MEASURE_DELETE, &mqtt, body) == 200 && plate_measure_count(carbon) == 2);
    assert(strstr(profile_log, "REMOVE=cc2_") && strstr(profile_log, m100));
    assert(!strcmp(plates.plates[0].measure, m80)); /* the newest one left */
    snprintf(body, sizeof(body), "%s", m80); assert(call(MEASURE_DELETE, &mqtt, body) == 200);
    snprintf(body, sizeof(body), "%s", first);
    assert(call(MEASURE_DELETE, &mqtt, body) == 409 && strstr(response, "at least one"));
    /* A deleted nozzle is no longer named by measurements or selected. */
    measure_find(first)->nozzle[0] = 0; memcpy(measure_find(first)->nozzle, hardened, sizeof(hardened));
    assert(plates_save() == 0);
    snprintf(body, sizeof(body), "%s", hardened);
    assert(call(NOZZLE_DELETE, &mqtt, body) == 200 && !plates.nozzle[0] && !measure_find(first)->nozzle[0] &&
           plates.nozzle_count == 1);
    new_process(); assert(plates_available && plates.nozzle_count == 1 && plates.measure_count == 1);
    unlink(plates_path);
}

/* The console as the calibration job sees it: every command finishes at once. */
static console_state console;
static char console_log[512];
static int console_ok = 1;
static int fake_console(console_state *c, const char *command) {
    size_t used = strlen(console_log);
    snprintf(console_log + used, sizeof(console_log) - used, "%s%s", used ? "|" : "", command);
    pthread_mutex_lock(&c->lock);
    c->generation++; c->busy = 0; c->completed = 1; c->success = console_ok;
    snprintf(c->command, sizeof(c->command), "%s", command);
    pthread_mutex_unlock(&c->lock);
    return 0;
}
static int calibrate(const mqtt_client *mqtt, const char *body) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    plates_calibrate_response(pair[0], mqtt, &console, body, strlen(body));
    close(pair[0]);
    size_t used = 0; ssize_t n;
    while ((n = recv(pair[1], response + used, sizeof(response) - 1 - used, 0)) > 0) used += (size_t)n;
    response[used] = 0; close(pair[1]);
    return status_of(response);
}
/* What the firmware's calibration does to the file: only the slot changes. */
static void set_slot_a(const plate_mesh *m) {
    size_t length, new_length; char *file = slurp(printer_autosave_path, &length);
    char *updated = autosave_with_mesh(file, length, "default", m, &new_length); assert(updated);
    FILE *f = fopen(printer_autosave_path, "wb"); assert(f);
    assert(fwrite(updated, 1, new_length, f) == new_length); fclose(f); free(file); free(updated);
    memory_a = *m;
}
/* Heater switch-offs the job sends. */
static int beds_off;
static int fake_bed_off(void) { beds_off++; return 0; }
static void cancel_calibration(const mqtt_client *mqtt) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    plates_calibrate_cancel_response(pair[0], mqtt); close(pair[0]); close(pair[1]);
}
static void bed(double now, double target) {
    telemetry.values[U_BT] = now; telemetry.values[U_BG] = target;
    telemetry.present |= (UINT32_C(1) << U_BT) | (UINT32_C(1) << U_BG);
}

/* A calibration run here: homing, heating, the soak, probing and the plate measurement. */
static void test_calibration_job(void) {
    mqtt_client mqtt = {0};
    plate_mesh at60 = grid(0.6), at70 = grid(0.68), b = grid(0.3);
    write_autosave(&at60, &b); memory_a = at60; memory_b = b;
    unlink(plates_path); new_process(); fresh(&mqtt);
    pthread_mutex_init(&console.lock, NULL);
    plates_console = fake_console; plates_bed_off = fake_bed_off;
    assert(call(SAVE, &mqtt, "A\nCarbon\n0.05\n60") == 201);
    char plate[17], first[17], body[128];
    snprintf(plate, sizeof(plate), "%s", plates.plates[0].id); snprintf(first, sizeof(first), "%s", plates.plates[0].measure);
    assert(call(NOZZLE, &mqtt, "\n0.6 hardened\n0.6\n0") == 200);
    char nozzle[17]; snprintf(nozzle, sizeof(nozzle), "%s", plates.nozzles[0].id);

    assert(calibrate(&mqtt, "A\n70\n61") == 400 && calibrate(&mqtt, "A\n29\n10") == 400 && calibrate(&mqtt, "C\n70\n10") == 400);
    snprintf(body, sizeof(body), "B\n70\n10\n\n%s", plate);
    assert(calibrate(&mqtt, body) == 409 && strstr(response, "other side"));
    /* Unhomed: G28 first, then the bed heats; nothing probes before it holds the temperature for the soak. */
    snprintf(mqtt.homed_axes, sizeof(mqtt.homed_axes), "%s", "");
    profile_log[0] = console_log[0] = 0;
    snprintf(body, sizeof(body), "A\n70\n10\n%s\n%s", nozzle, plate);
    assert(calibrate(&mqtt, body) == 202 && plates_cal.stage == CAL_HOMING && !strcmp(console_log, "G28"));
    assert(plates_has_profile(measure_find(first))); /* the slot's measurement kept a profile first */
    assert(calibrate(&mqtt, body) == 409 && strstr(response, "already running"));
    char late[24];
    assert(plates_print_prepare(&mqtt, 'A', 1, "", "", late) && call(MOUNT, &mqtt, plate) == 409);
    plates_calibration_tick(&mqtt, &console); assert(plates_cal.stage == CAL_HOMING); /* waits for X, Y and Z */
    snprintf(mqtt.homed_axes, sizeof(mqtt.homed_axes), "%s", "xyz");
    plates_calibration_tick(&mqtt, &console);
    assert(plates_cal.stage == CAL_HEATING && !strcmp(console_log, "G28|M140 S70"));
    bed(45, 70); plates_calibration_tick(&mqtt, &console); assert(plates_cal.stage == CAL_HEATING);
    bed(69.4, 70); plates_calibration_tick(&mqtt, &console); assert(plates_cal.stage == CAL_SOAKING);
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"calibration\":{\"stage\":\"soaking\",\"side\":\"A\",\"temp\":70,\"soak\":600,\"remaining\":"));
    plates_calibration_tick(&mqtt, &console); assert(plates_cal.stage == CAL_SOAKING);
    plates_cal.soak_until = monotonic_ms() - 1;
    /* The end of the soak waits for fresh evidence: no bed telemetry, a bed below its target or stale
     * MQTT hold the probing back. */
    telemetry.present &= ~((UINT32_C(1) << U_BT) | (UINT32_C(1) << U_BG));
    plates_calibration_tick(&mqtt, &console); assert(plates_cal.stage == CAL_SOAKING && plates_cal.held);
    bed(66, 70); plates_calibration_tick(&mqtt, &console); assert(plates_cal.stage == CAL_SOAKING);
    bed(69.6, 70); mqtt.last_message -= 20; plates_calibration_tick(&mqtt, &console);
    assert(plates_cal.stage == CAL_SOAKING && !strstr(console_log, "BED_MESH_CALIBRATE"));
    fresh(&mqtt);
    plates_calibration_tick(&mqtt, &console);
    assert(plates_cal.stage == CAL_PROBING && !plates_cal.held && strstr(console_log, "|BED_MESH_CALIBRATE PROFILE=default BED_TEMP=70"));
    /* The firmware saves the new mesh in the slot; it becomes the plate's 70 degree measurement with the nozzle. */
    set_slot_a(&at70);
    plates_calibration_tick(&mqtt, &console); assert(plates_cal.stage == CAL_SAVING);
    plates_calibration_tick(&mqtt, &console);
    assert(!plates_cal.stage && !strcmp(plates_cal.result, "saved") && plate_measure_count(plate) == 2);
    const plate_measure *m = measure_find(plates_cal.measure);
    assert(m && m->temp == 70 && !strcmp(m->nozzle, nozzle) && plate_mesh_equal(&m->mesh, &at70, 1));
    assert(!strcmp(plates.current, plate) && !strcmp(plates.plates[0].measure, m->id) && plates_has_profile(m));
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"stage\":\"off\"") && strstr(response, "\"result\":\"saved\""));
    assert(!plates_print_prepare(&mqtt, 'A', 1, first, "", late) && !strcmp(late, first));

    /* Without a plate the result only stays in the printer; no soak probes at once. */
    console_log[0] = 0;
    assert(calibrate(&mqtt, "A\n60\n0") == 202 && !strcmp(console_log, "M140 S60"));
    plates_calibration_tick(&mqtt, &console); bed(60, 60); plates_calibration_tick(&mqtt, &console);
    plates_calibration_tick(&mqtt, &console); assert(plates_cal.stage == CAL_PROBING);
    plates_calibration_tick(&mqtt, &console); assert(!plates_cal.stage && !strcmp(plates_cal.result, "done"));
    assert(plate_measure_count(plate) == 2);

    /* Cancelled while soaking on an idle printer whose bed holds the job's target: the heater goes off. */
    assert(calibrate(&mqtt, "A\n80\n5") == 202); bed(80, 80);
    plates_calibration_tick(&mqtt, &console); plates_calibration_tick(&mqtt, &console);
    assert(plates_cal.stage == CAL_SOAKING);
    beds_off = 0; cancel_calibration(&mqtt);
    assert(!plates_cal.stage && !strcmp(plates_cal.result, "cancelled") && beds_off == 1);
    /* A print started by another client, a stale status, another target or no bed telemetry: the
     * job ends without touching the heater. */
    for (int c = 0; c < 5; ++c) {
        fresh(&mqtt); bed(80, 80);
        assert(calibrate(&mqtt, "A\n80\n5") == 202);
        plates_calibration_tick(&mqtt, &console); plates_calibration_tick(&mqtt, &console);
        assert(plates_cal.stage == CAL_SOAKING);
        if (c == 0) mqtt.machine_status = 2;           /* printing */
        if (c == 1) mqtt.machine_status = 3;           /* busy with something else (e.g. paused/loading) */
        if (c == 2) mqtt.last_message -= 20;           /* stale MQTT */
        if (c == 3) bed(80, 60);                       /* a target the job did not set */
        if (c == 4) telemetry.present &= ~(UINT32_C(1) << U_BG);
        beds_off = 0; cancel_calibration(&mqtt);
        assert(!plates_cal.stage && !strcmp(plates_cal.result, "cancelled") && beds_off == 0);
    }
    fresh(&mqtt);
    /* A heating failure on an idle printer still switches off what the job heated. */
    assert(calibrate(&mqtt, "A\n80\n5") == 202); bed(30, 80);
    plates_calibration_tick(&mqtt, &console); assert(plates_cal.stage == CAL_HEATING);
    beds_off = 0; plates_cal.since -= 31 * 60000; plates_calibration_tick(&mqtt, &console);
    assert(!strcmp(plates_cal.result, "failed") && !strcmp(plates_cal.error, "heating") && beds_off == 1);
    assert(calibrate(&mqtt, "A\n80\n5") == 202); bed(80, 80);
    plates_calibration_tick(&mqtt, &console); plates_calibration_tick(&mqtt, &console); bed(80, 0);
    plates_calibration_tick(&mqtt, &console);
    assert(!strcmp(plates_cal.result, "failed") && !strcmp(plates_cal.error, "heating"));
    /* Evidence that stays missing for a minute stops the run without touching a heater it cannot see. */
    assert(calibrate(&mqtt, "A\n80\n5") == 202); bed(80, 80);
    plates_calibration_tick(&mqtt, &console); plates_calibration_tick(&mqtt, &console);
    plates_cal.soak_until = monotonic_ms() - 1;
    telemetry.present &= ~((UINT32_C(1) << U_BT) | (UINT32_C(1) << U_BG));
    plates_calibration_tick(&mqtt, &console); assert(plates_cal.stage == CAL_SOAKING);
    beds_off = 0; plates_cal.held -= 61000; plates_calibration_tick(&mqtt, &console);
    assert(!plates_cal.stage && !strcmp(plates_cal.result, "failed") && !strcmp(plates_cal.error, "telemetry") && !beds_off);
    snprintf(mqtt.homed_axes, sizeof(mqtt.homed_axes), "%s", ""); console_ok = 0;
    assert(calibrate(&mqtt, "A\n80\n5") == 202); plates_calibration_tick(&mqtt, &console);
    assert(!strcmp(plates_cal.result, "failed") && !strcmp(plates_cal.error, "homing"));
    console_ok = 1; telemetry.present &= ~((UINT32_C(1) << U_BT) | (UINT32_C(1) << U_BG));

    /* What a measurement records can be corrected, e.g. the nozzle of one made before nozzles were listed. */
    snprintf(body, sizeof(body), "%s\n70\n%s", first, nozzle);
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    plates_measure_edit_response(pair[0], body, strlen(body)); close(pair[0]);
    ssize_t got = recv(pair[1], response, sizeof(response) - 1, 0); response[got > 0 ? got : 0] = 0; close(pair[1]);
    assert(status_of(response) == 409 && strstr(response, "already has a measurement"));
    snprintf(body, sizeof(body), "%s\n65\n%s", first, nozzle);
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    plates_measure_edit_response(pair[0], body, strlen(body)); close(pair[0]);
    got = recv(pair[1], response, sizeof(response) - 1, 0); response[got > 0 ? got : 0] = 0; close(pair[1]);
    assert(status_of(response) == 200 && measure_find(first)->temp == 65 && !strcmp(measure_find(first)->nozzle, nozzle));
    new_process(); assert(measure_find(first)->temp == 65);
    unlink(plates_path);
}

static void test_print_setup(void) {
    char directory[] = "/tmp/cc2-setup-XXXXXX", path[64];
    assert(mkdtemp(directory));
    snprintf(path, sizeof(path), "%s/job.gcode", directory);
    FILE *f = fopen(path, "w"); assert(f);
    fputs("; HEADER_BLOCK_START\n; generated by OrcaSlicer 2.4.2\n; HEADER_BLOCK_END\nM140 S0\n"
          "M104 S140\nM140 S70 ; set bed\nM190 S70 A\nG28\n", f);
    for (int i = 0; i < 20000; ++i) fprintf(f, "G1 X%d Y10 E0.1\n", i % 200);
    fputs("; CONFIG_BLOCK_START\n; bed_temperature_formula = by_highest_temp\n; nozzle_diameter = 0.6\n"
          "; nozzle_temperature = 250\n; CONFIG_BLOCK_END\n", f);
    fclose(f);
    double bed, nozzle;
    assert(gcode_read_setup(directory, "job.gcode", &bed, &nozzle) == 0 && bed == 70 && fabs(nozzle - 0.6) < 1e-9);
    unlink(path); rmdir(directory);
}

static void test_empty_plate(void) {
    mqtt_client mqtt = {0}; plate_mesh a = grid(0.6);
    plates_cal.stage = CAL_IDLE;
    write_autosave(&a, NULL); memory_a = a; memory_has_b = 0;
    unlink(plates_path); new_process(); fresh(&mqtt);
    size_t before_len; char *before = slurp(printer_autosave_path, &before_len);
    uds_down = 1; profile_log[0] = console_log[0] = 0;
    assert(call(13, &mqtt, "B\nPLA 30\n0") == 201);
    assert(plates.count == 1 && plates.measure_count == 0 && !plates.plates[0].measure[0]);
    char id[17]; snprintf(id, sizeof(id), "%s", plates.plates[0].id);
    size_t after_len; char *after = slurp(printer_autosave_path, &after_len);
    assert(before_len == after_len && !memcmp(before, after, before_len) && !profile_log[0] && !console_log[0]);
    free(before); free(after);
    new_process(); fresh(&mqtt); assert(plates_available && plates.count == 1 && !plate_base(&plates.plates[0]));
    assert(call(MOUNT, &mqtt, id) == 404 && !plates.current[0] && !plates.pending[0]);
    assert(call(13, &mqtt, "B\nPLA 30\n0") == 409);
    mqtt.machine_status = 2; assert(call(13, &mqtt, "B\nBusy\n0") == 409); fresh(&mqtt);
    assert(call(13, &mqtt, "C\nBad\n0") == 400 && call(13, &mqtt, "B\nBad\n2") == 400);
    uds_down = 0;
    char body[80]; snprintf(body, sizeof(body), "B\n30\n0\n\n%s", id);
    console_log[0] = 0; assert(calibrate(&mqtt, body) == 202 && plates_cal.temp == 30 && !strcmp(plates_cal.plate, id));
    cancel_calibration(&mqtt);
    /* Attach the first real measurement to the previously empty profile. */
    memory_b = grid(0.3); memory_has_b = 1; write_autosave(&a, &memory_b);
    int status, profile, applied; plate_measure *measure = NULL;
    assert(!plates_take_measure(&mqtt, id, 30, "", 0, &status, &measure, &profile, &applied));
    assert(measure && measure->temp == 30 && plates_consistent());
    new_process(); assert(plates_available && plate_base(&plates.plates[0]) && plates.measure_count == 1);
    unlink(plates_path);
}

static void test_background_transport(void) {
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    plates_uds=uds_query_json;plates_background_close();
    plates_background.fd=pair[0];plates_background.buffer=malloc(PLATES_REPLY_MAX+1);assert(plates_background.buffer);
    snprintf(plates_background.request,sizeof(plates_background.request),"%s",plates_profiles_query);
    plates_background.sent=strlen(plates_profiles_query);plates_background.id=203;
    plates_background.deadline=monotonic_ms()+2000;
    char *reply=NULL;size_t length=0;long long before=monotonic_ms();
    assert(plates_background_step(plates_profiles_query,&reply,&length)==0 && monotonic_ms()-before<100);
    const char *part="{\"id\":0}\003{\"id\":203,\"result\":";
    assert(write(pair[1],part,strlen(part))==(ssize_t)strlen(part));
    assert(plates_background_step(plates_profiles_query,&reply,&length)==0);
    assert(write(pair[1],"{}}\003",4)==4);
    assert(plates_background_step(plates_profiles_query,&reply,&length)==1);
    assert(strstr(reply,"\"id\":203") && !plates_background_active());free(reply);close(pair[1]);
    assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    plates_background.fd=pair[0];plates_background.buffer=malloc(PLATES_REPLY_MAX+1);assert(plates_background.buffer);
    snprintf(plates_background.request,sizeof(plates_background.request),"%s",plates_profiles_query);
    plates_background.sent=strlen(plates_profiles_query);plates_background.id=203;plates_background.deadline=monotonic_ms()-1;
    assert(plates_background_step(plates_profiles_query,&reply,&length)==-1 && !plates_background_active());
    close(pair[1]);plates_uds=fake_uds;
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
    if (getenv("CC2_TEST_ACTIVE_ONLY")) { test_active_mount(); return 0; }
    test_names_and_numbers();
    test_autosave();
    test_store();
    test_version_1();
    test_http();
    test_active_mount();
    test_measures();
    test_calibration_job();
    test_empty_plate();
    test_print_setup();
    test_background_transport();
    printf("PASS: plate library\n");
    return 0;
}
