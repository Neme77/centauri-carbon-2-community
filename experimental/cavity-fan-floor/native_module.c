#define _GNU_SOURCE
#include "../../cc2-control/src/cavity_floor_protocol.h"
#include <stdlib.h>
#include <sys/mman.h>
#ifndef CC2_CAVITY_TEST
#include "../../cc2-control/src/sha256.h"
#endif
#ifdef CC2_NATIVE_STOCK13
#include "../firmware-port/native_profile13.h"
#define CAVITY_NATIVE_SHA256 CC2_NATIVE13_SHA256
#define CAVITY_STATE_ADDRESS CC2_NATIVE13_EXHAUST_STATE
#define CAVITY_CHOICE_ADDRESS CC2_NATIVE13_EXHAUST_CHOICE
#else
#define CAVITY_NATIVE_SHA256 "19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745"
#define CAVITY_STATE_ADDRESS 0x6328b4
#define CAVITY_CHOICE_ADDRESS 0x6340c4
#endif
#define CAVITY_ASM_STRING(value) CAVITY_ASM_LITERAL(value)
#define CAVITY_ASM_LITERAL(value) #value

typedef void (*cavity_state_fn)(void *,void *);
static cavity_state_fn original_state;
static int native_ready,channel=-1;
static unsigned native_sequence,native_generation=1,native_phase,native_disabled,native_writer;
static uintptr_t native_owner;
static unsigned long long process_nonce;

static int cavity_phase(void *string) {
    const char *data=0;size_t size=0;
    memcpy(&data,string,sizeof(data));
    memcpy(&size,(char *)string+sizeof(void *),sizeof(size));
    if(!data||size>32)return 0;
    if(size==8&&!memcmp(data,"printing",8))return 1;
    if(size==6&&!memcmp(data,"paused",6))return 2;
    if(size==9&&!memcmp(data,"cancelled",9))return 3;
    if(size==8&&!memcmp(data,"complete",8))return 4;
    if(size==7&&!memcmp(data,"standby",7))return 5;
    if(size==5&&!memcmp(data,"error",5))return 6;
    return 0;
}
static int cavity_snapshot(void *self,cavity_identity *id) {
    unsigned first=__atomic_load_n(&native_sequence,__ATOMIC_ACQUIRE);
    if((first&1)||!native_ready||__atomic_load_n(&native_disabled,__ATOMIC_RELAXED))return 0;
    uintptr_t owner=__atomic_load_n(&native_owner,__ATOMIC_RELAXED);
    unsigned phase=__atomic_load_n(&native_phase,__ATOMIC_RELAXED);
    id->pid=(long)getpid();id->nonce=process_nonce;
    id->generation=__atomic_load_n(&native_generation,__ATOMIC_RELAXED);
    id->printing=phase==1;
    __sync_synchronize();
    return (!self||owner==(uintptr_t)self) && first==__atomic_load_n(&native_sequence,__ATOMIC_ACQUIRE);
}
static void cavity_publish(void) {
    cavity_identity id;char text[128];
    if(!cavity_snapshot(0,&id)){if(channel>=0)unlinkat(channel,"state",0);return;}
    int n=snprintf(text,sizeof(text),"CC2CF1 %ld %016llx %u %d\n",id.pid,id.nonce,id.generation,id.printing);
    if(n<=0||!cavity_write_at(channel,"state",text,(size_t)n)) {
        __atomic_store_n(&native_disabled,1,__ATOMIC_RELEASE);
        unlinkat(channel,"state",0);
    }
}
/* GCC's nontrivial by-value std::string parameter is passed indirectly.
 * Preserve the original call chain's ownership. Never destroy/copy it. */
void cc2_cavity_state_changed(void *self,void *string) {
    int phase=cavity_phase(string);
    if(__sync_lock_test_and_set(&native_writer,1)) {
        __atomic_store_n(&native_disabled,1,__ATOMIC_RELEASE);
        if(channel>=0)unlinkat(channel,"state",0);
        original_state(self,string);return;
    }
    __atomic_add_fetch(&native_sequence,1,__ATOMIC_SEQ_CST);
    /* If the native C++ callback unwinds, no old ready/printing capability may
     * survive. The forwarding wrapper remains disabled until a full restart. */
    if(channel>=0)unlinkat(channel,"state",0);
    if(!phase||__atomic_load_n(&native_phase,__ATOMIC_RELAXED)!=(unsigned)phase||
       __atomic_load_n(&native_owner,__ATOMIC_RELAXED)!=(uintptr_t)self) {
        unsigned generation=__atomic_add_fetch(&native_generation,1,__ATOMIC_RELAXED);
        if(!generation)__atomic_store_n(&native_disabled,1,__ATOMIC_RELAXED);
    }
    __atomic_store_n(&native_phase,(unsigned)phase,__ATOMIC_RELAXED);
    __atomic_store_n(&native_owner,(uintptr_t)self,__ATOMIC_RELAXED);
    /* A concurrent native policy callback observes the odd sequence and uses
     * its original speed while this native state handler is in progress. */
    original_state(self,string);
    __atomic_add_fetch(&native_sequence,1,__ATOMIC_SEQ_CST);
    cavity_publish();
    __sync_lock_release(&native_writer);
}

double cc2_cavity_minimum(void *self,double selected) {
#ifdef CC2_CAVITY_ABI_TEST
    (void)self;
    return isfinite(selected)&&selected>=0&&selected<=1 ? fmax(selected,.6) : selected;
#else
    cavity_identity id,after;char text[256];double percent;
    if(!isfinite(selected)||selected<0||selected>1 || !cavity_snapshot(self,&id) || !id.printing ||
       !cavity_enabled() || !cavity_read_at(channel,"request",text) ||
       !cavity_request_parse(text,&id,&percent) || !cavity_snapshot(self,&after) ||
       !after.printing || id.pid!=after.pid || id.nonce!=after.nonce ||
       id.generation!=after.generation)return selected;
    return fmax(selected,percent/100.0);
#endif
}

#if defined(__arm__)
/* The displaced instructions do not read PC. The native frame is retained;
 * r0-r3/r12/lr and all caller-saved VFP registers survive the policy call. */
__attribute__((naked)) void cc2_cavity_choice(void) {
    __asm__ volatile(
        "push {r0-r3,r12,lr}\n"
        "vpush {d0-d7}\n"
        "ldr r0,[fp,#-0x60]\n"
        "vldr d0,[fp,#-0x5c]\n"
        "bl cc2_cavity_minimum\n"
        "vstr d0,[fp,#-0x5c]\n"
        "vpop {d0-d7}\n"
        "pop {r0-r3,r12,lr}\n"
        "ldr r3,[fp,#-0x60]\n"
        "vldr d6,[r3,#0xc8]\n"
        "ldr pc,[pc,#-4]\n"
        ".word " CAVITY_ASM_STRING(CAVITY_CHOICE_ADDRESS) " + 8\n");
}
#endif

#ifndef CC2_CAVITY_TEST
_Static_assert(sizeof(void *)==4,"Qualified ARM32 ABI only");
struct cavity_patch {uintptr_t address;unsigned char bytes[8];uintptr_t destination;};
static struct cavity_patch patches[2]={
    {CAVITY_STATE_ADDRESS,{0x10,0x48,0x2d,0xe9,0x08,0xb0,0x8d,0xe2},0},
    {CAVITY_CHOICE_ADDRESS,{0x60,0x30,0x1b,0xe5,0x32,0x6b,0x93,0xed},0}
};
static void cavity_jump(void *address,uintptr_t destination) {
    uint32_t code[2]={0xe51ff004,(uint32_t)destination};
    memcpy(address,code,sizeof(code));
    __builtin___clear_cache((char *)address,(char *)address+8);
}
__attribute__((constructor)) static void cavity_activate(void) {
    if(geteuid()!=0||!cavity_enabled())return;
    char path[512],digest[65];ssize_t length=readlink("/proc/self/exe",path,sizeof(path)-1);
    if(length<0)return;
    path[length]=0;
    if(strcmp(path,"/opt/usr/cc2-reactor-runtime-v2/elegoo_printer"))return;
    int fd=open("/proc/self/exe",O_RDONLY|O_CLOEXEC);
    if(fd<0)return;
    int okay=sha256_fd(fd,digest);close(fd);
    if(!okay||strcmp(digest,CAVITY_NATIVE_SHA256))return;
    for(unsigned i=0;i<2;i++)
        if(memcmp((const void *)patches[i].address,patches[i].bytes,8))return;
    fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC|O_NONBLOCK);
    if(fd<0)return;
    ssize_t random_bytes=read(fd,&process_nonce,sizeof(process_nonce));close(fd);
    if(random_bytes!=(ssize_t)sizeof(process_nonce)||!process_nonce)return;
    channel=cavity_directory(1);if(channel<0)return;
    unlinkat(channel,"state",0);
    long page=sysconf(_SC_PAGESIZE);if(page<4096||(page&(page-1)))return;
    unsigned char *trampoline=mmap(0,16,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(trampoline==MAP_FAILED)return;
    memcpy(trampoline,patches[0].bytes,8);cavity_jump(trampoline+8,patches[0].address+8);
    if(mprotect(trampoline,16,PROT_READ|PROT_EXEC))return;
    memcpy(&original_state,&trampoline,sizeof(original_state));
    void (*state_hook)(void *,void *)=cc2_cavity_state_changed;
    void (*choice_hook)(void)=cc2_cavity_choice;
    memcpy(&patches[0].destination,&state_hook,sizeof(state_hook));
    memcpy(&patches[1].destination,&choice_hook,sizeof(choice_hook));
    unsigned writable=0;
    for(unsigned i=0;i<2;i++) {
        uintptr_t base=patches[i].address&~((uintptr_t)page-1);
        if(mprotect((void *)base,(size_t)page,PROT_READ|PROT_WRITE|PROT_EXEC))break;
        writable++;
    }
    if(writable!=2) {
        for(unsigned i=0;i<writable;i++)mprotect((void *)(patches[i].address&~((uintptr_t)page-1)),(size_t)page,PROT_READ|PROT_EXEC);
        return;
    }
    for(unsigned i=0;i<2;i++)cavity_jump((void *)patches[i].address,patches[i].destination);
    okay=1;
    for(unsigned i=0;i<2;i++)
        if(mprotect((void *)(patches[i].address&~((uintptr_t)page-1)),(size_t)page,PROT_READ|PROT_EXEC))okay=0;
    if(!okay) {
        /* Keep forwarding hooks resident if page permission restoration fails;
         * native_ready stays false, so no manual floor can take effect. */
        __atomic_store_n(&native_disabled,1,__ATOMIC_RELEASE);return;
    }
    native_ready=1;
    cavity_publish();
}
#endif
