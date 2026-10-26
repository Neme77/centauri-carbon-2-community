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
 * -- snapshots what needs sending, then spawns one short-lived detached worker
 * thread if none is already in flight. The worker only ever writes into
 * spoolman_outbox under spoolman_outbox_mutex; spoolman_tick() is the only
 * place that drains the outbox back into the shared `spools` struct, so
 * `spools` itself is read or written only from the main thread.
 *
 * Accounting. Spoolman's /use ADDS use_weight to the spool's used weight; it is
 * a delta, not an absolute assignment, so sending the same grams twice charges
 * them twice. Three amounts are therefore kept per linked spool:
 *   spoolman_unsent    grams charged locally and not yet handed to a request;
 *   spoolman_inflight  grams handed to a request whose outcome is not known
 *                      yet (the connection dropped after the request was sent,
 *                      a 5xx, a truncated reply). Never sent again blindly;
 *   (sent)             grams Spoolman confirmed -- they leave both amounts.
 * Before every /use the worker reads Spoolman's used_weight; if the outcome of
 * the request is then unknown it reads it again (a later cycle at the latest)
 * and compares: used_weight grew by the grams -> it was applied; it did not ->
 * the grams go back to spoolman_unsent. If it changed in some other way, or the
 * baseline was lost (restart in the middle of a request), the grams are dropped
 * rather than risk charging them twice; the next pull brings the spool to
 * Spoolman's own figure anyway. Failures that cannot have reached Spoolman
 * (no connection, request not fully sent, a 4xx) simply stay in spoolman_unsent.
 * All three amounts are saved with the library, so a restart loses nothing.
 *
 * A pull sets the local remaining weight to Spoolman's figure minus what
 * Spoolman has not been told about yet (unsent + inflight), so usage charged
 * while a request was in flight is not undone by its reply. Every request is
 * tagged with the settings generation (spoolman_epoch) and the spool's link
 * generation (spoolman_gen); a reply that no longer matches is discarded. */

#define SPOOLMAN_TICK_MS 120000LL     /* same cadence as the local library's own autosave */
#define SPOOLMAN_TIMEOUT_S 5
#define SPOOLMAN_BODY_MAX 8192        /* one spool object */
#define SPOOLMAN_HEADER_MAX 8192
#define SPOOLMAN_HOST_MAX 128
#define SPOOLMAN_PATH_MAX 128
#define SPOOLMAN_CATALOG_MAX 200
#define SPOOLMAN_CATALOG_BODY_MAX 131072   /* the spool list; a longer reply is rejected, never cut */
#define SPOOLMAN_TOLERANCE_G 0.05     /* used_weight comparisons, grams */

/* spoolman_request() results below 100 (otherwise the HTTP status). */
#define SPOOLMAN_E_NOSEND (-1)    /* nothing was delivered (connect or send failed): the server cannot have acted */
#define SPOOLMAN_E_UNKNOWN (-2)   /* the request went out in full but no complete, well-formed reply came back */
#define SPOOLMAN_E_TOOBIG (-3)    /* the reply does not fit the caller's buffer */

enum {
    SPOOLMAN_NONE,       /* no change to the accounting */
    SPOOLMAN_SENT,       /* /use confirmed (200, or used_weight grew by the grams) */
    SPOOLMAN_NOT_SENT,   /* definitely not applied: the grams go back to unsent */
    SPOOLMAN_AMBIGUOUS,  /* outcome unknown: the grams stay in flight, base recorded */
    SPOOLMAN_APPLIED,    /* an earlier ambiguous request turned out to be applied */
    SPOOLMAN_REQUEUE,    /* an earlier ambiguous request turned out not to be applied */
    SPOOLMAN_DROP        /* cannot tell: forget the in-flight grams, never send them again */
};

/* What one Spoolman spool object says (parsed with the library's bounded parsers). */
typedef struct {
    char spoolman_id[SPOOLMAN_ID_MAX + 1];
    char name[SPOOL_TEXT_MAX + 1], material[SPOOL_MATERIAL_MAX + 1], color[8];
    double remaining, used, net, density;
    int have_remaining, have_used;
} spoolman_spool;

typedef struct {
    char spool_id[SPOOL_ID_LEN + 1];        /* the local spool this result is for; empty when is_new */
    char spoolman_id[SPOOLMAN_ID_MAX + 1];  /* the Spoolman spool it was for */
    unsigned epoch, gen;                    /* settings / link generation the request started under */
    int outcome;                            /* SPOOLMAN_* */
    double base;                            /* AMBIGUOUS: used_weight seen just before the request */
    int have_pull;                          /* a fresh read of the Spoolman spool came back */
    int is_new;                             /* a Spoolman spool with no local entry yet -- create one */
    spoolman_spool spool;                   /* the pull, or the spool to import */
} spoolman_result;

static pthread_mutex_t spoolman_outbox_mutex = PTHREAD_MUTEX_INITIALIZER;
static spoolman_result spoolman_outbox[SPOOLS_MAX + SPOOLMAN_CATALOG_MAX];
static int spoolman_outbox_count;
static int spoolman_worker_running;
/* Worker requests check this atomic flag without reading the main-thread store. */
static atomic_int spoolman_paused;
static long long spoolman_last_tick_ms;

/* What the worker needs, copied out of `spools` under the main thread before
 * the worker starts -- the worker never touches `spools` directly. */
typedef struct {
    char url[SPOOLMAN_URL_MAX + 1];
    char basic_auth[SPOOLMAN_AUTH_MAX + 1];
    unsigned epoch;
    int count;
    struct {
        char spool_id[SPOOL_ID_LEN + 1];
        char spoolman_id[SPOOLMAN_ID_MAX + 1];
        unsigned gen;
        double send;       /* grams to hand to /use now (already moved to spoolman_inflight); 0 = pull only */
        int reconcile;     /* an earlier request is still unresolved: settle it, send nothing new */
        double inflight, base;
    } jobs[SPOOLS_MAX];
    int known_count;                                      /* every spoolman_id already linked locally */
    char known_ids[SPOOLS_MAX][SPOOLMAN_ID_MAX + 1];
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

/* Case-insensitive "starts with" for HTTP header names (ASCII only). */
static int spoolman_prefix_ci(const char *text, const char *prefix, size_t n) {
    for (size_t i = 0; i < n; ++i)
        if (tolower((unsigned char)text[i]) != tolower((unsigned char)prefix[i])) return 1;
    return 0;
}

/* One request/response over a fresh connection (Spoolman's own API is small
 * and infrequent here; a persistent connection is not worth the complexity).
 * `body` may be NULL (GET). Returns the HTTP status code (>= 100) once a complete reply was read -- its
 * headers parsed, and its body read in full according to Content-Length or chunked framing, or to the end of
 * the connection when neither is given -- with the body NUL-terminated in `reply`. Otherwise one of the
 * SPOOLMAN_E_* codes: a caller that sent a write must treat SPOOLMAN_E_UNKNOWN and SPOOLMAN_E_TOOBIG as
 * "may have been applied". Nothing is ever cut: a reply that does not fit is TOOBIG. */
static int spoolman_request(const char *host, int port, const char *basic_auth, const char *method,
                            const char *path, const char *body, char *reply, size_t reply_cap) {
    if (reply_cap) reply[0] = 0;
    if (atomic_load(&spoolman_paused)) return SPOOLMAN_E_NOSEND;
    int fd = spoolman_connect(host, port);
    if (fd < 0) return SPOOLMAN_E_NOSEND;
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
    if (n <= 0 || (size_t)n >= sizeof(request)) { close(fd); return SPOOLMAN_E_NOSEND; }
    const char *p = request; size_t left = (size_t)n;
    while (left) {
        if (atomic_load(&spoolman_paused)) { close(fd); return SPOOLMAN_E_NOSEND; }
        ssize_t sent = send(fd, p, left, MSG_NOSIGNAL);
        /* Part of a request is never acted on: the server waits for the rest of the declared body. */
        if (sent <= 0) { close(fd); return SPOOLMAN_E_NOSEND; }
        p += sent; left -= (size_t)sent;
    }
    /* From here on the server may have acted. Room for the headers plus the body, with slack for chunk framing. */
    size_t raw_cap = SPOOLMAN_HEADER_MAX + reply_cap * 2 + 64, used = 0;
    char *raw = malloc(raw_cap);
    if (!raw) { close(fd); return SPOOLMAN_E_UNKNOWN; }
    int eof = 0;
    while (used < raw_cap) {
        ssize_t got = recv(fd, raw + used, raw_cap - used, 0);
        if (got < 0) break;
        if (got == 0) { eof = 1; break; }
        used += (size_t)got;
    }
    close(fd);
    int result = SPOOLMAN_E_UNKNOWN;
    size_t head = 0;
    for (size_t i = 0; i + 4 <= used && i < SPOOLMAN_HEADER_MAX; ++i)
        if (!memcmp(raw + i, "\r\n\r\n", 4)) { head = i + 4; break; }
    int status = 0;
    if (head && used >= 12 && !memcmp(raw, "HTTP/1.", 7) && isdigit((unsigned char)raw[9]) &&
        isdigit((unsigned char)raw[10]) && isdigit((unsigned char)raw[11])) status = atoi(raw + 9);
    if (status < 100 || status > 599) goto done;
    {
        long content_length = -1;
        int chunked = 0;
        for (size_t line = strcspn(raw, "\n") + 1; line < head;) {
            size_t len = strcspn(raw + line, "\r\n");
            if (len >= 15 && !spoolman_prefix_ci(raw + line, "content-length:", 15)) {
                const char *v = raw + line + 15;
                while (*v == ' ' || *v == '\t') ++v;
                char *endp; errno = 0;
                long value = strtol(v, &endp, 10);
                if (endp == v || errno || value < 0) goto done;
                content_length = value;
            } else if (len >= 18 && !spoolman_prefix_ci(raw + line, "transfer-encoding:", 18)) {
                char value[64]; size_t vl = len - 18 < sizeof(value) - 1 ? len - 18 : sizeof(value) - 1;
                memcpy(value, raw + line + 18, vl); value[vl] = 0;
                for (char *c = value; *c; ++c) *c = (char)tolower((unsigned char)*c);
                chunked = strstr(value, "chunked") != NULL;
            }
            line += len; while (line < head && (raw[line] == '\r' || raw[line] == '\n')) ++line;
        }
        const char *data = raw + head;
        size_t have = used - head, out = 0;
        if (status < 200 || status == 204 || status == 304) {
            /* no body */
        } else if (chunked) {
            size_t at = 0; int finished = 0;
            while (at < have) {
                char *endp; errno = 0;
                unsigned long size = strtoul(data + at, &endp, 16);
                if (endp == data + at || errno || size > SPOOLMAN_CATALOG_BODY_MAX * 2UL) goto done;
                const char *eol = memchr(endp, '\n', have - (size_t)(endp - data));
                if (!eol) goto done;
                at = (size_t)(eol - data) + 1;
                if (!size) { finished = 1; break; }
                if (at + size + 2 > have) goto done;
                if (out + size + 1 > reply_cap) { result = SPOOLMAN_E_TOOBIG; goto done; }
                memcpy(reply + out, data + at, size); out += size;
                at += size;
                if (data[at] == '\r') ++at;
                if (at >= have || data[at] != '\n') goto done;
                ++at;
            }
            if (!finished) goto done;
        } else if (content_length >= 0) {
            if ((size_t)content_length + 1 > reply_cap) { result = SPOOLMAN_E_TOOBIG; goto done; }
            if (have < (size_t)content_length) goto done;      /* cut short */
            memcpy(reply, data, (size_t)content_length); out = (size_t)content_length;
        } else {
            /* Delimited by the end of the connection: only a clean end counts. */
            if (!eof) goto done;
            if (have + 1 > reply_cap) { result = SPOOLMAN_E_TOOBIG; goto done; }
            memcpy(reply, data, have); out = have;
        }
        reply[out] = 0;
        result = status;
    }
done:
    free(raw);
    return result;
}

/* A flat Spoolman spool object -> the fields we use, each through the library's bounded parsers: numbers must
 * be finite and in range, texts must pass spool_text_ok. A field that fails is simply absent, never trusted. */
static int spoolman_text(const char *obj, const char *end, const char *key, char *out, size_t cap, size_t max, int empty_ok) {
    if (spools_raw_member(obj, end, key, out, cap) && spool_text_ok(out, max, empty_ok)) return 1;
    out[0] = 0;
    return 0;
}

static int spoolman_parse_spool(const char *obj, const char *end, spoolman_spool *e) {
    memset(e, 0, sizeof(*e));
    double id;
    if (!plates_member_double(obj, end, "id", &id) || id < 1 || id > 999999999999999.0 || id != floor(id)) return 0;
    snprintf(e->spoolman_id, sizeof(e->spoolman_id), "%.0f", id);
    e->have_remaining = plates_member_double(obj, end, "remaining_weight", &e->remaining) &&
                        e->remaining >= -1e6 && e->remaining <= 25000;
    e->have_used = plates_member_double(obj, end, "used_weight", &e->used) && e->used >= 0 && e->used <= 1e7;
    const char *fe, *filament = json_member_object(obj, end, "filament", '{', &fe);
    if (filament) {
        spoolman_text(filament, fe, "material", e->material, sizeof(e->material), SPOOL_MATERIAL_MAX, 0);
        spoolman_text(filament, fe, "name", e->name, sizeof(e->name), SPOOL_TEXT_MAX, 1);
        const char *hex; int length;
        if (json_member_raw_string(filament, fe, "color_hex", &hex, &length) && length == 6) {
            char color[8] = {'#'};
            int ok = 1;
            for (int i = 0; i < 6; ++i) {
                if (!isxdigit((unsigned char)hex[i])) ok = 0;
                color[i + 1] = (char)toupper((unsigned char)hex[i]);
            }
            color[7] = 0;
            if (ok) memcpy(e->color, color, sizeof(e->color));
        }
        if (!plates_member_double(filament, fe, "density", &e->density) || e->density < 0.5 || e->density > 3)
            e->density = 0;
        if (!plates_member_double(filament, fe, "weight", &e->net) || e->net < 1 || e->net > 20000) e->net = 0;
    }
    return 1;
}

/* A [...] list of spool objects. Returns how many were read, or -1 when the text is not a well-formed array. */
static int spoolman_parse_list(const char *json, spoolman_spool *out, int max) {
    const char *end = json + strlen(json), *list = json_skip_space(json, end);
    if (list >= end || *list != '[') return -1;
    const char *list_end = json_container_end(list, end);
    if (!list_end) return -1;
    int count = 0;
    for (const char *item = json_next_element(list, list_end); item;) {
        const char *item_end = json_container_end(item, list_end);
        if (!item_end) return -1;
        if (count < max && *item == '{' && spoolman_parse_spool(item, item_end, &out[count])) count++;
        item = json_next_element(item_end, list_end);
    }
    return count;
}

static int spoolman_parse_one(const char *json, spoolman_spool *out) {
    const char *end = json + strlen(json), *obj = json_skip_space(json, end);
    if (obj >= end || *obj != '{') return 0;
    const char *obj_end = json_container_end(obj, end);
    return obj_end && spoolman_parse_spool(obj, obj_end, out);
}

static pthread_mutex_t spoolman_catalog_mutex = PTHREAD_MUTEX_INITIALIZER;
static spoolman_spool spoolman_catalog[SPOOLMAN_CATALOG_MAX];
static int spoolman_catalog_count;
static int spoolman_catalog_loading;
static char spoolman_catalog_error[128] = "Not fetched yet";

static const char *spoolman_failure_text(int code) {
    return code == SPOOLMAN_E_TOOBIG ? "Spoolman's spool list is too large to read" :
           code == SPOOLMAN_E_UNKNOWN ? "Spoolman sent an incomplete reply" :
           code > 0 ? "Spoolman refused the request" : "Could not reach Spoolman";
}

static void spoolman_stage(const spoolman_result *r) {
    pthread_mutex_lock(&spoolman_outbox_mutex);
    if (spoolman_outbox_count < (int)(sizeof(spoolman_outbox) / sizeof(spoolman_outbox[0])))
        spoolman_outbox[spoolman_outbox_count++] = *r;
    pthread_mutex_unlock(&spoolman_outbox_mutex);
}

/* GET /api/v1/spool/{id}. 1 = read and parsed. */
static int spoolman_get_spool(const char *host, int port, const char *base, const char *auth, const char *id,
                              spoolman_spool *out) {
    char reply[SPOOLMAN_BODY_MAX], path[SPOOLMAN_PATH_MAX + 64];
    snprintf(path, sizeof(path), "%s/api/v1/spool/%s", base, id);
    return spoolman_request(host, port, auth, "GET", path, NULL, reply, sizeof(reply)) == 200 &&
           spoolman_parse_one(reply, out) && !strcmp(out->spoolman_id, id);
}

/* Runs off the main thread. Only touches spoolman_outbox (its own mutex) and
 * the stack-local copy of what to send -- never `spools`. */
static void *spoolman_worker(void *arg) {
    spoolman_job_batch *batch = arg;
    char host[SPOOLMAN_HOST_MAX], base[SPOOLMAN_PATH_MAX];
    int port;
    if (!spoolman_parse_url(batch->url, host, sizeof(host), &port, base, sizeof(base))) {
        pthread_mutex_lock(&spoolman_outbox_mutex); spoolman_worker_running = 0; pthread_mutex_unlock(&spoolman_outbox_mutex);
        free(batch);
        return NULL;
    }
    for (int i = 0; i < batch->count; ++i) {
        spoolman_result r; memset(&r, 0, sizeof(r));
        snprintf(r.spool_id, sizeof(r.spool_id), "%s", batch->jobs[i].spool_id);
        snprintf(r.spoolman_id, sizeof(r.spoolman_id), "%s", batch->jobs[i].spoolman_id);
        r.epoch = batch->epoch; r.gen = batch->jobs[i].gen;
        const char *sid = batch->jobs[i].spoolman_id;
        spoolman_spool before, after;
        int have_before = spoolman_get_spool(host, port, base, batch->basic_auth, sid, &before);
        const spoolman_spool *pull = have_before ? &before : NULL;
        if (batch->jobs[i].reconcile) {
            /* An earlier /use has an unknown outcome: read Spoolman's used weight and decide, send nothing new. */
            if (have_before && before.have_used) {
                double base_used = batch->jobs[i].base, grams = batch->jobs[i].inflight, tol = SPOOLMAN_TOLERANCE_G;
                if (base_used < 0) r.outcome = SPOOLMAN_DROP;
                else if (before.used >= base_used + grams - tol) r.outcome = SPOOLMAN_APPLIED;
                else if (before.used <= base_used + tol) r.outcome = SPOOLMAN_REQUEUE;
                else r.outcome = SPOOLMAN_DROP;
            }
        } else if (batch->jobs[i].send > 0) {
            double grams = batch->jobs[i].send;
            if (!have_before || !before.have_used) {
                r.outcome = SPOOLMAN_NOT_SENT;       /* could not even read the baseline: nothing was sent */
            } else {
                char body[64], path[SPOOLMAN_PATH_MAX + 64], reply[SPOOLMAN_BODY_MAX];
                snprintf(body, sizeof(body), "{\"use_weight\":%.3f}", grams);
                snprintf(path, sizeof(path), "%s/api/v1/spool/%s/use", base, sid);
                int status = spoolman_request(host, port, batch->basic_auth, "PUT", path, body, reply, sizeof(reply));
                if (status == 200) r.outcome = SPOOLMAN_SENT;
                else if (status == SPOOLMAN_E_NOSEND || (status >= 300 && status < 500)) r.outcome = SPOOLMAN_NOT_SENT;
                else r.outcome = SPOOLMAN_AMBIGUOUS;      /* 5xx, cut-off or unreadable reply: it may have been applied */
                if (r.outcome != SPOOLMAN_NOT_SENT) {
                    int have_after = spoolman_get_spool(host, port, base, batch->basic_auth, sid, &after);
                    pull = have_after ? &after : NULL;
                    r.base = before.used;
                    /* Applied is certain when Spoolman's used weight has grown by the grams. Not applied is not
                     * certain yet -- the request may still be running -- so that is left for a later cycle. */
                    if (r.outcome == SPOOLMAN_AMBIGUOUS && have_after && after.have_used &&
                        after.used >= before.used + grams - SPOOLMAN_TOLERANCE_G) r.outcome = SPOOLMAN_SENT;
                }
            }
        }
        if (pull && pull->have_remaining) { r.have_pull = 1; r.spool = *pull; }
        if (r.outcome != SPOOLMAN_NONE || r.have_pull) spoolman_stage(&r);
    }
    /* Auto-import: any Spoolman spool with no local entry yet gets one, so
     * connecting Spoolman is enough -- nothing to create by hand. */
    {
        char *list_body = malloc(SPOOLMAN_CATALOG_BODY_MAX);
        spoolman_spool *entries = malloc(sizeof(*entries) * SPOOLMAN_CATALOG_MAX);
        if (list_body && entries) {
            char list_path[SPOOLMAN_PATH_MAX + 32];
            snprintf(list_path, sizeof(list_path), "%s/api/v1/spool", base);
            if (spoolman_request(host, port, batch->basic_auth, "GET", list_path, NULL, list_body, SPOOLMAN_CATALOG_BODY_MAX) == 200) {
                int n = spoolman_parse_list(list_body, entries, SPOOLMAN_CATALOG_MAX);
                for (int i = 0; i < n; ++i) {
                    int known = 0;
                    for (int k = 0; k < batch->known_count; ++k)
                        if (!strcmp(batch->known_ids[k], entries[i].spoolman_id)) { known = 1; break; }
                    if (known || !entries[i].have_remaining) continue;
                    spoolman_result r; memset(&r, 0, sizeof(r));
                    r.is_new = 1; r.epoch = batch->epoch; r.have_pull = 1;
                    r.spool = entries[i];
                    snprintf(r.spoolman_id, sizeof(r.spoolman_id), "%s", entries[i].spoolman_id);
                    spoolman_stage(&r);
                }
            }
        }
        free(list_body); free(entries);
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
    atomic_store(&spoolman_paused, !spools.spoolman_enabled);
    pthread_mutex_lock(&spoolman_outbox_mutex);
    int count = spoolman_outbox_count; spoolman_outbox_count = 0;
    spoolman_result *drained = count ? malloc((size_t)count * sizeof(*drained)) : NULL;
    if (drained) memcpy(drained, spoolman_outbox, (size_t)count * sizeof(*drained));
    else count = 0;
    int running = spoolman_worker_running;
    pthread_mutex_unlock(&spoolman_outbox_mutex);

    for (int i = 0; i < count; ++i) {
        const spoolman_result *r = &drained[i];
        if (r->epoch != spoolman_epoch) continue;        /* the server settings changed since the request began */
        if (r->is_new) {
            if (!spools.spoolman_enabled) continue;
            if (spool_find_by_spoolman_id(r->spoolman_id) || spools.count >= SPOOLS_MAX) continue;
            /* Built off to the side, then copied in -- spools_new_id() checks the array
             * for a free id, so the slot must not already "contain itself" while empty. */
            spool_entry s; memset(&s, 0, sizeof(s));
            spools_new_id(s.id);
            snprintf(s.spoolman_id, sizeof(s.spoolman_id), "%s", r->spoolman_id);
            snprintf(s.material, sizeof(s.material), "%s", r->spool.material[0] ? r->spool.material : "PLA");
            snprintf(s.name, sizeof(s.name), "%s", r->spool.name);
            snprintf(s.color, sizeof(s.color), "%s", r->spool.color[0] ? r->spool.color : "#808080");
            s.diameter = 1.75;
            s.density = r->spool.density > 0 ? r->spool.density : SPOOL_DENSITY;
            s.net = r->spool.net > 0 ? r->spool.net : (r->spool.remaining >= 1 ? r->spool.remaining : 1000);
            s.remaining = r->spool.remaining;
            s.spoolman_base = -1;
            s.created = (long long)spools_wall(NULL);
            spools.spools[spools.count++] = s;
            spools_log('n', s.id, -1, s.remaining, 0, "", "");
            spools_touch(1);
            continue;
        }
        spool_entry *sp = spool_find(r->spool_id);
        /* Unlinked, relinked to another Spoolman spool or deleted meanwhile: this answer is for something else. */
        if (!sp || sp->spoolman_gen != r->gen || strcmp(sp->spoolman_id, r->spoolman_id)) continue;
        switch (r->outcome) {
        case SPOOLMAN_SENT: case SPOOLMAN_APPLIED: case SPOOLMAN_DROP:
            sp->spoolman_inflight = 0; sp->spoolman_base = -1; break;
        case SPOOLMAN_NOT_SENT: case SPOOLMAN_REQUEUE:
            sp->spoolman_unsent += sp->spoolman_inflight; sp->spoolman_inflight = 0; sp->spoolman_base = -1; break;
        case SPOOLMAN_AMBIGUOUS:
            sp->spoolman_base = r->base; break;
        default: break;
        }
        if (r->have_pull && spools.spoolman_enabled) {
            const spoolman_spool *p = &r->spool;
            if (p->material[0]) snprintf(sp->material, sizeof(sp->material), "%s", p->material);
            if (p->name[0]) snprintf(sp->name, sizeof(sp->name), "%s", p->name);
            if (p->color[0]) snprintf(sp->color, sizeof(sp->color), "%s", p->color);
            /* Spoolman's figure, less what it has not been told about: usage charged while the request was out,
             * and grams still in flight. Never the bare reply, which would undo them. */
            sp->remaining = p->remaining - sp->spoolman_unsent - sp->spoolman_inflight;
            if (sp->remaining < -1e6) sp->remaining = -1e6;
        }
        spools_touch(r->outcome != SPOOLMAN_NONE);
    }
    free(drained);

    if (running || !spools.spoolman_enabled || !spools.spoolman_url[0] || now - spoolman_last_tick_ms < SPOOLMAN_TICK_MS) return;
    spoolman_job_batch *batch = malloc(sizeof(*batch));
    if (!batch) return;
    memset(batch, 0, sizeof(*batch));
    snprintf(batch->url, sizeof(batch->url), "%s", spools.spoolman_url);
    snprintf(batch->basic_auth, sizeof(batch->basic_auth), "%s", spools.spoolman_basic_auth);
    batch->epoch = spoolman_epoch;
    int moved = 0;
    for (int i = 0; i < spools.count && batch->count < SPOOLS_MAX; ++i) {
        spool_entry *sp = &spools.spools[i];
        if (!sp->spoolman_id[0]) continue;
        if (batch->known_count < SPOOLS_MAX)
            snprintf(batch->known_ids[batch->known_count++], SPOOLMAN_ID_MAX + 1, "%s", sp->spoolman_id);
        int job = batch->count;
        if (sp->spoolman_inflight > 0) {
            batch->jobs[job].reconcile = 1;
            batch->jobs[job].inflight = sp->spoolman_inflight;
            batch->jobs[job].base = sp->spoolman_base;
        } else if (sp->spoolman_unsent > 0) {
            /* Written down before it is sent: if CC2 stops in the middle, the grams are in flight, not unsent,
             * and are never sent twice. */
            sp->spoolman_inflight = sp->spoolman_unsent;
            sp->spoolman_unsent = 0;
            sp->spoolman_base = -1;
            batch->jobs[job].send = sp->spoolman_inflight;
            moved = 1;
        }
        batch->jobs[job].gen = sp->spoolman_gen;
        snprintf(batch->jobs[job].spool_id, sizeof(batch->jobs[job].spool_id), "%s", sp->id);
        snprintf(batch->jobs[job].spoolman_id, sizeof(batch->jobs[job].spoolman_id), "%s", sp->spoolman_id);
        batch->count++;
    }
    if (moved) { spools_touch(1); (void)spools_save(); }
    pthread_t thread;
    pthread_mutex_lock(&spoolman_outbox_mutex);
    if (pthread_create(&thread, NULL, spoolman_worker, batch) == 0) { spoolman_worker_running = 1; pthread_detach(thread); }
    else {
        free(batch);
        /* Nothing went out: put the grams back where they were. */
        for (int i = 0; i < spools.count; ++i) {
            spool_entry *sp = &spools.spools[i];
            if (sp->spoolman_id[0] && sp->spoolman_inflight > 0 && sp->spoolman_base < 0) {
                sp->spoolman_unsent += sp->spoolman_inflight; sp->spoolman_inflight = 0;
            }
        }
    }
    pthread_mutex_unlock(&spoolman_outbox_mutex);
    spoolman_last_tick_ms = now;
}

/* ---- HTTP: POST /api/spoolman {url, basic_auth} (empty url turns sync off) ---- */
static void spoolman_config_response(int fd, const char *body, size_t length) {
    spool_form f;
    if (!spool_form_parse(body, length, &f)) { spools_fail(fd, 400, "Invalid request"); return; }
    if (!spools_open(fd)) return;
    const char *url = spool_form_get(&f, "url"), *auth = spool_form_get(&f, "basic_auth");
    const char *enabled = spool_form_get(&f, "enabled");
    if (enabled && strcmp(enabled, "0") && strcmp(enabled, "1")) { spools_fail(fd, 400, "Invalid enabled flag"); return; }
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
    int changed = (url && strcmp(url, spools.spoolman_url)) || (auth && strcmp(auth, spools.spoolman_basic_auth));
    int toggled = enabled && spools.spoolman_enabled != (enabled[0] == '1');
    if (enabled) spools.spoolman_enabled = enabled[0] == '1';
    if (url) snprintf(spools.spoolman_url, sizeof(spools.spoolman_url), "%s", url);
    if (auth) snprintf(spools.spoolman_basic_auth, sizeof(spools.spoolman_basic_auth), "%s", auth);
    if (changed) {
        /* A different server (or login): what was pending was for the old one, and requests still running
         * for it are discarded when they answer. */
        spoolman_epoch++;
        for (int i = 0; i < spools.count; ++i) {
            spools.spools[i].spoolman_unsent = spools.spools[i].spoolman_inflight = 0;
            spools.spools[i].spoolman_base = -1;
        }
    }
    if (spools_commit(fd, before)) {
        atomic_store(&spoolman_paused, !spools.spoolman_enabled);
        if (toggled && spools.spoolman_enabled) spoolman_last_tick_ms = 0;
        spools_reply(fd, 200, "{\"ok\":true}\n");
    }
}

/* ---- Spoolman catalog: the browser's own picker, instead of typing an id blind ------------- */

static void *spoolman_catalog_worker(void *arg) {
    char url[SPOOLMAN_URL_MAX + 1], auth[SPOOLMAN_AUTH_MAX + 1];
    memcpy(url, arg, sizeof(url)); memcpy(auth, (char *)arg + sizeof(url), sizeof(auth));
    free(arg);
    char host[SPOOLMAN_HOST_MAX], base[SPOOLMAN_PATH_MAX], path[SPOOLMAN_PATH_MAX + 64]; int port;
    char *body = malloc(SPOOLMAN_CATALOG_BODY_MAX);
    const char *error = NULL;
    int count = 0;
    spoolman_spool *parsed = malloc(sizeof(*parsed) * SPOOLMAN_CATALOG_MAX);
    if (!body || !parsed) error = "Out of memory";
    else if (!spoolman_parse_url(url, host, sizeof(host), &port, base, sizeof(base))) error = "Invalid URL";
    else {
        snprintf(path, sizeof(path), "%s/api/v1/spool", base);
        int status = spoolman_request(host, port, auth, "GET", path, NULL, body, SPOOLMAN_CATALOG_BODY_MAX);
        if (status != 200) error = spoolman_failure_text(status);
        else if ((count = spoolman_parse_list(body, parsed, SPOOLMAN_CATALOG_MAX)) < 0) error = "Spoolman sent an unreadable spool list";
    }
    free(body);
    pthread_mutex_lock(&spoolman_catalog_mutex);
    if (error) snprintf(spoolman_catalog_error, sizeof(spoolman_catalog_error), "%s", error);
    else { spoolman_catalog_error[0] = 0; spoolman_catalog_count = count; memcpy(spoolman_catalog, parsed, (size_t)count * sizeof(parsed[0])); }
    spoolman_catalog_loading = 0;
    pthread_mutex_unlock(&spoolman_catalog_mutex);
    free(parsed);
    return NULL;
}

/* POST /api/spoolman/catalog/refresh -- starts (or no-ops if one is already running) a fetch. */
static void spoolman_catalog_refresh_response(int fd) {
    if (!spools_open(fd)) return;
    if (!spools.spoolman_enabled) { spools_fail(fd, 409, "Spoolman integration is disabled"); return; }
    if (!spools.spoolman_url[0]) { spools_fail(fd, 400, "Set the Spoolman server URL first"); return; }
    pthread_mutex_lock(&spoolman_catalog_mutex);
    int already = spoolman_catalog_loading;
    if (!already) spoolman_catalog_loading = 1;
    pthread_mutex_unlock(&spoolman_catalog_mutex);
    if (!already) {
        char *arg = malloc(sizeof(spools.spoolman_url) + sizeof(spools.spoolman_basic_auth));
        if (arg) {
            memcpy(arg, spools.spoolman_url, sizeof(spools.spoolman_url));
            memcpy(arg + sizeof(spools.spoolman_url), spools.spoolman_basic_auth, sizeof(spools.spoolman_basic_auth));
            pthread_t thread;
            if (pthread_create(&thread, NULL, spoolman_catalog_worker, arg) == 0) pthread_detach(thread);
            else { free(arg); pthread_mutex_lock(&spoolman_catalog_mutex); spoolman_catalog_loading = 0; pthread_mutex_unlock(&spoolman_catalog_mutex); }
        }
    }
    spools_reply(fd, 202, "{\"ok\":true}\n");
}

/* GET /api/spoolman/catalog -- the cached result of the last refresh. */
static void spoolman_catalog_response(int fd) {
    if (!spools.spoolman_enabled) { spools_fail(fd, 409, "Spoolman integration is disabled"); return; }
    pthread_mutex_lock(&spoolman_catalog_mutex);
    size_t cap = 16384;
    json_builder b = {malloc(cap), 0, cap, 0};
    if (!b.data) { pthread_mutex_unlock(&spoolman_catalog_mutex); spools_fail(fd, 500, "Out of memory"); return; }
    json_builder_printf(&b, "{\"loading\":%s,\"error\":", spoolman_catalog_loading ? "true" : "false");
    json_builder_string(&b, spoolman_catalog_error);
    json_builder_printf(&b, ",\"spools\":[");
    for (int i = 0; i < spoolman_catalog_count; ++i) {
        const spoolman_spool *e = &spoolman_catalog[i];
        json_builder_printf(&b, "%s{\"id\":\"%s\",\"name\":", i ? "," : "", e->spoolman_id);
        json_builder_string(&b, e->name);
        json_builder_printf(&b, ",\"material\":");
        json_builder_string(&b, e->material);
        json_builder_printf(&b, ",\"color\":\"%s\",\"remaining\":%.1f}", e->color, e->have_remaining ? e->remaining : 0.0);
    }
    json_builder_printf(&b, "]}\n");
    pthread_mutex_unlock(&spoolman_catalog_mutex);
    if (b.failed) spools_fail(fd, 500, "Catalog response is too large");
    else respond(fd, 200, "OK", "application/json; charset=utf-8", b.data, b.length);
    free(b.data);
}
