/* Acquire a fresh cache observation, never authorize from an expired sample.
 * A quiet MQTT session has a ten-second heartbeat; the unchanged live guard
 * still requires age <=5 and fresh UDS immediately before preparation. */
#ifndef CC2_APP_BINDING_OBSERVATION_H
#define CC2_APP_BINDING_OBSERVATION_H
#include "readiness_policy.h"

static int aw_mqtt_only_expired(const ng_json *p,const ng_json *u){
    double age,status;int enabled;
    int machine=ng_member(p,0,"machine"),job=ng_member(p,0,"print"),values=ng_member(u,0,"values");
    if(!ng_field_bool(p,0,"connected",1)||!ng_number(p,0,"last_message_age",&age)||age<=5||age>15||
       !ng_number(p,machine,"status",&status)||status!=1||
       !ng_heater_safe(p,ng_member(p,0,"extruder"))||!ng_heater_safe(p,ng_member(p,0,"heater_bed"))||
       !ng_field_bool(u,0,"connected",1)||!ng_field_bool(u,0,"fresh",1)||
       !cr_zero(u,values,"nozzle_target")||!cr_zero(u,values,"bed_target")||!cr_zero(u,values,"live_velocity")||
       !cr_temperature(u,values,"nozzle_temperature")||!cr_temperature(u,values,"bed_temperature"))return 0;
    const char *state=ng_string(u,0,"print_state"),*native=ng_string(p,ng_member(p,job,"native"),"state");
    if(!state||!native||strcmp(state,native)||
       (strcmp(state,"standby")&&strcmp(state,"complete")&&strcmp(state,"cancelled"))||
       !ng_bool(p,job,"enabled",&enabled)||(enabled&&!strcmp(state,"standby")))return 0;
    int recovery=ng_member(p,0,"recovery"),drying=ng_member(p,0,"bed_drying");
    return ng_field_bool(p,recovery,"available",0)&&ng_field_bool(p,recovery,"reboot_pending",0)&&
           ng_field_bool(p,drying,"active",0);
}

static int aw_fresh_ready(void){
    if(sc_fresh())return 1;
    uint64_t now=sc_now(),deadline=now+10000u;int requested=0;
    if(!now||sc_cancelled)return 0;
    ng_json *p=malloc(sizeof(*p)),*u=malloc(sizeof(*u));
    if(!p||!u){free(p);free(u);return 0;}
    int okay=0;
    while(!sc_cancelled){
        now=sc_now();if(!now||now>=deadline)break;
        char *printer=NULL,*uds=NULL;size_t psize=0,usize=0;
        int parsed=vc_http_body("/api/printer",&printer,&psize)&&vc_http_body("/api/uds",&uds,&usize)&&
            ng_json_parse(p,printer,psize)&&ng_json_parse(u,uds,usize);
        free(printer);free(uds);
        if(!parsed){fputs("STOP: app_binding_observation_unavailable\n",stderr);break;}
        if(cr_ready(p,u)){
            /* A collected sample is not permission: the pinned original reader
             * must obtain and accept its own new observations as before. */
            okay=sc_fresh();break;
        }
        if(!aw_mqtt_only_expired(p,u)){
            fputs("STOP: app_binding_unsafe_live_observation\n",stderr);break;
        }
        if(!requested){
            /* GET Canvas enables the application's existing bounded native
             * information poll. It never sends a printer-control command. */
            /* This GET only enables the ordinary read-side information poll.
             * Its Canvas payload cannot authorize or veto an app-only update;
             * the original printer/UDS readiness guard remains authoritative. */
            char *hint=NULL;size_t hint_size=0;
            (void)vc_http_body("/api/canvas",&hint,&hint_size);free(hint);
            requested=1;
            puts("ACQUIRING FRESH MQTT: quiet cached sample expired; original five-second guard unchanged.");fflush(stdout);
        }
        struct timespec pause={0,250000000};
        while(nanosleep(&pause,&pause)&&errno==EINTR&&!sc_cancelled){}
    }
    free(p);free(u);return okay&&!sc_cancelled;
}
#endif
