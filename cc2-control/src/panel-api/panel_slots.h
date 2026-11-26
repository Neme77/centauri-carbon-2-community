/* Experimental publication core; caller owns the shared maintenance barrier.
 * Release signature verification, service restart and health checks belong to
 * the installer, not this module. Never expose commit as an HTTP command.
 */
#ifndef CC2_PANEL_SLOTS_H
#define CC2_PANEL_SLOTS_H
#include "panel_contract.h"
static inline int ps_id(const char *s){
    if(strlen(s)!=64)return 0;
    for(unsigned i=0;i<64;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;
    return 1;
}
static inline int ps_file(int root,const char *name,char *out,size_t size){
    int fd=openat(root,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);struct stat st;
    if(!pc_private(fd,0,0600)||fstat(fd,&st)||(size_t)st.st_size!=size){if(fd>=0)close(fd);return 0;}
    size_t done=0;
    while(done<size){ssize_t n=read(fd,out+done,size-done);if(n<0&&errno==EINTR)continue;if(n<=0){close(fd);return 0;}done+=(size_t)n;}
    close(fd);return 1;
}
static inline int ps_absent(int root,const char *name){struct stat st;return fstatat(root,name,&st,AT_SYMLINK_NOFOLLOW)<0&&errno==ENOENT;}
static inline int ps_write(int root,const char *name,const char *data,size_t size){
    /* Work file is never executable or selected. A leftover is preserved and
     * refused rather than overwritten after an uncertain storage failure. */
    int fd=openat(root,"publication.next",O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0)return 0;
    int okay=fchmod(fd,0600)==0;size_t done=0;
    while(okay&&done<size){ssize_t n=write(fd,data+done,size-done);if(n<0&&errno==EINTR)continue;if(n<=0){okay=0;break;}done+=(size_t)n;}
    if(okay)okay=fsync(fd)==0;
    if(close(fd))okay=0;
    if(okay)okay=renameat(root,"publication.next",root,name)==0;
    return okay&&fsync(root)==0;
}
static inline int ps_slot(int root,const char *id){
    if(!pc_private(root,1,0700)||!ps_id(id))return 0;
    int fd=openat(root,id,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return 0;
    struct pc_contract c;int okay=pc_read(fd,&c)&&!strcmp(id,c.app_sha)&&pc_unchanged(fd,&c);close(fd);return okay;
}
static inline int ps_selected(int root,char id[65]){
    char record[65];memset(id,0,65);
    if(!pc_private(root,1,0700)||!ps_file(root,"active",record,65)||record[64]!='\n')return 0;
    memcpy(id,record,64);return ps_id(id);
}
static inline int ps_active(int root,char id[65]){return ps_selected(root,id)&&ps_slot(root,id);}
static inline int ps_select(int root,const char *id){
    if(!ps_slot(root,id))return 0;
    char record[65];memcpy(record,id,64);record[64]='\n';return ps_write(root,"active",record,65);
}
static inline int ps_pending(int root,char old[65],char next[65]){
    char record[130];memset(old,0,65);memset(next,0,65);
    if(!ps_file(root,"pending",record,130)||record[64]!='\n'||record[129]!='\n')return 0;
    memcpy(old,record,64);memcpy(next,record+65,64);
    return ps_id(old)&&ps_id(next)&&strcmp(old,next)!=0;
}
static inline int ps_begin(int root,const char *expected,const char *next){
    char active[65];
    if(!ps_id(expected)||!ps_slot(root,next)||!ps_active(root,active)||strcmp(active,expected)||
       !strcmp(expected,next)||!ps_absent(root,"pending")||!ps_absent(root,"publication.next"))return 0;
    char record[130];memcpy(record,expected,64);record[64]='\n';memcpy(record+65,next,64);record[129]='\n';
    return ps_write(root,"pending",record,sizeof(record));
}
static inline int ps_activate(int root){
    char old[65],next[65],active[65];
    return ps_pending(root,old,next)&&ps_slot(root,old)&&ps_active(root,active)&&
        !strcmp(active,old)&&ps_select(root,next);
}
static inline int ps_finish(int root,int healthy){
    char old[65],next[65],active[65];
    if(!pc_private(root,1,0700)||!ps_pending(root,old,next)||!ps_selected(root,active)||
       (strcmp(active,old)&&strcmp(active,next))||!ps_absent(root,"publication.next"))return 0;
    if(healthy){
        if(strcmp(active,next)||!ps_slot(root,next)||!ps_slot(root,old))return 0;
    }else{
        if(!ps_slot(root,old)||(!strcmp(active,next)&&!ps_select(root,old)))return 0;
    }
    return unlinkat(root,"pending",0)==0&&fsync(root)==0;
}
static inline int ps_service_ready(int root,char id[65]){
    return ps_absent(root,"pending")&&ps_absent(root,"publication.next")&&ps_active(root,id);
}
#endif
