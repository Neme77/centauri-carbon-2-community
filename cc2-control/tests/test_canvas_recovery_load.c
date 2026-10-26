#include "../src/canvas_state.h"
#include "../src/control.h"
#include <assert.h>
#include <stdio.h>
static void trays(mqtt_client *m,int first){
 int n=snprintf(m->canvas_snapshot,sizeof(m->canvas_snapshot),"{\"result\":{\"canvas_info\":{\"canvas_list\":[{\"connected\":1,\"tray_list\":[{\"tray_id\":0,\"status\":%d},{\"tray_id\":1,\"status\":1},{\"tray_id\":2,\"status\":1},{\"tray_id\":3,\"status\":1}]}]}}}",first);
 assert(n>0&&(size_t)n<sizeof(m->canvas_snapshot));m->canvas_snapshot_len=(size_t)n;
}
static int action(mqtt_client *m,const char *a){char script[700],reason[256];return control_build_script(a,m,script,sizeof(script),reason,sizeof(reason));}
int main(void){
 mqtt_client m={0};m.connected=m.registered=m.have_machine_status=m.canvas_direct_known=m.canvas_direct_idle=m.canvas_head_loaded=1;
 m.machine_status=1;m.canvas_direct_active=-1;m.last_message=time(NULL);trays(&m,1);
 for(int i=0;i<4;i++){
  char a[64],expected[64],script[700],reason[256];snprintf(a,sizeof(a),"canvas:recover-load:%d",i);snprintf(expected,sizeof(expected),"CANVAS_LOAD_FILAMENT CHANNEL=%d",i);
  assert(canvas_recovery_load_allowed(&m,i));assert(control_build_script(a,&m,script,sizeof(script),reason,sizeof(reason)));assert(!strcmp(script,expected));
  canvas_slot_view v=canvas_slot_policy(&m,i);assert(v.state==CV_TRANSITION&&!v.load&&!v.unload&&v.reason_load==CR_UNASSIGNED_HEAD);
 }
 assert(!action(&m,"canvas:load:1")&&!action(&m,"canvas:unload:1"));
 const char *invalid[]={"canvas:recover-load:","canvas:recover-load:-1","canvas:recover-load:4","canvas:recover-load:1junk","canvas:recover-load:1:2","canvas:recover-load:4294967297","canvas:recover-load:01"};
 for(size_t i=0;i<sizeof(invalid)/sizeof(*invalid);i++)assert(!action(&m,invalid[i]));
 trays(&m,0);assert(!action(&m,"canvas:recover-load:0"));assert(action(&m,"canvas:recover-load:1"));
 trays(&m,2);assert(!action(&m,"canvas:recover-load:1"));trays(&m,1);
 m.connected=0;assert(!action(&m,"canvas:recover-load:1"));m.connected=1;
 m.registered=0;assert(!action(&m,"canvas:recover-load:1"));m.registered=1;
 m.canvas_direct_known=0;assert(!action(&m,"canvas:recover-load:1"));m.canvas_direct_known=1;
 m.have_machine_status=0;assert(!action(&m,"canvas:recover-load:1"));m.have_machine_status=1;
 const int busy[]={0,2,3,13};for(size_t i=0;i<sizeof(busy)/sizeof(*busy);i++){m.machine_status=busy[i];assert(!action(&m,"canvas:recover-load:1"));}m.machine_status=1;
 m.canvas_direct_idle=0;assert(!action(&m,"canvas:recover-load:1"));m.canvas_direct_idle=1;
 m.canvas_head_loaded=0;assert(!action(&m,"canvas:recover-load:1"));m.canvas_head_loaded=1;
 m.canvas_direct_active=0;assert(!action(&m,"canvas:recover-load:1"));m.canvas_direct_active=-1;
 m.last_message=time(NULL)-20;assert(!action(&m,"canvas:recover-load:1"));m.last_message=time(NULL)+60;assert(!action(&m,"canvas:recover-load:1"));m.last_message=time(NULL);
 m.canvas_snapshot_len=0;assert(!action(&m,"canvas:recover-load:1"));trays(&m,1);
 m.canvas_snapshot_len--;assert(!action(&m,"canvas:recover-load:1"));trays(&m,1);
 char *entry=strstr(m.canvas_snapshot,"\"tray_id\":3");assert(entry);entry[10]='2';
 assert(!action(&m,"canvas:recover-load:1"));trays(&m,1);
 entry=strstr(m.canvas_snapshot,"\"tray_id\":3");assert(entry);entry[10]='4';
 assert(!action(&m,"canvas:recover-load:1"));
 puts("PASS: explicit native recovery accepts every present slot, keeps ordinary transition guards and refuses unsafe/missing states");
 return 0;
}
