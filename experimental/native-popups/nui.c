// SPDX-License-Identifier: GPL-3.0-or-later
/* Long-press pop-ups for the CC2 Community Home screen.
 *
 * Runs inside the screen program. The Home view's show function is wrapped:
 * after it runs, the Nozzle and Build plate cards get a long-press handler,
 * and a one-second timer re-checks in case the cards are created later. A long
 * press opens an LVGL overlay on the top layer, built with the program's own
 * widgets and fonts. Choices go to CC2 Control on this printer
 * (127.0.0.1:8081): GET /api/plates, POST /api/plates/nozzle/select and
 * POST /api/plates/mount, the same requests as the CC2 Control web page.
 * Changes are made only while the printer is idle. NUI_TEST builds replace the
 * HTTP layer with fixtures for the host tests. */
#include "nui.h"

#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_CLOEXEC 02000000
#define SOL_SOCKET 1
#define SO_RCVTIMEO 20
#define SO_SNDTIMEO 21

/* The CC2 Community palette (native-display assets). */
#define C(hex) ((lv_color_t){0xff000000u | (hex)})
#define BACKDROP C(0x0b1018)
#define SURFACE C(0x171e29)
#define CARD C(0x2a3545)
#define CARD_DOWN C(0x263b46)
#define TEXT C(0xedf2f7)
#define DIM C(0x9aa9b9)
#define ACCENT C(0x7eead8)
#define WARN C(0xffc66d)

#define CARD_W 180
#define CARD_H 52
#define CARD_X 332
static const lv_coord_t card_y[] = {79, 139}; /* Nozzle, Build plate */
enum { NOZZLE, PLATE };

void ui_view_home_home_show(void *a, void *b, void *c, void *d);

/* ---------- HTTP to CC2 Control ---------- */

#ifndef NUI_TEST
/* One HTTP/1.0 exchange; the status code (body moved to the start of buf) or -1. */
static int http(const char *method, const char *path, const char *body, char *buf, size_t cap) {
    int s = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (s < 0) return -1;
    struct { long sec, usec; } tv = {2, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    struct { uint16_t family, port; uint32_t addr; uint8_t zero[8]; } a = {AF_INET, 0x911f /* 8081 */, 0x0100007f, {0}};
    if (connect(s, &a, sizeof(a))) { close(s); return -1; }
    size_t blen = body ? strlen(body) : 0;
    char req[512];
    int n = snprintf(req, sizeof(req),
                     "%s %s HTTP/1.0\r\nHost: 127.0.0.1:8081\r\nX-CC2-Request: 1\r\nContent-Type: text/plain\r\n"
                     "Content-Length: %u\r\nConnection: close\r\n\r\n", method, path, (unsigned)blen);
    if (n < 0 || (size_t)n >= sizeof(req) || write(s, req, (size_t)n) != n ||
        (blen && write(s, body, blen) != (long)blen)) { close(s); return -1; }
    size_t got = 0; long r;
    while (got + 1 < cap && (r = read(s, buf + got, cap - 1 - got)) > 0) got += (size_t)r;
    close(s);
    buf[got] = 0;
    if (strncmp(buf, "HTTP/1.", 7) || got < 12) return -1;
    int code = (int)strtoul(buf + 9, NULL, 10);
    char *e = strstr(buf, "\r\n\r\n");
    if (e) memmove(buf, e + 4, strlen(e + 4) + 1); else buf[0] = 0;
    return code;
}
#endif

/* ---------- minimal JSON ---------- */

typedef struct jv { char type; char *key, *str; double num; struct jv *kid, *next; } jv; /* o a s n t f z */
static const char *jp;
static int jspace(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }
static void jws(void) { while (*jp && jspace(*jp)) jp++; }
static jv *jnew(char t) { jv *v = calloc(1, sizeof(jv)); if (v) v->type = t; return v; }
static char *jstr(void) {
    if (*jp != '"') return NULL;
    jp++;
    const char *start = jp; size_t n = 0;
    while (start[n] && start[n] != '"') n += start[n] == '\\' && start[n + 1] ? 2 : 1;
    char *o = malloc(n + 1); size_t k = 0;
    if (!o) return NULL;
    while (*jp && *jp != '"') {
        char c = *jp++;
        if (c == '\\' && *jp) {
            c = *jp++;
            if (c == 'n') c = ' ';
            else if (c == 'u') { for (int i = 0; i < 4 && *jp; i++) jp++; c = '?'; }
        }
        o[k++] = c;
    }
    if (*jp == '"') jp++;
    o[k] = 0; return o;
}
static void jfree(jv *v) { while (v) { jv *n = v->next; jfree(v->kid); free(v->key); free(v->str); free(v); v = n; } }
static jv *jparse(int depth) {
    jws();
    if (depth > 16) return NULL;
    jv *v;
    if (*jp == '{' || *jp == '[') {
        char end = *jp == '{' ? '}' : ']';
        if (!(v = jnew(*jp == '{' ? 'o' : 'a'))) return NULL;
        jp++;
        jv **tail = &v->kid;
        for (;;) {
            jws();
            if (*jp == end) { jp++; break; }
            if (!*jp) break;
            char *key = NULL;
            if (v->type == 'o') { key = jstr(); jws(); if (*jp == ':') jp++; }
            jv *k = jparse(depth + 1);
            if (!k) { free(key); break; }
            k->key = key; *tail = k; tail = &k->next;
            jws(); if (*jp == ',') jp++;
        }
        return v;
    }
    if (*jp == '"') { if ((v = jnew('s'))) v->str = jstr(); return v; }
    if (!strncmp(jp, "true", 4)) { jp += 4; return jnew('t'); }
    if (!strncmp(jp, "false", 5)) { jp += 5; return jnew('f'); }
    if (!strncmp(jp, "null", 4)) { jp += 4; return jnew('z'); }
    char *end; double d = strtod(jp, &end);
    if (end == jp) return NULL;
    jp = end;
    if ((v = jnew('n'))) v->num = d;
    return v;
}
static jv *jget(jv *o, const char *key) {
    for (jv *k = o ? o->kid : NULL; k; k = k->next) if (k->key && !strcmp(k->key, key)) return k;
    return NULL;
}
static const char *jgets(jv *o, const char *key) { jv *v = jget(o, key); return v && v->type == 's' && v->str ? v->str : ""; }
static jv *jkids(jv *o, const char *key) { jv *v = jget(o, key); return v ? v->kid : NULL; }

#define HTTP_CAP (64 * 1024)
#ifdef NUI_TEST
const char *nui_test_get(const char *path);
int nui_test_post(const char *path, const char *body, char *reply, size_t cap);
static jv *get_json(const char *path) {
    const char *text = nui_test_get(path);
    if (!text) return NULL;
    jp = text;
    return jparse(0);
}
#else
/* GET path as parsed JSON, or NULL. */
static jv *get_json(const char *path) {
    char *buf = malloc(HTTP_CAP);
    if (!buf) return NULL;
    jv *root = NULL;
    if (http("GET", path, NULL, buf, HTTP_CAP) == 200) { jp = buf; root = jparse(0); }
    free(buf);
    return root;
}
#endif
static int printer_idle(void) {
    jv *root = get_json("/api/printer");
    int idle = root && !strcmp(jgets(jget(root, "machine"), "status_name"), "Idle");
    jfree(root);
    return idle;
}
/* POST; the status code, with CC2 Control's error text (if any) in err. */
static int post(const char *path, const char *body, char *err, size_t cap) {
    char *buf = malloc(4096);
    if (!buf) return -1;
#ifdef NUI_TEST
    int code = nui_test_post(path, body, buf, 4096);
#else
    int code = http("POST", path, body, buf, 4096);
#endif
    err[0] = 0;
    if (code != 200 && code != 202) {
        jp = buf; jv *r = jparse(0);
        snprintf(err, cap, "%s", *jgets(r, "error") ? jgets(r, "error") : strstr(buf, "reboot_required") ? "reboot_required" : "");
        jfree(r);
    }
    free(buf);
    return code;
}

/* ---------- fonts and styling ---------- */

/* The program's font whose line height is nearest px (font_info has 11 entries). */
static const lv_font_t *font_px(int px) {
    const lv_font_t *best = get_font(0); int bd = 1000;
    for (int i = 0; i <= 10; i++) {
        const lv_font_t *f = get_font(i);
        if (!f) continue;
        int h = *(const int16_t *)((const char *)f + 8), d = h > px ? h - px : px - h;
        if (d < bd) { bd = d; best = f; }
    }
    return best;
}
static void plain(lv_obj_t *o, lv_color_t bg, int opa, int radius) {
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, (uint8_t)opa, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_shadow_width(o, 0, 0);
    lv_obj_set_style_outline_width(o, 0, 0);
    lv_obj_set_style_pad_top(o, 0, 0); lv_obj_set_style_pad_bottom(o, 0, 0);
    lv_obj_set_style_pad_left(o, 0, 0); lv_obj_set_style_pad_right(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}
static lv_obj_t *label(lv_obj_t *parent, const char *text, int px, lv_color_t colour) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font_px(px), 0);
    lv_obj_set_style_text_color(l, colour, 0);
    lv_label_set_text(l, text);
    return l;
}

/* ---------- the pop-up ---------- */

#define MAX_ROWS 12
typedef struct { char id[40], measure[40], name[72], detail[96]; int current, usable; } row;
static struct {
    lv_obj_t *backdrop, *panel, *status, *list;
    lv_timer_t *close_timer;
    int kind, confirm;
    row rows[MAX_ROWS]; int nrows;
} pop;

static void pop_close(void) {
    if (pop.close_timer) { lv_timer_del(pop.close_timer); pop.close_timer = NULL; }
    if (pop.backdrop) lv_obj_del_async(pop.backdrop);
    pop.backdrop = pop.panel = pop.status = pop.list = NULL;
}
static void status(const char *text, lv_color_t colour) {
    if (!pop.status) return;
    lv_obj_set_style_text_color(pop.status, colour, 0);
    lv_label_set_text(pop.status, text);
}
static void close_later_cb(lv_timer_t *t) { (void)t; pop.close_timer = NULL; lv_timer_del(t); pop_close(); }
static void close_later(void) { if (!pop.close_timer) pop.close_timer = lv_timer_create(close_later_cb, 1000, NULL); }

static void backdrop_cb(lv_event_t *e) {
    if (lv_event_get_target(e) == pop.backdrop) pop_close(); /* a tap outside the panel */
}
static void close_cb(lv_event_t *e) { (void)e; pop_close(); }

static void render(void);
static void row_cb(lv_event_t *e);
static void confirm_cb(lv_event_t *e);

static lv_obj_t *button(lv_obj_t *parent, int w, int h, int outlined) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    plain(b, CARD, 255, 8);
    lv_obj_set_style_bg_color(b, CARD_DOWN, LV_STATE_PRESSED);
    if (outlined) { lv_obj_set_style_border_width(b, 2, 0); lv_obj_set_style_border_color(b, ACCENT, 0); }
    return b;
}

/* Fills pop.rows for the panel kind; NULL or a message. */
static const char *load_rows(void) {
    pop.nrows = 0;
    jv *root = get_json("/api/plates");
    if (!root) return "CC2 Control did not answer";
    jv *avail = jget(root, "available");
    if (!avail || avail->type != 't') { jfree(root); return "The plate library is not available"; }
    if (pop.kind == NOZZLE) {
        const char *installed = jgets(root, "nozzle");
        for (jv *n = jkids(root, "nozzles"); n && pop.nrows < MAX_ROWS; n = n->next) {
            const char *id = jgets(n, "id");
            if (!*id) continue;
            int measured = 0;
            for (jv *p = jkids(root, "plates"); p && !measured; p = p->next)
                for (jv *m = jkids(p, "measures"); m; m = m->next)
                    if (!strcmp(jgets(m, "nozzle"), id)) { measured = 1; break; }
            row *r = &pop.rows[pop.nrows++];
            memset(r, 0, sizeof(*r));
            snprintf(r->id, sizeof(r->id), "%s", id);
            snprintf(r->name, sizeof(r->name), "%s", jgets(n, "name"));
            /* Any nozzle can be chosen, as on the Plates page: the mesh belongs to the plate and
             * the nozzle only adds its Z correction. Unmeasured nozzles are only labelled. */
            r->current = !strcmp(id, installed);
            r->usable = 1;
            snprintf(r->detail, sizeof(r->detail), "%s", r->current ? "Installed" : measured ? "" : "No bed mesh");
        }
    } else {
        const char *current = jgets(root, "current");
        for (jv *p = jkids(root, "plates"); p && pop.nrows < MAX_ROWS; p = p->next) {
            const char *pid = jgets(p, "id"), *base = jgets(p, "measure");
            jv *ms = jkids(p, "measures");
            if (!ms) {
                row *r = &pop.rows[pop.nrows++];
                memset(r, 0, sizeof(*r));
                snprintf(r->id, sizeof(r->id), "%s", pid);
                snprintf(r->name, sizeof(r->name), "%s", jgets(p, "name"));
                snprintf(r->detail, sizeof(r->detail), "Side %s \xc2\xb7 not calibrated", jgets(p, "side"));
                continue;
            }
            for (jv *m = ms; m && pop.nrows < MAX_ROWS; m = m->next) {
                row *r = &pop.rows[pop.nrows++];
                memset(r, 0, sizeof(*r));
                const char *nid = jgets(m, "nozzle"), *nname = "";
                for (jv *z = jkids(root, "nozzles"); z; z = z->next) if (!strcmp(jgets(z, "id"), nid)) nname = jgets(z, "name");
                jv *t = jget(m, "temp");
                snprintf(r->id, sizeof(r->id), "%s", pid);
                snprintf(r->measure, sizeof(r->measure), "%s", jgets(m, "id"));
                snprintf(r->name, sizeof(r->name), "%s \xc2\xb7 %d\xc2\xb0" "C", jgets(p, "name"), t && t->type == 'n' ? (int)t->num : 0);
                snprintf(r->detail, sizeof(r->detail), "Side %s%s%s", jgets(p, "side"), *nname ? " \xc2\xb7 " : "", nname);
                r->usable = 1;
                r->current = !strcmp(pid, current) && !strcmp(jgets(m, "id"), base);
            }
        }
    }
    jfree(root);
    return NULL;
}

/* A fresh, empty list. The old one may hold the button whose tap is being handled,
 * so it is deleted only after the event finishes. */
static void new_list(void) {
    if (pop.list) lv_obj_del_async(pop.list);
    pop.list = lv_obj_create(pop.panel);
    plain(pop.list, SURFACE, 0, 0);
    lv_obj_add_flag(pop.list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(pop.list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_set_flex_flow(pop.list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(pop.list, 8, 0);
    lv_obj_set_pos(pop.list, 22, 74);
    lv_obj_set_size(pop.list, 440, 192);
}

/* The list, or the restart confirmation for pop.confirm. */
static void render(void) {
    if (!pop.panel) return;
    new_list();
    if (pop.confirm >= 0) {
        row *r = &pop.rows[pop.confirm];
        lv_obj_t *l = label(pop.list, r->name, 20, TEXT); lv_obj_set_width(l, 436);
        l = label(pop.list, "This mesh is not in the printer yet. Loading it restarts the printer (about a minute).", 16, DIM);
        lv_obj_set_width(l, 436);
        lv_obj_t *yes = button(pop.list, 436, 44, 1);
        lv_obj_add_event_cb(yes, confirm_cb, LV_EVENT_CLICKED, (void *)1);
        lv_obj_align(label(yes, "Load and restart", 18, ACCENT), LV_ALIGN_CENTER, 0, 0);
        lv_obj_t *no = button(pop.list, 436, 44, 0);
        lv_obj_add_event_cb(no, confirm_cb, LV_EVENT_CLICKED, (void *)0);
        lv_obj_align(label(no, "Cancel", 18, TEXT), LV_ALIGN_CENTER, 0, 0);
        return;
    }
    for (int i = 0; i < pop.nrows; i++) {
        row *r = &pop.rows[i];
        int tall = pop.kind == PLATE;
        lv_obj_t *b = button(pop.list, 436, tall ? 52 : 44, r->current);
        if (!r->usable) lv_obj_set_style_bg_color(b, SURFACE, 0);
        lv_obj_add_event_cb(b, row_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *name = label(b, r->name, 18, r->usable || r->current ? TEXT : DIM);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_set_width(name, 300);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 14, tall ? -9 : 0);
        if (tall) lv_obj_align(label(b, r->detail, 14, DIM), LV_ALIGN_LEFT_MID, 14, 12);
        const char *tag = r->current ? (pop.kind == NOZZLE ? "Installed" : "Active") : pop.kind == NOZZLE ? r->detail : "";
        if (*tag) lv_obj_align(label(b, tag, 14, r->current ? ACCENT : DIM), LV_ALIGN_RIGHT_MID, -14, 0);
    }
    if (!pop.nrows) {
        lv_obj_t *l = label(pop.list, pop.kind == NOZZLE ? "No nozzles saved yet. Add them on the Plates page of CC2 Control."
                                                         : "No plates saved yet. Calibrate a plate on the Plates page of CC2 Control.", 16, DIM);
        lv_obj_set_width(l, 436);
    }
}

static void row_cb(lv_event_t *e) {
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    if (i < 0 || i >= pop.nrows) return;
    row *r = &pop.rows[i];
    if (r->current) { status(pop.kind == NOZZLE ? "Already installed" : "Already active", DIM); close_later(); return; }
    if (!r->usable) {
        status("Calibrate this plate in CC2 Control first", WARN);
        return;
    }
    if (!printer_idle()) { status("The printer is busy; nothing changed", WARN); return; }
    char body[128], err[160], msg[200];
    int code;
    if (pop.kind == NOZZLE) {
        snprintf(body, sizeof(body), "%s\n", r->id);
        code = post("/api/plates/nozzle/select", body, err, sizeof(err));
    } else {
        snprintf(body, sizeof(body), "%s\n%s\n", r->id, r->measure);
        code = post("/api/plates/mount", body, err, sizeof(err));
        if (code == 409 && !strcmp(err, "reboot_required")) { pop.confirm = i; status("Load this mesh?", TEXT); render(); return; }
    }
    if (code == 200) {
        for (int k = 0; k < pop.nrows; k++) pop.rows[k].current = k == i;
        snprintf(msg, sizeof(msg), "%s %s", r->name, pop.kind == NOZZLE ? "selected" : "active");
        status(msg, ACCENT);
        render();
        close_later();
    } else {
        snprintf(msg, sizeof(msg), "Not changed: %s", *err ? err : "CC2 Control refused");
        status(msg, WARN);
    }
}

static void confirm_cb(lv_event_t *e) {
    int yes = (int)(intptr_t)lv_event_get_user_data(e), i = pop.confirm;
    pop.confirm = -1;
    if (!yes || i < 0) { status("Nothing changed", DIM); render(); return; }
    if (!printer_idle()) { status("The printer is busy; nothing changed", WARN); render(); return; }
    char body[128], err[160], msg[200];
    snprintf(body, sizeof(body), "%s\n%s\nREBOOT\n", pop.rows[i].id, pop.rows[i].measure);
    int code = post("/api/plates/mount", body, err, sizeof(err));
    if (code == 200 || code == 202) { status("Loading the mesh; the printer restarts now", ACCENT); close_later(); return; }
    snprintf(msg, sizeof(msg), "Not changed: %s", *err ? err : "CC2 Control refused");
    status(msg, WARN);
    render();
}

static void pop_open(int kind) {
    if (pop.backdrop) return;
    pop.kind = kind; pop.confirm = -1;
    lv_obj_t *top = lv_disp_get_layer_top(lv_disp_get_default());
    pop.backdrop = lv_obj_create(top);
    plain(pop.backdrop, BACKDROP, 170, 0);
    lv_obj_set_pos(pop.backdrop, 0, 0);
    lv_obj_set_size(pop.backdrop, 532, 300);
    lv_obj_add_event_cb(pop.backdrop, backdrop_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *panel = pop.panel = lv_obj_create(pop.backdrop);
    plain(panel, SURFACE, 255, 12);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, CARD, 0);
    lv_obj_set_pos(panel, 24, 12);
    lv_obj_set_size(panel, 484, 276);

    lv_obj_t *title = label(panel, kind == NOZZLE ? "Nozzle" : "Build plate", 22, ACCENT);
    lv_obj_set_pos(title, 22, 14);
    pop.status = label(panel, kind == NOZZLE ? "Choose the installed nozzle" : "Choose the plate and bed mesh", 15, DIM);
    lv_label_set_long_mode(pop.status, LV_LABEL_LONG_DOT);
    lv_obj_set_width(pop.status, 360);
    lv_obj_set_pos(pop.status, 22, 44);

    lv_obj_t *close = button(panel, 76, 36, 0);
    lv_obj_set_pos(close, 392, 12);
    lv_obj_add_event_cb(close, close_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_align(label(close, "Close", 16, TEXT), LV_ALIGN_CENTER, 0, 0);

    const char *err = load_rows();
    if (err) status(err, WARN);
    render();
}

/* ---------- finding the Home cards ---------- */

static void card_cb(lv_event_t *e) {
    int kind = (int)(intptr_t)lv_event_get_user_data(e);
    lv_indev_wait_release(lv_indev_get_act()); /* the lift must not also tap the card */
    pop_open(kind);
}

static int near(int a, int b, int tol) { return a - b <= tol && b - a <= tol; }
static void scan(lv_obj_t *o, int depth) {
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    int w = a.x2 - a.x1 + 1, h = a.y2 - a.y1 + 1;
    if (near(w, CARD_W, 4) && near(h, CARD_H, 4) && near(a.x1, CARD_X, 6)) {
        for (int k = 0; k < 2; k++)
            if (near(a.y1, card_y[k], 6) && !lv_obj_has_flag(o, LV_OBJ_FLAG_USER_4)) {
                lv_obj_add_flag(o, LV_OBJ_FLAG_USER_4 | LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_event_cb(o, card_cb, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)k);
            }
        return;
    }
    if (depth >= 6) return;
    uint32_t n = lv_obj_get_child_cnt(o);
    for (uint32_t i = 0; i < n && i < 64; i++) scan(lv_obj_get_child(o, (int32_t)i), depth + 1);
}
static void attach(void) {
    lv_obj_t *scr = lv_disp_get_scr_act(lv_disp_get_default());
    if (scr) scan(scr, 0);
}
static void tick(lv_timer_t *t) { (void)t; attach(); }

static lv_timer_t *ticker;
/* Replaces ui_view_home_home_show in the view table. */
void nui_home_show(void *a, void *b, void *c, void *d) {
    ui_view_home_home_show(a, b, c, d);
    if (!ticker) ticker = lv_timer_create(tick, 1000, NULL);
    attach();
}
