/* Read-only process identity. No authorization of hardware operations here.
 * Caller must hold the shared maintenance barrier and select exactly one
 * panel process. PID discovery/uniqueness and service integration are pending.
 */
#ifndef CC2_PANEL_PROCESS_H
#define CC2_PANEL_PROCESS_H
#include <stdlib.h>
#include <stdint.h>
#include <sys/vfs.h>
#include "panel_contract.h"
struct pc_process {pid_t pid; unsigned long long start; dev_t device; ino_t inode; struct pc_contract contract;};
static inline int pc_start(int proc,unsigned long long *out){
    int fd=openat(proc,"stat",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    char buf[4096];ssize_t n;
    if(fd<0)return 0;
    do {n=read(fd,buf,sizeof(buf)-1);}while(n<0&&errno==EINTR);
    close(fd);if(n<=0||n==(ssize_t)sizeof(buf)-1)return 0;buf[n]=0;
    /* comm may contain spaces and ')'; fields after its final ')' are numeric
     * except state. starttime is field 22, independent of wall-clock changes. */
    char *p=strrchr(buf,')');if(!p||p[1]!=' ')return 0;p+=2;
    for(unsigned field=3;field<22;field++){
        p=strchr(p,' ');if(!p)return 0;while(*p==' ')p++;
    }
    if(*p<'0'||*p>'9')return 0;
    errno=0;char *end=NULL;unsigned long long value=strtoull(p,&end,10);
    if(errno||end==p||(*end!=' '&&*end!='\n')||!value)return 0;
    *out=value;return 1;
}
static inline int pc_process_read(int slot,int procroot,pid_t pid,struct pc_process *out){
    memset(out,0,sizeof(*out));
    if(pid<=0)return 0;
    struct statfs fs;
#ifndef PC_PROCESS_FIXTURE
    if(fstatfs(procroot,&fs)||(unsigned long)fs.f_type!=0x9fa0UL)return 0;
#else
    (void)fs; /* Only offline fixture executables define this. */
#endif
    char name[32];int len=snprintf(name,sizeof(name),"%ld",(long)pid);
    if(len<1||(size_t)len>=sizeof(name))return 0;
    int proc=openat(procroot,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(proc<0)return 0;
    struct stat owner={0},app={0},live={0};struct pc_process value={0};
    int okay=!fstat(proc,&owner)&&owner.st_uid==0&&pc_start(proc,&value.start)&&pc_read(slot,&value.contract);
    int appfd=-1,livefd=-1;
    if(okay){
        appfd=openat(slot,"cc2-control",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
        /* /proc/PID/exe is deliberately a kernel symlink. Never follow an
         * arbitrary pathname supplied by the application or HTTP client. */
        livefd=openat(proc,"exe",O_RDONLY|O_CLOEXEC|O_NONBLOCK);
        okay=pc_private(appfd,0,0755)&&pc_private(livefd,0,0755)&&
            !fstat(appfd,&app)&&!fstat(livefd,&live)&&app.st_dev==live.st_dev&&app.st_ino==live.st_ino;
    }
    char digest[65];unsigned long long last=0;
    if(okay)okay=sha256_fd(livefd,digest)&&!strcmp(digest,value.contract.app_sha)&&
        pc_start(proc,&last)&&last==value.start&&pc_unchanged(slot,&value.contract);
    if(appfd>=0)close(appfd);
    if(livefd>=0)close(livefd);
    close(proc);
    if(!okay)return 0;
    value.pid=pid;value.device=app.st_dev;value.inode=app.st_ino;*out=value;return 1;
}
static inline int pc_process_same(int slot,int procroot,const struct pc_process *before){
    struct pc_process now;
    return pc_process_read(slot,procroot,before->pid,&now)&&now.start==before->start&&
        now.device==before->device&&now.inode==before->inode&&
        now.contract.api==before->contract.api&&!strcmp(now.contract.app_sha,before->contract.app_sha);
}
#endif
