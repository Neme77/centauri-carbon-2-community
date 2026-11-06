/* Native maintenance read-only predicate. Terminal cancelled jobs are idle
 * only with matching fresh native state, zero targets and no actual motion. */
#ifndef CC2_CANVAS_READINESS_POLICY_H
#define CC2_CANVAS_READINESS_POLICY_H
#include "../../../cc2-control/src/connections/core.h"
static inline int cr_zero(const ng_json *j,int at,const char *key){double value;return ng_number(j,at,key,&value)&&value==0;}
static inline int cr_temperature(const ng_json *j,int at,const char *key){double value;return ng_number(j,at,key,&value)&&value>=0&&value<=50;}
static inline int cr_ready(const ng_json *printer,const ng_json *uds){
    double age,status;int enabled;
    int machine=ng_member(printer,0,"machine"),job=ng_member(printer,0,"print"),values=ng_member(uds,0,"values");
    if(!ng_field_bool(printer,0,"connected",1)||!ng_number(printer,0,"last_message_age",&age)||age<0||age>5||
       !ng_number(printer,machine,"status",&status)||status!=1||
       !ng_heater_safe(printer,ng_member(printer,0,"extruder"))||!ng_heater_safe(printer,ng_member(printer,0,"heater_bed"))||
       !ng_field_bool(uds,0,"connected",1)||!ng_field_bool(uds,0,"fresh",1)||
       !cr_zero(uds,values,"nozzle_target")||!cr_zero(uds,values,"bed_target")||!cr_zero(uds,values,"live_velocity")||
       !cr_temperature(uds,values,"nozzle_temperature")||!cr_temperature(uds,values,"bed_temperature"))return 0;
    const char *state=ng_string(uds,0,"print_state"),*native=ng_string(printer,ng_member(printer,job,"native"),"state");
    if(!state||!native||strcmp(state,native)||
       (strcmp(state,"standby")&&strcmp(state,"complete")&&strcmp(state,"cancelled"))||
       !ng_bool(printer,job,"enabled",&enabled)||(enabled&&!strcmp(state,"standby")))return 0;
    int recovery=ng_member(printer,0,"recovery"),drying=ng_member(printer,0,"bed_drying");
    return ng_field_bool(printer,recovery,"available",0)&&ng_field_bool(printer,recovery,"reboot_pending",0)&&
           ng_field_bool(printer,drying,"active",0);
}
#endif
