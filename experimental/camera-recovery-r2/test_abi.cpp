#include <string>
#include <memory>
#include <cstring>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <new>
extern "C" void cc2_abi_construct(void (*)(void),void *,const int *);
extern "C" void *cc2_abi_assign(void (*)(void),void *,void *);
static std::shared_ptr<int> factory(const int *value){return std::make_shared<int>(*value);}
static std::shared_ptr<int> &assignment(std::shared_ptr<int> *destination,std::shared_ptr<int> *source){return *destination=std::move(*source);}
struct NativeString { const char *data; uint32_t size; unsigned char storage[16]; };
int main(){
    assert(sizeof(void*)==4&&sizeof(std::string)==24&&sizeof(std::shared_ptr<int>)==8);
    std::string s="Camera device insertion";NativeString native{};std::memcpy(&native,&s,24);
    assert(native.size==s.size()&&!std::memcmp(native.data,s.data(),s.size()));
    auto ptr=std::make_shared<int>(42);void *parts[2];std::memcpy(parts,&ptr,8);
    assert(parts[0]==ptr.get()&&parts[1]);
    /* The exact non-trivial return ABI used by the native make_shared hook. */
    alignas(std::shared_ptr<int>) unsigned char storage[sizeof(std::shared_ptr<int>)];
    auto original=&factory;void (*entry)(void);std::memcpy(&entry,&original,sizeof(entry));
    int value=57;cc2_abi_construct(entry,storage,&value);
    auto *constructed=std::launder(reinterpret_cast<std::shared_ptr<int> *>(storage));
    assert(**constructed==57&&constructed->use_count()==1);constructed->~shared_ptr();
    std::shared_ptr<int> destination,source=std::make_shared<int>(73);
    auto original_assign=&assignment;std::memcpy(&entry,&original_assign,sizeof(entry));
    assert(cc2_abi_assign(entry,&destination,&source)==&destination);
    assert(!source&&*destination==73&&destination.use_count()==1);
    const char payload[]="Camera device insertion after direct startup";
    NativeString borrowed{payload,sizeof(payload)-1,{0}};
    std::memcpy(borrowed.storage,&borrowed.size,sizeof(borrowed.size));
    const auto *view=reinterpret_cast<const std::string *>(&borrowed);
    assert(view->size()==sizeof(payload)-1&&std::string(*view)==payload);
    puts("PASS: ARM C++ string view, shared_ptr publication hook and hidden-return ABI");
}
