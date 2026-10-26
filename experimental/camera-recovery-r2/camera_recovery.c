#define _GNU_SOURCE
#include "recovery_core.h"
#include "sha256_file.h"
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <errno.h>

#define QUALIFIED "ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99"
typedef struct { const char *data; uint32_t length; unsigned char storage[16]; } arm_string;
typedef struct { void *object,*control; } arm_shared;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static cc2_recovery state;
static void *server;
static void (*native_accept)(void *,arm_shared *);
static arm_shared *(*native_assign)(arm_shared *,arm_shared *);
static void (*native_start)(void *);
static int (*native_broadcast)(void *,const arm_string *);
static int (*native_send)(void *,const arm_string *);
static unsigned long initial_calls,initial_returns,connections;

static void write_best_effort(int fd,const char *p,size_t n) {
    while(n) {
        ssize_t written=write(fd,p,n);
        if(written>0){p+=written;n-=(size_t)written;}
        else if(written<0&&errno==EINTR)continue;
        else break;
    }
}
static void refuse(const char *why) {
    write_best_effort(2,"Camera recovery refused: ",25);
    write_best_effort(2,why,strlen(why));write_best_effort(2,"\n",1);_exit(126);
}
static void status_locked(void) {
    char line[400];
    int n=snprintf(line,sizeof(line),"trial=RECOVERY_R2 pid=%ld patched=1 ready=%d epoch=%u announced=%lu suppressed=%lu failed=%lu initial_calls=%lu initial_returns=%lu connections=%lu\n",
                   (long)getpid(),state.ready,state.epoch,state.sent,state.duplicate,state.failed,initial_calls,initial_returns,connections);
    int fd=open("/tmp/cc2-camera-recovery.status",O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
    if(fd>=0){if(n>0&&(size_t)n<sizeof(line))write_best_effort(fd,line,(size_t)n);(void)close(fd);}
}
static int socket_key(int fd,cc2_socket_key *key) {
    struct stat st;
    if(fstat(fd,&st)||!S_ISSOCK(st.st_mode))return 0;
    key->device=(uint64_t)st.st_dev;key->inode=(uint64_t)st.st_ino;return key->inode!=0;
}
static void take(void){if(pthread_mutex_lock(&lock))refuse("lock");}
static void release(void){if(pthread_mutex_unlock(&lock))refuse("unlock");}
static void bind_server(void *object) {
    void *expected=NULL;
    if(!object)refuse("missing native server");
    if(!__atomic_compare_exchange_n(&server,&expected,object,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST)&&expected!=object)
        refuse("unexpected control server");
}
static pthread_mutex_t *server_mutex(void *object){return (pthread_mutex_t *)((unsigned char *)object+0x1c);}
static int presence(void *object,const arm_string *message,int present) {
    bind_server(object);
    take();
    cc2_recovery_presence(&state,present);status_locked();release();
    return native_broadcast(object,message);
}
static int inserted(void *object,const arm_string *message){return presence(object,message,1);}
static int removed(void *object,const arm_string *message){return presence(object,message,0);}

/* The native main() calls Control::Start directly, without a device-insertion
 * notification. Announce after it returns, in the same order as the native
 * physical-insertion path. This is an initialization event, not an RTC token. */
static void initial_start(void *control) {
    take();++initial_calls;status_locked();release();
    native_start(control);
    void *object;
    memcpy(&object,(unsigned char *)control+0x34,sizeof(object));
    take();++initial_returns;status_locked();release();
    arm_string message={cc2_insertion,(uint32_t)cc2_insertion_length,{0}};
    memcpy(message.storage,&message.length,sizeof(message.length));
    (void)inserted(object,&message);
}
static void accepting(void *object,arm_shared *destination) {
    bind_server(object);
    uintptr_t offset=(uintptr_t)destination-(uintptr_t)object;
    if(offset!=0xb4&&offset!=0xbc)refuse("control connection slot ABI");
    native_accept(object,destination);
}
/* Publish under the native sendMsg mutex, then check readiness. This closes
 * the window where readiness could change between a pre-publication check
 * and the native shared_ptr assignment. Lock order is always server -> ours;
 * presence() releases ours before calling native_broadcast(). */
static arm_shared *published(arm_shared *destination,arm_shared *source) {
    void *object=__atomic_load_n(&server,__ATOMIC_SEQ_CST);
    uintptr_t offset=(uintptr_t)destination-(uintptr_t)object;
    if(!object||(offset!=0xb4&&offset!=0xbc))refuse("publication ABI");
    pthread_mutex_t *mutex=server_mutex(object);
    if(pthread_mutex_lock(mutex))refuse("native server lock");
    take();
    arm_shared *result=native_assign(destination,source);
    ++connections;
    cc2_socket_key key;
    int fd=-1;
    if(destination->object)memcpy(&fd,destination->object,sizeof(fd));
    if(fd>=0&&socket_key(fd,&key)&&cc2_recovery_claim(&state,key)) {
        ssize_t n=send(fd,cc2_insertion,cc2_insertion_length,MSG_NOSIGNAL|MSG_DONTWAIT);
        cc2_recovery_result(&state,n==(ssize_t)cc2_insertion_length);
        /* Never leave a partial JSON frame on a live control connection. No
         * retry: normal native disconnect/reconnect owns subsequent recovery. */
        if(n>0&&n!=(ssize_t)cc2_insertion_length)(void)shutdown(fd,SHUT_RDWR);
    }
    status_locked();release();
    if(pthread_mutex_unlock(mutex))refuse("native server unlock");
    return result;
}
static int broadcast_client(void *client,const arm_string *message) {
    if(!cc2_recovery_is_insertion(message->data,message->length))return native_send(client,message);
    take();
    int fd;memcpy(&fd,client,sizeof(fd));cc2_socket_key key;
    int result;
    if(!socket_key(fd,&key)) {
        result=native_send(client,message);
        cc2_recovery_result(&state,result);
    } else if(cc2_recovery_claim(&state,key)) {
        result=native_send(client,message);
        cc2_recovery_result(&state,result);
    } else result=1;
    status_locked();release();return result;
}
static uint32_t branch(uintptr_t site,uintptr_t destination) {
    intptr_t d=(intptr_t)destination-(intptr_t)(site+8);
    if((d&3)||d< -33554432||d>33554428)refuse("branch range");
    return 0xeb000000u|((uint32_t)(d/4)&0xffffffu);
}
__attribute__((constructor)) static void activate(void) {
    const char *enable=getenv("CC2_CAMERA_RECOVERY_TRIAL");
    if(!enable||strcmp(enable,"1"))return;
    if(sizeof(void *)!=4||sizeof(arm_string)!=24||sizeof(arm_shared)!=8)refuse("ARM C++ ABI");
    char path[256],hash[65];ssize_t n=readlink("/proc/self/exe",path,sizeof(path)-1);
    if(n<0||n>=(ssize_t)sizeof(path)-1)refuse("path");
    path[n]=0;
    if(strcmp(path,"/opt/bin/ai_camera"))refuse("executable path");
    if(cc2_sha256_file("/proc/self/exe",hash)||strcmp(hash,QUALIFIED))refuse("executable checksum");
    const uintptr_t sites[]={0xea488,0xea570,0x1528ac,0x152a1c,0x152a50,0x7d098,0x152c70,0x152ec0};
    const uint32_t expected[]={0xeb01a10d,0xeb01a0d3,0xeb000315,0xebfffdc7,0xebfffdba,0xeb01b3da,0xebfffed8,0xebfffe44};
    uintptr_t a=0x153508;memcpy(&native_assign,&a,4);
    a=0x1527d8;memcpy(&native_accept,&a,4);
    a=0xea008;memcpy(&native_start,&a,4);
    a=0x1528c4;memcpy(&native_broadcast,&a,4);
    a=0x152140;memcpy(&native_send,&a,4);
    if(*(const uint32_t *)0x1297cc!=0xeb00e6f3u)refuse("community camera fingerprint");
    if(*(const uint32_t *)0xea008!=0xe92d4830u)refuse("native startup fingerprint");
    for(unsigned i=0;i<8;i++)if(*(const uint32_t *)sites[i]!=expected[i])refuse("call fingerprint");
    long size=sysconf(_SC_PAGESIZE);if(size!=4096)refuse("page size");
    unsigned char *tramp=NULL;
    for(uintptr_t hint=0x1000000;hint<=0x2000000;hint+=0x200000) {
        void *p=mmap((void *)hint,(size_t)size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(p==MAP_FAILED)continue;
        int fits=1;
        for(unsigned i=0;i<8;i++) {
            intptr_t d=(intptr_t)p+8*i-(intptr_t)(sites[i]+8);
            if((d&3)||d< -33554432||d>33554428)fits=0;
        }
        if(fits){tramp=p;break;}
        (void)munmap(p,(size_t)size);
    }
    if(!tramp)refuse("trampoline allocation");
    uint32_t words[16]={0};
    for(unsigned i=0;i<8;i++)words[2*i]=0xe51ff004;
    int (*i_fn)(void *,const arm_string *)=inserted;
    int (*r_fn)(void *,const arm_string *)=removed;
    int (*b_fn)(void *,const arm_string *)=broadcast_client;
    arm_shared *(*p_fn)(arm_shared *,arm_shared *)=published;
    void (*s_fn)(void *)=initial_start;
    void (*a_fn)(void *,arm_shared *)=accepting;
    memcpy(words+1,&i_fn,4);memcpy(words+3,&r_fn,4);memcpy(words+5,&p_fn,4);
    memcpy(words+7,&b_fn,4);memcpy(words+9,&b_fn,4);
    memcpy(words+11,&s_fn,4);memcpy(words+13,&a_fn,4);memcpy(words+15,&a_fn,4);
    memcpy(tramp,words,sizeof(words));__builtin___clear_cache((char *)tramp,(char *)tramp+sizeof(words));
    if(mprotect(tramp,(size_t)size,PROT_READ|PROT_EXEC))refuse("trampoline protection");
    for(unsigned i=0;i<8;i++) {
        uintptr_t page=sites[i]&~(uintptr_t)4095;
        uint32_t instruction=branch(sites[i],(uintptr_t)tramp+8*i);
        if(mprotect((void *)page,(size_t)size,PROT_READ|PROT_WRITE|PROT_EXEC))refuse("text protection");
        __atomic_store_n((uint32_t *)sites[i],instruction,__ATOMIC_SEQ_CST);
        __builtin___clear_cache((char *)sites[i],(char *)sites[i]+4);
        if(mprotect((void *)page,(size_t)size,PROT_READ|PROT_EXEC))refuse("text restoration");
    }
    take();status_locked();release();unsetenv("CC2_CAMERA_RECOVERY_TRIAL");
}
