/* Closed-profile host staging only. No native stop, UART, firmware write or
 * reboot action exists here. A qualified packager pins the running app, complete
 * recovery profile and fresh guard in this separate executable (no hash cycle
 * with the generic boot helper which is part of that profile). */
#define _GNU_SOURCE
#include "startup_tx.h"
#include "profile.h"
#include "../transfer/app_transfer.h"
#include "../../../cc2-control/src/maintenance_barrier.h"
#include "../../../cc2-control/src/cavity_floor_protocol.h"
#include <dirent.h>
#include <signal.h>
#include <sys/vfs.h>
#include <sys/wait.h>
#include <time.h>
#ifndef SC_BASE
#define SC_BASE "/opt/usr/cc2-rfid"
#endif
#ifndef SC_PREFIX
#define SC_PREFIX ""
#endif
#ifndef SC_PROFILE_SHA
#define SC_PROFILE_SHA "UNCONFIGURED"
#endif
#ifndef SC_APP_SHA
#define SC_APP_SHA "UNCONFIGURED"
#endif
#ifndef SC_FRESH_SHA
#define SC_FRESH_SHA "UNCONFIGURED"
#endif
#ifndef SC_FRESH_PATH
#define SC_FRESH_PATH SC_BASE "/fresh-guard.sh"
#endif
#ifndef SC_READY
#define SC_READY "/tmp/cc2-rfid-native-r2/ready"
#endif
/* Packaging selects one closed baseline. The normal/legacy build continues
 * to require its existing RFID guard; the stock build never accepts it. */
#ifndef SC_STOCK_BOOTSTRAP
#define SC_STOCK_BOOTSTRAP 0
#endif
#ifndef SC_WITH_CAVITY
#define SC_WITH_CAVITY 1
#endif
#ifndef SC_CAVITY_BOOTSTRAP_OPTIONAL
#define SC_CAVITY_BOOTSTRAP_OPTIONAL 0
#endif
#ifndef SC_VERIFY_CAVITY_CHANNEL
#define SC_VERIFY_CAVITY_CHANNEL 0
#endif
#if (SC_STOCK_BOOTSTRAP != 0 && SC_STOCK_BOOTSTRAP != 1) || (SC_WITH_CAVITY != 0 && SC_WITH_CAVITY != 1)
#error Unsupported native baseline selection
#endif
#if (SC_CAVITY_BOOTSTRAP_OPTIONAL != 0 && SC_CAVITY_BOOTSTRAP_OPTIONAL != 1) || (SC_CAVITY_BOOTSTRAP_OPTIONAL && (!SC_STOCK_BOOTSTRAP || !SC_WITH_CAVITY))
#error Optional cavity bootstrap requires the closed cumulative stock profile
#endif
#ifndef SC_BOOT_ID
#define SC_BOOT_ID "/proc/sys/kernel/random/boot_id"
#endif
#ifndef SC_ORIGINAL
#define SC_ORIGINAL "/opt/inst/ota/canvas_gd32-20251029_184624-00.00.00.44-app.bin"
#endif
#ifndef SC_NATIVE_SHA
#define SC_NATIVE_SHA "19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745"
#endif
#ifndef SC_RUN_SHA
#define SC_RUN_SHA "05e4614779fc59b43c604eda7d85f1e8534b24d3cf284b16226c9c3bbc975118"
#endif
#ifndef SC_OLD_RFID_SHA
#define SC_OLD_RFID_SHA "17367981b1b2183f978f1e5de5bd4f8097094e24e6a9a271735b52e7b6441104"
#endif
#ifndef SC_REACTOR_SHA
#define SC_REACTOR_SHA "4639364c68489ec40c4e0c4f425550f3aa19e44c2ee61ed641affdc560e7e92c"
#endif
#ifndef SC_CAVITY_SHA
#define SC_CAVITY_SHA "52d400605dd51f89eb95248c42a1661b3c0e60fadec5453d27a987729332fb10"
#endif
#ifndef SC_NETWORK_SHA
#define SC_NETWORK_SHA "bde512195dd6bf3846f3103b5e1acbd89a62e5ee819e405389c97f5fc188ff3a"
#endif
#ifndef SC_STRICT_SHA
#define SC_STRICT_SHA "fe6210eebfde592e4b0d276d2097abb0cfa3d53264da7ee9d83d8f24d7597ce2"
#endif
#ifndef SC_GUARD_TIMEOUT_MS
#define SC_GUARD_TIMEOUT_MS 20000u
#endif
static const char *sc_paths[]={
    SC_PREFIX "/etc/init.d/cc2-reactor-launch",
    SC_PREFIX "/opt/usr/cc2-reactor-runtime-v2/launcher.sh",
    SC_PREFIX "/opt/usr/cc2-reactor-runtime-v2/SHA256SUMS",
    SC_PREFIX "/opt/usr/cc2-network-manager/profile.sh",
    SC_PREFIX "/opt/usr/cc2-network-manager/reactor-entry.sh",
    SC_PREFIX "/opt/usr/cc2-network-manager/activate.sh",
    SC_PREFIX "/opt/usr/cc2-network-manager/SHA256SUMS",
    SC_PREFIX "/opt/usr/cc2-network-manager/startup.sha256"
};
static volatile sig_atomic_t sc_cancelled;
static void sc_signal(int value){(void)value;sc_cancelled=1;}
static uint64_t sc_now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t)||t.tv_sec<0)return 0;return (uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;}
static int sc_boot_current(char boot[37]){
    int fd=open(SC_BOOT_ID,O_RDONLY|O_CLOEXEC);if(fd<0)return 0;
    char bytes[38]={0};int okay=cl_io(fd,(unsigned char*)bytes,37,0);close(fd);
    if(!okay||bytes[36]!='\n')return 0;
    bytes[36]=0;memcpy(boot,bytes,37);return cl_boot_id_valid(boot);
}
static int sc_hash(const char *path,const char *digest){return strlen(digest)==64 && cl_digest_path(path,digest);}
static int sc_read_plan(struct cl_store *store,struct tx_file files[8]){
    unsigned char bytes[4097];struct stat st;
    int fd=openat(store->directory,"startup-plan",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if(fd<0)return 0;
    int okay=!fstat(fd,&st)&&st.st_size>0&&st.st_size<=4096&&cl_private(fd,0,st.st_size)&&cl_io(fd,bytes,(size_t)st.st_size,0);
    close(fd);if(!okay)return 0;
    bytes[st.st_size]=0;if(memchr(bytes,0,(size_t)st.st_size))return 0;
    const char *magic="CC2-STARTUP-PLAN-1\n";
    if(strncmp((char*)bytes,magic,strlen(magic)))return 0;
    char *line=(char*)bytes+strlen(magic);
    const char *roles[]={"dispatch","stock-entry"};const char *paths[]={SC_BASE "/boot-dispatch.sh",SC_BASE "/stock-entry.sh"};
    for(unsigned i=0;i<2;i++){
        char *end=strchr(line,'\n');if(!end)return 0;*end=0;size_t n=strlen(roles[i]);
        if(strlen(line)!=n+65 || strncmp(line,roles[i],n) || line[n]!=' ' || !cl_hex(line+n+1,64) || !sc_hash(paths[i],line+n+1))return 0;
        line=end+1;
    }
    int payload=openat(store->directory,"payload",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(!cl_private(payload,1,0)){if(payload>=0)close(payload);return 0;}
    size_t total=0;
    for(unsigned i=0;okay&&i<8;i++){
        char *end=strchr(line,'\n');if(!end){okay=0;break;}*end=0;
        struct tx_file *f=files+i;unsigned index=0,old=0,new=0;char extra=0,canonical[256];
        int fields=sscanf(line,"%u %o %o %64s %64s%c",&index,&old,&new,f->old_sha,f->new_sha,&extra);
        if(fields!=5 || index!=i || !tx_mode((mode_t)old) || !tx_mode((mode_t)new) || !tx_hex(f->old_sha) || !tx_hex(f->new_sha)){okay=0;break;}
        int n=snprintf(canonical,sizeof(canonical),"%u %04o %04o %s %s",i,old,new,f->old_sha,f->new_sha);
        if(n<1 || (size_t)n>=sizeof(canonical) || strcmp(canonical,line)){okay=0;break;}
        f->path=sc_paths[i];f->old_mode=(mode_t)old;f->new_mode=(mode_t)new;
        char name[16];snprintf(name,sizeof(name),"%u",i);unsigned char *data=NULL;
        okay=tx_read(payload,name,f->new_sha,0600,&data,&f->length);f->bytes=data;
        total+=f->length;if(total>131072u)okay=0;
        line=end+1;
    }
    if(*line)okay=0;
    close(payload);return okay;
}
static int sc_one_process(const char *name,const char *expected,const char *digest,pid_t *result){
    DIR *directory=opendir("/proc");if(!directory)return 0;
    unsigned found=0;int okay=1;
    for(;;){
        errno=0;struct dirent64 *item=readdir64(directory);
        if(!item){if(errno)okay=0;break;}
        char *end=NULL;errno=0;long value=strtol(item->d_name,&end,10);
        if(errno||!end||*end||value<=1)continue;
        char path[96],exe[1024];int n=snprintf(path,sizeof(path),"/proc/%ld/exe",value);
        if(n<1||(size_t)n>=sizeof(path)){okay=0;break;}
        ssize_t got=readlink(path,exe,sizeof(exe)-1);
        if(got<0){if(errno==ENOENT||errno==ESRCH)continue;okay=0;break;}
        if((size_t)got>=sizeof(exe)-1){okay=0;break;}exe[got]=0;
        char *base=strrchr(exe,'/');base=base?base+1:exe;
        if(strcmp(base,name)){
            size_t length=strlen(name);
            if(!strncmp(base,name,length)&&!strcmp(base+length," (deleted)")){okay=0;break;}
            continue;
        }
        /* /proc/PID/exe is intentionally a kernel link. Follow only this
         * constructed proc path after checking its exact readlink target. */
        int executable=open(path,O_RDONLY|O_CLOEXEC);struct stat st;char actual[65];
        int valid=executable>=0&&!fstat(executable,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&st.st_nlink==1&&
            !(st.st_mode&022)&&sha256_fd(executable,actual)&&!strcmp(actual,digest);
        if(executable>=0)close(executable);
        if(strcmp(exe,expected)||!valid){okay=0;break;}
        found++;*result=(pid_t)value;
    }
    closedir(directory);return okay&&found==1;
}
struct sc_ready {char magic[8];uint64_t nonce,token,deadline;uint32_t pid,phase;};
_Static_assert(sizeof(struct sc_ready)==40,"RFID ready ABI");
/* Bind both observations to the same process lifetime even before an RFID
 * capability exists. /proc stat field 22 is the kernel process start time. */
static uint64_t sc_process_start(pid_t pid){
    char path[96],bytes[4096];snprintf(path,sizeof(path),"/proc/%ld/stat",(long)pid);
    int fd=open(path,O_RDONLY|O_CLOEXEC);if(fd<0)return 0;
    ssize_t n=read(fd,bytes,sizeof(bytes)-1);close(fd);
    if(n<=0||(size_t)n>=sizeof(bytes)-1)return 0;
    bytes[n]=0;char *p=strrchr(bytes,')');if(!p||p[1]!=' ')return 0;
    p+=2;
    for(unsigned field=3;field<22;field++){
        p=strchr(p,' ');if(!p)return 0;
        while(*p==' ')p++;
    }
    if(*p<'0'||*p>'9')return 0;
    errno=0;char *end=NULL;unsigned long long value=strtoull(p,&end,10);
    return !errno&&end&&*end==' '?(uint64_t)value:0;
}
static int sc_native_profile_mode(struct sc_ready *ready,int strict,int initial){
    int stock=SC_STOCK_BOOTSTRAP&&!strict;
    int okay=1;
    if(!stock){
    int fd=open(SC_READY,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if(fd<0)return 0;
    okay=cl_private(fd,0,sizeof(*ready))&&cl_io(fd,(unsigned char*)ready,sizeof(*ready),0);close(fd);
    uint64_t now=sc_now();
    if(!okay||!now||memcmp(ready->magic,"CC2RF1\0\0",8)||!ready->nonce||ready->phase!=1||
       (ready->token&&ready->deadline>=now/1000u))return 0;
    }
    pid_t pid=0;
    if(!sc_one_process("elegoo_printer",SC_PREFIX "/opt/usr/cc2-reactor-runtime-v2/elegoo_printer",SC_NATIVE_SHA,&pid))return 0;
    if(stock){
        memset(ready,0,sizeof(*ready));ready->pid=(uint32_t)pid;
        ready->nonce=sc_process_start(pid);if(!ready->nonce)return 0;
    }else if(ready->pid!=(uint32_t)pid)return 0;
    const char *paths[]={SC_PREFIX "/opt/usr/cc2-reactor-runtime-v2/libcc2-reactor-v2.so",
        SC_PREFIX "/opt/usr/cc2-cavity-floor/libcc2-cavity-floor-r1.so",
        SC_PREFIX "/opt/usr/cc2-network-manager/libcc2-network-compat-r1.so",
        strict?SC_BASE "/libcc2-rfid.so":SC_PREFIX "/opt/usr/cc2-rfid-frame-fix-r1/libcc2-rfid-native-r2.so"};
    const char *digests[]={SC_REACTOR_SHA,SC_CAVITY_SHA,SC_NETWORK_SHA,strict?SC_STRICT_SHA:SC_OLD_RFID_SHA};
    char path[96],line[4096];snprintf(path,sizeof(path),"/proc/%ld/maps",(long)pid);
    FILE *maps=fopen(path,"r");if(!maps)return 0;unsigned seen=0;
    while(fgets(line,sizeof(line),maps)){
        char *p=strchr(line,'/');if(!p||!strstr(p,"libcc2-"))continue;
        p[strcspn(p,"\r\n")]=0;unsigned index=4;
        for(unsigned i=0;i<4;i++)if(!strcmp(paths[i],p))index=i;
        if(index==4||(!SC_WITH_CAVITY&&index==1)||(stock&&index==3)||!sc_hash(paths[index],digests[index])){okay=0;break;}
        seen|=1u<<index;
    }
    if(ferror(maps))okay=0;
    unsigned required=1u|((SC_WITH_CAVITY&&!(initial&&SC_STOCK_BOOTSTRAP&&SC_CAVITY_BOOTSTRAP_OPTIONAL))?2u:0u)|(stock?0u:8u);
    if(okay&&SC_WITH_CAVITY&&SC_VERIFY_CAVITY_CHANNEL&&!initial){
        cavity_identity cavity;char text[256];int channel=cavity_directory(0);
        okay=channel>=0&&cavity_read_at(channel,"state",text)&&cavity_identity_parse(text,&cavity)&&
            cavity.pid==(long)pid&&!cavity.printing;
        if(channel>=0)close(channel);
    }
    fclose(maps);return okay&&(seen&required)==required; /* shim optional; all mapped modules pinned */
}
static int sc_native_profile(struct sc_ready *ready,int strict){return sc_native_profile_mode(ready,strict,0);}
static int sc_native(struct sc_ready *ready){return SC_CAVITY_BOOTSTRAP_OPTIONAL?sc_native_profile_mode(ready,0,1):sc_native_profile(ready,0);}
static int sc_fresh(void){
    if(sc_cancelled||!sc_hash(SC_FRESH_PATH,SC_FRESH_SHA))return 0;
    pid_t child=fork();if(child<0)return 0;
    if(!child){if(setsid()<0)_exit(126);execl(SC_FRESH_PATH,SC_FRESH_PATH,"--check",(char*)NULL);_exit(127);}
    uint64_t now=sc_now(),deadline=now+SC_GUARD_TIMEOUT_MS;int status=0;
    for(;;){
        pid_t result=waitpid(child,&status,WNOHANG);
        if(result==child)return !sc_cancelled&&WIFEXITED(status)&&WEXITSTATUS(status)==0;
        if(result<0&&errno!=EINTR)return 0;
        if(!now||sc_cancelled||sc_now()>=deadline){
            if(kill(-child,SIGKILL)&&errno!=ESRCH)return 0;
            while(waitpid(child,&status,0)<0&&errno==EINTR){}
            return 0;
        }
        struct timespec pause={0,20000000};nanosleep(&pause,NULL);
    }
}
static int sc_backup(struct cl_store *store,int write_it){
    int fd=open(SC_ORIGINAL,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);if(fd<0)return 0;
    struct stat st;unsigned char *stock=malloc(AP_SIZE);int okay=stock&&!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==0&&st.st_nlink==1&&st.st_size==AP_SIZE&&
        cl_io(fd,stock,AP_SIZE,0)&&ap_image(stock,AP_SIZE,0);close(fd);
    if(okay&&write_it)okay=tx_save(store->directory,"original-stock-44.bin",stock,AP_SIZE,AP_STOCK_SHA);
    free(stock);return okay;
}
int main(int argc,char **argv){
    int stage=argc==2&&!strcmp(argv[1],"--stage-host");
    if(!stage&&(argc!=2||strcmp(argv[1],"--check"))){fprintf(stderr,"STOP: unsupported_host_stage_action\n");return 1;}
    if(strlen(SC_PROFILE_SHA)!=64||strlen(SC_APP_SHA)!=64||strlen(SC_FRESH_SHA)!=64||
       !tx_hex(SC_PROFILE_SHA)||!tx_hex(SC_APP_SHA)||!tx_hex(SC_FRESH_SHA)){fprintf(stderr,"STOP: host_stage_unconfigured\n");return 1;}
    if(geteuid()!=0||(getenv("LD_PRELOAD")&&*getenv("LD_PRELOAD")))return 1;
    signal(SIGINT,sc_signal);signal(SIGTERM,sc_signal);signal(SIGHUP,sc_signal);signal(SIGPIPE,SIG_IGN);umask(077);
    struct maintenance_barrier barrier=MAINTENANCE_BARRIER_INIT;
    if(!maintenance_barrier_acquire(&barrier)){fprintf(stderr,"STOP: installer_or_maintenance_busy\n");return 1;}
    struct cl_store store;struct tx_file files[8];memset(files,0,sizeof(files));
    int okay=cl_store_open(&store,SC_BASE);struct cl_record expected;memset(&expected,0,sizeof(expected));
    for(unsigned i=0;i<32;i++){unsigned value=0;if(sscanf(SC_PROFILE_SHA+2*i,"%2x",&value)!=1){okay=0;break;}expected.plan[i]=(unsigned char)value;}
    const char *phase="profile_and_live_preflight";
    struct statfs fs;pid_t app=0;struct sc_ready first={0},last={0};
    okay=okay&&!fstatfs(store.directory,&fs)&&(unsigned long)fs.f_type!=0x01021994ul&&(unsigned long)fs.f_type!=0x858458f6ul&&
        cl_profile(&store,&expected)&&sc_read_plan(&store,files)&&sc_hash(SC_PREFIX "/opt/bin/run_printer.sh",SC_RUN_SHA)&&
        sc_one_process("cc2-control",SC_PREFIX "/opt/usr/cc2-control/cc2-control",SC_APP_SHA,&app)&&sc_native(&first)&&sc_fresh()&&sc_backup(&store,0);
    struct cl_record record={0};int have_journal=0;
    if(okay){
        phase="journal_and_target_preflight";
        struct stat st;int exists=fstatat(store.directory,"lifecycle",&st,AT_SYMLINK_NOFOLLOW)==0;
        if(!exists&&errno!=ENOENT)okay=0;
        if(exists){have_journal=cl_store_read(&store,&record);okay=have_journal&&(record.phase==CL_HOST_STAGED||record.phase==CL_DISABLED)&&!memcmp(record.plan,expected.plan,32);}
        for(unsigned i=0;okay&&i<8;i++){int state=tx_current(files+i);if(state<0||((!have_journal||record.phase==CL_DISABLED)&&state!=0))okay=0;}
    }
    if(okay&&stage){
        phase="durable_backup_and_second_live_guard";
        pid_t last_app=0;
        okay=sc_backup(&store,1)&&sc_fresh()&&sc_native(&last)&&
            sc_one_process("cc2-control",SC_PREFIX "/opt/usr/cc2-control/cc2-control",SC_APP_SHA,&last_app)&&
            app==last_app&&first.pid==last.pid&&first.nonce==last.nonce&&!sc_cancelled;
        if(okay&&!have_journal){
            phase="host_staged_journal_commit";
            record=expected;record.phase=CL_HOST_STAGED;record.generation=1;
            okay=sc_boot_current(record.boot);
            int random=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
            if(okay)okay=random>=0&&cl_io(random,record.transaction,16,0)&&cl_nonzero(record.transaction,16);
            if(random>=0)close(random);
            if(okay)okay=cl_store_commit(&store,&record,0);
        }else if(okay){
            char current[37];okay=sc_boot_current(current);
            if(okay&&record.phase==CL_DISABLED){
                struct cl_facts facts={.profile=1,.backup=1,.barrier=1,.idle=1,.cold=1,.guard=1,.host_staged=1};
                okay=cl_advance(&store,CL_STAGE_HOST,&facts,current);
            }else if(okay&&strcmp(record.boot,current)){
                /* Old native still runs: this resumed publication needs a
                 * subsequent complete boot, not merely the interrupted boot. */
                uint64_t previous=record.generation;
                if(previous==UINT64_MAX)okay=0;
                else{record.generation++;memcpy(record.boot,current,37);okay=cl_store_commit(&store,&record,previous);}
            }
        }
        if(okay){phase="startup_file_transaction";okay=tx_prepare(&store,files,8)&&!sc_cancelled&&tx_run(&store,files,8,0);}
    }
    for(unsigned i=0;i<8;i++)free((void*)files[i].bytes);
    cl_store_close(&store);
    if(!maintenance_barrier_release(&barrier)){okay=0;phase="operation_lock_release";}
    if(!okay){fprintf(stderr,"STOP: qualified_host_stage_failed phase=%s; retain component backup and journal\n",phase);return 1;}
    puts(stage?"HOST STAGED: original backup and HOST_STAGED intent durable; eight targets committed. Full idle-printer reboot required; no process stopped or Canvas write performed.":
        "HOST STAGE CHECK PASS: exact profile, running barrier-capable app/native, fresh idle/cold and original image verified; no startup target changed.");
    return 0;
}
