#include <cassert>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
extern "C" {
void cc2_cavity_state_changed(void *,std::string);
void string_bridge_begin(void (*)(void *,void *));
int string_bridge_printing(void);
int string_bridge_has_state(void);
void string_bridge_end(void);
}
static std::string expected;
static unsigned calls;
static bool throw_original;
static void original(void *self,std::string text){
    assert(self==&calls&&text==expected);calls++;
    if(throw_original)throw std::runtime_error("dummy native exception");
}
int main(){
    void (*native)(void *,std::string)=original;
    void (*bridge)(void *,void *);static_assert(sizeof(bridge)==sizeof(native),"function pointer ABI");
    std::memcpy(&bridge,&native,sizeof(bridge));string_bridge_begin(bridge);
    for(const char *text:{"printing","paused","printing","standby","","long unknown state to exercise a heap allocated native string"}){
        expected=text;cc2_cavity_state_changed(&calls,std::string(text));
        assert(string_bridge_printing()==(expected=="printing"));
    }
    assert(calls==6);
    expected="printing";throw_original=true;bool caught=false;
    try{cc2_cavity_state_changed(&calls,std::string(expected));}catch(const std::runtime_error &){caught=true;}
    assert(caught&&!string_bridge_printing()&&!string_bridge_has_state());
    string_bridge_end();
    std::puts("PASS: actual C++ nontrivial by-value string ABI, native argument/lifetime forwarding and exception unwinding through the C wrapper; only dummy callbacks executed.");
}
