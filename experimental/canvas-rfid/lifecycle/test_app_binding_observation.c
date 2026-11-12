#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static int sc_cancelled,scenario,guards,requests,reads,pauses;
static uint64_t stamp;
static uint64_t sc_now(void){return stamp;}
static int sc_fresh(void){guards++;return scenario==0||(guards>1&&scenario!=4);}
static int vc_http_body(const char *path,char **out,size_t *size){
    if(!strcmp(path,"/api/canvas")){
        requests++;if(scenario==3)return 0;
        *out=strdup("Canvas read hint, not permission");if(!*out)return 0;*size=strlen(*out);return 1;
    }
    reads++;if(scenario==5)return 0;
    char body[1024];int age=scenario==2?16:(stamp>=1500&&scenario!=1?0:7);
    int target=scenario==6||(scenario==7&&stamp>=1250)?200:0;
    int connected=scenario==8?0:1,velocity=scenario==9?1:0,recovery=scenario==10?1:0;
    if(!strcmp(path,"/api/printer"))
        snprintf(body,sizeof(body),"{\"connected\":true,\"last_message_age\":%d,\"machine\":{\"status\":1},\"extruder\":{\"temperature\":24,\"target\":%d},\"heater_bed\":{\"temperature\":20,\"target\":0},\"print\":{\"enabled\":false,\"native\":{\"state\":\"standby\"}},\"recovery\":{\"available\":%s,\"reboot_pending\":false},\"bed_drying\":{\"active\":false}}",age,target,recovery?"true":"false");
    else
        snprintf(body,sizeof(body),"{\"connected\":%s,\"fresh\":true,\"values\":{\"nozzle_target\":0,\"bed_target\":0,\"live_velocity\":%d,\"nozzle_temperature\":24,\"bed_temperature\":20},\"print_state\":\"standby\"}",connected?"true":"false",velocity);
    *out=strdup(body);*size=strlen(body);return *out!=NULL;
}
static int fake_pause(const struct timespec *duration,struct timespec *remaining){
    (void)remaining;pauses++;stamp+=(uint64_t)duration->tv_nsec/1000000u;
    if(scenario==11)sc_cancelled=1;
    return 0;
}
#define nanosleep fake_pause
#include "app_binding_observation.h"
#undef nanosleep
int main(void){
    for(scenario=0;scenario<12;scenario++){
        stamp=1000;sc_cancelled=guards=requests=reads=pauses=0;
        int okay=aw_fresh_ready();
        assert(okay==(scenario==0||scenario==3||scenario==12));
        if(scenario==0)assert(guards==1&&requests==0&&reads==0);
        if(scenario==1)assert(stamp>=11000&&guards==1&&requests==1);
        if(scenario==2||scenario==6||scenario==8||scenario==9||scenario==10)assert(requests==0&&pauses==0&&guards==1);
        if(scenario==3)assert(requests==1&&guards==2); /* No Canvas firmware operation exists here. */
        if(scenario==4)assert(guards==2); /* Collected ready never bypasses pinned guard. */
        if(scenario==7)assert(requests==1&&pauses==1&&guards==1);
        if(scenario==11)assert(sc_cancelled&&guards==1);
    }
    scenario=12;stamp=1000;sc_cancelled=guards=requests=reads=pauses=0;
    assert(aw_fresh_ready()&&guards==2&&requests==1&&pauses==2);
    puts("PASS: 13 acquisition cases, exact pinned guard required, expired MQTT only, native safety and timeout/cancellation preserved");
    return 0;
}
