// Experimental bridge for one hash-pinned vendor executable. Not a general ABI.
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <vector>
#include <stdexcept>
#include <utility>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "fingerprints.h"
#if !defined(CC2_SYNTAX_ONLY) && !defined(CC2_BRIDGE_TEST)
static_assert(sizeof(void*)==4,"ARM32 build required");
#endif
namespace bridge {
using byte=unsigned char;
#ifdef CC2_BRIDGE_TEST
uintptr_t test_resolve(uintptr_t address);
#endif
template<class F> F original(uintptr_t address){
#ifdef CC2_BRIDGE_TEST
 return reinterpret_cast<F>(test_resolve(address));
#else
 return reinterpret_cast<F>(address);
#endif
}
void *at(void *p,size_t offset){return static_cast<byte*>(p)+offset*(sizeof(void*)/4);}
struct Shared {
 void *object=nullptr,*control=nullptr;
 Shared()=default;
 Shared(const Shared &other){original<void(*)(void*,const void*)>(0x2cfbe0)(this,&other);}
 Shared(Shared &&other) noexcept:object(other.object),control(other.control){other.object=other.control=nullptr;}
 Shared &operator=(Shared other) noexcept {std::swap(object,other.object);std::swap(control,other.control);return *this;}
 ~Shared(){original<void(*)(void*)>(0x2cf490)(this);}
 static Shared from(const void *slot){Shared result;original<void(*)(void*,const void*)>(0x2cfbe0)(&result,slot);return result;}
};
#if !defined(CC2_SYNTAX_ONLY) && !defined(CC2_BRIDGE_TEST)
static_assert(sizeof(Shared)==8,"shared_ptr bridge size");
#endif
struct Vec {byte *begin,*end,*capacity;};
Vec view(void *slot){Vec v;std::memcpy(&v,slot,sizeof(v));return v;}
size_t count(Vec v,size_t stride){
 uintptr_t b=reinterpret_cast<uintptr_t>(v.begin),e=reinterpret_cast<uintptr_t>(v.end),c=reinterpret_cast<uintptr_t>(v.capacity);
 if(e<b||c<e||(e-b)%stride|| (e-b)/stride>65536)throw std::runtime_error("CC2 reactor vector invariant");
 return (e-b)/stride;
}
void drop_at(void *slot,size_t index){
 Vec v=view(slot);size_t n=count(v,sizeof(Shared));if(index>=n)throw std::runtime_error("CC2 reactor erase invariant");
 Shared release=Shared::from(v.begin+index*sizeof(Shared));
 // Release the container's original count, then transfer remaining ownership.
 original<void(*)(void*)>(0x2cf490)(v.begin+index*sizeof(Shared));
 std::memmove(v.begin+index*sizeof(Shared),v.begin+(index+1)*sizeof(Shared),(n-index-1)*sizeof(Shared));
 v.end-=sizeof(Shared);std::memset(v.end,0,sizeof(Shared));std::memcpy(slot,&v,sizeof(v));
}
Shared take_callback(void *owner,void *callback){
 Shared keep;
 for(size_t offset:{size_t(96),size_t(108)}){
  void *slot=at(owner,offset);Vec v=view(slot);size_t n=count(v,sizeof(Shared));
  for(size_t i=0;i<n;){
   void *object;std::memcpy(&object,v.begin+i*sizeof(Shared),sizeof(object));
   if(object==callback){if(!keep.object)keep=Shared::from(v.begin+i*sizeof(Shared));drop_at(slot,i);v=view(slot);n=count(v,sizeof(Shared));}
   else ++i;
  }
 }
 if(!keep.object)throw std::runtime_error("CC2 callback missing owner");
 return keep;
}
struct Json {
 alignas(8) byte data[16];bool initialized=false;
 ~Json(){if(initialized)original<void(*)(void*)>(0x2d0914)(data);}
 void copy(const void *source){original<void(*)(void*,const void*)>(0x2da7dc)(data,source);initialized=true;}
};
const double never=9999999999999999.0;
Shared current(void *owner){return original<Shared(*)(void*)>(0x46cf54)(owner);}
Shared dispatch(void *owner){return Shared::from(at(owner,144));}
void end_greenlet(void *owner,const Shared &old){original<void(*)(void*,Shared)>(0x46c518)(owner,old);}
double now(){return original<double(*)()>(0x2bc578)();}
bool process(void *owner){return *static_cast<byte*>(at(owner,12))!=0;}
double &next_timer(void *owner){return *static_cast<double*>(at(owner,32));}
void *handler(void *owner,int fd){
 // GCC ARM32 RB-tree: map header +4, root +8, node key +16, shared value +20.
 void *node;std::memcpy(&node,at(owner,240+8),sizeof(node));
 for(size_t i=0;node&&i<65536;i++){
  int key;std::memcpy(&key,at(node,16),4);
  if(key==fd)return at(node,20);
  void *next;std::memcpy(&next,at(node,fd<key?8:12),sizeof(next));node=next;
 }
 if(node)throw std::runtime_error("CC2 reactor map invariant");
 return nullptr;
}
void *handler_object(void *owner,int fd){void *slot=handler(owner,fd),*p=nullptr;if(slot)std::memcpy(&p,slot,sizeof(p));return p;}
bool callable(void *function){return original<bool(*)(const void*)>(0x2c3668)(function)==false;}
struct Event {pollfd fd;Shared handle;};
static unsigned long callback_count=0,exception_count=0;
static void exception_note(const char *where) noexcept {
 ++exception_count;
#ifdef CC2_BRIDGE_TEST
 (void)where;return;
#endif
 int fd=open("/opt/usr/cc2-reactor-runtime-v2/exceptions.log",O_WRONLY|O_CREAT|O_APPEND,0600);
 if(fd>=0){struct stat st;if(fstat(fd,&st)==0&&st.st_size>65536&&ftruncate(fd,0)!=0){close(fd);return;}dprintf(fd,"exception pid=%ld count=%lu: %s; propagating\n",static_cast<long>(getpid()),exception_count,where);close(fd);}
}
static void status_note(void *owner,double eventtime) noexcept {
#ifndef CC2_BRIDGE_TEST
 static double last=0;
 if(eventtime-last<60)return;
 last=eventtime;
 try{
  size_t normal=count(view(at(owner,96)),sizeof(Shared));
  size_t async=count(view(at(owner,108)),sizeof(Shared));
  size_t timers=count(view(at(owner,16)),sizeof(Shared));
  int fd=open("/opt/usr/cc2-reactor-runtime-v2/reactor-state.txt",O_WRONLY|O_CREAT|O_TRUNC,0600);
  if(fd>=0){dprintf(fd,"pid=%ld\ncompleted_callbacks=%lu\nexceptions=%lu\nnormal_callbacks=%lu\nasync_callbacks=%lu\ntimers=%lu\n",static_cast<long>(getpid()),callback_count,exception_count,static_cast<unsigned long>(normal),static_cast<unsigned long>(async),static_cast<unsigned long>(timers));close(fd);}
 }catch(...){/* Diagnostics must not change scheduler failure handling. */}
#else
 (void)owner;(void)eventtime;
#endif
}
extern "C" double cc2_callback(void *self,double eventtime){
 void *owner;std::memcpy(&owner,self,sizeof(owner));
 Shared keep=take_callback(owner,self);
 try{
  Shared timer=Shared::from(at(self,8));
  original<void(*)(void*,Shared)>(0x46aa30)(owner,timer);
  Json result;
  original<void(*)(void*,const void*,double)>(0x4714c8)(result.data,at(self,16),eventtime);result.initialized=true;
  Shared completion=Shared::from(at(self,32));
  if(completion.object){Json copy;copy.copy(result.data);original<void(*)(void*,void*)>(0x469664)(completion.object,copy.data);}
  ++callback_count;return never;
 }catch(...){exception_note("callback");throw;}
}
extern "C" double cc2_timers(void *owner,double eventtime,bool busy){
 double &next=next_timer(owner);
 if(eventtime<next)return busy?0.0:std::min(1.0,std::max(0.001,next-eventtime));
 next=never;Shared previous=dispatch(owner);
 Vec v=view(at(owner,16));size_t n=count(v,sizeof(Shared));
 std::vector<Shared> pending;pending.reserve(n);
 for(size_t i=0;i<n;i++)pending.push_back(Shared::from(v.begin+i*sizeof(Shared)));
 for(auto &t:pending){
  if(!t.object)continue;
  double &wake=*static_cast<double*>(at(t.object,16));double waketime=wake;
  if(eventtime>=waketime){
   wake=never;wake=waketime=original<double(*)(const void*,double)>(0x472a34)(t.object,eventtime);
   if(previous.object!=dispatch(owner).object){next=std::min(next,waketime);end_greenlet(owner,previous);return 0.0;}
  }
  next=std::min(next,waketime);
 }
 return 0.0;
}
extern "C" void cc2_poll(void *owner){
 Shared previous=current(owner);
 // Assignment into original shared_ptr storage through its original operator=.
 original<void(*)(void*,const void*)>(0x470e08)(at(owner,144),&previous);
 bool busy=true;double eventtime=now();
 try{
  while(process(owner)){
   status_note(owner,eventtime);
   double timeout=cc2_timers(owner,eventtime,busy);busy=false;
   Vec v=view(at(owner,264));size_t n=count(v,sizeof(pollfd));
   int res=original<int(*)(pollfd*,nfds_t,int)>(0x2bc7e8)(reinterpret_cast<pollfd*>(v.begin),n,static_cast<int>(std::ceil(timeout*1000.0)));
   eventtime=now();if(res<=0)continue;
   v=view(at(owner,264));n=count(v,sizeof(pollfd));
   std::vector<Event> ready;ready.reserve(n);
   for(size_t i=0;i<n;i++){
    pollfd fd;std::memcpy(&fd,v.begin+i*sizeof(fd),sizeof(fd));
    if(!(fd.revents&(POLLIN|POLLHUP|POLLOUT)))continue;
    void *slot=handler(owner,fd.fd);if(!slot)continue;
    Shared h=Shared::from(slot);if(h.object)ready.push_back(Event{fd,std::move(h)});
   }
   for(auto &event:ready){
    const pollfd &fd=event.fd;void *h=event.handle.object;
    if(handler_object(owner,fd.fd)!=h)continue;
    busy=true;
    if((fd.revents&(POLLIN|POLLHUP))&&callable(h)){
     original<void(*)(const void*,double)>(0x3d566c)(h,eventtime);
     if(previous.object!=dispatch(owner).object){end_greenlet(owner,previous);eventtime=now();break;}
    }
    if(handler_object(owner,fd.fd)==h&&(fd.revents&POLLOUT)&&callable(at(h,16))){
     original<void(*)(const void*,double)>(0x3d566c)(at(h,16),eventtime);
     if(previous.object!=dispatch(owner).object){end_greenlet(owner,previous);eventtime=now();break;}
    }
   }
  }
  Shared empty;original<void(*)(void*,const void*)>(0x470e08)(at(owner,144),&empty);
 }catch(...){exception_note("poll");throw;}
}
struct WaitGuard {
 void *slot;void *object;
 ~WaitGuard(){
  Vec v=view(slot);size_t n=count(v,sizeof(Shared));
  for(size_t i=0;i<n;i++){void *p;std::memcpy(&p,v.begin+i*sizeof(Shared),sizeof(p));if(p==object){drop_at(slot,i);return;}}
 }
};
// Original nontrivial JSON return uses the ARM hidden result pointer in r0.
extern "C" void cc2_wait(void *out,void *self,double waketime,const void *fallback){
 void *result=at(self,8);
 if(original<bool(*)(const void*)>(0x3095cc)(result)){
  void *owner;std::memcpy(&owner,self,sizeof(owner));Shared wait=current(owner);
  if(!wait.object)throw std::runtime_error("CC2 completion has no current coroutine");
  void *waiting=at(self,24);
  original<void(*)(void*,const void*)>(0x47124c)(waiting,&wait);
  WaitGuard guard{waiting,wait.object};
  try{original<double(*)(void*,double)>(0x46b23c)(owner,waketime);}
  catch(...){exception_note("completion wait");throw;}
  if(original<bool(*)(const void*)>(0x3095cc)(result)){
   original<void(*)(void*,const void*)>(0x2da7dc)(out,fallback);return;
  }
 }
 original<void(*)(void*,const void*)>(0x2da7dc)(out,result);
}
}
#if !defined(CC2_SYNTAX_ONLY) && !defined(CC2_BRIDGE_TEST)
static void fail(const char *reason){dprintf(2,"CC2 REACTOR V2 STOP: %s\n",reason);_exit(125);}
static void patch(uintptr_t address,void *destination){
 long page=sysconf(_SC_PAGESIZE);if(page<=0)fail("page size");uintptr_t base=address&~(uintptr_t(page)-1);
 if(mprotect(reinterpret_cast<void*>(base),page,PROT_READ|PROT_WRITE|PROT_EXEC))fail("mprotect writable");
 uint32_t words[2]={0xe51ff004,static_cast<uint32_t>(reinterpret_cast<uintptr_t>(destination))};
 std::memcpy(reinterpret_cast<void*>(address),words,sizeof(words));
 __builtin___clear_cache(reinterpret_cast<char*>(address),reinterpret_cast<char*>(address+8));
 if(mprotect(reinterpret_cast<void*>(base),page,PROT_READ|PROT_EXEC))fail("mprotect executable");
}
__attribute__((constructor)) static void activate(){
 const char *enabled=getenv("CC2_REACTOR_RUNTIME_V2");if(!enabled||std::strcmp(enabled,"1"))return;
 char path[512];ssize_t n=readlink("/proc/self/exe",path,sizeof(path)-1);if(n<0)fail("exe path");path[n]=0;
 if(std::strcmp(path,"/opt/usr/cc2-reactor-runtime-v2/elegoo_printer"))fail("unsupported executable path");
 for(const auto &f:fingerprints)if(std::memcmp(reinterpret_cast<void*>(f.address),f.bytes,8))fail("original instruction fingerprint");
 patch(0x469cd4,reinterpret_cast<void*>(&bridge::cc2_callback));
 patch(0x46be24,reinterpret_cast<void*>(&bridge::cc2_timers));
 patch(0x46d520,reinterpret_cast<void*>(&bridge::cc2_poll));
 patch(0x4697b8,reinterpret_cast<void*>(&bridge::cc2_wait));
 int marker=open("/opt/usr/cc2-reactor-runtime-v2/activation.marker",O_WRONLY|O_CREAT|O_TRUNC,0600);
 if(marker<0)fail("activation marker");
 dprintf(marker,"pid=%ld callback=%p timers=%p poll=%p wait=%p\n",static_cast<long>(getpid()),reinterpret_cast<void*>(&bridge::cc2_callback),reinterpret_cast<void*>(&bridge::cc2_timers),reinterpret_cast<void*>(&bridge::cc2_poll),reinterpret_cast<void*>(&bridge::cc2_wait));close(marker);
 unsetenv("LD_PRELOAD");unsetenv("CC2_REACTOR_RUNTIME_V2");
 dprintf(2,"CC2 REACTOR V2 ACTIVE: callback, timers, poll, completion wait; original code preserved on disk\n");
}
#endif
