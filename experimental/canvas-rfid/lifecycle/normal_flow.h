/* Invoked by the configured coordinator only. The normal transfer worker is
 * hash-pinned, acquires lifecycle/worker locks itself and owns UART recovery.
 * The parent keeps the app installer barrier while releasing the lifecycle
 * lock for that child; it never bypasses the child's independent live checks. */
#ifndef CC2_CANVAS_NORMAL_FLOW_H
#define CC2_CANVAS_NORMAL_FLOW_H
#ifndef SC_TRANSFER_SHA
#define SC_TRANSFER_SHA "UNCONFIGURED"
#endif
struct vc_summary {struct cl_record record;int busy,barrier,new_boot;};
static int vc_lock_busy(int directory,const char *name,int optional,int *busy){
    int fd=openat(directory,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if(fd<0)return optional&&errno==ENOENT;
    int okay=cl_private(fd,0,0);
    if(okay&&flock(fd,LOCK_SH|LOCK_NB)){
        if(errno==EWOULDBLOCK||errno==EAGAIN)*busy=1;else okay=0;
    }
    close(fd);return okay;
}
static int vc_summary_read(struct vc_summary *summary){
    memset(summary,0,sizeof(*summary));struct cl_store view={-1,-1};char boot[37];
    view.directory=open(SC_BASE,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    /* Status is journal evidence, never activation/reboot permission. Avoid
     * hashing the 29MB native executable on every UI poll. All mutating/verify
     * paths independently validate the full profile under exclusive ownership. */
    int okay=cl_private(view.directory,1,0)&&cl_store_read(&view,&summary->record)&&
        vc_profile_bound(&summary->record)&&vc_boot(boot)&&
        vc_lock_busy(view.directory,"lifecycle.lock",0,&summary->busy)&&vc_lock_busy(view.directory,"worker.lock",1,&summary->busy);
    if(okay){summary->new_boot=strcmp(summary->record.boot,boot)!=0;summary->barrier=maintenance_barrier_active();}
    cl_store_close(&view);return okay;
}
static int vc_needs_reboot(const struct vc_summary *summary){
    if(summary->busy||summary->new_boot)return 0;
    unsigned phase=summary->record.phase;
    return phase==CL_HOST_STAGED||phase==CL_FILTER_ACKED||phase==CL_STOCK_ACKED||phase==CL_HOST_REMOVE_INTENT;
}
static int vc_status(void){
    struct vc_summary summary;
    if(!vc_summary_read(&summary)){puts("{\"journal_valid\":false,\"busy\":true,\"reboot_required\":false}");return 1;}
    unsigned phase=summary.record.phase;
    printf("{\"journal_valid\":true,\"profile_bound\":true,\"phase\":%u,\"generation\":%llu,\"busy\":%s,\"barrier\":%s,\"reboot_required\":%s,\"recovery_required\":%s,\"physical_flash_readback\":false}\n",
        phase,(unsigned long long)summary.record.generation,summary.busy?"true":"false",summary.barrier?"true":"false",
        vc_needs_reboot(&summary)?"true":"false",!summary.busy&&(phase==CL_FILTER_INTENT||phase==CL_STOCK_INTENT)?"true":"false");
    return 0;
}
static void vc_retain_barrier(struct maintenance_barrier *barrier){
    if(barrier->fd>=0)close(barrier->fd);
    barrier->fd=-1;
    puts("MAINTENANCE BARRIER RETAINED: no further printer changes; inspect lifecycle status and reboot only after the transfer worker has completed.");
}
static int vc_transfer_observation(int install){
#ifdef SC_NATIVE_RESTORE_GUARD
    return vc_native_ready(install)&&vc_native_ready(install);
#else
    return sc_fresh()&&(!install||vc_canvas_http())&&sc_fresh();
#endif
}
static int vc_transfer(int install){
    if(geteuid()!=0||strlen(SC_TRANSFER_SHA)!=64||!tx_hex(SC_TRANSFER_SHA)||
       (getenv("LD_PRELOAD")&&*getenv("LD_PRELOAD"))){fprintf(stderr,"STOP: coordinated_transfer_unconfigured\n");return 1;}
    signal(SIGINT,sc_signal);signal(SIGTERM,sc_signal);signal(SIGHUP,sc_signal);signal(SIGPIPE,SIG_IGN);umask(077);
    struct maintenance_barrier barrier=MAINTENANCE_BARRIER_INIT;
    if(!maintenance_barrier_acquire(&barrier)){fprintf(stderr,"STOP: installer_or_maintenance_busy\n");return 1;}
    struct cl_store store;struct cl_record before={0},after={0};struct sc_ready first={0},last={0};
    pid_t app=0,last_app=0;char boot[37]={0};
    int okay=cl_store_open(&store,SC_BASE)&&cl_store_read(&store,&before)&&vc_profile_bound(&before)&&cl_profile(&store,&before)&&
        vc_backup(&store)&&vc_targets(&store)&&sc_hash(SC_PREFIX "/opt/bin/run_printer.sh",SC_RUN_SHA)&&
        sc_hash(SC_BASE "/transfer-worker",SC_TRANSFER_SHA)&&vc_boot(boot);
    if(okay){
        if(install)okay=before.phase==CL_HOST_READY;
        else okay=before.phase==CL_ACTIVE||before.phase==CL_FILTER_ACKED;
    }
    okay=okay&&sc_one_process("cc2-control",SC_APP_PATH,SC_APP_SHA,&app)&&
        sc_native_profile(&first,1)&&vc_transfer_observation(install)&&sc_native_profile(&last,1)&&
        sc_one_process("cc2-control",SC_APP_PATH,SC_APP_SHA,&last_app)&&
        app==last_app&&first.pid==last.pid&&first.nonce==last.nonce&&!sc_cancelled;
    cl_store_close(&store);
    if(!okay){
        (void)maintenance_barrier_release(&barrier);fprintf(stderr,"STOP: coordinated_transfer_preflight_failed; no worker started\n");return 1;
    }
    fflush(stdout);pid_t child=fork();
    if(child<0){(void)maintenance_barrier_release(&barrier);return 1;}
    if(!child){
        if(sc_cancelled)_exit(130);
        if(setsid()<0)_exit(126);
        if(sc_cancelled)_exit(130);
        execl(SC_BASE "/transfer-worker",SC_BASE "/transfer-worker",install?"--install-filter":"--restore-original",(char*)NULL);
        _exit(127);
    }
    int status=0,waited=0,cancel_sent=0;uint64_t next_cancel=0;
    for(;;){
        pid_t result=waitpid(child,&status,WNOHANG);
        if(result==child){waited=1;break;}
        if(result<0&&errno!=EINTR)break;
        if(sc_cancelled&&(!cancel_sent||sc_now()>=next_cancel)){
            /* The worker owns its bounded original-image recovery and ignores
             * further cancellation during that recovery. Redeliver slowly to
             * cover the fork/exec window; never SIGKILL the transfer worker. */
            if(kill(-child,SIGTERM)&&errno!=ESRCH)break;
            if(kill(child,SIGTERM)&&errno!=ESRCH)break;
            cancel_sent=1;next_cancel=sc_now()+1000u;
        }
        struct timespec pause={0,20000000};nanosleep(&pause,NULL);
    }
    int read_back=cl_store_open(&store,SC_BASE)&&cl_store_read(&store,&after)&&vc_profile_bound(&after)&&cl_profile(&store,&after)&&
        !memcmp(before.transaction,after.transaction,16)&&!memcmp(before.plan,after.plan,32);
    int keep=!waited||!read_back;
    if(read_back&&(after.phase==CL_FILTER_INTENT||after.phase==CL_STOCK_INTENT||after.phase==CL_FILTER_ACKED||after.phase==CL_STOCK_ACKED))keep=1;
    okay=waited&&WIFEXITED(status)&&WEXITSTATUS(status)==0&&read_back&&before.generation<=UINT64_MAX-2&&
        after.generation>=before.generation+2&&after.phase==(install?CL_FILTER_ACKED:CL_STOCK_ACKED)&&!strcmp(after.boot,boot);
    cl_store_close(&store);
    if(keep)vc_retain_barrier(&barrier);
    else if(!maintenance_barrier_release(&barrier))okay=0;
    if(!okay){fprintf(stderr,"STOP: coordinated_transfer_not_verified; retain log, lifecycle and original backup\n");return 1;}
    puts("TRANSFER COMPLETE: durable protocol ACK; full idle-printer reboot required for verification. No physical flash readback is claimed.");
    return 0;
}
static int vc_remove_host(void){
    /* Removal is a separate explicit action, never a side effect of polling or
     * a failed transfer. Stock ACK + a later verified boot must precede it. */
    struct maintenance_barrier barrier=MAINTENANCE_BARRIER_INIT;
    if(!maintenance_barrier_acquire(&barrier))return 1;
    struct cl_store store;struct cl_record record={0};struct tx_file files[8];memset(files,0,sizeof(files));
    struct sc_ready first={0},last={0};pid_t app=0,last_app=0;char boot[37]={0};
    int okay=cl_store_open(&store,SC_BASE)&&cl_store_read(&store,&record)&&vc_profile_bound(&record)&&cl_profile(&store,&record)&&
        (record.phase==CL_STOCK_VERIFIED||record.phase==CL_HOST_REMOVE_INTENT)&&vc_backup(&store)&&
        (record.phase!=CL_STOCK_VERIFIED||vc_targets(&store))&&
        (SC_IMMUTABLE_BOOT||sc_read_plan(&store,files))&&vc_boot(boot)&&
        sc_hash(SC_PREFIX "/opt/bin/run_printer.sh",SC_RUN_SHA)&&
        sc_one_process("cc2-control",SC_APP_PATH,SC_APP_SHA,&app);
    int strict=okay&&sc_native_profile(&first,1);
    if(okay&&!strict)okay=(SC_IMMUTABLE_BOOT||record.phase==CL_HOST_REMOVE_INTENT)&&sc_native_profile(&first,0);
    okay=okay&&vc_boot_observation(0)&&sc_native_profile(&last,strict)&&
        sc_one_process("cc2-control",SC_APP_PATH,SC_APP_SHA,&last_app)&&
        app==last_app&&first.pid==last.pid&&first.nonce==last.nonce&&!sc_cancelled;
    if(okay){
        struct cl_facts facts={.profile=1,.backup=1,.barrier=1,.idle=1,.cold=1,.guard=1,.canvas=1};
        if(record.phase==CL_STOCK_VERIFIED)okay=cl_advance(&store,CL_REMOVE_HOST,&facts,boot);
        else if(strict&&strcmp(record.boot,boot)){
            uint64_t previous=record.generation;
            if(previous==UINT64_MAX)okay=0;
            else{record.generation++;memcpy(record.boot,boot,37);okay=cl_store_commit(&store,&record,previous);}
        }
        if(okay&&!SC_IMMUTABLE_BOOT)okay=tx_run(&store,files,8,1);
    }
    for(unsigned i=0;i<8;i++)free((void*)files[i].bytes);
    cl_store_close(&store);
    if(!maintenance_barrier_release(&barrier))okay=0;
    if(!okay){fprintf(stderr,"STOP: verified_stock_host_removal_incomplete; preserve snapshots and journal\n");return 1;}
    puts(SC_IMMUTABLE_BOOT?
         "HOST RESTORED: stock boot recorded; complete idle reboot required. Original backup retained.":
         "HOST RESTORED: exact original eight startup files; complete idle reboot required. All legacy backups and the stock image remain preserved.");
    return 0;
}
static int vc_advance(void){
    struct vc_summary summary;
    if(!vc_summary_read(&summary)){fprintf(stderr,"STOP: lifecycle_status_unavailable\n");return 1;}
    if(summary.busy){puts("LIFECYCLE BUSY: existing worker owns the operation; no additional action started.");return 0;}
    if(vc_needs_reboot(&summary)){puts("REBOOT REQUIRED: waiting for a different kernel boot; no action repeated.");return 0;}
    switch(summary.record.phase){
    case CL_HOST_STAGED:
        if(vc_verify(CL_VERIFY_HOST_BOOT,CL_HOST_STAGED,CL_HOST_READY))return 1;
        return vc_transfer(1);
    case CL_HOST_READY:return vc_transfer(1);
    case CL_FILTER_ACKED:return vc_verify(CL_VERIFY_FILTER_BOOT,CL_FILTER_ACKED,CL_ACTIVE);
    case CL_STOCK_ACKED:return vc_verify(CL_VERIFY_STOCK_BOOT,CL_STOCK_ACKED,CL_STOCK_VERIFIED);
    case CL_ACTIVE:puts("LIFECYCLE ACTIVE: no transfer repeated.");return 0;
    case CL_STOCK_VERIFIED:puts("ORIGINAL VERIFIED: no automatic filter reinstall; host removal may be requested separately.");return 0;
    case CL_HOST_REMOVE_INTENT:
        if(vc_remove_host())return 1;
        if(!vc_summary_read(&summary))return 1;
        if(vc_needs_reboot(&summary)){puts("REBOOT REQUIRED: restored host profile must be observed on the next complete boot.");return 0;}
        return vc_verify_mode(CL_VERIFY_HOST_REMOVED,CL_HOST_REMOVE_INTENT,CL_DISABLED,1);
    case CL_DISABLED:puts("HOST DISABLED: original startup profile retained; no transfer repeated.");return 0;
    case CL_FILTER_INTENT:case CL_STOCK_INTENT:
        fprintf(stderr,"RECOVERY REQUIRED: incomplete durable intent; use the qualified boot recovery, do not retry a normal transfer\n");return 1;
    default:fprintf(stderr,"STOP: lifecycle_phase_requires_explicit_action\n");return 1;
    }
}
#endif
