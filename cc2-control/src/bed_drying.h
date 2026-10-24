#ifndef CC2_BED_DRYING_H
#define CC2_BED_DRYING_H
/* HTTP-loop owned. A short native heater lease bounds heat if CC2 dies or hangs;
 * no long dwell, worker, firmware configuration edit or automatic resumption. */
enum { DRY_OFF, DRY_CHECK, DRY_ARM, DRY_HEAT, DRY_HEATING, DRY_HOLDING,
       DRY_STOPPING, DRY_RESTORE, DRY_DONE, DRY_INTERRUPTED, DRY_RECOVERY };
static struct {
    int stage, temp, minutes, original_timeout, restore_only, stopping_started, target_seen;
    long long since, hold_until, next_lease, retry_at;
    unsigned long connection, stop_messages;
    const char *reason;
    plates_exchange exchange;
    char query[384];
} drying = {.exchange={.fd=-1}, .reason="none"};
static const char *drying_journal = "/opt/usr/cc2-bed-drying.session";
static const char *drying_config = "/opt/inst/printer.cfg";
static const char *drying_autosave = "/opt/usr/cfg/autosave.cfg";
static long long (*drying_clock)(void) = monotonic_ms;

static int drying_active(void) {
    return (drying.stage >= DRY_CHECK && drying.stage <= DRY_RESTORE) || drying.stage == DRY_RECOVERY;
}
static int drying_timeout_file(const char *path, int *value) {
    FILE *f=fopen(path,"r");
    if(!f)return errno==ENOENT?0:-1;
    char line[512];int section=0,seen=0,ok=1;
    while(fgets(line,sizeof(line),f)) {
        char *p=line;while(isspace((unsigned char)*p))p++;
        if(*p=='['){char name[80];section=sscanf(p,"[%79[^]]]",name)==1&&!strcmp(name,"idle_timeout");}
        if(section&&!strncmp(p,"heater_bed_idle_timeout",23)&&
           (p[23]=='='||isspace((unsigned char)p[23]))) {
            char *eq=strchr(p,'='),*end=NULL;long n=eq?strtol(eq+1,&end,10):0;
            while(end&&isspace((unsigned char)*end))end++;
            if(!eq||end==eq+1||(*end&&*end!='#'&&*end!=';')||n<60||n>259200||seen)ok=0;
            else {*value=(int)n;seen=1;}
        }
    }
    if(ferror(f))ok=0;
    fclose(f);return ok?seen:-1;
}
static int drying_configured_timeout(int *value) {
    /* Autosave may override the base configuration; restore the configured value. */
    int override;
    int status=drying_timeout_file(drying_autosave,&override);
    if(status<0)return 0;
    if(status){*value=override;return 1;}
    return drying_timeout_file(drying_config,value)==1;
}
static int drying_marker_write(void) {
    char text[80];int n=snprintf(text,sizeof(text),"CC2DRY1 %d %d\n",drying.original_timeout,drying.temp);
    int fd=open(drying_journal,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if(fd<0)return 0;
    int ok=write(fd,text,(size_t)n)==n;
    if(fsync(fd))ok=0;
    if(close(fd))ok=0;
    if(ok){
        char directory[512];snprintf(directory,sizeof(directory),"%s",drying_journal);
        char *slash=strrchr(directory,'/');if(slash)*slash=0;else strcpy(directory,".");
        int dir=open(directory,O_RDONLY);if(dir<0)ok=0;else {if(fsync(dir))ok=0;close(dir);}
    }
    if(!ok)unlink(drying_journal);
    return ok;
}
static void drying_load(void) {
    int fd=open(drying_journal,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0){if(errno!=ENOENT){drying.stage=DRY_RECOVERY;drying.reason="storage";}return;}
    struct stat st;char text[80]={0},canonical[80];int timeout=0,temp=0;
    int ok=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&!(st.st_mode&0077)&&st.st_size>0&&st.st_size<79;
    ssize_t n=ok?read(fd,text,sizeof(text)-1):-1;close(fd);
    ok=ok&&n==st.st_size&&sscanf(text,"CC2DRY1 %d %d",&timeout,&temp)==2&&timeout>=60&&timeout<=259200&&temp>=30&&temp<=90;
    if(ok){snprintf(canonical,sizeof(canonical),"CC2DRY1 %d %d\n",timeout,temp);ok=!strcmp(text,canonical);}
    drying.stage=DRY_RECOVERY;drying.reason=ok?"restart":"storage";
    if(ok){drying.original_timeout=timeout;drying.temp=temp;}
}
static int drying_sample(const mqtt_client *mqtt, double *bed, double *target) {
    double nozzle,velocity;
    const char *state=uds_print_state(&telemetry);
    time_t age=time(NULL)-mqtt->last_message;
    return mqtt->connected&&mqtt->registered&&mqtt->have_machine_status&&mqtt->machine_status==1&&
        !mqtt->print_enabled&&age>=0&&age<=15&&state&&
        (!strcmp(state,"standby")||!strcmp(state,"complete")||!strcmp(state,"cancelled"))&&
        uds_value(&telemetry,U_BT,bed)&&uds_value(&telemetry,U_BG,target)&&
        uds_value(&telemetry,U_EG,&nozzle)&&nozzle==0&&
        uds_value(&telemetry,U_LIVE_SPEED,&velocity)&&fabs(velocity)<0.01&&
        isfinite(*bed)&&*bed>=0&&*bed<=110&&isfinite(*target);
}
static int drying_ready(const mqtt_client *mqtt,console_state *console) {
    double bed,target;
    return experiments.bed_drying&&experiments_storage_valid&&!drying_active()&&!app_update_active()&&
        app_update_ready(mqtt,console)&&!plates_cal.stage&&!plates.pending[0]&&!plates_late_active()&&
        !z_offset_pending&&drying_sample(mqtt,&bed,&target)&&target==0;
}
static void drying_command(const char *script) {
    plates_exchange_close(&drying.exchange);
    snprintf(drying.query,sizeof(drying.query),"{\"id\":204,\"method\":\"gcode/script\",\"params\":{\"script\":\"%s\"}}\003",script);
}
/* Exact correlated success, not a substring in an unsolicited report. */
static int drying_result(const char *text,size_t length,int help) {
    const char *end=text+length,*root=json_skip_space(text,end);
    const char *root_end=root<end&&*root=='{'?json_container_end(root,end):NULL;
    if(!root_end||json_skip_space(root_end,end)!=end||json_member(root,root_end,"error"))return 0;
    int id=-1;if(!json_member_int(root,root_end,"id",&id)||id!=(help?203:204))return 0;
    const char *result=json_member(root,root_end,"result");
    if(!result)return 0;
    if(!help){const char *re=*result=='{'?json_container_end(result,root_end):NULL;
        return re&&json_skip_space(result+1,re)==re-1;}
    const char *re=result<root_end&&*result=='{'?json_container_end(result,root_end):NULL;
    return re&&json_member(result,re,"SET_HEATER_BED_IDLE_TIMEOUT")&&json_member(result,re,"M140");
}
static void drying_stop(const char *reason,int restore_only) {
    plates_exchange_close(&drying.exchange);drying.query[0]=0;
    drying.stage=DRY_STOPPING;drying.since=drying_clock();drying.retry_at=0;
    drying.reason=reason;drying.restore_only=restore_only;drying.stopping_started=0;
}
static void drying_tick(const mqtt_client *mqtt) {
    if(!drying_active())return;
    long long now=drying_clock();double bed=0,target=0;
    int fresh=drying_sample(mqtt,&bed,&target);
    if(drying.stage==DRY_RECOVERY) {
        /* Never adopt a hot bed after a process restart. The last short lease
         * expires in the firmware; restore its configured timeout only at zero. */
        if(fresh&&target==0&&drying.original_timeout){drying.stage=DRY_RESTORE;drying.reason="restart";drying.query[0]=0;}
        else return;
    }
    if(drying.stage<DRY_STOPPING&&(!fresh||telemetry.connections!=drying.connection)) {
        int foreign=mqtt->have_machine_status&&mqtt->machine_status!=1;
        drying_stop(foreign?"busy":"telemetry",foreign);
    }
    if(drying.stage>=DRY_CHECK&&drying.stage<=DRY_HEAT&&fresh&&target!=0&&
       !(drying.stage==DRY_HEAT&&drying.exchange.sent>0&&fabs(target-drying.temp)<=0.5))
        drying_stop("changed",1);
    if(drying.stage==DRY_STOPPING&&mqtt->have_machine_status&&mqtt->machine_status!=1)
        drying.restore_only=1;
    if(drying.stage==DRY_HEATING||drying.stage==DRY_HOLDING) {
        if(!drying.target_seen&&fabs(target-drying.temp)<=0.5)drying.target_seen=1;
        if(!drying.target_seen&&now-drying.since<10000)return;
        if(fresh&&fabs(target-drying.temp)>0.5)drying_stop("changed",1);
        else if(now-drying.since>=30LL*60000&&drying.stage==DRY_HEATING)drying_stop("heat_timeout",0);
        else if(drying.stage==DRY_HEATING&&fabs(bed-drying.temp)<=1) {
            drying.stage=DRY_HOLDING;drying.hold_until=now+(long long)drying.minutes*60000;
        }
        else if(drying.stage==DRY_HOLDING&&now>=drying.hold_until)drying_stop("elapsed",0);
    }
    if(drying.stage==DRY_STOPPING) {
        if(!fresh){drying.reason="telemetry";return;} /* Do not cool somebody else's active print. */
        if(target==0&&drying.stopping_started&&telemetry.messages>drying.stop_messages) {
            drying.stage=DRY_RESTORE;drying.query[0]=0;
        } else if(drying.restore_only|| (target!=0&&fabs(target-drying.temp)>0.5)) {
            /* A changed nonzero setpoint belongs to another user. Leave the
             * native short timeout in place until the bed is switched off. */
            if(target!=0)return;
            drying.stage=DRY_RESTORE;drying.query[0]=0;
        } else if(!drying.query[0]&&now>=drying.retry_at) {
            drying_command("M140 S0");drying.stop_messages=telemetry.messages;
            drying.stopping_started=1;drying.retry_at=now+5000;
        }
    }
    if(drying.stage==DRY_RESTORE&&!drying.query[0]) {
        if(now<drying.retry_at)return;
        if(!fresh||target!=0||!drying.original_timeout)return;
        char command[100];snprintf(command,sizeof(command),"SET_HEATER_BED_IDLE_TIMEOUT TIMEOUT=%d",drying.original_timeout);
        drying_command(command);
    }
    if((drying.stage==DRY_HEATING||drying.stage==DRY_HOLDING)&&!drying.query[0]&&now>=drying.next_lease) {
        drying_command("SET_HEATER_BED_IDLE_TIMEOUT TIMEOUT=90");
    }
    if(!drying.query[0])return;
    /* A transfer/print or manual action can revoke authority before an unsent
     * request. Cancel it; never send a stale queued heater command later. */
    if(!fresh&&drying.exchange.sent==0){plates_exchange_close(&drying.exchange);drying.query[0]=0;return;}
    char *reply=NULL;size_t length=0;
    int help=drying.stage==DRY_CHECK;
    int result=plates_exchange_step(&drying.exchange,drying.query,&reply,&length);
    if(!result)return;
    int accepted=result>0&&drying_result(reply,length,help);free(reply);drying.query[0]=0;
    if(!accepted) {
        if(help){
            if(unlink(drying_journal)&&errno!=ENOENT){drying.stage=DRY_RECOVERY;drying.reason="storage";return;}
            drying.stage=DRY_INTERRUPTED;drying.reason="unsupported";return;
        }
        if(drying.stage==DRY_STOPPING||drying.stage==DRY_RESTORE){drying.retry_at=now+5000;return;}
        drying_stop(help?"unsupported":"command",0);return;
    }
    if(drying.stage==DRY_CHECK) {
        drying.stage=DRY_ARM;drying_command("SET_HEATER_BED_IDLE_TIMEOUT TIMEOUT=90");
    } else if(drying.stage==DRY_ARM) {
        drying.stage=DRY_HEAT;char command[32];snprintf(command,sizeof(command),"M140 S%d",drying.temp);drying_command(command);
    } else if(drying.stage==DRY_HEAT) {
        drying.stage=DRY_HEATING;drying.target_seen=0;drying.since=now;drying.next_lease=now+30000;
    } else if(drying.stage==DRY_RESTORE) {
        if(unlink(drying_journal)&&errno!=ENOENT){drying.stage=DRY_RECOVERY;drying.reason="storage";return;}
        drying.stage=!strcmp(drying.reason,"elapsed")?DRY_DONE:DRY_INTERRUPTED;
    } else if(drying.stage==DRY_HEATING||drying.stage==DRY_HOLDING)drying.next_lease=now+30000;
}
static int drying_status_json(char *body,size_t capacity,const mqtt_client *mqtt) {
    static const char *states[]={"off","checking","arming","heating","heating","holding","stopping","stopping","completed","interrupted","interrupted"};
    double bed=0,target=0;int fresh=drying_sample(mqtt,&bed,&target);
    long long left=drying.stage==DRY_HOLDING?(drying.hold_until-drying_clock()+999)/1000:0;
    return snprintf(body,capacity,
        "{\"enabled\":%s,\"active\":%s,\"state\":\"%s\",\"reason\":\"%s\",\"temperature\":%d,\"minutes\":%d,\"remaining\":%lld,\"fresh\":%s,\"bed\":%.2f,\"target\":%.2f}\n",
        experiments.bed_drying?"true":"false",drying_active()?"true":"false",
        states[drying.stage],drying.reason,drying.temp,drying.minutes,left>0?left:0,fresh?"true":"false",bed,target);
}
static void drying_response(int fd,const mqtt_client *mqtt,console_state *console) {
    (void)console;char body[640];int n=drying_status_json(body,sizeof(body),mqtt);
    if(n>0&&(size_t)n<sizeof(body))respond(fd,200,"OK","application/json",body,(size_t)n);
}
static void drying_start_response(int fd,const char *request,const char *body,size_t length,mqtt_client *mqtt,console_state *console) {
    if(!app_update_authorized(request,mqtt)){app_update_fail(fd,401,"LAN code confirmation required or temporarily locked");return;}
    int temp=0,minutes=0;char canonical[32],input[32]={0};
    if(length>=sizeof(input)){app_update_fail(fd,400,"Invalid bed heating settings");return;}
    memcpy(input,body,length);
    if(sscanf(input,"%d:%d",&temp,&minutes)!=2||temp<30||temp>90||minutes<1||minutes>720){app_update_fail(fd,400,"Use 30-90 C and 1-720 minutes");return;}
    snprintf(canonical,sizeof(canonical),"%d:%d",temp,minutes);
    if(strlen(canonical)!=length||memcmp(body,canonical,length)){app_update_fail(fd,400,"Invalid bed heating settings");return;}
    if(!drying_ready(mqtt,console)){app_update_fail(fd,409,"Bed heating needs an enabled experiment, fresh idle/cold printer and no other operation");return;}
    if(!drying_configured_timeout(&drying.original_timeout)){app_update_fail(fd,409,"Cannot verify the configured bed idle timeout");return;}
    drying.temp=temp;drying.minutes=minutes;
    if(!drying_marker_write()){app_update_fail(fd,503,"Cannot record bed heating session; no heat requested");return;}
    drying.stage=DRY_CHECK;drying.reason="none";drying.connection=telemetry.connections;drying.since=drying_clock();
    snprintf(drying.query,sizeof(drying.query),"{\"id\":203,\"method\":\"gcode/help\",\"params\":{}}\003");
    drying_response(fd,mqtt,console);
}
static void drying_stop_response(int fd,const char *request,mqtt_client *mqtt,console_state *console) {
    if(!app_update_authorized(request,mqtt)){app_update_fail(fd,401,"LAN code confirmation required or temporarily locked");return;}
    if(drying_active()&&drying.stage!=DRY_RECOVERY)drying_stop("manual",0);
    drying_response(fd,mqtt,console);
}
#endif
