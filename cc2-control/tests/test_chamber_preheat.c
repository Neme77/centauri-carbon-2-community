#define main cc2_original_main
#include "../src/main.c"
#undef main
#include <assert.h>
static mqtt_client machine;
static console_state commands;
static long long clock_ms=1000;
static long long fake_clock(void){return clock_ms;}
static int heats,offs,leases,arms,restores,support=1,last_fan=-1,fans;
static char last_restore[400];
static void snapshot(double bed,double target){
    telemetry.values[U_BT]=bed;telemetry.values[U_BG]=target;telemetry.messages++;
    clock_gettime(CLOCK_MONOTONIC,&telemetry.last_rx);
    machine.last_message=time(NULL);
}
static void chamber(double value){machine.chamber_temp=value;snapshot(telemetry.values[U_BT],telemetry.values[U_BG]);}
static int fake_rpc(const char *query,char **reply,size_t *length){
    const char *text="{\"id\":204,\"result\":{}}";
    if(strstr(query,"gcode/help"))text=support?
        "{\"id\":203,\"result\":{\"SET_HEATER_BED_IDLE_TIMEOUT\":\"t\",\"SET_CAVITY_TEMPERATURE_MODE\":\"m\"}}":
        "{\"id\":203,\"result\":{\"SET_HEATER_BED_IDLE_TIMEOUT\":\"t\"}}";
    else if(strstr(query,"MODE=1\\nM106 P2 S51")&&strstr(query,"TIMEOUT=90"))arms++;
    else if(strstr(query,"TIMEOUT=600")){restores++;snprintf(last_restore,sizeof(last_restore),"%s",query);}
    else if(strstr(query,"TIMEOUT=90"))leases++;
    else if(strstr(query,"M140 S100")){heats++;snapshot(telemetry.values[U_BT],100);}
    else if(strstr(query,"M140 S0")){offs++;snapshot(telemetry.values[U_BT],0);}
    else if(strstr(query,"M106 P2 S")){fans++;last_fan=atoi(strstr(query,"M106 P2 S")+9);}
    else assert(0);
    *reply=strdup(text);assert(*reply);*length=strlen(text);return 0;
}
static const char *auth="POST /api/bed-drying/preheat HTTP/1.1\r\nX-CC2-LAN: fixture-confirmation\r\n\r\n";
static char response[2048];
static void start(const char *body,int expected){
    int pair[2];char status[32];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    drying_preheat_response(pair[0],auth,body,strlen(body),&machine,&commands);
    close(pair[0]);ssize_t n=read(pair[1],response,sizeof(response)-1);assert(n>0);response[n]=0;close(pair[1]);
    snprintf(status,sizeof(status),"HTTP/1.1 %d",expected);
    if(!strstr(response,status)){fprintf(stderr,"expected %d for %s:\n%s\n",expected,body,response);assert(0);}
}
static const char *status(void){static char body[DRY_STATUS_MAX];assert(drying_status_json(body,sizeof(body),&machine)>0);return body;}
static void tick(void){drying_tick(&machine);}
/* Two ticks: a lease renewal due at the same moment takes the first. */
static void later(long long ms){clock_ms+=ms;chamber(machine.chamber_temp);tick();tick();}
static void reset(void){
    plates_exchange_close(&drying.exchange);memset(&drying,0,sizeof(drying));drying.exchange.fd=-1;drying.reason="none";drying.fan_pending=-1;
    unlink(drying_journal);heats=offs=leases=arms=restores=fans=0;last_fan=-1;support=1;clock_ms=1000;last_restore[0]=0;
    machine.machine_status=1;machine.print_enabled=0;strcpy(telemetry.print_state,"standby");
    machine.have_chamber_temp=1;machine.chamber_temp=30;telemetry.values[U_CAVITY_MODE]=0;telemetry.values[U_AF]=0;snapshot(25,0);
}
/* Start, check the firmware, close the grille with the fan at 20 %, heat the bed: heating. */
static void heat(void){
    start("100:45:30",200);tick();assert(drying.stage==DRY_ARM);tick();assert(arms==1&&drying.stage==DRY_HEAT);
    tick();assert(heats==1&&drying.stage==DRY_HEATING&&drying.fan_sent==51);
}
static void finish(void){for(int i=0;i<5&&drying_active();i++)later(1000);}
int main(void){
    char temporary[]="/tmp/cc2-preheat-XXXXXX",journal[300],config[300],autosave[300];assert(mkdtemp(temporary));
    snprintf(journal,sizeof(journal),"%s/session",temporary);snprintf(config,sizeof(config),"%s/config",temporary);snprintf(autosave,sizeof(autosave),"%s/autosave",temporary);
    drying_journal=journal;drying_config=config;drying_autosave=autosave;drying_clock=fake_clock;
    FILE *f=fopen(config,"w");assert(f);fputs("[heater_bed]\nmin_temp=-100\nmax_temp=125\n[idle_timeout]\nheater_bed_idle_timeout=600\n",f);fclose(f);
    assert(drying_bed_limit()==125);
    assert(!pthread_mutex_init(&commands.lock,NULL));
    machine.connected=machine.registered=machine.have_machine_status=1;
    machine.have_extruder_temp=machine.have_bed_temp=machine.have_extruder_target=machine.have_bed_target=1;
    machine.extruder_temp=machine.bed_temp=25;strcpy(machine.password,"fixture-confirmation");
    uds_init(&telemetry);int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));telemetry.fd=pair[0];telemetry.ready=1;
    telemetry.have_print_state=1;telemetry.connections=1;
    telemetry.present=(1u<<U_ET)|(1u<<U_BT)|(1u<<U_BG)|(1u<<U_EG)|(1u<<U_LIVE_SPEED)|(1u<<U_AF)|(1u<<U_CAVITY_MODE);
    telemetry.values[U_ET]=25;
    plates_uds=fake_rpc;experiments_storage_valid=1;

    /* Opt-in, bounds (bed capped at 110 below the printer's 125), canonical text. */
    reset();start("100:45:30",409);experiments.bed_drying=1;
    start("100:45",400);start("59:45:30",400);start("101:45:30",400);start("100:29:30",400);start("100:56:30",400);
    start("100:45:0",400);start("100:45:241",400);start("0100:45:30",400);start("100:45:30x",400);
    assert(strstr(response,"60-100 C")||strstr(response,"Invalid"));
    /* Refused without grille/fan/chamber readings, at the target already, or with a heater on. */
    telemetry.present&=~(1u<<U_CAVITY_MODE);start("100:45:30",409);telemetry.present|=1u<<U_CAVITY_MODE;
    machine.have_chamber_temp=0;start("100:45:30",409);machine.have_chamber_temp=1;
    chamber(45);start("100:45:30",409);chamber(30);
    snapshot(25,60);start("100:45:30",409);snapshot(25,0);
    assert(!heats&&!arms&&!leases);
    /* A warm bed left by a print is fine for preheat (both telemetry sources see it). */
    machine.bed_temp=70;snapshot(70,0);heat();snapshot(70,100);machine.bed_temp=25;
    { char text[80];int fd=open(journal,O_RDONLY);assert(fd>=0);ssize_t n=read(fd,text,sizeof(text)-1);close(fd);assert(n>0);text[n]=0;
      assert(!strcmp(text,"CC2DRY2 600 100 0 0\n")); }
    assert(strstr(status(),"\"mode\":\"preheat\"")&&strstr(status(),"\"chamber_target\":45")&&strstr(status(),"\"ready\":false"));
    drying_stop("manual",0);finish();assert(!drying_active());
    /* Straight after a completed or cancelled print the vendor still reports print.enable: the fresh
     * native state decides, so a warm-bed preheat starts ... */
    reset();machine.print_enabled=1;strcpy(telemetry.print_state,"complete");machine.bed_temp=70;snapshot(70,0);heat();
    drying_stop("manual",0);snapshot(70,0);finish();assert(!drying_active()&&restores==1);
    reset();machine.print_enabled=1;strcpy(telemetry.print_state,"cancelled");heat();drying_stop("manual",0);finish();assert(!drying_active());
    /* ... but printing, paused, a busy machine status, and stale or disconnected telemetry are refused. */
    reset();machine.print_enabled=1;machine.bed_temp=70;snapshot(70,0);
    strcpy(telemetry.print_state,"printing");start("100:45:30",409);
    strcpy(telemetry.print_state,"paused");start("100:45:30",409);
    strcpy(telemetry.print_state,"complete");machine.machine_status=2;start("100:45:30",409);machine.machine_status=1;
    telemetry.last_rx.tv_sec-=10;start("100:45:30",409);snapshot(70,0);
    machine.last_message-=60;start("100:45:30",409);snapshot(70,0);
    machine.connected=0;start("100:45:30",409);machine.connected=1;
    telemetry.values[U_LIVE_SPEED]=50;start("100:45:30",409);telemetry.values[U_LIVE_SPEED]=0;
    assert(!drying_active()&&!heats);machine.bed_temp=25;
    reset();machine.bed_temp=70;snapshot(70,0);heat();snapshot(70,100);machine.bed_temp=25;

    /* The fan stays at 20 % while the chamber is 5 C or more short, then tapers; leases continue. */
    chamber(38);later(16000);assert(fans==0);
    chamber(42);later(16000);assert(fans==1&&last_fan==31&&drying.fan_sent==31);
    later(16000);assert(fans==1); /* unchanged: nothing sent */
    later(16000);assert(leases>=1);
    /* Ready only after two minutes within 1 C (the sensor's step) of the target; a dip restarts the wait. */
    chamber(44);later(1000);assert(drying.stage==DRY_HEATING&&drying.ready_since);
    chamber(43);later(1000);assert(!drying.ready_since);
    chamber(44);later(1000);later(119000);assert(drying.stage==DRY_HEATING);
    later(1000);assert(drying.stage==DRY_HOLDING&&drying.ready&&strstr(status(),"\"ready\":true")&&strstr(status(),"\"state\":\"holding\""));
    assert(strstr(status(),"\"levelled\":false"));
    chamber(45.2);later(16000);assert(last_fan==0);
    /* The hold ends: bed off, then grille, fan and idle timeout back as they were. */
    clock_ms=drying.hold_until;tick();assert(offs==1&&drying.stage==DRY_STOPPING);finish();
    assert(drying.stage==DRY_DONE&&restores==1&&access(journal,F_OK));
    assert(strstr(last_restore,"SET_HEATER_BED_IDLE_TIMEOUT TIMEOUT=600\\nSET_CAVITY_TEMPERATURE_MODE MODE=0\\nM106 P2 S0"));

    /* A grille already closed and the fan at 30 % before the start are what is restored. */
    reset();telemetry.values[U_CAVITY_MODE]=1;telemetry.values[U_AF]=0.3;heat();snapshot(25,100);
    drying_stop("manual",0);tick();assert(offs==1);finish();
    assert(!drying_active()&&strstr(last_restore,"MODE=1\\nM106 P2 S77"));

    /* A print starting is the hand-off: its heating is left alone and restoration waits for idle. */
    reset();heat();snapshot(25,100);chamber(40);
    machine.machine_status=2;strcpy(telemetry.print_state,"printing");snapshot(25,100);tick();
    assert(!strcmp(drying.reason,"print")&&strstr(status(),"\"state\":\"handed_off\"")&&offs==0);
    later(600000);assert(offs==0&&restores==0&&!strcmp(drying.reason,"print"));
    machine.machine_status=1;machine.print_enabled=1;strcpy(telemetry.print_state,"complete");snapshot(60,0);finish();
    assert(!drying_active()&&drying.stage==DRY_DONE&&restores==1&&offs==0&&strstr(status(),"\"state\":\"completed\""));

    /* A chamber that levels off short of the target is predicted while it still rises ... */
    reset();heat();snapshot(25,100);
    for(int i=0;i<21;i++){chamber(40-10*exp(-i/6.0));later(60000);}
    { const char *p=strstr(status(),"\"plateau\":");assert(p);double plateau=atof(p+10);assert(plateau>39&&plateau<41); }
    assert(drying.stage==DRY_HEATING);
    /* ... and once it has risen less than 1 C in 20 minutes the hold starts there, marked levelled. */
    for(int i=0;i<10;i++){chamber(40);later(60000);}
    assert(drying.stage==DRY_HEATING);
    for(int i=0;i<10&&drying.stage==DRY_HEATING;i++){chamber(40);later(60000);}
    assert(drying.stage==DRY_HOLDING&&drying.ready&&drying.levelled&&strstr(status(),"\"levelled\":true"));
    drying_stop("manual",0);finish();
    /* A chamber still creeping up (1 C per 15 minutes) runs to the two-hour limit. */
    reset();heat();snapshot(25,100);
    for(int i=0;i<119;i++){chamber(30+i/15);later(60000);}
    assert(drying.stage==DRY_HEATING);
    clock_ms=drying.since+120LL*60000;chamber(38);tick();assert(drying.stage==DRY_STOPPING&&!strcmp(drying.reason,"heat_timeout"));finish();

    /* Losing the chamber reading stops it. */
    reset();heat();snapshot(25,100);machine.have_chamber_temp=0;later(1000);assert(!strcmp(drying.reason,"chamber"));
    machine.have_chamber_temp=1;finish();assert(!drying_active()&&restores==1);

    /* CC2 restart: the journal never resumes heat but restores the recorded grille and fan. */
    reset();telemetry.values[U_CAVITY_MODE]=1;telemetry.values[U_AF]=0.3;heat();snapshot(25,100);
    plates_exchange_close(&drying.exchange);memset(&drying,0,sizeof(drying));drying.exchange.fd=-1;drying_load();
    assert(drying.stage==DRY_RECOVERY&&drying.preheat&&drying.previous_grille==1&&drying.previous_fan==77);
    tick();assert(restores==0&&heats==1);snapshot(25,0);tick();
    assert(!drying_active()&&restores==1&&strstr(last_restore,"MODE=1\\nM106 P2 S77"));
    { int fd=open(journal,O_WRONLY|O_CREAT|O_EXCL,0600);assert(fd>=0);assert(write(fd,"CC2DRY2 600 111 0 0\n",20)==20);close(fd);
      memset(&drying,0,sizeof(drying));drying.exchange.fd=-1;drying_load();assert(!strcmp(drying.reason,"storage"));unlink(journal); }

    /* Firmware without the grille commands: nothing heats. */
    reset();support=0;start("100:45:30",200);tick();assert(!drying_active()&&!heats&&!arms&&drying.stage==DRY_INTERRUPTED);

    /* A printer whose bed limit is under 100 caps preheat there. */
    { char low[300];snprintf(low,sizeof(low),"%s/low",temporary);f=fopen(low,"w");assert(f);fputs("[heater_bed]\nmax_temp=90\n",f);fclose(f);
      drying_config=low;reset();start("100:45:30",400);assert(strstr(response,"60-90 C"));drying_config=config;unlink(low); }
    unlink(config);unlink(autosave);assert(!rmdir(temporary));close(pair[0]);close(pair[1]);pthread_mutex_destroy(&commands.lock);
    puts("PASS chamber preheat: opt-in/bounds, start after a completed/cancelled print (print.enable retained), printing/paused/stale refused, grille/fan/chamber readings, fan taper, two-minute ready, levelled-off hold, hold, restore of grille/fan/timeout, print hand-off, plateau, heat limit, lost chamber, restart without resume, unsupported firmware");
    return 0;
}
