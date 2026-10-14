#define CC2_SCREEN_TEST
#define CC2_EJECT_STATE "screen-test.state"
#define CC2_EJECT_CANCEL "screen-test.cancel"
#include "screen.cpp"
#include <cassert>
#include <string>
static int chosen=3,machine=1,height=400;static bool disabled=false;
static std::string text,command;static void(*event_click)(void*),(*event_delete)(void*),(*timer_callback)(void*);
static void *object(void *p){return p;}
static void label_text(void*,const char *s){text=s;}
static void clear_state(void*,uint16_t){disabled=false;}
static void add_state(void*,uint16_t){disabled=true;}
static int machine_state(){return machine;}
static void send(const char *s){command=s;}
static void mock_timer_delete(void*){timer_callback=nullptr;}
static int16_t get_height(void*){return height;}
static int16_t get_width(void*){return 320;}
static void update_layout(void*){}
static void add_flag(void*,uint32_t flags){assert(flags==0x10);}
static void scroll_dir(void*,int dir){assert(dir==12);}
static void size(void*,int16_t w,int16_t h){assert(w==296&&h==48);}
static void pos(void*,int16_t x,int16_t y){assert(x==12&&y==408);}
static void align(void*,int a,int16_t,int16_t){assert(a==9);}
static void *event(void*,void(*cb)(void*),int filter,void*){if(filter==7)event_click=cb;else{assert(filter==33);event_delete=cb;}return nullptr;}
static void *make_timer(void(*cb)(void*),uint32_t ms,void*){assert(ms==200);timer_callback=cb;return reinterpret_cast<void *>(2);}
int screen_test_selection(){return chosen;}
void *screen_test_resolve(uintptr_t address){
#define MAP(a,f) if(address==a)return reinterpret_cast<void *>(f)
 MAP(GUI_LABEL_TEXT,label_text);MAP(GUI_CLEAR_STATE,clear_state);MAP(GUI_ADD_STATE,add_state);MAP(GUI_MACHINE_STATUS,machine_state);MAP(GUI_SEND_GCODE,send);MAP(GUI_TIMER_DELETE,mock_timer_delete);MAP(GUI_HEIGHT,get_height);MAP(GUI_WIDTH,get_width);MAP(GUI_UPDATE_LAYOUT,update_layout);MAP(GUI_ADD_FLAG,add_flag);MAP(GUI_SCROLL_DIR,scroll_dir);MAP(GUI_BUTTON,object);MAP(GUI_SIZE,size);MAP(GUI_POS,pos);MAP(GUI_LABEL,object);MAP(GUI_ALIGN,align);MAP(GUI_EVENT,event);MAP(GUI_TIMER,make_timer);
 assert(false);return nullptr;
}
static void write_status(const char *result){FILE *f=fopen(CC2_EJECT_STATE,"w");assert(f);fprintf(f,"%ld 2 %s 20 1\n",(long)getpid(),result);fclose(f);}
int main(){
 unlink(CC2_EJECT_STATE);unlink(CC2_EJECT_CANCEL);old_create=object;
 assert(create(reinterpret_cast<void *>(1)));assert(disabled&&height==400);assert(event_click&&event_delete&&timer_callback);
 write_status("unrecognized");timer_callback(nullptr);assert(disabled);event_click(nullptr);assert(command.empty());
 write_status("empty");timer_callback(nullptr);assert(!disabled&&text.find("slot 3")!=std::string::npos);
 event_click(nullptr);assert(command.empty()&&text.find("Tap")!=std::string::npos);
 chosen=4;event_click(nullptr);assert(command.empty());event_click(nullptr);
 assert(command=="CANVAS_MOTOR_CONTROL CHANNEL=3 EJECT=1 SPEED=0 DISTANCE=0 TIMEOUT=1");
 command.clear();machine=2;timer_callback(nullptr);assert(disabled);event_click(nullptr);assert(command.empty());
 machine=1;chosen=0;timer_callback(nullptr);assert(disabled);
 write_status("running");timer_callback(nullptr);assert(!disabled&&text.find("Stop")!=std::string::npos);
 event_click(nullptr);assert(!access(CC2_EJECT_CANCEL,F_OK));assert(command.empty());
 event_delete(nullptr);assert(!timer_callback&&!button&&!label&&!timer);
 unlink(CC2_EJECT_STATE);unlink(CC2_EJECT_CANCEL);puts("PASS native screen callback registration, selection, confirmation, idle guard, progress, cancellation and cleanup");
}
