#include <string.h>
void cc2_abi_construct(void (*entry)(void),void *result,const int *value) {
    void (*call)(void *,const int *);
    _Static_assert(sizeof(call)==sizeof(entry),"function pointer size");
    memcpy(&call,&entry,sizeof(call));call(result,value);
}
void *cc2_abi_assign(void (*entry)(void),void *destination,void *source) {
    void *(*call)(void *,void *);
    _Static_assert(sizeof(call)==sizeof(entry),"function pointer size");
    memcpy(&call,&entry,sizeof(call));return call(destination,source);
}
