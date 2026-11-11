/* Install an inert, closed-profile component directory under the ordinary
 * app installer's lock. No startup change, native stop, UART or flash here.
 * First migration only; an existing lifecycle requires its own upgrade flow. */
#define _GNU_SOURCE
#include "../host/sha256.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#ifndef CP_PARENT
#define CP_PARENT "/opt/usr"
#endif
#ifndef CP_PREFIX
#define CP_PREFIX ""
#endif
#ifndef CP_LOCK
#define CP_LOCK "/tmp/cc2-control-install.lock"
#endif
#ifndef CP_TARGET
#define CP_TARGET "cc2-rfid"
#endif
#ifndef CP_FAULT
#define CP_FAULT(point) ((void)0)
#endif
struct cp_file {const char *name;size_t size;mode_t mode;const char *sha;};
#ifndef CP_CONFIG
#error A qualified publisher configuration is required
#endif
#include CP_CONFIG
static int cp_private(int fd,int directory){
    struct stat st;return fd>=0&&!fstat(fd,&st)&&st.st_uid==0&&
        (directory?S_ISDIR(st.st_mode):S_ISREG(st.st_mode)&&st.st_nlink==1)&&!(st.st_mode&022);
}
static int cp_hash(int fd,const struct cp_file *file,int mode){
    struct stat st;char digest[65];
    return cp_private(fd,0)&&!fstat(fd,&st)&&st.st_size>=0&&(uint64_t)st.st_size<=67108864u&&
        (file->size==SIZE_MAX||(uint64_t)st.st_size==file->size)&&
        (!mode||(st.st_mode&07777)==file->mode)&&sha256_fd(fd,digest)&&!strcmp(digest,file->sha);
}
#ifdef CP_APP_BINDING
#define CP_SUBDIR "incoming"
static int cp_binding_name(const char *name){
    return !strcmp(name,"coordinator")||!strcmp(name,"capture")||
        !strcmp(name,"app-integration.json")||!strcmp(name,"activate-on-ready")||
        !strcmp(name,"transfer-worker");
}
static int cp_target_valid(void){
    if(strlen(CP_TARGET)!=64)return 0;
    for(unsigned i=0;i<64;i++)if(!((CP_TARGET[i]>='0'&&CP_TARGET[i]<='9')||
        (CP_TARGET[i]>='a'&&CP_TARGET[i]<='f')))return 0;
    return 1;
}
#else
#define CP_SUBDIR "payload"
static int cp_target_valid(void){return !strcmp(CP_TARGET,"cc2-rfid")||!strcmp(CP_TARGET,"cc2-cavity-floor");}
#endif
static int cp_open(int root,const char *name){
#ifdef CP_APP_BINDING
    if(!strncmp(name,"incoming/",9)){
        if(!cp_binding_name(name+9))return -1;
        int sub=openat(root,"incoming",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(!cp_private(sub,1)){if(sub>=0)close(sub);return -1;}
        int fd=openat(sub,name+9,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);close(sub);return fd;
    }
#else
    if(!strncmp(name,"payload/",8)){
        if(strlen(name)!=9||name[8]<'0'||name[8]>'7')return -1;
        int sub=openat(root,"payload",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(!cp_private(sub,1)){if(sub>=0)close(sub);return -1;}
        int fd=openat(sub,name+8,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);close(sub);return fd;
    }
#endif
    if(strchr(name,'/')||!strcmp(name,".")||!strcmp(name,".."))return -1;
    return openat(root,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
}
static int cp_expected(const char *name,int nested){
    if(!nested&&!strcmp(name,CP_SUBDIR))return 1;
    char full[96];int n=snprintf(full,sizeof(full),nested?CP_SUBDIR "/%s":"%s",name);
    if(n<1||(size_t)n>=sizeof(full))return 0;
    for(size_t i=0;i<sizeof(cp_files)/sizeof(cp_files[0]);i++)if(!strcmp(full,cp_files[i].name))return 1;
    return 0;
}
static int cp_names(int fd,int nested){
    int copy=openat(fd,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(copy<0)return 0;
    DIR *dir=fdopendir(copy);if(!dir){close(copy);return 0;}
    int okay=1;struct dirent64 *entry;errno=0;
    while((entry=readdir64(dir))){
        if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
        if(!cp_expected(entry->d_name,nested)){okay=0;break;}
        if(!nested&&!strcmp(entry->d_name,CP_SUBDIR)){
            int sub=openat(fd,CP_SUBDIR,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
            okay=cp_private(sub,1)&&cp_names(sub,1);if(sub>=0)close(sub);if(!okay)break;
        }
        errno=0;
    }
    if(!entry&&errno)okay=0;
    if(closedir(dir))okay=0;
    return okay;
}
static int cp_tree(int root,int installed){
    if(!cp_private(root,1)||!cp_names(root,0))return 0;
    struct stat st;if(fstat(root,&st)||(installed&&(st.st_mode&07777)!=0700))return 0;
    for(size_t i=0;i<sizeof(cp_files)/sizeof(cp_files[0]);i++){
        int fd=cp_open(root,cp_files[i].name);int okay=cp_hash(fd,cp_files+i,installed);if(fd>=0)close(fd);if(!okay)return 0;
    }
    return 1;
}
static int cp_dependencies(void){
    for(size_t i=0;i<sizeof(cp_before)/sizeof(cp_before[0]);i++){
        char path[1024];int n=snprintf(path,sizeof(path),"%s%s",CP_PREFIX,cp_before[i].name);
        if(n<1||(size_t)n>=sizeof(path))return 0;
        int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
        int okay=cp_hash(fd,cp_before+i,cp_before[i].mode!=0);if(fd>=0)close(fd);if(!okay)return 0;
    }
    return 1;
}
static int cp_copy_file(int source,int target,const struct cp_file *file){
    int input=cp_open(source,file->name);if(!cp_hash(input,file,0)){if(input>=0)close(input);return 0;}
    if(lseek(input,0,SEEK_SET)<0){close(input);return 0;}
    int output=openat(target,file->name,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    int okay=output>=0;unsigned char bytes[16384];size_t total=0;
    while(okay&&total<file->size){
        size_t amount=file->size-total;if(amount>sizeof(bytes))amount=sizeof(bytes);
        ssize_t got=read(input,bytes,amount);
        if(got<0&&errno==EINTR)continue;
        if(got<=0){okay=0;break;}
        size_t written=0;
        while(written<(size_t)got){ssize_t n=write(output,bytes+written,(size_t)got-written);if(n<0&&errno==EINTR)continue;if(n<=0){okay=0;break;}written+=(size_t)n;}
        total+=(size_t)got;
    }
    close(input);
    if(okay)okay=!fchmod(output,file->mode)&&!fsync(output);
    if(output>=0&&close(output))okay=0;
    return okay;
}
static int cp_lock_identity(int held){
    struct stat first,now;
    return cp_private(held,1)&&!fstat(held,&first)&&!lstat(CP_LOCK,&now)&&S_ISDIR(now.st_mode)&&
        (first.st_mode&07777)==0700&&now.st_dev==first.st_dev&&now.st_ino==first.st_ino;
}
int main(int argc,char **argv){
    int retire=argc==3&&!strcmp(argv[1],"--retire-dormant");
    if(geteuid()!=0||argc!=3||!cp_target_valid()||
       (strcmp(argv[1],"--check")&&strcmp(argv[1],"--publish")&&!retire)||
       (retire&&strcmp(CP_TARGET,"cc2-rfid"))){fprintf(stderr,"STOP: expected_closed_profile_publish_action\n");return 1;}
    umask(077);
    int lock=open(CP_LOCK,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    int source=open(argv[2],O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    int parent=open(CP_PARENT,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    int okay=cp_lock_identity(lock)&&cp_tree(source,0)&&cp_private(parent,1)&&cp_dependencies();
#ifdef CP_APP_BINDING
    struct stat parent_stat;
    okay=okay&&!fstat(parent,&parent_stat)&&(parent_stat.st_mode&07777)==0700;
#endif
    int installed=-1;
    if(okay){
        installed=openat(parent,CP_TARGET,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(installed>=0)okay=cp_tree(installed,1);
        else if(errno!=ENOENT)okay=0;
    }
    if(okay&&!strcmp(argv[1],"--publish")&&installed<0){
        uint64_t nonce=0;int random=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
        okay=random>=0&&read(random,&nonce,sizeof(nonce))==(ssize_t)sizeof(nonce)&&nonce;
        if(random>=0)close(random);
        char stage[96];int n=snprintf(stage,sizeof(stage),".cc2-rfid-stage-%ld-%016llx",(long)getpid(),(unsigned long long)nonce);
        okay=okay&&n>0&&(size_t)n<sizeof(stage)&&!mkdirat(parent,stage,0700);
        int temporary=okay?openat(parent,stage,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC):-1;
        okay=okay&&cp_private(temporary,1)&&!mkdirat(temporary,CP_SUBDIR,0700);
        for(size_t i=0;okay&&i<sizeof(cp_files)/sizeof(cp_files[0]);i++){
            okay=cp_copy_file(source,temporary,cp_files+i);if(okay)CP_FAULT("copied");
        }
        if(okay){int sub=openat(temporary,CP_SUBDIR,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);okay=sub>=0&&!fsync(sub);if(sub>=0)close(sub);}
        okay=okay&&cp_tree(temporary,1)&&!fsync(temporary)&&cp_dependencies()&&cp_lock_identity(lock);
        if(okay){CP_FAULT("before-publish");okay=!syscall(SYS_renameat2,parent,stage,parent,CP_TARGET,1u);}
        if(okay){CP_FAULT("published");okay=!fsync(parent);}
        if(temporary>=0)close(temporary);
        /* An interrupted/unpublished private stage is evidence, not garbage.
         * A later retry uses a new directory; no stale directory is deleted. */
    }
    if(okay&&retire){
        /* Exact dormant inventory only: any lifecycle/log/foreign evidence
         * makes cp_tree fail above. Preserve the tree in a no-replace archive. */
        uint64_t nonce=0;int random=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
        okay=installed>=0&&random>=0&&read(random,&nonce,sizeof(nonce))==(ssize_t)sizeof(nonce)&&nonce;
        if(random>=0)close(random);
        char archive[96];int n=snprintf(archive,sizeof(archive),".cc2-rfid-dormant-%ld-%016llx",(long)getpid(),(unsigned long long)nonce);
        okay=okay&&n>0&&(size_t)n<sizeof(archive)&&cp_tree(installed,1)&&cp_lock_identity(lock);
        if(okay)okay=!syscall(SYS_renameat2,parent,CP_TARGET,parent,archive,1u)&&!fsync(parent);
    }
    if(installed>=0)close(installed);
    if(parent>=0)close(parent);
    if(source>=0)close(source);
    if(lock>=0)close(lock);
    if(!okay){fprintf(stderr,"STOP: dormant_component_publication_refused; retain existing component and staging\n");return 1;}
    if(retire){puts("DORMANT COMPONENT ARCHIVED: exact inactive tree retained; native startup and Canvas unchanged.");return 0;}
    puts(!strcmp(argv[1],"--publish")?"RFID COMPONENT PREPARED: startup, native process and Canvas unchanged; bound activation waits for the verified app and installer unlock.":"RFID COMPONENT CHECK PASS: exact source and previous startup profile; no file changed.");
    return 0;
}
