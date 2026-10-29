#include <cassert>
#include <cstring>
#include <string>
#include <iostream>
extern "C" void *cc2_test_return(void *,unsigned);
extern "C" int cc2_test_inspect(const void *,const char *);
int main() {
    using Return=std::string (*)(unsigned);
    auto original=&cc2_test_return;Return returned;
    static_assert(sizeof(original)==sizeof(returned));
    std::memcpy(&returned,&original,sizeof(returned));
    for(unsigned i=0;i<10000;i++) {
        auto ip=returned(i%2);
        assert(ip==(i%2?"255.255.255.255":""));
        assert(cc2_test_inspect(&ip,ip.c_str()));
        auto copy=ip;assert(copy==ip);
    }
    for(const std::string value:{"wlan0","cc2lan0","a-longer-native-heap-string"})
        assert(cc2_test_inspect(&value,value.c_str()));
    std::cout<<"PASS real std::string hidden return, SSO, copy and destructor ABI; size="<<sizeof(std::string)<<"\n";
}
