#ifndef CC2_EXTERNAL_FILAMENT_H
#define CC2_EXTERNAL_FILAMENT_H
#include "canvas_json.h"
typedef struct {int disconnected,idle,native_idle,active_channel,have_sensor,filament_detected;double temperature;const char *filament,*filament_end;} external_readback;
static inline int external_parse(const char *json,size_t length,external_readback *out){
    memset(out,0,sizeof(*out));if(!json||!length||length>8192)return 0;const char *end=json+length;
    const char *root=ccv_json_skip_space(json,end),*re,*se,*ce,*le,*xe;
    if(root==end||*root!='{'||!(re=ccv_json_container_end(root,end))||ccv_json_skip_space(re,end)!=end||ccv_json_member(root,re,"error"))return 0;
    const char *result=ccv_json_member_object(root,re,"result",'{',&re);
    const char *status=result?ccv_json_member_object(result,re,"status",'{',&se):NULL;
    const char *canvas=status?ccv_json_member_object(status,se,"canvas_dev",'{',&ce):NULL;
    const char *lite=canvas?ccv_json_member_object(canvas,ce,"canvas_lite",'{',&le):NULL;
    double connected,active;
    if(!lite||!ccv_numeric(lite,le,"connected",&connected)||(connected!=0&&connected!=1)||
       !ccv_numeric(canvas,ce,"active_cid",&active))return 0;
    if(active < -1 || active > 3 || active != (int)active)return 0;
    out->active_channel=(int)active;
    out->disconnected=connected==0;
    const char *ps=status?ccv_json_member_object(status,se,"print_stats",'{',&xe):NULL;
    const char *state=ps?ccv_json_member(ps,xe,"state"):NULL;
    int idle=state&&((xe-state>=9&&!memcmp(state,"\"standby\"",9))||
        (xe-state>=10&&!memcmp(state,"\"complete\"",10))||(xe-state>=11&&!memcmp(state,"\"cancelled\"",11)));
    out->native_idle=idle;
    out->idle=out->disconnected&&active==-1&&idle;
    const char *sensor=ccv_json_member_object(status,se,"filament_switch_sensor filament_sensor",'{',&xe);
    const char *detected=sensor?ccv_json_member(sensor,xe,"filament_detected"):NULL;
    if(detected){
        const char *tail=NULL;
        if(xe-detected>=4&&!memcmp(detected,"true",4)){out->filament_detected=1;tail=detected+4;}
        else if(xe-detected>=5&&!memcmp(detected,"false",5))tail=detected+5;
        if(tail){tail=ccv_json_skip_space(tail,xe);out->have_sensor=tail<xe&&(*tail==','||*tail=='}');}
    }
    const char *external=ccv_json_member_object(lite,le,"extern_channel",'{',&xe);
    if(external)out->filament=ccv_json_member_object(external,xe,"filament",'{',&out->filament_end);
    if(out->filament){
        const char *temp=ccv_json_member(out->filament,out->filament_end,"nozzle_max_temp");
        if(temp&&*temp=='"'){
            char *tail=NULL;double v=strtod(temp+1,&tail);
            if(tail>temp+1&&tail<out->filament_end&&*tail=='"'&&isfinite(v)&&v>=120&&v<=320)out->temperature=v;
        }
    }
    return 1;
}
#endif
