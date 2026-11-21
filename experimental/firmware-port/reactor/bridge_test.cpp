#include <cstddef>
#define CC2_BRIDGE_TEST
#include "reactor_module.cpp"
#include <functional>
#include <cassert>
#include <iostream>
#include <map>
#include <memory>
#ifdef CC2_NATIVE_CO
#include "co_routine.h"
#endif
namespace bridge {
struct Control {int count;std::function<void()> dispose;};
static int alive=0,json_alive=0;
static void copy_shared(void *dest,const void *src){auto d=static_cast<Shared*>(dest);auto s=static_cast<const Shared*>(src);d->object=s->object;d->control=s->control;if(d->control)++static_cast<Control*>(d->control)->count;}
static void destroy_shared(void *p){auto s=static_cast<Shared*>(p);auto c=static_cast<Control*>(s->control);if(c&&!--c->count){c->dispose();delete c;--alive;}}
static Shared own(void *p,std::function<void()> dispose=[](){}){Shared s;s.object=p;s.control=new Control{1,dispose};++alive;return s;}
static void assign_shared(void *dest,const void *src){Shared copy=Shared::from(src);auto d=static_cast<Shared*>(dest);*d=std::move(copy);}
static void copy_json(void *dest,const void *src){std::memcpy(dest,src,16);++json_alive;}
static void destroy_json(void*){assert(json_alive>0);--json_alive;}
static bool null_json(const void *p){return *static_cast<const int*>(p)==0;}
static bool empty_function(const void *p){return !*static_cast<const std::function<void(double)>*>(p);}
static void call_void(const void *p,double d){(*static_cast<const std::function<void(double)>*>(p))(d);}
static double call_double(const void *p,double d){return (*static_cast<const std::function<double(double)>*>(p))(d);}
static void call_json(void *out,const void *p,double d){int result=(*static_cast<const std::function<int(double)>*>(p))(d);std::memset(out,0,16);std::memcpy(out,&result,4);++json_alive;}
struct Owner {
 alignas(16) byte data[1024]{};
 Owner(){new(at(data,16))std::vector<Shared>();new(at(data,96))std::vector<Shared>();new(at(data,108))std::vector<Shared>();new(at(data,144))Shared();new(at(data,264))std::vector<pollfd>();next_timer(data)=never;}
 ~Owner(){refs(16).~vector();refs(96).~vector();refs(108).~vector();static_cast<Shared*>(at(data,144))->~Shared();fds().~vector();}
 std::vector<Shared>&refs(size_t n){return *static_cast<std::vector<Shared>*>(at(data,n));}
 std::vector<pollfd>&fds(){return *static_cast<std::vector<pollfd>*>(at(data,264));}
};
struct Timer {std::function<double(double)> fn;double wake;};
struct Completion {Shared owner;alignas(8)byte result[16*(sizeof(void*)/4)]{};std::vector<Shared> waiting;};
struct Callback {Shared owner,timer;std::function<int(double)> fn;Shared completion;};
static_assert(offsetof(Callback,fn)==16*(sizeof(void*)/4),"callback function layout");
static_assert(offsetof(Callback,completion)==32*(sizeof(void*)/4),"callback completion layout");
static_assert(offsetof(Completion,waiting)==24*(sizeof(void*)/4),"completion vector layout");
static void unregister_timer(void *o,Shared timer){auto &v=*static_cast<std::vector<Shared>*>(at(o,16));static_cast<Timer*>(timer.object)->wake=never;v.erase(std::remove_if(v.begin(),v.end(),[&](const Shared&s){return s.object==timer.object;}),v.end());}
static void complete(void *p,void *result){std::memcpy(static_cast<Completion*>(p)->result,result,16);}
static void push_wait(void *v,const void *s){static_cast<std::vector<Shared>*>(v)->push_back(Shared::from(s));}
static Shared get_current(void *o){return Shared::from(at(o,144));}
static std::function<double(void*,double)> pause_action;
static double pause_fn(void *o,double d){return pause_action(o,d);}
static double monotonic(){return 1.0;}
static std::function<int(pollfd*,nfds_t,int)> poll_action;
static int poll_fn(pollfd*p,nfds_t n,int t){return poll_action(p,n,t);}
static void end_fn(void*,Shared){}
uintptr_t test_resolve(uintptr_t a){
 switch(a){
#define FN(A,F) case A:return reinterpret_cast<uintptr_t>(&F)
 FN(0xd0460,copy_shared);FN(0xcfd10,destroy_shared);FN(0x271c90,assign_shared);
 FN(0xdb05c,copy_json);FN(0xd1194,destroy_json);FN(0x109e4c,null_json);
 FN(0xc3ee8,empty_function);FN(0x1d62b4,call_void);FN(0x2738bc,call_double);FN(0x272350,call_json);
 FN(0x26b8b8,unregister_timer);FN(0x26a4ec,complete);FN(0x2720d4,push_wait);
 FN(0x26dddc,get_current);FN(0x26c0c4,pause_fn);FN(0x35ee3c,monotonic);FN(0xbf70c,poll_fn);FN(0x26d3a0,end_fn);
#undef FN
 default:throw std::runtime_error("unmocked vendor function");
 }
}
}
using namespace bridge;
#ifdef CC2_NATIVE_CO
struct NativeRun {Callback *callback;bool threw;};
static void *native_worker(void *arg){
 auto &run=*static_cast<NativeRun*>(arg);
 try{cc2_callback(run.callback,1);}catch(const std::runtime_error&){run.threw=true;}
 return nullptr;
}
static void native_cycles(){
 for(int i=0;i<1024;i++){
  Owner o;auto c=new Callback();auto t=new Timer{[](double){return never;},0};
  c->owner=own(o.data);c->timer=own(t,[t]{delete t;});
  auto object=std::make_shared<int>(7);std::weak_ptr<int> weak=object;bool fail=(i%2)!=0;
  c->fn=[object,fail](double){co_yield_ct();if(fail)throw std::runtime_error("after native yield");return *object;};object.reset();
  o.refs(16).push_back(c->timer);o.refs(96).push_back(own(c,[c]{delete c;}));
  NativeRun run{c,false};stCoRoutine_t *co=nullptr;stCoRoutineAttr_t attr;attr.stack_size=128*1024;
  assert(co_create(&co,&attr,native_worker,&run)==0);co_resume(co);
  assert(o.refs(96).empty()&&o.refs(16).empty()&&!weak.expired());
  co_resume(co);assert(weak.expired()&&run.threw==fail&&alive==0&&json_alive==0);co_release(co);
 }
 std::cout<<"PASS: replacement bridge with 1024 native libco cycles, including 512 exceptions after suspension\n";
}
struct TimerRun {Owner *owner;double result;};
static void *timer_worker(void *arg){
 auto &r=*static_cast<TimerRun*>(arg);r.result=cc2_timers(r.owner->data,17.25,false);return nullptr;
}
static bool floating_cycles(){
 for(int i=0;i<256;i++){
  Owner o;next_timer(o.data)=0;int unexpected=0;double observed=0;
  auto a=new Timer(),b=new Timer();a->wake=0;b->wake=never;
  a->fn=[&](double time){observed=time;co_yield_ct();return 100.5;};
  b->fn=[&](double){++unexpected;return never;};
  o.refs(16).push_back(own(a,[a]{delete a;}));o.refs(16).push_back(own(b,[b]{delete b;}));
  TimerRun run{&o,-1};stCoRoutine_t *co=nullptr;stCoRoutineAttr_t attr;
  if(co_create(&co,&attr,timer_worker,&run))return false;co_resume(co);
#if defined(__arm__)
  // Simulate another coroutine using every callee-saved VFP register.
  // libco's ARM context switch saves r4-r11 but not d8-d15.
  unsigned long long bits=0;double poison=never;std::memcpy(&bits,&poison,8);
  asm volatile("vmov d8, %Q0, %R0\n\tvmov d9, %Q0, %R0\n\tvmov d10, %Q0, %R0\n\tvmov d11, %Q0, %R0\n\tvmov d12, %Q0, %R0\n\tvmov d13, %Q0, %R0\n\tvmov d14, %Q0, %R0\n\tvmov d15, %Q0, %R0" : : "r"(bits) : "d8","d9","d10","d11","d12","d13","d14","d15","memory");
#endif
  co_resume(co);co_release(co);
  if(observed!=17.25||unexpected!=0||next_timer(o.data)!=100.5||run.result!=0){
   std::cerr<<"FLOATING REGRESSION: cycle="<<i<<" observed="<<observed<<" disabled_timer_calls="<<unexpected<<" next="<<next_timer(o.data)<<" result="<<run.result<<"\n";return false;
  }
 }
 std::cout<<"PASS: 256 timer suspend/resume cycles preserve time and leave disabled timers inactive despite VFP register reuse\n";return true;
}

#endif
int main(){
 // Actual replacement callback code: release normal/async owners, success and throw.
 for(bool async:{false,true})for(bool fail:{false,true}){
  Owner o;auto c=new Callback();auto t=new Timer{[](double){return never;},0};auto result=new Completion();
  c->owner=own(o.data);c->timer=own(t,[t]{delete t;});c->completion=own(result,[result]{delete result;});
  c->fn=[fail](double){if(fail)throw std::runtime_error("callback error");return 7;};
  o.refs(16).push_back(c->timer);o.refs(async?108:96).push_back(own(c,[c]{delete c;}));
  bool thrown=false;try{assert(cc2_callback(c,1)==never);}catch(const std::runtime_error&){thrown=true;}
  assert(thrown==fail&&o.refs(16).empty()&&o.refs(96).empty()&&o.refs(108).empty());
 }
 assert(alive==0&&json_alive==0);
 // Timer snapshot: a callback reallocates and cancels another pending timer.
 {
  Owner o;next_timer(o.data)=0;int calls=0;auto a=new Timer(),b=new Timer();
  Shared sa=own(a,[a]{delete a;}),sb=own(b,[b]{delete b;});a->wake=b->wake=0;
  b->fn=[&](double){calls+=100;return never;};
  a->fn=[&](double){calls++;unregister_timer(o.data,sb);for(int i=0;i<1000;i++)o.refs(16).push_back(Shared());return never;};
  o.refs(16).push_back(sa);o.refs(16).push_back(sb);assert(cc2_timers(o.data,1,false)==0&&calls==1);
 }
 assert(alive==0);
 // Poll: vector reallocation and disappearing second fd cannot invalidate snapshot.
 {
  Owner o;*static_cast<byte*>(at(o.data,12))=1;int calls=0;
  struct Node {alignas(16)byte bytes[96]{};} node1,node2;
  struct Handler {std::function<void(double)> read,write;} h1,h2;
  Shared s1=own(&h1),s2=own(&h2);
  int k1=1,k2=2;std::memcpy(at(node1.bytes,16),&k1,4);std::memcpy(at(node2.bytes,16),&k2,4);
  std::memcpy(at(node1.bytes,20),&s1,sizeof(s1));std::memcpy(at(node2.bytes,20),&s2,sizeof(s2));
  void *n1=node1.bytes,*n2=node2.bytes;std::memcpy(at(o.data,248),&n1,sizeof(n1));std::memcpy(at(node1.bytes,12),&n2,sizeof(n2));
  h1.read=[&](double){calls++;void *zero=nullptr;std::memcpy(at(node1.bytes,12),&zero,sizeof(zero));for(int i=3;i<2003;i++)o.fds().push_back({i,POLLIN,0});};
  h2.read=[&](double){calls+=100;};o.fds().push_back({1,POLLIN,0});o.fds().push_back({2,POLLIN,0});
  poll_action=[&](pollfd*p,nfds_t n,int){for(size_t i=0;i<n;i++)p[i].revents=POLLIN;*static_cast<byte*>(at(o.data,12))=0;return 2;};
  cc2_poll(o.data);assert(calls==1);
 }
 assert(alive==0);
 // Completion wait: release waiter on exception, preserve exception and fallback.
 {
  Owner o;Completion c;c.owner=own(o.data);*static_cast<Shared*>(at(o.data,144))=own(o.data);
  byte fallback[16]{};*reinterpret_cast<int*>(fallback)=9;byte out[16]{};
  pause_action=[](void*,double)->double{throw std::runtime_error("pause error");};
  bool thrown=false;try{cc2_wait(out,&c,1,fallback);}catch(const std::runtime_error&){thrown=true;}assert(thrown&&c.waiting.empty());
  pause_action=[](void*,double){return 1.0;};cc2_wait(out,&c,1,fallback);assert(*reinterpret_cast<int*>(out)==9&&c.waiting.empty());destroy_json(out);
 }
 assert(alive==0&&json_alive==0);
 // Toolhead shutdown: only can_pause changes; the lookahead slot (+36) and all other bytes stay untouched.
 {
  alignas(16) byte toolhead[0x400];for(size_t i=0;i<sizeof(toolhead);i++)toolhead[i]=static_cast<byte>(i*7+1);
  byte *can_pause=static_cast<byte*>(at(toolhead,0x90));*can_pause=1;
  byte expected[sizeof(toolhead)];std::memcpy(expected,toolhead,sizeof(toolhead));expected[can_pause-toolhead]=0;
  cc2_toolhead_shutdown(toolhead);assert(std::memcmp(expected,toolhead,sizeof(toolhead))==0);
 }
 // Toolhead shutdown against a model of the vendor look-ahead queue (read from 02.01.01.13):
 // ToolHead +36 shared_ptr<LookAheadQueue>, queue vector<Move*>; flush() copies the queue,
 // calls _process_moves(copy) (the reactor can run the shutdown handler there, as in drip_move),
 // then deletes and erases its own moves; check_busy() reads queue.empty(); drip_move's
 // end-signal catch calls LookAheadQueue::reset(). CC2_SHOW_VENDOR_SHUTDOWN swaps in the vendor
 // handler (shared_ptr reset) to show the use-after-free under ASan.
 {
  struct M{static int &live(){static int n=0;return n;}M(){++live();}~M(){--live();}};
  struct Q{std::vector<M*> q;~Q(){for(auto m:q)delete m;}};
  const size_t la=sizeof(void*)==4?36:40;
  alignas(16) byte th[0x400]{};
  auto &sp=*new(at(th,la))std::shared_ptr<Q>(std::make_shared<Q>());
  Q *const original=sp.get();
  static void (*shutdown)(void*)=nullptr;
#ifdef CC2_SHOW_VENDOR_SHUTDOWN
  shutdown=[](void *t){*static_cast<byte*>(at(t,0x90))=0;static_cast<std::shared_ptr<Q>*>(at(t,sizeof(void*)==4?36:40))->reset();};
#else
  shutdown=&cc2_toolhead_shutdown;
#endif
  auto add=[&](int n){for(int i=0;i<n;i++)sp->q.push_back(new M);};
  auto flush=[&](bool shut_inside){Q *self=sp.get();std::vector<M*> copy=self->q;if(shut_inside)shutdown(th);(void)copy;for(auto m:self->q)delete m;self->q.clear();};
  auto check_busy=[&](){return sp->q.empty();};
  auto drip_end=[&](){for(auto m:sp->q)delete m;sp->q.clear();};
  // 1. shutdown while flush() is inside _process_moves: no use-after-free, no double free
  *static_cast<byte*>(at(th,0x90))=1;add(5);flush(true);
  assert(sp.get()==original&&*static_cast<byte*>(at(th,0x90))==0&&sp->q.empty()&&M::live()==0);
  // 2. later check_busy() from the idle timeouts reads the kept, valid queue
  for(int i=0;i<3;i++)assert(check_busy());
  // 3. repeated shutdown, also with moves still queued and no flush running
  add(4);for(int i=0;i<3;i++)shutdown(th);
  assert(sp.get()==original&&sp->q.size()==4&&M::live()==4&&!check_busy());
  // 4. the kept moves are consumed once by the next flush or by drip_move's end-signal reset
  flush(false);assert(M::live()==0&&check_busy());
  add(3);shutdown(th);drip_end();assert(M::live()==0&&check_busy());
  // 5. no growth: many shutdown/flush rounds leave nothing behind
  for(int r=0;r<100;r++){add(2);flush(true);shutdown(th);}
  assert(M::live()==0&&sp->q.empty()&&sp.get()==original);
  sp.~shared_ptr();assert(M::live()==0);
 }
#ifdef CC2_NATIVE_CO
 if(!floating_cycles())return 42;
 native_cycles();
#endif
 std::cout<<"PASS: replacement bridge callback/timer/poll/wait/toolhead-shutdown logic, refcounts, exception cleanup, reallocation and fd removal\n";
 std::cout<<"LIMIT: vendor entry calls mocked; original ARM function binding is separately fingerprint-audited and requires runtime testing.\n";
}
