#ifndef CC2_CANVAS_STATE_H
#define CC2_CANVAS_STATE_H
#include "mqtt.h"
#include "canvas_json.h"
#include <time.h>
enum canvas_slot_kind { CV_UNKNOWN,CV_EMPTY,CV_PRESENT,CV_LOADED,CV_TRANSITION };
enum canvas_reason { CR_NONE,CR_UNAVAILABLE,CR_EMPTY,CR_BUSY,CR_HEAD_OCCUPIED,CR_NOT_LOADED,CR_ALREADY_LOADED,CR_TRANSITION,CR_UNASSIGNED_HEAD };
typedef struct {int state,load,unload,reason_load,reason_unload;} canvas_slot_view;
static inline const char *canvas_kind_name(int state) {
 static const char *names[]={"unknown","empty","present","loaded","transition"};
 return state>=0&&state<=CV_TRANSITION?names[state]:names[0];
}
static inline const char *canvas_reason_name(int reason) {
 static const char *names[]={"none","unavailable","empty","busy","head_occupied","not_loaded","already_loaded","transition","unassigned_head"};
 return reason>=0&&reason<=CR_UNASSIGNED_HEAD?names[reason]:names[1];
}
/* MQTT Canvas is a retained delta cache within one live session; the existing
 * disconnect path clears it. Do not add full Canvas UDS subscriptions/polling.
 * Direct UDS provides only active channel and the small head-presence boolean.
 */
static inline int canvas_source_valid(const mqtt_client *m) {
 time_t now=time(NULL);
 return m->connected&&m->registered&&m->last_message>0&&now>=m->last_message&&now-m->last_message<=30
   &&m->canvas_snapshot_len>0&&m->canvas_snapshot_len<sizeof(m->canvas_snapshot)
   &&m->canvas_direct_known;
}
static inline const char *canvas_child(const char *p,const char *end,const char *key,const char **child_end) {
 return p?ccv_json_member_object(p,end,key,'{',child_end):NULL;
}
/* Parse only the connected module and exact tray id. Missing/duplicate entries
 * are unknown, never inferred to be empty. Values from material strings cannot
 * masquerade as top-level status fields.
 */
static inline int canvas_reported_status(const mqtt_client *m,int slot) {
 const char *p=m->canvas_snapshot,*end=p+m->canvas_snapshot_len,*re=end,*ie=end,*le=end;
 if(slot<0||slot>3||p==end||*p!='{'||ccv_json_container_end(p,end)!=end)return -1;
 const char *result=canvas_child(p,end,"result",&re);
 const char *info=canvas_child(result,re,"canvas_info",&ie);
 const char *list=info?ccv_json_member_object(info,ie,"canvas_list",'[',&le):NULL;
 if(!list)return -1;
 int found=-1,modules=0;
 for(p=list+1;p<le;){
  p=ccv_json_skip_space(p,le);if(p>=le||*p==']')break;
  if(*p!='{')return -1;
  const char *me=ccv_json_container_end(p,le);if(!me)return -1;
  double connected;
  if(ccv_numeric(p,me,"connected",&connected)&&connected==1){
   if(++modules!=1)return -1;
   const char *te,*trays=ccv_json_member_object(p,me,"tray_list",'[',&te);if(!trays)return -1;
   for(const char *t=trays+1;t<te;){
    t=ccv_json_skip_space(t,te);if(t>=te||*t==']')break;
    if(*t!='{')return -1;
    const char *se=ccv_json_container_end(t,te);if(!se)return -1;
    double id,value;
    if(ccv_numeric(t,se,"tray_id",&id)&&id==slot){
     if(found!=-1||!ccv_numeric(t,se,"status",&value)||value<0||value>2||value!=floor(value))return -1;
     found=(int)value;
    }
    t=ccv_json_skip_space(se,te);if(t<te&&*t==',')t++;else if(t>=te||*t!=']')return -1;
   }
  }
  p=ccv_json_skip_space(me,le);if(p<le&&*p==',')p++;else if(p>=le||*p!=']')return -1;
 }
 return found;
}
/* Explicit native-load recovery is separate from ordinary Load/Unload.
 * A lost head/channel association is not an empty head or a completed load. */
static inline int canvas_recovery_load_allowed(const mqtt_client *m,int slot) {
 time_t now=time(NULL);
 if(!canvas_source_valid(m)||now-m->last_message>15||!m->have_machine_status||m->machine_status!=1||
    !m->canvas_direct_idle||m->canvas_direct_active!=-1||!m->canvas_head_loaded||
    canvas_reported_status(m,slot)!=1)return 0;
 /* An incomplete tray set or a reported loaded channel is another ambiguity. */
 for(int i=0;i<4;i++){int status=canvas_reported_status(m,i);if(status!=0&&status!=1)return 0;}
 return 1;
}
static inline canvas_slot_view canvas_slot_policy(const mqtt_client *m,int slot) {
 canvas_slot_view v={CV_UNKNOWN,0,0,CR_UNAVAILABLE,CR_UNAVAILABLE};
 if(!canvas_source_valid(m))return v;
 int status=canvas_reported_status(m,slot),active=m->canvas_direct_active,head=m->canvas_head_loaded;
 if(status<0||active<-1||active>3)return v;
 if((head&&active==-1)||(!head&&active!=-1)){
  /* Distinguish a head-presence report without a known Canvas channel. This
   * can occur after reboot with filament still in the head. It does not prove
   * which slot feeds the head (or rule out external filament). Keep both
   * commands blocked; the new reason only enables accurate recovery guidance. */
  v.state=CV_TRANSITION;v.reason_load=v.reason_unload=head?CR_UNASSIGNED_HEAD:CR_TRANSITION;return v;
 }
 if(status==0&&active!=slot)v.state=CV_EMPTY;
 else if(status==1&&active!=slot)v.state=CV_PRESENT;
 else if(status==2&&active==slot&&head)v.state=CV_LOADED;
 else {v.state=CV_TRANSITION;v.reason_load=v.reason_unload=CR_TRANSITION;return v;}
 if(!m->have_machine_status||m->machine_status!=1||!m->canvas_direct_idle){
  v.reason_load=v.reason_unload=CR_BUSY;return v;
 }
 if(v.state==CV_EMPTY){v.reason_load=v.reason_unload=CR_EMPTY;return v;}
 if(v.state==CV_LOADED){v.unload=1;v.reason_unload=CR_NONE;v.reason_load=CR_ALREADY_LOADED;return v;}
 v.reason_unload=CR_NOT_LOADED;
 if(active==-1&&!head){v.load=1;v.reason_load=CR_NONE;}
 else v.reason_load=CR_HEAD_OCCUPIED;
 return v;
}
#endif
