/* Actual temperatures of a screw measurement (bed, nozzle, chamber): taken at each new "Result is z=", kept in
 * the history file as an optional last field, shown in the history JSON, readable both ways. */
#include "../src/screw_history.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
static void expect(int condition, const char *label) {
    printf("%s: %s\n", condition ? "PASS" : "FAIL", label);
    if (!condition) failures++;
}
static int contains(const char *text, const char *needle) { return text && strstr(text, needle) != NULL; }

static void probe(char *out, size_t cap, double x, double y, double z) {
    size_t n = strlen(out);
    snprintf(out + n, cap - n, "// probe at %.3f,%.3f is z=%.6f\n// Result is z=%.6f\n", x, y, z, z);
}

static screw_history_store h, back;

int main(void) {
    char out[4096] = "";
    expect(screw_history_count_results("", 0) == 0 && screw_history_count_results("G28\nok\n", 7) == 0,
           "no result lines are counted as zero");
    probe(out, sizeof(out), 27.9, 25.95, -0.7);
    expect(screw_history_count_results(out, strlen(out)) == 1, "one result line is counted");
    probe(out, sizeof(out), 227.15, 25.95, -0.6);
    probe(out, sizeof(out), 227.15, 225.5, -0.8);
    probe(out, sizeof(out), 27.9, 225.5, -0.75);
    expect(screw_history_count_results(out, strlen(out)) == 4, "four result lines are counted");

    time_t t0 = 1762800000;
    control_screws_job start = {CONTROL_SCREWS_JOB_START, -1, 0, t0, 1, 60};
    screw_ist full = {7, {61.7, 139.8, 23.0}};
    expect(screw_history_record_ist(&h, &start, out, strlen(out), t0 + 100, &full) && h.s[0].m[0].have_ist == 7 &&
               h.s[0].m[0].ist[0] == 61.7 && h.s[0].m[0].ist[1] == 139.8 && h.s[0].m[0].ist[2] == 23.0,
           "a measurement keeps the temperatures given at its last result");
    control_screws_job fr = {CONTROL_SCREWS_JOB_PROBE, 1, 0, t0, 1, 60};
    screw_ist part = {1 | 4, {62.0, 0, 24.0}};
    expect(screw_history_record_ist(&h, &fr, out, strlen(out), t0 + 200, &part) && h.s[0].m[1].have_ist == 5,
           "an unavailable sensor leaves only its own field out");
    control_screws_job check = {CONTROL_SCREWS_JOB_CHECK, -1, 0, t0, 1, 60};
    expect(screw_history_record(&h, &check, out, strlen(out), t0 + 300) && h.s[0].m[2].have_ist == 0,
           "without a sample nothing is recorded (no value instead of a made-up one)");
    screw_ist none = {0, {0, 0, 0}};
    control_screws_job quick = {CONTROL_SCREWS_JOB_QUICK, -1, 0, t0 + 400, 0, 0};
    expect(screw_history_record_ist(&h, &quick, out, strlen(out), t0 + 500, &none) && h.s[1].m[0].have_ist == 0,
           "an empty sample records nothing");
    screw_ist q = {7, {60.4, 140.0, 22.0}};
    control_screws_job quick2 = {CONTROL_SCREWS_JOB_QUICK, -1, 0, t0 + 600, 0, 0}; /* a bed target not recorded */
    expect(screw_history_record_ist(&h, &quick2, out, strlen(out), t0 + 700, &q) && h.s[2].m[0].have_ist == 7 &&
               h.s[2].m[0].bed == -1,
           "temperatures are kept for a measurement without a recorded bed target");

    screw_ist wild = {7, {999.0, 1.0e9, -300.0}};
    control_screws_job fl_probe = {CONTROL_SCREWS_JOB_PROBE, 0, 0, t0, 1, 60};
    screw_history_store scratch;
    memset(&scratch, 0, sizeof(scratch));
    expect(screw_history_record_ist(&scratch, &fl_probe, out, strlen(out), t0 + 350, &wild) &&
               scratch.s[0].m[0].have_ist == 0,
           "implausible temperatures are not recorded");

    char *json = screw_history_json(&h, NULL);
    expect(contains(json, "\"bed\":60,\"ist\":{\"bed\":61.7,\"nozzle\":139.8,\"chamber\":23.0}"),
           "the history JSON carries the actual temperatures after the bed target");
    expect(contains(json, "\"ist\":{\"bed\":62.0,\"chamber\":24.0}"), "only the known keys are written");
    expect(contains(json, "\"ist\":{\"bed\":60.4,\"nozzle\":140.0,\"chamber\":22.0}"), "also without a bed target");
    char *first = strstr(json, "\"kind\":\"check\"");
    expect(first && !contains(strstr(first, "}"), "\"kind\":\"check\"") , "sanity: json parsed");
    /* the check measurement has no "ist": its object ends right after z */
    char *check_obj = strstr(json, "\"kind\":\"check\"");
    char *check_end = check_obj ? strchr(check_obj, '}') : NULL;
    char segment[512] = "";
    if (check_obj && check_end && (size_t)(check_end - check_obj) < sizeof(segment)) {
        memcpy(segment, check_obj, (size_t)(check_end - check_obj));
        segment[check_end - check_obj] = 0;
    }
    expect(segment[0] && !strstr(segment, "\"ist\""), "a measurement without temperatures has no ist object");
    free(json);

    char dir[] = "/tmp/cc2-screw-ist-XXXXXX";
    expect(mkdtemp(dir) != NULL, "temporary directory");
    char path[256];
    snprintf(path, sizeof(path), "%s/screw-history.txt", dir);
    expect(screw_history_save(&h, path), "history with temperatures is saved");
    expect(screw_history_load(&back, path) && back.count == h.count, "history with temperatures is loaded");
    expect(back.s[0].m[0].have_ist == 7 && back.s[0].m[0].ist[0] == 61.7 && back.s[0].m[0].ist[1] == 139.8 &&
               back.s[0].m[0].ist[2] == 23.0 && back.s[0].m[1].have_ist == 5 && back.s[0].m[2].have_ist == 0 &&
               back.s[2].m[0].have_ist == 7 && back.s[2].m[0].bed == -1,
           "a save/load round trip keeps temperatures, missing fields and a missing bed target");

    /* An older program reads the first fourteen fields and an optional bed number and ignores the rest. */
    FILE *f = fopen(path, "r");
    char line[512], mline[512] = "";
    while (f && fgets(line, sizeof(line), f)) if (line[0] == 'M' && !mline[0]) memcpy(mline, line, sizeof(mline));
    if (f) fclose(f);
    long long a; int kind, corner, twice, used = 0, bed = -1; unsigned hv, hv2; double z[4], z2[4];
    int fields = sscanf(mline, "M %lld %d %d %d %u %u %lf %lf %lf %lf %lf %lf %lf %lf%n", &a, &kind, &corner, &twice,
                        &hv, &hv2, &z[0], &z[1], &z[2], &z[3], &z2[0], &z2[1], &z2[2], &z2[3], &used);
    expect(fields == 14 && sscanf(mline + used, " %d", &bed) == 1 && bed == 60,
           "a program from before the temperatures still reads the line and its bed target");

    /* Older files: no temperature field, with and without a bed target. */
    FILE *w = fopen(path, "w");
    fputs(SCREW_HISTORY_MAGIC "\nS 1762800000 1762800100 0\n"
          "M 1762800050 0 -1 0 15 0 -0.7 -0.6 -0.8 -0.75 0 0 0 0 60\n"
          "M 1762800060 2 -1 0 15 0 -0.7 -0.6 -0.8 -0.75 0 0 0 0\n", w);
    fclose(w);
    expect(screw_history_load(&back, path) && back.count == 1 && back.s[0].count == 2 && back.s[0].m[0].have_ist == 0 &&
               back.s[0].m[0].bed == 60 && back.s[0].m[1].have_ist == 0 && back.s[0].m[1].bed == -1,
           "old lines without the temperature field stay readable");
    /* Temperatures without a bed number, with a bed number, and a broken field. */
    w = fopen(path, "w");
    fputs(SCREW_HISTORY_MAGIC "\nS 1762800000 1762800100 0\n"
          "M 1762800050 0 -1 0 15 0 -0.7 -0.6 -0.8 -0.75 0 0 0 0 T61.7,139.8,23.0\n"
          "M 1762800060 2 -1 0 15 0 -0.7 -0.6 -0.8 -0.75 0 0 0 0 60 T-,140.5,-\n"
          "M 1762800070 2 -1 0 15 0 -0.7 -0.6 -0.8 -0.75 0 0 0 0 60 Tabc\n"
          "M 1762800080 2 -1 0 15 0 -0.7 -0.6 -0.8 -0.75 0 0 0 0 60 T999999,1,1\n", w);
    fclose(w);
    expect(screw_history_load(&back, path) && back.s[0].count == 4 && back.s[0].m[0].have_ist == 7 &&
               back.s[0].m[0].bed == -1 && back.s[0].m[1].have_ist == 2 && back.s[0].m[1].ist[1] == 140.5 &&
               back.s[0].m[1].bed == 60,
           "a temperature field is read with or without a bed number; '-' marks a missing sensor");
    expect(back.s[0].m[2].have_ist == 0 && back.s[0].m[3].have_ist == 0 && back.s[0].m[2].bed == 60,
           "a broken or implausible temperature field is ignored, the line still loads");
    unlink(path);
    rmdir(dir);
    return failures ? 1 : 0;
}
