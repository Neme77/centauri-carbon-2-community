/* Executes actual menu callbacks against deterministic LVGL stand-ins. */
#define ND_UI_FIXTURE 1
#define ND_PREFERENCE_PATH "./language-preference"
#include "display_language.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>

struct fake { int alive; int16_t x,y,w,h; const char *label;
    void (*callback)(event *); void *data; struct fake *parent; };
static struct fake pool[100];
static unsigned used;
static int themed_calls;
static int fail_at=-1, saved=-1, daemon_calls, refresh_calls, jumped;
static obj *allocate(obj *parent) {
    if ((int)used==fail_at) return NULL;
    assert(used<100);
    struct fake *f=&pool[used++]; memset(f,0,sizeof(*f));
    f->alive=1;f->parent=parent;return f;
}
static obj *object_create(obj *p) {return allocate(p);}
static obj *button_create(obj *p) {return allocate(p);}
static obj *label_create(obj *p) {return allocate(p);}
static void object_delete(obj *p) {
    struct fake *f=p; assert(f && f->alive);
    for(unsigned i=0;i<used;i++) if(pool[i].alive && pool[i].parent==f) object_delete(&pool[i]);
    f->alive=0;
}
static void position(obj *p,int16_t x,int16_t y){struct fake *f=p;assert(f->alive);f->x=x;f->y=y;}
static void size(obj *p,int16_t w,int16_t h){struct fake *f=p;assert(f->alive);f->w=w;f->h=h;}
static void background(obj *p,uint32_t c,uint32_t s){assert(((struct fake *)p)->alive);assert(c>>24==255);(void)s;}
static void opacity(obj *p,uint8_t o,uint32_t s){(void)p;(void)o;(void)s;}
static void border(obj *p,int16_t w,uint32_t s){(void)p;(void)w;(void)s;}
static void pad_left(obj *p,int16_t w,uint32_t s){(void)p;(void)w;(void)s;}
static void pad_right(obj *p,int16_t w,uint32_t s){(void)p;(void)w;(void)s;}
static void pad_top(obj *p,int16_t w,uint32_t s){(void)p;(void)w;(void)s;}
static void pad_bottom(obj *p,int16_t w,uint32_t s){(void)p;(void)w;(void)s;}
static void text_color(obj *p,uint32_t c,uint32_t s){(void)p;(void)s;assert(c>>24==255);}
static void text(obj *p,const char *t){struct fake *f=p;assert(f->alive);f->label=t;}
static void style(obj *p,const void *s,uint32_t v){(void)p;(void)s;(void)v;}
static void center(obj *p,int a,int16_t x,int16_t y){(void)p;(void)a;(void)x;(void)y;}
static void *event_data(event *e){return ((struct fake *)e)->data;}
static void *on_event(obj *p,void (*cb)(event *),int code,void *d){
    struct fake *f=p;assert(code==4);f->callback=cb;f->data=d;return f;
}
static const char *image_path(unsigned id){assert(id==0x1ec);return "current-theme-back";}
static obj *themed_button(obj *p,int16_t x,int16_t y,int16_t w,int16_t h,
        const char *label,const void *image,int code,void (*cb)(event *),void *data){
    assert(!label && !strcmp(image,"current-theme-back"));
    assert(x==358 && y==211 && w==88 && h==34 && code==4);
    themed_calls++;
    obj *b=allocate(p);if(!b)return NULL;
    position(b,x,y);size(b,w,h);on_event(b,cb,code,data);return b;
}
static void native_save_language(int i){assert(i>=0 && i<11);saved=i;}
static void daemon_language(const char *s){assert(!strcmp(s,"en")||!strcmp(s,"zh"));daemon_calls++;}
static void refresh_menu(void){refresh_calls++;}
static void refresh_views(void){refresh_calls++;}
static void jump(int i){jumped=i;}
static void reset(void){
    memset(pool,0,sizeof(pool));used=0;fail_at=-1;fixture_menu=NULL;
    language=(struct nd_language){0};fixture_native=1;saved=-1;daemon_calls=refresh_calls=jumped=0;
    assert(unlink(preference)==0 || access(preference,F_OK)!=0);
}
int main(void){
    char path[]="/var/tmp/cc2-ui-XXXXXX";char *dir=mkdtemp(path);assert(dir);assert(chdir(dir)==0);
    reset();assert(nd_display_language_menu(NULL));
    for(int i=0;i<14;i++){
        struct fake *f=buttons[i];assert(f && f->callback);
        assert(f->x>=0 && f->y>=0 && f->x+f->w<=462 && f->y+f->h<=211);
        f->callback(f);assert(effective()==i);assert(saved==nd_language_native(i));
        assert(!strcmp(nd_display_language_name(),nd_language_names[i]));
        for(unsigned j=0;j<648;j++)assert(nd_display_get_text(j));
    }
    assert(!strcmp(nd_display_get_text(648),""));
    assert(daemon_calls==14 && refresh_calls==28);
    language=(struct nd_language){0};assert(effective()==13); /* restart */
    struct fake invalid={0};choose(&invalid);assert(daemon_calls==14);
    int outside=14;invalid.data=&outside;choose(&invalid);assert(daemon_calls==14);
    assert(themed_calls==1);
    struct fake *back_button=((struct fake *)back_label)->parent;
    assert(back_button->callback);back_button->callback(back_button);assert(jumped==13);
    assert(nd_display_language_menu(NULL)); /* replace existing menu */
    object_delete(fixture_menu);fixture_menu=NULL;
    for(int allocation=0;allocation<33;allocation++){
        reset();fail_at=allocation;nd_display_language_menu(NULL);
    }
    reset();assert(nd_display_language_menu(NULL));
    /* An unwritable destination must not change native language or daemon. */
    assert(unlink(preference)!=0);assert(mkdir(preference,0700)==0);
    struct fake *f=buttons[11];f->callback(f);
    assert(fixture_native==1 && saved==-1 && daemon_calls==0);
    assert(error_label && ((struct fake *)error_label)->label[0]);
    assert(rmdir(preference)==0);assert(chdir("/")==0);assert(rmdir(dir)==0);
    puts("PASS actual UI callbacks: 14 languages, bounds, persistence, event rejection, recreation, allocation and save failures");
    return 0;
}
