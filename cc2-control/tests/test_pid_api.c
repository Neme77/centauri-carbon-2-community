#define main cc2_main_original
#define console_start mock_console_start
#include "../src/main.c"
#undef main
#undef console_start
#include <assert.h>
static char sent[700];
int mock_console_start(console_state *state,const char *script,char *reason,size_t cap){(void)state;(void)reason;(void)cap;snprintf(sent,sizeof(sent),"%s",script);return 0;}
static int act(console_state *c,mqtt_client *m,const char *action){int p[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,p));sent[0]=0;control_response(p[0],c,m,action,strlen(action));char b[1024];int n=read(p[1],b,sizeof(b)-1);assert(n>0);b[n]=0;close(p[0]);close(p[1]);return atoi(strchr(b,' ')+1);}
int main(void){
 console_state c;console_init(&c,"/tmp/unused");mqtt_client m;memset(&m,0,sizeof(m));m.connected=m.registered=m.have_machine_status=1;m.machine_status=1;m.last_message=time(NULL);
 assert(act(&c,&m,"pid:extruder:200")==202&&!strcmp(sent,"PID_CALIBRATE HEATER=extruder TARGET=200.0\nTURN_OFF_HEATERS"));
 assert(act(&c,&m,"pid:heater_bed:60")==202);
 const char *bad[]={"pid:extruder:nan","pid:extruder:301","pid:heater_bed:121","pid:heater_bed:0","pid:unknown:60","pid:extruder:200\nM112","pid:save:foo"};
 for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(act(&c,&m,bad[i])==409&&!sent[0]);
 m.machine_status=2;assert(act(&c,&m,"pid:extruder:200")==409);m.machine_status=3;assert(act(&c,&m,"pid:heater_bed:60")==409);m.machine_status=1;
 m.last_message-=30;assert(act(&c,&m,"pid:extruder:200")==409);m.last_message=time(NULL);
 assert(act(&c,&m,"pid:save")==409&&!sent[0]);
 strcpy(c.command,"PID_CALIBRATE HEATER=extruder TARGET=200.0\nTURN_OFF_HEATERS");c.completed=c.success=1;
 strcpy(c.output,"PID parameters: pid_Kp=22.1 pid_Ki=1.2 pid_Kd=103.4\n{\"feedback\": {\"command\": \"pid_calibrate\", \"result\": \"completed\"}}\n");assert(pid_ready_locked(&c));assert(act(&c,&m,"pid:save")==202&&!strcmp(sent,"SAVE_CONFIG"));
 int peers[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,peers));pid_status_response(peers[0],&c);char reply[1024];int n=read(peers[1],reply,sizeof(reply)-1);assert(n>0);reply[n]=0;close(peers[0]);close(peers[1]);assert(strstr(reply,"\"ready\":true")&&strstr(reply,"\"kp\":22.1")&&strstr(reply,"\"heater\":\"extruder\""));
 c.busy=1;assert(act(&c,&m,"pid:save")==409);c.busy=0;c.success=0;assert(act(&c,&m,"pid:save")==409);c.success=1;
 strcpy(c.output,"{\"feedback\":{\"command\":\"pid_calibrate\",\"result\":\"failed\"}}\n");assert(!pid_ready_locked(&c));assert(act(&c,&m,"pid:save")==409);
 assert(!pid_completed_report("{\"command\":\"other\",\"result\":\"completed\"}\n"));
 assert(!pid_completed_report("\"command\""));assert(!pid_completed_report("\"command\":\"pid_calibrate\",\"result\""));
 console_destroy(&c);puts("PASS PID heater limits, strict requests, fresh idle guard, completion and save gate");return 0;
}
