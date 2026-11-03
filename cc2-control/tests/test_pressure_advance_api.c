#define main cc2_main_original
#define console_start mock_console_start
#include "../src/main.c"
#undef main
#undef console_start
#include <assert.h>

static char sent[700];
int mock_console_start(console_state *s,const char *command,char *reason,size_t cap){
    (void)s;(void)reason;(void)cap;snprintf(sent,sizeof(sent),"%s",command);return 0;
}
static int action(mqtt_client *m,const char *text){
    int peers[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,peers));sent[0]=0;
    control_response(peers[0],NULL,m,text,strlen(text));
    char out[1024];ssize_t n=recv(peers[1],out,sizeof(out)-1,0);assert(n>0);out[n]=0;
    close(peers[0]);close(peers[1]);return atoi(strchr(out,' ')+1);
}
static void sample(const char *state,const char *pressure){
    char json[600];int n=snprintf(json,sizeof(json),
        "{\"id\":11,\"result\":{\"eventtime\":1,\"status\":{\"print_stats\":{\"state\":\"%s\",\"cavity_temperature_mode\":0},\"extruder\":{\"temperature\":25,\"target\":0,\"pressure_advance\":%s}}}}",state,pressure);
    assert(n>0&&uds_message(&telemetry,json,(size_t)n));
}
static void read_status(mqtt_client *m,char *out,size_t cap,int diagnostic){
    int peers[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,peers));
    if(diagnostic)uds_response(peers[0]);else printer_response(peers[0],m);
    ssize_t n=recv(peers[1],out,cap-1,0);assert(n>0);out[n]=0;
    close(peers[0]);close(peers[1]);
}
int main(void){
    mqtt_client m={0};m.connected=m.registered=m.have_machine_status=1;
    m.machine_status=1;m.last_message=time(NULL);
    uds_init(&telemetry);
    assert(action(&m,"tune:pressure-advance:0.05")==409&&!sent[0]);
    int peers[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,peers));telemetry.fd=peers[0];
    sample("standby","0.04237");
    char out[6000];read_status(&m,out,sizeof(out),0);
    assert(strstr(out,"\"pressure_advance\":0.042370"));
    assert(action(&m,"tune:pressure-advance:0.05")==202);
    assert(!strcmp(sent,"SET_PRESSURE_ADVANCE EXTRUDER=extruder ADVANCE=0.050000"));
    read_status(&m,out,sizeof(out),0);
    assert(strstr(out,"\"pressure_advance\":0.042370")); /* Acceptance is not readback. */
    sample("standby","0.05");read_status(&m,out,sizeof(out),0);
    assert(strstr(out,"\"pressure_advance\":0.050000"));
    assert(action(&m,"tune:pressure-advance:0")==202&&strstr(sent,"ADVANCE=0.000000"));
    assert(!strstr(sent,"SAVE_CONFIG")&&!strstr(sent,"SMOOTH_TIME"));
    assert(action(&m,"tune:pressure-advance:1")==202&&strstr(sent,"ADVANCE=1.000000"));
    const char *bad[]={"","-1","1.000001","nan","inf","1e-2","0.1;M112","0.1\nM104 S250","0.0000001","1.2.3"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++){
        char command[120];snprintf(command,sizeof(command),"tune:pressure-advance:%s",bad[i]);
        assert(action(&m,command)==409&&!sent[0]);
    }
    m.machine_status=2;strcpy(m.filename,"cube.gcode");strcpy(m.print_state,"printing");sample("printing","0.03");
    assert(action(&m,"tune:pressure-advance:0.04")==202);
    strcpy(m.print_state,"paused");sample("paused","0.03");
    assert(action(&m,"tune:pressure-advance:0.04")==202);
    sample("error","0.03");assert(action(&m,"tune:pressure-advance:0.04")==409&&!sent[0]);
    m.machine_status=1;sample("printing","0.03");
    assert(action(&m,"tune:pressure-advance:0.04")==409&&!sent[0]);
    sample("standby","0.03");m.last_message-=16;
    assert(action(&m,"tune:pressure-advance:0.04")==409&&!sent[0]);m.last_message=time(NULL);
    telemetry.last_rx.tv_sec-=10;
    assert(action(&m,"tune:pressure-advance:0.04")==409&&!sent[0]);
    uds_close(&telemetry);close(peers[1]);
    assert(!socketpair(AF_UNIX,SOCK_STREAM,0,peers));telemetry.fd=peers[0];sample("standby","null");
    assert(action(&m,"tune:pressure-advance:0.04")==409&&!sent[0]);
    read_status(&m,out,sizeof(out),0);assert(strstr(out,"\"pressure_advance\":null"));
    sample("standby","0.0325");read_status(&m,out,sizeof(out),1);
    assert(strstr(out,"\"pressure_advance\":0.032500")&&strstr(out,"\"cavity_temperature_mode\":0.000000"));
    assert(!strstr(out,"\"(null)\":"));
    uds_close(&telemetry);close(peers[1]);
    puts("PASS: Pressure Advance precision, real readback, runtime-only dispatch, Idle/printing/paused and stale/unknown/injection refusals");
    return 0;
}
