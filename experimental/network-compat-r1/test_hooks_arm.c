#define _GNU_SOURCE
#define CC2_NET_TEST 1
#include "network_compat.c"
#include <assert.h>
static int fixture_camera;
static char last_name[32];
int cc2_sha256_file(const char *path,char out[65]) {
    assert(!strcmp(path,"/proc/self/exe"));
    strcpy(out,fixture_camera?CAMERA_SHA:PRINTER_SHA);return 0;
}
static int fixture_connected(void){return 1;}
static cc2_net_string *fixture_camera_ip(cc2_net_string *out){assert(cc2_net_string_set(out,"192.0.2.103"));return out;}
static int fixture_has_ip(void *self,const cc2_net_string *name){(void)self;assert(name->length<sizeof(last_name));memcpy(last_name,name->data,name->length);last_name[name->length]=0;return 1;}
static int fixture_changed(void *self,const cc2_net_string *name){(void)self;return cc2_net_string_equal(name,"cc2lan0");}
static cc2_net_string *fixture_local_ip(cc2_net_string *out,void *self){(void)self;return fixture_camera_ip(out);}
static uintptr_t address_of(const void *object,size_t size){uintptr_t address=0;assert(size==sizeof(address));memcpy(&address,object,size);return address;}
static void entry(uintptr_t address,unsigned push,unsigned fp,uintptr_t helper){
    static uintptr_t pages[5];static size_t page_count;
    uintptr_t page=address&~(uintptr_t)4095;
    void *p=(void *)page;size_t i;
    for(i=0;i<page_count;i++)if(pages[i]==page)break;
    if(i==page_count){
        p=mmap((void *)page,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
        assert(p==(void *)page);assert(page_count<5);pages[page_count++]=page;
    }else assert(!mprotect(p,4096,PROT_READ|PROT_WRITE));
    unsigned words[]={push,fp,0xe24bd000u|(fp&0xfffu),0xe8bd0000u|(push&0xffffu),0xe51ff004u,(unsigned)helper};
    memcpy((void *)address,words,sizeof(words));
    __builtin___clear_cache((char *)address,(char *)address+sizeof(words));
    assert(!mprotect(p,4096,PROT_READ|PROT_EXEC));
}
static void write_mode(const char *value){
    int fd=open(MODE_FILE,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);assert(fd>=0);
    assert(!fchmod(fd,0600));assert(write(fd,value,strlen(value))==(ssize_t)strlen(value));close(fd);
}
int main(int argc,char **argv){
    assert(argc==2);assert(sizeof(void *)==4);fixture_camera=!strcmp(argv[1],"camera");
    assert(!setenv("CC2_NETWORK_COMPAT_TRIAL","1",1));
    (void)unlink(MODE_FILE);write_mode("wifi\n");
    if(fixture_camera){
        int (*connected)(void)=fixture_connected;
        cc2_net_string *(*get_ip)(cc2_net_string *)=fixture_camera_ip;
        entry(0x17ae44,0xe92d4800,0xe28db004,address_of(&connected,sizeof(connected)));
        entry(0x17af78,0xe92d4870,0xe28db010,address_of(&get_ip,sizeof(get_ip)));
        activate();
        uintptr_t address=0x17ae44;memcpy(&connected,&address,4);
        address=0x17af78;memcpy(&get_ip,&address,4);
        assert(connected()==1);cc2_net_string out;get_ip(&out);assert(cc2_net_string_equal(&out,"192.0.2.103"));
        write_mode("ethernet\n");
        assert(connected()==0);get_ip(&out);assert(cc2_net_string_equal(&out,""));
        write_mode("wifi\n");assert(connected()==1);get_ip(&out);assert(cc2_net_string_equal(&out,"192.0.2.103"));
    }else{
        int (*has_ip)(void *,const cc2_net_string *)=fixture_has_ip;
        int (*changed)(void *,const cc2_net_string *)=fixture_changed;
        cc2_net_string *(*get_ip)(cc2_net_string *,void *)=fixture_local_ip;
        entry(0x8340d0,0xe92d4810,0xe28db008,address_of(&has_ip,sizeof(has_ip)));
        entry(0x833f04,0xe92d4810,0xe28db008,address_of(&changed,sizeof(changed)));
        entry(0x87dec0,0xe92d4810,0xe28db008,address_of(&get_ip,sizeof(get_ip)));
        activate();
        uintptr_t address=0x8340d0;memcpy(&has_ip,&address,4);
        address=0x833f04;memcpy(&changed,&address,4);
        address=0x87dec0;memcpy(&get_ip,&address,4);
        cc2_net_string input,out;assert(cc2_net_string_set(&input,"wlan0"));
        assert(has_ip(NULL,&input)==1&&!strcmp(last_name,"wlan0"));assert(!changed(NULL,&input));
        write_mode("ethernet\n");assert(has_ip(NULL,&input)==1&&!strcmp(last_name,"cc2lan0"));assert(changed(NULL,&input));
        assert(cc2_net_string_equal(&input,"wlan0"));
        assert(cc2_net_string_set(&input,"other0"));assert(has_ip(NULL,&input)==1&&!strcmp(last_name,"other0"));
        get_ip(&out,NULL);assert(cc2_net_string_equal(&out,""));
        assert(!unlink(MODE_FILE));get_ip(&out,NULL);assert(cc2_net_string_equal(&out,"192.0.2.103"));
    }
    (void)unlink(MODE_FILE);
    printf("PASS ARM live entry/trampoline/wrapper return and mode transitions: %s\n",argv[1]);return 0;
}
