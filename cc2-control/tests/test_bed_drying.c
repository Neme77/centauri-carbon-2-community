#define main cc2_original_main
#include "../src/main.c"
#undef main
#include <assert.h>
static mqtt_client machine;
static console_state commands;
static long long clock_ms=1000;
static long long fake_clock(void){return clock_ms;}
static int heats,offs,leases,restores,support=1,fail=0,apply=1;
static char directory[256];
static void snapshot(double bed,double target){
    telemetry.values[U_BT]=bed;telemetry.values[U_BG]=target;telemetry.messages++;
    clock_gettime(CLOCK_MONOTONIC,&telemetry.last_rx);
    machine.last_message=time(NULL);
}
static int fake_rpc(const char *query,char **reply,size_t *length){
    const char *text="{\"id\":204,\"result\":{}}";
    if(strstr(query,"gcode/help"))text=support?"{\"id\":203,\"result\":{\"SET_HEATER_BED_IDLE_TIMEOUT\":\"test\",\"M140\":\"test\"}}":"{\"id\":203,\"result\":{\"M140\":\"test\"}}";
    else if(strstr(query,"M140 S70")){heats++;if(apply)snapshot(25,70);}
    else if(strstr(query,"M140 S0")){offs++;if(apply)snapshot(telemetry.values[U_BT],0);}
    else if(strstr(query,"TIMEOUT=90"))leases++;
    else if(strstr(query,"TIMEOUT=600"))restores++;
    else assert(0);
    if(fail)text="{\"id\":204,\"error\":{\"message\":\"fixture failure\"}}";
    *reply=strdup(text);assert(*reply);*length=strlen(text);return 0;
}
static const char *auth="POST /api/bed-drying/start HTTP/1.1\r\nX-CC2-LAN: fixture-confirmation\r\n\r\n";
static void start(const char *body,int expected){
    int pair[2];char response[2048],status[32];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    drying_start_response(pair[0],auth,body,strlen(body),&machine,&commands);
    close(pair[0]);ssize_t n=read(pair[1],response,sizeof(response)-1);assert(n>0);response[n]=0;close(pair[1]);
    snprintf(status,sizeof(status),"HTTP/1.1 %d",expected);assert(strstr(response,status));
}
static void tick(void){drying_tick(&machine);}
static void reset(void){
    plates_exchange_close(&drying.exchange);memset(&drying,0,sizeof(drying));drying.exchange.fd=-1;drying.reason="none";
    unlink(drying_journal);heats=offs=leases=restores=0;support=apply=1;fail=0;clock_ms=1000;
    machine.machine_status=1;machine.print_enabled=0;strcpy(telemetry.print_state,"standby");snapshot(25,0);
}
static void heat(void){start("70:1",200);tick();assert(drying.stage==DRY_ARM);tick();assert(leases==1);tick();assert(heats==1);assert(drying.stage==DRY_HEATING);}
static void finish(void){for(int i=0;i<5&&drying_active();i++){clock_ms+=1000;snapshot(70,0);tick();}}
int main(void){
    char temporary[]="/tmp/cc2-drying-XXXXXX",journal[300],config[300],autosave[300];assert(mkdtemp(temporary));
    snprintf(directory,sizeof(directory),"%s",temporary);snprintf(journal,sizeof(journal),"%s/session",directory);snprintf(config,sizeof(config),"%s/config",directory);snprintf(autosave,sizeof(autosave),"%s/autosave",directory);
    drying_journal=journal;drying_config=config;drying_autosave=autosave;drying_clock=fake_clock;
    FILE *f=fopen(config,"w");assert(f);fputs("[controller_fan board]\nidle_timeout=30\n[idle_timeout]\nheater_bed_idle_timeout=600\nheater_bed_idle_timeout_min=60\n",f);fclose(f);
    int timeout;assert(drying_configured_timeout(&timeout)&&timeout==600);
    assert(!pthread_mutex_init(&commands.lock,NULL));
    machine.connected=machine.registered=machine.have_machine_status=1;
    machine.have_extruder_temp=machine.have_bed_temp=machine.have_extruder_target=machine.have_bed_target=1;
    machine.extruder_temp=machine.bed_temp=25;strcpy(machine.password,"fixture-confirmation");
    uds_init(&telemetry);int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));telemetry.fd=pair[0];telemetry.ready=1;
    telemetry.have_print_state=1;telemetry.present=(1u<<U_ET)|(1u<<U_BT)|(1u<<U_BG)|(1u<<U_EG)|(1u<<U_LIVE_SPEED);telemetry.connections=1;
    telemetry.values[U_ET]=25;
    plates_uds=fake_rpc;experiments_storage_valid=1;
    reset();start("70:1",409);assert(!heats&&!leases);
    experiments.bed_drying=1;
    atomic_store(&upload_busy,1);start("70:1",409);atomic_store(&upload_busy,0);
    telemetry.values[U_ET]=70;start("70:1",409);telemetry.values[U_ET]=25;
    snapshot(70,0);start("70:1",409);snapshot(25,0);

    start("999999999999999999999999:1",400);start("70:0",400);start("70:721",400);start("91:1",400);start("70:1x",400);start("070:1",400);
    assert(!heats&&!leases);heat();
    tick();assert(drying.stage==DRY_HEATING&&drying.hold_until==0);
    clock_ms+=31000;snapshot(30,70);tick();assert(leases==2&&heats==1);
    snapshot(69,70);tick();assert(drying.stage==DRY_HOLDING);long long until=drying.hold_until;
    clock_ms=until-1;tick();assert(drying.stage==DRY_HOLDING);
    clock_ms=until;tick();assert(offs==1&&drying.stage==DRY_STOPPING);finish();
    assert(drying.stage==DRY_DONE&&restores==1&&access(journal,F_OK));
    reset();heat();drying_stop("manual",0);apply=0;tick();assert(offs==1&&drying_active());
    clock_ms+=1000;tick();assert(drying_active()&&restores==0);snapshot(25,0);tick();assert(!drying_active()&&restores==1);
    reset();heat();snapshot(25,55);tick();assert(drying_active()&&heats==1&&offs==0);clock_ms+=120000;tick();assert(leases==1&&offs==0);
    snapshot(25,0);finish();assert(!drying_active());
    reset();heat();machine.machine_status=2;strcpy(telemetry.print_state,"printing");tick();assert(offs==0&&leases==1);
    machine.machine_status=1;strcpy(telemetry.print_state,"complete");snapshot(70,70);tick();assert(offs==0);snapshot(70,0);finish();assert(!drying_active());
    reset();heat();machine.last_message-=60;tick();clock_ms+=100000;tick();assert(leases==1&&heats==1&&offs==0);snapshot(25,70);tick();assert(offs==1);finish();
    reset();support=0;start("70:1",200);tick();assert(!drying_active()&&!heats&&!leases&&drying.stage==DRY_INTERRUPTED);
    reset();heat();/* Simulate CC2 death: persisted marker never resumes heat. */
    plates_exchange_close(&drying.exchange);memset(&drying,0,sizeof(drying));drying.exchange.fd=-1;drying_load();
    assert(drying.stage==DRY_RECOVERY);tick();assert(heats==1&&leases==1&&offs==0);
    snapshot(25,0);tick();assert(!drying_active()&&heats==1&&restores==1);
    reset();heat();
    plates_exchange_close(&drying.exchange);memset(&drying,0,sizeof(drying));drying.exchange.fd=-1;drying_load();
    drying_stop("manual",0);tick();assert(offs==1);finish();assert(!drying_active()&&restores==1);
    reset();heat();telemetry.connections++;tick();assert(drying.stage==DRY_STOPPING);finish();
    assert(!drying_result("{\"id\":204,\"error\":{},\"result\":{}}",33,0));
    reset();assert(!symlink("missing",journal));start("70:1",503);assert(!heats&&!leases);unlink(journal);
    unlink(config);assert(!rmdir(directory));close(pair[0]);close(pair[1]);pthread_mutex_destroy(&commands.lock);
    puts("PASS bed heating: opt-in/bounds, native lease, timed hold, explicit zero readback, manual/foreign ownership, stale/reconnect, restart without resume and storage refusal");
    return 0;
}
