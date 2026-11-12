/* Scan-only variant: keep the recovery-worker profile implementation byte-stable.
 * Native evidence is supplied only after a full process/executable observation.
 * Exact native role path/digest and every other profile role remain mandatory. */
#ifndef CC2_SCAN_PROFILE_H
#define CC2_SCAN_PROFILE_H
#include "profile.h"
static inline int cl_profile_native_checked(struct cl_store *store,const struct cl_record *record,
                                           const char *native_path,const char *native_digest) {
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
        if(role==1&&native_path&&native_digest){
            if(strcmp(path,native_path)||strcmp(first,native_digest))return 0;
        }else if(!cl_digest_path(path,first)) return 0;
        line=end+1;
    }
    return seen==63;
}
#endif
