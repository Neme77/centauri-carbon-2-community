#ifndef CC2_CAVITY_FLOOR_PROTOCOL_H
#define CC2_CAVITY_FLOOR_PROTOCOL_H
#include <errno.h>
#include <fcntl.h>
#include <linux/stat.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#ifndef CC2_CAVITY_CHANNEL
#define CC2_CAVITY_CHANNEL "/tmp/cc2-cavity-floor"
#endif
#ifndef CC2_CAVITY_COMPONENT
#define CC2_CAVITY_COMPONENT "/opt/usr/cc2-cavity-floor"
#endif
#define CC2_CAVITY_MARKER "CC2-CAVITY-FLOOR-R1\n"
typedef struct {long pid;unsigned long long nonce;unsigned generation;int printing;} cavity_identity;

/* statx avoids newer glibc fstat/time64 symbol requirements in the optional
 * shared object. Unsupported kernels leave the native policy unchanged. */
static inline int cavity_stat(int fd,unsigned type,unsigned mode,unsigned links,unsigned limit) {
    struct statx s;
    memset(&s,0,sizeof(s));
    if(syscall(SYS_statx,fd,"",AT_EMPTY_PATH|AT_SYMLINK_NOFOLLOW,STATX_BASIC_STATS,&s))return 0;
    unsigned required=STATX_TYPE|STATX_MODE|STATX_UID|STATX_NLINK|STATX_SIZE;
    return (s.stx_mask&required)==required && s.stx_uid==0 &&
        (s.stx_mode&S_IFMT)==type && (s.stx_mode&0777)==mode &&
        (!links||s.stx_nlink==links) && (!limit||s.stx_size<=limit);
}
static inline int cavity_directory(int create) {
    if(create && mkdir(CC2_CAVITY_CHANNEL,0700) && errno!=EEXIST)return -1;
    int fd=open(CC2_CAVITY_CHANNEL,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(fd>=0 && !cavity_stat(fd,S_IFDIR,0700,0,0)){close(fd);return -1;}
    return fd;
}
static inline int cavity_read_at(int dir,const char *name,char out[256]) {
    int fd=openat(dir,name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return 0;
    int okay=cavity_stat(fd,S_IFREG,0600,1,255);
    ssize_t n=okay?read(fd,out,256):-1;
    close(fd);
    if(n<=0||n>255)return 0;
    out[n]=0;
    return !memchr(out,0,(size_t)n);
}
/* Small fixed wire grammar: no locale, floating parsing or newer scanf ABI.
 * Bound each digit before arithmetic; reject signs, overflow and leading zeroes. */
static inline int cavity_decimal(const char **cursor,unsigned limit,unsigned *out) {
    const char *p=*cursor;unsigned value=0,digits=0;
    if(*p<'0'||*p>'9'||(*p=='0'&&p[1]>='0'&&p[1]<='9'))return 0;
    while(*p>='0'&&*p<='9') {
        unsigned digit=(unsigned)(*p-'0');
        if(value>limit/10||(value==limit/10&&digit>limit%10))return 0;
        value=value*10+digit;p++;digits++;
    }
    if(!digits)return 0;
    *out=value;*cursor=p;return 1;
}
static inline int cavity_wire(const char *text,cavity_identity *id,unsigned limit,unsigned *last) {
    memset(id,0,sizeof(*id));
    if(strncmp(text,"CC2CF1 ",7))return 0;
    const char *p=text+7;unsigned pid;
    if(!cavity_decimal(&p,4194304,&pid)||pid<=1||*p++!=' ')return 0;
    id->pid=(long)pid;
    for(unsigned i=0;i<16;i++) {
        unsigned digit;
        if(*p>='0'&&*p<='9')digit=(unsigned)(*p-'0');
        else if(*p>='a'&&*p<='f')digit=(unsigned)(*p-'a'+10);
        else return 0;
        id->nonce=(id->nonce<<4)|digit;p++;
    }
    if(!id->nonce||*p++!=' '||!cavity_decimal(&p,UINT32_MAX,&id->generation)||
       !id->generation||*p++!=' '||!cavity_decimal(&p,limit,last))return 0;
    return !*p||(*p=='\n'&&!p[1]);
}
static inline int cavity_identity_parse(const char *text,cavity_identity *id) {
    unsigned printing;
    if(!cavity_wire(text,id,1,&printing))return 0;
    id->printing=(int)printing;return 1;
}
static inline int cavity_request_parse(const char *text,const cavity_identity *current,double *percent) {
    cavity_identity id;unsigned value;
    if(!cavity_wire(text,&id,100,&value)||!current->printing||id.pid!=current->pid||
       id.nonce!=current->nonce||id.generation!=current->generation)return 0;
    *percent=value;return 1;
}
static inline int cavity_write_at(int dir,const char *name,const char *text,size_t length) {
    static unsigned sequence;
    char temporary[80];int fd=-1;
    if(length>255 || (strcmp(name,"state")&&strcmp(name,"request")))return 0;
    for(int attempt=0;attempt<8;attempt++) {
        unsigned number=__sync_add_and_fetch(&sequence,1);
        snprintf(temporary,sizeof(temporary),".write-%ld-%u",(long)getpid(),number);
        fd=openat(dir,temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
        if(fd>=0)break;
        if(errno!=EEXIST)return 0;
    }
    if(fd<0)return 0;
    int okay=cavity_stat(fd,S_IFREG,0600,1,255);
    ssize_t n=okay?write(fd,text,length):-1;
    if(close(fd))okay=0;
    if(n!=(ssize_t)length)okay=0;
    if(okay && renameat(dir,temporary,dir,name))okay=0;
    if(!okay)unlinkat(dir,temporary,0);
    return okay;
}
static inline int cavity_enabled(void) {
    int fd=open(CC2_CAVITY_COMPONENT "/enabled",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return 0;
    char text[64];ssize_t n=cavity_stat(fd,S_IFREG,0600,1,63)?read(fd,text,sizeof(text)):-1;
    close(fd);
    return n==(ssize_t)(sizeof(CC2_CAVITY_MARKER)-1) && !memcmp(text,CC2_CAVITY_MARKER,sizeof(CC2_CAVITY_MARKER)-1);
}
#endif
