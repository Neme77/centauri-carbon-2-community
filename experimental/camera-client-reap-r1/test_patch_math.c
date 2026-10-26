#include "camera_reap.c"
#include <assert.h>
#ifdef NDEBUG
#error These qualification tests require active assertions.
#endif
#include <sys/wait.h>
static uintptr_t decoded(uintptr_t site,uint32_t word){
    int32_t immediate=(int32_t)(word<<8)>>8;
    return site+8+(intptr_t)immediate*4;
}
int main(void){
    unsigned char enabled=0; initialize_enabled(&enabled); assert(enabled==1);
    assert(ENABLE_SITE==0x162f28);
    uintptr_t sites[]={0x163cd4,0x162f28,0x3000000};
    for(size_t i=0;i<sizeof(sites)/sizeof(sites[0]);i++){
        uintptr_t destinations[]={sites[i]+8,0x1000000,0x1200000,0x2000000};
        for(size_t j=0;j<sizeof(destinations)/sizeof(destinations[0]);j++){
            intptr_t delta=(intptr_t)destinations[j]-(intptr_t)(sites[i]+8);
            if(delta< -33554432||delta>33554428)continue;
            uint32_t b=branch(sites[i],destinations[j]);
            assert((b&0xff000000u)==0xeb000000u&&decoded(sites[i],b)==destinations[j]);
        }
    }
    for(int mode=0;mode<3;mode++){
        pid_t pid=fork();assert(pid>=0);
        if(!pid){
            if(mode==0){setenv("CC2_CAMERA_REAP_TRIAL","1",1);activate();}
            else if(mode==1)(void)branch(0x163cd4,0x1000001);
            else (void)branch(0x163cd4,0x70000000);
            _exit(99);
        }
        int status;assert(waitpid(pid,&status,0)==pid);
        assert(WIFEXITED(status)&&WEXITSTATUS(status)==126);
    }
    puts("PASS: ARM BL target math and wrong executable/unaligned/out-of-range refusal");
}
