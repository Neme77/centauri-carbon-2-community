#define CC2_CAVITY_TEST
#define CC2_CAVITY_ABI_TEST
#include "native_module.c"
#include <assert.h>
#if !defined(__arm__)
#error ARM qualification only
#endif

extern double invoke_choice(void *,double);
extern unsigned char continuation_start[],continuation_end[],state_return_start[],state_return_end[];
__asm__(
    ".text\n.align 2\n.global invoke_choice\n.type invoke_choice,%function\n"
    "invoke_choice:\n"
    "push {r4-r11,lr}\nsub sp,sp,#132\nadd fp,sp,#132\n"
    "str r0,[fp,#-0x60]\nvstr d0,[fp,#-0x5c]\n"
    "movw r1,#0x1122\nmovw r2,#0x3344\nmovw r12,#0x5566\n"
    "vmov.f64 d1,#1.0\nvmov.f64 d2,#2.0\nvmov.f64 d3,#3.0\n"
    "vmov.f64 d4,#4.0\nvmov.f64 d5,#5.0\n"
    "adr lr,1f\nb cc2_cavity_choice\n"
    "1:\nldr r4,[fp,#-0x60]\ncmp r0,r4\nbne 2f\ncmp r3,r4\nbne 2f\n"
    "movw r4,#0x1122\ncmp r1,r4\nbne 2f\nmovw r4,#0x3344\ncmp r2,r4\nbne 2f\n"
    "movw r4,#0x5566\ncmp r12,r4\nbne 2f\n"
    "vmov.f64 d6,#1.0\nvcmp.f64 d1,d6\nvmrs APSR_nzcv,FPSCR\nbne 2f\n"
    "vmov.f64 d6,#2.0\nvcmp.f64 d2,d6\nvmrs APSR_nzcv,FPSCR\nbne 2f\n"
    "vmov.f64 d6,#3.0\nvcmp.f64 d3,d6\nvmrs APSR_nzcv,FPSCR\nbne 2f\n"
    "vmov.f64 d6,#4.0\nvcmp.f64 d4,d6\nvmrs APSR_nzcv,FPSCR\nbne 2f\n"
    "vmov.f64 d6,#5.0\nvcmp.f64 d5,d6\nvmrs APSR_nzcv,FPSCR\nbne 2f\n"
    "b 3f\n"
    "2:\nvmov.f64 d0,#-1.0\n"
    "3:\nadd sp,sp,#132\npop {r4-r11,pc}\n"
    ".global continuation_start\ncontinuation_start:\n"
    "vldr d7,[fp,#-0x5c]\nvmov.f64 d0,d7\nbx lr\n"
    ".global continuation_end\ncontinuation_end:\n"
    ".global state_return_start\nstate_return_start:\n"
    "sub sp,sp,#0x4c\nmov r0,#42\nsub sp,fp,#8\npop {r4,fp,pc}\n"
    ".global state_return_end\nstate_return_end:\n");

int main(void) {
    unsigned char *page=mmap((void *)0x634000,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
    assert(page==(void *)0x634000);
    memcpy(page+0xcc,continuation_start,(size_t)(continuation_end-continuation_start));
    __builtin___clear_cache((char *)page,(char *)page+4096);
    _Alignas(8) unsigned char owner[0x300]={0};double native=.25;memcpy(owner+0xc8,&native,8);
    assert(invoke_choice(owner,.1)==.6);
    assert(invoke_choice(owner,.9)==.9);
    assert(invoke_choice(owner,0)==.6);
    assert(isnan(invoke_choice(owner,NAN)));
    unsigned char *state=mmap((void *)0x632000,4096,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
    assert(state==(void *)0x632000);
    unsigned char prefix[8]={0x10,0x48,0x2d,0xe9,0x08,0xb0,0x8d,0xe2};
    memcpy(state+0x8b4,prefix,8);
    memcpy(state+0x8bc,state_return_start,(size_t)(state_return_end-state_return_start));
    unsigned char *trampoline=mmap(0,16,PROT_READ|PROT_WRITE|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(trampoline!=MAP_FAILED);memcpy(trampoline,prefix,8);
    uint32_t jump[2]={0xe51ff004,0x6328bc};memcpy(trampoline+8,jump,8);
    __builtin___clear_cache((char *)state,(char *)state+4096);
    __builtin___clear_cache((char *)trampoline,(char *)trampoline+16);
    int (*call)(void *,void *);memcpy(&call,&trampoline,sizeof(call));assert(call(owner,0)==42);
    puts("PASS: actual ARM choice trampoline, hard-float return and caller registers, displaced prologue and stack return on dummy mapped code. No vendor executable run.");
    return 0;
}
