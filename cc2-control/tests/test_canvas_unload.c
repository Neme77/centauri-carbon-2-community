#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

/* The firmware's own layout, as SAVE_CONFIG writes it. */
static const char AUTOSAVE[] =
    "#*# <---------------------- SAVE_CONFIG ---------------------->\n"
    "#*# DO NOT EDIT THIS BLOCK OR BELOW. The contents are auto-generated.\n"
    "#*#\n"
    "#*# [input_shaper]\n"
    "#*# shaper_type_x = zv\n"
    "#*# shaper_freq_x = 52.600000\n"
    "#*#\n"
    "#*#\n"
    "#*# [stepper_z]\n"
    "#*# position_endstop = 5.539131\n";

static const char VENDOR[] =
    "[extruder]\n"
    "unload_fila_dist=10\n"
    "[canvas_dev]\n"
    "serial=/dev/ttyS1\n"
    "unload_fila_dist_2=5\n"
    "unload_fila_dist=80    # 退料距离 范围(60-350）\n"
    "  unload_fila_dist : 85 # the last one wins\n"
    "unload_fila_time=30#1\n"
    "[stepper_x]\n"
    "unload_fila_dist=20\n";

static char response[4096];
static int status_of(const char *text) { int status = 0; sscanf(text, "HTTP/1.1 %d", &status); return status; }

static void write_file(const char *path, const char *text) {
    FILE *f = fopen(path, "wb"); assert(f);
    assert(fwrite(text, 1, strlen(text), f) == strlen(text)); fclose(f);
}

static char *read_all(const char *path) {
    size_t length = 0; char *text = plates_read_file(path, AUTOSAVE_FILE_MAX, &length);
    assert(text); return text;
}

#define GET 0
#define SAVE 1
#define RESTART 2
static int call(int handler, mqtt_client *mqtt, const char *body) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    size_t length = body ? strlen(body) : 0;
    if (handler == GET) canvas_unload_response(pair[0]);
    else if (handler == SAVE) canvas_unload_save_response(pair[0], mqtt, body, length);
    else canvas_unload_restart_response(pair[0], mqtt, body, length);
    close(pair[0]);
    size_t used = 0; ssize_t n;
    while ((n = recv(pair[1], response + used, sizeof(response) - 1 - used, 0)) > 0) used += (size_t)n;
    response[used] = 0; close(pair[1]);
    return status_of(response);
}

static void fresh(mqtt_client *mqtt) {
    mqtt->connected = mqtt->registered = 1; mqtt->have_machine_status = 1; mqtt->machine_status = 1;
    mqtt->last_message = time(NULL);
    telemetry.fd = 0; telemetry.ready = 1; clock_gettime(CLOCK_MONOTONIC, &telemetry.last_rx);
}

static pid_t refuse_launch(void) { assert(!"the guard must stop this reboot"); return -1; }

static char *rewrite(const char *text, int value) {
    size_t length = 0; char *copy = canvas_unload_rewrite(text, strlen(text), value, &length);
    if (copy) assert(strlen(copy) == length);
    return copy;
}

static void test_vendor_value(void) {
    double value = 0;
    assert(canvas_unload_configured(VENDOR, strlen(VENDOR), &value) && value == 85);
    const char none[] = "[canvas_dev]\nunload_fila_dist_2=5\n# unload_fila_dist=90\n[x]\nunload_fila_dist=91\n";
    assert(!canvas_unload_configured(none, strlen(none), &value));
    const char bad[] = "[canvas_dev]\nunload_fila_dist=far\n";
    assert(!canvas_unload_configured(bad, strlen(bad), &value));
}

static void test_rewrite(void) {
    /* A new section goes after the last one, separated the way SAVE_CONFIG separates them. */
    char *added = rewrite(AUTOSAVE, 300);
    assert(added && !strcmp(added, "#*# <---------------------- SAVE_CONFIG ---------------------->\n"
                                   "#*# DO NOT EDIT THIS BLOCK OR BELOW. The contents are auto-generated.\n"
                                   "#*#\n#*# [input_shaper]\n#*# shaper_type_x = zv\n#*# shaper_freq_x = 52.600000\n"
                                   "#*#\n#*#\n#*# [stepper_z]\n#*# position_endstop = 5.539131\n"
                                   "#*#\n#*#\n#*# [canvas_dev]\n#*# unload_fila_dist = 300\n"));
    /* Replacing changes only that line; removing restores the original bytes. */
    char *changed = rewrite(added, 120);
    assert(changed && strstr(changed, "#*# unload_fila_dist = 120\n") && !strstr(changed, "= 300") &&
           strlen(changed) == strlen(added));
    char *removed = rewrite(changed, 0);
    assert(removed && !strcmp(removed, AUTOSAVE));
    free(added); free(changed); free(removed);

    /* A section in the middle, and one holding other keys, which stay. */
    const char middle[] =
        "#*# <---------------------- SAVE_CONFIG ---------------------->\n#*#\n"
        "#*# [canvas_dev]\n#*# unload_fila_dist = 200\n#*#\n#*#\n#*# [stepper_z]\n#*# position_endstop = 5\n";
    removed = rewrite(middle, 0);
    assert(removed && !strcmp(removed, "#*# <---------------------- SAVE_CONFIG ---------------------->\n"
                                       "#*#\n#*#\n#*# [stepper_z]\n#*# position_endstop = 5\n"));
    free(removed);
    const char shared[] =
        "#*# <---------------------- SAVE_CONFIG ---------------------->\n"
        "#*# [canvas_dev]\n#*# load_fila_dist = 900\n";
    added = rewrite(shared, 350);
    assert(added && !strcmp(added, "#*# <---------------------- SAVE_CONFIG ---------------------->\n"
                                   "#*# [canvas_dev]\n#*# unload_fila_dist = 350\n#*# load_fila_dist = 900\n"));
    removed = rewrite(added, 0);
    assert(removed && !strcmp(removed, shared));
    free(added); free(removed);

    /* A file that ends without a newline keeps doing so on that line. */
    const char tail[] = "#*# <---------------------- SAVE_CONFIG ---------------------->\n"
                        "#*# [canvas_dev]\n#*# unload_fila_dist = 61";
    changed = rewrite(tail, 62);
    assert(changed && !strcmp(changed, "#*# <---------------------- SAVE_CONFIG ---------------------->\n"
                                       "#*# [canvas_dev]\n#*# unload_fila_dist = 62"));
    free(changed);
    const char short_tail[] = "#*# <---------------------- SAVE_CONFIG ---------------------->";
    added = rewrite(short_tail, 70);
    assert(added && !strcmp(added, "#*# <---------------------- SAVE_CONFIG ---------------------->\n"
                                   "#*#\n#*#\n#*# [canvas_dev]\n#*# unload_fila_dist = 70\n"));
    free(added);

    /* Removing what is not there changes nothing. */
    removed = rewrite(AUTOSAVE, 0);
    assert(removed && !strcmp(removed, AUTOSAVE));
    free(removed);

    /* Files this code does not edit. */
    assert(!rewrite("[printer]\nkinematics = corexy\n", 100));
    assert(!rewrite("#*# <---------------------- SAVE_CONFIG ---------------------->\r\n#*# [x]\r\n", 100));
    assert(!rewrite("#*# <---------------------- SAVE_CONFIG ---------------------->\n"
                    "#*# [canvas_dev]\n#*# unload_fila_dist = 100\n#*# [canvas_dev]\n#*# unload_fila_dist = 90\n", 120));
    assert(!rewrite("#*# <---------------------- SAVE_CONFIG ---------------------->\n"
                    "#*# [canvas_dev]\n#*# unload_fila_dist = 100\n#*# unload_fila_dist = 90\n", 120));
    assert(!rewrite("#*# <---------------------- SAVE_CONFIG ---------------------->\n"
                    "#*# [canvas_dev]\n#*# unload_fila_dist = lots\n", 120));
    /* A section before the marker is the printer configuration's, not the SAVE_CONFIG block's. */
    const char *ambiguous[] = {
        "#*# [canvas_dev] \n#*# unload_fila_dist = 100\n",
        "#*#  [canvas_dev]\n#*# unload_fila_dist = 100\n",
        "#*#[canvas_dev]\n#*# unload_fila_dist = 100\n",
        "#*# [canvas_dev]\n#*#  unload_fila_dist = 100\n",
        "#*# [canvas_dev]\n#*#\tunload_fila_dist = 100\n",
        "#*# [canvas_dev]\n#*#unload_fila_dist = 100\n",
        "#*# [canvas_dev]\n#*# unload_fila_dist = 100\n#*#  [canvas_dev]\n#*# unload_fila_dist = 90\n",
    };
    for (size_t i = 0; i < sizeof(ambiguous) / sizeof(*ambiguous); ++i) {
        char file[512];
        snprintf(file, sizeof(file), "%s%s", AUTOSAVE, ambiguous[i]);
        assert(!rewrite(file, 120));
        assert(!rewrite(file, 0));
    }
    added = rewrite("[canvas_dev]\nunload_fila_dist = 80\n#*# <---------------------- SAVE_CONFIG ---------------------->\n", 90);
    assert(added && strstr(added, "[canvas_dev]\nunload_fila_dist = 80\n") && strstr(added, "#*# unload_fila_dist = 90\n"));
    free(added);
}

static void test_http(void) {
    mqtt_client mqtt; memset(&mqtt, 0, sizeof(mqtt)); fresh(&mqtt);
    printer_autosave_path = "autosave.cfg"; printer_config_path = "printer_dsp.cfg";
    canvas_unload_pending_path = "canvas-unload-pending"; boot_id_path = "boot_id";
    write_file("autosave.cfg", AUTOSAVE); write_file("printer_dsp.cfg", VENDOR); write_file("boot_id", "boot-one\n");
    unlink("autosave_backup.cfg"); unlink("canvas-unload-pending");

    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"editable\":true") &&
           strstr(response, "\"min\":60,\"max\":350") && strstr(response, "\"configured\":85") &&
           strstr(response, "\"override\":null") && strstr(response, "\"pending_restart\":false") &&
           strstr(response, "\"restart\":\"none\""));

    const char *refused[] = {"", "59", "351", "1000", "abc", "1e2", "-80", "80.5", "0", "default ", "Default"};
    for (size_t i = 0; i < sizeof(refused) / sizeof(*refused); ++i) {
        int status = call(SAVE, &mqtt, refused[i]);
        assert(status == 400 || (status == 200 && !strcmp(refused[i], "default ")));
    }
    char *unchanged = read_all("autosave.cfg"); assert(!strcmp(unchanged, AUTOSAVE)); free(unchanged);

    mqtt.machine_status = 2; assert(call(SAVE, &mqtt, "300") == 409 && strstr(response, "idle"));
    fresh(&mqtt); mqtt.last_message = time(NULL) - 60; assert(call(SAVE, &mqtt, "300") == 409);
    fresh(&mqtt); assert(call(RESTART, &mqtt, "RESTART") == 409 && strstr(response, "waiting"));

    assert(call(SAVE, &mqtt, " 300\n") == 200 && strstr(response, "\"override\":300") &&
           strstr(response, "\"configured\":85") && strstr(response, "\"pending_restart\":true"));
    char *saved = read_all("autosave.cfg"), *backup = read_all("autosave_backup.cfg");
    assert(strstr(saved, "#*# [canvas_dev]\n#*# unload_fila_dist = 300\n") && !strcmp(backup, AUTOSAVE));
    free(saved); free(backup);
    assert(call(SAVE, &mqtt, "300") == 200 && strstr(response, "\"override\":300")); /* nothing to change */

    /* A layout it does not edit is reported and left alone. */
    write_file("autosave.cfg", "[printer]\n");
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"editable\":false") && strstr(response, "\"override\":null"));
    assert(call(SAVE, &mqtt, "200") == 409);
    char *foreign = read_all("autosave.cfg"); assert(!strcmp(foreign, "[printer]\n")); free(foreign);
    write_file("autosave.cfg", AUTOSAVE);
    assert(call(SAVE, &mqtt, "300") == 200);

    /* The restart needs its confirmation and an idle printer, then waits for the reboot guard. */
    assert(call(RESTART, &mqtt, "restart") == 400 && !reboot_pending);
    mqtt.machine_status = 2; assert(call(RESTART, &mqtt, "RESTART") == 409 && !reboot_pending);
    fresh(&mqtt);
    assert(call(RESTART, &mqtt, "RESTART\n") == 202 && reboot_pending && reboot_guard == plates_reboot_guard);
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"restart\":\"restarting\""));
    assert(call(SAVE, &mqtt, "200") == 409 && strstr(response, "restart is already pending"));
    /* A print started meanwhile stops the reboot; the page learns why, once. */
    reboot_launcher = refuse_launch; mqtt.machine_status = 2; reboot_due = recovery_clock() - 1;
    recovery_tick();
    assert(!reboot_pending && !strcmp(reboot_error, "printer_busy"));
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"restart\":\"printer_busy\"") &&
           strstr(response, "\"pending_restart\":true"));
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"restart\":\"none\""));
    reboot_guard = NULL; reboot_error = "none"; fresh(&mqtt);

    /* After the next boot the saved value is the one in effect. */
    write_file("boot_id", "boot-two\n");
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"pending_restart\":false") &&
           strstr(response, "\"override\":300") && access("canvas-unload-pending", F_OK) != 0);

    /* Default removes the override and restores the file. */
    assert(call(SAVE, &mqtt, "default") == 200 && strstr(response, "\"override\":null") &&
           strstr(response, "\"pending_restart\":true"));
    char *restored = read_all("autosave.cfg"); assert(!strcmp(restored, AUTOSAVE)); free(restored);

    /* Failure to persist restart tracking must never change autosave.cfg. */
    unlink("canvas-unload-pending");
    canvas_unload_pending_path = "missing-directory/pending";
    assert(call(SAVE, &mqtt, "200") == 500 && strstr(response, "configuration unchanged"));
    restored = read_all("autosave.cfg"); assert(!strcmp(restored, AUTOSAVE)); free(restored);
    canvas_unload_pending_path = "canvas-unload-pending";
    boot_id_path = "missing-boot-id";
    assert(call(SAVE, &mqtt, "200") == 500 && strstr(response, "configuration unchanged"));
    restored = read_all("autosave.cfg"); assert(!strcmp(restored, AUTOSAVE)); free(restored);
    boot_id_path = "boot_id";

    /* If the configuration commit fails, remove only a newly-created marker.
     * Preserve an existing pending change, which still needs a reboot. */
    assert(!mkdir("autosave.cfg.cc2-new", 0700));
    assert(call(SAVE, &mqtt, "200") == 500);
    assert(access("canvas-unload-pending", F_OK) != 0);
    write_file("canvas-unload-pending", "boot-two");
    assert(call(SAVE, &mqtt, "200") == 500 && canvas_unload_pending());
    assert(!rmdir("autosave.cfg.cc2-new"));
    restored = read_all("autosave.cfg"); assert(!strcmp(restored, AUTOSAVE)); free(restored);

    /* An unreadable configuration is reported as unknown. */
    unlink("printer_dsp.cfg");
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"configured\":null"));
}

static int sha256_file(const char *path, char out[65]) {
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    int ok = sha256_fd(fd, out);
    close(fd);
    return ok;
}

static void test_sha256(void) {
    const char *vectors[][2] = {
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
    };
    char digest[65];
    for (size_t i = 0; i < sizeof(vectors) / sizeof(*vectors); ++i) {
        write_file("sha-input", vectors[i][0]);
        assert(sha256_file("sha-input", digest) && !strcmp(digest, vectors[i][1]));
    }
    /* One million "a": many 64 KiB reads and a final partial block. */
    FILE *f = fopen("sha-input", "wb"); assert(f);
    for (int i = 0; i < 1000000; ++i) fputc('a', f);
    fclose(f);
    assert(sha256_file("sha-input", digest) && !strcmp(digest, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    assert(!sha256_file("missing-file", digest));
    unlink("sha-input");
}

/* GET until the program check has finished. */
static void settle(mqtt_client *mqtt) {
    for (int i = 0; i < 500; ++i) {
        assert(call(GET, mqtt, NULL) == 200);
        pthread_mutex_lock(&canvas_unload_check_lock);
        int checking = canvas_unload_checking;
        pthread_mutex_unlock(&canvas_unload_check_lock);
        if (!checking) { assert(call(GET, mqtt, NULL) == 200); return; }
        nanosleep(&(struct timespec){0, 10000000}, NULL);
    }
    assert(!"program check did not finish");
}

static void test_extended(void) {
    mqtt_client mqtt; memset(&mqtt, 0, sizeof(mqtt)); fresh(&mqtt);
    write_file("autosave.cfg", AUTOSAVE); unlink("canvas-unload-pending");
    char digest[65];

    /* The running program matches the fingerprint: 1000 mm is allowed, 1001 is not. */
    write_file("printer-program", "elegoo_printer with unload_fila_dist max 1000.0");
    assert(sha256_file("printer-program", digest));
    canvas_unload_extended_sha256 = digest; canvas_unload_program = "printer-program";
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"max\":350,\"extended\":false"));
    settle(&mqtt);
    assert(strstr(response, "\"max\":1000,\"extended\":true"));
    assert(call(SAVE, &mqtt, "1001") == 400 && strstr(response, "from 60 to 1000"));
    assert(call(SAVE, &mqtt, "10000") == 400);
    assert(call(SAVE, &mqtt, "1000") == 200 && strstr(response, "\"override\":1000"));
    assert(call(SAVE, &mqtt, "560") == 200 && strstr(response, "\"override\":560"));

    /* Any other program, including a changed one, keeps the stock range. */
    write_file("printer-program", "elegoo_printer, stock V4.4 build");
    settle(&mqtt);
    assert(strstr(response, "\"max\":350,\"extended\":false"));
    assert(call(SAVE, &mqtt, "600") == 400 && strstr(response, "from 60 to 350"));
    assert(call(SAVE, &mqtt, "350") == 200 && strstr(response, "\"override\":350"));
    canvas_unload_program = "missing-program";
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"max\":350,\"extended\":false"));
    unlink("printer-program");
}

/* A result is kept only for the exact process and file it was computed for. */
/* Runs on the check thread; only the first check sees the replacement, so the hook can stay set. */
static void replace_program_during_check(void) {
    static int done;
    if (!done++) write_file("printer-program", "elegoo_printer, stock V4.4 build!");
}

static void test_extended_revalidation(void) {
    mqtt_client mqtt; memset(&mqtt, 0, sizeof(mqtt)); fresh(&mqtt);
    write_file("autosave.cfg", AUTOSAVE); unlink("canvas-unload-pending");
    const char *patched = "elegoo_printer with unload_fila_dist max 1000.0";
    char digest[65];
    write_file("printer-program", patched);
    assert(sha256_file("printer-program", digest));
    canvas_unload_extended_sha256 = digest; canvas_unload_program = "printer-program";

    /* The program changes while it is being hashed: the match is not kept, and the
     * program running afterwards is checked on its own. */
    canvas_unload_after_hash = replace_program_during_check;
    settle(&mqtt);
    assert(strstr(response, "\"max\":350,\"extended\":false"));
    write_file("printer-program", patched);
    settle(&mqtt);
    assert(strstr(response, "\"max\":1000,\"extended\":true"));

    /* Rewritten with the same size and its modification time put back: the change time
     * still differs, so the earlier result no longer applies. */
    struct stat st;
    assert(stat("printer-program", &st) == 0);
    nanosleep(&(struct timespec){0, 20000000}, NULL);
    write_file("printer-program", "elegoo_printer with unload_fila_dist max 350.00");
    struct timespec times[2] = {st.st_atim, st.st_mtim};
    assert(utimensat(AT_FDCWD, "printer-program", times, 0) == 0);
    /* A save checks the running program again instead of trusting the last answer. */
    assert(call(SAVE, &mqtt, "600") == 400 && strstr(response, "from 60 to 350"));
    settle(&mqtt);
    assert(strstr(response, "\"max\":350,\"extended\":false"));
    canvas_unload_program = "missing-program";
    unlink("printer-program");
}

/* Starts ./elegoo_printer (a copy of this test, which then only waits) and waits until it runs under that name. */
static pid_t start_printer(void) {
    pid_t pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
        execl("./elegoo_printer", "elegoo_printer", "--stand-in-printer", (char *)NULL);
        _exit(127);
    }
    char path[64], comm[32];
    snprintf(path, sizeof(path), "/proc/%d/comm", (int)pid);
    for (int i = 0; i < 500; ++i) {
        FILE *f = fopen(path, "r");
        int named = f && fgets(comm, sizeof(comm), f) && !strcmp(comm, "elegoo_printer\n");
        if (f) fclose(f);
        if (named) return pid;
        nanosleep(&(struct timespec){0, 2000000}, NULL);
    }
    assert(!"the stand-in printer did not start");
    return -1;
}

static void stop_printer(pid_t pid) { kill(pid, SIGKILL); waitpid(pid, NULL, 0); }

/* The real lookup through /proc: one process named elegoo_printer, its program file
 * and its start time. */
static void test_running_printer(void) {
    mqtt_client mqtt; memset(&mqtt, 0, sizeof(mqtt)); fresh(&mqtt);
    size_t length = 0;
    char *program = plates_read_file("/proc/self/exe", 256 * 1024 * 1024, &length);
    assert(program);
    int out = open("elegoo_printer", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0755);
    assert(out >= 0 && write(out, program, length) == (ssize_t)length && close(out) == 0);
    free(program);
    char digest[65];
    assert(sha256_file("elegoo_printer", digest));
    canvas_unload_extended_sha256 = digest; canvas_unload_program = NULL;

    pid_t first = start_printer();
    settle(&mqtt);
    assert(strstr(response, "\"max\":1000,\"extended\":true"));
    /* Two printer processes: uncertain, so the stock range. */
    pid_t second = start_printer();
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"max\":350,\"extended\":false"));
    stop_printer(second);
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"max\":1000,\"extended\":true"));
    /* A restarted printer is another process, checked again before 1000 mm applies. */
    stop_printer(first);
    first = start_printer();
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"max\":350,\"extended\":false"));
    settle(&mqtt);
    assert(strstr(response, "\"max\":1000,\"extended\":true"));
    stop_printer(first);
    assert(call(GET, &mqtt, NULL) == 200 && strstr(response, "\"max\":350,\"extended\":false"));
    canvas_unload_program = "missing-program";
    unlink("elegoo_printer");
}

static void test_start_time(void) {
    unsigned long long start = 0;
    const char *stat_line = "812 (elegoo_printer) S 1 812 812 0 -1 4194560 9182 0 0 0 1234 567 0 0 20 0 9 0 4242 "
                            "301989888 4510 4294967295 65536 6123456 0\n";
    assert(canvas_unload_parse_start_time(stat_line, &start) && start == 4242);
    /* A process name may hold spaces and parentheses; fields count from the last ")". */
    const char *odd = "77 (a) b (c) S 1 77 77 0 -1 0 0 0 0 0 0 0 0 0 20 0 1 0 99 1 1 1\n";
    assert(canvas_unload_parse_start_time(odd, &start) && start == 99);
    assert(!canvas_unload_parse_start_time("77 (short) S 1 2 3\n", &start));
    assert(!canvas_unload_parse_start_time("no parenthesis", &start));
    assert(!canvas_unload_parse_start_time("1 (x) S 1 1 1 0 -1 0 0 0 0 0 0 0 0 0 20 0 1 0 abc 1\n", &start));
}

int main(int argc, char **argv) {
    if (argc > 1 && !strcmp(argv[1], "--stand-in-printer")) { for (;;) pause(); }
    /* Never the host's processes: tests name the printer program explicitly. */
    canvas_unload_program = "missing-program";
    test_vendor_value();
    test_rewrite();
    test_http();
    test_sha256();
    test_extended();
    test_extended_revalidation();
    test_running_printer();
    test_start_time();
    puts("PASS: Canvas pull-back override: vendor value, SAVE_CONFIG-layout edits and exact removal, refused layouts, "
         "range/idle checks, backup, pending restart by boot id, guarded reboot, 1000 mm range only for the "
         "fingerprinted running program, rechecked for in-flight and same-size changes and before saving, one printer process "
         "with its start time, SHA-256 vectors");
    return 0;
}


