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
static int call_config(const char *body) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    spoolman_config_response(pair[0], body, strlen(body));
    close(pair[0]);
    size_t used = 0; ssize_t n;
    while ((n = recv(pair[1], response + used, sizeof(response) - 1 - used, 0)) > 0) used += (size_t)n;
    response[used] = 0; close(pair[1]);
    return atoi(strchr(response, ' ') + 1);
}

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

/* ---- end-to-end against a tiny fake Spoolman on loopback ----------------------------------- */

static int fake_spoolman_port;
static volatile int fake_spoolman_use_seen, fake_spoolman_get_seen, fake_spoolman_list_seen;
/* The list endpoint's body for whichever test is running; set before starting the server. */
static const char *fake_spoolman_list_body = "[]";

typedef struct { int requests; volatile int bound; } fake_spoolman_args;

static void *fake_spoolman_server(void *arg) {
    fake_spoolman_args *a_ = arg;
    int requests = a_->requests;
    int server = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1; setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in a; memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_LOOPBACK); a.sin_port = 0;
    assert(!bind(server, (struct sockaddr *)&a, sizeof(a)));
    socklen_t alen = sizeof(a); assert(!getsockname(server, (struct sockaddr *)&a, &alen));
    fake_spoolman_port = ntohs(a.sin_port);
    assert(!listen(server, 4));
    a_->bound = 1;
    for (int handled = 0; handled < requests; ++handled) {
        int fd = accept(server, NULL, NULL);
        if (fd < 0) break;
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
        int is_use = strstr(req, "PUT") && strstr(req, "/use");
        int is_list = strstr(req, "GET /api/v1/spool ") != NULL;
        if (is_use) fake_spoolman_use_seen = 1;
        else if (is_list) fake_spoolman_list_seen = 1;
        else fake_spoolman_get_seen = 1;
        const char *body = is_use ? "{\"id\":7}" : is_list ? fake_spoolman_list_body :
            "{\"id\":7,\"remaining_weight\":612.5,\"filament\":{\"material\":\"PETG\",\"name\":\"Jet Black\",\"color_hex\":\"1a1a1a\"}}";
        char reply[65536];
        int len = snprintf(reply, sizeof(reply),
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
            strlen(body), body);
        send(fd, reply, (size_t)len, 0);
        close(fd);
    }
    close(server);
    return NULL;
}

static void test_end_to_end_push_and_pull(void) {
    fake_spoolman_args args = {3, 0};           /* use, get-by-id, and the auto-import list fetch */
    fake_spoolman_list_body = "[]";             /* nothing new to import in this test */
    fake_spoolman_use_seen = fake_spoolman_get_seen = fake_spoolman_list_seen = 0;
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, fake_spoolman_server, &args));
    for (int i = 0; i < 200 && !args.bound; ++i) sleep_ms(5);
    assert(args.bound);

    memset(&spools, 0, sizeof(spools));
    spools.job.last_tray = -1;
    spools_available = 1;
    spoolman_last_tick_ms = 0;
    snprintf(spools.spoolman_url, sizeof(spools.spoolman_url), "http://127.0.0.1:%d", fake_spoolman_port);
    spool_entry *sp = &spools.spools[spools.count++];
    snprintf(sp->id, sizeof(sp->id), "3333333333333333");
    snprintf(sp->material, sizeof(sp->material), "PLA");
    snprintf(sp->color, sizeof(sp->color), "#FFFFFF");
    sp->diameter = 1.75; sp->density = 1.24; sp->remaining = 500;
    snprintf(sp->spoolman_id, sizeof(sp->spoolman_id), "7");
    sp->spoolman_unsent = 12.5;

    spoolman_tick(SPOOLMAN_TICK_MS);               /* due: spoolman_last_tick_ms starts at 0 */
    for (int i = 0; i < 2000 && spoolman_worker_running; ++i) sleep_ms(5);
    assert(!spoolman_worker_running);
    spoolman_tick(SPOOLMAN_TICK_MS * 100);        /* drains the outbox into `spools` */

    assert(fake_spoolman_use_seen && fake_spoolman_get_seen && fake_spoolman_list_seen);
    assert(near(sp->spoolman_unsent, 0));
    assert(!strcmp(sp->material, "PETG"));
    assert(!strcmp(sp->name, "Jet Black"));
    assert(!strcmp(sp->color, "#1A1A1A"));
    assert(near(sp->remaining, 612.5));
    pthread_join(thread, NULL);
    puts("PASS: a tick pushes unsent usage and pulls material/colour/name/remaining from Spoolman");
}

static void test_auto_import(void) {
    /* Tick 1: no linked spools yet, so just the list fetch (1 request). Tick 2: the
     * spool imported by tick 1 is now linked, so its own GET runs too (2 requests). */
    fake_spoolman_args args = {3, 0};
    fake_spoolman_list_body =
        "[{\"id\":9,\"remaining_weight\":430.0,"
        "\"filament\":{\"material\":\"ABS\",\"name\":\"Fire Red\",\"color_hex\":\"cc2222\",\"weight\":1000,\"density\":1.04}}]";
    fake_spoolman_use_seen = fake_spoolman_get_seen = fake_spoolman_list_seen = 0;
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, fake_spoolman_server, &args));
    for (int i = 0; i < 200 && !args.bound; ++i) sleep_ms(5);
    assert(args.bound);

    memset(&spools, 0, sizeof(spools));
    spools.job.last_tray = -1;
    spools_available = 1;
    spoolman_last_tick_ms = 0;
    snprintf(spools.spoolman_url, sizeof(spools.spoolman_url), "http://127.0.0.1:%d", fake_spoolman_port);

    spoolman_tick(SPOOLMAN_TICK_MS);               /* no linked spools, but still due: runs the import pass */
    for (int i = 0; i < 2000 && spoolman_worker_running; ++i) sleep_ms(5);
    assert(!spoolman_worker_running);
    spoolman_tick(SPOOLMAN_TICK_MS * 100);         /* drains the outbox: creates the new local spool */

    assert(fake_spoolman_list_seen);
    assert(spools.count == 1);
    spool_entry *sp = &spools.spools[0];
    assert(!strcmp(sp->spoolman_id, "9"));
    assert(!strcmp(sp->material, "ABS") && !strcmp(sp->name, "Fire Red") && !strcmp(sp->color, "#CC2222"));
    assert(near(sp->remaining, 430.0) && near(sp->net, 1000.0) && near(sp->density, 1.04) && near(sp->diameter, 1.75));

    /* That same drain call, seeing enough virtual time has passed, already started a
     * second cycle (the import made spool 9 "known", so this one does its own GET
     * plus another list fetch -- the last 2 of this test's 3-request budget). Wait
     * for it and confirm the still-unchanged catalog did not create a duplicate. */
    for (int i = 0; i < 2000 && spoolman_worker_running; ++i) sleep_ms(5);
    assert(!spoolman_worker_running);
    spoolman_tick(SPOOLMAN_TICK_MS * 300);
    assert(spools.count == 1);

    pthread_join(thread, NULL);
    puts("PASS: connecting Spoolman auto-imports a spool it doesn't know locally yet, once each");
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 1024);
    spools_clock = clock_fake; spools_wall = wall_fake;
    test_parse_url();
    test_persistence_and_backward_compat();
    test_charge_accumulates_only_when_linked();
    test_config_endpoint();
    test_end_to_end_push_and_pull();
    test_auto_import();
    return 0;
}
