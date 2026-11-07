/* App-specific RFID files only. The caller authenticates the update payload and
 * performs the live native/app preflight before prepare. This low-level module
 * owns a separate journal and the existing lifecycle flock; it never modifies
 * the Canvas journal, startup targets, loaded guard, original image or UART.
 * Not a public installer/CLI. */
#ifndef CC2_RFID_APP_BINDING_TX_H
#define CC2_RFID_APP_BINDING_TX_H
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "journal.h"
#ifndef AU_COMPONENT
#define AU_COMPONENT "/opt/usr/cc2-rfid"
#endif
#ifndef AU_DIRECTORY
#define AU_DIRECTORY "/opt/usr/cc2-rfid-app-update"
#endif
#ifndef AU_APP
#define AU_APP "/opt/usr/cc2-control/cc2-control"
#endif
#ifndef AU_BARRIER
#define AU_BARRIER "/tmp/cc2-control-install.lock"
#endif
#ifndef AU_FAULT
#define AU_FAULT(point,index) ((void)0)
#endif
#define AU_COUNT 4u
#define AU_LIMIT 1048576u
enum au_phase {AU_PREPARED=1,AU_APPLYING,AU_APPLIED,AU_ROLLING_BACK,AU_ROLLED_BACK,AU_COMMITTED};
struct au_file {const char *name,*before,*after;mode_t mode;};
struct au_plan {const char *old_app,*new_app,*profile;struct au_file files[AU_COUNT];};
struct au_record {unsigned phase;unsigned long long generation;char plan[65],journal[65];};
struct au_store {int directory,lock;struct cl_store native;};
static inline int au_hex(const char *s){
    if(!s||strlen(s)!=64)return 0;
    for(unsigned i=0;i<64;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;
    return 1;
}
static inline int au_directory_private(int fd){
    struct stat st;
    return fd>=0&&!fstat(fd,&st)&&S_ISDIR(st.st_mode)&&st.st_uid==0&&(st.st_mode&07777)==0700;
}
static inline int au_open_file(int directory,const char *name,const char *digest,mode_t mode){
    int fd=openat(directory,name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    struct stat st;char actual[65];
    int okay=fd>=0&&!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&st.st_nlink==1&&
        (st.st_mode&07777)==mode&&st.st_size>=0&&(uint64_t)st.st_size<=AU_LIMIT&&
        sha256_fd(fd,actual)&&!strcmp(actual,digest)&&lseek(fd,0,SEEK_SET)==0;
    if(!okay&&fd>=0){close(fd);fd=-1;}
    return fd;
}
static inline int au_match(int directory,const char *name,const char *digest,mode_t mode){
    int fd=au_open_file(directory,name,digest,mode);
    if(fd<0)return 0;
    close(fd);return 1;
}
static inline int au_plan_hash(const struct au_plan *p,char digest[65]){
    static const char *names[]={"coordinator","capture","app-integration.json","activate-on-ready"};
    if(!p||!au_hex(p->old_app)||!au_hex(p->new_app)||!au_hex(p->profile)||!strcmp(p->old_app,p->new_app))return 0;
    sha256_state state;sha256_init(&state);char line[320];
    int n=snprintf(line,sizeof(line),"CC2RFUP1 %s %s %s\n",p->old_app,p->new_app,p->profile);
    if(n<0||(size_t)n>=sizeof(line))return 0;
    sha256_update(&state,(unsigned char*)line,(size_t)n);
    for(unsigned i=0;i<AU_COUNT;i++){
        const struct au_file *f=p->files+i;
        if(!f->name||strcmp(f->name,names[i])||!au_hex(f->before)||!au_hex(f->after)||
            f->mode!=(i<2?0700:0600))return 0;
        n=snprintf(line,sizeof(line),"%s %04o %s %s\n",f->name,(unsigned)f->mode,f->before,f->after);
        if(n<0||(size_t)n>=sizeof(line))return 0;
        sha256_update(&state,(unsigned char*)line,(size_t)n);
    }
    sha256_hex(&state,digest);return 1;
}
static inline void au_close(struct au_store *s){
    cl_store_close(&s->native);
    if(s->lock>=0)close(s->lock);
    if(s->directory>=0)close(s->directory);
    s->directory=s->lock=-1;
}
static inline int au_open(struct au_store *s,int create){
    memset(s,0,sizeof(*s));s->directory=s->lock=s->native.directory=s->native.lock=-1;
    if(create&&mkdir(AU_DIRECTORY,0700)&&errno!=EEXIST)return 0;
    s->directory=open(AU_DIRECTORY,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(!au_directory_private(s->directory))goto bad;
    s->lock=openat(s->directory,"binding.lock",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK,0600);
    if(!cl_private(s->lock,0,0)||flock(s->lock,LOCK_EX|LOCK_NB)||!cl_store_open(&s->native,AU_COMPONENT))goto bad;
    return 1;
bad:
    au_close(s);return 0;
}
static inline int au_barrier(int borrowed){
    struct stat a,b;
    return au_directory_private(borrowed)&&!fstat(borrowed,&a)&&!lstat(AU_BARRIER,&b)&&
        S_ISDIR(b.st_mode)&&b.st_uid==0&&(b.st_mode&07777)==0700&&a.st_dev==b.st_dev&&a.st_ino==b.st_ino;
}
static inline int au_app(const char *digest){
    int fd=open(AU_APP,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);struct stat st;char actual[65];
    int okay=fd>=0&&!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&st.st_nlink==1&&
        !(st.st_mode&022)&&st.st_size>0&&(uint64_t)st.st_size<=8u*AU_LIMIT&&sha256_fd(fd,actual)&&!strcmp(actual,digest);
    if(fd>=0)close(fd);
    return okay;
}
static inline int au_native(struct au_store *s,const struct au_plan *p,char journal[65]){
    struct cl_record record;char profile[65];static const char hex[]="0123456789abcdef";
    if(!cl_store_read(&s->native,&record)||record.phase!=CL_ACTIVE)return 0;
    for(unsigned i=0;i<32;i++){profile[2*i]=hex[record.plan[i]>>4];profile[2*i+1]=hex[record.plan[i]&15];}profile[64]=0;
    if(strcmp(profile,p->profile))return 0;
    int fd=openat(s->native.directory,"lifecycle",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    int okay=cl_private(fd,0,CL_RECORD_SIZE)&&sha256_fd(fd,journal);
    if(fd>=0)close(fd);
    return okay;
}
static inline int au_temporary(int directory,char name[80]){
    unsigned char nonce[16];char text[33];static const char hex[]="0123456789abcdef";
    int random=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
    int okay=random>=0&&cl_io(random,nonce,sizeof(nonce),0);
    if(random>=0)close(random);
    if(!okay)return -1;
    for(unsigned i=0;i<16;i++){text[2*i]=hex[nonce[i]>>4];text[2*i+1]=hex[nonce[i]&15];}text[32]=0;
    int n=snprintf(name,80,".binding-%ld-%s",(long)getpid(),text);
    if(n<1||n>=80)return -1;
    return openat(directory,name,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
}
static inline int au_state_bytes(const struct au_record *r,char bytes[512]){
    if(r->phase<AU_PREPARED||r->phase>AU_COMMITTED||!r->generation||!au_hex(r->plan)||!au_hex(r->journal))return 0;
    int n=snprintf(bytes,512,"CC2RFUP1 %u %llu %s %s\n",r->phase,r->generation,r->plan,r->journal);
    if(n<1||n>=440)return 0;
    sha256_state hash;char digest[65];sha256_init(&hash);sha256_update(&hash,(unsigned char*)bytes,(size_t)n);sha256_hex(&hash,digest);
    memcpy(bytes+n,digest,64);bytes[n+64]='\n';return n+65;
}
static inline int au_read_state(struct au_store *s,struct au_record *record){
    char bytes[512],canonical[512];struct stat st;
    int fd=openat(s->directory,"state",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if(fd<0)return 0;
    int okay=!fstat(fd,&st)&&st.st_size>0&&st.st_size<(off_t)sizeof(bytes)&&cl_private(fd,0,st.st_size)&&
        cl_io(fd,(unsigned char*)bytes,(size_t)st.st_size,0);
    close(fd);if(!okay)return 0;
    bytes[st.st_size]=0;memset(record,0,sizeof(*record));
    if(sscanf(bytes,"CC2RFUP1 %u %llu %64s %64s",&record->phase,&record->generation,record->plan,record->journal)!=4)return 0;
    int n=au_state_bytes(record,canonical);
    return n==st.st_size&&!memcmp(bytes,canonical,(size_t)n);
}
static inline int au_write_state(struct au_store *s,struct au_record *record,unsigned phase){
    struct au_record before;struct stat st;
    if(record->generation){
        if(!au_read_state(s,&before)||before.generation!=record->generation||before.phase!=record->phase||
            strcmp(before.plan,record->plan)||strcmp(before.journal,record->journal)||record->generation==~0ULL)return 0;
    }else if(!fstatat(s->directory,"state",&st,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT)return 0;
    struct au_record next=*record;next.phase=phase;next.generation++;
    char bytes[512],temp[80];int n=au_state_bytes(&next,bytes);
    if(!n)return 0;
    int fd=au_temporary(s->directory,temp);
    int okay=fd>=0&&cl_io(fd,(unsigned char*)bytes,(size_t)n,1)&&!fsync(fd);
    if(fd>=0&&close(fd))okay=0;
    if(okay)okay=!renameat(s->directory,temp,s->directory,"state")&&!fsync(s->directory);
    if(okay){*record=next;AU_FAULT("state",phase);}
    return okay;
}
static inline int au_current(struct au_store *s,const struct au_file *f){
    return au_match(s->native.directory,f->name,f->after,f->mode)?1:
        au_match(s->native.directory,f->name,f->before,f->mode)?0:-1;
}
static inline int au_copy(int source,const char *name,const char *digest,mode_t source_mode,
                           int target,const char *dest,mode_t mode,int replace){
    int input=au_open_file(source,name,digest,source_mode);
    if(input<0)return 0;
    char temp[80],actual[65];int output=au_temporary(target,temp);unsigned char bytes[16384];int okay=output>=0;
    for(;;){
        if(!okay)break;
        ssize_t n=read(input,bytes,sizeof(bytes));
        if(n<0&&errno==EINTR)continue;
        if(n<0){okay=0;break;}
        if(!n)break;
        okay=cl_io(output,bytes,(size_t)n,1);
    }
    close(input);
    okay=okay&&!fchmod(output,mode)&&!fsync(output)&&lseek(output,0,SEEK_SET)==0&&sha256_fd(output,actual)&&!strcmp(actual,digest);
    if(output>=0&&close(output))okay=0;
    if(okay){
        if(replace)okay=!renameat(target,temp,target,dest);
        else okay=!syscall(SYS_renameat2,target,temp,target,dest,1u);
    }
    if(okay)okay=!fsync(target);
    /* Uncertain temporary paths remain as evidence, never recursively removed. */
    return okay;
}
static inline int au_snapshot(struct au_store *s,int source,const char *name,const char *digest,mode_t mode,const char *dest){
    struct stat st;
    if(!fstatat(s->directory,dest,&st,AT_SYMLINK_NOFOLLOW))return au_match(s->directory,dest,digest,0600);
    if(errno!=ENOENT)return 0;
    return au_copy(source,name,digest,mode,s->directory,dest,0600,0);
}
static inline int au_prepare(struct au_store *s,const struct au_plan *p,int source,int borrowed){
    char plan[65],journal[65];struct au_record state;struct stat st;
    if(!au_plan_hash(p,plan)||!au_barrier(borrowed)||!au_directory_private(source)||!au_app(p->old_app)||!au_native(s,p,journal))return 0;
    if(!fstatat(s->directory,"state",&st,AT_SYMLINK_NOFOLLOW)){
        if(!au_read_state(s,&state)||strcmp(state.plan,plan)||strcmp(state.journal,journal)||state.phase!=AU_PREPARED)return 0;
        for(unsigned i=0;i<AU_COUNT;i++){
            char name[32];const struct au_file *f=p->files+i;
            snprintf(name,sizeof(name),"old.%u",i);
            if(au_current(s,f)!=0||!au_match(s->directory,name,f->before,0600))return 0;
            snprintf(name,sizeof(name),"new.%u",i);
            if(!au_match(s->directory,name,f->after,0600))return 0;
        }
        return 1;
    }
    if(errno!=ENOENT)return 0;
    for(unsigned i=0;i<AU_COUNT;i++){
        const struct au_file *f=p->files+i;char old[32],fresh[32];
        snprintf(old,sizeof(old),"old.%u",i);snprintf(fresh,sizeof(fresh),"new.%u",i);
        if(au_current(s,f)!=0||!au_snapshot(s,s->native.directory,f->name,f->before,f->mode,old)||
            !au_snapshot(s,source,f->name,f->after,f->mode,fresh))return 0;
        AU_FAULT("snapshot",i);
    }
    if(!au_barrier(borrowed))return 0;
    memset(&state,0,sizeof(state));memcpy(state.plan,plan,65);memcpy(state.journal,journal,65);
    return au_write_state(s,&state,AU_PREPARED);
}
static inline int au_run(struct au_store *s,const struct au_plan *p,int borrowed,int rollback){
    char plan[65],journal[65];struct au_record state;
    if(!au_plan_hash(p,plan)||!au_barrier(borrowed)||!au_read_state(s,&state)||strcmp(state.plan,plan)||
        !au_native(s,p,journal)||strcmp(state.journal,journal)||
        !(au_app(p->new_app)||(rollback&&au_app(p->old_app))))return 0;
    if(!rollback&&(state.phase==AU_ROLLING_BACK||state.phase==AU_ROLLED_BACK))return 0;
    for(unsigned i=0;i<AU_COUNT;i++){
        char name[32];const struct au_file *f=p->files+i;
        snprintf(name,sizeof(name),"old.%u",i);
        if(au_current(s,f)<0||!au_match(s->directory,name,f->before,0600))return 0;
        snprintf(name,sizeof(name),"new.%u",i);
        if(!au_match(s->directory,name,f->after,0600))return 0;
    }
    unsigned complete=rollback?AU_ROLLED_BACK:AU_APPLIED;
    if(state.phase==complete||(!rollback&&state.phase==AU_COMMITTED)){
        for(unsigned i=0;i<AU_COUNT;i++)if(au_current(s,p->files+i)!=(rollback?0:1))return 0;
        return 1;
    }
    if(!au_write_state(s,&state,rollback?AU_ROLLING_BACK:AU_APPLYING))return 0;
    for(unsigned pos=0;pos<AU_COUNT;pos++){
        unsigned i=rollback?AU_COUNT-pos-1:pos;const struct au_file *f=p->files+i;
        if(!au_barrier(borrowed)||au_current(s,f)<0)return 0;
        if(au_current(s,f)!=(rollback?0:1)){
            char name[32];snprintf(name,sizeof(name),"%s.%u",rollback?"old":"new",i);
            if(!au_copy(s->directory,name,rollback?f->before:f->after,0600,s->native.directory,f->name,f->mode,1))return 0;
            AU_FAULT("replace",i);
        }
    }
    return au_write_state(s,&state,complete);
}
#endif
