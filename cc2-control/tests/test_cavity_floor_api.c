#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

static int dummy_native(const cavity_identity *id){return id->pid==1234;}
static int call(mqtt_client *m,const char *body,char *reply,size_t capacity){
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    if(body)cavity_floor_change(pair[0],m,body,strlen(body));else cavity_floor_response(pair[0]);
    ssize_t n=recv(pair[1],reply,capacity-1,0);assert(n>0);reply[n]=0;
    close(pair[0]);close(pair[1]);return atoi(strchr(reply,' ')+1);
}
static void state(int dir,unsigned generation,int printing){
    char text[128];int n=snprintf(text,sizeof(text),"CC2CF1 1234 1122334455667788 %u %d\n",generation,printing);
    assert(cavity_write_at(dir,"state",text,(size_t)n));
}
int main(void){
    assert(!mkdir(CC2_CAVITY_COMPONENT,0700));
    int fd=open(CC2_CAVITY_COMPONENT "/enabled",O_CREAT|O_EXCL|O_WRONLY,0600);assert(fd>=0);
    assert(write(fd,CC2_CAVITY_MARKER,sizeof(CC2_CAVITY_MARKER)-1)==(ssize_t)(sizeof(CC2_CAVITY_MARKER)-1));close(fd);
    int dir=cavity_directory(1);assert(dir>=0);state(dir,2,1);cavity_native_check=dummy_native;
    mqtt_client m={0};m.connected=m.registered=m.have_machine_status=1;m.machine_status=2;m.last_message=time(NULL);
    char reply[2048],text[256];
    assert(call(&m,NULL,reply,sizeof(reply))==200&&strstr(reply,"\"ready\":true")&&strstr(reply,"\"minimum\":null"));
    assert(call(&m,"60",reply,sizeof(reply))==202);
    assert(cavity_read_at(dir,"request",text));
    assert(!strcmp(text,"CC2CF1 1234 1122334455667788 2 60\n"));
    assert(call(&m,NULL,reply,sizeof(reply))==200&&strstr(reply,"\"minimum\":60.000"));
    const char *bad[]={"","-1","101","nan","inf","1e2","60.5","60\nM112","0;M107"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(call(&m,bad[i],reply,sizeof(reply))==400);
    m.last_message-=16;assert(call(&m,"80",reply,sizeof(reply))==409);m.last_message=time(NULL);
    m.connected=0;assert(call(&m,"80",reply,sizeof(reply))==409);m.connected=1;
    m.registered=0;assert(call(&m,"80",reply,sizeof(reply))==409);m.registered=1;
    m.machine_status=1;assert(call(&m,"80",reply,sizeof(reply))==409);
    /* Automatic remains available even after telemetry loss: no speed command. */
    assert(call(&m,"0",reply,sizeof(reply))==202&&!cavity_read_at(dir,"request",text));m.machine_status=2;
    state(dir,3,0);assert(call(&m,"80",reply,sizeof(reply))==409);
    state(dir,4,1);assert(call(&m,"100",reply,sizeof(reply))==202);
    state(dir,5,1);assert(call(&m,NULL,reply,sizeof(reply))==200&&strstr(reply,"\"minimum\":null"));
    assert(!unlink(CC2_CAVITY_COMPONENT "/enabled"));assert(call(&m,"80",reply,sizeof(reply))==409);
    unlinkat(dir,"request",0);unlinkat(dir,"state",0);close(dir);
    assert(!rmdir(CC2_CAVITY_CHANNEL));assert(!rmdir(CC2_CAVITY_COMPONENT));
    puts("PASS: exhaust minimum API, actual wire request, native identity, current print, stale/offline/invalid refusals and automatic release; no printer transport used.");
    return 0;
}
