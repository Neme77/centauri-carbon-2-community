#include "cavity_floor_protocol.h"

static int cavity_native_verified(const cavity_identity *id) {
    char path[96],exe[512],line[1024];
    snprintf(path,sizeof(path),"/proc/%ld/exe",id->pid);
    ssize_t n=readlink(path,exe,sizeof(exe)-1);
    if(n<=0||n>=(ssize_t)sizeof(exe)-1)return 0;
    exe[n]=0;
    if(strcmp(exe,"/opt/usr/cc2-reactor-runtime-v2/elegoo_printer"))return 0;
    snprintf(path,sizeof(path),"/proc/%ld/maps",id->pid);
    FILE *f=fopen(path,"r");if(!f)return 0;
    int mapped=0;size_t bytes=0;
    while(fgets(line,sizeof(line),f)) {
        bytes+=strlen(line);if(bytes>131072)break;
        char *module=strstr(line,CC2_CAVITY_COMPONENT "/libcc2-cavity-floor-r1.so");
        if(module && !strcmp(module,CC2_CAVITY_COMPONENT "/libcc2-cavity-floor-r1.so\n")){mapped=1;break;}
    }
    fclose(f);return mapped;
}
/* Production uses the real process/maps check; API tests replace only this
 * boundary with a dummy native identity, never a printer command transport. */
static int (*cavity_native_check)(const cavity_identity *)=cavity_native_verified;
static int cavity_current(int dir,cavity_identity *id) {
    char text[256];
    return cavity_enabled() && cavity_read_at(dir,"state",text) &&
        cavity_identity_parse(text,id) && cavity_native_check(id);
}
static void cavity_floor_response(int fd) {
    cavity_identity id;char request[256],body[256],minimum[32];double value=0;
    int installed=cavity_enabled(),dir=cavity_directory(0);
    int ready=dir>=0 && cavity_current(dir,&id);
    int requested=ready&&id.printing&&cavity_read_at(dir,"request",request)&&cavity_request_parse(request,&id,&value)&&value>0;
    if(requested)snprintf(minimum,sizeof(minimum),"%.3f",value);else snprintf(minimum,sizeof(minimum),"null");
    if(dir>=0)close(dir);
    snprintf(body,sizeof(body),"{\"installed\":%s,\"ready\":%s,\"printing\":%s,\"minimum\":%s}\n",
        installed?"true":"false",ready?"true":"false",ready&&id.printing?"true":"false",minimum);
    respond(fd,200,"OK","application/json; charset=utf-8",body,strlen(body));
}
static void cavity_floor_change(int fd,mqtt_client *mqtt,const char *body,size_t length) {
    char input[16],*end;long percent;
    if(!length||length>=sizeof(input)||memchr(body,0,length)) {
        plates_fail(fd,400,"Exhaust minimum must be an integer from 0 to 100");return;
    }
    memcpy(input,body,length);input[length]=0;errno=0;
    percent=strtol(input,&end,10);
    if(errno||end==input||*end||percent<0||percent>100) {
        plates_fail(fd,400,"Exhaust minimum must be an integer from 0 to 100");return;
    }
    int dir=cavity_directory(0);
    if(dir<0){plates_fail(fd,409,"Native exhaust minimum component is not ready");return;}
    /* Automatic is a release of ownership, not a command forcing speed zero. */
    if(!percent) {
        int okay=!unlinkat(dir,"request",0)||errno==ENOENT;
        close(dir);
        if(!okay){plates_fail(fd,503,"Cannot release exhaust minimum");return;}
        const char *reply="{\"accepted\":true,\"automatic\":true}\n";
        respond(fd,202,"Accepted","application/json; charset=utf-8",reply,strlen(reply));return;
    }
    cavity_identity id;time_t now=time(NULL);
    if(!mqtt->connected||!mqtt->registered||!mqtt->have_machine_status||mqtt->machine_status!=2||
       mqtt->last_message<=0||now<mqtt->last_message||now-mqtt->last_message>15||
       !cavity_current(dir,&id)||!id.printing) {
        close(dir);plates_fail(fd,409,"Exhaust minimum requires a fresh running print and its native component");return;
    }
    char text[128];int n=snprintf(text,sizeof(text),"CC2CF1 %ld %016llx %u %u\n",id.pid,id.nonce,id.generation,(unsigned)percent);
    if(n<=0||!cavity_write_at(dir,"request",text,(size_t)n)) {
        close(dir);plates_fail(fd,503,"Cannot write exhaust minimum request");return;
    }
    cavity_identity after;
    if(!cavity_current(dir,&after)||after.pid!=id.pid||after.nonce!=id.nonce||after.generation!=id.generation||!after.printing) {
        char current[256];
        if(cavity_read_at(dir,"request",current)&&!strcmp(current,text))unlinkat(dir,"request",0);
        close(dir);plates_fail(fd,409,"Print state changed; exhaust minimum was not retained");return;
    }
    close(dir);
    const char *reply="{\"accepted\":true,\"applies_on_native_cycle\":true}\n";
    respond(fd,202,"Accepted","application/json; charset=utf-8",reply,strlen(reply));
}
