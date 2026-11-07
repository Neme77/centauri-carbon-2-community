/* Closed-profile host stage, post-boot evidence and coordinated transfer.
 * Not installable until full package/updater and physical qualification. */
#define main coordinator_stage_main
#include "stage_worker.c"
#undef main
#include "canvas_observation.h"
#include "app_binding_observation.h"
static int vc_targets_state(struct cl_store *store,int state){
    struct tx_file files[8];memset(files,0,sizeof(files));char plan[65];
    int okay=sc_read_plan(store,files)&&tx_plan(files,8,plan);
    int snapshots=-1;
    if(okay){
        snapshots=openat(store->directory,"startup-tx",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        okay=cl_private(snapshots,1,0)&&tx_receipt(snapshots,"ready",plan,0)&&tx_receipt(snapshots,state?"applied":"restored",plan,0);
    }
    for(unsigned i=0;okay&&i<8;i++){
        char name[32];snprintf(name,sizeof(name),"%u.old",i);
        okay=tx_current(files+i)==state&&tx_read(snapshots,name,files[i].old_sha,0600,NULL,NULL);
        snprintf(name,sizeof(name),"%u.new",i);
        if(okay)okay=tx_read(snapshots,name,files[i].new_sha,0600,NULL,NULL);
    }
    if(snapshots>=0)close(snapshots);
    for(unsigned i=0;i<8;i++)free((void*)files[i].bytes);
    return okay;
}
static int vc_targets(struct cl_store *store){return vc_targets_state(store,1);}
static int vc_backup(struct cl_store *store){
    int fd=openat(store->directory,"original-stock-44.bin",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if(fd<0)return 0;
    unsigned char *bytes=malloc(AP_SIZE);
    int okay=bytes&&cl_private(fd,0,AP_SIZE)&&cl_io(fd,bytes,AP_SIZE,0)&&ap_image(bytes,AP_SIZE,0);
    close(fd);free(bytes);return okay;
}
static int vc_boot(char boot[37]){
    int fd=open(SC_BOOT_ID,O_RDONLY|O_CLOEXEC);if(fd<0)return 0;
    char bytes[38]={0};int okay=cl_io(fd,(unsigned char*)bytes,37,0);close(fd);
    if(!okay||bytes[36]!='\n')return 0;
    bytes[36]=0;memcpy(boot,bytes,37);return cl_boot_id_valid(boot);
}
static int vc_profile_bound(const struct cl_record *record){
    if(strlen(SC_PROFILE_SHA)!=64||!tx_hex(SC_PROFILE_SHA))return 0;
    char expected[65];static const char hex[]="0123456789abcdef";
    for(unsigned i=0;i<32;i++){expected[i*2]=hex[record->plan[i]>>4];expected[i*2+1]=hex[record->plan[i]&15];}expected[64]=0;
    return !strcmp(expected,SC_PROFILE_SHA);
}
static int vc_verify_mode(enum cl_event event,unsigned wanted,unsigned complete,int disabled){
    if(geteuid()!=0||strlen(SC_PROFILE_SHA)!=64||!tx_hex(SC_PROFILE_SHA)||
       strlen(SC_APP_SHA)!=64||!tx_hex(SC_APP_SHA)||strlen(SC_FRESH_SHA)!=64||!tx_hex(SC_FRESH_SHA)||
       (getenv("LD_PRELOAD")&&*getenv("LD_PRELOAD")))return 1;
    signal(SIGINT,sc_signal);signal(SIGTERM,sc_signal);signal(SIGHUP,sc_signal);signal(SIGPIPE,SIG_IGN);umask(077);
    struct maintenance_barrier barrier=MAINTENANCE_BARRIER_INIT;
    if(!maintenance_barrier_acquire(&barrier)){fprintf(stderr,"STOP: installer_or_maintenance_busy\n");return 1;}
    struct cl_store store;struct cl_record record={0};char boot[37]={0};pid_t app=0,last_app=0;
    struct sc_ready first={0},last={0};
    /* Only the read-only scan preflight acquires a new quiet MQTT sample.
     * Firmware transfer and boot verification keep their existing checks. */
    int scan_check=wanted==CL_ACTIVE&&complete==CL_ACTIVE&&event==CL_VERIFY_FILTER_BOOT&&!disabled;
    int okay=cl_store_open(&store,SC_BASE)&&cl_store_read(&store,&record)&&cl_profile(&store,&record);
    okay=okay&&vc_profile_bound(&record)&&(record.phase==wanted||record.phase==complete)&&vc_boot(boot)&&
        vc_backup(&store)&&(disabled?vc_targets_state(&store,0):vc_targets(&store))&&sc_hash(SC_PREFIX "/opt/bin/run_printer.sh",SC_RUN_SHA)&&
        sc_one_process("cc2-control",SC_PREFIX "/opt/usr/cc2-control/cc2-control",SC_APP_SHA,&app)&&
        sc_native_profile(&first,!disabled)&&(scan_check?aw_fresh_ready():sc_fresh())&&vc_canvas_http()&&
        (scan_check?aw_fresh_ready():sc_fresh())&&sc_native_profile(&last,!disabled)&&
        sc_one_process("cc2-control",SC_PREFIX "/opt/usr/cc2-control/cc2-control",SC_APP_SHA,&last_app)&&
        app==last_app&&first.pid==last.pid&&first.nonce==last.nonce&&!sc_cancelled;
    if(okay&&record.phase!=complete){
        struct cl_facts facts={.profile=1,.backup=1,.barrier=1,.idle=1,.cold=1,.guard=1,.canvas=1,.host_staged=1,.host_removed=disabled};
        okay=cl_advance(&store,event,&facts,boot);
    }
    cl_store_close(&store);
    if(!maintenance_barrier_release(&barrier))okay=0;
    if(!okay){fprintf(stderr,"STOP: post_boot_evidence_incomplete_or_commit_uncertain; retain lifecycle journal\n");return 1;}
    printf("POST BOOT VERIFIED: lifecycle=%u; qualified native profile and live Canvas observed. Protocol ACKs remain distinct from physical flash readback.\n",complete);
    return 0;
}
static int vc_verify(enum cl_event event,unsigned wanted,unsigned complete){return vc_verify_mode(event,wanted,complete,0);}
#include "normal_flow.h"
#ifndef VC_ENTRY
#define VC_ENTRY main
#endif
int VC_ENTRY(int argc,char **argv){
    if(argc!=2){fprintf(stderr,"STOP: unsupported_coordinator_action\n");return 1;}
    if(!strcmp(argv[1],"--check")||!strcmp(argv[1],"--stage-host"))return coordinator_stage_main(argc,argv);
    if(!strcmp(argv[1],"--verify-host-boot"))return vc_verify(CL_VERIFY_HOST_BOOT,CL_HOST_STAGED,CL_HOST_READY);
    if(!strcmp(argv[1],"--verify-filter-boot"))return vc_verify(CL_VERIFY_FILTER_BOOT,CL_FILTER_ACKED,CL_ACTIVE);
    if(!strcmp(argv[1],"--verify-stock-boot"))return vc_verify(CL_VERIFY_STOCK_BOOT,CL_STOCK_ACKED,CL_STOCK_VERIFIED);
    if(!strcmp(argv[1],"--check-active"))return vc_verify(CL_VERIFY_FILTER_BOOT,CL_ACTIVE,CL_ACTIVE);
    if(!strcmp(argv[1],"--remove-host"))return vc_remove_host();
    if(!strcmp(argv[1],"--verify-disabled-boot"))return vc_verify_mode(CL_VERIFY_HOST_REMOVED,CL_HOST_REMOVE_INTENT,CL_DISABLED,1);
    if(!strcmp(argv[1],"--install-filter"))return vc_transfer(1);
    if(!strcmp(argv[1],"--restore-original"))return vc_transfer(0);
    if(!strcmp(argv[1],"--advance"))return vc_advance();
    if(!strcmp(argv[1],"--status"))return vc_status();
    fprintf(stderr,"STOP: unsupported_coordinator_action\n");return 1;
}
