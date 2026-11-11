#include "../src/screw_history.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static void expect(int condition, const char *label) {
    printf("%s: %s\n", condition ? "PASS" : "FAIL", label);
    if (!condition) failures++;
}

/* Console output of one PROBE SAMPLES=3 at a corner, as the CC2 firmware prints it. */
static void probe(char *out, size_t cap, double x, double y, double z) {
    size_t n = strlen(out);
    snprintf(out + n, cap - n,
             "// probe at %.3f,%.3f is z=%.6f\n// probe at %.3f,%.3f is z=%.6f\n// probe at %.3f,%.3f is z=%.6f\n"
             "// Result is z=%.6f\n",
             x, y, z - 0.0004, x, y, z + 0.0003, x, y, z, z);
}

static int contains(const char *text, const char *needle) { return text && strstr(text, needle) != NULL; }

static screw_history_store h, loaded;

int main(void) {
    char out[8192] = "";
    probe(out, sizeof(out), 29.997, 30.0, -0.6818);
    probe(out, sizeof(out), 230.0, 30.0, -0.6758);
    probe(out, sizeof(out), 230.0, 225.0, -0.8698);
    probe(out, sizeof(out), 29.997, 225.0, -0.7508);
    screw_history_measurement m;
    memset(&m, 0, sizeof(m));
    expect(screw_history_parse(out, strlen(out), &m) == 4 && m.have == 15 && !m.have2 &&
               m.z[0] == -0.6818 && m.z[1] == -0.6758 && m.z[2] == -0.8698 && m.z[3] == -0.7508,
           "four corner results are taken from Result is z, assigned by probe position");

    char twice[8192] = "// probe at 100.000,100.000 is z=-0.5\n// Result is z=-0.5\n";
    probe(twice, sizeof(twice), 230.0, 30.0, -0.6758);
    probe(twice, sizeof(twice), 230.0, 30.0, -0.6741);
    memset(&m, 0, sizeof(m));
    expect(screw_history_parse(twice, strlen(twice), &m) == 2 && m.have == 2 && m.have2 == 2 &&
               m.z[1] == -0.6758 && m.z2[1] == -0.6741,
           "a twice-probed corner keeps first and second result; a result before any corner probe is ignored");

    memset(&h, 0, sizeof(h));
    time_t t0 = 1762800000;
    control_screws_job start = {CONTROL_SCREWS_JOB_START, -1, 0, t0};
    expect(screw_history_record(&h, &start, out, strlen(out), t0 + 120) && h.count == 1 && h.s[0].started == t0 &&
               !h.s[0].ended && h.s[0].count == 1,
           "a completed start opens a session in the history");
    control_screws_job fr = {CONTROL_SCREWS_JOB_PROBE, 1, 1, t0};
    expect(screw_history_record(&h, &fr, twice, strlen(twice), t0 + 200) && h.count == 1 && h.s[0].count == 2 &&
               h.s[0].m[1].have == 2 && h.s[0].m[1].have2 == 2 && h.s[0].m[1].corner == 1,
           "a re-probe joins the open session and keeps only its own corner");
    control_screws_job rl = {CONTROL_SCREWS_JOB_PROBE, 3, 0, t0};
    expect(!screw_history_record(&h, &rl, twice, strlen(twice), t0 + 210) && h.s[0].count == 2,
           "a re-probe without a result for its corner records nothing");
    expect(!screw_history_record(&h, &start, "", 0, t0 + 220) && h.count == 1, "a job without results records nothing");
    expect(!screw_history_tick(&h, 1, t0 + 230) && !h.s[0].ended, "the session stays open while the tramming session runs");

    size_t length = 0;
    char *json = screw_history_json(&h, &length);
    expect(json && length == strlen(json) && contains(json, "{\"sessions\":[{\"kind\":\"session\",\"started\":1762800000,\"ended\":null,") &&
               contains(json, "{\"time\":1762800120,\"kind\":\"start\",\"corner\":null,\"twice\":false,"
                              "\"z\":{\"FL\":-0.681800,\"FR\":-0.675800,\"RR\":-0.869800,\"RL\":-0.750800}}") &&
               contains(json, "{\"time\":1762800200,\"kind\":\"probe\",\"corner\":\"FR\",\"twice\":true,"
                              "\"z\":{\"FR\":-0.675800},\"z2\":{\"FR\":-0.674100}}"),
           "JSON lists the open session with null end, raw values and z2 for a twice probe");
    free(json);

    expect(screw_history_tick(&h, 0, t0 + 300) && h.s[0].ended == t0 + 300, "the session is closed when the tramming session ends");
    expect(!screw_history_tick(&h, 0, t0 + 310), "a closed session is not closed again");

    control_screws_job check = {CONTROL_SCREWS_JOB_CHECK, -1, 0, t0 + 1000};
    expect(screw_history_record(&h, &check, out, strlen(out), t0 + 1100) && h.count == 2 && h.s[1].started == t0 + 1000,
           "a job with no open session of its start opens one (for example after a clear)");
    control_screws_job restart = {CONTROL_SCREWS_JOB_START, -1, 0, t0 + 2000};
    expect(screw_history_record(&h, &restart, out, strlen(out), t0 + 2100) && h.count == 3 && h.s[1].ended == t0 + 2100,
           "a new start closes the open session");

    json = screw_history_json(&h, &length);
    expect(json && strstr(json, "\"started\":1762802000") < strstr(json, "\"started\":1762800000"), "newest session is listed first");
    free(json);

    char dir[] = "/tmp/cc2-screw-history-XXXXXX";
    expect(mkdtemp(dir) != NULL, "temporary directory");
    char path[256];
    snprintf(path, sizeof(path), "%s/screw-history.txt", dir);
    expect(screw_history_load(&loaded, path) && loaded.count == 0, "a missing file is an empty history");
    expect(screw_history_save(&h, path), "history is saved");
    expect(screw_history_load(&loaded, path) && loaded.count == 3, "history is loaded again");
    char *a = screw_history_json(&h, NULL);
    char *b = screw_history_json(&loaded, NULL);
    /* The open session was closed at its last measurement while loading. */
    expect(a && b && contains(b, "\"started\":1762802000,\"ended\":1762802100,") &&
               strstr(a, "\"started\":1762801000") && strstr(b, "\"started\":1762801000") &&
               !strcmp(strstr(a, "\"started\":1762801000"), strstr(b, "\"started\":1762801000")),
           "a save/load round trip keeps every value; a session left open by a restart is closed");
    free(a);
    free(b);

    char temporary[300];
    snprintf(temporary, sizeof(temporary), "%s.new", path);
    FILE *stale = fopen(temporary, "w");
    if (stale) { fputs("stale", stale); fclose(stale); }
    expect(screw_history_save(&h, path) && access(temporary, F_OK) != 0, "a stale temporary file does not block saving");

    FILE *bad = fopen(path, "w");
    if (bad) { fputs(SCREW_HISTORY_MAGIC "\nS 1762800000 0\nM garbage\n", bad); fclose(bad); }
    expect(!screw_history_load(&loaded, path) && loaded.count == 0, "a malformed file is ignored");
    bad = fopen(path, "w");
    if (bad) { fputs("something else\n", bad); fclose(bad); }
    expect(!screw_history_load(&loaded, path) && loaded.count == 0, "a file without the header is ignored");
    unlink(path);
    rmdir(dir);

    memset(&h, 0, sizeof(h));
    for (int i = 0; i < SCREW_HISTORY_SESSIONS + 5; i++) {
        control_screws_job s = {CONTROL_SCREWS_JOB_START, -1, 0, t0 + i * 100};
        (void)screw_history_record(&h, &s, out, strlen(out), t0 + i * 100 + 50);
    }
    expect(h.count == SCREW_HISTORY_SESSIONS && h.s[0].started == t0 + 500, "only the newest 20 sessions are kept");
    control_screws_job last = {CONTROL_SCREWS_JOB_CHECK, -1, 0, h.s[h.count - 1].started};
    for (int i = 0; i < SCREW_HISTORY_MEASUREMENTS + 3; i++)
        (void)screw_history_record(&h, &last, out, strlen(out), t0 + 10000 + i);
    expect(h.s[h.count - 1].count == SCREW_HISTORY_MEASUREMENTS && h.s[h.count - 1].m[SCREW_HISTORY_MEASUREMENTS - 1].time == t0 + 10000 + SCREW_HISTORY_MEASUREMENTS + 2,
           "a session keeps its newest 64 measurements");

    /* One-shot screws:measure: its own closed entry, marked quick, with its own limit. */
    memset(&h, 0, sizeof(h));
    control_screws_job open_start = {CONTROL_SCREWS_JOB_START, -1, 0, t0};
    (void)screw_history_record(&h, &open_start, out, strlen(out), t0 + 100);
    control_screws_job quick = {CONTROL_SCREWS_JOB_QUICK, -1, 0, t0 + 200};
    expect(screw_history_record(&h, &quick, out, strlen(out), t0 + 400) && h.count == 2 && h.s[0].ended == t0 + 400 &&
               h.s[1].quick && h.s[1].started == t0 + 200 && h.s[1].ended == t0 + 400 && h.s[1].m[0].kind == CONTROL_SCREWS_JOB_QUICK &&
               h.s[1].m[0].have == 15,
           "a quick measurement closes an open session and is stored as its own closed quick entry");
    expect(!screw_history_tick(&h, 0, t0 + 500), "a quick entry is never treated as an open session");
    control_screws_job probe_after = {CONTROL_SCREWS_JOB_PROBE, 1, 0, t0 + 600};
    (void)screw_history_record(&h, &probe_after, twice, strlen(twice), t0 + 700);
    expect(h.count == 3 && !h.s[2].quick && h.s[2].started == t0 + 600, "a session job after a quick entry opens a new session");
    json = screw_history_json(&h, &length);
    expect(json && contains(json, "{\"kind\":\"quick\",\"started\":1762800200,\"ended\":1762800400,\"measurements\":[{\"time\":1762800400,\"kind\":\"quick\",\"corner\":null,\"twice\":false,") &&
               contains(json, "{\"kind\":\"session\",\"started\":1762800000,"),
           "JSON marks quick entries and sessions");
    free(json);
    char qdir[] = "/tmp/cc2-screw-quick-XXXXXX";
    expect(mkdtemp(qdir) != NULL, "temporary directory for quick entries");
    char qpath[256];
    snprintf(qpath, sizeof(qpath), "%s/screw-history.txt", qdir);
    expect(screw_history_save(&h, qpath) && screw_history_load(&loaded, qpath) && loaded.count == 3 && loaded.s[1].quick &&
               !loaded.s[0].quick && loaded.s[1].m[0].kind == CONTROL_SCREWS_JOB_QUICK,
           "quick entries survive a save/load round trip");
    FILE *old = fopen(qpath, "w");
    if (old) { fputs(SCREW_HISTORY_MAGIC "\nS 1762800000 1762800300\n", old); fclose(old); }
    expect(screw_history_load(&loaded, qpath) && loaded.count == 1 && !loaded.s[0].quick && loaded.s[0].ended == 1762800300,
           "a file written before quick entries (two-field session lines) still loads");
    unlink(qpath);
    rmdir(qdir);
    memset(&h, 0, sizeof(h));
    for (int i = 0; i < SCREW_HISTORY_SESSIONS; i++) {
        control_screws_job st = {CONTROL_SCREWS_JOB_START, -1, 0, t0 + i * 10};
        (void)screw_history_record(&h, &st, out, strlen(out), t0 + i * 10 + 5);
    }
    for (int i = 0; i < SCREW_HISTORY_QUICK + 4; i++) {
        control_screws_job q = {CONTROL_SCREWS_JOB_QUICK, -1, 0, t0 + 1000 + i * 10};
        (void)screw_history_record(&h, &q, out, strlen(out), t0 + 1000 + i * 10 + 5);
    }
    int sessions = 0, quicks = 0;
    for (int i = 0; i < h.count; i++) h.s[i].quick ? quicks++ : sessions++;
    expect(sessions == SCREW_HISTORY_SESSIONS && quicks == SCREW_HISTORY_QUICK && h.s[h.count - 1].started == t0 + 1000 + (SCREW_HISTORY_QUICK + 3) * 10,
           "quick measurements keep their own limit of 10 and never push sessions out");

    memset(&loaded, 0, sizeof(loaded));
    json = screw_history_json(&loaded, &length);
    expect(json && !strcmp(json, "{\"sessions\":[]}\n"), "an empty history is an empty list");
    free(json);

    return failures ? 1 : 0;
}
