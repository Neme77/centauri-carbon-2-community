#define main cc2_main_original
#define console_emergency_stop tools_test_emergency_stop
#include "../src/main.c"
#undef main
#undef console_emergency_stop
#include <assert.h>

static mqtt_client model;
static console_state console;
static int server,telemetry_peer;
static int emergency_calls;
int tools_test_emergency_stop(console_state *c,char *reason,size_t cap){(void)c;(void)reason;(void)cap;emergency_calls++;return 0;}
static char http_reply[1024];
static int http_action_origin(const char *path,const char *body,const char *origin){
    /* HTTP origin validation needs a real AF_INET local address. Keep Unix
     * socket pairs only for native firmware transport/direct-response fixtures. */
    int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);
    struct sockaddr_in addr;memset(&addr,0,sizeof(addr));addr.sin_family=AF_INET;addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(!bind(listener,(struct sockaddr *)&addr,sizeof(addr))&&!listen(listener,1));
    socklen_t size=sizeof(addr);assert(!getsockname(listener,(struct sockaddr *)&addr,&size));
    service_http_port=ntohs(addr.sin_port);
    int pair[2];pair[1]=socket(AF_INET,SOCK_STREAM,0);assert(pair[1]>=0);
    assert(!connect(pair[1],(struct sockaddr *)&addr,sizeof(addr)));
    pair[0]=accept(listener,NULL,NULL);assert(pair[0]>=0);close(listener);
    char request[1024],expected_origin[80];snprintf(expected_origin,sizeof(expected_origin),"http://localhost:%d",service_http_port);
    int n=snprintf(request,sizeof(request),"POST %s HTTP/1.1\r\nHost: localhost:%d\r\nOrigin: %s\r\nX-CC2-Request: 1\r\nContent-Length: %zu\r\n\r\n%s",path,service_http_port,origin?origin:expected_origin,strlen(body),body);
    handle_client(pair[0],request,(size_t)n,"none",&model,&console);close(pair[0]);
    ssize_t got=read(pair[1],http_reply,sizeof(http_reply)-1);assert(got>0);http_reply[got]=0;close(pair[1]);
    int status=0;assert(sscanf(http_reply,"HTTP/1.1 %d",&status)==1);return status;
}
static int http_action(const char *path,const char *body){return http_action_origin(path,body,NULL);}
static void put(const char *path,const char *text){FILE *f=fopen(path,"wb");assert(f);assert(fwrite(text,1,strlen(text),f)==strlen(text));assert(!fclose(f));}
static void native(const char *state){
    char text[400];snprintf(text,sizeof(text),"{\"id\":11,\"result\":{\"eventtime\":1,\"status\":{\"print_stats\":{\"state\":\"%s\"},\"extruder\":{\"target\":0},\"heater_bed\":{\"target\":0}}}}",state);
    assert(uds_message(&telemetry,text,strlen(text)));
}
static int action(const char *body){
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    tools_action(pair[0],&model,body,strlen(body));close(pair[0]);
    char response[1024];ssize_t n=read(pair[1],response,sizeof(response)-1);assert(n>0);response[n]=0;close(pair[1]);
    int status=0;assert(sscanf(response,"HTTP/1.1 %d",&status)==1);return status;
}
static int accept_request(const char *command){
    tools_tick(&model);int peer=accept(server,NULL,NULL);assert(peer>=0);
    char request[1024];ssize_t n=recv(peer,request,sizeof(request)-1,MSG_DONTWAIT);assert(n>0);request[n]=0;
    assert(strstr(request,"gcode/subscribe_report")&&strstr(request,command));return peer;
}
static void reply_rotation_label(double value,const char *command,const char *label){
    int peer=accept_request(command);
    char reply[512];int n=snprintf(reply,sizeof(reply),"{\"id\":410,\"result\":{}}\003{\"id\":0,\"report\":{\"message\":\"// Extruder '%s' rotation distance set to %.6f\",\"error_code\":0}}\003{\"id\":411,\"result\":{}}\003",label,value);
    /* Fragment an actual stream frame to exercise buffering across ticks. */
    assert(send(peer,reply,19,0)==19);tools_tick(&model);assert(tools.busy);
    assert(send(peer,reply+19,(size_t)n-19,0)==n-19);
    tools_tick(&model);
    close(peer);
}
/* Replay the empty label observed on Neme77's printer for read/apply/save paths. */
static void reply_rotation(double value,const char *command){reply_rotation_label(value,command,"");}
static void transport_edges(void){
    const char *reports[]={"","\"unrelated report\"","\"Extruder 'other' rotation distance set to 28.8\"",
        "\"Extruder 'extruder' rotation distance set to -28.8\"",
        "\"Extruder 'extruder' rotation distance set to 28.8\",\"Extruder 'extruder' rotation distance set to 29\"",
        "\"Extruder 'extruder' rotation distance set to 28.8\",broken",
        "\"// Extruder '' rotation distance set to -28.8\"",
        "\"// Extruder '' rotation distance set to 0\"",
        "\"// Extruder '' rotation distance set to nan\"",
        "\"// Extruder '' rotation distance set to 28.8\",\"Extruder 'extruder' rotation distance set to 28.8\"",
        "\"// Extruder '' rotation distance set to 28.8junk\""};
    for(size_t i=0;i<sizeof(reports)/sizeof(reports[0]);i++){
        tools_rotation_reading r=tools_rotation_report(reports[i],strlen(reports[i]));assert(!r.valid&&r.value==0);
    }
    const char good[]="\"// Extruder 'extruder' rotation distance set to 28.800000\"";
    tools_rotation_reading r=tools_rotation_report(good,strlen(good));assert(r.valid&&r.value==28.8);
    const char empty[]="\"// Extruder '' rotation distance set to 28.800000\"";
    r=tools_rotation_report(empty,strlen(empty));assert(r.valid&&r.value==28.8);
    nt_call x={.fd=-1};
    const char *malformed="{\"id\":411,\"error\":{\"message\":\"refused\"}}";
    assert(nt_frame(&x,malformed,strlen(malformed))<0&&!x.success);
    memset(&x,0,sizeof(x));x.fd=-1;
    assert(nt_frame(&x,"{\"id\":411}",10)<0);
    memset(&x,0,sizeof(x));x.fd=-1;
    assert(nt_frame(&x,"{\"id\":999,\"result\":{}}",strlen("{\"id\":999,\"result\":{}}"))==0&&!x.done);
    memset(&x,0,sizeof(x));x.fd=-1;x.started=1;x.deadline=20;
    assert(nt_step(&x,20)<0&&x.done&&!x.success);
    char huge[9000];memset(huge,'x',sizeof(huge));
    const char *head="{\"id\":411,\"error\":{\"message\":\"";size_t h=strlen(head);memcpy(huge,head,h);
    memcpy(huge+sizeof(huge)-4,"\"}}",4);
    memset(&x,0,sizeof(x));x.fd=-1;assert(nt_frame(&x,huge,strlen(huge))<0&&!x.success);
}

static int cell_registry,cell_pending_kind,cell_metadata_fail;
static int cell_metadata(const char *query,char **reply,size_t *length){
    assert(!strcmp(query,cell_query));
    if(cell_metadata_fail)return -1;
    const char *registry=cell_registry==2?"\"ACCEPT\":{\"help\":\"Other probe calibration\"}":cell_registry?
        "\"ABORT\":{\"help\":\"Abort load cell calibration tool\"},\"ACCEPT\":{\"help\":\"Accept calibration results and apply to load cell\"},\"TARE\":{\"help\":\"Tare the load cell\"},\"CALIBRATE\":{\"help\":\"Enter the load cell value in grams\"}":"";
    const char *pending=cell_pending_kind==2?"\"extruder\":{\"pid_Kp\":\"20\"}":cell_pending_kind?
        "\"load_cell_probe\":{\"counts_per_gram\":\"25.125\",\"reference_tare_counts\":\"-125\"}":"";
    char body[2200];int n=snprintf(body,sizeof(body),"{\"id\":203,\"result\":{\"status\":{\"gcode\":{\"commands\":{%s}},\"configfile\":{\"save_config_pending\":%s,\"save_config_pending_items\":{%s},\"settings\":{\"load_cell_probe\":{\"safety_limit\":\"1200\",\"counts_per_gram\":\"25\",\"reference_tare_counts\":\"0\"}}}}}}",registry,cell_pending_kind?"true":"false",pending);
    assert(n>0&&(size_t)n<sizeof(body));*reply=strdup(body);assert(*reply);*length=(size_t)n;return 0;
}
static void cell_reply(const char *command,const char *report){
    tools_tick(&model);int peer=accept_request(command);
    char body[1800];int n=snprintf(body,sizeof(body),"{\"id\":410,\"result\":{}}\003%s{\"id\":411,\"result\":{}}\003",report);
    assert(send(peer,body,(size_t)n,0)==n);tools_tick(&model);close(peer);
}
static void cell_cases(void){
    cell_journal_path="cell-session";canvas_unload_program="native-program";put(canvas_unload_program,"fixture");
    native("standby");
    const char temps[]="{\"id\":11,\"result\":{\"eventtime\":1,\"status\":{\"extruder\":{\"temperature\":24,\"target\":0},\"heater_bed\":{\"temperature\":22,\"target\":0}}}}";
    assert(uds_message(&telemetry,temps,strlen(temps)));
    plates_uds=cell_metadata;
    assert(action("cell:accept")==409&&action("cell:measure:500")==400);
    cell_registry=2;assert(action("cell:start")==202);tools_tick(&model);assert(!tools.busy&&!cell_active());
    cell_registry=0;cell_pending_kind=2;assert(action("cell:start")==202);tools_tick(&model);assert(!tools.busy&&!cell_active());cell_pending_kind=0;
    assert(action("cell:tare")==202);
    cell_reply("LOAD_CELL_TARE","{\"id\":0,\"report\":{\"message\":\"// Load cell tare value: -0.01% (-125)\"}}\003");
    assert(!tools.busy&&!strcmp(tools.error,"none")&&!cell_active());
    assert(action("cell:start")==202);cell_reply("LOAD_CELL_CALIBRATE","");
    cell_registry=1;tools_tick(&model);assert(!tools.busy&&cell.state==CELL_ZERO&&cell_same_owner());
    assert(action("cell:accept")==409&&action("rotation:read")==409);
    assert(http_action("/api/control","pid:save")==409);
    assert(http_action("/api/console/command","M112")==202);
    assert(action("cell:zero")==202);cell_reply("TARE","{\"id\":0,\"report\":{\"message\":\"// Load cell tare value: 0.00% (2)\"}}\003");
    tools_tick(&model);assert(!tools.busy&&cell.state==CELL_LOAD&&cell.tare==0);
    /* The session survives an application restart without automatically sending anything. */
    cell.loaded=0;cell.state=0;cell_load();assert(cell.state==CELL_LOAD&&cell_same_owner());
    assert(action("cell:measure:1200")==400&&action("cell:measure:nan")==400);
    assert(action("cell:measure:500")==202);
    cell_reply("CALIBRATE GRAMS=500.000000","{\"id\":0,\"report\":{\"message\":\"// Calibration value: 2.00% (12437), Counts/gram: 25.12500, Total capacity: +/- 25.00Kg\"}}\003{\"id\":0,\"report\":{\"message\":\"// Accept calibration with the ACCEPT command.\"}}\003");
    tools_tick(&model);assert(!tools.busy&&cell.state==CELL_REVIEW&&cell.counts==25.125);
    cell_pending_kind=2;assert(action("cell:accept")==202);tools_tick(&model);assert(!tools.busy&&cell.state==CELL_REVIEW);cell_pending_kind=0;
    assert(action("cell:accept")==202);
    cell_reply("ACCEPT","{\"id\":0,\"report\":{\"message\":\"// Load cell calibration settings: counts_per_gram: 25.125000 reference_tare_counts: -125\"}}\003");
    cell_registry=0;cell_pending_kind=1;tools_tick(&model);assert(!tools.busy&&cell.state==CELL_APPLIED&&cell.tare==-125);
    /* The malformed TARE count (2) must not defeat a correct ACCEPT reference,
     * but neither the pre-ACCEPT placeholder nor a mismatched native pending
     * reference may satisfy the save guard. */
    cell_snapshot accepted={.pending=1,.have_parameters=1,.counts_per_gram=25.125,.tare_counts=-125};
    assert(cell_pending_matches(accepted));accepted.tare_counts=2;assert(!cell_pending_matches(accepted));
    accepted.tare_counts=-126;assert(!cell_pending_matches(accepted));
    assert(action("cell:abort")==409&&pending_autosave_guard());
    /* A file changed outside the wizard is never overwritten by native SAVE_CONFIG. */
    size_t length;char *before=rotation_file(printer_autosave_path,&length);assert(before);
    put(printer_autosave_path,"external change\n");assert(action("cell:save")==202);tools_tick(&model);assert(!tools.busy&&cell.state==CELL_APPLIED&&cell.tare==-125);
    put(printer_autosave_path,before);
    assert(action("cell:save")==202);cell_reply("SAVE_CONFIG","");
    FILE *f=fopen(printer_autosave_path,"a");assert(f);assert(fputs("#*# [load_cell_probe]\n#*# counts_per_gram = 25.125\n#*# reference_tare_counts = -125\n",f)>=0);assert(!fclose(f));
    tools_tick(&model);assert(!tools.busy&&cell.state==CELL_SAVED&&cell_saved_matches());
    assert(action("cell:restart")==202);tools_tick(&model);assert(!tools.busy&&reboot_pending&&reboot_guard==cell_reboot_guard&&reboot_guard());
    native("printing");assert(!reboot_guard());native("standby");reboot_pending=0;
    /* A real new boot clears ownership. A browser/app refresh does not. */
    put(boot_id_path,"boot-three\n");cell.loaded=0;cell.state=CELL_OFF;cell_load();assert(!cell_active());put(printer_autosave_path,before);free(before);
    cell_pending_kind=0;
    assert(action("cell:start")==202);cell_reply("LOAD_CELL_CALIBRATE","");cell_registry=1;tools_tick(&model);
    assert(cell.state==CELL_ZERO&&action("cell:accept")==409);
    assert(action("cell:abort")==202);cell_reply("ABORT","{\"id\":0,\"report\":{\"message\":\"// Load cell calibration aborted\"}}\003");
    cell_registry=0;tools_tick(&model);assert(!tools.busy&&!cell_active());
    /* Lost command acknowledgement cannot enable ACCEPT or replay a mutation. */
    assert(action("cell:start")==202);tools_tick(&model);int peer=accept_request("LOAD_CELL_CALIBRATE");
    tools.call.deadline=monotonic_ms()-1;tools_tick(&model);close(peer);assert(!tools.busy&&cell.state==CELL_UNKNOWN&&action("cell:accept")==409);
    cell_registry=2;assert(action("cell:abort")==202);tools_tick(&model);assert(!tools.busy&&cell.state==CELL_UNKNOWN);
    cell_registry=1;assert(action("cell:abort")==202);cell_reply("ABORT","{\"id\":0,\"report\":{\"message\":\"// Load cell calibration aborted\"}}\003");cell_registry=0;tools_tick(&model);assert(!cell_active());
    plates_uds=uds_query_json;
    puts("PASS: native load-cell wizard steps, registry/session ownership, unknown ACK, incomplete ACCEPT, exact native SAVE_CONFIG, external edits, emergency access, reboot and refresh recovery");
}
int main(void){
    char root[]="/tmp/cc2-tools-XXXXXX";assert(mkdtemp(root)&&!chdir(root));
    rotation_config_path="printer.cfg";printer_autosave_path="autosave.cfg";rotation_pending_path="rotation-pending";
    canvas_unload_pending_path="canvas-pending";boot_id_path="boot-id";object_query_path="native.sock";
    put(boot_id_path,"boot-one\n");
    put(rotation_config_path,"[extruder]\ngear_ratio=52:10\nmicrosteps=16\nrotation_distance=28.8\n");
    const char *autosave="#*# <---------------------- SAVE_CONFIG ---------------------->\n#*# [bed_mesh default]\n#*# points = 0, 1, 2\n#*# [extruder]\n#*# pid_Kp = 21.7\n#*# [probe]\n#*# z_offset = -0.5\n";
    put(printer_autosave_path,autosave);
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));uds_init(&telemetry);telemetry.fd=pair[0];telemetry_peer=pair[1];native("standby");
    mqtt_init(&model);model.connected=model.registered=model.have_machine_status=1;model.machine_status=1;model.last_message=time(NULL);model.print_enabled=1;
    console_init(&console,"none");recovery_console=&console;
    tools.busy=1;
    assert(http_action_origin("/api/control","pid:save","http://foreign.invalid")==403);
    assert(http_action("/api/control","pid:save")==409&&strstr(http_reply,"native calibration tool"));
    assert(http_action("/api/control","system:emergency_stop")==202);
    assert(http_action("/api/console/command","M112")==202&&emergency_calls==2);
    tools.busy=0;
    /* The newly integrated nozzle feature retains its independent heater and
     * SAVE_CONFIG ownership. These rejected actions must not send native G-code. */
    nx_pending_path="nozzle-pending";
    nx.stage=NX_RUNNING;assert(action("rotation:read")==409);nx.stage=NX_IDLE;
    put(nx_pending_path,"boot-one");assert(nx_pending()&&cell_other_pending());
    assert(action("rotation:apply:28.8:29")==409);
    assert(action("rotation:save:28.8")==409&&!tools.busy);
    assert(!unlink(nx_pending_path));
    server=socket(AF_UNIX,SOCK_STREAM,0);assert(server>=0);struct sockaddr_un addr;memset(&addr,0,sizeof(addr));addr.sun_family=AF_UNIX;strcpy(addr.sun_path,"native.sock");assert(!bind(server,(struct sockaddr *)&addr,sizeof(addr))&&!listen(server,4));
    tools_config_read();assert(tools.editable&&tools.configured==28.8&&!tools.have_override);
    assert(action("rotation:read")==202);reply_rotation_label(28.8,"DISTANCE=0","extruder");assert(!tools.busy&&tools.valid&&tools.active==28.8);
    /* A real empty-label reading different from configuration must remain live
     * data; neither configured 28.8 nor a file write may stand in for it. */
    assert(action("rotation:read")==202);reply_rotation(27.91,"DISTANCE=0");
    assert(!tools.busy&&tools.valid&&tools.active==27.91&&tools.configured==28.8&&!rotation_pending());
    size_t before_length=0;char *before=plates_read_file(printer_autosave_path,RD_CONFIG_MAX,&before_length);
    assert(before&&before_length==strlen(autosave)&&!memcmp(before,autosave,before_length));free(before);
    assert(action("rotation:read")==202);reply_rotation(28.8,"DISTANCE=0");assert(!tools.busy&&tools.valid&&tools.active==28.8);
    assert(action("rotation:apply:28.8:27.36")==202);reply_rotation(28.8,"DISTANCE=0");assert(tools.busy&&tools.phase==1);
    reply_rotation(27.36,"DISTANCE=27.3600000000");assert(!tools.busy&&tools.valid&&fabs(tools.active-27.36)<1e-8&&tools.original==28.8);
    /* A changed active value must never be overwritten using a stale calculation. */
    assert(action("rotation:apply:27.36:28.8")==202);reply_rotation(29.0,"DISTANCE=0");assert(!tools.busy&&!tools.valid&&strstr(tools.error,"changed meanwhile"));
    /* External native print ownership wins before the second stage sends. */
    assert(action("rotation:apply:29:28.8")==202);reply_rotation(29,"DISTANCE=0");assert(tools.busy);
    native("printing");tools_tick(&model);assert(!tools.busy);
    int aborted=accept(server,NULL,NULL);assert(aborted>=0);char byte;assert(recv(aborted,&byte,1,0)==0);close(aborted);native("standby");
    assert(action("rotation:save:27.36")==202);reply_rotation(27.36,"DISTANCE=0");assert(!tools.busy&&tools.valid&&rotation_pending()&&rotation_pending_intact());
    size_t length;char *saved=plates_read_file(printer_autosave_path,RD_CONFIG_MAX,&length);assert(saved);
    assert(strstr(saved,"rotation_distance = 27.3600000000")&&strstr(saved,"pid_Kp = 21.7")&&strstr(saved,"points = 0, 1, 2")&&strstr(saved,"z_offset = -0.5"));free(saved);
    assert(pending_autosave_guard()&&action("rotation:apply:27.36:28.8")==409);
    assert(action("rotation:restart")==202&&reboot_guard&&reboot_guard());
    native("printing");assert(!reboot_guard());native("standby");assert(reboot_guard());reboot_pending=0;
    put(printer_autosave_path,autosave);assert(rotation_pending()&&!rotation_pending_intact());assert(action("rotation:restart")==409);
    assert(action("rotation:save:27.36")==202);reply_rotation(27.36,"DISTANCE=0");assert(rotation_pending_intact());
    put(boot_id_path,"boot-two\n");assert(!rotation_pending());
    const char *bad[]={"rotation:apply:-28:29","rotation:apply:nan:29","rotation:apply:28:inf","rotation:apply:28:0","rotation:save:28 junk","cell:read\nM112"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(action(bad[i])==400);
    assert(action("cell:read")==202);int peer=accept_request("LOAD_CELL_READ");
    const char cell[]="{\"id\":410,\"result\":{}}\003{\"id\":0,\"report\":{\"message\":\"12.3g (0.02%)\"}}\003{\"id\":411,\"result\":{}}\003";
    assert(send(peer,cell,sizeof(cell)-1,0)==sizeof(cell)-1);tools_tick(&model);close(peer);assert(!tools.busy&&!strcmp(tools.error,"none"));
    assert(action("cell:diagnostic")==202);assert(action("cell:read")==409);peer=accept_request("LOAD_CELL_DIAGNOSTIC");
    tools.call.deadline=monotonic_ms()-1;tools_tick(&model);close(peer);assert(!tools.busy&&strstr(tools.error,"timed out"));
    assert(action("rotation:read")==202);reply_rotation(27.36,"DISTANCE=0");assert(!tools.busy&&tools.valid);
    /* A symlink must not redirect the persistent editor. */
    assert(!rename(printer_autosave_path,"real-autosave.cfg")&&!symlink("real-autosave.cfg",printer_autosave_path));
    assert(!rotation_save(30)&&!rotation_pending());assert(!unlink(printer_autosave_path)&&!rename("real-autosave.cfg",printer_autosave_path));
    cell_cases();transport_edges();uds_close(&telemetry);close(telemetry_peer);close(server);console_destroy(&console);
    puts("PASS: real native sockets, fragmented replies, runtime read/apply, stale calculation/external-print refusal, exact save/backup/reboot conflict guards, load-cell tools and bounded failures");return 0;
}
