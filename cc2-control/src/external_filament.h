#ifndef CC2_EXTERNAL_FILAMENT_H
#define CC2_EXTERNAL_FILAMENT_H
#include "canvas_json.h"
typedef struct {int disconnected,idle;const char *filament,*filament_end;} external_readback;
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
    out->disconnected=connected==0;
    const char *ps=status?ccv_json_member_object(status,se,"print_stats",'{',&xe):NULL;
    const char *state=ps?ccv_json_member(ps,xe,"state"):NULL;
    int idle=state&&((xe-state>=9&&!memcmp(state,"\"standby\"",9))||
        (xe-state>=10&&!memcmp(state,"\"complete\"",10))||(xe-state>=11&&!memcmp(state,"\"cancelled\"",11)));
    out->idle=out->disconnected&&active==-1&&idle;
    const char *external=ccv_json_member_object(lite,le,"extern_channel",'{',&xe);
    if(external)out->filament=ccv_json_member_object(external,xe,"filament",'{',&out->filament_end);
    return 1;
}
#endif
