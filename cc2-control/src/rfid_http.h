/* Private host trial. A pinned read-only helper owns the scan; no UART here. */
#include <sys/resource.h>
#include "rfid_preview.h"
#ifndef CC2_RFID_OWNER_UID
#define CC2_RFID_OWNER_UID 0
#endif
#ifndef CC2_RFID_HELPER
#define CC2_RFID_HELPER "/opt/usr/cc2-rfid-frame-capture-r1/cc2-rfid-frame-capture-r1"
#endif
#ifndef CC2_RFID_HELPER_SHA
#define CC2_RFID_HELPER_SHA "53cdc2d893fda5304cd986f8f508871c188f4b595a5d5dc7143c23f0e1068113"
#endif
#ifndef CC2_RFID_READY
#define CC2_RFID_READY "/tmp/cc2-rfid-native-r2/ready"
#endif
#include "rfid_runtime.h"
#ifndef CC2_RFID_NATIVE_SCAN
#define CC2_RFID_NATIVE_SCAN 0
#endif
typedef struct {uint64_t epoch;uint32_t pid;} rfid_identity;
static struct {
    pid_t child;int fd,valid,cancelled,cancel_sent,native_child;unsigned generation;
    double deadline,expires,next_native_check;size_t used;char output[12288];
    const char *state;char error[96];rfid_identity identity;rfid_preview preview;
} rfid={.fd=-1,.state="idle"};
static double rfid_now(void){struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);return now.tv_sec+now.tv_nsec/1e9;}
static int rfid_identity_read(rfid_identity *out){
    int fd=open(CC2_RFID_READY,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);if(fd<0)return 0;
    unsigned char bytes[41];struct stat st;ssize_t n=read(fd,bytes,sizeof(bytes));
    int good=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==CC2_RFID_OWNER_UID&&st.st_nlink==1&&(st.st_mode&0777)==0600&&n==40;
    close(fd);if(!good||memcmp(bytes,"CC2RF1\0\0",8))return 0;
    uint32_t phase;memcpy(&phase,bytes+36,4);memcpy(&out->epoch,bytes+8,8);memcpy(&out->pid,bytes+32,4);
    return phase==1&&out->pid>1&&out->epoch;
}
static int rfid_same_identity(void){rfid_identity now;return rfid_identity_read(&now)&&now.epoch==rfid.identity.epoch&&now.pid==rfid.identity.pid;}
static int rfid_printer_idle_limit(const mqtt_client *mqtt,console_state *console,time_t age_limit){
    double nozzle,bed,nt,bt;time_t now=time(NULL);const char *native=uds_print_state(&telemetry);
    pthread_mutex_lock(&console->lock);int busy=console->busy;pthread_mutex_unlock(&console->lock);
    return !busy&&gcode_idle_for_mutation(mqtt)&&now>=mqtt->last_message&&now-mqtt->last_message<=age_limit&&
        native&&(!strcmp(native,"standby")||!strcmp(native,"complete")||!strcmp(native,"cancelled"))&&
        uds_value(&telemetry,U_ET,&nozzle)&&uds_value(&telemetry,U_BT,&bed)&&
        uds_value(&telemetry,U_EG,&nt)&&uds_value(&telemetry,U_BG,&bt)&&
        isfinite(nozzle)&&isfinite(bed)&&nozzle<50&&bed<40&&nt==0&&bt==0&&
        !app_update_active()&&!connections_busy()&&!plates_background_active()&&!cell_active()&&!tools.busy&&
        !drying_active()&&!nx_active()&&!pending_autosave_guard();
}
static int rfid_idle_limit(const mqtt_client *mqtt,console_state *console,time_t age_limit){return rfid_runtime_allowed(0)&&rfid_printer_idle_limit(mqtt,console,age_limit);}
static int rfid_idle(const mqtt_client *mqtt,console_state *console){return rfid_idle_limit(mqtt,console,5);}
static int rfid_native_scan(void){return CC2_RFID_NATIVE_SCAN&&rfid_runtime_get(0).mode==1;}
static int rfid_native_scan_available(console_state *console){
    pthread_mutex_lock(&console->lock);int busy=console->busy;pthread_mutex_unlock(&console->lock);
    return !busy&&rfid_runtime_allowed(0)&&runout.x.fd<0&&!plates_late_active()&&
        !app_update_active()&&!connections_busy()&&!plates_background_active()&&!cell_active()&&
        !tools.busy&&!drying_active()&&!nx_active()&&!pending_autosave_guard()&&
        !recovery_available()&&!reboot_pending;
}
static int rfid_scan_ready(const mqtt_client *mqtt,console_state *console){
    return rfid_native_scan()?rfid_native_scan_available(console):rfid_idle(mqtt,console);
}
static const char *rfid_blocked_reason(const mqtt_client *mqtt,console_state *console){
    if(rfid_native_scan())return rfid_native_scan_available(console)?"native_not_ready":"printer_not_ready";
    double value;
    if(uds_value(&telemetry,U_ET,&value)&&value>=50)return "nozzle_hot";
    if(uds_value(&telemetry,U_BT,&value)&&value>=40)return "bed_hot";
    if((uds_value(&telemetry,U_EG,&value)&&value!=0)||(uds_value(&telemetry,U_BG,&value)&&value!=0))return "heaters_on";
    time_t now=time(NULL);
    if(now<mqtt->last_message||now-mqtt->last_message>5)return "stale_telemetry";
    return rfid_idle(mqtt,console)?"native_not_ready":"printer_not_ready";
}
static void rfid_cancel(const char *state){
    rfid.valid=0;rfid.state=state;
    if(rfid.child){
        rfid.cancelled=1;
        /* Before SCAN ARMED the helper runs the manager's bounded preflight.
         * Interrupting that helper makes its old guard kill the manager before
         * the shell can release its lock. Let preflight finish, discard any
         * result, then interrupt only the armed capture. */
        if(!rfid.cancel_sent&&strstr(rfid.output,"SCAN ARMED:")){
            kill(rfid.child,SIGTERM);rfid.cancel_sent=1;rfid.deadline=rfid_now()+3;
        }
    }
}
static int rfid_helper_open(char path[512]){
    char expected[65];if(!rfid_runtime_helper(path,512,expected,1))return -1;
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);if(fd<0)return -1;
    struct stat st;char digest[65];
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=CC2_RFID_OWNER_UID||st.st_nlink!=1||(st.st_mode&0022)||
       !sha256_fd(fd,digest)||strcmp(digest,expected)||lseek(fd,0,SEEK_SET)<0){close(fd);return -1;}
    return fd;
}
static int rfid_read_output(void){
    char chunk[1024];ssize_t n=read(rfid.fd,chunk,sizeof(chunk));
    if(n<=0)return 0;
    size_t used=rfid.used,count=(size_t)n;
    if(used>=sizeof(rfid.output)-1||count>=sizeof(rfid.output)-1-used)return -1;
    memcpy(rfid.output+used,chunk,count);rfid.used=used+count;rfid.output[rfid.used]=0;
    return 1;
}
static void rfid_tick(mqtt_client *mqtt,console_state *console){
    if(rfid.child||rfid.valid){
        int safe=rfid_same_identity();
        if(safe&&rfid_native_scan()){
            safe=rfid_native_scan_available(console);
            if(safe&&rfid.child&&!rfid.cancelled&&strstr(rfid.output,"SCAN ARMED:")&&
               rfid_now()>=rfid.next_native_check){
                safe=installer_native_observation(0,1)==0;
                rfid.next_native_check=rfid_now()+1;
            }
        }else if(safe)safe=rfid_idle_limit(mqtt,console,15);
        if(!safe)rfid_cancel("state_changed");
    }
    if(rfid.valid&&rfid_now()>rfid.expires)rfid_cancel("expired");
    if(!rfid.child)return;
    for(unsigned count=0;count<4;count++){
        int n=rfid_read_output();
        if(n<0){rfid_cancel("output_limit");break;}
        if(!n)break;
    }
    if(rfid.cancelled)rfid_cancel(rfid.state);
    if(rfid_now()>rfid.deadline){
        if(rfid.cancel_sent){
            if(!rfid.native_child)kill(rfid.child,SIGKILL);
            else rfid.deadline=rfid_now()+3; /* Native guard releases its owned installer barrier cooperatively. */
        }
        else {rfid_cancel("timeout");if(!rfid.cancel_sent){kill(rfid.child,SIGTERM);rfid.cancel_sent=1;rfid.deadline=rfid_now()+3;}}
    }
    int status=0;pid_t done=waitpid(rfid.child,&status,WNOHANG);if(done==0)return;
    if(done<0&&errno==EINTR)return;
    rfid.child=0;
    /* The child may write its last bytes between the nonblocking read and
     * waitpid. Drain those bytes before closing; never signal a reaped PID. */
    for(unsigned count=0;count<16;count++){
        int n=rfid_read_output();
        if(n<0){rfid.cancelled=1;rfid.state="output_limit";break;}
        if(!n)break;
    }
    if(rfid.used==sizeof(rfid.output)-1){rfid.cancelled=1;rfid.state="output_limit";}
    close(rfid.fd);rfid.fd=-1;
    if(rfid.cancelled)return;
    rfid.output[rfid.used]=0;
    if(done<0||!WIFEXITED(status)||WEXITSTATUS(status)||memchr(rfid.output,0,rfid.used)){
        rfid.state="scan_failed";
        /* A quiet-cache refusal may be recovered before SCAN ARMED. Report
         * the first failure after the latest successful readiness check,
         * rather than presenting the already recovered refusal to the user. */
        const char *from=rfid.output,*passed;
        while((passed=strstr(from,"LIVE GUARD PASS:")))from=passed+strlen("LIVE GUARD PASS:");
        const char *stop=strstr(from,"STOP: ");
        if(stop){stop+=6;size_t n=strspn(stop,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_");
            if(n&&n<sizeof(rfid.error)){memcpy(rfid.error,stop,n);rfid.error[n]=0;}}
        return;
    }
    if(rfid_native_scan()&&installer_native_observation(0,1)!=0){
        rfid_cancel("state_changed");return;
    }
    unsigned results=0;rfid_preview preview={0};
    char *line=rfid.output;
    while(*line){char *end=strchr(line,'\n');if(end)*end=0;
        if(!strncmp(line,"RFID_RESULT=",12)){results++;if(!rfid_decode(line,&preview)){rfid.state="unsupported_tag";return;}}
        if(!end)break;
        line=end+1;
    }
    if(results!=1){rfid.state="no_result";return;}
    rfid.preview=preview;rfid.valid=1;rfid.expires=rfid_now()+120;rfid.state="preview";
}
static void rfid_status_response(int fd,mqtt_client *mqtt,console_state *console){
    rfid_tick(mqtt,console);rfid_identity identity;
    char helper_path[512],helper_sha[65];struct stat st;
    int installed=rfid_runtime_helper(helper_path,sizeof(helper_path),helper_sha,0)&&!lstat(helper_path,&st)&&S_ISREG(st.st_mode)&&st.st_uid==CC2_RFID_OWNER_UID&&st.st_nlink==1;
    char body[1024],preview[600]="null";
    if(rfid.valid){rfid_preview *p=&rfid.preview;
        snprintf(preview,sizeof(preview),"{\"uid\":\"%s\",\"brand\":\"Anycubic\",\"material\":\"%s\",\"sku\":\"%s\",\"color\":\"#%s\",\"diameter\":1.75,\"nozzle_min\":%u,\"nozzle_max\":%u,\"bed_min\":%u,\"bed_max\":%u,\"weight\":null,\"remaining\":null}",
            p->uid,p->material,p->sku,p->color,p->nozzle_min,p->nozzle_max,p->bed_min,p->bed_max);
    }
    int ready=installed&&rfid_identity_read(&identity)&&rfid_scan_ready(mqtt,console);
    snprintf(body,sizeof(body),"{\"installed\":%s,\"ready\":%s,\"busy\":%s,\"armed\":%s,\"state\":\"%s\",\"reason\":\"%s\",\"error\":\"%s\",\"generation\":%u,\"preview\":%s}",
        installed?"true":"false",ready?"true":"false",rfid.child?"true":"false",
        rfid.child&&strstr(rfid.output,"SCAN ARMED:")?"true":"false",rfid.state,ready?"none":rfid_blocked_reason(mqtt,console),rfid.error,rfid.generation,preview);
    respond(fd,200,"OK","application/json",body,strlen(body));
}
static void rfid_action_response(int fd,mqtt_client *mqtt,console_state *console,const char *body,size_t length){
    rfid_tick(mqtt,console);
    if(length==6&&!memcmp(body,"cancel",6)){rfid_cancel("cancelled");rfid_status_response(fd,mqtt,console);return;}
    if(length==4&&!memcmp(body,"scan",4)){
        if(rfid.child||!rfid_scan_ready(mqtt,console)||!rfid_identity_read(&rfid.identity)){plates_fail(fd,409,"RFID requires idle/cold state and the qualified host component");return;}
        char helper_path[512];int helper=rfid_helper_open(helper_path),pipes[2];struct rlimit limit;
        if(helper<0){plates_fail(fd,409,"Qualified RFID scan helper is not installed");return;}
        if(getrlimit(RLIMIT_NOFILE,&limit)||limit.rlim_cur>1048576||pipe2(pipes,O_CLOEXEC)){close(helper);plates_fail(fd,503,"Cannot prepare bounded RFID scan");return;}
        pid_t pid=fork();
        if(!pid){
            if(setsid()<0||dup2(pipes[1],STDOUT_FILENO)<0||dup2(pipes[1],STDERR_FILENO)<0)_exit(125);
            for(int i=3;i<(int)limit.rlim_cur;i++)if(i!=helper)close(i);
            char *const args[]={helper_path,"--scan",NULL};
            char *const environment[]={"PATH=/usr/sbin:/usr/bin:/sbin:/bin","LANG=C",NULL};
            fexecve(helper,args,environment);_exit(125);
        }
        close(helper);close(pipes[1]);
        if(pid<0){close(pipes[0]);plates_fail(fd,503,"Cannot start RFID scan");return;}
        int flags=fcntl(pipes[0],F_GETFL,0);
        if(flags<0||fcntl(pipes[0],F_SETFL,flags|O_NONBLOCK)<0){kill(pid,SIGKILL);waitpid(pid,NULL,0);close(pipes[0]);plates_fail(fd,503,"Cannot observe RFID scan");return;}
        rfid.child=pid;rfid.fd=pipes[0];rfid.used=0;rfid.output[0]=0;rfid.error[0]=0;rfid.valid=0;rfid.cancelled=0;rfid.cancel_sent=0;
        rfid.next_native_check=0;rfid.native_child=rfid_runtime_get(0).mode==1;rfid.generation++;rfid.state="scanning";rfid.deadline=rfid_now()+(rfid.native_child?150:90);
        rfid_status_response(fd,mqtt,console);return;
    }
    unsigned generation;int slot,end=-1;char input[48];
    if(length>=sizeof(input)||memchr(body,0,length)){plates_fail(fd,400,"Invalid RFID action");return;}
    memcpy(input,body,length);input[length]=0;
    if(sscanf(input,"apply:%u:%d%n",&generation,&slot,&end)!=2||end<0||input[end]||slot<0||slot>3){plates_fail(fd,400,"Invalid RFID action");return;}
    if(rfid.child||!rfid.valid||generation!=rfid.generation||rfid_now()>rfid.expires||!rfid_runtime_allowed(1)||!rfid_idle(mqtt,console)||!rfid_same_identity()){
        plates_fail(fd,409,"RFID preview expired or printer state changed; scan again");return;
    }
    char action[160];rfid_preview *p=&rfid.preview;
    snprintf(action,sizeof(action),"canvas:material:%d:%s:%s:%u:%u:%s:Anycubic",slot,p->material,p->color,p->nozzle_min,p->nozzle_max,p->sku);
    /* One-shot consumption even on rejection; never replay a queued setting. */
    rfid.valid=0;rfid.state="submitted";
    control_response(fd,console,mqtt,action,strlen(action));
}
