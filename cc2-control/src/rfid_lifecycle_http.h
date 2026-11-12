/* The app remains responsive while the pinned coordinator observes its APIs.
 * Firmware/serial/recovery ownership stays entirely in that coordinator. */
#ifndef CC2_RFID_LIFECYCLE_HTTP_H
#define CC2_RFID_LIFECYCLE_HTTP_H
static struct {pid_t child;double next;unsigned failures;int result;uint64_t generation;} rfid_native_job;

static int rn_reboot(rfid_runtime mode){
    return mode.mode==1&&!mode.new_boot&&(mode.phase==1||mode.phase==4||mode.phase==7||mode.phase==9);
}
static int rn_blocks(void){
    /* A staged lifecycle blocks mutations. A dormant install waits for explicit activation. */
    rfid_runtime mode=rfid_runtime_get(1);
    return rfid_native_job.child||mode.mode<0||(mode.mode==1&&(mode.phase!=-1&&mode.phase!=5&&mode.phase!=10));
}
static int rn_restore_available(console_state *console){
    pthread_mutex_lock(&console->lock);int busy=console->busy;pthread_mutex_unlock(&console->lock);
    return !busy&&!rfid.child&&runout.x.fd<0&&!plates_late_active()&&!app_update_active()&&
        !connections_busy()&&!plates_background_active()&&!cell_active()&&!tools.busy&&
        !drying_active()&&!nx_active()&&!pending_autosave_guard()&&!recovery_available()&&!reboot_pending;
}
static int rn_strict_mapped(void){
    rfid_identity identity;if(!rfid_identity_read(&identity))return 0;
    char path[96];int n=snprintf(path,sizeof(path),"/proc/%u/maps",identity.pid);
    if(n<1||(size_t)n>=sizeof(path))return 0;
    FILE *file=fopen(path,"r");if(!file)return 0;
    char line[1024],expected[512];int found=0;
    n=snprintf(expected,sizeof(expected),"%s/libcc2-rfid.so\n",CC2_RFID_NATIVE_ROOT);
    if(n<1||(size_t)n>=sizeof(expected)){fclose(file);return 0;}
    while(fgets(line,sizeof(line),file)){
        char *name=strchr(line,'/');
        if(name&&!strcmp(name,expected)){found=1;break;}
    }
    fclose(file);return found;
}
static int rn_start(const char *action){
    if(rfid_native_job.child)return 0;
    rfid_runtime mode=rfid_runtime_get(1);if(mode.mode!=1)return 0;
    char path[512],program[512];int n=snprintf(program,sizeof(program),"%s/coordinator",CC2_RFID_NATIVE_ROOT);
    if(n<1||(size_t)n>=sizeof(program))return 0;
    int helper=open(program,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);struct stat st;char digest[65];
    if(helper<0)return 0;
    int okay=!fstat(helper,&st)&&S_ISREG(st.st_mode)&&st.st_uid==CC2_RFID_OWNER_UID&&st.st_nlink==1&&
        !(st.st_mode&022)&&sha256_fd(helper,digest)&&!strcmp(digest,mode.coordinator)&&lseek(helper,0,SEEK_SET)>=0;
    struct rlimit limit;if(!okay||getrlimit(RLIMIT_NOFILE,&limit)||limit.rlim_cur>1048576){close(helper);return 0;}
    n=snprintf(path,sizeof(path),"%s/application-action.log",CC2_RFID_NATIVE_ROOT);
    if(n<1||(size_t)n>=sizeof(path)){close(helper);return 0;}
    int log=open(path,O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK,0600);
    if(log<0){close(helper);return 0;}
    okay=!fstat(log,&st)&&S_ISREG(st.st_mode)&&st.st_uid==CC2_RFID_OWNER_UID&&st.st_nlink==1&&
        (st.st_mode&0777)==0600&&!ftruncate(log,0);
    if(!okay){close(log);close(helper);return 0;}
    pid_t child=fork();
    if(!child){
        if(setsid()<0||dup2(log,STDOUT_FILENO)<0||dup2(log,STDERR_FILENO)<0)_exit(125);
        for(int fd=3;fd<(int)limit.rlim_cur;fd++)if(fd!=helper)close(fd);
        char *const args[]={program,(char*)action,NULL};
        char *const environment[]={"PATH=/usr/sbin:/usr/bin:/sbin:/bin","LANG=C",NULL};
        fexecve(helper,args,environment);_exit(125);
    }
    close(log);close(helper);if(child<0)return 0;
    rfid_cancel("cancelled");rfid_native_job.child=child;rfid_native_job.result=-1;
    rfid_native_job.generation=mode.generation;return 1;
}
static const char *rn_automatic(rfid_runtime mode,int strict){
    if(mode.mode!=1||rn_reboot(mode))return NULL;
    /* No normal retry of durable flash intent, and no activation of a dormant
     * installation. STOCK_VERIFIED requires explicit host removal. */
    if(mode.phase==1)return strict?"--advance":"--stage-host";
    if(mode.phase==2||mode.phase==4||mode.phase==7||mode.phase==9)return "--advance";
    return NULL;
}
static int rn_resume_required(rfid_runtime mode){
    return mode.mode==1&&!rn_reboot(mode)&&(mode.phase==1||mode.phase==2);
}
static void rfid_native_tick(console_state *console){
    double now=rfid_now();
    if(rfid_native_job.child){
        int status=0;pid_t done=waitpid(rfid_native_job.child,&status,WNOHANG);
        if(!done||(done<0&&errno==EINTR))return;
        rfid_native_job.child=0;
        rfid_native_job.result=done>0&&WIFEXITED(status)&&WEXITSTATUS(status)==0?0:1;
        if(rfid_native_job.result)rfid_native_job.failures++;else rfid_native_job.failures=0;
        rfid_native_job.next=now+30;
        (void)rfid_runtime_get(1);
        return;
    }
    if(now<rfid_native_job.next||rfid_native_job.failures>=3)return;
    rfid_native_job.next=now+5;
    rfid_runtime mode=rfid_runtime_get(0);
    if(mode.mode!=1||mode.phase < -1||rn_reboot(mode)||rn_resume_required(mode)||rfid.child||maintenance_barrier_active()||
       !rn_restore_available(console))return;
    const char *action=rn_automatic(mode,mode.phase==1?rn_strict_mapped():0);
    if(action&&!rn_start(action)){rfid_native_job.failures++;rfid_native_job.next=now+30;}
}
static void rfid_native_status_response(int fd){
    rfid_runtime mode=rfid_runtime_get(0);
    char body[512];int n=snprintf(body,sizeof(body),
        "{\"installed\":%s,\"metadata_valid\":%s,\"app_binding_sha256\":\"%s\",\"phase\":%d,\"generation\":%llu,\"busy\":%s,\"reboot_required\":%s,\"recovery_required\":%s,\"result\":%d,\"retry_required\":%s,\"activation_requested\":false,\"physical_flash_readback\":false}\n",
        mode.mode?"true":"false",mode.mode==1?"true":"false",mode.app,mode.phase,(unsigned long long)mode.generation,
        rfid_native_job.child?"true":"false",rn_reboot(mode)?"true":"false",
        mode.mode<0||mode.phase==-2||mode.phase==3||mode.phase==6?"true":"false",rfid_native_job.result,
        rfid_native_job.failures>=3||rn_resume_required(mode)?"true":"false");
    respond(fd,200,"OK","application/json; charset=utf-8",body,(size_t)n);
}
static void rfid_native_action_response(int fd,console_state *console,const char *body,size_t length){
    rfid_runtime mode=rfid_runtime_get(1);const char *action=NULL;
    if(length==8&&!memcmp(body,"activate",8)&&(mode.phase==-1||mode.phase==1||mode.phase==10))action="--stage-host";
    if(length==6&&!memcmp(body,"resume",6)){
        action=rn_automatic(mode,mode.phase==1?rn_strict_mapped():0);
    }
    if(length==7&&!memcmp(body,"restore",7)&&mode.phase==5)action="--restore-original";
    if(length==6&&!memcmp(body,"remove",6)&&(mode.phase==8||mode.phase==9))action="--remove-host";
    if(mode.mode!=1||!action||!rn_restore_available(console)||!rn_start(action)){
        plates_fail(fd,409,"RFID lifecycle action requires its qualified component and an idle/cold printer; preserve the lifecycle journal and backup");return;
    }
    rfid_native_job.failures=0;
    const char *accepted="{\"accepted\":true}\n";
    respond(fd,202,"Accepted","application/json; charset=utf-8",accepted,strlen(accepted));
}
#endif
