/* Recover only an authenticated installer's private, app-specific transaction.
 * No HTTP supplied paths, native stops or UART commands. Metadata breaks the
 * app -> configured worker(app SHA) build cycle, as with managed RFID runtime.
 * The worker rechecks its compiled plan, app identity and transaction journal. */
#ifndef CC2_RFID_APP_UPDATE_BOOT_H
#define CC2_RFID_APP_UPDATE_BOOT_H
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "sha256.h"
#include "connections/json.h"
#ifndef CC2_RFID_APP_UPDATES
#define CC2_RFID_APP_UPDATES "/opt/usr/cc2-rfid-app-updates"
#endif
#ifndef CC2_RFID_APP_BOOT_MS
#define CC2_RFID_APP_BOOT_MS 30000u
#endif
#if CC2_RFID_APP_BOOT_MS < 1 || CC2_RFID_APP_BOOT_MS > 30000
#error Unsupported app-binding recovery timeout
#endif
static int aab_hex(const char *s){
    if(!s||strlen(s)!=64)return 0;
    for(unsigned i=0;i<64;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;
    return 1;
}
static int aab_directory(int fd){
    struct stat st;
    return fd>=0&&!fstat(fd,&st)&&S_ISDIR(st.st_mode)&&st.st_uid==0&&(st.st_mode&07777)==0700;
}
static int aab_file(int fd,mode_t mode,off_t limit){
    struct stat st;
    return fd>=0&&!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&st.st_nlink==1&&
        (st.st_mode&07777)==mode&&st.st_size>0&&st.st_size<=limit;
}
static int aab_worker(int directory,const char *self){
    int metadata=openat(directory,"recovery.json",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    struct stat st;char bytes[2049],expected[65]={0};
    int okay=aab_file(metadata,0600,2048)&&!fstat(metadata,&st);
    size_t used=0;
    while(okay&&used<(size_t)st.st_size){
        ssize_t n=read(metadata,bytes+used,(size_t)st.st_size-used);
        if(n<0&&errno==EINTR)continue;
        if(n<=0){okay=0;break;}used+=(size_t)n;
    }
    if(metadata>=0)close(metadata);
    if(!okay)return -1;
    bytes[used]=0;
    ng_json *json=malloc(sizeof(*json));if(!json)return -1;
    double version=0;
    okay=ng_json_parse(json,bytes,used)&&json->v[0].type=='{'&&json->v[0].count==4&&
        ng_number(json,0,"protocol",&version)&&version==1;
    const char *app=ng_string(json,0,"app_sha256"),*worker=ng_string(json,0,"worker_sha256"),
        *plan=ng_string(json,0,"plan_sha256");
    okay=okay&&aab_hex(app)&&aab_hex(worker)&&aab_hex(plan)&&!strcmp(app,self);
    if(okay)memcpy(expected,worker,65);
    free(json);if(!okay)return -1;
    int fd=openat(directory,"worker",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);char actual[65];
    okay=aab_file(fd,0700,4*1024*1024)&&sha256_fd(fd,actual)&&!strcmp(actual,expected)&&lseek(fd,0,SEEK_SET)==0;
    if(!okay&&fd>=0){close(fd);fd=-1;}
    return fd;
}
static int rfid_app_update_boot(void){
    int parent=open(CC2_RFID_APP_UPDATES,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    if(parent<0)return errno==ENOENT;
    if(!aab_directory(parent)){close(parent);return 0;}
    int executable=open("/proc/self/exe",O_RDONLY|O_CLOEXEC);char self[65];struct stat st;
    int okay=executable>=0&&!fstat(executable,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&st.st_nlink==1&&
        !(st.st_mode&022)&&sha256_fd(executable,self);
    if(executable>=0)close(executable);
    if(!okay){close(parent);return 0;}
    int directory=openat(parent,self,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    int absent=directory<0&&errno==ENOENT;close(parent);
    if(absent)return 1;
    if(!aab_directory(directory)){if(directory>=0)close(directory);return 0;}
    int worker=aab_worker(directory,self);close(directory);
    if(worker<0)return 0;
    struct rlimit limit;
    if(getrlimit(RLIMIT_NOFILE,&limit)||limit.rlim_cur>1048576){close(worker);return 0;}
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC,&now)){close(worker);return 0;}
    uint64_t deadline=(uint64_t)now.tv_sec*1000u+(unsigned)now.tv_nsec/1000000u+CC2_RFID_APP_BOOT_MS;
    pid_t child=fork();
    if(!child){
        for(int fd=3;fd<(int)limit.rlim_cur;fd++)if(fd!=worker)close(fd);
        char *const args[]={"rfid-app-update-worker","--recover",NULL};
        char *const environment[]={"PATH=/usr/sbin:/usr/bin:/sbin:/bin","LANG=C",NULL};
        fexecve(worker,args,environment);_exit(125);
    }
    close(worker);if(child<0)return 0;
    for(;;){
        int status=0;pid_t result=waitpid(child,&status,WNOHANG);
        if(result==child)return WIFEXITED(status)&&WEXITSTATUS(status)==0;
        if(result<0&&errno!=EINTR)return 0;
        if(clock_gettime(CLOCK_MONOTONIC,&now)||
            (uint64_t)now.tv_sec*1000u+(unsigned)now.tv_nsec/1000000u>=deadline){
            /* Only bounded file replacement, never a flash worker. Atomic
             * files and durable intent allow safe retry after this stop. */
            kill(child,SIGKILL);while(waitpid(child,NULL,0)<0&&errno==EINTR){}return 0;
        }
        struct timespec pause={0,100000000};nanosleep(&pause,NULL);
    }
}
#endif
