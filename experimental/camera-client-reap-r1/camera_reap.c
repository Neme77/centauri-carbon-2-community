#define _GNU_SOURCE
#include "reap_core.h"
#include "sha256_file.h"
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define CAMERA_HASH "ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99"
#define SPAWN_SITE ((uintptr_t)0x163cd4)
#define ENABLE_SITE ((uintptr_t)0x162f28)
#define HANDLE_ADDRESS ((uintptr_t)0x163eb4)
#define REMOVE_ADDRESS ((uintptr_t)0x1645dc)
#define STATUS "/tmp/cc2-camera-reap.status"
static cc2_reap_ops ops;
static cc2_reap_stats stats;
static pthread_mutex_t invocation_lock=PTHREAD_MUTEX_INITIALIZER;
static void *(*vector_new)(size_t,const void *);
static void (*vector_delete)(void *);
static const void *nothrow_token;
static int enable_once;
static void best_effort_write(int fd,const char *data,size_t length) {
    while(length) {
        ssize_t n=write(fd,data,length);
        if(n>0){data+=n;length-=(size_t)n;}
        else if(n<0&&errno==EINTR)continue;
        else break;
    }
}
static void fail(const char *why) {
    static const char prefix[]="Camera reap trial refused: ";
    best_effort_write(2,prefix,sizeof(prefix)-1);
    best_effort_write(2,why,strlen(why));
    best_effort_write(2,"\n",1);
    _exit(126);
}
static void status_write(void) {
    char data[512];
    int n=snprintf(data,sizeof(data),
        "trial=R2 pid=%ld patched=1 enable_lan_once=%d spawned=%lu reaped=%lu rejected=%lu retained=%lu last_error=%d executable_sha256=%s\n",
        (long)getpid(),enable_once,stats.spawned,stats.reaped,stats.rejected,
        (unsigned long)stats.retained,stats.last_error,CAMERA_HASH);
    int fd=open(STATUS,O_CREAT|O_TRUNC|O_WRONLY|O_NOFOLLOW,0600);
    if(fd<0)return;
    if(n>0&&(size_t)n<sizeof(data))best_effort_write(fd,data,(size_t)n);
    (void)close(fd);
}
static void symbol(void *destination,size_t size,const char *name) {
    void *value=dlsym(RTLD_DEFAULT,name);
    if(!value||size!=sizeof(value))fail("required runtime symbol unavailable");
    memcpy(destination,&value,size);
}
static void *allocate_vector(size_t size){return vector_new(size,nothrow_token);}
static void release_vector(void *p){vector_delete(p);}
static void reject_client(void *object,int fd) {
    /* Same close/remove order as the qualified native handler's normal exit. */
    (void)close(fd);
    void (*remove_client)(void *,int);
    uintptr_t address=REMOVE_ADDRESS;
    memcpy(&remove_client,&address,sizeof(remove_client));
    remove_client(object,fd);
}
/* Signature and vector/member-pointer layout are bound to the exact ELF.
 * No call to std::thread's throwing constructor remains on this one callsite. */
static void client_spawn(cc2_thread_vector *v,const uint32_t member[2],void *const *object,const int *fd) {
    if((uintptr_t)*object+0x24!=(uintptr_t)v||member[0]!=HANDLE_ADDRESS||member[1]!=0||*fd<0)
        fail("qualified call ABI changed");
    if(pthread_mutex_lock(&invocation_lock))fail("invocation lock");
    (void)cc2_reap_spawn(v,*object,*fd,&ops,&stats);
    status_write();
    if(pthread_mutex_unlock(&invocation_lock))fail("invocation unlock");
}
/* A manual ai_camera restart otherwise starts LAN streaming paused. This
 * separately opted-in constructor final-store hook enables it once, after the native final reset. Later vendor
 * pause/resume and restart behavior is not overridden. */
static void initialize_enabled(unsigned char *flag){__atomic_store_n(flag,1,__ATOMIC_SEQ_CST);}
static void *trampoline_page(long page_size) {
    for(uintptr_t hint=0x1000000;hint<=0x2000000;hint+=0x200000) {
        void *p=mmap((void *)hint,(size_t)page_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(p==MAP_FAILED)continue;
        intptr_t delta=(intptr_t)p-(intptr_t)(SPAWN_SITE+8);
        intptr_t delta2=(intptr_t)p+8-(intptr_t)(ENABLE_SITE+8);
        if(!(delta&3)&&delta>=-33554432&&delta<=33554428&&delta2>=-33554432&&delta2<=33554428)return p;
        (void)munmap(p,(size_t)page_size);
    }
    fail("no unused branch trampoline page in range");
    return NULL;
}
static uint32_t branch(uintptr_t site,uintptr_t destination) {
    intptr_t delta=(intptr_t)destination-(intptr_t)(site+8);
    if((delta&3)||delta< -33554432||delta>33554428)fail("branch range");
    return 0xeb000000u|((uint32_t)(delta/4)&0xffffffu);
}
static void patch_call(uintptr_t site,uint32_t expected,uintptr_t destination,long size) {
    uint32_t old;
    memcpy(&old,(const void *)site,sizeof(old));
    if(old!=expected)fail("instruction fingerprint");
    uintptr_t page=site&~((uintptr_t)size-1);
    if(mprotect((void *)page,(size_t)size,PROT_READ|PROT_WRITE|PROT_EXEC))fail("text write protection");
    __atomic_store_n((uint32_t *)site,branch(site,destination),__ATOMIC_SEQ_CST);
    __builtin___clear_cache((char *)site,(char *)(site+4));
    if(mprotect((void *)page,(size_t)size,PROT_READ|PROT_EXEC))fail("restore text protection");
}
__attribute__((constructor)) static void activate(void) {
    const char *enabled=getenv("CC2_CAMERA_REAP_TRIAL");
    if(!enabled||strcmp(enabled,"1"))return;
    if(sizeof(void *)!=4||sizeof(pthread_t)!=4||sizeof(cc2_thread_vector)!=12)fail("ARM32 thread/vector ABI");
    char path[256],hash[65];
    ssize_t n=readlink("/proc/self/exe",path,sizeof(path)-1);
    if(n<0||n>=(ssize_t)sizeof(path)-1)fail("executable path");
    path[n]=0;
    if(strcmp(path,"/opt/bin/ai_camera"))fail("executable path is not qualified");
    if(cc2_sha256_file("/proc/self/exe",hash)||strcmp(hash,CAMERA_HASH))fail("executable checksum");
    if(*(const uint32_t *)0x1297cc!=0xeb00e6f3u||
       *(const uint32_t *)SPAWN_SITE!=0xeb00080cu||
       *(const uint32_t *)ENABLE_SITE!=0xebfc6be8u||
       *(const uint32_t *)HANDLE_ADDRESS!=0xe92d4810u||
       *(const uint32_t *)0x1631c4!=0xe51b3018u)fail("native code guards");
    symbol(&vector_new,sizeof(vector_new),"_ZnwjRKSt9nothrow_t");
    symbol(&vector_delete,sizeof(vector_delete),"_ZdlPv");
    nothrow_token=dlsym(RTLD_DEFAULT,"_ZSt7nothrow");
    if(!nothrow_token)fail("nothrow token unavailable");
    symbol(&ops.create,sizeof(ops.create),"pthread_create");
    symbol(&ops.tryjoin,sizeof(ops.tryjoin),"pthread_tryjoin_np");
    uintptr_t handle=HANDLE_ADDRESS;
    memcpy(&ops.handle,&handle,sizeof(ops.handle));
    ops.allocate_vector=allocate_vector;ops.free_vector=release_vector;ops.reject=reject_client;
    long page_size=sysconf(_SC_PAGESIZE);
    if(page_size!=4096)fail("unqualified page size");
    unsigned char *tramp=trampoline_page(page_size);
    uint32_t words[4]={0xe51ff004u,0,0xe51ff004u,0};
    void (*spawn)(cc2_thread_vector *,const uint32_t *,void *const *,const int *)=client_spawn;
    void (*enable)(unsigned char *)=initialize_enabled;
    memcpy(&words[1],&spawn,4);memcpy(&words[3],&enable,4);
    memcpy(tramp,words,sizeof(words));
    __builtin___clear_cache((char *)tramp,(char *)tramp+sizeof(words));
    if(mprotect(tramp,(size_t)page_size,PROT_READ|PROT_EXEC))fail("trampoline execute protection");
    const char *restore=getenv("CC2_CAMERA_REAP_ENABLE_LAN");
    enable_once=restore&&!strcmp(restore,"1");
    if(enable_once)patch_call(ENABLE_SITE,0xebfc6be8u,(uintptr_t)tramp+8,page_size);
    patch_call(SPAWN_SITE,0xeb00080cu,(uintptr_t)tramp,page_size);
    status_write();
    unsetenv("CC2_CAMERA_REAP_TRIAL");
    unsetenv("CC2_CAMERA_REAP_ENABLE_LAN");
    /* No polling, detached helpers or background reaper. Native stop still
     * joins every retained thread using the original vector and allocator. */
}
