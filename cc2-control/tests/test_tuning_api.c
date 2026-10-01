#define main cc2_main_original
#define console_start mock_console_start
#include "../src/main.c"
#undef main
#undef console_start
#include <assert.h>
static char sent_script[700];
int mock_console_start(console_state *s,const char *command,char *reason,size_t cap){
 (void)s;(void)reason;(void)cap;snprintf(sent_script,sizeof(sent_script),"%s",command);return 0;
}
static int request_tune(mqtt_client *m,const char *action){
 int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
 sent_script[0]=0;control_response(pair[0],NULL,m,action,strlen(action));
 char response[700];ssize_t n=recv(pair[1],response,sizeof(response)-1,0);assert(n>0);response[n]=0;
 close(pair[0]);close(pair[1]);return atoi(strchr(response,' ')+1);
}
int main(void){
 mqtt_client m;memset(&m,0,sizeof(m));m.connected=m.registered=m.have_machine_status=1;
 m.machine_status=2;m.last_message=time(NULL);strcpy(m.filename,"cube.gcode");strcpy(m.print_state,"printing");
 uds_init(&telemetry);
 assert(request_tune(&m,"tune:speed:80")==409&&!sent_script[0]);
 int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));telemetry.fd=pair[0];
 const char *initial="{\"id\":11,\"result\":{\"eventtime\":1,\"status\":{\"gcode_move\":{\"speed_factor\":1,\"extrude_factor\":1}}}}";
 assert(uds_message(&telemetry,initial,strlen(initial)));
 assert(request_tune(&m,"tune:speed:80")==202&&!strcmp(sent_script,"M220 S80"));
 assert(request_tune(&m,"tune:flow:95")==202&&!strcmp(sent_script,"M221 S95"));
 assert(request_tune(&m,"tune:speed:80junk")==409&&!sent_script[0]);
 telemetry.last_rx.tv_sec-=10;
 assert(request_tune(&m,"tune:flow:100")==409&&!sent_script[0]);
 uds_close(&telemetry);close(pair[1]);puts("PASS tuning API readback gate and protected command dispatch");return 0;
}
