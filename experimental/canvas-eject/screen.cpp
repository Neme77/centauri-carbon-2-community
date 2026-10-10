// Printer-screen adapter for the qualified LVGL 8 stock executable.
#include "hook.h"
#include "qualified.h"
#include "eject.h"
#include <climits>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <signal.h>
#include <fcntl.h>
#ifndef CC2_SCREEN_TEST
static_assert(sizeof(void*)==4,"ARM32 screen adapter required");
#else
void *screen_test_resolve(uintptr_t);
int screen_test_selection();
#endif
#ifndef CC2_EJECT_STATE
#define CC2_EJECT_STATE "/tmp/cc2-canvas-eject.state"
#define CC2_EJECT_CANCEL "/tmp/cc2-canvas-eject.cancel"
#endif
namespace {
template<class T>T fn(uintptr_t address){
#ifdef CC2_SCREEN_TEST
    return reinterpret_cast<T>(screen_test_resolve(address));
#else
    return reinterpret_cast<T>(address);
#endif
}
void *(*old_create)(void *);
void *button=nullptr,*label=nullptr,*timer=nullptr;
int armed=-1;double deadline=0;
double now(){timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
bool status(char *result,int &slot,double &mm){
    result[0]=0;
    FILE *f=fopen(CC2_EJECT_STATE,"r");if(!f)return false;
    long pid;int ready;char extra;
    bool ok=fscanf(f,"%ld %d %23s %lf %d %c",&pid,&slot,result,&mm,&ready,&extra)==5;
    fclose(f);bool valid=false;
    for(int i=0;i<=eject::stop_failed;i++)if(!strcmp(result,eject::name(static_cast<eject::Result>(i))))valid=true;
    return ok&&valid&&ready==1&&pid>1&&pid<=INT_MAX&&slot>=-1&&slot<=3&&
        std::isfinite(mm)&&mm>=0&&mm<=2000&&kill(pid,0)==0;
}
int selection(){
#ifdef CC2_SCREEN_TEST
    int slot=screen_test_selection();
#else
    int slot=*reinterpret_cast<int *>(GUI_FILAMENT_VIEW+0x44);
#endif
    return slot>=1&&slot<=4?slot-1:-1;
}
void update(void *){
    char state[24],text[96];int slot;double mm;
    bool available=status(state,slot,mm),busy=available&&!strcmp(state,"running");
    int chosen=selection();
    if(busy)snprintf(text,sizeof(text),"Stop ejection: slot %d (%.0f mm)",slot+1,mm);
    else if(armed==chosen&&now()<deadline)snprintf(text,sizeof(text),"Nozzle unloaded? Tap to eject %d",chosen+1);
    else if(!available)snprintf(text,sizeof(text),"Ejection unavailable");
    else if(chosen<0)snprintf(text,sizeof(text),"Select a slot to eject");
    else snprintf(text,sizeof(text),"Eject slot %d (%s)",chosen+1,slot<0?"ready":state);
    fn<void(*)(void*,const char*)>(GUI_LABEL_TEXT)(label,text);
    bool enabled=busy||(available&&chosen>=0&&fn<int(*)()>(GUI_MACHINE_STATUS)()==1);
    fn<void(*)(void*,uint16_t)>(enabled?GUI_CLEAR_STATE:GUI_ADD_STATE)(button,0x80);
}
void clicked(void *){
    char state[24];int slot;double mm;
    if(!status(state,slot,mm))return;
    if(!strcmp(state,"running")){
        int fd=open(CC2_EJECT_CANCEL,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
        if(fd>=0)close(fd);
        return;
    }
    int chosen=selection();if(chosen<0||fn<int(*)()>(GUI_MACHINE_STATUS)()!=1)return;
    if(armed!=chosen||now()>=deadline){armed=chosen;deadline=now()+5;update(nullptr);return;}
    char command[96];snprintf(command,sizeof(command),"CANVAS_MOTOR_CONTROL CHANNEL=%d EJECT=1 SPEED=0 DISTANCE=0 TIMEOUT=1",chosen);
    fn<void(*)(const char*)>(GUI_SEND_GCODE)(command);armed=-1;deadline=0;
}
void deleted(void *){
    if(timer)fn<void(*)(void*)>(GUI_TIMER_DELETE)(timer);
    button=label=timer=nullptr;armed=-1;deadline=0;
}
void *create(void *parent){
    void *panel=old_create(parent);if(!panel)return panel;
    // Keep the vendor viewport size. Explicitly enable vertical scrolling so
    // the new row remains reachable even if the outer page clips its children.
    fn<void(*)(void*)>(GUI_UPDATE_LAYOUT)(panel);
    int16_t height=fn<int16_t(*)(void*)>(GUI_HEIGHT)(panel);
    int16_t width=fn<int16_t(*)(void*)>(GUI_WIDTH)(panel);
    fn<void(*)(void*,uint32_t)>(GUI_ADD_FLAG)(panel,0x10);
    fn<void(*)(void*,int)>(GUI_SCROLL_DIR)(panel,12); // LV_DIR_VER
    button=fn<void*(*)(void*)>(GUI_BUTTON)(panel);
    fn<void(*)(void*,int16_t,int16_t)>(GUI_SIZE)(button,width-24,48);
    fn<void(*)(void*,int16_t,int16_t)>(GUI_POS)(button,12,height+8);
    label=fn<void*(*)(void*)>(GUI_LABEL)(button);
    fn<void(*)(void*,int,int16_t,int16_t)>(GUI_ALIGN)(label,9,0,0); // LV_ALIGN_CENTER
    fn<void*(*)(void*,void(*)(void*),int,void*)>(GUI_EVENT)(button,clicked,7,nullptr);
    fn<void*(*)(void*,void(*)(void*),int,void*)>(GUI_EVENT)(panel,deleted,33,nullptr);
    timer=fn<void*(*)(void(*)(void*),uint32_t,void*)>(GUI_TIMER)(update,200,nullptr);
    update(nullptr);return panel;
}
}
#ifndef CC2_SCREEN_TEST
__attribute__((constructor)) static void init(){
    try{old_create=reinterpret_cast<decltype(old_create)>(install_hook(reinterpret_cast<void *>(GUI_CREATE),reinterpret_cast<void *>(create),GUI_CREATE_ENTRY));}
    catch(const std::exception &e){fprintf(stderr,"Canvas screen adapter refused: %s\n",e.what());_exit(126);}
}
#endif
