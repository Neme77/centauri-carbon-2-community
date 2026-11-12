#ifndef CC2_CALIBRATION_TOOLS_H
#define CC2_CALIBRATION_TOOLS_H
#include "rotation_core.h"
#include "native_tools_transport.h"
enum {TOOL_NONE,TOOL_ROTATION_READ,TOOL_ROTATION_APPLY,TOOL_ROTATION_SAVE,TOOL_CELL_READ,TOOL_CELL_DIAGNOSTIC,TOOL_CELL_GUIDE};
static const char *rotation_config_path="/opt/inst/printer.cfg";
static const char *rotation_pending_path="/tmp/cc2-control-rotation-pending";
static struct {
    nt_call call;
    int busy,mode,phase,valid,have_original,config_read,editable,have_configured,have_override;
    unsigned long connection,generation;
    double expected,target,active,original,configured,override;
    long long completed_at;
    const char *error;
} tools={.call={.fd=-1},.error="none"};

static int rotation_regular(const char *path,int absent_ok){
    struct stat st;if(lstat(path,&st))return absent_ok&&errno==ENOENT;
    return S_ISREG(st.st_mode)&&st.st_nlink==1;
}
static char *rotation_file(const char *path,size_t *length){
    if(!rotation_regular(path,0))return NULL;
    return plates_read_file(path,RD_CONFIG_MAX,length);
}
static void tools_config_read(void){
    size_t length=0;rd_entry entry;tools.config_read=1;
    tools.have_configured=tools.have_override=tools.editable=0;
    char *file=rotation_file(rotation_config_path,&length);
    if(file&&rd_parse(file,length,0,&entry)&&entry.present){tools.configured=entry.value;tools.have_configured=1;}free(file);
    file=rotation_file(printer_autosave_path,&length);
    if(file&&rd_parse(file,length,1,&entry)){
        tools.editable=tools.have_configured;tools.have_override=entry.present;tools.override=entry.value;
    }free(file);
}
static int rotation_pending_value(double *value){
    size_t length=0;char boot[64];
    if(!rotation_regular(rotation_pending_path,1))return 1;
    char *record=plates_read_file(rotation_pending_path,128,&length);
    if(!record)return errno!=ENOENT;
    if(!canvas_unload_boot_id(boot,sizeof(boot))){free(record);return 1;}
    char *line=strchr(record,'\n');size_t n=line?(size_t)(line-record):length;
    if(n!=strlen(boot)||memcmp(record,boot,n)){free(record);unlink(rotation_pending_path);return 0;}
    if(value&&line){double v;if(rd_decimal(line+1,record+length,&v))*value=v;}
    free(record);return 1;
}
static int rotation_pending(void){return rotation_pending_value(NULL);}
static int rotation_pending_intact(void){
    double expected=0;if(!rotation_pending_value(&expected))return 1;
    size_t length=0;rd_entry entry;char *file=rotation_file(printer_autosave_path,&length);
    int ok=expected>0&&file&&rd_parse(file,length,1,&entry)&&entry.present&&fabs(entry.value-expected)<1e-8;
    free(file);return ok;
}
static const char *tools_ready(const mqtt_client *m){
    const char *reason=plates_printer_ready(m);if(reason)return reason;
    const char *state=uds_print_state(&telemetry);
    /* A cancelled job remains in print_stats after the machine returns Idle. */
    if(!state||(strcmp(state,"standby")&&strcmp(state,"complete")&&strcmp(state,"cancelled")))return "Native printer must be idle";
    if(drying_active())return "Stop the bed-heating session first";
    if(plates_background_active())return "Plate verification is still running";
    return NULL;
}
static int rotation_save(double value){
    char boot[64],record[96],backup[PATH_MAX_LOCAL];size_t length=0,next_length=0,again_length=0,old_length=0;
    tools_config_read();if(!tools.editable||!rd_positive(value)||!canvas_unload_boot_id(boot,sizeof(boot)))return 0;
    char *file=rotation_file(printer_autosave_path,&length);if(!file)return 0;
    char *next=rd_rewrite(file,length,value,&next_length);if(!next){free(file);return 0;}
    char *again=rotation_file(printer_autosave_path,&again_length);
    int unchanged=again&&length==again_length&&!memcmp(again,file,length);free(again);free(file);
    if(!unchanged||!rotation_regular(rotation_pending_path,1)||!autosave_backup_path(backup,sizeof(backup))||!rotation_regular(backup,1)){
        free(next);return 0;
    }
    int existed=rotation_pending();
    char *old=existed?plates_read_file(rotation_pending_path,128,&old_length):NULL;
    if(existed&&!old){free(next);return 0;}
    int n=snprintf(record,sizeof(record),"%s\n%.10f",boot,value);
    int marked=n>0&&(size_t)n<sizeof(record)&&plates_replace_file(rotation_pending_path,NULL,record,(size_t)n,0600)==0;
    int saved=marked&&plates_replace_file(printer_autosave_path,backup,next,next_length,autosave_mode())==0;
    if(marked&&!saved){if(old)(void)plates_replace_file(rotation_pending_path,NULL,old,old_length,0600);else unlink(rotation_pending_path);}
    free(old);free(next);tools_config_read();return saved;
}
/* This call targets EXTRUDER=extruder on its own subscribed connection. Some
 * deployed firmware registers that mux name but leaves this->name empty in its
 * report. Accept only those two exact labels and one complete positive reading;
 * never substitute the configured distance or accept arbitrary extruder names. */
typedef struct {int valid;double value;} tools_rotation_reading;
static tools_rotation_reading tools_rotation_report(const char *reports,size_t length){
    tools_rotation_reading result={0,0};
    const char *p=reports,*end=p+length;int found=0;
    const char prefix[]="Extruder 'extruder' rotation distance set to ";
    const char empty_prefix[]="Extruder '' rotation distance set to ";
    while(p<end){
        if(*p==',')p++;
        if(p==end||*p!='"')return (tools_rotation_reading){0,0};
        const char *se=ccv_json_string_end(p,end);if(!se)return (tools_rotation_reading){0,0};
        const char *text=p+1,*stop=se-1;
        if(stop-text>=3&&!memcmp(text,"// ",3))text+=3;
        size_t prefix_length=0;
        if((size_t)(stop-text)>=sizeof(prefix)-1&&!memcmp(text,prefix,sizeof(prefix)-1))prefix_length=sizeof(prefix)-1;
        else if((size_t)(stop-text)>=sizeof(empty_prefix)-1&&!memcmp(text,empty_prefix,sizeof(empty_prefix)-1))prefix_length=sizeof(empty_prefix)-1;
        if(prefix_length){
            text+=prefix_length;double v=0;
            if(found++||!rd_decimal(text,stop,&v))return (tools_rotation_reading){0,0};
            result.value=v;
        }
        p=se;
    }
    if(found!=1)return (tools_rotation_reading){0,0};
    result.valid=1;return result;
}
static void tools_finish(const char *error){
    nt_close(&tools.call);tools.busy=0;tools.error=error?error:"none";tools.completed_at=monotonic_ms();
    tools.call.done=1;if(error)tools.call.success=0;
    if(error)tools.valid=0;
}
static int rotation_reboot_guard(void);
#include "load_cell_guide.h"
static void tools_tick(const mqtt_client *m){
    if(tools.connection!=telemetry.connections){tools.valid=0;tools.have_original=0;}
    if(!tools.busy)return;
    if(tools.mode==TOOL_CELL_GUIDE){cell_tick(m);return;}
    const char *reason=tools_ready(m);
    if(reason||tools.connection!=telemetry.connections){tools_finish(reason?reason:"Printer service changed");return;}
    int result=nt_step(&tools.call,monotonic_ms());if(!result)return;
    if(result<0){tools_finish(tools.call.error);return;}
    if(tools.mode==TOOL_CELL_READ||tools.mode==TOOL_CELL_DIAGNOSTIC){
        tools_finish(tools.call.reports_count?NULL:"Native tool returned no reading");return;
    }
    tools_rotation_reading reading=tools_rotation_report(tools.call.reports,tools.call.reports_used);
    if(!reading.valid||!rd_positive(reading.value)){
        tools_finish("No unambiguous positive rotation distance returned");return;
    }
    double value=reading.value;
    if(!tools.phase&&(tools.mode==TOOL_ROTATION_APPLY||tools.mode==TOOL_ROTATION_SAVE)){
        if(fabs(value-tools.expected)>1e-7){tools_finish("Rotation distance changed meanwhile; read it again");return;}
        if(tools.mode==TOOL_ROTATION_SAVE){
            if(nx_pending()||canvas_unload_pending()||plates.pending[0]||plates_cal.stage||!rotation_save(value)){
                tools_finish("Cannot save rotation distance; configuration unchanged or a restart is already pending");return;
            }
        }else{
            const char *pending=pending_autosave_guard();if(pending){tools_finish(pending);return;}
            if(!tools.have_original){tools.original=value;tools.have_original=1;}
            char command[160];snprintf(command,sizeof(command),"SET_EXTRUDER_ROTATION_DISTANCE EXTRUDER=extruder DISTANCE=%.10f",tools.target);
            tools.phase=1;
            if(nt_start(&tools.call,command,object_query_path,monotonic_ms(),5000)<0)tools_finish(tools.call.error);
            return;
        }
    }
    if(tools.phase&&fabs(value-tools.target)>1e-7){tools_finish("Native rotation distance does not match the request");return;}
    tools.active=value;tools.valid=1;tools_finish(NULL);
}
static void tools_status(int fd){
    cell_load();
    if(!tools.config_read)tools_config_read();
    char body[NT_REPLY_MAX+1200],error[256],tare[48];json_escape(error,sizeof(error),tools.error);
    if(cell.state==CELL_APPLIED||cell.state==CELL_SAVED)snprintf(tare,sizeof(tare),"%.0f",cell.tare);
    else snprintf(tare,sizeof(tare),"null");
    json_builder b={body,0,sizeof(body),0};
    int pending=rotation_pending(),conflict=pending&&!rotation_pending_intact();
    json_builder_printf(&b,"{\"busy\":%s,\"generation\":%lu,\"error\":\"%s\",\"mode\":%d,\"active\":",tools.busy?"true":"false",tools.generation,error,tools.mode);
    if(tools.valid)json_builder_printf(&b,"%.10f",tools.active);else json_builder_printf(&b,"null");
    json_builder_printf(&b,",\"original\":");if(tools.have_original)json_builder_printf(&b,"%.10f",tools.original);else json_builder_printf(&b,"null");
    json_builder_printf(&b,",\"configured\":");if(tools.have_configured)json_builder_printf(&b,"%.10f",tools.configured);else json_builder_printf(&b,"null");
    json_builder_printf(&b,",\"saved\":");if(tools.have_override)json_builder_printf(&b,"%.10f",tools.override);else json_builder_printf(&b,"null");
    json_builder_printf(&b,",\"editable\":%s,\"pending_restart\":%s,\"pending_conflict\":%s,\"reports\":[%s],\"cell\":{\"stage\":%d,\"owned\":%s,\"max_grams\":%.6f,\"counts_per_gram\":%.6f,\"tare_counts\":%s,\"grams\":%.6f}}\n",
        tools.editable?"true":"false",pending?"true":"false",conflict?"true":"false",tools.call.reports,cell.state,cell.owned?"true":"false",cell_max_grams(cell.safety),cell.counts,tare,cell.grams);
    plates_reply(fd,b.failed?500:200,b.failed?"{\"error\":\"Tool status too large\"}":body);
}
static int rotation_reboot_guard(void){
    const char *state=uds_print_state(&telemetry);double nozzle,bed;
    return plates_reboot_guard()&&!tools.busy&&!drying_active()&&state&&
        (!strcmp(state,"standby")||!strcmp(state,"complete")||!strcmp(state,"cancelled"))&&
        uds_value(&telemetry,U_EG,&nozzle)&&uds_value(&telemetry,U_BG,&bed)&&nozzle==0&&bed==0;
}
static void tools_action(int fd,mqtt_client *m,const char *body,size_t length){
    if(!length||length>=160||memchr(body,0,length)){plates_fail(fd,400,"Invalid calibration tool action");return;}
    char action[160];memcpy(action,body,length);action[length]=0;
    if(tools.busy){plates_fail(fd,409,"A native tool is still running");return;}
    const char *reason=tools_ready(m);if(reason){plates_fail(fd,409,reason);return;}
    if(!strncmp(action,"cell:",5)&&strcmp(action,"cell:read")&&strcmp(action,"cell:diagnostic")){cell_action(fd,m,action);return;}
    if(cell_active()){plates_fail(fd,409,"Finish the load-cell calibration session first");return;}
    if(!strcmp(action,"rotation:restart")){
        if(!rotation_pending()||!rotation_pending_intact()){plates_fail(fd,409,"No intact rotation distance change is waiting for restart");return;}
        plates_mqtt=m;
        if(!rotation_reboot_guard()){plates_fail(fd,409,"Turn both heaters off before restarting the printer");return;}
        reboot_guard=rotation_reboot_guard;reboot_pending=1;reboot_launched=0;reboot_due=recovery_clock()+2;reboot_error="none";
        plates_reply(fd,202,"{\"accepted\":true,\"restarting\":true}");return;
    }
    int mode=TOOL_NONE,end=0;double expected=0,target=0;
    if(!strcmp(action,"rotation:read"))mode=TOOL_ROTATION_READ;
    else if(!strcmp(action,"cell:read"))mode=TOOL_CELL_READ;
    else if(!strcmp(action,"cell:diagnostic"))mode=TOOL_CELL_DIAGNOSTIC;
    else if(sscanf(action,"rotation:apply:%lf:%lf%n",&expected,&target,&end)==2&&end&&action[end]==0&&rd_positive(expected)&&rd_positive(target))mode=TOOL_ROTATION_APPLY;
    else if(sscanf(action,"rotation:save:%lf%n",&expected,&end)==1&&end&&action[end]==0&&rd_positive(expected))mode=TOOL_ROTATION_SAVE;
    if(!mode){plates_fail(fd,400,"Invalid calibration tool action");return;}
    if(mode==TOOL_ROTATION_APPLY){reason=pending_autosave_guard();if(reason){plates_fail(fd,409,reason);return;}}
    if(mode==TOOL_ROTATION_SAVE&&(nx_pending()||canvas_unload_pending()||plates.pending[0]||plates_cal.stage)){
        plates_fail(fd,409,"Resolve the pending configuration change first");return;
    }
    tools_config_read();tools.mode=mode;tools.phase=0;tools.expected=expected;tools.target=target;
    tools.valid=0;tools.error="none";tools.connection=telemetry.connections;tools.generation++;
    const char *command=mode==TOOL_CELL_READ?"LOAD_CELL_READ":mode==TOOL_CELL_DIAGNOSTIC?"LOAD_CELL_DIAGNOSTIC":
        "SET_EXTRUDER_ROTATION_DISTANCE EXTRUDER=extruder DISTANCE=0";
    if(nt_start(&tools.call,command,object_query_path,monotonic_ms(),mode==TOOL_CELL_DIAGNOSTIC?20000:5000)<0){
        tools_finish(tools.call.error);plates_fail(fd,503,tools.error);return;
    }
    tools.busy=1;plates_reply(fd,202,"{\"accepted\":true}");
}
#endif
