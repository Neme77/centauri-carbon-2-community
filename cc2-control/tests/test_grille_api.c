#define main cc2_main_original
#define console_start mock_console_start
#include "../src/main.c"
#undef main
#undef console_start
#include <assert.h>

static char sent[700];
static int queue_busy;
int mock_console_start(console_state *s,const char *command,char *reason,size_t cap){
    (void)s;
    if(queue_busy){snprintf(reason,cap,"Another command is running");return -1;}
    snprintf(sent,sizeof(sent),"%s",command);return 0;
}
static int action(mqtt_client *m,const char *text){
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    sent[0]=0;control_response(pair[0],NULL,m,text,strlen(text));
    char response[1024];ssize_t n=recv(pair[1],response,sizeof(response)-1,0);assert(n>0);response[n]=0;
    close(pair[0]);close(pair[1]);return atoi(strchr(response,' ')+1);
}
static void native_state(const char *state,double nozzle,double bed){
    char json[400];int n=snprintf(json,sizeof(json),
        "{\"id\":11,\"result\":{\"eventtime\":1,\"status\":{\"print_stats\":{\"state\":\"%s\"},\"extruder\":{\"target\":%g},\"heater_bed\":{\"target\":%g}}}}",state,nozzle,bed);
    assert(n>0&&uds_message(&telemetry,json,(size_t)n));
}
int main(void){
    mqtt_client m;memset(&m,0,sizeof(m));m.connected=m.registered=m.have_machine_status=1;
    m.machine_status=1;m.last_message=time(NULL);
    uds_init(&telemetry);
    assert(action(&m,"grille:open")==409&&!sent[0]);
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));telemetry.fd=pair[0];
    native_state("standby",0,0);
    /* The flaps do not require XYZ homing or a cold nozzle; no motion/heater commands are sent. */
    assert(action(&m,"grille:open")==202&&!strcmp(sent,"EXHAUST_GRILLE_OPEN"));
    assert(action(&m,"grille:close")==202&&!strcmp(sent,"EXHAUST_GRILLE_CLOSE"));
    native_state("complete",0,0);m.print_enabled=1;
    assert(action(&m,"grille:open")==202); /* Retained completed-print flag is not active ownership. */
    const char *bad[]={"grille:toggle","grille:open junk","grille:close\nM112","grille:"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(action(&m,bad[i])==409&&!sent[0]);
    const char *busy[]={"printing","paused","error","cancelled"};
    for(size_t i=0;i<sizeof(busy)/sizeof(busy[0]);i++){
        native_state(busy[i],0,0);assert(action(&m,"grille:open")==409&&!sent[0]);
    }
    native_state("standby",180,0);assert(action(&m,"grille:open")==409&&!sent[0]);
    native_state("standby",0,50);assert(action(&m,"grille:close")==409&&!sent[0]);
    native_state("standby",0,0);telemetry.last_rx.tv_sec-=10;
    assert(action(&m,"grille:open")==409&&!sent[0]);native_state("standby",0,0);
    m.machine_status=2;assert(action(&m,"grille:open")==409&&!sent[0]);m.machine_status=1;
    m.last_message=time(NULL)-16;assert(action(&m,"grille:open")==409&&!sent[0]);
    m.last_message=time(NULL)+60;assert(action(&m,"grille:open")==409&&!sent[0]);
    m.last_message=time(NULL);m.registered=0;assert(action(&m,"grille:open")==409&&!sent[0]);m.registered=1;
    drying.stage=DRY_HOLDING;assert(action(&m,"grille:open")==409&&!sent[0]);drying.stage=DRY_OFF;
    queue_busy=1;assert(action(&m,"grille:open")==409&&!sent[0]);queue_busy=0;
    char script[2],reason[120];assert(!control_build_script("grille:open",&m,script,sizeof(script),reason,sizeof(reason)));
    uds_close(&telemetry);close(pair[1]);
    puts("PASS: exhaust grille native dispatch, strict actions, stale/active/heating guards, retained completion and queue refusal");return 0;
}
