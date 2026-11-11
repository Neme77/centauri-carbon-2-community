#ifndef CC2_SCREW_HISTORY_H
#define CC2_SCREW_HISTORY_H

/* Persistent screw-tramming history (issue #329). Every successfully completed
 * session job (start, single-corner re-probe, four-corner check) is recorded
 * with the raw "Result is z=" value of each corner, so earlier sessions are
 * still there after a reload, the end of a session or a restart. A completed
 * one-shot screws:measure is kept as its own entry, marked quick: it has no
 * session and no settle, so its values are not mixed with session values. The
 * newest SCREW_HISTORY_SESSIONS sessions and SCREW_HISTORY_QUICK quick entries
 * are kept in a small line-based text file, written atomically after each
 * change. */

#include "control.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#define SCREW_HISTORY_SESSIONS 20
#define SCREW_HISTORY_QUICK 10
#define SCREW_HISTORY_MEASUREMENTS 64
#define SCREW_HISTORY_MAGIC "cc2-screw-history 1"

typedef struct {
    time_t time;
    int kind, corner, twice;
    unsigned have, have2; /* bit i: z[i] / z2[i] present */
    double z[CONTROL_SCREW_CORNERS], z2[CONTROL_SCREW_CORNERS];
} screw_history_measurement;

typedef struct {
    time_t started, ended; /* ended 0: the session is still open */
    int quick;             /* a one-shot screws:measure, not a session */
    int count;
    screw_history_measurement m[SCREW_HISTORY_MEASUREMENTS];
} screw_history_session;

typedef struct {
    int count; /* oldest first */
    screw_history_session s[SCREW_HISTORY_SESSIONS + SCREW_HISTORY_QUICK];
} screw_history_store;

static const char *const screw_history_kinds[] = {"start", "probe", "check", "quick"};

/* One "Result is z=" per PROBE command, assigned to the corner of the last
 * in-range "probe at X,Y" before it (as lib/screws.ts probeResults does).
 * The first result of a corner goes to z, the second to z2. Returns the
 * number of results used. */
static int screw_history_parse(const char *text, size_t length, screw_history_measurement *out) {
    int corner = -1, used = 0;
    size_t i = 0;
    while (i < length) {
        size_t end = i;
        while (end < length && text[end] != '\n') end++;
        char line[256];
        size_t n = end - i < sizeof(line) - 1 ? end - i : sizeof(line) - 1;
        memcpy(line, text + i, n);
        line[n] = 0;
        for (char *p = line; *p; p++) {
            if (!strncasecmp(p, "probe at ", 9)) {
                char *after = NULL;
                double x = strtod(p + 9, &after), y;
                if (after && after != p + 9 && *after == ',') {
                    char *rest = NULL;
                    y = strtod(after + 1, &rest);
                    if (rest && rest != after + 1 && isfinite(x) && isfinite(y)) {
                        int at = control_screws_corner_at(x, y);
                        if (at >= 0) corner = at;
                    }
                }
                break;
            }
            if (!strncasecmp(p, "Result is z=", 12)) {
                char *after = NULL;
                double z = strtod(p + 12, &after);
                if (corner >= 0 && after && after != p + 12 && isfinite(z) && fabs(z) < 100.0) {
                    unsigned bit = 1u << corner;
                    if (!(out->have & bit)) { out->z[corner] = z; out->have |= bit; }
                    else if (!(out->have2 & bit)) { out->z2[corner] = z; out->have2 |= bit; }
                    else { out->z[corner] = out->z2[corner]; out->z2[corner] = z; }
                    used++;
                }
                break;
            }
        }
        i = end + 1;
    }
    return used;
}

static screw_history_session *screw_history_open(screw_history_store *h) {
    return h->count && !h->s[h->count - 1].ended && !h->s[h->count - 1].quick ? &h->s[h->count - 1] : NULL;
}

/* Appends an entry; at its type's limit the oldest entry of that type goes, so
 * quick measurements never push sessions out (or the other way round). */
static screw_history_session *screw_history_new_session(screw_history_store *h, time_t started, int quick) {
    int same = 0, oldest = -1;
    for (int i = 0; i < h->count; i++)
        if (!h->s[i].quick == !quick && same++ == 0) oldest = i;
    if (same >= (quick ? SCREW_HISTORY_QUICK : SCREW_HISTORY_SESSIONS) && oldest >= 0) {
        memmove(&h->s[oldest], &h->s[oldest + 1], sizeof(h->s[0]) * (size_t)(h->count - oldest - 1));
        h->count--;
    }
    screw_history_session *s = &h->s[h->count++];
    memset(s, 0, sizeof(*s));
    s->started = started;
    s->quick = quick ? 1 : 0;
    return s;
}

/* Adds a completed job's results. A start opens a new session (closing an open
 * one); other jobs join the open session of the same start, or open one if the
 * history has none (for example after a history clear). Returns 1 when
 * something was recorded and the store needs saving. */
static int screw_history_record(screw_history_store *h, const control_screws_job *job,
                                const char *text, size_t length, time_t now) {
    screw_history_measurement m;
    memset(&m, 0, sizeof(m));
    if (!job || job->kind < CONTROL_SCREWS_JOB_START || job->kind > CONTROL_SCREWS_JOB_QUICK) return 0;
    if (!screw_history_parse(text, length, &m)) return 0;
    if (job->kind == CONTROL_SCREWS_JOB_PROBE) {
        if (job->corner < 0 || job->corner >= CONTROL_SCREW_CORNERS) return 0;
        unsigned bit = 1u << job->corner;
        m.have &= bit;
        m.have2 &= bit;
        if (!m.have) return 0;
    }
    if (!job->twice) m.have2 = 0;
    m.time = now;
    m.kind = job->kind;
    m.corner = job->kind == CONTROL_SCREWS_JOB_PROBE ? job->corner : -1;
    m.twice = job->twice ? 1 : 0;
    screw_history_session *s = screw_history_open(h);
    if (job->kind == CONTROL_SCREWS_JOB_QUICK) {
        /* A one-shot measurement ends any session; it is its own closed entry. */
        if (s) s->ended = now;
        s = screw_history_new_session(h, job->started ? job->started : now, 1);
        s->ended = now;
    } else if (job->kind == CONTROL_SCREWS_JOB_START || !s || s->started != job->started) {
        if (s) s->ended = now;
        s = screw_history_new_session(h, job->started ? job->started : now, 0);
    }
    if (s->count == SCREW_HISTORY_MEASUREMENTS) {
        memmove(&s->m[0], &s->m[1], sizeof(s->m[0]) * (SCREW_HISTORY_MEASUREMENTS - 1));
        s->count--;
    }
    s->m[s->count++] = m;
    return 1;
}

/* Closes the open session once the tramming session is over. Returns 1 when
 * the store changed. */
static int screw_history_tick(screw_history_store *h, int session_active, time_t now) {
    screw_history_session *s = screw_history_open(h);
    if (!s || session_active) return 0;
    s->ended = now;
    return 1;
}

static int screw_history_encode(const screw_history_store *h, FILE *f) {
    if (fprintf(f, "%s\n", SCREW_HISTORY_MAGIC) < 0) return 0;
    for (int i = 0; i < h->count; i++) {
        const screw_history_session *s = &h->s[i];
        if (fprintf(f, "S %lld %lld %d\n", (long long)s->started, (long long)s->ended, s->quick) < 0) return 0;
        for (int k = 0; k < s->count; k++) {
            const screw_history_measurement *m = &s->m[k];
            if (fprintf(f, "M %lld %d %d %d %u %u", (long long)m->time, m->kind, m->corner, m->twice,
                        m->have, m->have2) < 0) return 0;
            for (int c = 0; c < CONTROL_SCREW_CORNERS; c++)
                if (fprintf(f, " %.6f", m->have & (1u << c) ? m->z[c] : 0.0) < 0) return 0;
            for (int c = 0; c < CONTROL_SCREW_CORNERS; c++)
                if (fprintf(f, " %.6f", m->have2 & (1u << c) ? m->z2[c] : 0.0) < 0) return 0;
            if (fputc('\n', f) == EOF) return 0;
        }
    }
    return 1;
}

/* Reads the file; a missing file is an empty history. A malformed file is
 * ignored (empty history) and replaced on the next save. An open session left
 * by a restart is closed at its last measurement, because the tramming
 * session itself does not survive a restart. Returns 1 on a readable file. */
static int screw_history_load(screw_history_store *h, const char *path) {
    memset(h, 0, sizeof(*h));
    FILE *f = fopen(path, "r");
    if (!f) return errno == ENOENT;
    char line[512];
    int ok = fgets(line, sizeof(line), f) && !strncmp(line, SCREW_HISTORY_MAGIC "\n", sizeof(SCREW_HISTORY_MAGIC));
    while (ok && fgets(line, sizeof(line), f)) {
        long long a, b;
        if (line[0] == 'S') {
            /* "S started ended [quick]": files written before quick entries have two fields. */
            int quick = 0, fields = sscanf(line, "S %lld %lld %d", &a, &b, &quick);
            if (fields < 2 || a <= 0 || b < 0 || quick < 0 || quick > 1) { ok = 0; break; }
            screw_history_session *s = screw_history_new_session(h, (time_t)a, quick);
            s->ended = (time_t)b;
        } else if (line[0] == 'M') {
            screw_history_measurement m;
            memset(&m, 0, sizeof(m));
            double *z = m.z, *z2 = m.z2;
            if (!h->count || sscanf(line, "M %lld %d %d %d %u %u %lf %lf %lf %lf %lf %lf %lf %lf",
                                    &a, &m.kind, &m.corner, &m.twice, &m.have, &m.have2,
                                    &z[0], &z[1], &z[2], &z[3], &z2[0], &z2[1], &z2[2], &z2[3]) != 14 ||
                m.kind < CONTROL_SCREWS_JOB_START || m.kind > CONTROL_SCREWS_JOB_QUICK ||
                m.corner < -1 || m.corner >= CONTROL_SCREW_CORNERS || m.have > 15 || m.have2 > 15) { ok = 0; break; }
            m.time = (time_t)a;
            screw_history_session *s = &h->s[h->count - 1];
            if (s->count == SCREW_HISTORY_MEASUREMENTS) {
                memmove(&s->m[0], &s->m[1], sizeof(s->m[0]) * (SCREW_HISTORY_MEASUREMENTS - 1));
                s->count--;
            }
            s->m[s->count++] = m;
        } else { ok = 0; break; }
    }
    fclose(f);
    if (!ok) { memset(h, 0, sizeof(*h)); return 0; }
    screw_history_session *open = screw_history_open(h);
    if (open) open->ended = open->count ? open->m[open->count - 1].time : open->started;
    return 1;
}

static int screw_history_save(const screw_history_store *h, const char *path) {
    char temporary[600];
    if (snprintf(temporary, sizeof(temporary), "%s.new", path) >= (int)sizeof(temporary)) return 0;
    unlink(temporary);
    int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
    if (fd < 0) return 0;
    FILE *f = fdopen(fd, "w");
    if (!f) { close(fd); unlink(temporary); return 0; }
    int ok = screw_history_encode(h, f);
    if (fflush(f)) ok = 0;
    if (fsync(fileno(f))) ok = 0;
    if (fclose(f)) ok = 0;
    if (ok && rename(temporary, path)) ok = 0;
    if (!ok) unlink(temporary);
    return ok;
}

static int screw_history_append(char **out, size_t *len, size_t *cap, const char *format, ...)
    __attribute__((format(printf, 4, 5)));
static int screw_history_append(char **out, size_t *len, size_t *cap, const char *format, ...) {
    for (;;) {
        va_list args;
        va_start(args, format);
        int n = vsnprintf(*out + *len, *cap - *len, format, args);
        va_end(args);
        if (n < 0) return 0;
        if ((size_t)n < *cap - *len) { *len += (size_t)n; return 1; }
        size_t next = *cap * 2 + (size_t)n;
        char *grown = realloc(*out, next);
        if (!grown) return 0;
        *out = grown;
        *cap = next;
    }
}

static int screw_history_corners_json(char **out, size_t *len, size_t *cap, unsigned have, const double *z) {
    if (!screw_history_append(out, len, cap, "{")) return 0;
    int first = 1;
    for (int c = 0; c < CONTROL_SCREW_CORNERS; c++) {
        if (!(have & (1u << c))) continue;
        if (!screw_history_append(out, len, cap, "%s\"%s\":%.6f", first ? "" : ",", control_screws_corner_id(c), z[c]))
            return 0;
        first = 0;
    }
    return screw_history_append(out, len, cap, "}");
}

/* {"sessions":[...]} newest session first, measurements oldest first. Returns
 * a malloc'd string, or NULL when out of memory. */
static char *screw_history_json(const screw_history_store *h, size_t *length) {
    size_t cap = 4096, len = 0;
    char *out = malloc(cap);
    if (!out) return NULL;
    out[0] = 0;
    int ok = screw_history_append(&out, &len, &cap, "{\"sessions\":[");
    for (int i = h->count - 1; ok && i >= 0; i--) {
        const screw_history_session *s = &h->s[i];
        ok = screw_history_append(&out, &len, &cap, "%s{\"kind\":\"%s\",\"started\":%lld,\"ended\":",
                                  i == h->count - 1 ? "" : ",", s->quick ? "quick" : "session", (long long)s->started);
        if (ok) ok = s->ended ? screw_history_append(&out, &len, &cap, "%lld", (long long)s->ended)
                              : screw_history_append(&out, &len, &cap, "null");
        if (ok) ok = screw_history_append(&out, &len, &cap, ",\"measurements\":[");
        for (int k = 0; ok && k < s->count; k++) {
            const screw_history_measurement *m = &s->m[k];
            const char *corner = control_screws_corner_id(m->corner);
            ok = screw_history_append(&out, &len, &cap, "%s{\"time\":%lld,\"kind\":\"%s\",\"corner\":", k ? "," : "",
                                      (long long)m->time, screw_history_kinds[m->kind]);
            if (ok) ok = corner ? screw_history_append(&out, &len, &cap, "\"%s\"", corner)
                                : screw_history_append(&out, &len, &cap, "null");
            if (ok) ok = screw_history_append(&out, &len, &cap, ",\"twice\":%s,\"z\":", m->twice ? "true" : "false");
            if (ok) ok = screw_history_corners_json(&out, &len, &cap, m->have, m->z);
            if (ok && m->twice && m->have2) {
                ok = screw_history_append(&out, &len, &cap, ",\"z2\":");
                if (ok) ok = screw_history_corners_json(&out, &len, &cap, m->have2, m->z2);
            }
            if (ok) ok = screw_history_append(&out, &len, &cap, "}");
        }
        if (ok) ok = screw_history_append(&out, &len, &cap, "]}");
    }
    if (ok) ok = screw_history_append(&out, &len, &cap, "]}\n");
    if (!ok) { free(out); return NULL; }
    if (length) *length = len;
    return out;
}

#endif
