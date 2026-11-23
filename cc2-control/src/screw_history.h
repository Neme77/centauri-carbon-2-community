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

/* The single probes behind a result ("probe at X,Y is z=..."): up to six per corner (2 x 3), kept only while the average of
 * a PROBE's probes is that PROBE's own Result (within SCREW_PROBE_TOLERANCE_MM); otherwise the corner keeps none. */
#define SCREW_PROBES_MAX 6
#define SCREW_PROBE_TOLERANCE_MM 0.001
typedef struct {
    time_t time;
    int kind, corner, twice;
    unsigned have, have2; /* bit i: z[i] / z2[i] present */
    double z[CONTROL_SCREW_CORNERS], z2[CONTROL_SCREW_CORNERS];
    unsigned have_p;      /* bit i: the probes of corner i are known (pn[i] of them in p[i]) */
    unsigned char pn[CONTROL_SCREW_CORNERS];
    float p[CONTROL_SCREW_CORNERS][SCREW_PROBES_MAX]; /* float: micrometres need seven digits, the store stays small */
    int bed;              /* bed target in C the job measured at; -1: not recorded (older entries) */
    unsigned have_ist;    /* bit i: ist[i] present; 0 for entries from before the actual temperatures */
    double ist[3];        /* actual temperatures in C at the last result: bed, nozzle, chamber */
} screw_history_measurement;

/* Actual temperatures read while a measurement runs: bit i of have says ist-value v[i] is known
 * (0 bed, 1 nozzle, 2 chamber). A sensor that gave no value leaves its bit out. */
typedef struct { unsigned have; double v[3]; } screw_ist;
#define SCREW_IST_MAX 400.0 /* above this a stored temperature is taken as damaged and ignored */
static const char *const screw_ist_names[3] = {"bed", "nozzle", "chamber"};

/* Number of "Result is z=" lines in the console output: one per PROBE command, so the
 * main loop can take the temperatures whenever a new result has arrived. */
static inline int screw_history_count_results(const char *text, size_t length) {
    static const char key[] = "Result is z=";
    int n = 0;
    for (size_t i = 0; i + sizeof(key) - 1 <= length; i++)
        if (text[i] == 'R' && !strncasecmp(text + i, key, sizeof(key) - 1)) n++;
    return n;
}

typedef struct {
    time_t started, ended; /* ended 0: the session is still open */
    int quick;             /* a one-shot screws:measure, not a session */
    int end_reason;        /* CONTROL_SCREWS_END_*; NONE while open, for quick entries and older files */
    int centres;           /* 1: probed at the screw centres; 0: an older file, probed at 30/30, 230/30, 230/225, 30/225 */
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
static inline int screw_history_parse(const char *text, size_t length, screw_history_measurement *out) {
    int corner = -1, used = 0;
    double sample[16];
    int samples = 0;
    unsigned bad = 0; /* corners whose probes cannot be trusted (do not add up to their Result, or too many) */
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
                        corner = at; /* a probe outside the four screws (the Z reference) leaves no corner behind */
                        if (at < 0) samples = 0;
                        /* The probe's own height: " is z=<value>". */
                        const char *is = strstr(rest, "is z=");
                        char *vend = NULL;
                        double v = is ? strtod(is + 5, &vend) : 0.0;
                        if (at >= 0 && is && vend != is + 5 && isfinite(v) && fabs(v) < 100.0 && samples < (int)(sizeof(sample) / sizeof(sample[0])))
                            sample[samples++] = v;
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
                    /* The probes of this PROBE: their average must be its Result. */
                    double sum = 0.0;
                    for (int k = 0; k < samples; k++) sum += sample[k];
                    if (samples > 0 && fabs(sum / samples - z) <= SCREW_PROBE_TOLERANCE_MM &&
                        out->pn[corner] + samples <= SCREW_PROBES_MAX && !(bad & (1u << corner))) {
                        for (int k = 0; k < samples; k++) out->p[corner][out->pn[corner]++] = (float)sample[k];
                        out->have_p |= 1u << corner;
                    } else {
                        bad |= 1u << corner;
                        out->have_p &= ~(1u << corner);
                        out->pn[corner] = 0;
                    }
                }
                samples = 0;
                break;
            }
        }
        i = end + 1;
    }
    return used;
}

static inline screw_history_session *screw_history_open(screw_history_store *h) {
    return h->count && !h->s[h->count - 1].ended && !h->s[h->count - 1].quick ? &h->s[h->count - 1] : NULL;
}

/* Appends an entry; at its type's limit the oldest entry of that type goes, so
 * quick measurements never push sessions out (or the other way round). */
static inline screw_history_session *screw_history_new_session(screw_history_store *h, time_t started, int quick) {
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
    s->centres = 1; /* new entries are probed at the screw centres; reading an older file clears it */
    s->quick = quick ? 1 : 0;
    return s;
}

/* Adds a completed job's results. A start opens a new session (closing an open
 * one); other jobs join the open session of the same start, or open one if the
 * history has none (for example after a history clear). Returns 1 when
 * something was recorded and the store needs saving. */
static inline int screw_history_record_ist(screw_history_store *h, const control_screws_job *job,
                                    const char *text, size_t length, time_t now, const screw_ist *ist) {
    screw_history_measurement m;
    memset(&m, 0, sizeof(m));
    if (!job || job->kind < CONTROL_SCREWS_JOB_START || job->kind > CONTROL_SCREWS_JOB_QUICK) return 0;
    if (!screw_history_parse(text, length, &m)) return 0;
    if (job->kind == CONTROL_SCREWS_JOB_PROBE) {
        if (job->corner < 0 || job->corner >= CONTROL_SCREW_CORNERS) return 0;
        unsigned bit = 1u << job->corner;
        m.have &= bit;
        m.have2 &= bit;
        m.have_p &= bit;
        for (int c = 0; c < CONTROL_SCREW_CORNERS; c++) if (!(m.have_p & (1u << c))) m.pn[c] = 0;
        if (!m.have) return 0;
    }
    /* A corner measurement (quick, one corner) must have a result for its corner; front left comes with it. */
    if (job->kind == CONTROL_SCREWS_JOB_QUICK && job->corner >= 0 &&
        (job->corner >= CONTROL_SCREW_CORNERS || !(m.have & (1u << job->corner)))) return 0;
    if (!job->twice) m.have2 = 0;
    m.time = now;
    m.kind = job->kind;
    m.corner = job->kind == CONTROL_SCREWS_JOB_PROBE || (job->kind == CONTROL_SCREWS_JOB_QUICK && job->corner >= 0) ? job->corner : -1;
    m.twice = job->twice ? 1 : 0;
    m.bed = job->bed_set ? job->bed : -1;
    if (ist)
        for (int i = 0; i < 3; i++)
            if ((ist->have & (1u << i)) && isfinite(ist->v[i]) && ist->v[i] >= -50.0 && ist->v[i] <= SCREW_IST_MAX) {
                m.have_ist |= 1u << i;
                m.ist[i] = ist->v[i];
            }
    screw_history_session *s = screw_history_open(h);
    if (job->kind == CONTROL_SCREWS_JOB_QUICK) {
        /* A one-shot measurement ends any session; it is its own closed entry. */
        if (s) { s->ended = now; if (!s->end_reason) s->end_reason = CONTROL_SCREWS_END_MEASURE; }
        s = screw_history_new_session(h, job->started ? job->started : now, 1);
        s->ended = now;
    } else if (job->kind == CONTROL_SCREWS_JOB_START || !s || s->started != job->started) {
        if (s) { s->ended = now; if (!s->end_reason) s->end_reason = CONTROL_SCREWS_END_NEW_SESSION; }
        s = screw_history_new_session(h, job->started ? job->started : now, 0);
    }
    if (s->count == SCREW_HISTORY_MEASUREMENTS) {
        memmove(&s->m[0], &s->m[1], sizeof(s->m[0]) * (SCREW_HISTORY_MEASUREMENTS - 1));
        s->count--;
    }
    s->m[s->count++] = m;
    return 1;
}

static inline int screw_history_record(screw_history_store *h, const control_screws_job *job,
                                const char *text, size_t length, time_t now) {
    return screw_history_record_ist(h, job, text, length, now, NULL);
}

/* Closes the open session once the tramming session is over, with the reason it
 * ended (CONTROL_SCREWS_END_*). Returns 1 when the store changed. */
static inline int screw_history_tick(screw_history_store *h, int session_active, time_t now, int reason) {
    screw_history_session *s = screw_history_open(h);
    if (!s || session_active) return 0;
    s->ended = now;
    s->end_reason = control_screws_end_name(reason) ? reason : CONTROL_SCREWS_END_NONE;
    return 1;
}

static inline int screw_history_encode(const screw_history_store *h, FILE *f) {
    if (fprintf(f, "%s\n", SCREW_HISTORY_MAGIC) < 0) return 0;
    for (int i = 0; i < h->count; i++) {
        const screw_history_session *s = &h->s[i];
        /* The end reason is an optional fourth field: earlier versions read three and ignore it. */
        const char *why = control_screws_end_name(s->end_reason);
        /* A fifth field marks the measuring points: "c" for the screw centres. It needs the fourth, so a session
         * without a reason writes "-" there (an unknown reason is no reason, for this and for earlier versions).
         * Sessions of older files have no fifth field and keep it that way: their points are the earlier ones. */
        if (fprintf(f, "S %lld %lld %d%s%s%s\n", (long long)s->started, (long long)s->ended, s->quick,
                    why || s->centres ? " " : "", why ? why : s->centres ? "-" : "", s->centres ? " c" : "") < 0) return 0;
        for (int k = 0; k < s->count; k++) {
            const screw_history_measurement *m = &s->m[k];
            if (fprintf(f, "M %lld %d %d %d %u %u", (long long)m->time, m->kind, m->corner, m->twice,
                        m->have, m->have2) < 0) return 0;
            for (int c = 0; c < CONTROL_SCREW_CORNERS; c++)
                if (fprintf(f, " %.6f", m->have & (1u << c) ? m->z[c] : 0.0) < 0) return 0;
            for (int c = 0; c < CONTROL_SCREW_CORNERS; c++)
                if (fprintf(f, " %.6f", m->have2 & (1u << c) ? m->z2[c] : 0.0) < 0) return 0;
            /* Optional trailing field: earlier versions read the first fourteen and ignore it. */
            if (m->bed >= 0 && fprintf(f, " %d", m->bed) < 0) return 0;
            /* Optional trailing field after the bed target: " T<bed>,<nozzle>,<chamber>", "-" for a sensor
             * without a value. Earlier versions ignore it. A later field (profile) can follow it. */
            if (m->have_ist) {
                if (fputs(" T", f) == EOF) return 0;
                for (int c = 0; c < 3; c++) {
                    int w = m->have_ist & (1u << c) ? fprintf(f, "%s%.1f", c ? "," : "", m->ist[c])
                                                    : fprintf(f, "%s-", c ? "," : "");
                    if (w < 0) return 0;
                }
            }
            /* Optional trailing field after the temperatures: " P<corner>:<z>,<z>,...;<corner>:..." the single probes (up to six per
             * corner). Earlier versions ignore it. */
            if (m->have_p) {
                if (fputs(" P", f) == EOF) return 0;
                for (int c = 0, first = 1; c < CONTROL_SCREW_CORNERS; c++) {
                    if (!(m->have_p & (1u << c))) continue;
                    if (fprintf(f, "%s%d:", first ? "" : ";", c) < 0) return 0;
                    for (int k = 0; k < m->pn[c]; k++)
                        if (fprintf(f, "%s%.6f", k ? "," : "", m->p[c][k]) < 0) return 0;
                    first = 0;
                }
            }
            if (fputc('\n', f) == EOF) return 0;
        }
    }
    return 1;
}

/* Reads the file; a missing file is an empty history. A malformed file is
 * ignored (empty history) and replaced on the next save. An open session left
 * by a restart is closed at its last measurement, because the tramming
 * session itself does not survive a restart. Returns 1 on a readable file. */
static inline int screw_history_load(screw_history_store *h, const char *path) {
    memset(h, 0, sizeof(*h));
    FILE *f = fopen(path, "r");
    if (!f) return errno == ENOENT;
    char line[1024];
    int ok = fgets(line, sizeof(line), f) && !strncmp(line, SCREW_HISTORY_MAGIC "\n", sizeof(SCREW_HISTORY_MAGIC));
    while (ok && fgets(line, sizeof(line), f)) {
        long long a, b;
        if (line[0] == 'S') {
            /* "S started ended [quick [reason]]": files written before quick entries have two
             * fields, files written before end reasons three. An unknown reason is no reason. */
            char why[32] = "", points[8] = "";
            int quick = 0, fields = sscanf(line, "S %lld %lld %d %31s %7s", &a, &b, &quick, why, points);
            if (fields < 2 || a <= 0 || b < 0 || quick < 0 || quick > 1) { ok = 0; break; }
            screw_history_session *s = screw_history_new_session(h, (time_t)a, quick);
            s->ended = (time_t)b;
            s->end_reason = fields >= 4 ? control_screws_end_from_name(why) : CONTROL_SCREWS_END_NONE;
            s->centres = fields == 5 && !strcmp(points, "c");
        } else if (line[0] == 'M') {
            screw_history_measurement m;
            memset(&m, 0, sizeof(m));
            double *z = m.z, *z2 = m.z2;
            int used = 0;
            if (!h->count || sscanf(line, "M %lld %d %d %d %u %u %lf %lf %lf %lf %lf %lf %lf %lf%n",
                                    &a, &m.kind, &m.corner, &m.twice, &m.have, &m.have2,
                                    &z[0], &z[1], &z[2], &z[3], &z2[0], &z2[1], &z2[2], &z2[3], &used) != 14 ||
                m.kind < CONTROL_SCREWS_JOB_START || m.kind > CONTROL_SCREWS_JOB_QUICK ||
                m.corner < -1 || m.corner >= CONTROL_SCREW_CORNERS || m.have > 15 || m.have2 > 15) { ok = 0; break; }
            /* The bed target, written since it can be chosen; lines without it have none. */
            int bed = -1;
            m.bed = sscanf(line + used, " %d", &bed) == 1 && bed >= 0 && bed <= CONTROL_SCREWS_BED_MAX ? bed : -1;
            /* The actual temperatures: " T<bed>,<nozzle>,<chamber>", each a number or "-". A damaged
             * or implausible field is dropped as a whole; the measurement stays. */
            const char *tfield = strstr(line + used, " T");
            if (tfield) {
                const char *p = tfield + 2;
                screw_ist got = {0, {0, 0, 0}};
                int good = 1;
                for (int c = 0; c < 3 && good; c++) {
                    if (c) { if (*p == ',') p++; else good = 0; }
                    if (!good) break;
                    if (*p == '-') { p++; continue; }
                    char *after = NULL;
                    double v = strtod(p, &after);
                    if (after == p || !isfinite(v) || v < -50.0 || v > SCREW_IST_MAX) good = 0;
                    else { got.v[c] = v; got.have |= 1u << c; p = after; }
                }
                if (good) { m.have_ist = got.have; for (int c = 0; c < 3; c++) m.ist[c] = got.v[c]; }
            }
            /* The single probes: " P0:z,z,z;1:..."; a damaged field is dropped as a whole, the measurement stays. */
            const char *pfield = strstr(line + used, " P");
            if (pfield) {
                const char *q = pfield + 2;
                screw_history_measurement got;
                memset(&got, 0, sizeof(got));
                int good = 1;
                while (good && *q && *q != '\n') {
                    char *after = NULL;
                    long c = strtol(q, &after, 10);
                    if (after == q || *after != ':' || c < 0 || c >= CONTROL_SCREW_CORNERS || (got.have_p & (1u << c))) { good = 0; break; }
                    q = after + 1;
                    int n = 0;
                    for (;;) {
                        double v = strtod(q, &after);
                        if (after == q || !isfinite(v) || fabs(v) >= 100.0 || n >= SCREW_PROBES_MAX) { good = 0; break; }
                        got.p[c][n++] = (float)v;
                        q = after;
                        if (*q == ',') { q++; continue; }
                        break;
                    }
                    if (!good) break;
                    got.pn[c] = (unsigned char)n;
                    got.have_p |= 1u << c;
                    if (*q == ';') q++;
                }
                if (good && (got.have_p & ~m.have) == 0) {
                    m.have_p = got.have_p;
                    memcpy(m.pn, got.pn, sizeof(m.pn));
                    memcpy(m.p, got.p, sizeof(m.p));
                }
            }
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
    if (open) {
        open->ended = open->count ? open->m[open->count - 1].time : open->started;
        open->end_reason = CONTROL_SCREWS_END_RESTART;
    }
    return 1;
}

static inline int screw_history_save(const screw_history_store *h, const char *path) {
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

static inline int screw_history_append(char **out, size_t *len, size_t *cap, const char *format, ...)
    __attribute__((format(printf, 4, 5)));
static inline int screw_history_append(char **out, size_t *len, size_t *cap, const char *format, ...) {
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

static inline int screw_history_corners_json(char **out, size_t *len, size_t *cap, unsigned have, const double *z) {
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
static inline char *screw_history_json(const screw_history_store *h, size_t *length) {
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
            /* Only for measurements that recorded it; older entries stay as they were. */
            if (ok && m->bed >= 0) ok = screw_history_append(&out, &len, &cap, ",\"bed\":%d", m->bed);
            /* The actual temperatures, only the known ones; after the bed target so earlier fields keep their place. */
            if (ok && m->have_ist) {
                ok = screw_history_append(&out, &len, &cap, ",\"ist\":{");
                for (int c = 0, first = 1; ok && c < 3; c++) {
                    if (!(m->have_ist & (1u << c))) continue;
                    ok = screw_history_append(&out, &len, &cap, "%s\"%s\":%.1f", first ? "" : ",", screw_ist_names[c], m->ist[c]);
                    first = 0;
                }
                if (ok) ok = screw_history_append(&out, &len, &cap, "}");
            }
            /* The single probes of the corners that have them, after the temperatures. */
            if (ok && m->have_p) {
                ok = screw_history_append(&out, &len, &cap, ",\"probes\":{");
                for (int c = 0, first = 1; ok && c < CONTROL_SCREW_CORNERS; c++) {
                    if (!(m->have_p & (1u << c))) continue;
                    ok = screw_history_append(&out, &len, &cap, "%s\"%s\":[", first ? "" : ",", control_screws_corner_id(c));
                    for (int k = 0; ok && k < m->pn[c]; k++) ok = screw_history_append(&out, &len, &cap, "%s%.6f", k ? "," : "", m->p[c][k]);
                    if (ok) ok = screw_history_append(&out, &len, &cap, "]");
                    first = 0;
                }
                if (ok) ok = screw_history_append(&out, &len, &cap, "}");
            }
            if (ok) ok = screw_history_append(&out, &len, &cap, "}");
        }
        /* After the measurements, so the fields before them keep their order. */
        const char *why = control_screws_end_name(s->end_reason);
        /* "points": where the corners were probed, so that values of the earlier points are never read as today's. */
        if (ok) ok = why ? screw_history_append(&out, &len, &cap, "],\"end_reason\":\"%s\"", why)
                         : screw_history_append(&out, &len, &cap, "],\"end_reason\":null");
        if (ok) ok = screw_history_append(&out, &len, &cap, ",\"points\":\"%s\"}", s->centres ? "screws" : "old");
    }
    if (ok) ok = screw_history_append(&out, &len, &cap, "]}\n");
    if (!ok) { free(out); return NULL; }
    if (length) *length = len;
    return out;
}

#endif
