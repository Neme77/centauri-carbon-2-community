#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include <new>
#ifdef NDEBUG
#error Assertions must remain enabled
#endif
extern "C" {
void test_setup(void(*)(void*,const void*),void(*)(void*,const void*),int(*)(void*,int),void(*)(void*));
void cc2_rfid_parse(void*,const void*);
void cc2_rfid_state_changed(void*,const void*);
void cc2_rfid_stats_reset(void*);
int cc2_rfid_read(void*,int);
int test_phase(void), test_lock_available(void);
void test_cleanup(void);
}
static bool throw_state;
static bool throw_reset;
static void reset(void* owner) {
    if(throw_reset)throw std::runtime_error("native reset fixture");
    *reinterpret_cast<std::string*>(static_cast<char*>(owner)+0x6c)="standby";
}
static void parse(void*,const void*) {throw std::runtime_error("native parser fixture");}
static void state(void*,const void* value) {
    const std::string &s = *static_cast<const std::string*>(value);
    assert(s == "standby" || s == "printing");
    if (throw_state) throw std::runtime_error("native state fixture");
}
static int read(void*,int) {throw std::runtime_error("native read fixture");}
int main() {
    test_setup(parse,state,read,reset);
    alignas(std::string) char storage[256];
    using String=std::string;
    String *initial=new(storage+112) String("unknown");
    void *owner=storage+4;
    cc2_rfid_stats_reset(owner);assert(test_phase()==1 && *initial=="standby");
    throw_reset=true;
    try{cc2_rfid_stats_reset(owner);assert(false);}catch(const std::runtime_error&){}
    assert(test_phase()==0 && test_lock_available());
    throw_reset=false;cc2_rfid_stats_reset(owner);assert(test_phase()==1);
    initial->~String();
    std::string idle("standby"), printing("printing");
    cc2_rfid_state_changed(nullptr,&idle); assert(test_phase() == 1);
    throw_state = true;
    try {cc2_rfid_state_changed(nullptr,&printing); assert(false);} catch (const std::runtime_error&) {}
    assert(test_phase() == 0 && test_lock_available());
    throw_state = false; cc2_rfid_state_changed(nullptr,&idle); assert(test_phase() == 1);
    std::vector<unsigned char> frame(191,0); frame[1] = 0x40;
    try {cc2_rfid_parse(nullptr,&frame); assert(false);} catch (const std::runtime_error&) {}
    assert(test_lock_available());
    try {cc2_rfid_read(nullptr,1); assert(false);} catch (const std::runtime_error&) {}
    assert(test_lock_available());
    assert(!cc2_rfid_read(nullptr,1)); /* A throwing native transport stays disabled until reboot. */
    test_cleanup();
    std::puts("PASS: actual C++ string/vector ABI and exception unwind; no lock retained; uncertain native read remains disabled.");
}
