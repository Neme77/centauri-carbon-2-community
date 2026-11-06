/* The root-private recovery plan is bound by the durable journal. Each role
 * occurs once; a changed/missing boot entry or guard refuses UART access. */
#ifndef CC2_CANVAS_LIFECYCLE_PROFILE_H
#define CC2_CANVAS_LIFECYCLE_PROFILE_H
#include "journal.h"
#define CL_PROFILE_MAX 8192u
static inline int cl_digest_path(const char *path,const char expected[65]) {
    struct stat st;char digest[65];
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if(fd<0) return 0;
    int okay=!fstat(fd,&st) && S_ISREG(st.st_mode) && st.st_uid==0 && st.st_nlink==1 &&
             !(st.st_mode&022) && sha256_fd(fd,digest) && !strcmp(digest,expected);
    close(fd);return okay;
}
static inline int cl_hex(const char *s,size_t n) {
    for(size_t i=0;i<n;i++) if(!((s[i]>='0' && s[i]<='9') || (s[i]>='a' && s[i]<='f'))) return 0;
    return 1;
}
static inline int cl_profile(struct cl_store *store,const struct cl_record *record) {
    static const char *roles[]={"bootstrap","native","guard","entry","recover","marker"};
    unsigned char data[CL_PROFILE_MAX+1];struct stat st;char digest[65],expected[65];sha256_state hash;
    int fd=openat(store->directory,"recovery-profile",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if(fd<0) return 0;
    int okay=!fstat(fd,&st) && st.st_size>0 && (uint64_t)st.st_size<=CL_PROFILE_MAX &&
             cl_private(fd,0,st.st_size) && cl_io(fd,data,(size_t)st.st_size,0);
    close(fd);if(!okay) return 0;
    size_t size=(size_t)st.st_size;
    if(data[size-1]!='\n' || memchr(data,0,size)) return 0;
    sha256_init(&hash);sha256_update(&hash,data,size);sha256_hex(&hash,digest);
    static const char hex[]="0123456789abcdef";
    for(unsigned i=0;i<32;i++){expected[2*i]=hex[record->plan[i]>>4];expected[2*i+1]=hex[record->plan[i]&15];}
    expected[64]=0;
    if(strcmp(digest,expected)) return 0;
    data[size]=0;unsigned seen=0;char *line=(char*)data;
    while(*line) {
        char *end=strchr(line,'\n');if(!end) return 0;*end=0;
        char *first=strchr(line,' ');if(!first) return 0;*first++=0;
        unsigned role=6;
        for(unsigned i=0;i<6;i++) if(!strcmp(line,roles[i])) role=i;
        if(role==6 || (seen&(1u<<role))) return 0;
        seen|=1u<<role;
        size_t length=strlen(first);
        if(length<67 || first[64]!=' ' || first[65]!='/' || !cl_hex(first,64)) return 0;
        first[64]=0;char *path=first+65;
        if(strlen(path)>512 || strstr(path,"//") || strstr(path,"/../") || strstr(path,"/./")) return 0;
        for(char *p=path;*p;p++) if((unsigned char)*p<=32 || (unsigned char)*p>=127) return 0;
        if(!cl_digest_path(path,first)) return 0;
        line=end+1;
    }
    return seen==63;
}
#endif
