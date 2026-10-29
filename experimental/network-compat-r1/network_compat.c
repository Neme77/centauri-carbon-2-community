#define _GNU_SOURCE
#include "net_core.h"
#include "sha256_file.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define CAMERA_SHA "ec5633b17de329d5b18c3f8d8de6ebf389391a04193536d976ba167022d07b99"
#define PRINTER_SHA "19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745"
#define MODE_FILE "/tmp/cc2-network-compat-r1.mode"
static int (*original_connected)(void);
static cc2_net_string *(*original_camera_ip)(cc2_net_string *);
static int (*original_has_ip)(void *,const cc2_net_string *);
static int (*original_ip_changed)(void *,const cc2_net_string *);
static cc2_net_string *(*original_local_ip)(cc2_net_string *,void *);
static void refuse(const char *reason) {
    const char prefix[]="CC2 network compatibility refused: ";
    (void)write(2,prefix,sizeof(prefix)-1);(void)write(2,reason,strlen(reason));
    (void)write(2,"\n",1);_exit(126);
}
static int mode(void) {
    int fd=open(MODE_FILE,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)return 0;
    struct stat st;char data[16];
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=0||
       (st.st_mode&077)!=0||st.st_nlink!=1||st.st_size>15){close(fd);return 0;}
    ssize_t n=read(fd,data,sizeof(data));close(fd);
    return n>0?cc2_net_parse_mode(data,(size_t)n):0;
}
static int interface_ip(const char *name,char output[16]) {
    struct ifaddrs *all=NULL;output[0]=0;
    if(getifaddrs(&all))return 0;
    int found=0;
    for(struct ifaddrs *p=all;p;p=p->ifa_next) {
        if(!p->ifa_name||!p->ifa_addr||strcmp(p->ifa_name,name)||
           p->ifa_addr->sa_family!=AF_INET||!(p->ifa_flags&IFF_UP))continue;
        const struct sockaddr_in *address=(const struct sockaddr_in *)p->ifa_addr;
        if(address->sin_addr.s_addr==INADDR_ANY)continue;
        if(inet_ntop(AF_INET,&address->sin_addr,output,16)){found=1;break;}
    }
    freeifaddrs(all);return found;
}
static int ethernet_ready(void) {
    char ip[16],state[16];
    if(!interface_ip("cc2lan0",ip))return 0;
    FILE *f=fopen("/sys/class/net/cc2lan0/operstate","r");
    if(!f)return 0;
    int ok=fgets(state,sizeof(state),f)!=NULL&&!strcmp(state,"up\n");fclose(f);
    if(!ok)return 0;
    f=fopen("/sys/class/net/cc2lan0/carrier","r");
    if(!f)return 0;
    int carrier=0;ok=fscanf(f,"%d",&carrier)==1&&carrier==1;fclose(f);
    if(!ok)return 0;
    unsigned long long rx=0,tx=0;
    f=fopen("/sys/class/net/cc2lan0/statistics/rx_bytes","r");
    if(!f)return 0;
    ok=fscanf(f,"%llu",&rx)==1;fclose(f);if(!ok)return 0;
    f=fopen("/sys/class/net/cc2lan0/statistics/tx_bytes","r");
    if(!f)return 0;
    ok=fscanf(f,"%llu",&tx)==1;fclose(f);
    return ok&&(rx||tx);
}
static int selected_connected(void) {
    return mode()==2?ethernet_ready():original_connected();
}
static cc2_net_string *selected_camera_ip(cc2_net_string *out) {
    if(mode()!=2)return original_camera_ip(out);
    char ip[16];(void)interface_ip("cc2lan0",ip);
    if(!cc2_net_string_set(out,ip))refuse("camera IP representation");
    return out;
}
static int selected_has_ip(void *self,const cc2_net_string *name) {
    cc2_net_string temporary;
    return original_has_ip(self,cc2_net_map_name(name,&temporary,mode()));
}
static int selected_ip_changed(void *self,const cc2_net_string *name) {
    cc2_net_string temporary;
    return original_ip_changed(self,cc2_net_map_name(name,&temporary,mode()));
}
static cc2_net_string *selected_local_ip(cc2_net_string *out,void *self) {
    int selected=mode();
    if(!selected)return original_local_ip(out,self);
    char ip[16];(void)interface_ip(selected==2?"cc2lan0":"wlan0",ip);
    if(!cc2_net_string_set(out,ip))refuse("local IP representation");
    return out;
}
typedef struct {uintptr_t address;unsigned words[2];void *replacement;void *original;} hook;
static void function_pointer(void *out,void *address,size_t size) {
    if(size!=sizeof(address))refuse("function pointer ABI");
    memcpy(out,&address,size);
}
#ifndef CC2_NET_TEST
__attribute__((constructor))
#endif
static void activate(void) {
    const char *enabled=getenv("CC2_NETWORK_COMPAT_TRIAL");
    if(!enabled||strcmp(enabled,"1"))return;
    if(sizeof(void *)!=4||sizeof(cc2_net_string)!=24)refuse("ARM C++ ABI");
    char digest[65];
    if(cc2_sha256_file("/proc/self/exe",digest))refuse("cannot hash program");
    int camera=!strcmp(digest,CAMERA_SHA),printer=!strcmp(digest,PRINTER_SHA);
    if(!camera&&!printer)refuse("unqualified executable");
    if(sysconf(_SC_PAGESIZE)!=4096)refuse("page size");
    hook camera_hooks[]={
        {0x17ae44,{0xe92d4800,0xe28db004},NULL,NULL},
        {0x17af78,{0xe92d4870,0xe28db010},NULL,NULL}
    };
    hook printer_hooks[]={
        {0x8340d0,{0xe92d4810,0xe28db008},NULL,NULL},
        {0x833f04,{0xe92d4810,0xe28db008},NULL,NULL},
        {0x87dec0,{0xe92d4810,0xe28db008},NULL,NULL}
    };
    hook *hooks=camera?camera_hooks:printer_hooks;
    size_t count=camera?2:3;
    /* memcpy function pointers avoids nonportable ISO-C function/object casts. */
    int (*connected)(void)=selected_connected;
    cc2_net_string *(*camera_ip)(cc2_net_string *)=selected_camera_ip;
    int (*has_ip)(void *,const cc2_net_string *)=selected_has_ip;
    int (*ip_changed)(void *,const cc2_net_string *)=selected_ip_changed;
    cc2_net_string *(*local_ip)(cc2_net_string *,void *)=selected_local_ip;
    if(camera){memcpy(&hooks[0].replacement,&connected,4);memcpy(&hooks[1].replacement,&camera_ip,4);}
    else {memcpy(&hooks[0].replacement,&has_ip,4);memcpy(&hooks[1].replacement,&ip_changed,4);memcpy(&hooks[2].replacement,&local_ip,4);}
    for(size_t i=0;i<count;i++)
        if(!cc2_net_entry_words_valid(hooks[i].words)||
           memcmp((const void *)hooks[i].address,hooks[i].words,8))refuse("native instruction fingerprint");
    unsigned *trampoline=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(trampoline==MAP_FAILED)refuse("trampoline allocation");
    for(size_t i=0;i<count;i++) {
        unsigned *words=trampoline+4*i;
        words[0]=hooks[i].words[0];words[1]=hooks[i].words[1];
        words[2]=0xe51ff004u;words[3]=(unsigned)(hooks[i].address+8);
        hooks[i].original=words;
    }
    __builtin___clear_cache((char *)trampoline,(char *)trampoline+4096);
    if(mprotect(trampoline,4096,PROT_READ|PROT_EXEC))refuse("trampoline protection");
    if(camera){function_pointer(&original_connected,hooks[0].original,sizeof(original_connected));function_pointer(&original_camera_ip,hooks[1].original,sizeof(original_camera_ip));}
    else {function_pointer(&original_has_ip,hooks[0].original,sizeof(original_has_ip));function_pointer(&original_ip_changed,hooks[1].original,sizeof(original_ip_changed));function_pointer(&original_local_ip,hooks[2].original,sizeof(original_local_ip));}
    /* Constructor-only patching: native threads have not entered these methods. */
    for(size_t i=0;i<count;i++) {
        uintptr_t page=hooks[i].address&~(uintptr_t)4095;
        if(mprotect((void *)page,4096,PROT_READ|PROT_WRITE|PROT_EXEC))refuse("native write protection");
        unsigned words[]={0xe51ff004u,(unsigned)(uintptr_t)hooks[i].replacement};
        memcpy((void *)hooks[i].address,words,8);
        __builtin___clear_cache((char *)hooks[i].address,(char *)hooks[i].address+8);
        if(mprotect((void *)page,4096,PROT_READ|PROT_EXEC))refuse("native execute protection");
    }
    const char *path=camera?"/tmp/cc2-network-compat-r1.camera.status":"/tmp/cc2-network-compat-r1.printer.status";
    int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
    if(fd>=0){char line[220];int n=snprintf(line,sizeof(line),"trial=NETWORK_COMPAT_R1 pid=%ld role=%s patched=1 hooks=%zu mode=%d\n",(long)getpid(),camera?"camera":"printer",count,mode());if(n>0&&(size_t)n<sizeof(line))(void)write(fd,line,(size_t)n);close(fd);}
}
