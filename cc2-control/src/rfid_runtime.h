/* Native runtime metadata is installed privately with signed application
 * payloads. No paths or hashes come from an HTTP action. It breaks the build
 * dependency cycle: app -> coordinator(app SHA) -> capture(coordinator SHA).
 * Cheap status is cached; scan/apply force a fresh journal observation. */
#ifndef CC2_RFID_RUNTIME_H
#define CC2_RFID_RUNTIME_H
#include "connections/json.h"
#ifndef CC2_RFID_NATIVE_ROOT
#define CC2_RFID_NATIVE_ROOT "/opt/usr/cc2-rfid"
#endif
typedef struct {
    int mode,active,phase,new_boot,requested;uint64_t generation;
    char app[65],coordinator[65],capture[65],profile[65];
} rfid_runtime;
static rfid_runtime rfid_runtime_cache;
static double rfid_runtime_checked=-2;
static char rfid_runtime_self_sha[65];
static int rfid_runtime_self_checked;
static int rr_hex(const char *text){
    if(!text||strlen(text)!=64)return 0;
    unsigned nonzero=0;
    for(unsigned i=0;i<64;i++){
        if(!((text[i]>='0'&&text[i]<='9')||(text[i]>='a'&&text[i]<='f')))return 0;
        nonzero|=text[i]!='0';
    }
    return nonzero!=0;
}
static int rr_private(int fd,off_t size){
    struct stat st;
    return fd>=0&&!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==CC2_RFID_OWNER_UID&&st.st_nlink==1&&
        (st.st_mode&0777)==0600&&st.st_size==size;
}
static int rr_read(int fd,unsigned char *bytes,size_t size){
    size_t done=0;
    while(done<size){ssize_t got=read(fd,bytes+done,size-done);if(got<0&&errno==EINTR)continue;if(got<=0)return 0;done+=(size_t)got;}
    return 1;
}
static uint64_t rr_u64(const unsigned char *bytes){uint64_t n=0;for(unsigned i=0;i<8;i++)n|=(uint64_t)bytes[i]<<(i*8);return n;}
static int rr_activation(int root,const rfid_runtime *mode){
    char expected[192],actual[192];int size=snprintf(expected,sizeof(expected),"CC2-RFID-ACTIVATE-1\n%s\n%s\n",mode->app,mode->profile);
    if(size<1||(size_t)size>=sizeof(expected))return 0;
    int fd=openat(root,"activate-on-ready",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if(fd<0)return 0;
    int okay=rr_private(fd,size)&&rr_read(fd,(unsigned char*)actual,(size_t)size)&&!memcmp(expected,actual,(size_t)size);
    close(fd);return okay;
}
static int rr_journal(int root,rfid_runtime *out){
    unsigned char bytes[192];int fd=openat(root,"lifecycle",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if(fd<0)return 0;
    int okay=rr_private(fd,192)&&rr_read(fd,bytes,sizeof(bytes));close(fd);if(!okay)return 0;
    sha256_state hash;char digest[65];sha256_init(&hash);sha256_update(&hash,bytes,128);sha256_hex(&hash,digest);
    if(memcmp(digest,bytes+128,64)||memcmp(bytes,"CC2-CANVAS-LC1",14)||bytes[16]>=11||!rr_u64(bytes+24))return 0;
    for(unsigned i=14;i<24;i++)if(i!=16&&bytes[i])return 0;
    for(unsigned i=116;i<128;i++)if(bytes[i])return 0;
    unsigned transaction=0;for(unsigned i=32;i<48;i++)transaction|=bytes[i];if(!transaction)return 0;
    for(unsigned i=0;i<36;i++){
        unsigned char c=bytes[48+i];int dash=i==8||i==13||i==18||i==23;
        if(dash?c!='-':!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return 0;
    }
    static const char hex[]="0123456789abcdef";
    for(unsigned i=0;i<32;i++)if(out->profile[2*i]!=hex[bytes[84+i]>>4]||out->profile[2*i+1]!=hex[bytes[84+i]&15])return 0;
    out->phase=bytes[16];out->generation=rr_u64(bytes+24);out->active=out->phase==5;
    int boot=open("/proc/sys/kernel/random/boot_id",O_RDONLY|O_CLOEXEC);
    char current[38]={0};
    int valid=boot>=0&&rr_read(boot,(unsigned char*)current,37)&&current[36]=='\n';
    if(boot>=0)close(boot);
    /* Unknown kernel identity must never satisfy the required full reboot. */
    out->new_boot=valid&&memcmp(current,bytes+48,36)!=0;
    return 1;
}
static int rr_self(void){
    if(!rfid_runtime_self_checked){
        rfid_runtime_self_checked=1;
        int fd=open("/proc/self/exe",O_RDONLY|O_CLOEXEC);struct stat st;
        int good=fd>=0&&!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==CC2_RFID_OWNER_UID&&st.st_nlink==1&&!(st.st_mode&022)&&sha256_fd(fd,rfid_runtime_self_sha);
        if(fd>=0)close(fd);
        if(!good)rfid_runtime_self_sha[0]=0;
    }
    return rfid_runtime_self_sha[0]!=0;
}
static rfid_runtime rfid_runtime_get(int force){
    struct timespec stamp;double now=0;
    if(!clock_gettime(CLOCK_MONOTONIC,&stamp))now=stamp.tv_sec+stamp.tv_nsec/1e9;
    if(!force&&now>=rfid_runtime_checked&&now-rfid_runtime_checked<1)return rfid_runtime_cache;
    rfid_runtime out;memset(&out,0,sizeof(out));out.phase=-1;rfid_runtime_checked=now;
    int root=open(CC2_RFID_NATIVE_ROOT,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);struct stat st;
    if(root<0){out.mode=errno==ENOENT?0:-1;goto done;}
    out.mode=-1;
    if(fstat(root,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=CC2_RFID_OWNER_UID||(st.st_mode&077))goto close_root;
    int fd=openat(root,"app-integration.json",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if(fd<0){
        if(errno==ENOENT&&fstatat(root,"lifecycle",&st,AT_SYMLINK_NOFOLLOW)&&errno==ENOENT)out.mode=0;
        goto close_root;
    }
    char bytes[2049];off_t size=0;
    if(!fstat(fd,&st))size=st.st_size;
    int okay=size>0&&size<=2048&&rr_private(fd,size)&&rr_read(fd,(unsigned char*)bytes,(size_t)size);
    close(fd);if(!okay)goto close_root;
    bytes[size]=0;
    ng_json *j=malloc(sizeof(*j));if(!j)goto close_root;
    double protocol=0;
    okay=ng_json_parse(j,bytes,(size_t)size)&&j->v[0].type=='{'&&j->v[0].count==5&&
        ng_number(j,0,"protocol",&protocol)&&protocol==1;
    const char *a=ng_string(j,0,"app_sha256"),*c=ng_string(j,0,"coordinator_sha256"),
               *h=ng_string(j,0,"capture_sha256"),*p=ng_string(j,0,"profile_sha256");
    okay=okay&&rr_hex(a)&&rr_hex(c)&&rr_hex(h)&&rr_hex(p)&&rr_self()&&!strcmp(a,rfid_runtime_self_sha);
    if(okay){memcpy(out.app,a,65);memcpy(out.coordinator,c,65);memcpy(out.capture,h,65);memcpy(out.profile,p,65);out.mode=1;}
    free(j);
    if(okay&&!rr_journal(root,&out)){
        struct stat journal;
        /* Missing is dormant. Corrupt/unsafe existing evidence is unresolved,
         * and must never be treated as permission to create a new lifecycle. */
        if(!fstatat(root,"lifecycle",&journal,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT)out.phase=-2;
    }
    if(okay&&out.phase==-1)out.requested=rr_activation(root,&out);
close_root:
    close(root);
done:
    rfid_runtime_cache=out;return out;
}
static int rfid_runtime_allowed(int force){rfid_runtime mode=rfid_runtime_get(force);return mode.mode==0||(mode.mode==1&&mode.active);}
static int rfid_runtime_helper(char *path,size_t size,char digest[65],int require_active){
    rfid_runtime mode=rfid_runtime_get(1);
    if(mode.mode<0||(mode.mode==1&&require_active&&!mode.active))return 0;
    int n=snprintf(path,size,mode.mode==1?"%s/capture":"%s",mode.mode==1?CC2_RFID_NATIVE_ROOT:CC2_RFID_HELPER);
    if(n<1||(size_t)n>=size)return 0;
    const char *expected=mode.mode==1?mode.capture:CC2_RFID_HELPER_SHA;
    if(!rr_hex(expected))return 0;
    memcpy(digest,expected,65);return 1;
}
#endif
