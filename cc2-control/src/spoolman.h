/* Spoolman sync: an optional bridge between the local spool library (spools.h)
 * and a self-hosted Spoolman server on the LAN. Spoolman is the inventory
 * source of truth for any spool you link to it (spool_entry.spoolman_id):
 * this module periodically pushes the grams spools.h has measured locally
 * (PUT /api/v1/spool/{id}/use) and pulls Spoolman's own material/colour/name/
 * remaining-weight back into the local entry, the same way a refill or an
 * edit made in Spoolman's own web UI would otherwise never reach the printer.
 *
 * Network I/O never runs on the main loop thread. spoolman_tick() -- called
 * from spools_tick(), same place the local library's own periodic save runs
 * -- snapshots what needs sending under spoolman_outbox_mutex, then spawns one
 * short-lived detached worker thread if none is already in flight. The worker
 * only ever writes into spoolman_outbox under that same mutex; spoolman_tick()
 * is the only place that drains the outbox back into the shared `spools`
 * struct, so `spools` itself is read or written only from the main thread.
 * A failed push leaves spoolman_unsent untouched, so the next tick retries it
 * -- it is never lost, and (PUT /use is Spoolman's own absolute delta, not
 * read-modify-write) never double-counted either. */

#define SPOOLMAN_TICK_MS 120000LL     /* same cadence as the local library's own autosave */
#define SPOOLMAN_TIMEOUT_S 5
#define SPOOLMAN_BODY_MAX 8192
#define SPOOLMAN_HOST_MAX 128
#define SPOOLMAN_PATH_MAX 128

typedef struct {
    char spool_id[SPOOL_ID_LEN + 1];       /* the local spool this result is for */
    double sent_grams;                      /* > 0: this many grams were confirmed sent, subtract from spoolman_unsent */
    int have_pull;                          /* a fresh read of the Spoolman spool came back */
    char material[SPOOL_MATERIAL_MAX + 1], name[SPOOL_TEXT_MAX + 1], color[8];
    double remaining;
} spoolman_result;

static pthread_mutex_t spoolman_outbox_mutex = PTHREAD_MUTEX_INITIALIZER;
static spoolman_result spoolman_outbox[SPOOLS_MAX];
static int spoolman_outbox_count;
static int spoolman_worker_running;
static long long spoolman_last_tick_ms;

/* What the worker needs, copied out of `spools` under the main thread before
 * the worker starts -- the worker never touches `spools` directly. */
typedef struct {
    char url[SPOOLMAN_URL_MAX + 1];
    char basic_auth[SPOOLMAN_AUTH_MAX + 1];
    int count;
    struct {
        char spool_id[SPOOL_ID_LEN + 1];
        char spoolman_id[SPOOLMAN_ID_MAX + 1];
        double grams;   /* pending usage to report; 0 = pull only, no push */
    } jobs[SPOOLS_MAX];
} spoolman_job_batch;

static int spoolman_parse_url(const char *url, char *host, size_t host_cap, int *port, char *base, size_t base_cap) {
    const char *p = url;
    if (!strncmp(p, "http://", 7)) p += 7;
    else return 0; /* no TLS stack is linked into cc2-control; Spoolman on the LAN is plain HTTP */
    if (!*p) return 0;
    const char *slash = strchr(p, '/');
    const char *host_end = slash ? slash : p + strlen(p);
    const char *colon = memchr(p, ':', (size_t)(host_end - p));
    size_t hn = (size_t)((colon ? colon : host_end) - p);
    if (hn == 0 || hn >= host_cap) return 0;
    memcpy(host, p, hn); host[hn] = 0;
    *port = colon ? atoi(colon + 1) : 80;
    if (*port <= 0 || *port > 65535) return 0;
    size_t bn = slash ? strlen(slash) : 0;
    if (bn >= base_cap) return 0;
    if (slash) { memcpy(base, slash, bn); if (bn && base[bn - 1] == '/') --bn; }
    base[bn] = 0;
    return 1;
}

/* IP literal only -- Spoolman on the LAN is reached by address here, like
 * every other printer-side network target in this codebase. getaddrinfo's
 * NSS modules do not work reliably in this binary's static link, so a
 * hostname in the URL fails cleanly (connect fails) rather than resolving
 * unpredictably. */
static int spoolman_connect(const char *host, int port) {
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    struct timeval tv = {SPOOLMAN_TIMEOUT_S, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -1; }
    return fd;
}

/* One request/response over a fresh connection (Spoolman's own API is small
 * and infrequent here; a persistent connection is not worth the complexity).
 * `body` may be NULL (GET). Returns the HTTP status code, or -1 on a network
 * failure (no request was confirmed delivered); `reply` holds the response
 * body (not the headers) on success, truncated if it would overflow. */
static int spoolman_request(const char *host, int port, const char *basic_auth, const char *method,
                            const char *path, const char *body, char *reply, size_t reply_cap) {
    int fd = spoolman_connect(host, port);
    if (fd < 0) return -1;
    char auth_header[256] = "";
    if (basic_auth[0]) {
        char encoded[200];
        size_t n = strlen(basic_auth), o = 0;
        static const char table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (size_t i = 0; i < n && o + 4 < sizeof(encoded); i += 3) {
            uint32_t v = (uint32_t)(unsigned char)basic_auth[i] << 16;
            if (i + 1 < n) v |= (uint32_t)(unsigned char)basic_auth[i + 1] << 8;
            if (i + 2 < n) v |= (unsigned char)basic_auth[i + 2];
            encoded[o++] = table[(v >> 18) & 63]; encoded[o++] = table[(v >> 12) & 63];
            encoded[o++] = i + 1 < n ? table[(v >> 6) & 63] : '=';
            encoded[o++] = i + 2 < n ? table[v & 63] : '=';
        }
        encoded[o] = 0;
        snprintf(auth_header, sizeof(auth_header), "Authorization: Basic %s\r\n", encoded);
    }
    char request[SPOOLMAN_PATH_MAX + 512];
    size_t body_len = body ? strlen(body) : 0;
    int n = snprintf(request, sizeof(request),
        "%s %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n%sContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s",
        method, path, host, auth_header, body_len, body ? body : "");
    if (n <= 0 || (size_t)n >= sizeof(request)) { close(fd); return -1; }
    const char *p = request; size_t left = (size_t)n;
    while (left) { ssize_t sent = send(fd, p, left, 0); if (sent <= 0) { close(fd); return -1; } p += sent; left -= (size_t)sent; }
    char raw[SPOOLMAN_BODY_MAX]; size_t used = 0;
    while (used + 1 < sizeof(raw)) { ssize_t got = recv(fd, raw + used, sizeof(raw) - 1 - used, 0); if (got <= 0) break; used += (size_t)got; }
    close(fd);
    raw[used] = 0;
    int status = 0;
    if (strncmp(raw, "HTTP/1.", 7) == 0) status = atoi(raw + 9);
    const char *split = strstr(raw, "\r\n\r\n");
    const char *resp_body = split ? split + 4 : "";
    snprintf(reply, reply_cap, "%s", resp_body);
    return status >= 100 ? status : -1;
}

/* A string field from a flat JSON object, Spoolman's own escaping (plain
 * ASCII fields only -- material/colour/name never contain \u escapes in
 * practice, and a field that did would just fail spools_text_member's own
 * validation later and be dropped, never crash). */
static int spoolman_field(const char *json, const char *key, char *out, size_t cap) {
    const char *p = strstr(json, key);
    if (!p) { out[0] = 0; return 0; }
    p = strchr(p + strlen(key), ':');
    if (!p) { out[0] = 0; return 0; }
    ++p;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p != '"') { out[0] = 0; return 0; }
    ++p;
    size_t n = 0;
    while (*p && *p != '"' && n + 1 < cap) out[n++] = *p++;
    out[n] = 0;
    return 1;
}

static int spoolman_double_field(const char *json, const char *key, double *out) {
    const char *p = strstr(json, key);
    if (!p) return 0;
    p = strchr(p + strlen(key), ':');
    if (!p) return 0;
    char *endp;
    double value = strtod(p + 1, &endp);
    if (endp == p + 1) return 0;
    *out = value;
    return 1;
}

/* Runs off the main thread. Only touches spoolman_outbox (its own mutex) and
 * the stack-local copy of what to send -- never `spools`. */
static void *spoolman_worker(void *arg) {
    spoolman_job_batch *batch = arg;
    char host[SPOOLMAN_HOST_MAX], base[SPOOLMAN_PATH_MAX];
    int port;
    if (!spoolman_parse_url(batch->url, host, sizeof(host), &port, base, sizeof(base))) { free(batch); return NULL; }
    for (int i = 0; i < batch->count; ++i) {
        spoolman_result r; memset(&r, 0, sizeof(r));
        snprintf(r.spool_id, sizeof(r.spool_id), "%s", batch->jobs[i].spool_id);
        char reply[SPOOLMAN_BODY_MAX], path[SPOOLMAN_PATH_MAX + 32];
        if (batch->jobs[i].grams > 0) {
            char body[64];
            snprintf(body, sizeof(body), "{\"use_weight\":%.3f}", batch->jobs[i].grams);
            snprintf(path, sizeof(path), "%s/api/v1/spool/%s/use", base, batch->jobs[i].spoolman_id);
            if (spoolman_request(host, port, batch->basic_auth, "PUT", path, body, reply, sizeof(reply)) == 200)
                r.sent_grams = batch->jobs[i].grams;
        }
        snprintf(path, sizeof(path), "%s/api/v1/spool/%s", base, batch->jobs[i].spoolman_id);
        if (spoolman_request(host, port, batch->basic_auth, "GET", path, NULL, reply, sizeof(reply)) == 200) {
            const char *filament = strstr(reply, "\"filament\"");
            if (filament) {
                spoolman_field(filament, "\"material\"", r.material, sizeof(r.material));
                spoolman_field(filament, "\"name\"", r.name, sizeof(r.name));
                char color[8] = "";
                if (spoolman_field(filament, "\"color_hex\"", color + 1, sizeof(color) - 1) && color[1]) {
                    color[0] = '#';
                    for (char *c = color + 1; *c; ++c) *c = (char)toupper((unsigned char)*c);
                    snprintf(r.color, sizeof(r.color), "%s", color);
                }
            }
            if (spoolman_double_field(reply, "\"remaining_weight\"", &r.remaining)) r.have_pull = 1;
        }
        if (r.sent_grams > 0 || r.have_pull) {
            pthread_mutex_lock(&spoolman_outbox_mutex);
            if (spoolman_outbox_count < SPOOLS_MAX) spoolman_outbox[spoolman_outbox_count++] = r;
            pthread_mutex_unlock(&spoolman_outbox_mutex);
        }
    }
    pthread_mutex_lock(&spoolman_outbox_mutex);
    spoolman_worker_running = 0;
    pthread_mutex_unlock(&spoolman_outbox_mutex);
    free(batch);
    return NULL;
}

/* Called from spools_tick(). Drains any results the worker has staged, then
 * -- at most once per SPOOLMAN_TICK_MS, and only if no worker is already
 * running -- starts a new one for every spool that is linked (spoolman_id
 * set) and either has unsent usage or is simply due a refresh. */
static void spoolman_tick(long long now) {
    pthread_mutex_lock(&spoolman_outbox_mutex);
    int count = spoolman_outbox_count; spoolman_outbox_count = 0;
    spoolman_result drained[SPOOLS_MAX];
    if (count) memcpy(drained, spoolman_outbox, (size_t)count * sizeof(spoolman_result));
    int running = spoolman_worker_running;
    pthread_mutex_unlock(&spoolman_outbox_mutex);

    for (int i = 0; i < count; ++i) {
        spool_entry *sp = spool_find(drained[i].spool_id);
        if (!sp) continue;
        if (drained[i].sent_grams > 0) {
            sp->spoolman_unsent -= drained[i].sent_grams;
            if (sp->spoolman_unsent < 0) sp->spoolman_unsent = 0;
        }
        if (drained[i].have_pull) {
            if (drained[i].material[0]) snprintf(sp->material, sizeof(sp->material), "%s", drained[i].material);
            if (drained[i].name[0]) snprintf(sp->name, sizeof(sp->name), "%s", drained[i].name);
            if (drained[i].color[0]) snprintf(sp->color, sizeof(sp->color), "%s", drained[i].color);
            sp->remaining = drained[i].remaining;
        }
        spools_touch(0);
    }

    if (running || !spools.spoolman_url[0] || now - spoolman_last_tick_ms < SPOOLMAN_TICK_MS) return;
    spoolman_job_batch *batch = malloc(sizeof(*batch));
    if (!batch) return;
    memset(batch, 0, sizeof(*batch));
    snprintf(batch->url, sizeof(batch->url), "%s", spools.spoolman_url);
    snprintf(batch->basic_auth, sizeof(batch->basic_auth), "%s", spools.spoolman_basic_auth);
    for (int i = 0; i < spools.count && batch->count < SPOOLS_MAX; ++i) {
        spool_entry *sp = &spools.spools[i];
        if (!sp->spoolman_id[0]) continue;
        batch->jobs[batch->count].grams = sp->spoolman_unsent > 0 ? sp->spoolman_unsent : 0;
        snprintf(batch->jobs[batch->count].spool_id, sizeof(batch->jobs[batch->count].spool_id), "%s", sp->id);
        snprintf(batch->jobs[batch->count].spoolman_id, sizeof(batch->jobs[batch->count].spoolman_id), "%s", sp->spoolman_id);
        batch->count++;
    }
    if (!batch->count) { free(batch); spoolman_last_tick_ms = now; return; }
    pthread_t thread;
    pthread_mutex_lock(&spoolman_outbox_mutex);
    if (pthread_create(&thread, NULL, spoolman_worker, batch) == 0) { spoolman_worker_running = 1; pthread_detach(thread); }
    else free(batch);
    pthread_mutex_unlock(&spoolman_outbox_mutex);
    spoolman_last_tick_ms = now;
}

/* ---- HTTP: POST /api/spoolman {url, basic_auth} (empty url turns sync off) ---- */
static void spoolman_config_response(int fd, const char *body, size_t length) {
    spool_form f;
    if (!spool_form_parse(body, length, &f)) { spools_fail(fd, 400, "Invalid request"); return; }
    if (!spools_open(fd)) return;
    const char *url = spool_form_get(&f, "url"), *auth = spool_form_get(&f, "basic_auth");
    if (url && url[0]) {
        char host[SPOOLMAN_HOST_MAX], base[SPOOLMAN_PATH_MAX];
        int port;
        if (strlen(url) > SPOOLMAN_URL_MAX || !spoolman_parse_url(url, host, sizeof(host), &port, base, sizeof(base))) {
            spools_fail(fd, 400, "Invalid URL: use http://host:port, no TLS"); return;
        }
    } else if (url && !url[0]) {
        /* turning sync off: fine, nothing to validate */
    }
    if (auth && strlen(auth) > SPOOLMAN_AUTH_MAX) { spools_fail(fd, 400, "basic_auth is too long"); return; }
    spool_store *before = spools_snapshot(fd);
    if (!before) return;
    if (url) snprintf(spools.spoolman_url, sizeof(spools.spoolman_url), "%s", url);
    if (auth) snprintf(spools.spoolman_basic_auth, sizeof(spools.spoolman_basic_auth), "%s", auth);
    if (spools_commit(fd, before)) spools_reply(fd, 200, "{\"ok\":true}\n");
}
