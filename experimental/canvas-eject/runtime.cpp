// ABI bridge for the exact stock 02.01.00.00 Canvas library. No vendor binary is distributed.
#include "eject.h"
#include "hook.h"
#include "qualified.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <mutex>
#include <string>
#include <ctime>
#include <sys/stat.h>
#include <signal.h>
static_assert(sizeof(void*)==4,"ARM32 runtime required");
namespace {
const char *state_path="/tmp/cc2-canvas-eject.state",*cancel_path="/tmp/cc2-canvas-eject.cancel";
std::mutex frames,preload;
std::atomic<bool> active{false},ready{false};
double last_frame=0;
int selected=-1;
using Command=void(*)(void*,void*);
Command old_motor;
void (*old_parse)(void*,const void*),(*old_prefeed)(void*),(*old_ready)(void*),(*old_shutdown)(void*);
int (*integer)(void*,const std::string&,int,int,int);
double (*pause_reactor)(void*,double);
bool (*move)(void*,uint8_t,const void*,bool);
bool (*stop_motor)(void*,uint8_t,bool,bool);
template<class T>T resolve(const char *name){void *p=dlsym(RTLD_DEFAULT,name);if(!p)throw std::runtime_error("Missing Canvas ABI symbol");return reinterpret_cast<T>(p);}
double clock_now(){timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec/1e9;}
template<class T>T load(void *p,size_t off){T v;std::memcpy(&v,static_cast<unsigned char *>(p)+off,sizeof(v));return v;}
void publish(eject::Result r,double travel){
    // Preserve fault readback even when a malformed position is non-finite.
    travel=std::isfinite(travel)?std::fmin(std::fabs(travel),2000):0;
    FILE *f=fopen("/tmp/cc2-canvas-eject.state.new","w");if(!f)return;
    fprintf(f,"%ld %d %s %.2f %d\n",static_cast<long>(getpid()),selected,eject::name(r),travel,ready?1:0);
    if(fclose(f)==0)rename("/tmp/cc2-canvas-eject.state.new",state_path);
}
struct IO {
    void *self,*protocol,*reactor;int slot;
    eject::Sample sample(){
        std::lock_guard<std::mutex> lock(frames);
        void *s=static_cast<unsigned char *>(protocol)+0xe8;
        eject::Sample out;out.fresh=last_frame>0&&clock_now()-last_frame<=0.5;
        out.connected=load<uint8_t>(s,0x64)==1;
        auto &state=*reinterpret_cast<std::string *>(static_cast<unsigned char *>(self)+0x2f4);
        out.idle=ready&&(state=="standby"||state=="complete"||state=="cancelled");
        out.head_present=load<uint8_t>(s,0x61a)!=0;out.active=load<int32_t>(s,0x610);
        out.present=load<uint8_t>(s,0x5c+slot)!=0;
        out.moving=load<uint8_t>(s,0x4c+slot)!=0;
        out.fault=load<uint8_t>(s,0x48+slot)||load<uint8_t>(s,0x50+slot)||load<uint8_t>(s,0x54+slot)||load<uint8_t>(s,0x58+slot)||load<uint8_t>(s,0x60+slot);
        out.position=load<double>(s,8+slot*8);return out;
    }
    double now(){return clock_now();}
    void wait(){pause_reactor(reactor,clock_now()+0.05);}
    bool cancelled(){return access(cancel_path,F_OK)==0||!ready;}
    // Use the same feeder transport as stock motor control. Rocker direction
    // commands are maintenance operations, not part of stock filament moves.
    bool reverse(){const int16_t motor[3]={-1200,15,100};return move(protocol,slot,motor,true);}
    bool stop(){
        if(!stop_motor(protocol,slot,true,true))return false;
        // The ACK precedes the next status packet. Confirm physical stop before
        // reporting a result, including on cancellation and failed travel.
        double deadline=clock_now()+1;
        do{wait();auto s=sample();if(s.fresh&&!s.moving)return true;}while(clock_now()<deadline);
        return false;
    }
    void publish(eject::Result r,double mm){::publish(r,mm);}
};
void motor(void *self,void *shared){
    void *gcmd=load<void *>(shared,0);
    if(integer(gcmd,"EJECT",0,0,1)!=1){old_motor(self,shared);return;}
    int slot=integer(gcmd,"CHANNEL",-1,0,3);
    {std::lock_guard<std::mutex> lock(preload);if(active.exchange(true))throw std::runtime_error("Canvas ejection already running");}
    struct Done{~Done(){active=false;unlink(cancel_path);}}done;
    selected=slot;unlink(cancel_path);
    IO io{self,load<void *>(self,0),load<void *>(self,0x20),slot};
    eject::Result r;
    try {r=eject::run(io);}catch(...){publish(eject::fault,0);throw;}
    if(r!=eject::complete&&r!=eject::empty)throw std::runtime_error(std::string("Canvas ejection: ")+eject::name(r));
}
void parse(void *self,const void *data){
    const uintptr_t *vector=static_cast<const uintptr_t *>(data);
    if(vector[1]<vector[0]||vector[1]-vector[0]<39)return;
    std::lock_guard<std::mutex> lock(frames);
    bool initialized=load<uint8_t>(self,0x94c)!=0;
    old_parse(self,data);
    // Stock's first-frame initialization uses a distinct parsing path. Only
    // a later regular frame can qualify sensor/position data for ejection.
    if(initialized)last_frame=clock_now();
}
void prefeed(void *self){std::lock_guard<std::mutex> lock(preload);if(!active)old_prefeed(self);}
void on_ready(void *self){old_ready(self);ready=true;selected=-1;publish(eject::empty,0);}
void on_shutdown(void *self){ready=false;unlink(state_path);old_shutdown(self);}
}
__attribute__((constructor)) static void init(){
    char executable[256];ssize_t n=readlink("/proc/self/exe",executable,sizeof(executable)-1);
    if(n<0)return;
    executable[n]=0;
    const char *base=strrchr(executable,'/');
    if(!base||strcmp(base+1,"elegoo_printer")||access("/opt/usr/cc2-canvas-eject-disabled",F_OK)==0)return;
    try {
        integer=resolve<decltype(integer)>("_ZN12GCodeCommand7get_intERKNSt7__cxx1112basic_stringIcSt11char_traitsIcESaIcEEEiii");
        pause_reactor=resolve<decltype(pause_reactor)>("_ZN13SelectReactor5pauseEd");
        move=resolve<decltype(move)>(MOVE_SYMBOL);stop_motor=resolve<decltype(stop_motor)>(STOP_SYMBOL);
        // Hook every guard before making the new motion mode available.
        old_parse=reinterpret_cast<decltype(old_parse)>(install_hook(resolve<void *>(PARSE_SYMBOL),reinterpret_cast<void *>(parse),PARSE_ENTRY));
        old_prefeed=reinterpret_cast<decltype(old_prefeed)>(install_hook(resolve<void *>(PREFEED_SYMBOL),reinterpret_cast<void *>(prefeed),PREFEED_ENTRY));
        old_ready=reinterpret_cast<decltype(old_ready)>(install_hook(resolve<void *>(READY_SYMBOL),reinterpret_cast<void *>(on_ready),READY_ENTRY));
        old_shutdown=reinterpret_cast<decltype(old_shutdown)>(install_hook(resolve<void *>(SHUTDOWN_SYMBOL),reinterpret_cast<void *>(on_shutdown),SHUTDOWN_ENTRY));
        old_motor=reinterpret_cast<Command>(install_hook(resolve<void *>(MOTOR_SYMBOL),reinterpret_cast<void *>(motor),MOTOR_ENTRY));
    }catch(const std::exception &e){fprintf(stderr,"Canvas eject runtime refused: %s\n",e.what());_exit(126);}
}
