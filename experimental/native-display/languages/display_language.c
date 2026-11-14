/* Private exact-build prototype. Build verification must approve every hook.
 * No motion, heating, Canvas or native network command is issued here.
 */
#define _POSIX_C_SOURCE 200809L
#include "language_core.h"
#include "native_language_data.h"
#include <stdint.h>
#include <stddef.h>

typedef void obj;
typedef void event;
#ifdef ND_UI_FIXTURE
#define FN(ret,name,args,address) static ret name args
#else
#define FN(ret,name,args,address) static ret (*const name) args = (ret (*) args)(uintptr_t)(address)
#endif
FN(obj *, object_create, (obj *), ND_LV_OBJ_CREATE);
FN(obj *, button_create, (obj *), ND_LV_BTN_CREATE);
FN(const char *, image_path, (unsigned), ND_GET_UI_IMAGE_PATH);
FN(obj *, themed_button, (obj *, int16_t, int16_t, int16_t, int16_t, const char *, const void *, int, void (*)(event *), void *), ND_CREATE_BTN_CENTER_IMG);
FN(obj *, label_create, (obj *), ND_LV_LABEL_CREATE);
FN(void, object_delete, (obj *), ND_LV_OBJ_DEL);
FN(void, position, (obj *, int16_t, int16_t), ND_LV_OBJ_SET_POS);
FN(void, size, (obj *, int16_t, int16_t), ND_LV_OBJ_SET_SIZE);
FN(void, background, (obj *, uint32_t, uint32_t), ND_LV_OBJ_SET_STYLE_BG_COLOR);
FN(void, opacity, (obj *, uint8_t, uint32_t), ND_LV_OBJ_SET_STYLE_BG_OPA);
FN(void, border, (obj *, int16_t, uint32_t), ND_LV_OBJ_SET_STYLE_BORDER_WIDTH);
FN(void, pad_left, (obj *, int16_t, uint32_t), ND_LV_OBJ_SET_STYLE_PAD_LEFT);
FN(void, pad_right, (obj *, int16_t, uint32_t), ND_LV_OBJ_SET_STYLE_PAD_RIGHT);
FN(void, pad_top, (obj *, int16_t, uint32_t), ND_LV_OBJ_SET_STYLE_PAD_TOP);
FN(void, pad_bottom, (obj *, int16_t, uint32_t), ND_LV_OBJ_SET_STYLE_PAD_BOTTOM);
FN(void, text_color, (obj *, uint32_t, uint32_t), ND_LV_OBJ_SET_STYLE_TEXT_COLOR);
FN(void, text, (obj *, const char *), ND_LV_LABEL_SET_TEXT);
FN(void, style, (obj *, const void *, uint32_t), ND_LV_OBJ_ADD_STYLE);
FN(void, center, (obj *, int, int16_t, int16_t), ND_LV_OBJ_ALIGN);
FN(void *, event_data, (event *), ND_LV_EVENT_GET_USER_DATA);
FN(void *, on_event, (obj *, void (*)(event *), int, void *), ND_LV_OBJ_ADD_EVENT_CB);
FN(void, native_save_language, (int), ND_DATA_SET_LANGUAGE);
FN(void, daemon_language, (const char *), ND_DAEMON_REQUEST_SET_LANGUAGE);
FN(void, refresh_menu, (void), ND_SETTING_MENU_UPDATE_TEXT);
FN(void, refresh_views, (void), ND_UI_VIEW_REFRESH_ALL_WHILE_PAGE_CHANGE);
FN(void, jump, (int), ND_UI_VIEW_JUMP);

static struct nd_language language;
static obj *buttons[ND_LANGUAGES];
static obj *back_label;
static obj *error_label;
static int indices[ND_LANGUAGES];
#ifndef ND_PREFERENCE_PATH
#define ND_PREFERENCE_PATH "/opt/usr/cc2-native-display-language"
#endif
static const char preference[] = ND_PREFERENCE_PATH;

#ifdef ND_UI_FIXTURE
static int fixture_native;
static obj *fixture_menu;
static volatile int *native_index(void) { return &fixture_native; }
static obj **menu_slot(void) { return &fixture_menu; }
#else
static volatile int *native_index(void) { return (volatile int *)(uintptr_t)0x2565e4; }
static obj **menu_slot(void) { return (obj **)(uintptr_t)0x25b5f4; }
#endif

static int effective(void) {
    if (!language.loaded) nd_language_read(&language, preference);
    return nd_language_effective(&language, *native_index());
}

const char *nd_display_get_text(unsigned index) {
    int selected=effective();
    if (index >= ND_STRINGS || selected < 0) return "";
    if (selected >= ND_NATIVE_LANGUAGES) return nd_new_text[selected-ND_NATIVE_LANGUAGES][index];
#ifdef ND_UI_FIXTURE
    return index == 4 ? "Back" : "Native text";
#else
    const char *const *table=(const char *const *)(uintptr_t)0x12ca28;
    return table[(unsigned)selected*ND_STRINGS+index];
#endif
}

const char *nd_display_language_name(void) {
    int selected=effective();
    return selected >= 0 && selected < ND_LANGUAGES ? nd_language_names[selected] : "";
}

/* Preserve original native bounds even when a display-only locale is active. */
void nd_display_set_native_language(unsigned selected) {
    if (selected >= ND_NATIVE_LANGUAGES) return;
    uintptr_t caller=(uintptr_t)__builtin_return_address(0);
    if (caller == 0x59ea8u) {
        /* An explicit choice in the original first-run wizard supersedes ours. */
        if (!nd_language_save(&language, preference, (int)selected))
            language.selected=-1;
    }
    *native_index()=(int)selected;
}

static void selection_refresh(void) {
    int selected=effective();
    for (int i=0;i<ND_LANGUAGES;i++) if (buttons[i])
        background(buttons[i],i==selected ? 0xff007f96u : 0xff233247u,0);
    if (back_label) text(back_label,nd_display_get_text(4));
}

static void choose(event *ev) {
    int *id=(int *)event_data(ev);
    uintptr_t address=(uintptr_t)id,first=(uintptr_t)indices;
    if (address < first || address >= first+sizeof(indices) || (address-first)%sizeof(int)) return;
    int selected=*id;
    if (!nd_language_save(&language,preference,selected)) {
        if(error_label) text(error_label,"Language could not be saved");
        return;
    }
    int base=nd_language_native(selected);
    *native_index()=base;
    native_save_language(base);
    daemon_language(base==0 ? "zh" : "en");
    if(error_label) text(error_label,"");
    selection_refresh();
    refresh_menu();
    refresh_views();
}

static void back(event *ev) { (void)ev; jump(13); }

static obj *caption(obj *parent,const char *value) {
    obj *label=label_create(parent);
    if (!label) return NULL;
    style(label,(const void *)(uintptr_t)0x132c444,0);
    text_color(label,0xffedf3fau,0);
    text(label,value);
    center(label,9,0,0);
    return label;
}

obj *nd_display_language_menu(obj *parent) {
    obj **slot=menu_slot();
    if (*slot) {object_delete(*slot);*slot=NULL;}
    back_label=NULL;error_label=NULL;
    for (int i=0;i<ND_LANGUAGES;i++) buttons[i]=NULL;
    obj *menu=object_create(parent);
    if (!menu) return NULL;
    *slot=menu;
    position(menu,0,0);size(menu,462,254);
    background(menu,0xff171e29u,0);opacity(menu,255,0);border(menu,0,0);
    pad_left(menu,0,0);pad_right(menu,0,0);pad_top(menu,0,0);pad_bottom(menu,0,0);
    for (int i=0;i<ND_LANGUAGES;i++) {
        indices[i]=i;
        obj *button=button_create(menu);
        if (!button) continue;
        buttons[i]=button;
        position(button,(int16_t)(2+(i%3)*148),(int16_t)(2+(i/3)*41));
        size(button,142,35);
        caption(button,nd_language_names[i]);
        on_event(button,choose,4,&indices[i]);
    }
    /* Reuse the native themed Back resource and event helper (no generic LVGL fill).
     * Native geometry is 88 x 34; leave all fourteen choices unobstructed. */
    obj *button=themed_button(menu,358,211,88,34,NULL,image_path(0x1ec),4,back,NULL);
    if (button) {
        back_label=label_create(button);
        if (back_label) {
            style(back_label,(const void *)(uintptr_t)0x132c57c,0);
            text(back_label,nd_display_get_text(4));
            center(back_label,9,0,0);
        }
    }
    error_label=caption(menu,"");
    if(error_label) {position(error_label,2,215);size(error_label,300,24);}
    selection_refresh();
    return menu;
}
