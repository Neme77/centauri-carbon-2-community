#define _GNU_SOURCE
#include "reap_core.h"
#include <dlfcn.h>
#include <stdio.h>
int main(void){
    if(sizeof(void *)!=4||sizeof(pthread_t)!=4||sizeof(cc2_thread_vector)!=12){
        fputs("STOP: unsupported runtime ABI.\n",stderr);return 1;
    }
    void *cpp=dlopen("libstdc++.so.6",RTLD_NOW|RTLD_GLOBAL);
    if(!cpp){fprintf(stderr,"STOP: C++ runtime unavailable: %s\n",dlerror());return 1;}
    const char *names[]={"_ZnwjRKSt9nothrow_t","_ZdlPv","_ZSt7nothrow","pthread_create","pthread_tryjoin_np"};
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++){
        if(!dlsym(RTLD_DEFAULT,names[i])){
            fprintf(stderr,"STOP: required runtime symbol missing: %s\n",names[i]);return 1;
        }
    }
    printf("RUNTIME CHECK PASS: ARM32/glibc loader and required C++/pthread symbols; helper thread=%lu.\n",(unsigned long)pthread_self());
    return dlclose(cpp)?1:0;
}
