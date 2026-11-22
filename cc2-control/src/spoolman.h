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
#define SPOOLMAN_CATALOG_BODY_MAX 131072   /* the largest chunk of a chunked reply that is believed */
#define SPOOLMAN_TOLERANCE_G 0.05     /* used_weight comparisons, grams */
/* The auto-import reads the list in pages (GET /api/v1/spool?sort=id:asc&limit=..&offset=..): Spoolman's list has
 * no order unless one is asked for, and without `limit` it answers with everything at once. */
#define SPOOLMAN_PAGE_SIZE 20         /* spools asked for per page; halved when a page does not fit the buffer */
#define SPOOLMAN_PAGE_PARSE 64        /* spools read from one page, even from a server that ignores `limit` */
#define SPOOLMAN_PAGE_BODY_MAX 65536  /* one page; a longer reply is rejected, never cut */
#define SPOOLMAN_PAGES_MAX 64         /* requests per list pass: at most 1280 spools are looked at */
#define SPOOLMAN_REQUEST_MAX_MS 15000LL  /* whole request, connect to last byte (the 5 s timeout alone restarts per read) */
#define SPOOLMAN_CYCLE_MAX_MS 90000LL    /* whole sync cycle; what is left over is picked up by the next one */
#define SPOOLMAN_ORPHAN_MAX 8         /* linked spools reported as gone from Spoolman */

/* spoolman_request() results below 100 (otherwise the HTTP status). */
#define SPOOLMAN_E_NOSEND (-1)    /* nothing was delivered (connect or send failed): the server cannot have acted */
#define SPOOLMAN_E_UNKNOWN (-2)   /* the request went out in full but no complete, well-formed reply came back */
#define SPOOLMAN_E_TOOBIG (-3)    /* the reply does not fit the caller's buffer */
#define SPOOLMAN_E_TIMEOUT (-4)   /* the request went out in full but the reply did not arrive in time: as UNKNOWN */

enum {
    SPOOLMAN_NONE,       /* no change to the accounting */
    SPOOLMAN_SENT,       /* /use confirmed (200, or used_weight grew by the grams) */
    SPOOLMAN_NOT_SENT,   /* definitely not applied: the grams go back to unsent */
    SPOOLMAN_AMBIGUOUS,  /* outcome unknown: the grams stay in flight, base recorded */
    SPOOLMAN_APPLIED,    /* an earlier ambiguous request turned out to be applied */
    SPOOLMAN_REQUEUE,    /* an earlier ambiguous request turned out not to be applied */
    SPOOLMAN_DROP,       /* cannot tell: the in-flight grams are never sent again; they go on the list as "unsure" */
    SPOOLMAN_MISSING     /* Spoolman answers 404 for the spool: the grams go on the list as "not found" */
};

/* What one Spoolman spool object says (parsed with the library's bounded parsers). */
typedef struct {
    char spoolman_id[SPOOLMAN_ID_MAX + 1];
    char name[SPOOL_TEXT_MAX + 1], brand[SPOOL_TEXT_MAX + 1], material[SPOOL_MATERIAL_MAX + 1], color[8];
    double remaining, used, net, density, tare;
    int have_remaining, have_used;
    int archived;       /* Spoolman's own flag; the list never contains archived spools, a single spool can be one */
} spoolman_spool;

typedef struct {
    char spool_id[SPOOL_ID_LEN + 1];        /* the local spool this result is for; empty when is_new */
    char spoolman_id[SPOOLMAN_ID_MAX + 1];  /* the Spoolman spool it was for */
    unsigned epoch, gen;                    /* settings / link generation the request started under */
    int outcome;                            /* SPOOLMAN_* */
    double base;                            /* AMBIGUOUS: used_weight seen just before the request */
    int have_pull;                          /* a fresh read of the Spoolman spool came back (its weight may be missing) */
    int is_new;                             /* a Spoolman spool with no local entry yet -- create one */
    int missing;                            /* Spoolman answered 404 for the spool */
    int settle;                             /* the job settled an earlier request (its grams were in flight already) */
    spoolman_spool spool;                   /* the pull, or the spool to import */
} spoolman_result;

static pthread_mutex_t spoolman_outbox_mutex = PTHREAD_MUTEX_INITIALIZER;
static spoolman_result spoolman_outbox[SPOOLS_MAX * 2];   /* one result per linked spool, one per spool to import */
static int spoolman_outbox_count;
static int spoolman_worker_running;
/* Worker requests check this atomic flag without reading the main-thread store. */
static atomic_int spoolman_paused;
static long long spoolman_last_tick_ms;

/* What the worker needs, copied out of `spools` under the main thread before
 * the worker starts -- the worker never touches `spools` directly. */
typedef struct {
    char spool_id[SPOOL_ID_LEN + 1];
    char spoolman_id[SPOOLMAN_ID_MAX + 1];
    unsigned gen;
    double send;       /* grams to hand to /use now (already moved to spoolman_inflight); 0 = pull only */
    int reconcile;     /* an earlier request is still unresolved: settle it, send nothing new */
    double inflight, base;
} spoolman_job;

typedef struct {
    char url[SPOOLMAN_URL_MAX + 1];
    char basic_auth[SPOOLMAN_AUTH_MAX + 1];
    unsigned epoch;
    int count;
    spoolman_job jobs[SPOOLS_MAX];
    int known_count;                                      /* every spoolman_id already linked locally */
    int free_slots;                                       /* room left in the local library for new spools */
    char known_ids[SPOOLS_MAX][SPOOLMAN_ID_MAX + 1];
} spoolman_job_batch;

/* http://IPv4[:port][/base] only: no TLS stack is linked in, and no name is ever resolved. */
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
    /* Only an IPv4 address is ever connected to (see spoolman_connect): a host name would be accepted here and then
     * fail every cycle without a word, so it is refused up front. */
    struct in_addr probe;
    if (inet_pton(AF_INET, host, &probe) != 1) return 0;
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
 * SPOOLMAN_E_* codes: a caller that sent a write must treat SPOOLMAN_E_UNKNOWN, SPOOLMAN_E_TIMEOUT and
 * SPOOLMAN_E_TOOBIG as "may have been applied". Nothing is ever cut: a reply that does not fit is TOOBIG.
 * The 5 s socket timeout restarts with every byte a slow server sends, so the whole reply also has to arrive
 * within spoolman_request_max_ms. `total` (may be NULL) receives the X-Total-Count header, or -1. */
static long long spoolman_request_max_ms = SPOOLMAN_REQUEST_MAX_MS;

static int spoolman_request_ex(const char *host, int port, const char *basic_auth, const char *method,
                               const char *path, const char *body, char *reply, size_t reply_cap, long *total) {
    if (reply_cap) reply[0] = 0;
    if (total) *total = -1;
    if (atomic_load(&spoolman_paused)) return SPOOLMAN_E_NOSEND;
    const long long give_up = monotonic_ms() + spoolman_request_max_ms;
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
        if (atomic_load(&spoolman_paused) || monotonic_ms() >= give_up) { close(fd); return SPOOLMAN_E_NOSEND; }
        ssize_t sent = send(fd, p, left, MSG_NOSIGNAL);
        /* Part of a request is never acted on: the server waits for the rest of the declared body. */
        if (sent <= 0) { close(fd); return SPOOLMAN_E_NOSEND; }
        p += sent; left -= (size_t)sent;
    }
    /* From here on the server may have acted. Room for the headers plus the body, with slack for chunk framing. */
    size_t raw_cap = SPOOLMAN_HEADER_MAX + reply_cap * 2 + 64, used = 0;
    char *raw = malloc(raw_cap);
    if (!raw) { close(fd); return SPOOLMAN_E_UNKNOWN; }
    int eof = 0, timed_out = 0;
    while (used < raw_cap) {
        long long remaining_ms = give_up - monotonic_ms();
        if (remaining_ms <= 0) { timed_out = 1; break; }
        long long slice = remaining_ms < SPOOLMAN_TIMEOUT_S * 1000LL ? remaining_ms : SPOOLMAN_TIMEOUT_S * 1000LL;
        struct timeval tv = {(time_t)(slice / 1000), (suseconds_t)(slice % 1000 * 1000)};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        ssize_t got = recv(fd, raw + used, raw_cap - used, 0);
        if (got < 0) { timed_out = errno == EAGAIN || errno == EWOULDBLOCK; break; }
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
            } else if (len >= 14 && !spoolman_prefix_ci(raw + line, "x-total-count:", 14)) {
                if (total) {
                    const char *v = raw + line + 14;
                    while (*v == ' ' || *v == '\t') ++v;
                    char *endp; errno = 0;
                    long value = strtol(v, &endp, 10);
                    if (endp != v && !errno && value >= 0) *total = value;
                }
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
    if (result == SPOOLMAN_E_UNKNOWN && timed_out) result = SPOOLMAN_E_TIMEOUT;
    return result;
}

static int spoolman_request(const char *host, int port, const char *basic_auth, const char *method,
                            const char *path, const char *body, char *reply, size_t reply_cap) {
    return spoolman_request_ex(host, port, basic_auth, method, path, body, reply, reply_cap, NULL);
}

/* A JSON string's contents (escapes still in) as UTF-8 text. Spoolman's own JSON writer (Starlette, ensure_ascii off)
 * sends umlauts as UTF-8 and escapes only quotes, backslashes and control characters, but \uXXXX is valid JSON and
 * a proxy may use it. A surrogate pair is one character, a lone surrogate becomes U+FFFD, \u0000 is refused. */
static int spoolman_hex4(const char *in, int length, int at, unsigned int *out) {
    if (at < 0 || at + 4 > length) return 0;
    unsigned int value = 0;
    for (int k = 0; k < 4; ++k) {
        unsigned char c = (unsigned char)in[at + k];
        if (!isxdigit(c)) return 0;
        value = value * 16 + (unsigned int)(isdigit(c) ? c - '0' : tolower(c) - 'a' + 10);
    }
    *out = value;
    return 1;
}

static int spoolman_unescape(const char *in, int length, char *out, size_t cap) {
    size_t o = 0;
    for (int i = 0; i < length; ++i) {
        unsigned int cp = (unsigned char)in[i];
        if (in[i] != '\\') {                      /* plain text, UTF-8 included, goes through byte for byte */
            if (o + 1 >= cap) return 0;
            out[o++] = in[i];
            continue;
        }
        if (++i >= length) return 0;
        switch (in[i]) {
        case '"': cp = '"'; break;
        case '\\': cp = '\\'; break;
        case '/': cp = '/'; break;
        case 'b': cp = '\b'; break;
        case 'f': cp = '\f'; break;
        case 'n': cp = '\n'; break;
        case 'r': cp = '\r'; break;
        case 't': cp = '\t'; break;
        case 'u': {
            unsigned int high, low;
            if (!spoolman_hex4(in, length, i + 1, &high)) return 0;
            i += 4;
            cp = high;
            if (high >= 0xd800 && high <= 0xdbff) {
                if (i + 6 < length && in[i + 1] == '\\' && in[i + 2] == 'u' && spoolman_hex4(in, length, i + 3, &low) &&
                    low >= 0xdc00 && low <= 0xdfff) { cp = 0x10000 + ((high - 0xd800) << 10) + (low - 0xdc00); i += 6; }
                else cp = 0xfffd;
            } else if (high >= 0xdc00 && high <= 0xdfff) cp = 0xfffd;
            break;
        }
        default: return 0;
        }
        if (!cp) return 0;
        size_t need = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
        if (o + need >= cap) return 0;
        if (need == 1) out[o++] = (char)cp;
        else if (need == 2) { out[o++] = (char)(0xc0 | (cp >> 6)); out[o++] = (char)(0x80 | (cp & 63)); }
        else if (need == 3) { out[o++] = (char)(0xe0 | (cp >> 12)); out[o++] = (char)(0x80 | ((cp >> 6) & 63)); out[o++] = (char)(0x80 | (cp & 63)); }
        else { out[o++] = (char)(0xf0 | (cp >> 18)); out[o++] = (char)(0x80 | ((cp >> 12) & 63)); out[o++] = (char)(0x80 | ((cp >> 6) & 63)); out[o++] = (char)(0x80 | (cp & 63)); }
    }
    out[o] = 0;
    return 1;
}

/* A flat Spoolman spool object -> the fields we use, each through the library's bounded parsers: numbers must
 * be finite and in range, texts must pass spool_text_ok. A field that fails is simply absent, never trusted. */
static int spoolman_text(const char *obj, const char *end, const char *key, char *out, size_t cap, size_t max, int empty_ok) {
    const char *raw; int length;
    if (json_member_raw_string(obj, end, key, &raw, &length) && length >= 0 && spoolman_unescape(raw, length, out, cap) &&
        spool_text_ok(out, max, empty_ok)) return 1;
    out[0] = 0;
    return 0;
}

/* RRGGBB, or RRGGBBAA with the transparency last: a spool has no transparency, so only the RGB is kept
 * (the alpha must still be hex). Any other length is not a colour we know. */
static int spoolman_colour(const char *hex, int length, char out[8]) {
    if (length != 6 && length != 8) return 0;
    for (int i = 0; i < length; ++i) if (!isxdigit((unsigned char)hex[i])) return 0;
    out[0] = '#';
    for (int i = 0; i < 6; ++i) out[i + 1] = (char)toupper((unsigned char)hex[i]);
    out[7] = 0;
    return 1;
}

/* A number member that must lie in [min, max]; absent, null or out of range leaves `out` untouched and returns 0. */
static int spoolman_number(const char *obj, const char *end, const char *key, double min, double max, double *out) {
    double value;
    if (!plates_member_double(obj, end, key, &value) || value < min || value > max) return 0;
    *out = value;
    return 1;
}

static int spoolman_parse_spool(const char *obj, const char *end, spoolman_spool *e) {
    memset(e, 0, sizeof(*e));
    double id;
    if (!plates_member_double(obj, end, "id", &id) || id < 1 || id > 999999999999999.0 || id != floor(id)) return 0;
    snprintf(e->spoolman_id, sizeof(e->spoolman_id), "%.0f", id);
    e->have_remaining = plates_member_double(obj, end, "remaining_weight", &e->remaining) &&
                        e->remaining >= -1e6 && e->remaining <= 25000;
    e->have_used = plates_member_double(obj, end, "used_weight", &e->used) && e->used >= 0 && e->used <= 1e7;
    double initial = 0, filament_weight = 0, spool_tare = 0, filament_tare = 0, vendor_tare = 0;
    int have_initial = spoolman_number(obj, end, "initial_weight", 0, 25000, &initial);
    int have_spool_tare = spoolman_number(obj, end, "spool_weight", 0, 5000, &spool_tare);
    int have_filament_weight = 0, have_filament_tare = 0, have_vendor_tare = 0;
    spools_bool_member(obj, end, "archived", &e->archived);
    const char *fe, *filament = json_member_object(obj, end, "filament", '{', &fe);
    if (filament) {
        spoolman_text(filament, fe, "material", e->material, sizeof(e->material), SPOOL_MATERIAL_MAX, 0);
        spoolman_text(filament, fe, "name", e->name, sizeof(e->name), SPOOL_TEXT_MAX, 1);
        const char *hex; int length;
        if (json_member_raw_string(filament, fe, "color_hex", &hex, &length) && length >= 0) spoolman_colour(hex, length, e->color);
        /* A multi-colour filament has no color_hex: its first colour is the main one ("FF0000,00FF00,0000FF"). */
        if (!e->color[0] && json_member_raw_string(filament, fe, "multi_color_hexes", &hex, &length) && length > 0) {
            const char *comma = memchr(hex, ',', (size_t)length);
            spoolman_colour(hex, comma ? (int)(comma - hex) : length, e->color);
        }
        /* The maker, as the brand: the same text checks as every other text from Spoolman. */
        const char *ve, *vendor = json_member_object(filament, fe, "vendor", '{', &ve);
        if (vendor) {
            spoolman_text(vendor, ve, "name", e->brand, sizeof(e->brand), SPOOL_TEXT_MAX, 0);
            have_vendor_tare = spoolman_number(vendor, ve, "empty_spool_weight", 0, 5000, &vendor_tare);
        }
        if (!plates_member_double(filament, fe, "density", &e->density) || e->density < 0.5 || e->density > 3)
            e->density = 0;
        have_filament_weight = spoolman_number(filament, fe, "weight", 1e-9, 25000, &filament_weight);
        have_filament_tare = spoolman_number(filament, fe, "spool_weight", 0, 5000, &filament_tare);
    }
    /* Spoolman computes remaining_weight itself: max(initial_weight - used, 0) when the spool has its own start
     * weight, else max(filament.weight - used, 0), else it leaves the field out. When the field is missing but
     * its inputs are there (an answer that was cut down on the way), the same rule gives the same number. */
    if (!e->have_remaining && e->have_used) {
        if (have_initial) { e->remaining = initial - e->used > 0 ? initial - e->used : 0; e->have_remaining = 1; }
        else if (have_filament_weight) { e->remaining = filament_weight - e->used > 0 ? filament_weight - e->used : 0; e->have_remaining = 1; }
    }
    /* The full spool's weight: the filament's, else the spool's own start weight, else what was left plus used. */
    double net = have_filament_weight ? filament_weight : have_initial ? initial :
                 e->have_remaining && e->have_used ? e->remaining + e->used : 0;
    e->net = net >= 1 && net <= 20000 ? net : 0;
    /* The empty spool's weight, as Spoolman resolves it: the spool's, else the filament's, else the maker's. */
    e->tare = have_spool_tare ? spool_tare : have_filament_tare ? filament_tare : have_vendor_tare ? vendor_tare : 0;
    return 1;
}

/* elements: everything the list holds; parsed / bad: those looked at, read or not; consumed: how many of the first elements
 * were looked at (the rest, past the room for `max` spools, is left for the next page). */
typedef struct { int elements, parsed, bad, consumed; } spoolman_page_info;

/* A [...] list of spool objects, element by element: one that is not an object, or that cannot be read (no
 * valid id), is counted in `bad` and skipped -- it does not end the list. Returns how many were read (at most
 * `max`; any more are only counted in `elements`, and the next request has to start after `consumed`), or -1 when
 * the text is not a well-formed array. */
static int spoolman_parse_page(const char *json, spoolman_spool *out, int max, spoolman_page_info *info) {
    spoolman_page_info none; if (!info) info = &none;
    memset(info, 0, sizeof(*info));
    const char *end = json + strlen(json), *list = json_skip_space(json, end);
    if (list >= end || *list != '[') return -1;
    const char *list_end = json_container_end(list, end);
    if (!list_end) return -1;
    const char *p = json_skip_space(list + 1, list_end);
    int full = 0;
    while (p < list_end && *p != ']') {
        const char *item_end;
        if (*p == '{' || *p == '[') item_end = json_container_end(p, list_end);
        else if (*p == '"') item_end = json_string_end(p, list_end);
        else { item_end = p; while (item_end < list_end && *item_end != ',' && *item_end != ']') ++item_end; }
        if (!item_end) return -1;
        info->elements++;
        if (!full && *p == '{' && info->parsed >= max) full = 1;           /* no room for this one, nor for what follows */
        if (!full) {
            info->consumed++;
            if (*p == '{' && spoolman_parse_spool(p, item_end, &out[info->parsed])) info->parsed++;
            else info->bad++;
        }
        p = json_skip_space(item_end, list_end);
        if (p < list_end && *p == ',') p = json_skip_space(p + 1, list_end);
    }
    return info->parsed;
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
static int spoolman_catalog_truncated;   /* Spoolman has more spools than the catalog holds */
static char spoolman_catalog_error[128] = "Not fetched yet";

static void spoolman_stage(const spoolman_result *r) {
    pthread_mutex_lock(&spoolman_outbox_mutex);
    if (spoolman_outbox_count < (int)(sizeof(spoolman_outbox) / sizeof(spoolman_outbox[0])))
        spoolman_outbox[spoolman_outbox_count++] = *r;
    pthread_mutex_unlock(&spoolman_outbox_mutex);
}

/* ---- what the last sync cycle did: for GET /api/spoolman and one log line per change ------------------- */

enum {
    SPOOLMAN_R_NEVER, SPOOLMAN_R_OK, SPOOLMAN_R_UNREACHABLE, SPOOLMAN_R_UNAUTHORIZED, SPOOLMAN_R_FORBIDDEN,
    SPOOLMAN_R_NOT_FOUND, SPOOLMAN_R_SERVER, SPOOLMAN_R_REFUSED, SPOOLMAN_R_BAD_REPLY, SPOOLMAN_R_TOO_LARGE,
    SPOOLMAN_R_TIMEOUT, SPOOLMAN_R_BAD_URL
};
static const char *const spoolman_reason_name[] = {
    "never", "ok", "unreachable", "unauthorized", "forbidden", "not_found", "server_error", "refused",
    "bad_reply", "too_large", "timeout", "bad_url"
};

static int spoolman_reason_of(int code) {
    if (code == 200) return SPOOLMAN_R_OK;
    if (code == SPOOLMAN_E_NOSEND) return SPOOLMAN_R_UNREACHABLE;
    if (code == SPOOLMAN_E_UNKNOWN) return SPOOLMAN_R_BAD_REPLY;
    if (code == SPOOLMAN_E_TOOBIG) return SPOOLMAN_R_TOO_LARGE;
    if (code == SPOOLMAN_E_TIMEOUT) return SPOOLMAN_R_TIMEOUT;
    if (code == 401) return SPOOLMAN_R_UNAUTHORIZED;
    if (code == 403) return SPOOLMAN_R_FORBIDDEN;
    if (code == 404) return SPOOLMAN_R_NOT_FOUND;
    return code >= 500 ? SPOOLMAN_R_SERVER : SPOOLMAN_R_REFUSED;
}

/* What the catalog tells the page when the list could not be read. */
static const char *spoolman_reason_text(int reason) {
    return reason == SPOOLMAN_R_TOO_LARGE ? "Spoolman's spool list is too large to read" :
           reason == SPOOLMAN_R_BAD_REPLY ? "Spoolman sent an incomplete reply" :
           reason == SPOOLMAN_R_TIMEOUT ? "Spoolman did not answer in time" :
           reason == SPOOLMAN_R_UNREACHABLE ? "Could not reach Spoolman" : "Spoolman refused the request";
}

typedef struct {
    unsigned epoch;                 /* the server settings the cycle ran under */
    long long time;                 /* wall clock seconds when it ended; 0 = no cycle yet */
    int reason, http;               /* SPOOLMAN_R_*: the first thing that went wrong, or OK; the HTTP status if it had one */
    int pages, read, offered;       /* list requests made, spools read, new spools handed to the library */
    int unreadable, no_weight, library_full, too_large, no_material;   /* spools left out of the import, by reason */
    int archived_linked;            /* linked spools Spoolman has archived since (still synced, like any other) */
    int linked_no_weight;           /* linked spools whose weight Spoolman does not know (their text still syncs) */
    int orphans;                    /* linked spools Spoolman answered 404 for */
    char orphan_ids[SPOOLMAN_ORPHAN_MAX][SPOOLMAN_ID_MAX + 1];
} spoolman_status_t;

static pthread_mutex_t spoolman_status_mutex = PTHREAD_MUTEX_INITIALIZER;
static spoolman_status_t spoolman_status;
static int spoolman_log_count;     /* log lines written; the tests count them */

static void spoolman_note(spoolman_status_t *cy, int code) {
    int reason = spoolman_reason_of(code);
    if (reason == SPOOLMAN_R_OK || cy->reason != SPOOLMAN_R_OK) return;
    cy->reason = reason;
    cy->http = code > 0 ? code : 0;
}

/* One line when the outcome or the set of orphaned spools changes -- not one per cycle. Never the URL or login. */
static void spoolman_status_commit(const spoolman_status_t *now) {
    pthread_mutex_lock(&spoolman_status_mutex);
    spoolman_status_t old = spoolman_status;
    spoolman_status = *now;
    pthread_mutex_unlock(&spoolman_status_mutex);
    if (old.epoch != now->epoch) memset(&old, 0, sizeof(old));
    int first = !old.time;
    if (old.reason != now->reason && !(first && now->reason == SPOOLMAN_R_OK)) {
        spoolman_log_count++;
        if (now->http) fprintf(stderr, "Spoolman sync: %s (HTTP %d)\n", spoolman_reason_name[now->reason], now->http);
        else fprintf(stderr, "Spoolman sync: %s\n", spoolman_reason_name[now->reason]);
    }
    int orphans_changed = old.orphans != now->orphans;
    for (int i = 0; !orphans_changed && i < now->orphans && i < SPOOLMAN_ORPHAN_MAX; ++i)
        orphans_changed = strcmp(old.orphan_ids[i], now->orphan_ids[i]) != 0;
    if (orphans_changed) {
        spoolman_log_count++;
        char ids[SPOOLMAN_ORPHAN_MAX * (SPOOLMAN_ID_MAX + 1) + 1] = "";
        size_t used = 0;
        for (int i = 0; i < now->orphans && i < SPOOLMAN_ORPHAN_MAX; ++i)
            used += (size_t)snprintf(ids + used, sizeof(ids) - used, "%s%s", i ? "," : "", now->orphan_ids[i]);
        fprintf(stderr, "Spoolman sync: %d linked spool(s) not found in Spoolman%s%s\n", now->orphans, ids[0] ? ": " : "", ids);
    }
}

/* GET /api/v1/spool/{id}. 1 = read and parsed; `code` (may be NULL) gets the request's result. */
static int spoolman_get_spool_ex(const char *host, int port, const char *base, const char *auth, const char *id,
                                 spoolman_spool *out, int *code) {
    char reply[SPOOLMAN_BODY_MAX], path[SPOOLMAN_PATH_MAX + 64];
    snprintf(path, sizeof(path), "%s/api/v1/spool/%s", base, id);
    int status = spoolman_request(host, port, auth, "GET", path, NULL, reply, sizeof(reply));
    if (code) *code = status;
    return status == 200 && spoolman_parse_one(reply, out) && !strcmp(out->spoolman_id, id);
}

static int spoolman_get_spool(const char *host, int port, const char *base, const char *auth, const char *id,
                              spoolman_spool *out) {
    return spoolman_get_spool_ex(host, port, base, auth, id, out, NULL);
}

static long long spoolman_cycle_max_ms = SPOOLMAN_CYCLE_MAX_MS;
static int spoolman_rotate;   /* job the next cycle starts with, after one ran out of time; under spoolman_outbox_mutex */

/* Called for every spool of a list that was not seen before in this pass; returns 1 when nothing more is wanted. */
typedef int (*spoolman_spool_fn)(const spoolman_spool *e, void *ctx);

/* The whole spool list, a page at a time: sort=id:asc keeps the pages from overlapping or skipping (Spoolman
 * gives no order otherwise), limit/offset keep each reply small, and archived spools are not asked for. Each
 * spool is handed to `each` once. A spool that cannot be read is skipped and counted, not a reason to drop the rest.
 * Shared by the sync and by the catalog for "Send selected". */
static void spoolman_read_pages(const char *host, int port, const char *base, const char *auth, long long deadline,
                                spoolman_status_t *cy, spoolman_spool_fn each, void *ctx) {
    const int seen_cap = SPOOLMAN_PAGES_MAX * SPOOLMAN_PAGE_PARSE;
    char *body = malloc(SPOOLMAN_PAGE_BODY_MAX);
    spoolman_spool *page = malloc(sizeof(*page) * SPOOLMAN_PAGE_PARSE);
    unsigned long long *seen = malloc(sizeof(*seen) * (size_t)seen_cap);
    if (!body || !page || !seen) { free(body); free(page); free(seen); return; }
    int limit = SPOOLMAN_PAGE_SIZE, offset = 0, seen_count = 0, too_big_in_row = 0, done = 0;
    for (int request = 0; request < SPOOLMAN_PAGES_MAX; ++request) {
        if (monotonic_ms() >= deadline) { spoolman_note(cy, SPOOLMAN_E_TIMEOUT); break; }
        char path[SPOOLMAN_PATH_MAX + 96];
        snprintf(path, sizeof(path), "%s/api/v1/spool?sort=id:asc&limit=%d&offset=%d", base, limit, offset);
        long total;
        int status = spoolman_request_ex(host, port, auth, "GET", path, NULL, body, SPOOLMAN_PAGE_BODY_MAX, &total);
        if (status == SPOOLMAN_E_TOOBIG) {
            if (limit > 1) { limit = (limit + 1) / 2; continue; }        /* smaller pages */
            cy->too_large++; offset++;                                    /* one spool alone is too big: skip it */
            if (++too_big_in_row >= 3) { spoolman_note(cy, status); break; }
            continue;
        }
        too_big_in_row = 0;
        if (status != 200) { spoolman_note(cy, status); break; }
        spoolman_page_info info;
        if (spoolman_parse_page(body, page, SPOOLMAN_PAGE_PARSE, &info) < 0) { spoolman_note(cy, SPOOLMAN_E_UNKNOWN); break; }
        cy->pages++;
        cy->unreadable += info.bad;
        int fresh = 0;
        for (int i = 0; i < info.parsed; ++i) {
            const spoolman_spool *e = &page[i];
            unsigned long long id = strtoull(e->spoolman_id, NULL, 10);
            int dup = 0;
            for (int k = 0; k < seen_count && !dup; ++k) dup = seen[k] == id;
            if (dup || seen_count >= seen_cap) continue;
            seen[seen_count++] = id; fresh++; cy->read++;
            if (each(e, ctx)) done = 1;
        }
        if (!info.elements) break;                                        /* the end of the list */
        offset += info.consumed;                                          /* only what was looked at: the rest comes with the next page */
        if (total >= 0 ? offset >= total : info.elements < limit) break;
        if (info.parsed > 0 && !fresh) break;                             /* the same spools again: offset is not honoured */
        if (done) break;                                                  /* nothing more to learn */
    }
    free(body); free(page); free(seen);
}

/* What the sync does with each spool of the list: a linked one is kept in `listed` (so its own request can be
 * spared); a new one is handed to the library, up to what still fits there. */
typedef struct {
    const spoolman_job_batch *batch;
    spoolman_status_t *cy;
    spoolman_spool *listed;
    char *found;
    int staged, found_count;
} spoolman_pass;

static int spoolman_pass_spool(const spoolman_spool *e, void *ctx) {
    spoolman_pass *ps = ctx;
    const spoolman_job_batch *batch = ps->batch;
    spoolman_status_t *cy = ps->cy;
    int linked = -1;
    for (int k = 0; k < batch->known_count; ++k)
        if (!strcmp(batch->known_ids[k], e->spoolman_id)) { linked = k; break; }
    if (linked >= 0) {
        if (!ps->found[linked]) { ps->listed[linked] = *e; ps->found[linked] = 1; ps->found_count++; }
    } else if (e->archived) {
        /* the list does not contain these; a proxy that adds them is not a reason to import them */
    } else if (!e->have_remaining) cy->no_weight++;
    else if (ps->staged >= batch->free_slots) cy->library_full++;
    else {
        spoolman_result r; memset(&r, 0, sizeof(r));
        r.is_new = 1; r.epoch = batch->epoch; r.have_pull = 1;
        r.spool = *e;
        snprintf(r.spoolman_id, sizeof(r.spoolman_id), "%s", e->spoolman_id);
        spoolman_stage(&r);
        ps->staged++; cy->offered++;
        if (!e->material[0]) cy->no_material++;
    }
    return ps->staged >= batch->free_slots && ps->found_count == batch->known_count;
}

static void spoolman_list_pass(const char *host, int port, const char *base, const spoolman_job_batch *batch,
                               long long deadline, spoolman_status_t *cy, spoolman_spool *listed, char *found) {
    spoolman_pass ps = {batch, cy, listed, found, 0, 0};
    spoolman_read_pages(host, port, base, batch->basic_auth, deadline, cy, spoolman_pass_spool, &ps);
}

/* One job that sends or settles usage: unchanged accounting, see the top of this file. */
static void spoolman_run_job(const spoolman_job_batch *batch, int index, const char *host, int port, const char *base,
                             spoolman_status_t *cy, const spoolman_spool *listed_entry) {
    spoolman_result r; memset(&r, 0, sizeof(r));
    const spoolman_job *job = &batch->jobs[index];
    snprintf(r.spool_id, sizeof(r.spool_id), "%s", job->spool_id);
    snprintf(r.spoolman_id, sizeof(r.spoolman_id), "%s", job->spoolman_id);
    r.epoch = batch->epoch; r.gen = job->gen;
    r.settle = job->reconcile;
    const char *sid = job->spoolman_id;
    spoolman_spool before, after;
    int have_before, code = 0;
    if (listed_entry && !job->send && !job->reconcile) { before = *listed_entry; have_before = 1; }
    else have_before = spoolman_get_spool_ex(host, port, base, batch->basic_auth, sid, &before, &code);
    if (!have_before) {
        if (code == 404) {
            r.missing = 1;
            if (cy->orphans < SPOOLMAN_ORPHAN_MAX) snprintf(cy->orphan_ids[cy->orphans], SPOOLMAN_ID_MAX + 1, "%s", sid);
            cy->orphans++;
        } else if (code != 200) spoolman_note(cy, code);
    }
    const spoolman_spool *pull = have_before ? &before : NULL;
    if (job->reconcile) {
        /* An earlier /use has an unknown outcome: read Spoolman's used weight and decide, send nothing new. */
        if (!have_before && code == 404) r.outcome = SPOOLMAN_MISSING;
        else if (have_before && before.have_used) {
            double base_used = job->base, grams = job->inflight, tol = SPOOLMAN_TOLERANCE_G;
            if (base_used < 0) r.outcome = SPOOLMAN_DROP;
            else if (before.used >= base_used + grams - tol) r.outcome = SPOOLMAN_APPLIED;
            else if (before.used <= base_used + tol) r.outcome = SPOOLMAN_REQUEUE;
            else r.outcome = SPOOLMAN_DROP;
        }
    } else if (job->send > 0) {
        double grams = job->send;
        if (!have_before || !before.have_used) {
            /* could not even read the baseline: nothing was sent; a spool Spoolman does not have goes on the list */
            r.outcome = !have_before && code == 404 ? SPOOLMAN_MISSING : SPOOLMAN_NOT_SENT;
        } else {
            char body[64], path[SPOOLMAN_PATH_MAX + 64], reply[SPOOLMAN_BODY_MAX];
            snprintf(body, sizeof(body), "{\"use_weight\":%.3f}", grams);
            snprintf(path, sizeof(path), "%s/api/v1/spool/%s/use", base, sid);
            int status = spoolman_request(host, port, batch->basic_auth, "PUT", path, body, reply, sizeof(reply));
            if (status == 200) r.outcome = SPOOLMAN_SENT;
            else if (status == 404) { r.outcome = SPOOLMAN_MISSING; r.missing = 1; }
            else if (status == SPOOLMAN_E_NOSEND || (status >= 300 && status < 500)) r.outcome = SPOOLMAN_NOT_SENT;
            else r.outcome = SPOOLMAN_AMBIGUOUS;      /* 5xx, cut-off or unreadable reply: it may have been applied */
            if (status != 200) spoolman_note(cy, status);
            if (r.outcome != SPOOLMAN_NOT_SENT && r.outcome != SPOOLMAN_MISSING) {
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
    if (pull) {
        r.have_pull = 1; r.spool = *pull;
        if (pull->archived) cy->archived_linked++;
        if (!pull->have_remaining) cy->linked_no_weight++;
    }
    if (r.outcome != SPOOLMAN_NONE || r.have_pull || r.missing) spoolman_stage(&r);
}

/* Runs off the main thread. Only touches spoolman_outbox (its own mutex) and
 * the stack-local copy of what to send -- never `spools`. */
static void *spoolman_worker(void *arg) {
    spoolman_job_batch *batch = arg;
    char host[SPOOLMAN_HOST_MAX], base[SPOOLMAN_PATH_MAX];
    int port;
    spoolman_status_t cy; memset(&cy, 0, sizeof(cy));
    cy.epoch = batch->epoch; cy.reason = SPOOLMAN_R_OK;
    if (!spoolman_parse_url(batch->url, host, sizeof(host), &port, base, sizeof(base))) {
        cy.reason = SPOOLMAN_R_BAD_URL;
        /* Nothing can be sent: the grams the tick already moved to in-flight go back to unsent, not into limbo. */
        for (int i = 0; i < batch->count; ++i) {
            if (batch->jobs[i].send <= 0) continue;
            spoolman_result r; memset(&r, 0, sizeof(r));
            snprintf(r.spool_id, sizeof(r.spool_id), "%s", batch->jobs[i].spool_id);
            snprintf(r.spoolman_id, sizeof(r.spoolman_id), "%s", batch->jobs[i].spoolman_id);
            r.epoch = batch->epoch; r.gen = batch->jobs[i].gen; r.outcome = SPOOLMAN_NOT_SENT;
            spoolman_stage(&r);
        }
    } else {
        const long long deadline = monotonic_ms() + spoolman_cycle_max_ms;
        int count = batch->count, start = 0, stopped = -1;
        pthread_mutex_lock(&spoolman_outbox_mutex);
        if (count > 0) start = spoolman_rotate % count;
        pthread_mutex_unlock(&spoolman_outbox_mutex);
        /* 1. Usage first: grams to send or to settle. A job the time ran out for gives its grams back as not
         *    sent (they were already moved to in-flight); an unsettled one simply waits for the next cycle. */
        for (int n = 0; n < count; ++n) {
            int i = (start + n) % count;
            if (!batch->jobs[i].send && !batch->jobs[i].reconcile) continue;
            if (monotonic_ms() >= deadline) {
                if (stopped < 0) stopped = i;
                if (batch->jobs[i].send > 0) {
                    spoolman_result r; memset(&r, 0, sizeof(r));
                    snprintf(r.spool_id, sizeof(r.spool_id), "%s", batch->jobs[i].spool_id);
                    snprintf(r.spoolman_id, sizeof(r.spoolman_id), "%s", batch->jobs[i].spoolman_id);
                    r.epoch = batch->epoch; r.gen = batch->jobs[i].gen; r.outcome = SPOOLMAN_NOT_SENT;
                    spoolman_stage(&r);
                }
                spoolman_note(&cy, SPOOLMAN_E_TIMEOUT);
                continue;
            }
            spoolman_run_job(batch, i, host, port, base, &cy, NULL);
        }
        /* 2. The list: new spools to import, and the current state of the linked ones. */
        spoolman_spool *listed = batch->known_count ? malloc(sizeof(*listed) * (size_t)batch->known_count) : NULL;
        char found[SPOOLS_MAX]; memset(found, 0, sizeof(found));
        if (listed || !batch->known_count) spoolman_list_pass(host, port, base, batch, deadline, &cy, listed, found);
        /* 3. The linked spools that only want a refresh: from the list when they are in it, else one request each. */
        for (int n = 0; n < count; ++n) {
            int i = (start + n) % count;
            if (batch->jobs[i].send || batch->jobs[i].reconcile) continue;
            if (monotonic_ms() >= deadline) { if (stopped < 0) stopped = i; spoolman_note(&cy, SPOOLMAN_E_TIMEOUT); break; }
            int k = -1;
            for (int j = 0; j < batch->known_count; ++j)
                if (!strcmp(batch->known_ids[j], batch->jobs[i].spoolman_id)) { k = j; break; }
            spoolman_run_job(batch, i, host, port, base, &cy, k >= 0 && found[k] ? &listed[k] : NULL);
        }
        free(listed);
        pthread_mutex_lock(&spoolman_outbox_mutex);
        spoolman_rotate = stopped >= 0 ? stopped : 0;
        pthread_mutex_unlock(&spoolman_outbox_mutex);
    }
    cy.time = (long long)spools_wall(NULL);
    spoolman_status_commit(&cy);
    pthread_mutex_lock(&spoolman_outbox_mutex);
    spoolman_worker_running = 0;
    pthread_mutex_unlock(&spoolman_outbox_mutex);
    free(batch);
    return NULL;
}

/* ---- the list of usage that was not sent: the user decides, nothing here runs by itself ---------------------- */

/* "Send selected": one /use per chosen entry, to the Spoolman spool the user chose for it, the same way the sync
 * sends (the used weight is read before, a lost answer is settled from the used weight after). An answer that stays
 * unclear leaves the entry on the list as "unsure": it is never sent a second time. */
typedef struct { unsigned id; char spoolman_id[SPOOLMAN_ID_MAX + 1]; double grams; } spoolman_send_job;
typedef struct {
    char url[SPOOLMAN_URL_MAX + 1], basic_auth[SPOOLMAN_AUTH_MAX + 1];
    unsigned epoch;
    int count;
    spoolman_send_job jobs[SPOOLMAN_HELD_MAX];
} spoolman_send_batch;
typedef struct { unsigned id, epoch; int outcome; } spoolman_send_result;   /* SENT, NOT_SENT, MISSING or DROP (= unsure) */

static spoolman_send_result spoolman_send_outbox[SPOOLMAN_HELD_MAX];   /* under spoolman_outbox_mutex */
static int spoolman_send_outbox_count;

static void spoolman_send_stage(unsigned id, unsigned epoch, int outcome) {
    pthread_mutex_lock(&spoolman_outbox_mutex);
    if (spoolman_send_outbox_count < SPOOLMAN_HELD_MAX)
        spoolman_send_outbox[spoolman_send_outbox_count++] = (spoolman_send_result){id, epoch, outcome};
    pthread_mutex_unlock(&spoolman_outbox_mutex);
}

static int spoolman_send_one(const char *host, int port, const char *base, const char *auth, const char *sid, double grams) {
    spoolman_spool before, after;
    int code = 0;
    if (!spoolman_get_spool_ex(host, port, base, auth, sid, &before, &code)) return code == 404 ? SPOOLMAN_MISSING : SPOOLMAN_NOT_SENT;
    if (!before.have_used) return SPOOLMAN_NOT_SENT;
    char body[64], path[SPOOLMAN_PATH_MAX + 64], reply[SPOOLMAN_BODY_MAX];
    snprintf(body, sizeof(body), "{\"use_weight\":%.3f}", grams);
    snprintf(path, sizeof(path), "%s/api/v1/spool/%s/use", base, sid);
    int status = spoolman_request(host, port, auth, "PUT", path, body, reply, sizeof(reply));
    if (status == 200) return SPOOLMAN_SENT;
    if (status == 404) return SPOOLMAN_MISSING;
    if (status == SPOOLMAN_E_NOSEND || (status >= 300 && status < 500)) return SPOOLMAN_NOT_SENT;
    /* 5xx, cut-off or unreadable reply: it may have been applied. Applied is certain when the used weight grew. */
    if (spoolman_get_spool(host, port, base, auth, sid, &after) && after.have_used &&
        after.used >= before.used + grams - SPOOLMAN_TOLERANCE_G) return SPOOLMAN_SENT;
    return SPOOLMAN_DROP;
}

static void *spoolman_send_worker(void *arg) {
    spoolman_send_batch *batch = arg;
    char host[SPOOLMAN_HOST_MAX], base[SPOOLMAN_PATH_MAX];
    int port, usable = spoolman_parse_url(batch->url, host, sizeof(host), &port, base, sizeof(base));
    const long long deadline = monotonic_ms() + spoolman_cycle_max_ms;
    for (int i = 0; i < batch->count; ++i) {
        const spoolman_send_job *job = &batch->jobs[i];
        int outcome = SPOOLMAN_NOT_SENT;                  /* no time left, or no usable address: nothing went out */
        if (usable && monotonic_ms() < deadline)
            outcome = spoolman_send_one(host, port, base, batch->basic_auth, job->spoolman_id, job->grams);
        spoolman_send_stage(job->id, batch->epoch, outcome);
    }
    pthread_mutex_lock(&spoolman_outbox_mutex);
    spoolman_worker_running = 0;
    pthread_mutex_unlock(&spoolman_outbox_mutex);
    free(batch);
    return NULL;
}

/* Main thread (spoolman_tick): what a send worker has found out goes into the list. */
static void spoolman_held_apply(const spoolman_send_result *results, int count) {
    for (int i = 0; i < count; ++i) {
        if (results[i].epoch != spoolman_epoch) continue;     /* the server settings changed since: those entries became "unsure" */
        int at = -1;
        for (int k = 0; k < spools.held_count; ++k) if (spools.held[k].id == results[i].id) at = k;
        if (at < 0 || !spools.held[at].sending) continue;
        spoolman_held_entry *h = &spools.held[at];
        h->sending = 0;
        if (results[i].outcome == SPOOLMAN_SENT) {
            memmove(h, h + 1, (size_t)(spools.held_count - at - 1) * sizeof(*h));
            spools.held_count--;
        } else if (results[i].outcome == SPOOLMAN_MISSING) h->reason = SPOOLMAN_HELD_NOT_FOUND;
        else if (results[i].outcome == SPOOLMAN_DROP) h->reason = SPOOLMAN_HELD_UNSURE;
        spools_touch(1);
    }
}

static spoolman_held_entry *spoolman_held_find(const char *text) {
    if (!text[0] || strlen(text) > 10) return NULL;
    for (const char *p = text; *p; ++p) if (!isdigit((unsigned char)*p)) return NULL;
    unsigned long id = strtoul(text, NULL, 10);
    for (int i = 0; i < spools.held_count; ++i) if (spools.held[i].id == id) return &spools.held[i];
    return NULL;
}

/* POST /api/spoolman/held/send: one line `<entry id>=<Spoolman spool id>` per chosen entry. Answers 202 when the
 * request has started; the page watches GET /api/spools (`spoolman_held`) for the result. */
static void spoolman_held_send_response(int fd, const char *body, size_t length) {
    spool_form f;
    if (!spool_form_parse(body, length, &f) || f.count < 1) { spools_fail(fd, 400, "Invalid request"); return; }
    if (!spools_open(fd)) return;
    if (!spools.spoolman_enabled || !spools.spoolman_url[0]) { spools_fail(fd, 409, "Spoolman sync is off"); return; }
    spoolman_send_batch *batch = calloc(1, sizeof(*batch));
    if (!batch) { spools_fail(fd, 500, "Out of memory"); return; }
    spoolman_held_entry *entries[SPOOLMAN_HELD_MAX];
    for (int i = 0; i < f.count; ++i) {
        spoolman_held_entry *h = spoolman_held_find(f.key[i]);
        const char *target = f.value[i];
        if (!h) { free(batch); spools_fail(fd, 404, "Unknown entry"); return; }
        if (!target[0] || strlen(target) > SPOOLMAN_ID_MAX || !spoolman_id_ok(target)) { free(batch); spools_fail(fd, 400, "Choose a Spoolman spool"); return; }
        if (h->reason == SPOOLMAN_HELD_UNSURE) { free(batch); spools_fail(fd, 409, "This amount may already be counted in Spoolman: it is never sent again"); return; }
        if (h->sending) { free(batch); spools_fail(fd, 409, "This amount is being sent"); return; }
        entries[i] = h;
        batch->jobs[i].id = h->id; batch->jobs[i].grams = h->grams;
        snprintf(batch->jobs[i].spoolman_id, sizeof(batch->jobs[i].spoolman_id), "%s", target);
    }
    batch->count = f.count;
    snprintf(batch->url, sizeof(batch->url), "%s", spools.spoolman_url);
    snprintf(batch->basic_auth, sizeof(batch->basic_auth), "%s", spools.spoolman_basic_auth);
    batch->epoch = spoolman_epoch;
    pthread_mutex_lock(&spoolman_outbox_mutex);
    int busy = spoolman_worker_running;
    pthread_mutex_unlock(&spoolman_outbox_mutex);
    if (busy) { free(batch); spools_fail(fd, 409, "Spoolman sync is busy: try again in a moment"); return; }
    spool_store *before = spools_snapshot(fd);
    if (!before) { free(batch); return; }
    /* Written down before it is sent: if CC2 Control stops in the middle, the entry comes back as "unsure". */
    for (int i = 0; i < f.count; ++i) entries[i]->sending = 1;
    if (!spools_commit(fd, before)) { free(batch); return; }
    pthread_t thread;
    pthread_mutex_lock(&spoolman_outbox_mutex);
    int started = pthread_create(&thread, NULL, spoolman_send_worker, batch) == 0;
    if (started) { spoolman_worker_running = 1; pthread_detach(thread); }
    pthread_mutex_unlock(&spoolman_outbox_mutex);
    if (!started) {
        free(batch);
        for (int i = 0; i < f.count; ++i) entries[i]->sending = 0;     /* nothing went out */
        spools_touch(1); (void)spools_save();
        spools_fail(fd, 500, "Cannot start the request");
        return;
    }
    spools_reply(fd, 202, "{\"ok\":true}\n");
}

/* POST /api/spoolman/held/discard (`id`): takes one entry off the list. Nothing is sent to Spoolman. */
static void spoolman_held_discard_response(int fd, const char *body, size_t length) {
    spool_form f;
    const char *id;
    if (!spool_form_parse(body, length, &f) || !(id = spool_form_get(&f, "id"))) { spools_fail(fd, 400, "Invalid request"); return; }
    if (!spools_open(fd)) return;
    spoolman_held_entry *h = spoolman_held_find(id);
    if (!h) { spools_fail(fd, 404, "Unknown entry"); return; }
    if (h->sending) { spools_fail(fd, 409, "This amount is being sent"); return; }
    spool_store *before = spools_snapshot(fd);
    if (!before) return;
    h = spoolman_held_find(id);
    int at = (int)(h - spools.held);
    memmove(h, h + 1, (size_t)(spools.held_count - at - 1) * sizeof(*h));
    spools.held_count--;
    if (spools_commit(fd, before)) spools_reply(fd, 200, "{\"ok\":true}\n");
}

/* POST /api/spoolman/unlink-old: after an address change, takes every spool off the numbers of the old server. The
 * spools stay; amounts they still had pending go on the list. */
static void spoolman_unlink_old_response(int fd) {
    if (!spools_open(fd)) return;
    if (!spools.spoolman_stale_links) { spools_fail(fd, 409, "There are no old connections"); return; }
    spool_store *before = spools_snapshot(fd);
    if (!before) return;
    int full = 0;
    for (int i = 0; i < spools.count; ++i) {
        spool_entry *sp = &spools.spools[i];
        if (!sp->spoolman_id[0]) continue;
        if (!spoolman_hold(sp, sp->spoolman_unsent, SPOOLMAN_HELD_SERVER) ||
            !spoolman_hold(sp, sp->spoolman_inflight, SPOOLMAN_HELD_UNSURE)) full = 1;
        sp->spoolman_id[0] = 0;
        sp->spoolman_unsent = sp->spoolman_inflight = 0;
        sp->spoolman_base = -1;
        sp->spoolman_gen++;
        sp->spoolman_miss = 0;
    }
    if (full) {
        spools = *before; free(before);
        spools_fail(fd, 409, "The list of unsent usage is full: review it first");
        return;
    }
    spools.spoolman_stale_links = 0;
    if (spools_commit(fd, before)) spools_reply(fd, 200, "{\"ok\":true}\n");
}

/* POST /api/spoolman/keep-old: after an address change, the user says it is the same server: the spools keep their
 * numbers and the sync takes over what Spoolman says again. Nothing else changes. */
static void spoolman_keep_old_response(int fd) {
    if (!spools_open(fd)) return;
    if (!spools.spoolman_stale_links) { spools_fail(fd, 409, "There are no old connections"); return; }
    spool_store *before = spools_snapshot(fd);
    if (!before) return;
    spools.spoolman_stale_links = 0;
    if (spools_commit(fd, before)) spools_reply(fd, 200, "{\"ok\":true}\n");
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
    spoolman_send_result sent[SPOOLMAN_HELD_MAX];
    int sent_count = spoolman_send_outbox_count;
    memcpy(sent, spoolman_send_outbox, (size_t)sent_count * sizeof(sent[0]));
    spoolman_send_outbox_count = 0;
    int running = spoolman_worker_running;
    pthread_mutex_unlock(&spoolman_outbox_mutex);
    if (sent_count) spoolman_held_apply(sent, sent_count);

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
            snprintf(s.brand, sizeof(s.brand), "%s", r->spool.brand);   /* "" -> the tray says Generic */
            s.tare = r->spool.tare;
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
        /* One 404 is not believed (a wrong base path, a proxy that hiccups): only the second one in a row says the
         * spool is gone. A reply that read the spool, or took the grams, starts the count again. */
        int gone = 0;
        if (r->missing) gone = ++sp->spoolman_miss >= 2;
        else if (r->have_pull || r->outcome == SPOOLMAN_SENT || r->outcome == SPOOLMAN_APPLIED) sp->spoolman_miss = 0;
        switch (r->outcome) {
        case SPOOLMAN_SENT: case SPOOLMAN_APPLIED:
            sp->spoolman_inflight = 0; sp->spoolman_base = -1; break;
        case SPOOLMAN_DROP:
            /* Not sent again, and not forgotten: the user sees it as "unsure" (a full list forgets it, as before). */
            (void)spoolman_hold(sp, sp->spoolman_inflight, SPOOLMAN_HELD_UNSURE);
            sp->spoolman_inflight = 0; sp->spoolman_base = -1; break;
        case SPOOLMAN_MISSING:
            if (!gone) {
                /* The first 404: nothing was sent, so new grams go back to unsent; grams of an earlier request
                 * stay in flight and are settled by the next cycle. */
                if (!r->settle) { sp->spoolman_unsent += sp->spoolman_inflight; sp->spoolman_inflight = 0; sp->spoolman_base = -1; }
                break;
            }
            /* Spoolman has no such spool: the grams wait on the list instead of being tried again every cycle. */
            if (!spoolman_hold(sp, sp->spoolman_inflight, SPOOLMAN_HELD_NOT_FOUND)) sp->spoolman_unsent += sp->spoolman_inflight;
            sp->spoolman_inflight = 0; sp->spoolman_base = -1; break;
        case SPOOLMAN_NOT_SENT: case SPOOLMAN_REQUEUE:
            sp->spoolman_unsent += sp->spoolman_inflight; sp->spoolman_inflight = 0; sp->spoolman_base = -1; break;
        case SPOOLMAN_AMBIGUOUS:
            sp->spoolman_base = r->base; break;
        default: break;
        }
        /* Spools still connected to the numbers of an old server (the address changed and nobody has said yet whether
         * it is the same server): Spoolman's spool of that number may be another one, so nothing of it is taken over. */
        if (r->have_pull && spools.spoolman_enabled && !spools.spoolman_stale_links) {
            const spoolman_spool *p = &r->spool;
            if (p->material[0]) snprintf(sp->material, sizeof(sp->material), "%s", p->material);
            if (p->name[0]) snprintf(sp->name, sizeof(sp->name), "%s", p->name);
            if (p->brand[0]) snprintf(sp->brand, sizeof(sp->brand), "%s", p->brand);
            if (p->color[0]) snprintf(sp->color, sizeof(sp->color), "%s", p->color);
            /* Spoolman's figure, less what it has not been told about: usage charged while the request was out,
             * and grams still in flight. Never the bare reply, which would undo them. A spool Spoolman has no
             * weight for (neither the spool nor its filament has a start weight) keeps the weight it has here. */
            if (p->have_remaining) {
                sp->remaining = p->remaining - sp->spoolman_unsent - sp->spoolman_inflight;
                if (sp->remaining < -1e6) sp->remaining = -1e6;
            }
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
    batch->free_slots = SPOOLS_MAX - spools.count;
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

/* The same host, port and base path: how the address is written does not matter. Anything that is not a usable address
 * is never "the same". */
static int spoolman_same_server(const char *a, const char *b) {
    char host_a[SPOOLMAN_HOST_MAX], base_a[SPOOLMAN_PATH_MAX], host_b[SPOOLMAN_HOST_MAX], base_b[SPOOLMAN_PATH_MAX];
    int port_a, port_b;
    if (!spoolman_parse_url(a, host_a, sizeof(host_a), &port_a, base_a, sizeof(base_a)) ||
        !spoolman_parse_url(b, host_b, sizeof(host_b), &port_b, base_b, sizeof(base_b))) return 0;
    struct in_addr x, y;
    if (inet_pton(AF_INET, host_a, &x) != 1 || inet_pton(AF_INET, host_b, &y) != 1) return 0;
    return x.s_addr == y.s_addr && port_a == port_b && !strcmp(base_a, base_b);
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
            spools_fail(fd, 400, "Invalid URL: use http://IPv4-address[:port][/path] (host names are not resolved, no TLS)"); return;
        }
    } else if (url && !url[0]) {
        /* turning sync off: fine, nothing to validate */
    }
    if (auth && strlen(auth) > SPOOLMAN_AUTH_MAX) { spools_fail(fd, 400, "basic_auth is too long"); return; }
    spool_store *before = spools_snapshot(fd);
    if (!before) return;
    /* Another server, not another way of writing the same one (a slash at the end, the default port spelled out). */
    int url_changed = url && strcmp(url, spools.spoolman_url) && !spoolman_same_server(url, spools.spoolman_url);
    int changed = url_changed || (auth && strcmp(auth, spools.spoolman_basic_auth));
    int toggled = enabled && spools.spoolman_enabled != (enabled[0] == '1');
    if (enabled) spools.spoolman_enabled = enabled[0] == '1';
    if (url) snprintf(spools.spoolman_url, sizeof(spools.spoolman_url), "%s", url);
    if (auth) snprintf(spools.spoolman_basic_auth, sizeof(spools.spoolman_basic_auth), "%s", auth);
    if (changed) {
        /* A different server (or login): requests still running for the old one are discarded when they answer.
         * What was pending is not thrown away and not sent on its own: it goes on the list of unsent usage, where
         * the user decides. Turning the sync off (an empty address) is the user's own decision and clears it. */
        int turned_off = url && !url[0], linked = 0, full = 0;
        for (int i = 0; i < spools.count; ++i) {
            spool_entry *sp = &spools.spools[i];
            if (sp->spoolman_id[0]) linked++;
            if (!sp->spoolman_id[0] || turned_off) continue;
            if (!spoolman_hold(sp, sp->spoolman_unsent, url_changed ? SPOOLMAN_HELD_SERVER : SPOOLMAN_HELD_LOGIN) ||
                !spoolman_hold(sp, sp->spoolman_inflight, SPOOLMAN_HELD_UNSURE)) full = 1;
        }
        if (full) {
            spools = *before; free(before);
            spools_fail(fd, 409, "The list of unsent usage is full: review it first");
            return;
        }
        /* A request for the list's own entries may have reached the old server: it is not sent again. */
        for (int i = 0; i < spools.held_count; ++i)
            if (spools.held[i].sending) { spools.held[i].sending = 0; spools.held[i].reason = SPOOLMAN_HELD_UNSURE; }
        spoolman_epoch++;
        for (int i = 0; i < spools.count; ++i) {
            spools.spools[i].spoolman_unsent = spools.spools[i].spoolman_inflight = 0;
            spools.spools[i].spoolman_base = -1;
            spools.spools[i].spoolman_miss = 0;
        }
        if (url_changed) spools.spoolman_stale_links = !turned_off && linked > 0;
    }
    if (spools_commit(fd, before)) {
        atomic_store(&spoolman_paused, !spools.spoolman_enabled);
        if (toggled && spools.spoolman_enabled) spoolman_last_tick_ms = 0;
        spools_reply(fd, 200, "{\"ok\":true}\n");
    }
}

/* ---- GET /api/spoolman: what the last sync cycle did ---------------------------------------------------- */

static void spoolman_status_response(int fd) {
    if (!spools_open(fd)) return;
    pthread_mutex_lock(&spoolman_status_mutex);
    spoolman_status_t st = spoolman_status;
    pthread_mutex_unlock(&spoolman_status_mutex);
    if (st.epoch != spoolman_epoch) memset(&st, 0, sizeof(st));   /* a cycle for another server says nothing about this one */
    int unsent = 0, inflight = 0;
    double grams = 0, held_grams = 0;
    for (int i = 0; i < spools.held_count; ++i) held_grams += spools.held[i].grams;
    for (int i = 0; i < spools.count; ++i) {
        const spool_entry *sp = &spools.spools[i];
        if (!sp->spoolman_id[0]) continue;
        if (sp->spoolman_unsent > 0) { unsent++; grams += sp->spoolman_unsent; }
        if (sp->spoolman_inflight > 0) inflight++;
    }
    char text[1536];
    int n = snprintf(text, sizeof(text),
        "{\"enabled\":%s,\"configured\":%s,\"last_sync\":%lld,\"result\":\"%s\",\"http\":%d,"
        "\"unsent_spools\":%d,\"unsent_grams\":%.2f,\"inflight_spools\":%d,\"held_entries\":%d,\"held_grams\":%.2f,"
        "\"list\":{\"pages\":%d,\"read\":%d,\"new\":%d,\"unreadable\":%d,\"no_weight\":%d,\"library_full\":%d,"
        "\"too_large\":%d,\"no_material\":%d},\"archived_linked\":%d,\"linked_no_weight\":%d,\"orphaned\":[",
        spools.spoolman_enabled ? "true" : "false", spools.spoolman_url[0] ? "true" : "false", st.time,
        spoolman_reason_name[st.reason], st.http, unsent, grams, inflight, spools.held_count, held_grams, st.pages, st.read, st.offered,
        st.unreadable, st.no_weight, st.library_full, st.too_large, st.no_material, st.archived_linked,
        st.linked_no_weight);
    for (int i = 0; i < st.orphans && i < SPOOLMAN_ORPHAN_MAX && n > 0 && (size_t)n < sizeof(text) - 64; ++i)
        n += snprintf(text + n, sizeof(text) - (size_t)n, "%s\"%s\"", i ? "," : "", st.orphan_ids[i]);
    snprintf(text + n, sizeof(text) - (size_t)n, "],\"orphaned_count\":%d}\n", st.orphans);
    respond(fd, 200, "OK", "application/json; charset=utf-8", text, strlen(text));
}

/* ---- Spoolman catalog: the browser's own picker, instead of typing an id blind ------------- */

typedef struct { spoolman_spool *list; int count, truncated; } spoolman_catalog_fill;

static int spoolman_catalog_spool(const spoolman_spool *e, void *ctx) {
    spoolman_catalog_fill *fill = ctx;
    if (fill->count >= SPOOLMAN_CATALOG_MAX) { fill->truncated = 1; return 1; }
    fill->list[fill->count++] = *e;
    return 0;
}

/* The same pages the sync reads (see spoolman_read_pages), so a large library is no problem here either. */
static void *spoolman_catalog_worker(void *arg) {
    char url[SPOOLMAN_URL_MAX + 1], auth[SPOOLMAN_AUTH_MAX + 1];
    memcpy(url, arg, sizeof(url)); memcpy(auth, (char *)arg + sizeof(url), sizeof(auth));
    free(arg);
    char host[SPOOLMAN_HOST_MAX], base[SPOOLMAN_PATH_MAX]; int port;
    const char *error = NULL;
    spoolman_catalog_fill fill = {malloc(sizeof(spoolman_spool) * SPOOLMAN_CATALOG_MAX), 0, 0};
    if (!fill.list) error = "Out of memory";
    else if (!spoolman_parse_url(url, host, sizeof(host), &port, base, sizeof(base))) error = "Invalid URL";
    else {
        spoolman_status_t cy; memset(&cy, 0, sizeof(cy));
        cy.reason = SPOOLMAN_R_OK;
        spoolman_read_pages(host, port, base, auth, monotonic_ms() + spoolman_cycle_max_ms, &cy, spoolman_catalog_spool, &fill);
        if (cy.reason != SPOOLMAN_R_OK) error = spoolman_reason_text(cy.reason);
    }
    pthread_mutex_lock(&spoolman_catalog_mutex);
    if (error) snprintf(spoolman_catalog_error, sizeof(spoolman_catalog_error), "%s", error);
    else {
        spoolman_catalog_error[0] = 0; spoolman_catalog_count = fill.count; spoolman_catalog_truncated = fill.truncated;
        memcpy(spoolman_catalog, fill.list, (size_t)fill.count * sizeof(fill.list[0]));
    }
    spoolman_catalog_loading = 0;
    pthread_mutex_unlock(&spoolman_catalog_mutex);
    free(fill.list);
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
    /* The builder does not grow: room for the worst case of every entry (a name and a material of escapes only). */
    size_t cap = 1024 + (size_t)spoolman_catalog_count * 960;
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
    json_builder_printf(&b, "],\"truncated\":%s}\n", spoolman_catalog_truncated ? "true" : "false");
    pthread_mutex_unlock(&spoolman_catalog_mutex);
    if (b.failed) spools_fail(fd, 500, "Catalog response is too large");
    else respond(fd, 200, "OK", "application/json; charset=utf-8", b.data, b.length);
    free(b.data);
}
