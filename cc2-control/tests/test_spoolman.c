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
    assert(!spools.spoolman_enabled); /* an explicitly paused configuration stays paused */
    size_t legacy_length = 0;
    char *legacy = spools_encode(&spools, &legacy_length);
    const char *flag = ",\"spoolman_enabled\":false";
    char *at = strstr(legacy, flag);
    assert(at);
    memmove(at, at + strlen(flag), strlen(at + strlen(flag)) + 1);
    FILE *legacy_file = fopen(path, "wb"); assert(legacy_file);
    fputs(legacy, legacy_file); fclose(legacy_file); free(legacy);
    spools_load();
    assert(spools_available && spools.spoolman_enabled && spools.spoolman_url[0]);
    puts("PASS: explicit pause persists, while older configured trials migrate without losing enabled sync");
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
    _Atomic int use_mode;            /* 0 normal, 1 apply then drop the reply, 2 drop without applying, 3 apply + 500, 4 500 without applying */
    _Atomic int get_mode;            /* 0 normal, 1 drop, 2 chunked, 3 fragmented, 4 truncated */
    _Atomic int gets_down;           /* every GET is dropped while set */
    _Atomic int auto_gets_down;      /* set gets_down once a /use has been seen */
    _Atomic int get_delay_ms;
    _Atomic int use_count, get_count, list_count;
    _Atomic double last_use;
    _Atomic(const char *) list_body;         /* the whole list as one reply, whatever limit/offset say */
    _Atomic(const char *const *) list_items; /* a list the server pages: each element is one spool's JSON */
    _Atomic int list_n;
    _Atomic int ignore_paging;               /* a server that answers with the whole list for every request */
    _Atomic int ignore_limit;                /* a server that honours offset but not limit: the whole rest of the list */
    _Atomic int no_total;                    /* ... and leaves the X-Total-Count header out */
    _Atomic int answer_all;                  /* every request is answered with this HTTP status and no body */
    _Atomic int missing_id;                  /* GET/PUT of this Spoolman id answers 404 */
    _Atomic int no_remaining, archived;      /* the single spool is sent without remaining_weight / as archived */
    _Atomic int saw_sort, saw_allow_archived, max_limit;
    _Atomic int drip_ms;                     /* get_mode 5: one byte of the reply per this many ms */
    char auth_seen[256];                     /* the last Authorization header (read after fake_finish only) */
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

/* One page of the fake's list, as Spoolman answers: the slice limit/offset asks for, the full count in X-Total-Count. */
static void reply_list(int fd, const char *req) {
    const char *const *items = fake.list_items;
    int n = fake.list_n, limit = n, offset = 0;
    const char *q = strstr(req, "limit="); if (q) limit = atoi(q + 6);
    q = strstr(req, "offset="); if (q) offset = atoi(q + 7);
    if (fake.ignore_paging) { limit = n; offset = 0; }
    if (fake.ignore_limit) limit = n;
    if (offset > n) offset = n;
    int end = offset + limit > n ? n : offset + limit;
    size_t cap = 16;
    for (int i = offset; i < end; ++i) cap += strlen(items[i]) + 1;
    char *body = malloc(cap), *head = malloc(256);
    size_t at = 0;
    body[at++] = '[';
    for (int i = offset; i < end; ++i) { if (i > offset) body[at++] = ','; size_t k = strlen(items[i]); memcpy(body + at, items[i], k); at += k; }
    body[at++] = ']'; body[at] = 0;
    int h;
    if (fake.no_total) h = snprintf(head, 256, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", at);
    else h = snprintf(head, 256, "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nx-total-count: %d\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", n, at);
    fake_send(fd, head, (size_t)h, 0);
    fake_send(fd, body, at, 0);
    free(body); free(head);
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
        int is_list = strstr(req, "GET /api/v1/spool ") != NULL || strstr(req, "GET /api/v1/spool?") != NULL;
        const char *idp = strstr(req, "/api/v1/spool/");
        int id = idp ? atoi(idp + 14) : 7;
        const char *auth = strstr(req, "Authorization:");
        if (auth) { size_t k = strcspn(auth, "\r"); if (k >= sizeof(fake.auth_seen)) k = sizeof(fake.auth_seen) - 1; memcpy(fake.auth_seen, auth, k); fake.auth_seen[k] = 0; }
        if (fake.answer_all) {
            char head[128];
            int h = snprintf(head, sizeof(head), "HTTP/1.1 %d X\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", (int)fake.answer_all);
            fake_send(fd, head, (size_t)h, 0);
            if (is_list) fake.list_count++; else if (!is_use) fake.get_count++;
            close(fd); continue;
        }
        if (!is_list && fake.missing_id && id == fake.missing_id) {
            const char *nf = "HTTP/1.1 404 Not Found\r\nContent-Length: 2\r\nConnection: close\r\n\r\n{}";
            fake_send(fd, nf, strlen(nf), 0);
            if (!is_use) fake.get_count++;
            close(fd); continue;
        }
        if (fake.no_remaining)
            snprintf(body, sizeof(body),
                "{\"id\":%d,\"used_weight\":%.3f,\"archived\":%s,\"filament\":{\"material\":\"PETG\","
                "\"name\":\"Jet Black\",\"color_hex\":\"1a1a1a\"}}", id, (double)fake.used, fake.archived ? "true" : "false");
        else
            snprintf(body, sizeof(body),
                "{\"id\":%d,\"remaining_weight\":%.3f,\"used_weight\":%.3f,\"archived\":%s,\"filament\":{\"material\":\"PETG\","
                "\"name\":\"Jet Black\",\"color_hex\":\"1a1a1a\"}}", id, (double)fake.remaining, (double)fake.used,
                fake.archived ? "true" : "false");
        if (is_use) {
            fake.use_count++;
            const char *w = strstr(req, "use_weight");
            double grams = w ? atof(strchr(w, ':') + 1) : 0;
            fake.last_use = grams;
            if (fake.use_mode != 2 && fake.use_mode != 4) { fake.used = fake.used + grams; fake.remaining = fake.remaining - grams; }
            snprintf(body, sizeof(body), "{\"id\":%d,\"remaining_weight\":%.3f,\"used_weight\":%.3f}", id, fake.remaining, fake.used);
            if (fake.auto_gets_down) fake.gets_down = 1;
            if (fake.use_mode == 1 || fake.use_mode == 2) { close(fd); continue; }
            if (fake.use_mode == 3 || fake.use_mode == 4) fake_send(fd, "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", 77, 0);
            else reply_body(fd, body, 0);
        } else {
            if (is_list) {
                fake.list_count++;
                if (strstr(req, "sort=id:asc")) fake.saw_sort = 1;
                if (strstr(req, "allow_archived")) fake.saw_allow_archived = 1;
                const char *lim = strstr(req, "limit=");
                if (lim && atoi(lim + 6) > fake.max_limit) fake.max_limit = atoi(lim + 6);
            } else fake.get_count++;
            if (fake.get_delay_ms) sleep_ms(fake.get_delay_ms);
            if (fake.gets_down || fake.get_mode == 1) { close(fd); continue; }
            if (!is_list && fake.get_mode == 5) {                      /* slow drip: a byte now and then, never done in time */
                char head[128];
                int h = snprintf(head, sizeof(head), "HTTP/1.1 200 OK\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", strlen(body));
                fake_send(fd, head, (size_t)h, 0);
                for (size_t k = 0; k < strlen(body) && !fake.stop; ++k) { if (send(fd, body + k, 1, MSG_NOSIGNAL) <= 0) break; sleep_ms(fake.drip_ms); }
            } else if (is_list && fake.list_items) reply_list(fd, req);
            else reply_body(fd, is_list ? fake.list_body : body, fake.get_mode);
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
    spools.spoolman_enabled = 1;
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

static void test_integration_toggle(void) {
    fake_start(100, 600); fake_launch();
    spool_entry *sp = setup_linked(12.5, 500);
    snprintf(spools.spoolman_basic_auth, sizeof(spools.spoolman_basic_auth), "me:secret");
    unsigned epoch = spoolman_epoch;
    assert(call_config("enabled=0") == 200);
    assert(!spools.spoolman_enabled && spoolman_epoch == epoch);
    assert(!strcmp(sp->spoolman_id, "7") && near(sp->spoolman_unsent, 12.5));
    spools.enabled = 1;
    spool_copy_id(spools.slots[0].spool, sp->id);
    spools_charge(0, 100);
    double queued = sp->spoolman_unsent;
    assert(queued > 12.5 && sp->remaining < 500 && spools.enabled);
    assert(spools_save() == 0);
    cycle();
    assert(!worker_running() && fake.use_count == 0 && fake.get_count == 0 && fake.list_count == 0);
    spools_load();
    assert(spools_available && !spools.spoolman_enabled && spools.spoolman_url[0]);
    assert(!strcmp(spools.spoolman_basic_auth, "me:secret"));
    assert(!strcmp(spools.spools[0].spoolman_id, "7") && fabs(spools.spools[0].spoolman_unsent - queued) <= 0.0001);
    assert(call_config("enabled=invalid") == 400 && !spools.spoolman_enabled);
    assert(call_config("enabled=1") == 200 && spools.spoolman_enabled);
    cycle(); cycle();
    assert(fake.use_count == 1 && fabs(fake.last_use - queued) <= 0.001);
    assert(near(spools.spools[0].spoolman_unsent, 0) && near(spools.spools[0].spoolman_inflight, 0));
    fake_finish(); teardown_file();
    puts("PASS: disabling persists without requests or lost settings/links/usage; re-enabling sends queued usage once");
}

static void test_disable_during_baseline(void) {
    fake_start(100, 600); fake.get_delay_ms = 200; fake_launch();
    spool_entry *sp = setup_linked(12.5, 500);
    virtual_now += SPOOLMAN_TICK_MS;
    spoolman_tick(virtual_now);
    for (int i = 0; i < 200 && !fake.get_count; ++i) sleep_ms(5);
    assert(fake.get_count == 1);
    assert(call_config("enabled=0") == 200);
    wait_worker(); spoolman_tick(virtual_now + 1);
    assert(fake.use_count == 0 && fake.list_count == 0);
    assert(near(sp->remaining, 500) && near(sp->spoolman_unsent, 12.5) && near(sp->spoolman_inflight, 0));
    fake.get_delay_ms = 0;
    assert(call_config("enabled=1") == 200);
    cycle();
    assert(fake.use_count == 1 && near(fake.last_use, 12.5));
    fake_finish(); teardown_file();
    puts("PASS: disabling during a baseline read blocks new writes and keeps accounting for safe resumption");
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
    assert(spools.held_count == 1 && spools.held[0].reason == SPOOLMAN_HELD_UNSURE && near(spools.held[0].grams, 8));   /* shown, never sent again */
    fake_finish(); teardown_file();
    puts("PASS: in-flight grams without a recorded baseline are never sent again; they stay on the list as unsure");
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

/* All credential-shaped strings below are synthetic test fixtures for the loopback fake server,
 * already present in tharje's public #208 tests; they are not printer or user credentials. */

/* ---- the proxy login stays out of exported backups ----------------------------------------- */

static void test_backup_leaves_out_credentials(void) {
    fake_start(0, 0);
    spool_entry *sp = setup_linked(3.25, 500);
    sp->spoolman_inflight = 1.5; sp->spoolman_base = 7;
    char test_secret[64];
    snprintf(test_secret, sizeof(test_secret), "synthetic-%ld", (long)getpid());
    snprintf(spools.spoolman_basic_auth, sizeof(spools.spoolman_basic_auth), "test-user:%s", test_secret);
    size_t length = 0;
    char *local = spools_encode(&spools, &length);
    assert(local && strstr(local, test_secret));              /* the local file keeps it, sync needs it */
    free(local);
    char *data[4]; size_t lengths[4];
    (void)bk_capture(data, lengths);                                      /* the real export path */
    assert(data[3] && !strstr(data[3], test_secret) && !strstr(data[3], "test-user:"));
    assert(strstr(data[3], "\"spoolman_url\":\"http://127.0.0.1:"));
    assert(strstr(data[3], "\"spoolman_unsent\":0.0000") && strstr(data[3], "\"spoolman_inflight\":0.0000"));
    spool_store *restored = calloc(1, sizeof(*restored));
    assert(restored && bk_spools(data[3], data[3] + lengths[3], restored));
    assert(!restored->spoolman_enabled);
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
    assert(spoolman_parse_page(reply, entries, SPOOLMAN_CATALOG_MAX, NULL) == 40);
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
    assert(!spoolman_parse_one("{\"id\":\"7\"}", &e) && spoolman_parse_page("{}", &e, 1, NULL) == -1);
    assert(spoolman_parse_one("{\"id\":7,\"remaining_weight\":250.25,\"filament\":{\"material\":\"ABS\",\"name\":\"A \\\"B\\\"\"}}", &e));
    assert(e.have_remaining && near(e.remaining, 250.25) && !strcmp(e.material, "ABS") && !e.name[0]);   /* a name with quotes is not storable here: dropped, not mangled */
    puts("PASS: numbers, texts and colours from Spoolman go through the same bounded parsers as local data");
}

static void test_colour_with_alpha(void) {
    spoolman_spool e;
    /* Spoolman's color_hex is RRGGBB or RRGGBBAA (transparency last). The card has no transparency: keep RGB. */
    assert(spoolman_parse_one("{\"id\":1,\"remaining_weight\":10,\"filament\":{\"material\":\"PLA\",\"color_hex\":\"1a2b3c80\"}}", &e));
    assert(!strcmp(e.color, "#1A2B3C"));                       /* 8 characters: RGB taken, alpha dropped */
    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"color_hex\":\"1A2B3CFF\"}}", &e) && !strcmp(e.color, "#1A2B3C"));
    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"color_hex\":\"1a2b3c\"}}", &e) && !strcmp(e.color, "#1A2B3C"));   /* 6 as before */
    const char *bad[] = {"", "1a2b3", "1a2b3c8", "1a2b3c801", "#1a2b3c", "#1a2b3c80", "1a2b3czz", "zz2b3c80", "1a2b3c8g", "1a2b3c 80", "1a2b3z", "1a2b3c 8", "1a2b3c8 ", "\\u0031a2b3c80"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {                            /* other lengths and non-hex: rejected, no colour */
        char json[160];
        snprintf(json, sizeof(json), "{\"id\":1,\"filament\":{\"material\":\"PLA\",\"color_hex\":\"%s\"}}", bad[i]);
        assert(spoolman_parse_one(json, &e) && !e.color[0] && !strcmp(e.material, "PLA"));
    }
    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"color_hex\":null}}", &e) && !e.color[0]);
    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"color_hex\":12345678}}", &e) && !e.color[0]);
    puts("PASS: Spoolman colours with transparency (8 hex) keep their RGB; other lengths and junk are rejected");
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

static void test_auto_import_colour_with_alpha(void) {
    static char list[] = "[{\"id\":1,\"remaining_weight\":900.5,\"filament\":{\"material\":\"PETG\",\"name\":\"Glass\",\"color_hex\":\"336699cc\"}},"
                         "{\"id\":2,\"remaining_weight\":800,\"filament\":{\"material\":\"PLA\",\"name\":\"Plain\",\"color_hex\":\"ff8800\"}},"
                         "{\"id\":3,\"remaining_weight\":700,\"filament\":{\"material\":\"ABS\",\"name\":\"Odd\",\"color_hex\":\"33669\"}}]";
    fake_start(0, 0); fake.list_body = list; fake_launch();
    setup_linked(0, 500);
    spools.count = 0;
    cycle();
    spool_entry *a = spool_find_by_spoolman_id("1"), *b = spool_find_by_spoolman_id("2"), *c = spool_find_by_spoolman_id("3");
    assert(spools.count == 3 && a && b && c);
    assert(!strcmp(a->color, "#336699"));        /* 8 hex: the RGB is imported, not the grey fallback */
    assert(!strcmp(b->color, "#FF8800"));        /* 6 hex: as before */
    assert(!strcmp(c->color, "#808080"));        /* a length we do not know: still the grey fallback */
    fake_finish(); teardown_file();
    puts("PASS: an imported spool keeps the colour of an 8-digit Spoolman colour; unknown lengths still import as grey");
}

/* ---- the import in pages, with the real shapes of Spoolman's answers ---------------------------------- */

/* n spools as Spoolman lists them (ids first_id.., a comment of `pad` bytes to make them as big as real ones). */
static char **make_items(int n, int pad, int first_id) {
    char **items = calloc((size_t)n, sizeof(char *));
    for (int i = 0; i < n; ++i) {
        items[i] = malloc((size_t)pad + 512);
        int at = sprintf(items[i], "{\"id\":%d,\"remaining_weight\":%d.5,\"used_weight\":1,\"comment\":\"", first_id + i, 100 + i % 800);
        memset(items[i] + at, 'x', (size_t)pad); at += pad;
        sprintf(items[i] + at, "\",\"filament\":{\"material\":\"PLA\",\"name\":\"Colour %d\",\"color_hex\":\"00ff%02x\",\"weight\":1000,\"density\":1.24}}",
                first_id + i, (first_id + i) % 256);
    }
    return items;
}
static void free_items(char **items, int n) { for (int i = 0; i < n; ++i) free(items[i]); free(items); }

static char status_json[2048];
static void get_status(void) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    spoolman_status_response(pair[0]);
    close(pair[0]);
    size_t used = 0; ssize_t n;
    while ((n = recv(pair[1], status_json + used, sizeof(status_json) - 1 - used, 0)) > 0) used += (size_t)n;
    status_json[used] = 0; close(pair[1]);
    assert(strstr(status_json, " 200 "));
    char *body = strstr(status_json, "\r\n\r\n");
    assert(body); memmove(status_json, body + 4, strlen(body + 4) + 1);
}
static void status_has(const char *text) {
    if (!strstr(status_json, text)) { fprintf(stderr, "status lacks %s:\n%s\n", text, status_json); abort(); }
}

static void test_list_is_read_in_pages(void) {
    /* 100 spools of about 1.8 KB: 180 KB in all -- more than the old single 128 KiB reply could carry, so it used to fail whole. */
    int n = 100; char **items = make_items(n, 1500, 1);
    fake_start(0, 0); fake.list_items = (const char *const *)items; fake.list_n = n; fake_launch();
    setup_linked(0, 500);
    spools.count = 0;
    cycle();
    assert(spools.count == 100);
    assert(fake.list_count == 5 && fake.saw_sort && !fake.saw_allow_archived && fake.max_limit == SPOOLMAN_PAGE_SIZE);
    get_status();
    status_has("\"result\":\"ok\""); status_has("\"pages\":5,\"read\":100,\"new\":100,\"unreadable\":0");
    cycle();                                     /* nothing new: no duplicates, the same five pages */
    assert(spools.count == 100 && fake.list_count == 10);
    fake_finish(); teardown_file(); free_items(items, n);
    puts("PASS: the spool list is read in sorted pages (limit/offset, X-Total-Count), no archived asked for, every spool imported once");
}

static void test_page_too_big_is_halved(void) {
    int n = 30; char **items = make_items(n, 4500, 1);       /* 20 of them are 96 KB: over the 64 KiB page buffer */
    fake_start(0, 0); fake.list_items = (const char *const *)items; fake.list_n = n; fake_launch();
    setup_linked(0, 500);
    spools.count = 0;
    cycle();
    assert(spools.count == 30 && fake.list_count > 3);       /* smaller pages, same result */
    get_status(); status_has("\"too_large\":0");
    fake_finish(); teardown_file(); free_items(items, n);

    char **odd = make_items(5, 100, 1);                      /* one spool alone bigger than a page: left out and counted */
    free(odd[1]); odd[1] = malloc(70000 + 512);
    int at = sprintf(odd[1], "{\"id\":2,\"remaining_weight\":5,\"comment\":\""); memset(odd[1] + at, 'y', 70000); sprintf(odd[1] + at + 70000, "\"}");
    fake_start(0, 0); fake.list_items = (const char *const *)odd; fake.list_n = 5; fake_launch();
    setup_linked(0, 500);
    spools.count = 0;
    cycle();
    assert(spools.count == 4 && !spool_find_by_spoolman_id("2") && spool_find_by_spoolman_id("5"));
    get_status(); status_has("\"too_large\":1");
    fake_finish(); teardown_file(); free_items(odd, 5);
    puts("PASS: a page that does not fit is halved until it does; one spool that never fits is skipped and counted");
}

static void test_unreadable_spool_does_not_stop_the_import(void) {
    char **items = make_items(2, 50, 1);
    const char *mixed[] = {items[0], "5", "\"text\"", "null", "{\"id\":\"x\"}", "{\"id\":9,\"used_weight\":3,\"filament\":{\"material\":\"PLA\"}}",
                           items[1]};
    fake_start(0, 0); fake.list_items = mixed; fake.list_n = 7; fake_launch();
    setup_linked(0, 500);
    spools.count = 0;
    cycle();
    assert(spools.count == 2 && spool_find_by_spoolman_id("1") && spool_find_by_spoolman_id("2"));   /* the one after the junk too */
    get_status(); status_has("\"unreadable\":4"); status_has("\"no_weight\":1");
    fake_finish(); teardown_file(); free_items(items, 2);
    puts("PASS: elements that are not spools are skipped and counted; the spools after them are still imported");
}

static void test_the_stand_ins_of_an_import_are_counted(void) {
    /* What Spoolman gives and the import cannot use is replaced by PLA / grey: the status says how often. */
    const char *items[] = {
        "{\"id\":1,\"remaining_weight\":500.0,\"used_weight\":1,\"filament\":{\"material\":\"PLA\",\"name\":\"Fine\",\"color_hex\":\"ff0000\"}}",
        "{\"id\":2,\"remaining_weight\":500.0,\"used_weight\":1,\"filament\":{\"material\":\"PETG\",\"name\":\"No colour\"}}",
        "{\"id\":3,\"remaining_weight\":500.0,\"used_weight\":1,\"filament\":{\"material\":\"PETG\",\"name\":\"Short\",\"color_hex\":\"f00\"}}",
        "{\"id\":4,\"remaining_weight\":500.0,\"used_weight\":1,\"filament\":{\"material\":\"PETG\",\"name\":\"Hash\",\"color_hex\":\"#ff0000\"}}",
        "{\"id\":5,\"remaining_weight\":500.0,\"used_weight\":1,\"filament\":{\"material\":\"\",\"name\":\"No material\",\"color_hex\":\"00ff00\"}}",
        "{\"id\":6,\"remaining_weight\":500.0,\"used_weight\":1,\"filament\":{\"material\":\"ABCDEFGHIJKLMNOPQRSTUVWXYZABCDEFG\",\"name\":\"Long\",\"color_hex\":\"0000ff\"}}",
    };
    fake_start(0, 0); fake.list_items = items; fake.list_n = 6; fake_launch();
    setup_linked(0, 500);
    spools.count = 0;
    cycle();
    assert(spools.count == 6);
    spool_entry *no_colour = spool_find_by_spoolman_id("2"), *no_material = spool_find_by_spoolman_id("5"), *fine = spool_find_by_spoolman_id("1");
    assert(no_colour && !strcmp(no_colour->color, "#808080") && !strcmp(no_colour->material, "PETG"));
    assert(no_material && !strcmp(no_material->material, "PLA") && !strcmp(no_material->color, "#00FF00"));
    assert(fine && !strcmp(fine->color, "#FF0000"));
    get_status(); status_has("\"new\":6"); status_has("\"no_material\":2,\"no_colour\":3}");
    fake_finish(); teardown_file();
    puts("PASS: spools imported with the stand-in PLA or grey are counted in the status (no_material, no_colour)");
}

static void test_a_page_of_junk_does_not_end_the_import(void) {
    char **items = make_items(3, 50, 1);
    const char *mixed[24];
    for (int i = 0; i < 21; ++i) mixed[i] = i % 2 ? "7" : "{\"id\":\"x\"}";       /* the whole first page (20) and one more unreadable */
    for (int i = 0; i < 3; ++i) mixed[21 + i] = items[i];
    fake_start(0, 0); fake.list_items = mixed; fake.list_n = 24; fake_launch();
    setup_linked(0, 500);
    spools.count = 0;
    cycle();
    assert(spools.count == 3 && spool_find_by_spoolman_id("3"));              /* the good spools on the later page arrived */
    get_status(); status_has("\"unreadable\":21");
    fake_finish(); teardown_file(); free_items(items, 3);
    puts("PASS: a whole page of unreadable elements is skipped and counted; the import goes on with the next page");
}

static void test_log_lines_follow_the_orphan_ids(void) {
    spoolman_status_t a; memset(&a, 0, sizeof(a));
    a.epoch = 4242; a.time = 100; a.reason = SPOOLMAN_R_OK; a.orphans = 1; snprintf(a.orphan_ids[0], sizeof(a.orphan_ids[0]), "7");
    spoolman_status_commit(&a);
    int logged = spoolman_log_count;
    spoolman_status_commit(&a);
    assert(spoolman_log_count == logged);                                       /* nothing changed */
    snprintf(a.orphan_ids[0], sizeof(a.orphan_ids[0]), "8");                    /* one spool gone, another one gone: a change, same count */
    spoolman_status_commit(&a);
    assert(spoolman_log_count == logged + 1);
    puts("PASS: the orphan log line follows the ids, not only their number");
}

static void test_cycle_without_time_says_so(void) {
    fake_start(0, 0); fake.list_items = NULL; fake_launch();
    setup_linked(0, 500);
    spools.count = 0;
    spoolman_cycle_max_ms = 0;                                                  /* no time left even for the list */
    cycle();
    spoolman_cycle_max_ms = SPOOLMAN_CYCLE_MAX_MS;
    get_status(); status_has("\"result\":\"timeout\"");
    assert(fake.list_count == 0);
    fake_finish(); teardown_file();
    puts("PASS: a cycle that had no time for the list says so instead of reporting ok");
}

static void test_server_that_ignores_paging(void) {
    char **items = make_items(45, 50, 1);
    fake_start(0, 0); fake.list_items = (const char *const *)items; fake.list_n = 45; fake.ignore_paging = 1; fake.no_total = 1; fake_launch();
    setup_linked(0, 500);
    spools.count = 0;
    cycle();
    assert(spools.count == 45 && fake.list_count == 2);     /* the same list twice, then stop: no endless loop */
    fake_finish(); teardown_file(); free_items(items, 45);
    puts("PASS: a server that ignores limit/offset (and sends no total) is read once and the loop ends");
}

static void test_server_that_ignores_limit_but_honours_offset(void) {
    /* 120 spools, every reply carries everything from the offset on: only 64 are read from a reply, so the next
     * request must start after those 64, not after all 120 (which used to leave out spool 65 and on). */
    char **items = make_items(120, 50, 1);
    fake_start(0, 0); fake.list_items = (const char *const *)items; fake.list_n = 120; fake.ignore_limit = 1; fake_launch();
    setup_linked(0, 500);
    spools.count = 0;
    cycle();
    assert(spools.count == 120);
    get_status(); status_has("\"read\":120,\"new\":120");
    fake_finish(); teardown_file();
    fake_start(0, 0); fake.list_items = (const char *const *)items; fake.list_n = 120; fake.ignore_limit = 1; fake.no_total = 1; fake_launch();
    setup_linked(0, 500);
    spools.count = 0;
    cycle();
    assert(spools.count == 120);                                               /* the same without X-Total-Count */
    fake_finish(); teardown_file(); free_items(items, 120);
    puts("PASS: a server that ignores limit but honours offset is read to the end (more than 64 spools per reply)");
}

static void test_library_full(void) {
    char **items = make_items(5, 50, 1);
    fake_start(0, 0); fake.list_items = (const char *const *)items; fake.list_n = 5; fake_launch();
    setup_linked(0, 500);
    spools.count = SPOOLS_MAX - 2;                            /* room for two new spools */
    for (int i = 1; i < spools.count; ++i) { spools.spools[i] = spools.spools[0]; spools.spools[i].spoolman_id[0] = 0; snprintf(spools.spools[i].id, sizeof(spools.spools[i].id), "%016d", i); }
    spools.spools[0].spoolman_id[0] = 0;
    cycle();
    assert(spools.count == SPOOLS_MAX);
    get_status(); status_has("\"new\":2"); status_has("\"library_full\":3");
    fake_finish(); teardown_file(); free_items(items, 5);
    puts("PASS: only as many spools are handed over as the library has room for; the rest are counted, not silently dropped");
}

static void test_weights_come_from_the_spoolman_fields(void) {
    spoolman_spool e;
    /* remaining_weight is Spoolman's own: max(initial_weight - used, 0), else max(filament.weight - used, 0), else absent. */
    assert(spoolman_parse_one("{\"id\":1,\"initial_weight\":750,\"used_weight\":50,\"filament\":{\"weight\":1000}}", &e));
    assert(e.have_remaining && near(e.remaining, 700) && near(e.net, 1000));
    assert(spoolman_parse_one("{\"id\":1,\"used_weight\":250,\"filament\":{\"weight\":1000}}", &e) && e.have_remaining && near(e.remaining, 750));
    assert(spoolman_parse_one("{\"id\":1,\"initial_weight\":300,\"used_weight\":400,\"filament\":{\"weight\":1000}}", &e) && e.have_remaining && near(e.remaining, 0));
    assert(spoolman_parse_one("{\"id\":1,\"used_weight\":5,\"filament\":{\"material\":\"PLA\"}}", &e) && !e.have_remaining);   /* nothing to compute from */
    assert(spoolman_parse_one("{\"id\":1,\"remaining_weight\":400,\"used_weight\":100,\"initial_weight\":900,\"filament\":{}}", &e) && near(e.remaining, 400));   /* given: kept */
    /* The full spool's weight when the filament has none: the spool's own start weight, else what is left plus what was used. */
    assert(spoolman_parse_one("{\"id\":1,\"initial_weight\":750,\"used_weight\":50,\"filament\":{}}", &e) && near(e.net, 750));
    assert(spoolman_parse_one("{\"id\":1,\"remaining_weight\":400,\"used_weight\":100,\"filament\":{}}", &e) && near(e.net, 500));
    assert(spoolman_parse_one("{\"id\":1,\"remaining_weight\":400,\"filament\":{}}", &e) && near(e.net, 0));             /* cannot tell: the caller's default */
    /* The empty spool: the spool's own weight, else the filament's, else the maker's. */
    assert(spoolman_parse_one("{\"id\":1,\"spool_weight\":250,\"filament\":{\"spool_weight\":200,\"vendor\":{\"name\":\"V\",\"empty_spool_weight\":140}}}", &e) && near(e.tare, 250));
    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"spool_weight\":200,\"vendor\":{\"name\":\"V\",\"empty_spool_weight\":140}}}", &e) && near(e.tare, 200));
    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"vendor\":{\"name\":\"V\",\"empty_spool_weight\":140}}}", &e) && near(e.tare, 140));
    assert(spoolman_parse_one("{\"id\":1,\"spool_weight\":99999,\"filament\":{}}", &e) && near(e.tare, 0));
    puts("PASS: a missing remaining_weight is worked out the way Spoolman does it; the full-spool and empty-spool weights follow Spoolman's fields");
}

static void test_colours_makers_and_escapes(void) {
    spoolman_spool e;
    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"multi_color_hexes\":\"ff0000,00ff00,0000ff\",\"multi_color_direction\":\"coaxial\"}}", &e) && !strcmp(e.color, "#FF0000"));
    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"multi_color_hexes\":\"ff000080,00ff00\"}}", &e) && !strcmp(e.color, "#FF0000"));
    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"multi_color_hexes\":\"00ff00\"}}", &e) && !strcmp(e.color, "#00FF00"));
    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"color_hex\":\"112233\",\"multi_color_hexes\":\"ff0000,00ff00\"}}", &e) && !strcmp(e.color, "#112233"));   /* color_hex wins */
    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"multi_color_hexes\":\"zz0000,00ff00\"}}", &e) && !e.color[0]);        /* a first colour that is no colour: none */
    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"multi_color_hexes\":\",00ff00\"}}", &e) && !e.color[0]);

    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"vendor\":{\"id\":2,\"name\":\"Prusa Polymers\"}}}", &e) && !strcmp(e.brand, "Prusa Polymers"));
    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"vendor\":{\"id\":2,\"name\":\"A\\\"B\"}}}", &e) && !e.brand[0]);      /* not storable: left out */
    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"vendor\":{\"id\":2}}}", &e) && !e.brand[0]);
    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"vendor\":null}}", &e) && !e.brand[0]);
    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"vendor\":{\"name\":\"Gr\\u00fcn & Co\"}}}", &e) && !strcmp(e.brand, "Gr\xc3\xbcn & Co"));

    /* Names with JSON escapes read as the text they stand for (UTF-8), within the same limits as before. */
    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"name\":\"Gr\\u00fcn\"}}", &e) && !strcmp(e.name, "Gr\xc3\xbcn"));
    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"name\":\"\\ud83d\\ude00 hi\"}}", &e) && !strcmp(e.name, "\xf0\x9f\x98\x80 hi"));
    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"name\":\"a\\/b \\u00E9\"}}", &e) && !strcmp(e.name, "a/b \xc3\xa9"));
    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"name\":\"x\\ud83dy\"}}", &e) && !strcmp(e.name, "x\xef\xbf\xbdy"));   /* a lone surrogate */
    assert(spoolman_parse_one("{\"id\":1,\"filament\":{\"name\":\"Gr\xc3\xbcn\"}}", &e) && !strcmp(e.name, "Gr\xc3\xbcn"));    /* raw UTF-8, what Spoolman sends */
    const char *dropped[] = {"a\\u0000b", "a\\nb", "a\\tb", "a\\\\b", "a\\\"b", "a\\qb", "a\\u12", "a\\u12zz", "\\u0020x", "x\\u0020", "a\\u2028b", "a\\u0085b"};
    for (size_t i = 0; i < sizeof(dropped) / sizeof(dropped[0]); ++i) {
        char json[200];
        snprintf(json, sizeof(json), "{\"id\":1,\"filament\":{\"material\":\"PLA\",\"name\":\"%s\"}}", dropped[i]);
        assert(spoolman_parse_one(json, &e) && !e.name[0] && !strcmp(e.material, "PLA"));
    }
    char json[300], name[120];
    memset(name, 'n', 96); name[96] = 0;
    snprintf(json, sizeof(json), "{\"id\":1,\"filament\":{\"name\":\"%s\"}}", name);
    assert(spoolman_parse_one(json, &e) && strlen(e.name) == 96);
    name[96] = 'n'; name[97] = 0;
    snprintf(json, sizeof(json), "{\"id\":1,\"filament\":{\"name\":\"%s\"}}", name);
    assert(spoolman_parse_one(json, &e) && !e.name[0]);
    puts("PASS: multi-colour spools, makers and names with JSON escapes (UTF-8, surrogate pairs) are read; what is not storable is left out");
}

static void test_import_takes_what_spoolman_knows(void) {
    static char list[] = "[{\"id\":1,\"initial_weight\":800,\"used_weight\":200,\"spool_weight\":250,\"filament\":{\"material\":\"PLA\",\"name\":\"Gr\\u00fcn\","
                         "\"multi_color_hexes\":\"ff0000,00ff00\",\"vendor\":{\"id\":3,\"name\":\"Polymaker\"}}},"
                         "{\"id\":2,\"remaining_weight\":300,\"used_weight\":100,\"filament\":{\"material\":\"PETG\"}}]";
    fake_start(0, 0); fake.list_body = list; fake_launch();
    setup_linked(0, 500);
    spools.count = 0;
    cycle();
    spool_entry *a = spool_find_by_spoolman_id("1"), *b = spool_find_by_spoolman_id("2");
    assert(spools.count == 2 && a && b);
    assert(near(a->remaining, 600) && near(a->net, 800) && near(a->tare, 250));           /* no remaining_weight in the answer: worked out */
    assert(!strcmp(a->brand, "Polymaker") && !strcmp(a->color, "#FF0000") && !strcmp(a->name, "Gr\xc3\xbcn"));
    assert(near(b->net, 400) && near(b->tare, 0) && !b->brand[0]);                        /* start weight = left + used */
    fake_finish(); teardown_file();
    puts("PASS: an imported spool gets its maker, first colour, weights and tare from what Spoolman sends");
}

static void test_linked_spool_without_weight_still_syncs(void) {
    fake_start(100, 600); fake.no_remaining = 1; fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    cycle();
    assert(!strcmp(sp->material, "PETG") && !strcmp(sp->name, "Jet Black") && !strcmp(sp->color, "#1A1A1A"));   /* the fields that are there */
    assert(near(sp->remaining, 500));                                                                           /* the weight is left alone */
    get_status(); status_has("\"linked_no_weight\":1");
    fake_finish(); teardown_file();
    puts("PASS: a linked spool Spoolman has no weight for still gets its name, material and colour; its weight stays");
}

static void test_linked_spool_is_refreshed_from_the_list(void) {
    static char list[] = "[{\"id\":7,\"remaining_weight\":420,\"used_weight\":10,\"filament\":{\"material\":\"ABS\",\"name\":\"Listed\",\"color_hex\":\"ff0000\"}},"
                         "{\"id\":9,\"remaining_weight\":50,\"filament\":{\"material\":\"PLA\"}}]";
    const char *items[] = {"{\"id\":7,\"remaining_weight\":420,\"used_weight\":10,\"filament\":{\"material\":\"ABS\",\"name\":\"Listed\",\"color_hex\":\"ff0000\"}}",
                           "{\"id\":9,\"remaining_weight\":50,\"filament\":{\"material\":\"PLA\"}}"};
    (void)list;
    fake_start(0, 0); fake.list_items = items; fake.list_n = 2; fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    cycle();
    assert(fake.get_count == 0 && fake.list_count == 1);                    /* one list request instead of one request per spool */
    assert(!strcmp(sp->material, "ABS") && !strcmp(sp->name, "Listed") && !strcmp(sp->color, "#FF0000") && near(sp->remaining, 420));
    assert(spools.count == 2 && spool_find_by_spoolman_id("9"));
    fake_finish(); teardown_file();
    puts("PASS: a linked spool that only needs a refresh is refreshed from the list, without a request of its own");
}

static void test_archived_linked_spool_is_counted_not_left_silent(void) {
    fake_start(100, 600); fake.archived = 1; fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    cycle();
    assert(fake.get_count == 1 && !fake.saw_allow_archived);               /* not in the list, so asked for by itself */
    assert(!strcmp(sp->material, "PETG") && near(sp->remaining, 600));      /* still synced, as before */
    get_status(); status_has("\"archived_linked\":1");
    fake_finish(); teardown_file();
    puts("PASS: a linked spool that Spoolman has archived since is still synced and is reported, not left silent");
}

static void test_hostnames_are_refused(void) {
    char host[SPOOLMAN_HOST_MAX], base[SPOOLMAN_PATH_MAX]; int port;
    assert(spoolman_parse_url("http://192.0.2.5:7912", host, sizeof(host), &port, base, sizeof(base)));
    assert(!spoolman_parse_url("http://spoolman:7912", host, sizeof(host), &port, base, sizeof(base)));
    assert(!spoolman_parse_url("http://spoolman.local", host, sizeof(host), &port, base, sizeof(base)));
    assert(!spoolman_parse_url("http://localhost:7912", host, sizeof(host), &port, base, sizeof(base)));
    assert(!spoolman_parse_url("http://192.0.2.256", host, sizeof(host), &port, base, sizeof(base)));
    assert(!spoolman_parse_url("http://[::1]:7912", host, sizeof(host), &port, base, sizeof(base)));
    static char path[64];
    snprintf(path, sizeof(path), "spoolman-test-host-%ld.json", (long)getpid());
    spools_path = path; unlink(path);
    memset(&spools, 0, sizeof(spools)); spools.job.last_tray = -1; spools_available = 1;
    assert(call_config("url=http://spoolman:7912") == 400 && strstr(response, "IPv4") && strstr(response, "host names"));
    assert(call_config("url=not-a-url") == 400);
    assert(call_config("url=http://192.0.2.9:7912") == 200 && !strcmp(spools.spoolman_url, "http://192.0.2.9:7912"));
    /* a name that got into the saved settings some other way is reported, not retried in silence */
    spools.spoolman_enabled = 1; spoolman_epoch++; spoolman_last_tick_ms = 0; virtual_now = 0;
    snprintf(spools.spoolman_url, sizeof(spools.spoolman_url), "http://spoolman:7912");
    spool_entry *sp = &spools.spools[spools.count++];
    snprintf(sp->id, sizeof(sp->id), "5555555555555555"); snprintf(sp->material, sizeof(sp->material), "PLA");
    snprintf(sp->color, sizeof(sp->color), "#FFFFFF"); sp->diameter = 1.75; sp->density = 1.24; sp->net = 1000; sp->remaining = 900;
    snprintf(sp->spoolman_id, sizeof(sp->spoolman_id), "7"); sp->spoolman_unsent = 3; sp->spoolman_base = -1;
    int logged = spoolman_log_count;
    cycle();
    get_status(); status_has("\"result\":\"bad_url\"");
    assert(spoolman_log_count == logged + 1);
    assert(near(sp->spoolman_unsent, 3) && near(sp->spoolman_inflight, 0));      /* the grams are not left in flight for good */
    unlink(path);
    puts("PASS: host names are refused with a clear text (IPv4 only), also when one is already saved");
}

static void test_status_and_log_only_on_change(void) {
    fake_start(100, 600); fake_launch();
    spool_entry *sp = setup_linked(5, 500);
    int logged = spoolman_log_count;
    fake.answer_all = 401;
    cycle();
    get_status();
    status_has("\"result\":\"unauthorized\""); status_has("\"http\":401"); status_has("\"unsent_spools\":1"); status_has("\"unsent_grams\":5.00");
    assert(near(sp->spoolman_unsent, 5) && near(sp->spoolman_inflight, 0));          /* a 401 keeps the grams queued, and says so */
    assert(spoolman_log_count == logged + 1);
    cycle(); cycle();
    assert(spoolman_log_count == logged + 1);                                          /* the same state: no more lines */
    fake.answer_all = 403;
    cycle(); get_status(); status_has("\"result\":\"forbidden\"");
    assert(spoolman_log_count == logged + 2);
    fake.answer_all = 0;
    cycle(); get_status();
    status_has("\"result\":\"ok\""); status_has("\"unsent_spools\":0"); status_has("\"last_sync\":");
    assert(spoolman_log_count == logged + 3 && near(sp->spoolman_unsent, 0));          /* recovered: sent, and one line for it */
    cycle(); assert(spoolman_log_count == logged + 3);
    fake_finish(); teardown_file();
    puts("PASS: GET /api/spoolman says what the last cycle did (401, 403, ok, queued grams); one log line per change, none per cycle");
}

static void test_server_down_and_timeout_are_told_apart(void) {
    fake_start(100, 600); fake_launch();
    setup_linked(0, 500);
    fake.gets_down = 1;                                       /* the connection is dropped before any reply */
    cycle(); get_status(); status_has("\"result\":\"bad_reply\"");
    int port = fake.port; fake_finish();
    fake.port = port;
    cycle(); get_status(); status_has("\"result\":\"unreachable\"");          /* nobody listens any more */
    teardown_file();
    puts("PASS: an unreadable reply and an unreachable server are different results");
}

static void test_deleted_spool_is_reported_and_its_grams_go_on_the_list(void) {
    fake_start(100, 600); fake.missing_id = 7; fake_launch();
    spool_entry *sp = setup_linked(5, 500);
    int logged = spoolman_log_count;
    cycle();
    /* One 404 proves nothing (a wrong base path, a proxy that hiccups): the grams wait where they were, nothing is on the list. */
    get_status();
    status_has("\"orphaned\":[\"7\"]"); status_has("\"orphaned_count\":1");
    assert(near(sp->spoolman_unsent, 5) && near(sp->spoolman_inflight, 0) && spools.held_count == 0 && fake.use_count == 0);
    assert(spoolman_log_count == logged + 1);
    cycle();                                                                   /* the second 404 in a row: now it is believed */
    get_status();
    status_has("\"orphaned\":[\"7\"]"); status_has("\"orphaned_count\":1");
    status_has("\"unsent_grams\":0.00"); status_has("\"held_entries\":1,\"held_grams\":5.00");
    assert(near(sp->spoolman_unsent, 0) && near(sp->spoolman_inflight, 0) && fake.use_count == 0);   /* nothing is sent to a spool that is gone */
    assert(spools.held_count == 1 && spools.held[0].reason == SPOOLMAN_HELD_NOT_FOUND && near(spools.held[0].grams, 5));
    assert(!strcmp(spools.held[0].spool, sp->id) && !strcmp(spools.held[0].name, "Mine") && !strcmp(spools.held[0].spoolman_id, "7"));
    assert(spoolman_log_count == logged + 1);
    sp->spoolman_unsent = 2.5;                                                 /* more usage while it is still missing: the same entry grows */
    cycle();
    assert(spools.held_count == 1 && near(spools.held[0].grams, 7.5) && spoolman_log_count == logged + 1);
    fake.missing_id = 0;                                                       /* it is back (re-created under the same id) */
    cycle(); get_status(); status_has("\"orphaned_count\":0");
    assert(fake.use_count == 0 && spools.held_count == 1 && near(spools.held[0].grams, 7.5));   /* what is on the list is never sent by itself */
    fake_finish(); teardown_file();
    puts("PASS: a linked spool Spoolman answers 404 for twice in a row is reported (one log line); its grams then go on the list instead of being retried for ever, and nothing is sent by itself");
}

static void test_a_single_404_is_not_believed(void) {
    fake_start(100, 600); fake.missing_id = 7; fake_launch();
    spool_entry *sp = setup_linked(5, 500);
    cycle();                                                                   /* 404 once: the grams stay unsent */
    assert(near(sp->spoolman_unsent, 5) && spools.held_count == 0 && fake.use_count == 0);
    fake.missing_id = 0;                                                       /* then the spool answers: the count starts again, the grams go out */
    cycle();
    assert(fake.use_count == 1 && near(fake.last_use, 5) && spools.held_count == 0 && near(sp->spoolman_unsent, 0) && near(sp->spoolman_inflight, 0));
    sp->spoolman_unsent = 3;
    fake.missing_id = 7;                                                       /* 404 once more, not "twice in a row" any more */
    cycle();
    assert(near(sp->spoolman_unsent, 3) && spools.held_count == 0);
    cycle();
    assert(spools.held_count == 1 && spools.held[0].reason == SPOOLMAN_HELD_NOT_FOUND && near(spools.held[0].grams, 3) && near(sp->spoolman_unsent, 0));
    fake_finish(); teardown_file();
    puts("PASS: one 404 leaves the grams unsent; a good answer in between starts the count again; only two in a row put them on the list");
}

static void test_a_404_counts_even_when_no_usage_was_waiting(void) {
    fake_start(100, 600); fake.missing_id = 7; fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    cycle();                                                                   /* only a refresh: nothing to hold, but the 404 is counted */
    assert(sp->spoolman_miss == 1 && spools.held_count == 0);
    sp->spoolman_unsent = 5;                                                   /* used afterwards: this is already the second cycle in a row */
    cycle();
    assert(spools.held_count == 1 && spools.held[0].reason == SPOOLMAN_HELD_NOT_FOUND && near(spools.held[0].grams, 5) && fake.use_count == 0);
    fake_finish(); teardown_file();
    puts("PASS: a 404 for a spool without waiting usage still counts towards the two cycles in a row");
}

static void test_relinking_starts_the_404_count_again(void) {
    fake_start(100, 600); fake.missing_id = 7; fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    cycle();                                                                   /* one 404 for number 7 */
    char body[64];
    snprintf(body, sizeof(body), "id=%s\nspoolman_id=8", sp->id);
    assert(call_handler(spools_save_response, body) == 200);                   /* linked to another number, then back to 7 */
    snprintf(body, sizeof(body), "id=%s\nspoolman_id=7", sp->id);
    assert(call_handler(spools_save_response, body) == 200);
    sp = &spools.spools[0];
    sp->spoolman_unsent = 3;
    cycle();                                                                   /* the first 404 of the new link: not believed */
    assert(near(sp->spoolman_unsent, 3) && spools.held_count == 0);
    fake_finish(); teardown_file();
    puts("PASS: a spool that is linked anew starts the count of 404 answers again");
}

static void test_grams_in_flight_wait_for_the_second_404(void) {
    fake_start(100, 600); fake.missing_id = 7; fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    sp->spoolman_inflight = 4; sp->spoolman_base = 100;                        /* an earlier /use whose outcome is not known */
    cycle();                                                                   /* the check fails with 404: still unknown, still in flight */
    assert(near(sp->spoolman_inflight, 4) && near(sp->spoolman_base, 100) && near(sp->spoolman_unsent, 0) && spools.held_count == 0);
    cycle();
    assert(near(sp->spoolman_inflight, 0) && spools.held_count == 1 && spools.held[0].reason == SPOOLMAN_HELD_NOT_FOUND && near(spools.held[0].grams, 4));
    assert(fake.use_count == 0);
    fake_finish(); teardown_file();
    puts("PASS: grams of an unsettled /use stay in flight after one 404 and go on the list after the second");
}

static void test_request_and_cycle_have_a_deadline(void) {
    /* A server that drips a byte now and then never trips the 5 s read timeout: the whole request has its own limit. */
    fake_start(100, 600); fake.get_mode = 5; fake.drip_ms = 150; fake_launch();
    char reply[SPOOLMAN_BODY_MAX];
    spoolman_request_max_ms = 600;
    long long t0 = monotonic_ms();
    int status = spoolman_request("127.0.0.1", fake.port, "", "GET", "/api/v1/spool/7", NULL, reply, sizeof(reply));
    long long took = monotonic_ms() - t0;
    spoolman_request_max_ms = SPOOLMAN_REQUEST_MAX_MS;
    assert(status == SPOOLMAN_E_TIMEOUT && took < 2500);
    fake_finish();

    /* A cycle that runs out of time gives the grams it has not reached back as not sent, and starts there next time. */
    fake_start(100, 600); fake.get_delay_ms = 400; fake_launch();
    spool_entry *a = setup_linked(5, 500);
    spool_entry *b = &spools.spools[spools.count++];
    *b = *a; snprintf(b->id, sizeof(b->id), "4444444444444444"); snprintf(b->spoolman_id, sizeof(b->spoolman_id), "8");
    spoolman_rotate = 0;
    spoolman_cycle_max_ms = 300;
    cycle();
    assert(fake.use_count == 1 && near(a->spoolman_unsent, 0) && near(b->spoolman_unsent, 5) && near(b->spoolman_inflight, 0));
    get_status(); status_has("\"result\":\"timeout\"");
    cycle();                                                                    /* the second one is first in line now */
    assert(fake.use_count == 2 && near(b->spoolman_unsent, 0));
    spoolman_cycle_max_ms = SPOOLMAN_CYCLE_MAX_MS;
    fake_finish(); teardown_file();
    puts("PASS: a slow server cannot hold a request or a cycle open: both have a total deadline, unreached grams stay queued, the next cycle starts there");
}

static char *b64(const char *in) {
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t n = strlen(in); char *out = calloc(1, (n + 2) / 3 * 4 + 1); size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = (unsigned)(unsigned char)in[i] << 16 | (i + 1 < n ? (unsigned)(unsigned char)in[i + 1] << 8 : 0) | (i + 2 < n ? (unsigned char)in[i + 2] : 0);
        out[o++] = t[(v >> 18) & 63]; out[o++] = t[(v >> 12) & 63]; out[o++] = i + 1 < n ? t[(v >> 6) & 63] : '='; out[o++] = i + 2 < n ? t[v & 63] : '=';
    }
    return out;
}

static void test_basic_auth_header(void) {
    char login[SPOOLMAN_AUTH_MAX + 1];
    const char *logins[] = {"me:secret", "a:b", "ab:c", NULL};
    memset(login, 'u', 63); login[63] = ':'; memset(login + 64, 'p', 63); login[127] = 0;
    for (int i = 0; i < 4; ++i) {
        const char *who = i < 3 ? logins[i] : login;
        fake_start(100, 600); fake_launch();
        setup_linked(0, 500);
        snprintf(spools.spoolman_basic_auth, sizeof(spools.spoolman_basic_auth), "%s", who);
        cycle();
        fake_finish();
        char *want = b64(who), expect[300];
        snprintf(expect, sizeof(expect), "Authorization: Basic %s", want);
        assert(!strcmp(fake.auth_seen, expect));
        if (i == 3) assert(strlen(want) == 172);
        free(want);
        teardown_file();
    }
    fake_start(100, 600); fake_launch();
    setup_linked(0, 500);                                                    /* no login: no header at all */
    cycle(); fake_finish();
    assert(!fake.auth_seen[0]); teardown_file();
    puts("PASS: the Basic login is sent as Authorization (\"me:secret\", short logins, the longest 127 characters); none without a login");
}

/* ---- the list of usage that was not sent ---------------------------------------------------- */

static spool_entry *add_linked_spool(const char *id, const char *spoolman_id, double unsent) {
    spool_entry *sp = &spools.spools[spools.count++];
    memset(sp, 0, sizeof(*sp));
    snprintf(sp->id, sizeof(sp->id), "%s", id);
    snprintf(sp->material, sizeof(sp->material), "PETG"); snprintf(sp->color, sizeof(sp->color), "#101010");
    snprintf(sp->name, sizeof(sp->name), "Second");
    sp->diameter = 1.75; sp->density = 1.24; sp->net = 1000; sp->remaining = 700;
    snprintf(sp->spoolman_id, sizeof(sp->spoolman_id), "%s", spoolman_id);
    sp->spoolman_unsent = unsent; sp->spoolman_base = -1;
    return sp;
}

static int held_index(unsigned id) {
    for (int i = 0; i < spools.held_count; ++i) if (spools.held[i].id == id) return i;
    return -1;
}

/* The page sends the generation of the server settings its choice was made under; call_held_send sends the current one. */
static int call_held_send_raw(const char *body) { return call_handler(spoolman_held_send_response, body); }
static int call_held_send(const char *body) {
    char text[1024];
    snprintf(text, sizeof(text), "gen=%u\n%s", spoolman_generation(), body);
    return call_held_send_raw(text);
}
static int call_held_discard(const char *body) { return call_handler(spoolman_held_discard_response, body); }
static int call_unlink_old(void) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    spoolman_unlink_old_response(pair[0]);
    close(pair[0]);
    size_t used = 0; ssize_t n;
    while ((n = recv(pair[1], response + used, sizeof(response) - 1 - used, 0)) > 0) used += (size_t)n;
    response[used] = 0; close(pair[1]);
    return atoi(strchr(response, ' ') + 1);
}
/* Waits for the worker of a send and lets the main thread take its results. */
static void finish_send(void) { wait_worker(); spoolman_tick(virtual_now + 1); }

static char *get_spools_text(void) {
    static mqtt_client m;
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    spools_get_response(pair[0], &m);
    close(pair[0]);
    char *text = calloc(1, 400000); assert(text);
    size_t used = 0; ssize_t n;
    while ((n = recv(pair[1], text + used, 399999 - used, 0)) > 0) used += (size_t)n;
    close(pair[1]);
    char *body = strstr(text, "\r\n\r\n"); assert(body && strstr(text, " 200 "));
    memmove(text, body + 4, strlen(body + 4) + 1);
    return text;
}

static void test_address_or_login_change_puts_pending_usage_on_the_list(void) {
    fake_start(100, 600); fake_launch();
    spool_entry *sp = setup_linked(10, 500);
    sp->spoolman_inflight = 4; sp->spoolman_base = 100;
    spool_entry *two = add_linked_spool("5555555555555555", "8", 0);
    spool_entry *plain = add_linked_spool("6666666666666666", "", 0);
    snprintf(plain->spoolman_id, sizeof(plain->spoolman_id), "%s", "");
    /* A new address: the pending amounts are no longer lost; they are on the list, for the old link. */
    assert(call_config("url=http://127.0.0.1:9") == 200);
    assert(spools.held_count == 2);
    int server = -1, unsure = -1;
    for (int i = 0; i < spools.held_count; ++i) { if (spools.held[i].reason == SPOOLMAN_HELD_SERVER) server = i; if (spools.held[i].reason == SPOOLMAN_HELD_UNSURE) unsure = i; }
    assert(server >= 0 && near(spools.held[server].grams, 10) && !strcmp(spools.held[server].spoolman_id, "7"));
    assert(unsure >= 0 && near(spools.held[unsure].grams, 4));
    assert(near(sp->spoolman_unsent, 0) && near(sp->spoolman_inflight, 0) && !strcmp(sp->spoolman_id, "7") && !strcmp(two->spoolman_id, "8"));
    assert(spoolman_stale_count() == 2 && sp->spoolman_stale && two->spoolman_stale && !plain->spoolman_stale);
    char *text = get_spools_text();
    assert(strstr(text, "\"spoolman_stale_links\":2,\"spoolman_held\":[{\"id\":1,\"spool\":\"3333333333333333\",\"name\":\"Mine\",\"spoolman_id\":\"7\",\"grams\":10.00,\"time\":1790000000,\"reason\":\"server_changed\",\"sending\":false}"));
    assert(strstr(text, "\"reason\":\"unsure\""));
    free(text);
    /* Only the login changes: the same server, so the spools are not "old", and the reason says login. */
    sp->spoolman_unsent = 3;
    assert(call_config("basic_auth=me:secret") == 200);
    assert(spools.held_count == 3 && spools.held[2].reason == SPOOLMAN_HELD_LOGIN && near(spools.held[2].grams, 3));
    assert(spoolman_stale_count() == 2);                                        /* not cleared or set by a login change */
    /* A second amount for the same spool and reason is added to its entry, not a second row. */
    sp->spoolman_unsent = 1.5;
    assert(call_config("basic_auth=other:secret") == 200);
    assert(spools.held_count == 3 && near(spools.held[2].grams, 4.5));
    /* Turning the sync off (empty address) is the user's own decision: nothing is put on the list. */
    sp->spoolman_unsent = 6;
    assert(call_config("url=") == 200);
    assert(spools.held_count == 3 && near(sp->spoolman_unsent, 0) && spoolman_stale_count() == 0);
    /* The list is full: the change is refused and nothing is changed or lost. */
    memset(spools.held, 0, sizeof(spools.held));
    for (int i = 0; i < SPOOLMAN_HELD_MAX; ++i) { spools.held[i].id = (unsigned)i + 1; spools.held[i].reason = SPOOLMAN_HELD_NOT_FOUND; spools.held[i].grams = 1; snprintf(spools.held[i].spool, sizeof(spools.held[i].spool), "%016d", i); }
    spools.held_count = SPOOLMAN_HELD_MAX;
    snprintf(spools.spoolman_url, sizeof(spools.spoolman_url), "http://127.0.0.1:%d", fake.port);
    char kept_url[SPOOLMAN_URL_MAX + 1]; snprintf(kept_url, sizeof(kept_url), "%s", spools.spoolman_url);
    sp->spoolman_unsent = 1;
    assert(call_config("url=http://127.0.0.1:10") == 409);
    assert(!strcmp(spools.spoolman_url, kept_url));
    assert(near(sp->spoolman_unsent, 1) && spools.held_count == SPOOLMAN_HELD_MAX);
    fake_finish(); teardown_file();
    puts("PASS: a new address or login puts pending and in-flight usage on the list (server / login / unsure) instead of deleting it; a full list refuses the change");
}

static void test_list_survives_restart_and_older_files_load(void) {
    fake_start(0, 0);
    spool_entry *sp = setup_linked(0, 500);
    assert(spoolman_hold(sp, 12.5, SPOOLMAN_HELD_LOGIN) && spoolman_hold(sp, 4, SPOOLMAN_HELD_NOT_FOUND));
    sp->spoolman_stale = 1;
    assert(spools_save() == 0);
    spools_load();
    assert(spools_available && spools.held_count == 2 && spoolman_stale_count() == 1 && spools.spools[0].spoolman_stale && spools.held_last_id == 2);
    assert(spools.held[0].id == 1 && spools.held[0].reason == SPOOLMAN_HELD_LOGIN && near(spools.held[0].grams, 12.5));
    assert(!strcmp(spools.held[0].name, "Mine") && !strcmp(spools.held[0].spoolman_id, "7") && spools.held[0].time == fake_now);
    sp = &spools.spools[0];
    /* An id is never used again, even after an entry went. */
    spools.held_count = 1;
    assert(spoolman_hold(sp, 2, SPOOLMAN_HELD_SERVER) && spools.held[1].id == 3);
    /* A request that was out when CC2 Control stopped is "unsure" afterwards, whatever it was. */
    spools.held[1].sending = 1;
    assert(spools_save() == 0);
    spools_load();
    assert(spools.held_count == 2 && !spools.held[1].sending && spools.held[1].reason == SPOOLMAN_HELD_UNSURE);
    /* An entry that cannot be read is left out; the spools and the other entries still load. */
    size_t length = 0; char *data = spools_encode(&spools, &length);
    char *at = strstr(data, "\"reason\":4");
    assert(at); at[9] = '9';
    FILE *f = fopen(spools_path, "wb"); assert(f); fputs(data, f); fclose(f); free(data);
    spools_load();
    assert(spools_available && spools.count == 1 && spools.held_count == 1 && spools.held[0].id == 1);
    /* Neither the list nor the flag goes into an exported backup, and the backup format is unchanged. */
    char *backup[4]; size_t lengths[4];
    (void)bk_capture(backup, lengths);
    assert(backup[3] && !strstr(backup[3], "spoolman_held") && !strstr(backup[3], "spoolman_stale"));
    for (int i = 0; i < 4; ++i) free(backup[i]);
    /* A file written before the list existed has none of its keys. */
    data = spools_encode(&spools, &length);
    char *cut = strstr(data, ",\"spoolman_held_last_id\"");
    char *list_end = strstr(cut, "],\"slots\"");
    assert(cut && list_end);
    memmove(cut, list_end + 1, strlen(list_end + 1) + 1);
    char *flag = strstr(data, ",\"spoolman_stale\":true");           /* the flag of the spool is not in such a file either */
    assert(flag);
    memmove(flag, flag + strlen(",\"spoolman_stale\":true"), strlen(flag + strlen(",\"spoolman_stale\":true")) + 1);
    f = fopen(spools_path, "wb"); assert(f); fputs(data, f); fclose(f); free(data);
    spools_load();
    assert(spools_available && spools.count == 1 && spools.held_count == 0 && spoolman_stale_count() == 0 && !strcmp(spools.spools[0].spoolman_id, "7"));
    teardown_file();
    puts("PASS: the list, the flag of the old connections and the entry numbers survive a restart; a request that was out becomes unsure; a damaged entry is left out; backups carry none of it; older files load");
}

static void test_send_selected_sends_only_the_chosen_entries(void) {
    fake_start(100, 600); fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    assert(spoolman_hold(sp, 12.5, SPOOLMAN_HELD_LOGIN) && spoolman_hold(sp, 29.1, SPOOLMAN_HELD_SERVER) && spoolman_hold(sp, 4, SPOOLMAN_HELD_NOT_FOUND));
    assert(call_held_send("1=7\n2=7") == 202);
    assert(spools.held[0].sending && spools.held[1].sending && !spools.held[2].sending);       /* written down before it goes out */
    finish_send();
    assert(fake.use_count == 2 && near(fake.used, 100 + 12.5 + 29.1));
    assert(spools.held_count == 1 && spools.held[0].id == 3 && !spools.held[0].sending && spools.held[0].reason == SPOOLMAN_HELD_NOT_FOUND);
    assert(near(sp->remaining, 500) && near(sp->spoolman_unsent, 0));                          /* this printer's spool is not touched */
    assert(!worker_running());
    fake_finish(); teardown_file();
    puts("PASS: Send selected sends exactly the chosen entries to the chosen Spoolman spool; they leave the list, the others stay, this printer's spool is untouched");
}

static void test_send_is_refused_without_a_chosen_spool_or_for_unsure(void) {
    fake_start(100, 600); fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    assert(spoolman_hold(sp, 12.5, SPOOLMAN_HELD_SERVER) && spoolman_hold(sp, 4, SPOOLMAN_HELD_UNSURE));
    assert(call_held_send("1=") == 400);                 /* no spool chosen */
    assert(call_held_send("1=abc") == 400);
    assert(call_held_send("9=7") == 404);                /* no such entry */
    assert(call_held_send("2=7") == 409);                /* unsure: it is never sent again */
    assert(call_held_send("1=7\n2=7") == 409);           /* one bad entry stops the whole request */
    assert(call_held_send("") == 400);
    assert(!spools.held[0].sending && !spools.held[1].sending && fake.use_count == 0 && !worker_running());
    spools.spoolman_enabled = 0;
    assert(call_held_send("1=7") == 409);
    spools.spoolman_enabled = 1;
    pthread_mutex_lock(&spoolman_outbox_mutex); spoolman_worker_running = 1; pthread_mutex_unlock(&spoolman_outbox_mutex);
    assert(call_held_send("1=7") == 409 && !spools.held[0].sending);                            /* the sync is busy */
    pthread_mutex_lock(&spoolman_outbox_mutex); spoolman_worker_running = 0; pthread_mutex_unlock(&spoolman_outbox_mutex);
    assert(fake.use_count == 0 && spools.held_count == 2);
    fake_finish(); teardown_file();
    puts("PASS: nothing is sent without a chosen spool, for an unsure entry, for an unknown entry, with the sync off or while it is busy");
}

static void test_failed_sends_keep_the_entry(void) {
    /* Spoolman unreachable / refusing: nothing was sent, the entry stays as it was. */
    fake_start(100, 600); fake.answer_all = 401; fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    assert(spoolman_hold(sp, 12.5, SPOOLMAN_HELD_SERVER));
    assert(call_held_send("1=7") == 202); finish_send();
    assert(spools.held_count == 1 && !spools.held[0].sending && spools.held[0].reason == SPOOLMAN_HELD_SERVER && near(spools.held[0].grams, 12.5));
    assert(fake.use_count == 0);
    /* The target is not in Spoolman: the entry says so. */
    fake.answer_all = 0; fake.missing_id = 9;
    assert(call_held_send("1=9") == 202); finish_send();
    assert(spools.held_count == 1 && spools.held[0].reason == SPOOLMAN_HELD_NOT_FOUND && fake.use_count == 0);
    /* The answer is lost but Spoolman applied it: read back, confirmed, sent once. */
    fake.missing_id = 0; fake.use_mode = 1;
    assert(call_held_send("1=7") == 202); finish_send();
    assert(spools.held_count == 0 && fake.use_count == 1 && near(fake.used, 112.5));
    /* The answer is lost and it was not applied: it cannot be told apart from "applied later", so it is never sent again. */
    assert(spoolman_hold(sp, 5, SPOOLMAN_HELD_LOGIN));
    fake.use_mode = 2;
    assert(call_held_send("2=7") == 202); finish_send();
    assert(spools.held_count == 1 && spools.held[0].reason == SPOOLMAN_HELD_UNSURE && !spools.held[0].sending);
    fake.use_mode = 0;
    assert(call_held_send("2=7") == 409 && fake.use_count == 2 && near(fake.used, 112.5));
    fake_finish(); teardown_file();
    puts("PASS: a send that fails leaves the entry; a missing target is marked; a lost answer is settled from the used weight; an unclear one becomes unsure and is never sent twice");
}

static void test_send_result_after_a_settings_change_is_unsure(void) {
    fake_start(100, 600); fake.get_delay_ms = 250; fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    assert(spoolman_hold(sp, 12.5, SPOOLMAN_HELD_SERVER));
    assert(call_held_send("1=7") == 202);
    assert(call_config("url=http://127.0.0.1:9") == 200);       /* while the request is out */
    assert(!spools.held[0].sending && spools.held[0].reason == SPOOLMAN_HELD_UNSURE);
    finish_send();                                              /* its result belongs to the old server: dropped */
    assert(spools.held_count >= 1 && spools.held[0].reason == SPOOLMAN_HELD_UNSURE && !spools.held[0].sending);
    fake_finish(); teardown_file();
    puts("PASS: a send that is out when the address changes is not tracked on: its entry is unsure, never sent again");
}

static void test_dont_send_and_remove_send_nothing(void) {
    fake_start(100, 600); fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    assert(spoolman_hold(sp, 12.5, SPOOLMAN_HELD_LOGIN) && spoolman_hold(sp, 4, SPOOLMAN_HELD_UNSURE) && spoolman_hold(sp, 2, SPOOLMAN_HELD_NOT_FOUND));
    assert(call_held_discard("id=9") == 404 && call_held_discard("") == 400 && spools.held_count == 3);
    assert(call_held_discard("id=1") == 200);
    assert(spools.held_count == 2 && held_index(1) < 0 && held_index(2) >= 0 && held_index(3) >= 0);
    assert(call_held_discard("id=2") == 200 && spools.held_count == 1 && spools.held[0].id == 3);
    spools.held[0].sending = 1;
    assert(call_held_discard("id=3") == 409 && spools.held_count == 1);                         /* not while it is being sent */
    spools.held[0].sending = 0;
    assert(call_held_discard("id=3") == 200 && spools.held_count == 0);
    assert(fake.use_count == 0 && fake.get_count == 0 && fake.list_count == 0);                 /* not one request */
    assert(near(sp->remaining, 500));
    spools_load();
    assert(spools.held_count == 0);                                                              /* saved */
    fake_finish(); teardown_file();
    puts("PASS: Don't send / Remove from list takes one entry off, saves it and sends nothing to Spoolman");
}

static void test_remove_old_connections_unlinks_and_keeps_the_grams(void) {
    fake_start(100, 600); fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    add_linked_spool("5555555555555555", "8", 0);
    add_linked_spool("6666666666666666", "", 0);
    assert(call_unlink_old() == 409);                                           /* no old connections yet */
    assert(call_config("url=http://127.0.0.1:9") == 200 && spoolman_stale_count() == 2);
    sp = &spools.spools[0];
    sp->spoolman_unsent = 2.5;                                                  /* used again before the user decided */
    unsigned gen = sp->spoolman_gen;
    assert(call_unlink_old() == 200);
    assert(spools.count == 3 && !spools.spools[0].spoolman_id[0] && !spools.spools[1].spoolman_id[0] && sp->spoolman_gen == gen + 1);
    assert(near(sp->remaining, 500) && !strcmp(sp->name, "Mine"));              /* the spools stay */
    assert(spoolman_stale_count() == 0 && spools.held_count == 1 && spools.held[0].reason == SPOOLMAN_HELD_SERVER && near(spools.held[0].grams, 2.5));
    assert(call_unlink_old() == 409);
    char *text = get_spools_text();
    assert(strstr(text, "\"spoolman_stale_links\":0"));
    free(text);
    fake_finish(); teardown_file();
    puts("PASS: Remove old connections unlinks every spool (they stay), keeps what they still had pending on the list, and is gone afterwards");
}

/* A handler that takes only the socket; the whole reply (head and body) lands in `out`. */
static int call_plain(void (*handler)(int), char *out, size_t cap) {
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    handler(pair[0]);
    close(pair[0]);
    size_t used = 0; ssize_t n;
    while ((n = recv(pair[1], out + used, cap - 1 - used, 0)) > 0) used += (size_t)n;
    out[used] = 0; close(pair[1]);
    return atoi(strchr(out, ' ') + 1);
}

static void test_old_connections_stop_the_pull_until_the_user_decides(void) {
    fake_start(100, 600); fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    char body[64], reply[8192];
    snprintf(spools.spoolman_url, sizeof(spools.spoolman_url), "http://127.0.0.1:9");      /* the old server */
    snprintf(body, sizeof(body), "url=http://127.0.0.1:%d", (int)fake.port);
    assert(call_config(body) == 200 && spoolman_stale_count() == 1);                      /* a new server: spool 7 is an old connection */
    sp = &spools.spools[0];
    cycle();
    assert(fake.get_count + fake.list_count > 0);                                          /* the sync did run and did read Spoolman ... */
    assert(near(sp->remaining, 500) && !strcmp(sp->name, "Mine") && !strcmp(sp->material, "PLA") && !strcmp(sp->color, "#FFFFFF"));   /* ... and applied nothing */
    sp->spoolman_unsent = 2;                                                               /* usage is only collected meanwhile: nothing goes out */
    cycle(); cycle();
    assert(fake.use_count == 0 && near(sp->spoolman_unsent, 2) && near(sp->spoolman_inflight, 0) && fake.used == 100);
    assert(near(sp->remaining, 500) && !strcmp(sp->name, "Mine"));
    /* "Keep the connections": the same server under a new address; from now on the pull is applied again and what was
     * collected is sent. */
    assert(call_plain(spoolman_keep_old_response, reply, sizeof(reply)) == 200);
    assert(spoolman_stale_count() == 0 && !strcmp(sp->spoolman_id, "7") && spools.held_count == 0);
    cycle();
    assert(fake.use_count == 1 && near(fake.last_use, 2) && near(sp->spoolman_unsent, 0) && near(sp->spoolman_inflight, 0));
    assert(near(sp->remaining, 598) && !strcmp(sp->name, "Jet Black") && !strcmp(sp->material, "PETG"));   /* 600 less the 2 g sent above */
    assert(call_plain(spoolman_keep_old_response, reply, sizeof(reply)) == 409);          /* nothing left to decide */
    char *text = get_spools_text();
    assert(strstr(text, "\"spoolman_stale_links\":0"));
    free(text);
    spools_load();                                                                          /* the decision is saved */
    assert(spools_available && spoolman_stale_count() == 0 && !strcmp(spools.spools[0].spoolman_id, "7"));
    /* "Remove the old connections" lifts it as well: the spool is no longer linked, so there is nothing to pull into. */
    spools.spools[0].spoolman_stale = 1;
    assert(call_unlink_old() == 200 && spoolman_stale_count() == 0);
    fake_finish(); teardown_file();
    puts("PASS: while old connections exist nothing from Spoolman is applied to those spools and no usage is sent (it is collected); keeping or removing them lifts it");
}

static int is_server_change(const char *from, const char *to) {
    setup_linked(2, 500);
    snprintf(spools.spoolman_url, sizeof(spools.spoolman_url), "%s", from);
    char body[SPOOLMAN_URL_MAX + 8]; snprintf(body, sizeof(body), "url=%s", to);
    unsigned epoch = spoolman_epoch;
    assert(call_config(body) == 200);
    assert(!strcmp(spools.spoolman_url, to));                                              /* the new text is kept either way */
    int changed = spoolman_stale_count() > 0;
    assert(changed == (spools.held_count == 1) && changed == (spoolman_epoch == epoch + 1));
    assert(changed || near(spools.spools[0].spoolman_unsent, 2));                           /* the same server: nothing was touched */
    teardown_file();
    return changed;
}

/* ---- review of the list (23.11.): old connections, collected usage, 404 only with a living list, 5xx ---------- */

static void test_remove_old_connections_leaves_spools_imported_from_the_new_server(void) {
    /* The new server has two spools of its own (11 and 12). The old connection is spool 7 of the old address. */
    char **items = make_items(2, 10, 11);
    fake_start(100, 600); fake.list_items = (const char *const *)items; fake.list_n = 2; fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    char body[64], reply[8192];
    snprintf(spools.spoolman_url, sizeof(spools.spoolman_url), "http://127.0.0.1:9");
    snprintf(body, sizeof(body), "url=http://127.0.0.1:%d", (int)fake.port);
    assert(call_config(body) == 200 && spoolman_stale_count() == 1);
    cycle();                                                                   /* the sync imports the two spools of the new server */
    assert(spools.count == 3 && !strcmp(spools.spools[1].spoolman_id, "11") && !strcmp(spools.spools[2].spoolman_id, "12"));
    assert(spoolman_stale_count() == 1);                                       /* only the spool of the old address is an old connection */
    char *text = get_spools_text();
    assert(strstr(text, "\"spoolman_stale_links\":1,"));
    free(text);
    assert(call_unlink_old() == 200);
    sp = &spools.spools[0];
    assert(!sp->spoolman_id[0] && !strcmp(spools.spools[1].spoolman_id, "11") && !strcmp(spools.spools[2].spoolman_id, "12"));
    assert(spoolman_stale_count() == 0);
    cycle();
    assert(spools.count == 3);                                                 /* no spool of the new server is added a second time */
    assert(call_plain(spoolman_keep_old_response, reply, sizeof(reply)) == 409);
    fake_finish(); teardown_file(); free_items(items, 2);
    puts("PASS: Remove old connections lets go of the spools of the old address only; spools imported from the new server since stay linked and are not added twice");
}

static void test_usage_is_only_collected_while_old_connections_wait(void) {
    char **items = make_items(1, 10, 11);
    fake_start(100, 600); fake.list_items = (const char *const *)items; fake.list_n = 1; fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    char body[64];
    snprintf(spools.spoolman_url, sizeof(spools.spoolman_url), "http://127.0.0.1:9");
    snprintf(body, sizeof(body), "url=http://127.0.0.1:%d", (int)fake.port);
    assert(call_config(body) == 200);
    cycle();                                                                   /* spool 11 is imported (not an old connection) */
    spool_entry *fresh = &spools.spools[1];
    assert(spools.count == 2 && !strcmp(fresh->spoolman_id, "11") && !fresh->spoolman_stale);
    sp = &spools.spools[0];
    sp->spoolman_unsent = 2; fresh->spoolman_unsent = 3;                       /* both used before the decision */
    cycle(); cycle();
    assert(fake.use_count == 0 && near(sp->spoolman_unsent, 2) && near(fresh->spoolman_unsent, 3));   /* nothing is sent, nothing is lost */
    assert(near(sp->spoolman_inflight, 0) && near(fresh->spoolman_inflight, 0) && spools.held_count == 0);
    /* Removing the old connections: the collected grams of the old spool go on the list; the other spool sends its own. */
    assert(call_unlink_old() == 200);
    sp = &spools.spools[0]; fresh = &spools.spools[1];
    assert(spools.held_count == 1 && spools.held[0].reason == SPOOLMAN_HELD_SERVER && near(spools.held[0].grams, 2) && fake.use_count == 0);
    cycle();
    assert(fake.use_count == 1 && near(fake.last_use, 3) && near(fresh->spoolman_unsent, 0) && spools.held_count == 1);
    fake_finish(); teardown_file(); free_items(items, 1);
    puts("PASS: while old connections wait no usage is sent, only collected; after the choice it is sent (kept) or on the list (removed), none of it lost");
}

static void test_a_manual_link_is_not_an_old_connection(void) {
    fake_start(100, 600); fake_launch();
    setup_linked(0, 500);
    char body[96];
    snprintf(spools.spoolman_url, sizeof(spools.spoolman_url), "http://127.0.0.1:9");
    snprintf(body, sizeof(body), "url=http://127.0.0.1:%d", (int)fake.port);
    assert(call_config(body) == 200 && spoolman_stale_count() == 1);
    snprintf(body, sizeof(body), "id=%s\nspoolman_id=", spools.spools[0].id);   /* the user unlinks the old spool by hand ... */
    assert(call_handler(spools_save_response, body) == 200);
    assert(spoolman_stale_count() == 0);                                       /* ... nothing is left to decide */
    spool_entry *other = add_linked_spool("5555555555555555", "", 0);
    snprintf(body, sizeof(body), "id=%s\nspoolman_id=9", other->id);           /* ... and links another spool afterwards */
    assert(call_handler(spools_save_response, body) == 200);
    assert(spoolman_stale_count() == 0 && !spools.spools[1].spoolman_stale);   /* that one belongs to the server in use */
    spools.spools[0].spoolman_stale = 1;                                       /* an old connection, then relinked by hand */
    snprintf(spools.spools[0].spoolman_id, sizeof(spools.spools[0].spoolman_id), "7");
    snprintf(body, sizeof(body), "id=%s\nspoolman_id=8", spools.spools[0].id);
    assert(call_handler(spools_save_response, body) == 200 && spoolman_stale_count() == 0);
    fake_finish(); teardown_file();
    puts("PASS: a link set by hand is never an old connection; unlinking the old ones by hand leaves nothing to decide, and a spool linked later is not counted");
}

static void test_an_address_typed_after_the_sync_was_off_asks_about_the_old_connections(void) {
    /* With an empty address the printer cannot know which server the numbers belong to: typing one in again asks, even
     * when it is the same server (one click on "Keep connections"). Guessing "the same" could pull another server's data. */
    fake_start(100, 600); fake_launch();
    setup_linked(0, 500);
    char body[64], reply[8192];
    snprintf(body, sizeof(body), "url=http://127.0.0.1:%d", (int)fake.port);
    assert(call_config("url=") == 200 && spoolman_stale_count() == 0 && !spools.spoolman_url[0]);
    assert(call_config(body) == 200 && spoolman_stale_count() == 1);
    cycle();
    assert(near(spools.spools[0].remaining, 500) && fake.use_count == 0);                 /* nothing is taken over meanwhile */
    assert(call_plain(spoolman_keep_old_response, reply, sizeof(reply)) == 200 && spoolman_stale_count() == 0);
    cycle();
    assert(near(spools.spools[0].remaining, 600));
    fake_finish(); teardown_file();
    puts("PASS: an address typed in after the sync was off asks about the old connections once; Keep connections ends it");
}

static void test_remove_old_connections_with_a_full_list_changes_nothing(void) {
    fake_start(100, 600); fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    assert(call_config("url=http://127.0.0.1:9") == 200 && spoolman_stale_count() == 1);
    sp = &spools.spools[0];
    sp->spoolman_unsent = 2.5;
    for (int i = 0; i < SPOOLMAN_HELD_MAX; ++i) { spools.held[i].id = (unsigned)i + 1; spools.held[i].reason = SPOOLMAN_HELD_NOT_FOUND; spools.held[i].grams = 1; snprintf(spools.held[i].spool, sizeof(spools.held[i].spool), "%016d", i); }
    spools.held_count = SPOOLMAN_HELD_MAX;
    unsigned gen = sp->spoolman_gen;
    assert(call_unlink_old() == 409);                                          /* no room for the 2.5 g: refused as a whole */
    assert(!strcmp(sp->spoolman_id, "7") && sp->spoolman_stale && spoolman_stale_count() == 1 && sp->spoolman_gen == gen);
    assert(near(sp->spoolman_unsent, 2.5) && spools.held_count == SPOOLMAN_HELD_MAX);
    for (int i = 0; i < SPOOLMAN_HELD_MAX; ++i) assert(near(spools.held[i].grams, 1) && spools.held[i].reason == SPOOLMAN_HELD_NOT_FOUND);
    fake_finish(); teardown_file();
    puts("PASS: Remove old connections with a full list is refused and changes nothing: the link, the flag and the grams stay");
}

static void test_a_404_counts_only_while_the_list_answers(void) {
    /* A wrong base path or a proxy that says 404 to everything: the spool is not "gone", whatever the number of cycles. */
    fake_start(100, 600); fake.answer_all = 404; fake_launch();
    spool_entry *sp = setup_linked(5, 500);
    for (int i = 0; i < 4; ++i) cycle();
    get_status();
    status_has("\"orphaned_count\":0"); status_has("\"result\":\"not_found\"");      /* the error is shown, nothing is moved */
    assert(sp->spoolman_miss == 0 && spools.held_count == 0 && near(sp->spoolman_unsent, 5) && fake.use_count == 0);
    /* The list answers now, only this spool is missing: that counts, two cycles in a row. */
    fake.answer_all = 0; fake.missing_id = 7;
    cycle();
    assert(sp->spoolman_miss == 1 && spools.held_count == 0);
    cycle();
    assert(spools.held_count == 1 && spools.held[0].reason == SPOOLMAN_HELD_NOT_FOUND && near(spools.held[0].grams, 5));
    /* The same for "Send selected": a target that cannot be told from a wrong base path stays as it was. */
    fake.missing_id = 0; fake.answer_all = 404;
    spools.held[0].reason = SPOOLMAN_HELD_SERVER;
    assert(call_held_send("1=7") == 202); finish_send();
    assert(spools.held_count == 1 && spools.held[0].reason == SPOOLMAN_HELD_SERVER && !spools.held[0].sending && fake.use_count == 0);
    fake.answer_all = 0; fake.missing_id = 7;
    assert(call_held_send("1=7") == 202); finish_send();
    assert(spools.held[0].reason == SPOOLMAN_HELD_NOT_FOUND && fake.use_count == 0);          /* the list answers: it really is missing */
    fake_finish(); teardown_file();
    puts("PASS: 404 for a spool counts as gone only while Spoolman's list answers; a wrong base path moves no grams (sync and Send selected)");
}

static void test_a_failed_answer_between_two_404_starts_the_count_again(void) {
    fake_start(100, 600); fake.missing_id = 7; fake_launch();
    spool_entry *sp = setup_linked(5, 500);
    cycle();                                                                   /* 404 */
    assert(sp->spoolman_miss == 1);
    fake.missing_id = 0; fake.answer_all = 503;
    cycle();                                                                   /* 503: no answer about the spool at all */
    assert(sp->spoolman_miss == 0);
    fake.answer_all = 0; fake.missing_id = 7;
    cycle();                                                                   /* 404 again: the first one of a new run, not "twice in a row" */
    assert(sp->spoolman_miss == 1 && spools.held_count == 0 && near(sp->spoolman_unsent, 5));
    cycle();
    assert(spools.held_count == 1 && spools.held[0].reason == SPOOLMAN_HELD_NOT_FOUND);
    fake_finish(); teardown_file();
    puts("PASS: an error answer between two 404 answers starts the count again");
}

static void test_an_error_answer_starts_the_count_again_for_a_spool_without_usage(void) {
    /* A spool with nothing to send is only refreshed: an error answer for it must end the run of 404 answers as well. */
    fake_start(100, 600); fake.missing_id = 7; fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    cycle();
    assert(sp->spoolman_miss == 1);
    fake.missing_id = 0; fake.answer_all = 503;
    cycle();
    assert(sp->spoolman_miss == 0);
    fake.answer_all = 0; fake.missing_id = 7;
    cycle();
    assert(sp->spoolman_miss == 1);                                            /* the first of a new run, not the second */
    cycle();
    assert(sp->spoolman_miss == 2 && spools.held_count == 0);                  /* nothing was pending: nothing to put on the list */
    fake_finish(); teardown_file();
    puts("PASS: an error answer starts the count of 404 answers again for a spool that has no usage to send, too");
}

static void test_a_5xx_while_sending_selected_is_settled_from_the_used_weight(void) {
    fake_start(100, 600); fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    assert(spoolman_hold(sp, 12.5, SPOOLMAN_HELD_SERVER) && spoolman_hold(sp, 4, SPOOLMAN_HELD_LOGIN));
    /* Spoolman applied it and then said 500: the used weight grew, so it is counted: the entry goes (sending it again would count it twice). */
    fake.use_mode = 3;
    assert(call_held_send("1=7") == 202); finish_send();
    assert(fake.use_count == 1 && near(fake.used, 112.5));
    assert(spools.held_count == 1 && spools.held[0].id == 2 && spools.held[0].reason == SPOOLMAN_HELD_LOGIN);
    /* Spoolman said 500 and did not apply it: that cannot be told from "applied later": unsure, and never sent again. */
    fake.use_mode = 4;
    assert(call_held_send("2=7") == 202); finish_send();
    assert(fake.use_count == 2 && near(fake.used, 112.5));
    assert(spools.held_count == 1 && spools.held[0].reason == SPOOLMAN_HELD_UNSURE && !spools.held[0].sending);
    fake.use_mode = 0;
    assert(call_held_send("2=7") == 409 && fake.use_count == 2);
    fake_finish(); teardown_file();
    puts("PASS: a 500 on Send selected is settled from the used weight: applied -> gone from the list; not applied -> unsure, never sent twice");
}

static void test_a_call_is_checked_as_a_whole_and_may_carry_all_32_entries(void) {
    fake_start(100, 600); fake_launch();
    setup_linked(0, 500);
    for (int i = 0; i < SPOOLMAN_HELD_MAX; ++i) { spools.held[i].id = (unsigned)i + 1; spools.held[i].reason = SPOOLMAN_HELD_SERVER; spools.held[i].grams = 1; snprintf(spools.held[i].spool, sizeof(spools.held[i].spool), "%016d", i); snprintf(spools.held[i].spoolman_id, sizeof(spools.held[i].spoolman_id), "7"); }
    spools.held_count = SPOOLMAN_HELD_MAX;
    assert(call_held_send("1=7\n1=8") == 400);                                 /* the same entry twice would be sent twice */
    assert(call_held_send("1=7\n2=7\n1=7") == 400);
    assert(fake.use_count == 0 && !spools.held[0].sending && !worker_running());
    char body[512]; size_t at = 0;
    for (int i = 1; i <= 17; ++i) at += (size_t)snprintf(body + at, sizeof(body) - at, "%d=7\n", i);    /* more than 16 in one call */
    assert(call_held_send(body) == 202); finish_send();
    assert(fake.use_count == 17 && spools.held_count == SPOOLMAN_HELD_MAX - 17);
    for (int i = 0; i < spools.held_count; ++i) spools.held[i].id = (unsigned)i + 1;
    at = 0; body[0] = 0;
    for (int i = 1; i <= spools.held_count; ++i) at += (size_t)snprintf(body + at, sizeof(body) - at, "%d=7\n", i);
    assert(call_held_send(body) == 202); finish_send();                        /* the rest of the 32 in one call */
    assert(fake.use_count == 32 && spools.held_count == 0);
    fake_finish(); teardown_file();
    puts("PASS: the same entry twice in one call is refused as a whole; one call may carry more than 16 and up to all 32 entries");
}

static void test_the_same_server_written_differently_is_not_a_new_server(void) {
    fake_start(0, 0);
    assert(!is_server_change("http://192.0.2.5:7912", "http://192.0.2.5:7912/"));          /* a slash at the end */
    assert(!is_server_change("http://192.0.2.5/spoolman", "http://192.0.2.5/spoolman/"));
    assert(!is_server_change("http://192.0.2.5", "http://192.0.2.5:80"));                  /* the default port, written out */
    assert(!is_server_change("http://192.0.2.5:80/", "http://192.0.2.5"));
    assert(is_server_change("http://192.0.2.5:7912", "http://192.0.2.5:7913"));            /* another port */
    assert(is_server_change("http://192.0.2.5:7912", "http://192.0.2.6:7912"));            /* another host */
    assert(is_server_change("http://192.0.2.5:7912", "http://192.0.2.5:7912/other"));      /* another base path */
    assert(is_server_change("http://192.0.2.5:7912/a", "http://192.0.2.5:7912/A"));        /* paths are case sensitive */
    assert(is_server_change("", "http://192.0.2.5:7912"));                                 /* from nothing */
    /* A running request of the old text is not thrown away when only the text changed. */
    setup_linked(0, 500);
    snprintf(spools.spoolman_url, sizeof(spools.spoolman_url), "%s", "http://192.0.2.5:7912");
    unsigned epoch = spoolman_epoch;
    assert(call_config("url=http://192.0.2.5:7912/") == 200 && spoolman_epoch == epoch);
    teardown_file();
    puts("PASS: only another host, port or base path is a new server; a slash or the default port written out is not (nothing on the list, no old connections)");
}

static void wait_catalog(void) {
    for (int i = 0; i < 4000; ++i) {
        pthread_mutex_lock(&spoolman_catalog_mutex);
        int loading = spoolman_catalog_loading;
        pthread_mutex_unlock(&spoolman_catalog_mutex);
        if (!loading) return;
        sleep_ms(5);
    }
    assert(!"the catalog never finished");
}
static int count_of(const char *text, const char *what) {
    int n = 0;
    for (const char *p = text; (p = strstr(p, what)); p += strlen(what)) n++;
    return n;
}

static void test_the_catalog_is_read_in_pages(void) {
    static char reply[65536];
    /* 100 spools of about 1.8 KB: 180 KB, more than the one 128 KiB reply the catalog used to ask for. */
    int n = 100; char **items = make_items(n, 1500, 1);
    fake_start(0, 0); fake.list_items = (const char *const *)items; fake.list_n = n; fake_launch();
    setup_linked(0, 500);
    assert(call_plain(spoolman_catalog_refresh_response, reply, sizeof(reply)) == 202);
    wait_catalog();
    assert(call_plain(spoolman_catalog_response, reply, sizeof(reply)) == 200);
    assert(strstr(reply, "\"error\":\"\"") && strstr(reply, "\"truncated\":false") && count_of(reply, "{\"id\":\"") == 100);
    assert(fake.list_count == 5 && fake.saw_sort);                                          /* the same sorted pages as the sync reads */
    fake_finish(); free_items(items, n);
    /* More than the catalog holds: it says so instead of leaving spools out silently. */
    n = 250; items = make_items(n, 50, 1);
    fake_start(0, 0); fake.list_items = (const char *const *)items; fake.list_n = n; fake_launch();
    snprintf(spools.spoolman_url, sizeof(spools.spoolman_url), "http://127.0.0.1:%d", (int)fake.port);
    assert(call_plain(spoolman_catalog_refresh_response, reply, sizeof(reply)) == 202);
    wait_catalog();
    assert(call_plain(spoolman_catalog_response, reply, sizeof(reply)) == 200);
    assert(strstr(reply, "\"error\":\"\"") && strstr(reply, "\"truncated\":true") && count_of(reply, "{\"id\":\"") == SPOOLMAN_CATALOG_MAX);
    fake_finish(); free_items(items, n);
    /* A refusal is told as one. */
    fake_start(0, 0); fake.answer_all = 500; fake_launch();
    snprintf(spools.spoolman_url, sizeof(spools.spoolman_url), "http://127.0.0.1:%d", (int)fake.port);
    assert(call_plain(spoolman_catalog_refresh_response, reply, sizeof(reply)) == 202);
    wait_catalog();
    assert(call_plain(spoolman_catalog_response, reply, sizeof(reply)) == 200);
    assert(strstr(reply, "\"error\":\"Spoolman refused the request\""));          /* the last good list stays meanwhile */
    fake_finish(); teardown_file();
    puts("PASS: the catalog for \"Send selected\" is read in pages like the sync (past 128 KiB), says when it is cut at its limit, and tells a refusal");
}

static void test_one_entry_named_twice_in_different_spellings_is_refused(void) {
    fake_start(100, 600); fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    assert(spoolman_hold(sp, 12.5, SPOOLMAN_HELD_SERVER) && spoolman_hold(sp, 4, SPOOLMAN_HELD_NOT_FOUND));
    /* "1" and "01" are different texts but the same entry: it would be sent twice, to two spools. */
    assert(call_held_send("1=7\n01=8") == 400);
    assert(call_held_send("1=7\n001=7") == 400);
    assert(call_held_send("2=7\n1=7\n02=7") == 400);
    assert(!spools.held[0].sending && !spools.held[1].sending && fake.use_count == 0 && !worker_running());
    assert(call_held_send("1=7\n2=7") == 202); finish_send();                           /* two different entries still go together */
    assert(fake.use_count == 2 && spools.held_count == 0 && near(fake.used, 100 + 12.5 + 4));
    fake_finish(); teardown_file();
    puts("PASS: one entry named twice in one request (\"1\" and \"01\") is refused as a whole; nothing is sent twice");
}

static void test_an_entry_that_is_being_sent_is_not_sent_again(void) {
    fake_start(100, 600); fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    assert(spoolman_hold(sp, 12.5, SPOOLMAN_HELD_SERVER));
    assert(call_held_send("1=7") == 202);
    wait_worker();                                              /* the request is done, its result is not taken over yet */
    assert(!worker_running() && spools.held[0].sending && fake.use_count == 1);
    assert(call_held_send("1=7") == 409 && strstr(response, "being sent"));      /* the entry itself says no, not the busy worker */
    assert(fake.use_count == 1 && near(fake.used, 112.5));      /* the grams were not counted twice */
    spoolman_tick(virtual_now + 1);
    assert(spools.held_count == 0);
    fake_finish(); teardown_file();
    puts("PASS: an entry that is being sent is refused a second time (409), also when the worker is already done and its result not taken over yet");
}

static void test_a_send_result_of_an_older_server_setting_changes_nothing(void) {
    fake_start(100, 600); fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    assert(spoolman_hold(sp, 12.5, SPOOLMAN_HELD_SERVER));
    spools.held[0].sending = 1;                                 /* as if the request were out */
    spoolman_send_result old = {spools.held[0].id, spoolman_epoch - 1, SPOOLMAN_SENT};
    spoolman_held_apply(&old, 1);                               /* its answer is from before the last change of the server */
    assert(spools.held_count == 1 && spools.held[0].sending && spools.held[0].reason == SPOOLMAN_HELD_SERVER);
    spoolman_send_result now = {spools.held[0].id, spoolman_epoch, SPOOLMAN_SENT};
    spoolman_held_apply(&now, 1);                               /* the same answer under the current settings is taken over */
    assert(spools.held_count == 0);
    fake_finish(); teardown_file();
    puts("PASS: a send result that belongs to an older server setting is not taken over; one of the current settings is");
}

/* ---- the server settings: a catalog and a choice belong to the server they were read from ---------------- */

static unsigned gen_of(const char *reply) {
    const char *p = strstr(reply, "\"gen\":");
    assert(p);
    return (unsigned)strtoul(p + 6, NULL, 10);
}

static void test_the_generation_follows_the_server_settings(void) {
    setup_linked(0, 500);
    snprintf(spools.spoolman_url, sizeof(spools.spoolman_url), "%s", "http://192.0.2.5:7912");
    const unsigned g0 = spoolman_generation();
    assert(g0 == spoolman_generation());                                                   /* stable while nothing changes */
    assert(call_config("url=http://192.0.2.5:7912/") == 200 && spoolman_generation() == g0);   /* the same server, written differently */
    assert(call_config("enabled=0") == 200 && call_config("enabled=1") == 200 && spoolman_generation() == g0);   /* the switch alone */
    assert(call_config("basic_auth=other:secret") == 200);                                 /* another login */
    const unsigned g1 = spoolman_generation();
    assert(g1 != g0);
    assert(call_config("url=http://192.0.2.6:7912") == 200);                               /* another server */
    const unsigned g2 = spoolman_generation();
    assert(g2 != g1 && g2 != g0);
    char *text = get_spools_text();
    char want[48]; snprintf(want, sizeof(want), "\"spoolman_gen\":%u,", g2);
    assert(strstr(text, want));                                                            /* the page learns it with every look at the list */
    free(text);
    teardown_file();
    puts("PASS: the generation of the server settings changes with another address or login, not with the same server written differently or the switch");
}

static void test_send_selected_is_refused_for_another_generation(void) {
    fake_start(100, 600); fake_launch();
    spool_entry *sp = setup_linked(0, 500);
    assert(spoolman_hold(sp, 12.5, SPOOLMAN_HELD_NOT_FOUND));
    char body[64];
    assert(call_held_send_raw("1=7") == 400);                                             /* no generation: refused, not guessed */
    assert(call_held_send_raw("gen=abc\n1=7") == 400 && call_held_send_raw("gen=\n1=7") == 400);
    assert(call_held_send_raw("gen=99999999999\n1=7") == 400);
    snprintf(body, sizeof(body), "gen=%u\n1=7", spoolman_generation() + 1);
    assert(call_held_send_raw(body) == 409);
    /* A choice made before the login changed is refused ... */
    unsigned old = spoolman_generation();
    assert(call_config("basic_auth=other:secret") == 200);
    snprintf(body, sizeof(body), "gen=%u\n1=7", old);
    assert(call_held_send_raw(body) == 409);
    assert(strstr(response, "choose the spools again"));
    assert(!spools.held[0].sending && fake.use_count == 0 && !worker_running());
    /* ... and so is one made before the address changed: nothing goes out, not to the new server either. */
    old = spoolman_generation();
    snprintf(spools.spoolman_url, sizeof(spools.spoolman_url), "http://127.0.0.1:%d", (int)fake.port);
    assert(call_config("url=http://127.0.0.1:9") == 200);
    snprintf(body, sizeof(body), "gen=%u\n1=7", old);
    assert(call_held_send_raw(body) == 409);
    assert(!spools.held[0].sending && fake.use_count == 0 && fake.get_count == 0 && !worker_running());
    /* The same request with the current generation goes through. */
    snprintf(spools.spoolman_url, sizeof(spools.spoolman_url), "http://127.0.0.1:%d", (int)fake.port);
    snprintf(body, sizeof(body), "gen=%u\n1=7", spoolman_generation());
    assert(call_held_send_raw(body) == 202); finish_send();
    assert(fake.use_count == 1 && spools.held_count == 0);
    fake_finish(); teardown_file();
    puts("PASS: Send selected is refused (400 without a generation, 409 for an older one) before anything is sent; the current generation goes through");
}

static void test_a_login_entry_ends_when_the_server_changes(void) {
    fake_start(100, 600); fake_launch();
    spool_entry *sp = setup_linked(3, 500);
    assert(call_config("basic_auth=other:secret") == 200);
    assert(spools.held_count == 1 && spools.held[0].reason == SPOOLMAN_HELD_LOGIN && near(spools.held[0].grams, 3));
    /* The same server written differently changes nothing; another login keeps the entry a login entry. */
    char same[64]; snprintf(same, sizeof(same), "url=http://127.0.0.1:%d/", (int)fake.port);
    assert(call_config(same) == 200 && spools.held[0].reason == SPOOLMAN_HELD_LOGIN);
    sp->spoolman_unsent = 2;
    assert(call_config("basic_auth=third:secret") == 200);
    assert(spools.held_count == 1 && spools.held[0].reason == SPOOLMAN_HELD_LOGIN && near(spools.held[0].grams, 5));
    /* Another server: "the spool is the same" is no longer true, its number is the old server's. */
    sp->spoolman_unsent = 1;
    assert(call_config("url=http://127.0.0.1:9") == 200);
    assert(spools.held_count == 1 && spools.held[0].reason == SPOOLMAN_HELD_SERVER && near(spools.held[0].grams, 6));   /* with the usage of this change */
    char *text = get_spools_text();
    assert(!strstr(text, "login_changed") && count_of(text, "\"reason\":\"server_changed\"") == 1);
    free(text);
    spools_load();                                                                         /* saved that way */
    assert(spools.held_count == 1 && spools.held[0].reason == SPOOLMAN_HELD_SERVER && near(spools.held[0].grams, 6));
    /* One that was already being sent stays unsure, whatever its reason was. */
    memset(spools.held, 0, sizeof(spools.held)); spools.held_count = 0;
    sp = &spools.spools[0];
    assert(spoolman_hold(sp, 4, SPOOLMAN_HELD_LOGIN) && spoolman_hold(sp, 5, SPOOLMAN_HELD_NOT_FOUND));
    assert(call_config("url=http://127.0.0.1:10") == 200);
    assert(spools.held[0].reason == SPOOLMAN_HELD_SERVER && spools.held[1].reason == SPOOLMAN_HELD_NOT_FOUND);   /* only login entries end */
    fake_finish(); teardown_file();
    puts("PASS: an entry from a login change becomes a server entry when the server changes later (no \"same spool\" for a number of the old server); other reasons stay");
}

static void test_the_catalog_belongs_to_the_server_settings(void) {
    static char reply[65536];
    int n = 3; char **items = make_items(n, 50, 1);
    fake_start(0, 0); fake.list_items = (const char *const *)items; fake.list_n = n; fake_launch();
    setup_linked(0, 500);
    assert(call_plain(spoolman_catalog_refresh_response, reply, sizeof(reply)) == 202);
    wait_catalog();
    assert(call_plain(spoolman_catalog_response, reply, sizeof(reply)) == 200);
    const unsigned g1 = spoolman_generation();
    assert(gen_of(reply) == g1 && strstr(reply, "\"error\":\"\"") && count_of(reply, "{\"id\":\"") == 3);
    /* The login changes: what was read before is not the list of the server in use and is not served. */
    assert(call_config("basic_auth=other:secret") == 200);
    assert(call_plain(spoolman_catalog_response, reply, sizeof(reply)) == 200);
    assert(gen_of(reply) == spoolman_generation() && gen_of(reply) != g1);
    assert(count_of(reply, "{\"id\":\"") == 0 && strstr(reply, "\"error\":\"Not fetched yet\"") && strstr(reply, "\"truncated\":false"));
    assert(call_plain(spoolman_catalog_refresh_response, reply, sizeof(reply)) == 202);
    wait_catalog();
    assert(call_plain(spoolman_catalog_response, reply, sizeof(reply)) == 200);
    assert(gen_of(reply) == spoolman_generation() && count_of(reply, "{\"id\":\"") == 3);
    /* A read that is still on its way when the server changes is dropped: it never fills the list for the new settings,
     * and the new settings are not left waiting for it. */
    fake.get_delay_ms = 300;
    assert(call_plain(spoolman_catalog_refresh_response, reply, sizeof(reply)) == 202);
    const unsigned g2 = spoolman_generation();
    assert(call_config("url=http://127.0.0.1:9") == 200);                                  /* nothing listens there */
    assert(call_plain(spoolman_catalog_response, reply, sizeof(reply)) == 200);
    assert(gen_of(reply) != g2 && strstr(reply, "\"loading\":false") && count_of(reply, "{\"id\":\"") == 0);
    assert(call_plain(spoolman_catalog_refresh_response, reply, sizeof(reply)) == 202);    /* a new read; the old one is still running */
    wait_catalog();
    for (int i = 0; i < 8; ++i) sleep_ms(250);                                             /* the old read ends now (sleep_ms takes less than a second) */
    assert(call_plain(spoolman_catalog_response, reply, sizeof(reply)) == 200);
    assert(gen_of(reply) == spoolman_generation() && count_of(reply, "{\"id\":\"") == 0);
    assert(strstr(reply, "\"error\":\"Could not reach Spoolman\""));
    pthread_mutex_lock(&spoolman_catalog_mutex);
    assert(spoolman_catalog_count == 0 && spoolman_catalog_gen == spoolman_generation() && !spoolman_catalog_loading);
    pthread_mutex_unlock(&spoolman_catalog_mutex);
    fake_finish(); free_items(items, n); teardown_file();
    puts("PASS: the catalog is tied to the server settings it was read under: another login or address empties it, a read of the old server that ends late is dropped");
}

int main(void) {
    setvbuf(stdout, NULL, _IOLBF, 1024);
    spools_clock = clock_fake; spools_wall = wall_fake;
    test_parse_url();
    test_persistence_and_backward_compat();
    test_charge_accumulates_only_when_linked();
    test_config_endpoint();
    test_end_to_end_push_and_pull();
    test_integration_toggle();
    test_disable_during_baseline();
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
    test_colour_with_alpha();
    test_auto_import();
    test_auto_import_colour_with_alpha();
    test_list_is_read_in_pages();
    test_page_too_big_is_halved();
    test_unreadable_spool_does_not_stop_the_import();
    test_the_stand_ins_of_an_import_are_counted();
    test_a_page_of_junk_does_not_end_the_import();
    test_log_lines_follow_the_orphan_ids();
    test_cycle_without_time_says_so();
    test_server_that_ignores_paging();
    test_server_that_ignores_limit_but_honours_offset();
    test_library_full();
    test_weights_come_from_the_spoolman_fields();
    test_colours_makers_and_escapes();
    test_import_takes_what_spoolman_knows();
    test_linked_spool_without_weight_still_syncs();
    test_linked_spool_is_refreshed_from_the_list();
    test_archived_linked_spool_is_counted_not_left_silent();
    test_hostnames_are_refused();
    test_status_and_log_only_on_change();
    test_server_down_and_timeout_are_told_apart();
    test_deleted_spool_is_reported_and_its_grams_go_on_the_list();
    test_a_single_404_is_not_believed();
    test_a_404_counts_even_when_no_usage_was_waiting();
    test_relinking_starts_the_404_count_again();
    test_grams_in_flight_wait_for_the_second_404();
    test_request_and_cycle_have_a_deadline();
    test_basic_auth_header();
    test_address_or_login_change_puts_pending_usage_on_the_list();
    test_list_survives_restart_and_older_files_load();
    test_send_selected_sends_only_the_chosen_entries();
    test_send_is_refused_without_a_chosen_spool_or_for_unsure();
    test_failed_sends_keep_the_entry();
    test_send_result_after_a_settings_change_is_unsure();
    test_dont_send_and_remove_send_nothing();
    test_remove_old_connections_unlinks_and_keeps_the_grams();
    test_old_connections_stop_the_pull_until_the_user_decides();
    test_remove_old_connections_leaves_spools_imported_from_the_new_server();
    test_usage_is_only_collected_while_old_connections_wait();
    test_a_manual_link_is_not_an_old_connection();
    test_an_address_typed_after_the_sync_was_off_asks_about_the_old_connections();
    test_remove_old_connections_with_a_full_list_changes_nothing();
    test_a_404_counts_only_while_the_list_answers();
    test_a_failed_answer_between_two_404_starts_the_count_again();
    test_an_error_answer_starts_the_count_again_for_a_spool_without_usage();
    test_a_5xx_while_sending_selected_is_settled_from_the_used_weight();
    test_a_call_is_checked_as_a_whole_and_may_carry_all_32_entries();
    test_the_same_server_written_differently_is_not_a_new_server();
    test_the_catalog_is_read_in_pages();
    test_one_entry_named_twice_in_different_spellings_is_refused();
    test_an_entry_that_is_being_sent_is_not_sent_again();
    test_a_send_result_of_an_older_server_setting_changes_nothing();
    test_the_generation_follows_the_server_settings();
    test_send_selected_is_refused_for_another_generation();
    test_a_login_entry_ends_when_the_server_changes();
    test_the_catalog_belongs_to_the_server_settings();
    return 0;
}
