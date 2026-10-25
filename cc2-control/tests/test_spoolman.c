#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

static time_t fake_now = 1790000000;
static long long clock_fake(void) { return 1000000; }
static time_t wall_fake(time_t *t) { if (t) *t = fake_now; return fake_now; }
static void sleep_ms(long ms) { struct timespec ts = {0, ms * 1000000L}; nanosleep(&ts, NULL); }

static int near(double a, double b) { return fabs(a - b) < 1e-6; }

/* ---- spoolman_parse_url -------------------------------------------------------------------- */

static void test_parse_url(void) {
    char host[SPOOLMAN_HOST_MAX], base[SPOOLMAN_PATH_MAX]; int port;
    assert(spoolman_parse_url("http://192.0.2.5:7912", host, sizeof(host), &port, base, sizeof(base)));
    assert(!strcmp(host, "192.0.2.5") && port == 7912 && !base[0]);

    assert(spoolman_parse_url("http://192.0.2.5", host, sizeof(host), &port, base, sizeof(base)));
    assert(!strcmp(host, "192.0.2.5") && port == 80 && !base[0]);

    assert(spoolman_parse_url("http://192.0.2.5:7912/spoolman", host, sizeof(host), &port, base, sizeof(base)));
    assert(!strcmp(host, "192.0.2.5") && port == 7912 && !strcmp(base, "/spoolman"));

    assert(!spoolman_parse_url("https://192.0.2.5", host, sizeof(host), &port, base, sizeof(base))); /* no TLS stack */
    assert(!spoolman_parse_url("192.0.2.5:7912", host, sizeof(host), &port, base, sizeof(base)));    /* needs the scheme */
    assert(!spoolman_parse_url("http://", host, sizeof(host), &port, base, sizeof(base)));
    assert(!spoolman_parse_url("http://host:0", host, sizeof(host), &port, base, sizeof(base)));
    assert(!spoolman_parse_url("http://host:99999", host, sizeof(host), &port, base, sizeof(base)));
    puts("PASS: spoolman_parse_url accepts plain HTTP host[:port][/base], rejects the rest");
}

/* ---- persistence: new field is optional, old files still load ----------------------------- */

static void test_persistence_and_backward_compat(void) {
    static char path[64];
    snprintf(path, sizeof(path), "spoolman-test-%ld.json", (long)getpid());
    spools_path = path;
    unlink(path);
    memset(&spools, 0, sizeof(spools));
    spools.job.last_tray = -1;
    spool_entry *sp = &spools.spools[spools.count++];
    snprintf(sp->id, sizeof(sp->id), "aaaabbbbccccddd1");
    snprintf(sp->name, sizeof(sp->name), "Test"); snprintf(sp->material, sizeof(sp->material), "PLA");
    snprintf(sp->color, sizeof(sp->color), "#123456");
    sp->diameter = 1.75; sp->density = 1.24; sp->net = 1000; sp->remaining = 800;
    snprintf(sp->spoolman_id, sizeof(sp->spoolman_id), "42");
    snprintf(spools.spoolman_url, sizeof(spools.spoolman_url), "http://192.0.2.9:7912");
    snprintf(spools.spoolman_basic_auth, sizeof(spools.spoolman_basic_auth), "user:pass");
    spools_available = 1;
    assert(spools_save() == 0);

    spools_load();
    assert(spools_available && spools.count == 1);
    assert(!strcmp(spools.spools[0].spoolman_id, "42"));
    assert(!strcmp(spools.spoolman_url, "http://192.0.2.9:7912"));
    assert(!strcmp(spools.spoolman_basic_auth, "user:pass"));
    puts("PASS: spoolman_id and the sync config round-trip through save/load");

    /* A file saved by a version of CC2 Control before this feature existed:
     * no spoolman_* keys anywhere. Must still load every spool, not just the
     * new field -- the real risk this guards against. */
    FILE *f = fopen(path, "wb");
    assert(f);
    fputs("{\"version\":1,\"enabled\":false,\"slots\":["
          "{\"spool\":\"\",\"last\":\"\",\"question\":0,\"question_since\":0,\"question_mm\":0,\"question_job\":\"\",\"seen\":null},"
          "{\"spool\":\"\",\"last\":\"\",\"question\":0,\"question_since\":0,\"question_mm\":0,\"question_job\":\"\",\"seen\":null},"
          "{\"spool\":\"\",\"last\":\"\",\"question\":0,\"question_since\":0,\"question_mm\":0,\"question_job\":\"\",\"seen\":null},"
          "{\"spool\":\"\",\"last\":\"\",\"question\":0,\"question_since\":0,\"question_mm\":0,\"question_job\":\"\",\"seen\":null},"
          "{\"spool\":\"\",\"last\":\"\",\"question\":0,\"question_since\":0,\"question_mm\":0,\"question_job\":\"\",\"seen\":null}],"
          "\"job\":{\"active\":false,\"baseline\":false,\"last_tray\":-1,\"file\":\"\",\"uuid\":\"\",\"used\":0,\"duration\":0,"
          "\"between\":0,\"started\":0,\"result\":\"\",\"slots\":["
          "{\"mm\":0,\"grams\":0,\"spool\":\"\"},{\"mm\":0,\"grams\":0,\"spool\":\"\"},{\"mm\":0,\"grams\":0,\"spool\":\"\"},"
          "{\"mm\":0,\"grams\":0,\"spool\":\"\"},{\"mm\":0,\"grams\":0,\"spool\":\"\"}]},"
          "\"spools\":[{\"id\":\"aaaabbbbccccddd1\",\"name\":\"Old\",\"brand\":\"\",\"material\":\"PLA\",\"note\":\"\","
          "\"color\":\"#123456\",\"diameter\":1.75,\"density\":1.24,\"net\":1000,\"remaining\":800,\"tare\":0,\"low\":0,"
          "\"price\":0,\"created\":1,\"used\":1,\"archived\":false}],\"log\":[]}\n", f);
    fclose(f);
    spools_load();
    assert(spools_available && spools.count == 1);
    assert(!strcmp(spools.spools[0].name, "Old"));
    assert(!spools.spools[0].spoolman_id[0]);   /* missing key -> not linked, not a load failure */
    assert(!spools.spoolman_url[0]);
    unlink(path);
    puts("PASS: a save file from before Spoolman sync existed still loads every spool");
}

/* ---- charge accumulates spoolman_unsent only for linked spools ----------------------------- */

static void test_charge_accumulates_only_when_linked(void) {
    memset(&spools, 0, sizeof(spools));
    spools.job.last_tray = -1;
    spool_entry *linked = &spools.spools[spools.count++];
    snprintf(linked->id, sizeof(linked->id), "1111111111111111");
    linked->diameter = 1.75; linked->density = 1.24; linked->remaining = 1000;
    snprintf(linked->spoolman_id, sizeof(linked->spoolman_id), "7");
    spool_entry *unlinked = &spools.spools[spools.count++];
    snprintf(unlinked->id, sizeof(unlinked->id), "2222222222222222");
    unlinked->diameter = 1.75; unlinked->density = 1.24; unlinked->remaining = 1000;

    spool_copy_id(spools.slots[0].spool, linked->id);
    spool_copy_id(spools.slots[1].spool, unlinked->id);
    spools_charge(0, 100);
    spools_charge(1, 100);
    assert(linked->spoolman_unsent > 0);
    assert(near(unlinked->spoolman_unsent, 0));
    puts("PASS: spools_charge only queues Spoolman usage for a spool that is linked");
}

/* ---- HTTP config endpoint ------------------------------------------------------------------- */

static char response[4096];
static int call_handler(void (*handler)(int, const char *, size_t), const char *body) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    handler(pair[0], body, strlen(body));
    close(pair[0]);
    size_t used = 0; ssize_t n;
    while ((n = recv(pair[1], response + used, sizeof(response) - 1 - used, 0)) > 0) used += (size_t)n;
    response[used] = 0; close(pair[1]);
    return atoi(strchr(response, ' ') + 1);
}
static int call_config(const char *body) { return call_handler(spoolman_config_response, body); }

static void test_config_endpoint(void) {
    static char path[64];
    snprintf(path, sizeof(path), "spoolman-test-cfg-%ld.json", (long)getpid());
    spools_path = path;
    unlink(path);
    memset(&spools, 0, sizeof(spools));
    spools.job.last_tray = -1;
    spools_available = 1;

    assert(call_config("url=http://192.0.2.9:7912\nbasic_auth=me:secret") == 200);
    assert(!strcmp(spools.spoolman_url, "http://192.0.2.9:7912"));
    assert(!strcmp(spools.spoolman_basic_auth, "me:secret"));

    assert(call_config("url=https://192.0.2.9") == 400);     /* no TLS stack */
    assert(call_config("url=not-a-url") == 400);
    assert(call_config("url=") == 200 && !spools.spoolman_url[0]);  /* turns sync off */

    spools_available = 0;
    assert(call_config("url=http://192.0.2.9:7912") == 503);
    unlink(path);
    puts("PASS: the Spoolman config endpoint validates the URL and can turn sync off");
}

/* ---- a stateful fake Spoolman on loopback -------------------------------------------------- */

/* Everything here is touched by both the test thread and the server thread, so every field is atomic. */
typedef struct {
    _Atomic int stop, bound, port;
    _Atomic double used, remaining;  /* the spool's state on the "server" */
    _Atomic int use_mode;            /* 0 normal, 1 apply then drop the reply, 2 drop without applying, 3 apply + 500 */
    _Atomic int get_mode;            /* 0 normal, 1 drop, 2 chunked, 3 fragmented, 4 truncated */
    _Atomic int gets_down;           /* every GET is dropped while set */
    _Atomic int auto_gets_down;      /* set gets_down once a /use has been seen */
    _Atomic int get_delay_ms;
    _Atomic int use_count, get_count, list_count;
    _Atomic double last_use;
    _Atomic(const char *) list_body;
} fake_spoolman;

static fake_spoolman fake;
static pthread_t fake_thread;

static void fake_send(int fd, const char *data, size_t length, size_t piece) {
    for (size_t at = 0; at < length;) {
        size_t n = piece && length - at > piece ? piece : length - at;
        ssize_t sent = send(fd, data + at, n, MSG_NOSIGNAL);
        if (sent <= 0) return;
        at += (size_t)sent;
        if (piece) sleep_ms(2);
    }
}

static void reply_body(int fd, const char *body, int mode) {
    char head[256];
    size_t length = strlen(body);
    if (mode == 2) {
        size_t cut = length / 2;
        int h = snprintf(head, sizeof(head), "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n%zx\r\n", cut);
        fake_send(fd, head, (size_t)h, 0);
        fake_send(fd, body, cut, 0);
        h = snprintf(head, sizeof(head), "\r\n%zx\r\n", length - cut);
        fake_send(fd, head, (size_t)h, 0);
        fake_send(fd, body + cut, length - cut, 0);
        fake_send(fd, "\r\n0\r\n\r\n", 7, 0);
        return;
    }
    int h = snprintf(head, sizeof(head), "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
                     mode == 4 ? length + 40 : length);
    fake_send(fd, head, (size_t)h, 0);
    fake_send(fd, body, length, mode == 3 ? 7 : 0);
}

static void *fake_server(void *unused) {
    (void)unused;
    int server = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1; setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(!bind(server, (struct sockaddr *)&a, sizeof(a)));
    socklen_t alen = sizeof(a); assert(!getsockname(server, (struct sockaddr *)&a, &alen));
    fake.port = ntohs(a.sin_port);
    assert(!listen(server, 8));
    fake.bound = 1;
    while (!fake.stop) {
        struct pollfd pfd = {server, POLLIN, 0};
        if (poll(&pfd, 1, 20) <= 0) continue;
        int fd = accept(server, NULL, NULL);
        if (fd < 0) continue;
        char req[4096] = {0}; size_t used = 0; ssize_t n;
        struct timeval tv = {5, 0}; setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        while ((n = recv(fd, req + used, sizeof(req) - 1 - used, 0)) > 0) {
            used += (size_t)n;
            char *split = strstr(req, "\r\n\r\n");
            if (!split) continue;
            const char *cl = strstr(req, "Content-Length:");
            long want = cl ? atol(cl + 16) : 0;
            if (used >= (size_t)(split - req) + 4 + (size_t)want) break;
        }
        char body[2048];
        int is_use = strstr(req, "PUT") && strstr(req, "/use");
        int is_list = strstr(req, "GET /api/v1/spool ") != NULL;
        snprintf(body, sizeof(body),
            "{\"id\":7,\"remaining_weight\":%.3f,\"used_weight\":%.3f,\"filament\":{\"material\":\"PETG\","
            "\"name\":\"Jet Black\",\"color_hex\":\"1a1a1a\"}}", fake.remaining, fake.used);
        if (is_use) {
            fake.use_count++;
            const char *w = strstr(req, "use_weight");
            double grams = w ? atof(strchr(w, ':') + 1) : 0;
            fake.last_use = grams;
            if (fake.use_mode != 2) { fake.used = fake.used + grams; fake.remaining = fake.remaining - grams; }
            snprintf(body, sizeof(body), "{\"id\":7,\"remaining_weight\":%.3f,\"used_weight\":%.3f}", fake.remaining, fake.used);
            if (fake.auto_gets_down) fake.gets_down = 1;
            if (fake.use_mode == 1 || fake.use_mode == 2) { close(fd); continue; }
            if (fake.use_mode == 3) fake_send(fd, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", 77, 0);
            else reply_body(fd, body, 0);
        } else {
            if (is_list) fake.list_count++; else fake.get_count++;
            if (fake.get_delay_ms) sleep_ms(fake.get_delay_ms);
            if (fake.gets_down || fake.get_mode == 1) { close(fd); continue; }
            reply_body(fd, is_list ? fake.list_body : body, fake.get_mode);
        }
        close(fd);
    }
    close(server);
    return NULL;
}

static void fake_start(double used, double remaining) {
    memset((void *)&fake, 0, sizeof(fake));
    fake.used = used; fake.remaining = remaining;
    fake.list_body = "[]";
}
static void fake_launch(void) {
    assert(!pthread_create(&fake_thread, NULL, fake_server, NULL));
    for (int i = 0; i < 200 && !fake.bound; ++i) sleep_ms(5);
    assert(fake.bound);
}
static void fake_finish(void) { fake.stop = 1; pthread_join(fake_thread, NULL); }

static long long virtual_now;
static int worker_running(void) {
    pthread_mutex_lock(&spoolman_outbox_mutex);
    int running = spoolman_worker_running;
    pthread_mutex_unlock(&spoolman_outbox_mutex);
    return running;
}
static void wait_worker(void) {
    for (int i = 0; i < 4000 && worker_running(); ++i) sleep_ms(5);
    assert(!worker_running());
}
/* One full sync cycle: start a worker when due, wait for it, then drain its results. */
static void cycle(void) {
    virtual_now += SPOOLMAN_TICK_MS;
    spoolman_tick(virtual_now);
    wait_worker();
    spoolman_tick(virtual_now + 1);
}

static spool_entry *setup_linked(double unsent, double remaining) {
    static char path[64];
    snprintf(path, sizeof(path), "spoolman-test-sync-%ld.json", (long)getpid());
    spools_path = path; unlink(path);
    memset(&spools, 0, sizeof(spools));
    spools.job.last_tray = -1;
    spools_available = 1;
    spoolman_epoch++;
    spoolman_last_tick_ms = 0; virtual_now = 0;
    snprintf(spools.spoolman_url, sizeof(spools.spoolman_url), "http://127.0.0.1:%d", fake.port);
    spool_entry *sp = &spools.spools[spools.count++];
    snprintf(sp->id, sizeof(sp->id), "3333333333333333");
    snprintf(sp->material, sizeof(sp->material), "PLA");
    snprintf(sp->color, sizeof(sp->color), "#FFFFFF");
    snprintf(sp->name, sizeof(sp->name), "Mine");
    sp->diameter = 1.75; sp->density = 1.24; sp->remaining = remaining; sp->net = 1000;
    snprintf(sp->spoolman_id, sizeof(sp->spoolman_id), "7");
    sp->spoolman_unsent = unsent;
    sp->spoolman_base = -1;
    return sp;
}

static void teardown_file(void) { unlink(spools_path); }

static void test_end_to_end_push_and_pull(void) {
    fake_start(100, 600); fake_launch();
    spool_entry *sp = setup_linked(12.5, 500);
    cycle();
    assert(fake.use_count == 1 && fake.get_count >= 2 && fake.list_count >= 1);
    assert(near(fake.last_use, 12.5) && near(fake.used, 112.5));
    assert(near(sp->spoolman_unsent, 0) && near(sp->spoolman_inflight, 0));
    assert(!strcmp(sp->material, "PETG") && !strcmp(sp->name, "Jet Black") && !strcmp(sp->color, "#1A1A1A"));
    assert(near(sp->remaining, 587.5));
    fake_finish(); teardown_file();
    puts("PASS: a tick pushes unsent usage and pulls material/colour/name/remaining from Spoolman");
}

static void test_no_connection_keeps_usage_unsent(void) {
    fake_start(100, 600); fake_launch();
    spool_entry *sp = setup_linked(12.5, 500);
    fake_finish();                        /* nothing listens any more: connection refused */
    cycle();
    assert(near(sp->spoolman_unsent, 12.5) && near(sp->spoolman_inflight, 0));
    assert(near(sp->remaining, 500));
    teardown_file();
    puts("PASS: usage that cannot reach Spoolman stays queued and is not lost");
}

static void test_dropped_reply_is_never_resent_blindly(void) {
    /* The server applies the delta and the connection dies before CC2 sees the reply. */
    fake_start(100, 600); fake.use_mode = 1; fake_launch();
    spool_entry *sp = setup_linked(12.5, 500);
    cycle();
    assert(fake.use_count == 1 && near(fake.used, 112.5));
    assert(near(sp->spoolman_unsent, 0) && near(sp->spoolman_inflight, 0));   /* read back: used grew by 12.5 */
    cycle(); cycle();
    assert(fake.use_count == 1 && near(fake.used, 112.5));                     /* never charged twice */
    fake_finish(); teardown_file();
    puts("PASS: a /use whose reply was lost is confirmed from used_weight, not sent again");
}

static void test_unknown_outcome_stays_pending_until_settled(void) {
    /* Applied, reply lost, and Spoolman unreachable afterwards: the grams stay in flight. */
    fake_start(100, 600); fake.use_mode = 1; fake.auto_gets_down = 1; fake_launch();
    spool_entry *sp = setup_linked(12.5, 500);
    cycle();
    assert(fake.use_count == 1);
    assert(near(sp->spoolman_unsent, 0) && near(sp->spoolman_inflight, 12.5));
    for (int i = 0; i < 2; ++i) { cycle(); assert(fake.use_count == 1 && near(sp->spoolman_inflight, 12.5)); }
    fake.gets_down = 0; fake.auto_gets_down = 0; fake.use_mode = 0;
    cycle();                                                  /* Spoolman is back: used_weight shows it was applied */
    assert(fake.use_count == 1 && near(fake.used, 112.5));
    assert(near(sp->spoolman_inflight, 0) && near(sp->spoolman_unsent, 0));
    assert(near(sp->remaining, 587.5));
    fake_finish(); teardown_file();
    puts("PASS: an unknown outcome waits in flight and is settled from used_weight, never resent");
}

static void test_not_applied_is_requeued_then_sent_once(void) {
    fake_start(100, 600); fake.use_mode = 2; fake_launch();     /* the request is lost before it is applied */
    spool_entry *sp = setup_linked(12.5, 500);
    cycle();
    assert(fake.use_count == 1 && near(fake.used, 100) && near(sp->spoolman_inflight, 12.5));
    fake.use_mode = 0;
    cycle();                                                   /* used_weight never moved: put the grams back */
    assert(fake.use_count == 1 && near(sp->spoolman_unsent, 12.5) && near(sp->spoolman_inflight, 0));
    cycle();
    assert(fake.use_count == 2 && near(fake.used, 112.5) && near(sp->spoolman_unsent, 0));
    fake_finish(); teardown_file();
    puts("PASS: a request that was not applied is requeued and then delivered exactly once");
}

static void test_server_error_is_treated_as_unknown(void) {
    fake_start(100, 600); fake.use_mode = 3; fake.auto_gets_down = 1; fake_launch();
    spool_entry *sp = setup_linked(12.5, 500);
    cycle();
    assert(fake.use_count == 1 && near(sp->spoolman_unsent, 0));
    assert(near(sp->spoolman_inflight, 12.5) && near(sp->spoolman_base, 100));    
    fake_finish(); teardown_file();
    puts("PASS: a 5xx answer to /use is an unknown outcome, not a failure to retry");
}

static void test_lost_baseline_never_double_charges(void) {
    fake_start(100, 600); fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    sp->spoolman_inflight = 8; sp->spoolman_base = -1;       /* CC2 stopped in the middle of a request */
    cycle();
    assert(fake.use_count == 0 && near(sp->spoolman_inflight, 0) && near(sp->spoolman_unsent, 0));
    fake_finish(); teardown_file();
    puts("PASS: in-flight grams without a recorded baseline are dropped rather than risk charging twice");
}

static void test_local_usage_during_a_pull_is_kept(void) {
    fake_start(100, 600); fake.get_delay_ms = 250; fake_launch();
    spool_entry *sp = setup_linked(10, 500);
    spool_copy_id(spools.slots[0].spool, sp->id);
    virtual_now += SPOOLMAN_TICK_MS;
    spoolman_tick(virtual_now);                    /* worker starts, slowly */
    assert(worker_running());
    spools_charge(0, 1000);                        /* the print goes on meanwhile */
    double during = sp->spoolman_unsent;
    assert(during > 1);
    wait_worker();
    spoolman_tick(virtual_now + 1);
    assert(near(fake.used, 110) && near(sp->spoolman_unsent, during));
    assert(near(sp->remaining, 590 - during));     /* Spoolman's figure minus what it has not been told */
    fake_finish(); teardown_file();
    puts("PASS: usage charged while a pull is in flight is not undone by its reply");
}

static void test_pending_accounting_survives_restart(void) {
    fake_start(0, 0);
    spool_entry *sp = setup_linked(3.25, 500);
    sp->spoolman_inflight = 5.5; sp->spoolman_base = 42.5;
    assert(spools_save() == 0);
    spools_load();
    assert(spools_available && spools.count == 1);
    sp = &spools.spools[0];
    assert(near(sp->spoolman_unsent, 3.25) && near(sp->spoolman_inflight, 5.5) && near(sp->spoolman_base, 42.5));
    teardown_file();
    puts("PASS: unsent and in-flight grams, and the baseline, are saved and restored");
}

static void test_stale_results_are_discarded(void) {
    fake_start(100, 600); fake.get_delay_ms = 250; fake_launch();
    static char body[64];
    /* 1: the spool is relinked to another Spoolman spool while a request runs. */
    spool_entry *sp = setup_linked(10, 500);
    virtual_now += SPOOLMAN_TICK_MS; spoolman_tick(virtual_now);
    snprintf(body, sizeof(body), "id=%s\nspoolman_id=8", sp->id);
    assert(call_handler(spools_save_response, body) == 200);
    sp = &spools.spools[0];
    assert(!strcmp(sp->spoolman_id, "8") && near(sp->spoolman_unsent, 0) && near(sp->spoolman_inflight, 0));
    wait_worker(); spoolman_tick(virtual_now + 1);
    assert(!strcmp(sp->material, "PLA") && !strcmp(sp->name, "Mine") && near(sp->remaining, 500));
    /* 2: unlinked meanwhile. */
    sp = setup_linked(10, 500);
    virtual_now += SPOOLMAN_TICK_MS; spoolman_tick(virtual_now);
    snprintf(body, sizeof(body), "id=%s\nspoolman_id=", sp->id);
    assert(call_handler(spools_save_response, body) == 200);
    wait_worker(); spoolman_tick(virtual_now + 1);
    assert(!spools.spools[0].spoolman_id[0] && !strcmp(spools.spools[0].material, "PLA"));
    /* 3: the server settings change meanwhile. */
    sp = setup_linked(10, 500);
    virtual_now += SPOOLMAN_TICK_MS; spoolman_tick(virtual_now);
    assert(call_config("url=http://127.0.0.1:9") == 200);
    wait_worker(); spoolman_tick(virtual_now + 1);
    assert(!strcmp(spools.spools[0].material, "PLA") && near(spools.spools[0].spoolman_inflight, 0));
    fake_finish(); teardown_file();
    puts("PASS: replies for an old link or server never touch a spool that was relinked, unlinked or moved");
}

static void test_one_spoolman_spool_links_once(void) {
    fake_start(0, 0);
    setup_linked(0, 500);
    spool_entry *second = &spools.spools[spools.count++];
    snprintf(second->id, sizeof(second->id), "4444444444444444");
    snprintf(second->material, sizeof(second->material), "PLA"); snprintf(second->color, sizeof(second->color), "#000000");
    second->diameter = 1.75; second->density = 1.24; second->net = 1000; second->remaining = 1000;
    assert(call_handler(spools_save_response, "id=4444444444444444\nspoolman_id=7") == 409);   /* already linked to the first */
    assert(call_handler(spools_save_response, "material=PLA\ncolor=#112233\nname=Copy\nspoolman_id=7") == 409);
    assert(call_handler(spools_save_response, "id=4444444444444444\nspoolman_id=9") == 200);
    teardown_file();
    puts("PASS: a Spoolman spool can be linked to one local spool only");
}

/* ---- the proxy login stays out of exported backups ----------------------------------------- */

static void test_backup_leaves_out_credentials(void) {
    fake_start(0, 0);
    spool_entry *sp = setup_linked(3.25, 500);
    sp->spoolman_inflight = 1.5; sp->spoolman_base = 7;
    snprintf(spools.spoolman_basic_auth, sizeof(spools.spoolman_basic_auth), "admin:distinct-secret-4711");
    size_t length = 0;
    char *local = spools_encode(&spools, &length);
    assert(local && strstr(local, "distinct-secret-4711"));              /* the local file keeps it, sync needs it */
    free(local);
    char *data[4]; size_t lengths[4];
    (void)bk_capture(data, lengths);                                      /* the real export path */
    assert(data[3] && !strstr(data[3], "distinct-secret-4711") && !strstr(data[3], "admin:"));
    assert(strstr(data[3], "\"spoolman_url\":\"http://127.0.0.1:"));
    assert(strstr(data[3], "\"spoolman_unsent\":0.0000") && strstr(data[3], "\"spoolman_inflight\":0.0000"));
    spool_store *restored = calloc(1, sizeof(*restored));
    assert(restored && bk_spools(data[3], data[3] + lengths[3], restored));
    assert(!restored->spoolman_url[0] && !restored->spoolman_basic_auth[0]);   /* a restore leaves sync off */
    assert(near(restored->spools[0].spoolman_unsent, 0) && near(restored->spools[0].spoolman_inflight, 0));
    assert(!strcmp(restored->spools[0].spoolman_id, "7"));
    free(restored);
    for (int i = 0; i < 4; ++i) free(data[i]);
    teardown_file();
    puts("PASS: an exported backup carries no proxy login or sync accounting, and a restore leaves sync off");
}

/* ---- HTTP framing --------------------------------------------------------------------------- */

typedef struct { const char *data; size_t length, piece; int cut_early; _Atomic int bound, port; } raw_server;

static void *raw_server_run(void *arg) {
    raw_server *r = arg;
    int server = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1; setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(!bind(server, (struct sockaddr *)&a, sizeof(a)));
    socklen_t alen = sizeof(a); assert(!getsockname(server, (struct sockaddr *)&a, &alen));
    r->port = ntohs(a.sin_port);
    assert(!listen(server, 1));
    r->bound = 1;
    int fd = accept(server, NULL, NULL);
    if (fd >= 0) {
        char req[1024]; (void)recv(fd, req, sizeof(req), 0);
        fake_send(fd, r->data, r->length, r->piece);
        close(fd);
    }
    close(server);
    return NULL;
}

static int raw_request(const char *data, size_t length, size_t piece, char *reply, size_t cap) {
    raw_server r = {data, length, piece, 0, 0, 0};
    pthread_t t; assert(!pthread_create(&t, NULL, raw_server_run, &r));
    for (int i = 0; i < 200 && !r.bound; ++i) sleep_ms(5);
    int status = spoolman_request("127.0.0.1", r.port, "", "GET", "/x", NULL, reply, cap);
    pthread_join(t, NULL);
    return status;
}

static char *big_list(int n) {
    char *text = malloc((size_t)n * 400 + 8);
    size_t at = 0;
    at += (size_t)sprintf(text + at, "[");
    for (int i = 0; i < n; ++i)
        at += (size_t)sprintf(text + at, "%s{\"id\":%d,\"remaining_weight\":%d.5,\"used_weight\":1,\"comment\":\"%0200d\","
            "\"filament\":{\"material\":\"PLA\",\"name\":\"Colour %d\",\"color_hex\":\"00ff%02x\",\"weight\":1000,\"density\":1.24}}",
            i ? "," : "", i + 1, 100 + i, 0, i, i % 256);
    sprintf(text + at, "]");
    return text;
}

static void test_http_framing(void) {
    static char reply[200000];
    char *list = big_list(40);
    size_t length = strlen(list);
    assert(length > SPOOLMAN_BODY_MAX);                                    /* larger than the old single read buffer */
    char head[128], *wire = malloc(length + 256);
    int h = snprintf(head, sizeof(head), "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n", length);
    memcpy(wire, head, (size_t)h); memcpy(wire + h, list, length);
    size_t wire_len = (size_t)h + length;

    assert(raw_request(wire, wire_len, 0, reply, sizeof(reply)) == 200 && !strcmp(reply, list));
    assert(raw_request(wire, wire_len, 97, reply, sizeof(reply)) == 200 && !strcmp(reply, list));   /* fragmented */
    spoolman_spool *entries = malloc(sizeof(*entries) * SPOOLMAN_CATALOG_MAX);
    assert(spoolman_parse_list(reply, entries, SPOOLMAN_CATALOG_MAX) == 40);
    assert(!strcmp(entries[39].spoolman_id, "40") && !strcmp(entries[3].color, "#00FF03") && near(entries[0].remaining, 100.5));
    free(entries);

    assert(raw_request(wire, wire_len - 10, 0, reply, sizeof(reply)) == SPOOLMAN_E_UNKNOWN);       /* cut short */
    assert(raw_request(wire, wire_len, 0, reply, 4096) == SPOOLMAN_E_TOOBIG);                       /* never truncated */
    assert(raw_request("garbage", 7, 0, reply, sizeof(reply)) == SPOOLMAN_E_UNKNOWN);
    assert(raw_request("", 0, 0, reply, sizeof(reply)) == SPOOLMAN_E_UNKNOWN);
    const char *chunked = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6\r\n world\r\n0\r\n\r\n";
    assert(raw_request(chunked, strlen(chunked), 3, reply, sizeof(reply)) == 200 && !strcmp(reply, "hello world"));
    const char *chunked_cut = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n6\r\n wor";
    assert(raw_request(chunked_cut, strlen(chunked_cut), 0, reply, sizeof(reply)) == SPOOLMAN_E_UNKNOWN);
    const char *closed = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n{\"a\":1}";
    assert(raw_request(closed, strlen(closed), 0, reply, sizeof(reply)) == 200 && !strcmp(reply, "{\"a\":1}"));
    const char *refused = "HTTP/1.1 404 Not Found\r\nContent-Length: 2\r\n\r\n{}";
    assert(raw_request(refused, strlen(refused), 0, reply, sizeof(reply)) == 404);
    assert(spoolman_request("127.0.0.1", 1, "", "GET", "/x", NULL, reply, sizeof(reply)) == SPOOLMAN_E_NOSEND);
    free(list); free(wire);
    puts("PASS: replies over 8 KiB, fragmented, chunked, cut short or oversized are read whole or rejected, never cut");
}

static void test_remote_values_are_validated(void) {
    spoolman_spool e;
    assert(spoolman_parse_one("{\"id\":5,\"remaining_weight\":1e9,\"used_weight\":-3,\"filament\":{\"material\":\"PLA\\u0001\","
                              "\"name\":\"ok\",\"color_hex\":\"zzzzzz\",\"density\":99,\"weight\":0}}", &e));
    assert(!e.have_remaining && !e.have_used && !e.material[0] && !strcmp(e.name, "ok") && !e.color[0] && e.density == 0 && e.net == 0);
    assert(!spoolman_parse_one("{\"id\":0}", &e) && !spoolman_parse_one("{\"id\":1.5}", &e) && !spoolman_parse_one("[1]", &e));
    assert(!spoolman_parse_one("{\"id\":\"7\"}", &e) && spoolman_parse_list("{}", &e, 1) == -1);
    assert(spoolman_parse_one("{\"id\":7,\"remaining_weight\":250.25,\"filament\":{\"material\":\"ABS\",\"name\":\"A \\\"B\\\"\"}}", &e));
    assert(e.have_remaining && near(e.remaining, 250.25) && !strcmp(e.material, "ABS") && !e.name[0]);   /* a name with quotes is not storable here: dropped, not mangled */
    puts("PASS: numbers, texts and colours from Spoolman go through the same bounded parsers as local data");
}

static void test_auto_import(void) {
    char *list = big_list(40);                       /* well over 8 KiB */
    fake_start(0, 0); fake.list_body = list; fake_launch();
    setup_linked(0, 500);
    spools.count = 0;                                /* nothing linked locally: the import pass creates them */
    cycle();
    assert(fake.list_count >= 1 && spools.count == 40);
    spool_entry *sp = spool_find_by_spoolman_id("40");
    assert(sp && !strcmp(sp->material, "PLA") && !strcmp(sp->name, "Colour 39") && near(sp->remaining, 139.5) &&
           near(sp->net, 1000) && near(sp->density, 1.24) && near(sp->diameter, 1.75));
    cycle();
    assert(spools.count == 40);                      /* known now: no duplicates */
    fake_finish(); teardown_file(); free(list);
    puts("PASS: connecting Spoolman auto-imports every spool it doesn't know locally yet, once each");
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 1024);
    spools_clock = clock_fake; spools_wall = wall_fake;
    test_parse_url();
    test_persistence_and_backward_compat();
    test_charge_accumulates_only_when_linked();
    test_config_endpoint();
    test_end_to_end_push_and_pull();
    test_no_connection_keeps_usage_unsent();
    test_dropped_reply_is_never_resent_blindly();
    test_unknown_outcome_stays_pending_until_settled();
    test_not_applied_is_requeued_then_sent_once();
    test_server_error_is_treated_as_unknown();
    test_lost_baseline_never_double_charges();
    test_local_usage_during_a_pull_is_kept();
    test_pending_accounting_survives_restart();
    test_stale_results_are_discarded();
    test_one_spoolman_spool_links_once();
    test_backup_leaves_out_credentials();
    test_http_framing();
    test_remote_values_are_validated();
    test_auto_import();
    return 0;
}
