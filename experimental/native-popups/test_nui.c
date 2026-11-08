// SPDX-License-Identifier: GPL-3.0-or-later
/* Host test of nui.c with a small fake LVGL and fixture HTTP replies. */
#define NUI_TEST 1
#include "nui.c"

#include <assert.h>
#include <stdio.h>

/* ---------- fake LVGL ---------- */

struct _lv_obj_t {
    lv_obj_t *parent, *kids[96]; int nkids;
    lv_area_t a; uint32_t flags; int deleted;
    struct { lv_event_cb_t cb; uint32_t filter; void *ud; } cbs[4]; int ncbs;
    char text[256];
};
struct _lv_event_t { lv_obj_t *target; void *ud; uint32_t code; };
struct _lv_timer_t { lv_timer_cb_t cb; int live; };
struct _lv_font_t { uint32_t get_glyph_dsc, get_glyph_bitmap; int16_t line_height; }; /* the printer's 32-bit layout */

static lv_obj_t screen, top;
static struct _lv_font_t fonts[11];
static struct _lv_timer_t timers[16]; static int ntimers;
static int home_shown, released;

void ui_view_home_home_show(void *a, void *b, void *c, void *d) { (void)a; (void)b; (void)c; (void)d; home_shown++; }
lv_disp_t *lv_disp_get_default(void) { return NULL; }
lv_obj_t *lv_disp_get_scr_act(lv_disp_t *d) { (void)d; return &screen; }
lv_obj_t *lv_disp_get_layer_top(lv_disp_t *d) { (void)d; return &top; }
static lv_obj_t *add(lv_obj_t *parent) {
    lv_obj_t *o = calloc(1, sizeof(*o));
    o->parent = parent;
    assert(parent->nkids < 96);
    parent->kids[parent->nkids++] = o;
    o->flags = LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE;
    return o;
}
lv_obj_t *lv_obj_create(lv_obj_t *p) { return add(p); }
lv_obj_t *lv_btn_create(lv_obj_t *p) { return add(p); }
lv_obj_t *lv_label_create(lv_obj_t *p) { lv_obj_t *o = add(p); o->flags = 0; return o; }
void lv_label_set_text(lv_obj_t *o, const char *t) { snprintf(o->text, sizeof(o->text), "%s", t); }
void lv_label_set_long_mode(lv_obj_t *o, uint8_t m) { (void)o; (void)m; }
static void detach(lv_obj_t *o) {
    lv_obj_t *p = o->parent;
    for (int i = 0; i < p->nkids; i++)
        if (p->kids[i] == o) { memmove(&p->kids[i], &p->kids[i + 1], sizeof(p->kids[0]) * (size_t)(p->nkids - i - 1)); p->nkids--; break; }
    o->deleted = 1;
}
void lv_obj_del_async(lv_obj_t *o) { assert(!o->deleted); detach(o); }
lv_obj_t *lv_obj_get_child(const lv_obj_t *o, int32_t i) { return i < o->nkids ? o->kids[i] : NULL; }
uint32_t lv_obj_get_child_cnt(const lv_obj_t *o) { return (uint32_t)o->nkids; }
void lv_obj_get_coords(const lv_obj_t *o, lv_area_t *a) { *a = o->a; }
void lv_obj_add_flag(lv_obj_t *o, uint32_t f) { o->flags |= f; }
void lv_obj_clear_flag(lv_obj_t *o, uint32_t f) { o->flags &= ~f; }
int lv_obj_has_flag(const lv_obj_t *o, uint32_t f) { return (o->flags & f) == f; }
void lv_obj_set_pos(lv_obj_t *o, lv_coord_t x, lv_coord_t y) { (void)o; (void)x; (void)y; }
void lv_obj_set_size(lv_obj_t *o, lv_coord_t w, lv_coord_t h) { (void)o; (void)w; (void)h; }
void lv_obj_set_width(lv_obj_t *o, lv_coord_t w) { (void)o; (void)w; }
void lv_obj_align(lv_obj_t *o, uint8_t a, lv_coord_t x, lv_coord_t y) { (void)o; (void)a; (void)x; (void)y; }
void lv_obj_set_flex_flow(lv_obj_t *o, uint8_t f) { (void)o; (void)f; }
void lv_obj_set_scrollbar_mode(lv_obj_t *o, uint8_t m) { (void)o; (void)m; }
void lv_obj_add_event_cb(lv_obj_t *o, lv_event_cb_t cb, uint32_t filter, void *ud) {
    assert(o->ncbs < 4);
    o->cbs[o->ncbs].cb = cb; o->cbs[o->ncbs].filter = filter; o->cbs[o->ncbs++].ud = ud;
}
lv_obj_t *lv_event_get_target(lv_event_t *e) { return e->target; }
void *lv_event_get_user_data(lv_event_t *e) { return e->ud; }
lv_indev_t *lv_indev_get_act(void) { return (lv_indev_t *)&released; }
void lv_indev_wait_release(lv_indev_t *i) { assert(i); released++; }
lv_timer_t *lv_timer_create(lv_timer_cb_t cb, uint32_t period, void *ud) {
    (void)period; (void)ud; assert(ntimers < 16);
    timers[ntimers].cb = cb; timers[ntimers].live = 1;
    return &timers[ntimers++];
}
void lv_timer_del(lv_timer_t *t) { assert(t->live); t->live = 0; }
#define STYLE(name, type) void name(lv_obj_t *o, type v, uint32_t s) { (void)o; (void)v; (void)s; }
STYLE(lv_obj_set_style_bg_color, lv_color_t) STYLE(lv_obj_set_style_bg_opa, uint8_t)
STYLE(lv_obj_set_style_border_color, lv_color_t) STYLE(lv_obj_set_style_border_width, lv_coord_t)
STYLE(lv_obj_set_style_radius, lv_coord_t) STYLE(lv_obj_set_style_shadow_width, lv_coord_t)
STYLE(lv_obj_set_style_outline_width, lv_coord_t) STYLE(lv_obj_set_style_pad_top, lv_coord_t)
STYLE(lv_obj_set_style_pad_bottom, lv_coord_t) STYLE(lv_obj_set_style_pad_left, lv_coord_t)
STYLE(lv_obj_set_style_pad_right, lv_coord_t) STYLE(lv_obj_set_style_pad_row, lv_coord_t)
STYLE(lv_obj_set_style_text_color, lv_color_t) STYLE(lv_obj_set_style_text_font, const lv_font_t *)
const lv_font_t *get_font(int i) { return i >= 0 && i <= 10 ? &fonts[i] : NULL; }

/* ---------- fixtures ---------- */

static const char *PLATES =
    "{\"available\":true,\"current\":\"p1\",\"nozzle\":\"n1\","
    "\"nozzles\":[{\"id\":\"n1\",\"name\":\"HyperCHT 0.6\"},{\"id\":\"n2\",\"name\":\"CHT 0.4\"},{\"id\":\"n3\",\"name\":\"CM2 0.8\"}],"
    "\"plates\":[{\"id\":\"p1\",\"name\":\"Textured PEI\",\"side\":\"A\",\"measure\":\"m2\",\"measures\":["
    "{\"id\":\"m1\",\"temp\":60,\"nozzle\":\"n1\"},{\"id\":\"m2\",\"temp\":100,\"nozzle\":\"n1\"},{\"id\":\"m3\",\"temp\":80,\"nozzle\":\"n2\"}]},"
    "{\"id\":\"p2\",\"name\":\"Smooth \\\"B\\\"\",\"side\":\"B\",\"measures\":[]}]}";
static const char *printer_status = "Idle";
static char printer_json[128], last_post[256], last_body[256];
static int posts, post_code = 200;
static const char *post_reply = "{}";

const char *nui_test_get(const char *path) {
    if (!strcmp(path, "/api/plates")) return PLATES;
    if (!strcmp(path, "/api/printer")) {
        snprintf(printer_json, sizeof(printer_json), "{\"machine\":{\"status_name\":\"%s\"}}", printer_status);
        return printer_json;
    }
    return NULL;
}
int nui_test_post(const char *path, const char *body, char *reply, size_t cap) {
    posts++;
    snprintf(last_post, sizeof(last_post), "%s", path);
    snprintf(last_body, sizeof(last_body), "%s", body);
    snprintf(reply, cap, "%s", post_reply);
    return post_code;
}

/* ---------- helpers ---------- */

static void fire(lv_obj_t *o, uint32_t code) {
    for (int i = 0; i < o->ncbs; i++)
        if (o->cbs[i].filter == code) { lv_event_t e = {o, o->cbs[i].ud, code}; o->cbs[i].cb(&e); }
}
static lv_obj_t *card(lv_obj_t *parent, int x, int y, int w, int h) {
    lv_obj_t *o = add(parent);
    o->flags = 0;
    o->a = (lv_area_t){(lv_coord_t)x, (lv_coord_t)y, (lv_coord_t)(x + w - 1), (lv_coord_t)(y + h - 1)};
    return o;
}
/* The first object under o (depth first) with a label child whose text contains needle. */
static lv_obj_t *find(lv_obj_t *o, const char *needle) {
    for (int i = 0; i < o->nkids; i++) {
        lv_obj_t *k = o->kids[i];
        if (strstr(k->text, needle)) return o;
        lv_obj_t *f = find(k, needle);
        if (f) return f;
    }
    return NULL;
}
static int has_text(const char *needle) { return find(&top, needle) != NULL; }
static const char *status_text(void) { return pop.status ? pop.status->text : ""; }
static void run_timers(void) {
    for (int i = 0; i < ntimers; i++) if (timers[i].live && timers[i].cb != tick) timers[i].cb(&timers[i]);
}

int main(void) {
    for (int i = 0; i <= 10; i++) fonts[i].line_height = (int16_t)(12 + 2 * i);
    assert(font_px(22) == &fonts[5] && font_px(100) == &fonts[10] && font_px(0) == &fonts[0]);

    /* Card detection: the Nozzle and Build plate cards only, once each. */
    lv_obj_t *home = card(&screen, 0, 0, 532, 300);
    lv_obj_t *nozzle = card(home, 332, 79, 180, 52), *plate = card(home, 333, 140, 181, 52);
    lv_obj_t *chamber = card(home, 332, 199, 180, 52), *decoy = card(home, 332, 79, 100, 52);
    nui_home_show(NULL, NULL, NULL, NULL);
    nui_home_show(NULL, NULL, NULL, NULL);
    assert(home_shown == 2);
    assert(nozzle->ncbs == 1 && plate->ncbs == 1 && chamber->ncbs == 0 && decoy->ncbs == 0);
    assert(lv_obj_has_flag(nozzle, LV_OBJ_FLAG_USER_4 | LV_OBJ_FLAG_CLICKABLE));
    assert(nozzle->cbs[0].filter == LV_EVENT_LONG_PRESSED);
    tick(ticker);
    assert(nozzle->ncbs == 1);

    /* Nozzle pop-up: installed, measured and unmeasured nozzles. */
    fire(nozzle, LV_EVENT_LONG_PRESSED);
    assert(released == 1 && top.nkids == 1 && pop.backdrop == top.kids[0]);
    assert(has_text("Nozzle") && has_text("HyperCHT 0.6") && has_text("Installed") && has_text("No bed mesh"));
    fire(nozzle, LV_EVENT_LONG_PRESSED); /* a second long press does not stack another pop-up */
    assert(top.nkids == 1);
    printer_status = "Printing";
    fire(find(&top, "CHT 0.4"), LV_EVENT_CLICKED);
    assert(posts == 0 && strstr(status_text(), "busy"));
    printer_status = "Idle";
    fire(find(&top, "CHT 0.4"), LV_EVENT_CLICKED);
    assert(posts == 1 && !strcmp(last_post, "/api/plates/nozzle/select") && !strcmp(last_body, "n2\n"));
    assert(strstr(status_text(), "CHT 0.4 selected") && pop.close_timer);
    run_timers();
    assert(!pop.backdrop && top.nkids == 0);
    /* A nozzle without a bed mesh can be chosen too, as on the Plates page. */
    fire(nozzle, LV_EVENT_LONG_PRESSED);
    fire(find(&top, "CM2 0.8"), LV_EVENT_CLICKED);
    assert(posts == 2 && !strcmp(last_body, "n3\n") && strstr(status_text(), "CM2 0.8 selected"));
    run_timers();

    /* Build plate pop-up: one row per measurement, a mount that needs a restart. */
    fire(plate, LV_EVENT_LONG_PRESSED);
    assert(has_text("Build plate") && has_text("Textured PEI \xc2\xb7 60") && has_text("Active") && has_text("not calibrated"));
    assert(has_text("Smooth \"B\""));
    post_code = 409; post_reply = "{\"error\":\"reboot_required\"}";
    fire(find(&top, "Textured PEI \xc2\xb7 60"), LV_EVENT_CLICKED);
    assert(posts == 3 && !strcmp(last_post, "/api/plates/mount") && !strcmp(last_body, "p1\nm1\n"));
    assert(has_text("Load and restart") && pop.confirm == 0);
    fire(find(&top, "Cancel"), LV_EVENT_CLICKED);
    assert(posts == 3 && pop.confirm == -1 && !has_text("Load and restart"));
    fire(find(&top, "Textured PEI \xc2\xb7 60"), LV_EVENT_CLICKED);
    post_code = 202; post_reply = "{\"accepted\":true}";
    fire(find(&top, "Load and restart"), LV_EVENT_CLICKED);
    assert(posts == 5 && !strcmp(last_body, "p1\nm1\nREBOOT\n") && strstr(status_text(), "restarts"));
    post_code = 409; post_reply = "{\"error\":\"A Z offset change is still being confirmed\"}";
    pop_close();
    fire(plate, LV_EVENT_LONG_PRESSED);
    fire(find(&top, "Textured PEI \xc2\xb7 80"), LV_EVENT_CLICKED);
    assert(strstr(status_text(), "Not changed: A Z offset change"));
    fire(find(&top, "Close"), LV_EVENT_CLICKED);
    assert(!pop.backdrop);

    /* Malformed JSON never crashes the parser. */
    const char *bad[] = {"", "{", "[[[[", "{\"a\":", "{\"a\":\"\\", "nul", "{\"a\" 1}", "[1,,2]", "\"\\u12"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) { jp = bad[i]; jfree(jparse(0)); }
    char deep[64]; memset(deep, '[', sizeof(deep) - 1); deep[sizeof(deep) - 1] = 0;
    jp = deep; jfree(jparse(0));
    puts("PASS test_nui.c");
    return 0;
}
