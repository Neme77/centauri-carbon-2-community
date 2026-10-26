#include "../src/canvas_state.h"
#include "../src/control.h"
#include <assert.h>
#include <stdio.h>
static void snapshot(mqtt_client *m,const char *trays){
 int n=snprintf(m->canvas_snapshot,sizeof(m->canvas_snapshot),"{\"result\":{\"canvas_info\":{\"canvas_list\":[{\"connected\":1,\"tray_list\":%s}]}}}",trays);
 assert(n>0&&(size_t)n<sizeof(m->canvas_snapshot));m->canvas_snapshot_len=(size_t)n;
}
int main(void){
 mqtt_client m={0};m.connected=m.registered=m.have_machine_status=1;m.machine_status=1;m.last_message=time(NULL);
 m.canvas_direct_known=m.canvas_direct_idle=1;m.canvas_direct_active=-1;
 snapshot(&m,"[{\"tray_id\":0,\"status\":0},{\"tray_id\":1,\"status\":1},{\"tray_id\":2,\"status\":1},{\"tray_id\":3,\"status\":0}]");
 canvas_slot_view v=canvas_slot_policy(&m,0);assert(v.state==CV_EMPTY&&!v.load&&!v.unload);
 v=canvas_slot_policy(&m,1);assert(v.state==CV_PRESENT&&v.load&&!v.unload);
 char command[700],reason[256];
 assert(control_build_script("canvas:load:1",&m,command,sizeof(command),reason,sizeof(reason))&&!strcmp(command,"CANVAS_LOAD_FILAMENT CHANNEL=1"));
 assert(!control_build_script("canvas:unload:1",&m,command,sizeof(command),reason,sizeof(reason)));
 assert(!control_build_script("canvas:load:0",&m,command,sizeof(command),reason,sizeof(reason)));
 assert(!control_build_script("canvas:load:1junk",&m,command,sizeof(command),reason,sizeof(reason)));
 m.canvas_direct_active=1;m.canvas_head_loaded=1;
 snapshot(&m,"[{\"tray_id\":1,\"status\":2},{\"tray_id\":2,\"status\":1}]");
 v=canvas_slot_policy(&m,1);assert(v.state==CV_LOADED&&!v.load&&v.unload);
 assert(control_build_script("canvas:unload:1",&m,command,sizeof(command),reason,sizeof(reason))&&!strcmp(command,"CANVAS_UNLOAD_FILAMENT CHANNEL=1"));
 assert(!control_build_script("canvas:unload:2",&m,command,sizeof(command),reason,sizeof(reason)));
 assert(!control_build_script("canvas:load:2",&m,command,sizeof(command),reason,sizeof(reason)));
 m.canvas_head_loaded=0;v=canvas_slot_policy(&m,1);assert(v.state==CV_TRANSITION&&!v.load&&!v.unload);
 m.canvas_direct_active=-1;
 snapshot(&m,"[{\"tray_id\":1,\"status\":1}]");
 m.canvas_head_loaded=1;
 v=canvas_slot_policy(&m,1);
 assert(v.state==CV_TRANSITION&&!v.load&&!v.unload);
 assert(v.reason_load==CR_UNASSIGNED_HEAD&&v.reason_unload==CR_UNASSIGNED_HEAD);
 assert(!strcmp(canvas_reason_name(v.reason_load),"unassigned_head"));
 assert(!control_build_script("canvas:load:1",&m,command,sizeof(command),reason,sizeof(reason)));
 assert(!control_build_script("canvas:unload:1",&m,command,sizeof(command),reason,sizeof(reason)));
 /* Native recovery must restore a real channel/status; remembering a slot
  * or selecting its UI card does not provide that confirmation. */
 m.canvas_direct_active=1;snapshot(&m,"[{\"tray_id\":1,\"status\":2}]");
 v=canvas_slot_policy(&m,1);assert(v.state==CV_LOADED&&!v.load&&v.unload&&v.reason_unload==CR_NONE);
 m.canvas_direct_active=-1;snapshot(&m,"[{\"tray_id\":1,\"status\":1}]");
 m.canvas_head_loaded=0;m.machine_status=2;assert(!canvas_slot_policy(&m,1).load);
 m.machine_status=1;m.canvas_direct_idle=0;assert(!canvas_slot_policy(&m,1).load);
 m.canvas_direct_idle=1;m.last_message-=60;assert(canvas_slot_policy(&m,1).state==CV_UNKNOWN);
 m.last_message=time(NULL);m.canvas_direct_known=0;assert(!canvas_slot_policy(&m,1).load);
 m.canvas_direct_known=1;m.connected=0;assert(!canvas_slot_policy(&m,1).load);
 m.connected=1;m.registered=0;assert(!canvas_slot_policy(&m,1).load);
 m.registered=1;
 snapshot(&m,"[{\"tray_id\":1,\"status\":1},{\"tray_id\":1,\"status\":0}]");assert(canvas_slot_policy(&m,1).state==CV_UNKNOWN);
 snapshot(&m,"[{\"tray_id\":2,\"status\":1}]");assert(canvas_slot_policy(&m,1).state==CV_UNKNOWN);
 snapshot(&m,"[{\"tray_id\":1,\"status\":99}]");assert(canvas_slot_policy(&m,1).state==CV_UNKNOWN);
 snapshot(&m,"[{\"tray_id\":1,\"status\":1,\"filament_name\":\"status: 0\"}]");assert(canvas_slot_policy(&m,1).load);
 puts("PASS: Canvas empty/present/loaded/transitional slots; exact load/unload actions; head, busy, stale, missing and inconsistent guards");return 0;
}
