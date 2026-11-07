// SPDX-License-Identifier: GPL-3.0-or-later
/* The parts of the CC2 screen program (ec-eeb001-gui, LVGL 8.3, lv_coord_t int16,
 * 32-bit colour) and its C library that the native pop-ups call. Every function
 * here resolves to the program's own code or PLT entry (syms.ld, generated). */
#ifndef NUI_H
#define NUI_H
#include <stddef.h>
#include <stdint.h>

typedef struct _lv_obj_t lv_obj_t;
typedef struct _lv_event_t lv_event_t;
typedef struct _lv_timer_t lv_timer_t;
typedef struct _lv_indev_t lv_indev_t;
typedef struct _lv_disp_t lv_disp_t;
typedef struct _lv_font_t lv_font_t;
typedef int16_t lv_coord_t;
typedef struct { uint32_t full; } lv_color_t;
typedef struct { lv_coord_t x1, y1, x2, y2; } lv_area_t;
typedef void (*lv_event_cb_t)(lv_event_t *e);
typedef void (*lv_timer_cb_t)(lv_timer_t *t);

enum { LV_EVENT_PRESSED = 1, LV_EVENT_LONG_PRESSED = 5, LV_EVENT_CLICKED = 7, LV_EVENT_DELETE = 33 };
enum {
    LV_OBJ_FLAG_HIDDEN = 1u << 0, LV_OBJ_FLAG_CLICKABLE = 1u << 1, LV_OBJ_FLAG_SCROLLABLE = 1u << 4,
    LV_OBJ_FLAG_EVENT_BUBBLE = 1u << 14, LV_OBJ_FLAG_USER_4 = 1u << 30,
};
enum { LV_STATE_PRESSED = 0x0020 };
enum { LV_ALIGN_LEFT_MID = 7, LV_ALIGN_RIGHT_MID = 8, LV_ALIGN_CENTER = 9 };
enum { LV_FLEX_FLOW_COLUMN = 1 };
enum { LV_LABEL_LONG_DOT = 1, LV_LABEL_LONG_CLIP = 4 };
enum { LV_SCROLLBAR_MODE_OFF = 0, LV_SCROLLBAR_MODE_AUTO = 3 };

lv_disp_t *lv_disp_get_default(void);
lv_obj_t *lv_disp_get_scr_act(lv_disp_t *d);
lv_obj_t *lv_disp_get_layer_top(lv_disp_t *d);
lv_obj_t *lv_obj_create(lv_obj_t *parent);
lv_obj_t *lv_btn_create(lv_obj_t *parent);
lv_obj_t *lv_label_create(lv_obj_t *parent);
void lv_label_set_text(lv_obj_t *o, const char *text);
void lv_label_set_long_mode(lv_obj_t *o, uint8_t mode);
void lv_obj_del_async(lv_obj_t *o);
lv_obj_t *lv_obj_get_child(const lv_obj_t *o, int32_t id);
uint32_t lv_obj_get_child_cnt(const lv_obj_t *o);
void lv_obj_get_coords(const lv_obj_t *o, lv_area_t *a);
void lv_obj_add_flag(lv_obj_t *o, uint32_t f);
void lv_obj_clear_flag(lv_obj_t *o, uint32_t f);
int lv_obj_has_flag(const lv_obj_t *o, uint32_t f);
void lv_obj_set_pos(lv_obj_t *o, lv_coord_t x, lv_coord_t y);
void lv_obj_set_size(lv_obj_t *o, lv_coord_t w, lv_coord_t h);
void lv_obj_set_width(lv_obj_t *o, lv_coord_t w);
void lv_obj_align(lv_obj_t *o, uint8_t align, lv_coord_t x, lv_coord_t y);
void lv_obj_set_flex_flow(lv_obj_t *o, uint8_t flow);
void lv_obj_set_scrollbar_mode(lv_obj_t *o, uint8_t mode);
void lv_obj_add_event_cb(lv_obj_t *o, lv_event_cb_t cb, uint32_t filter, void *user_data);
lv_obj_t *lv_event_get_target(lv_event_t *e);
void *lv_event_get_user_data(lv_event_t *e);
lv_indev_t *lv_indev_get_act(void);
void lv_indev_wait_release(lv_indev_t *i);
lv_timer_t *lv_timer_create(lv_timer_cb_t cb, uint32_t period, void *user_data);
void lv_timer_del(lv_timer_t *t);
void lv_obj_set_style_bg_color(lv_obj_t *o, lv_color_t c, uint32_t sel);
void lv_obj_set_style_bg_opa(lv_obj_t *o, uint8_t opa, uint32_t sel);
void lv_obj_set_style_border_color(lv_obj_t *o, lv_color_t c, uint32_t sel);
void lv_obj_set_style_border_width(lv_obj_t *o, lv_coord_t w, uint32_t sel);
void lv_obj_set_style_radius(lv_obj_t *o, lv_coord_t r, uint32_t sel);
void lv_obj_set_style_shadow_width(lv_obj_t *o, lv_coord_t w, uint32_t sel);
void lv_obj_set_style_outline_width(lv_obj_t *o, lv_coord_t w, uint32_t sel);
void lv_obj_set_style_pad_top(lv_obj_t *o, lv_coord_t v, uint32_t sel);
void lv_obj_set_style_pad_bottom(lv_obj_t *o, lv_coord_t v, uint32_t sel);
void lv_obj_set_style_pad_left(lv_obj_t *o, lv_coord_t v, uint32_t sel);
void lv_obj_set_style_pad_right(lv_obj_t *o, lv_coord_t v, uint32_t sel);
void lv_obj_set_style_pad_row(lv_obj_t *o, lv_coord_t v, uint32_t sel);
void lv_obj_set_style_text_color(lv_obj_t *o, lv_color_t c, uint32_t sel);
void lv_obj_set_style_text_font(lv_obj_t *o, const lv_font_t *f, uint32_t sel);
const lv_font_t *get_font(int index);

/* C library (the program's PLT) */
void *malloc(size_t n);
void *calloc(size_t n, size_t m);
void free(void *p);
size_t strlen(const char *s);
int strncmp(const char *a, const char *b, size_t n);
int strcmp(const char *a, const char *b);
char *strstr(const char *h, const char *n);
void *memmove(void *d, const void *s, size_t n);
void *memset(void *d, int c, size_t n);
int snprintf(char *s, size_t n, const char *fmt, ...);
double strtod(const char *s, char **end);
unsigned long strtoul(const char *s, char **end, int base);
int socket(int d, int t, int p);
int connect(int s, const void *a, unsigned l);
int setsockopt(int s, int lvl, int opt, const void *v, unsigned l);
long write(int fd, const void *b, size_t n);
long read(int fd, void *b, size_t n);
int close(int fd);
#endif
