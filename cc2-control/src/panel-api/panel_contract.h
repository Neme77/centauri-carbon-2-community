/* Experimental panel/service contract. Not wired into printer operations yet.
 * Only a verified release installer may publish a slot. This record is NOT a
 * signature and cannot establish release provenance on its own.
 * Callers hold the shared maintenance barrier and retain the selected slot fd.
 * The service keeps its own component hashes and hardware guards unchanged.
 */
#ifndef CC2_PANEL_CONTRACT_H
#define CC2_PANEL_CONTRACT_H
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "sha256.h"
#define PC_API 1
struct pc_contract { unsigned api; char app_sha[65]; };
static inline int pc_private(int fd,int directory,mode_t mode){
    struct stat st;
    return fd>=0&&!fstat(fd,&st)&&st.st_uid==0&&
        (directory?S_ISDIR(st.st_mode):(S_ISREG(st.st_mode)&&st.st_nlink==1))&&
        (st.st_mode&07777)==mode;
}
static inline int pc_read(int slot,struct pc_contract *out){
    /* Clear the result on every refusal; no stale authorization survives. */
    memset(out,0,sizeof(*out));
    if(!pc_private(slot,1,0700))return 0;
    int fd=openat(slot,"panel.contract",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if(!pc_private(fd,0,0600)){if(fd>=0)close(fd);return 0;}
    char record[128]={0};size_t used=0;int valid=1;
    for(;;){
        ssize_t n=read(fd,record+used,sizeof(record)-used);
        if(n<0&&errno==EINTR)continue;
        if(n<0){valid=0;break;}
        if(!n)break;
        used+=(size_t)n;
        if(used==sizeof(record)){valid=0;break;}
    }
    close(fd);
    const char prefix[]="CC2-PANEL-ABI1\napi=1\nsha256=";
    size_t start=sizeof(prefix)-1;
    if(!valid||used!=start+65||memcmp(record,prefix,start)||record[used-1]!='\n')return 0;
    unsigned nonzero=0;
    for(size_t i=0;i<64;i++){
        unsigned char c=(unsigned char)record[start+i];
        if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return 0;
        nonzero|=c!='0';
    }
    if(!nonzero)return 0;
    fd=openat(slot,"cc2-control",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    char digest[65]={0};
    valid=pc_private(fd,0,0755)&&sha256_fd(fd,digest)&&!memcmp(digest,record+start,64);
    if(fd>=0)close(fd);
    if(!valid)return 0;
    out->api=PC_API;memcpy(out->app_sha,digest,65);return 1;
}
static inline int pc_unchanged(int slot,const struct pc_contract *before){
    struct pc_contract after;
    return before->api==PC_API&&pc_read(slot,&after)&&
        after.api==before->api&&!strcmp(before->app_sha,after.app_sha);
}
#endif
