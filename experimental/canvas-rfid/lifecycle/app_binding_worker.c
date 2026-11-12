/* Configured app-only upgrade worker. No transfer action is reachable from
 * this entry point. Reuse the coordinator's exact read-only native inspection;
 * the ordinary transfer entry is discarded by section garbage collection. */
#define VC_ENTRY au_unused_coordinator_entry
#include "coordinator.c"
#undef VC_ENTRY
#include "app_binding_tx.h"
#include "app_binding_observation.h"
#ifndef AU_WORKER_CONFIG
#error A qualified old/new app binding configuration is required
#endif
#include AU_WORKER_CONFIG
/* AU_WORKER_CONFIG supplies static const struct au_plan au_expected. */
struct au_owner {char boot[37];long pid;unsigned long long start,device,inode;};
static int aw_owner_bytes(const struct au_owner *owner,char bytes[512]){
    if(!cl_boot_id_valid(owner->boot)||owner->pid<=1||!owner->start)return 0;
    int n=snprintf(bytes,512,"CC2RFOWN1 %s %ld %llu %llx %llx\n",owner->boot,owner->pid,owner->start,owner->device,owner->inode);
    if(n<1||n>=440)return 0;
    sha256_state hash;char digest[65];sha256_init(&hash);sha256_update(&hash,(unsigned char*)bytes,(size_t)n);sha256_hex(&hash,digest);
    memcpy(bytes+n,digest,64);bytes[n+64]='\n';return n+65;
}
static int aw_owner_read(struct au_store *store,struct au_owner *owner){
    int fd=openat(store->directory,"owner",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);struct stat st;
    char bytes[512],canonical[512];
    int okay=fd>=0&&!fstat(fd,&st)&&st.st_size>0&&st.st_size<(off_t)sizeof(bytes)&&cl_private(fd,0,st.st_size)&&
        cl_io(fd,(unsigned char*)bytes,(size_t)st.st_size,0);
    if(fd>=0)close(fd);
    if(!okay)return 0;
    bytes[st.st_size]=0;memset(owner,0,sizeof(*owner));
    if(sscanf(bytes,"CC2RFOWN1 %36s %ld %llu %llx %llx",owner->boot,&owner->pid,&owner->start,&owner->device,&owner->inode)!=5)return 0;
    int n=aw_owner_bytes(owner,canonical);
    return n==st.st_size&&!memcmp(bytes,canonical,(size_t)n);
}
static int aw_owner_alive(const struct au_owner *owner){
    char boot[37];return vc_boot(boot)&&!strcmp(boot,owner->boot)&&sc_process_start((pid_t)owner->pid)==owner->start;
}
static int aw_owner_matches(const struct au_owner *owner,int held){
    struct stat st;char boot[37];
    return au_barrier(held)&&!fstat(held,&st)&&vc_boot(boot)&&!strcmp(boot,owner->boot)&&
        (unsigned long long)st.st_dev==owner->device&&(unsigned long long)st.st_ino==owner->inode;
}
static int aw_owner_save(struct au_store *store,int held){
    struct au_owner owner,previous;struct stat st;
    if(!au_barrier(held)||fstat(held,&st))return 0;
    memset(&owner,0,sizeof(owner));owner.pid=(long)getppid();owner.start=sc_process_start(getppid());
    owner.device=(unsigned long long)st.st_dev;owner.inode=(unsigned long long)st.st_ino;
    if(!vc_boot(owner.boot))return 0;
    if(!fstatat(store->directory,"owner",&st,AT_SYMLINK_NOFOLLOW)){
        if(!aw_owner_read(store,&previous))return 0;
        if(aw_owner_alive(&previous)&&!aw_owner_matches(&previous,held))return 0;
    }else if(errno!=ENOENT)return 0;
    char bytes[512],temp[80];int n=aw_owner_bytes(&owner,bytes);
    if(!n)return 0;
    int fd=au_temporary(store->directory,temp);
    int okay=fd>=0&&cl_io(fd,(unsigned char*)bytes,(size_t)n,1)&&!fsync(fd);
    if(fd>=0&&close(fd))okay=0;
    if(okay)okay=!renameat(store->directory,temp,store->directory,"owner")&&!fsync(store->directory);
    return okay;
}
static int aw_transaction_open(struct au_store *store){
    memset(store,0,sizeof(*store));store->directory=store->lock=store->native.directory=store->native.lock=-1;
    store->directory=open(AU_DIRECTORY,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(!au_directory_private(store->directory))goto bad;
    store->lock=openat(store->directory,"binding.lock",O_RDWR|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(!cl_private(store->lock,0,0)||flock(store->lock,LOCK_EX|LOCK_NB))goto bad;
    return 1;
bad:
    au_close(store);return 0;
}
static int aw_state(struct au_store *store,struct au_record *record){
    char plan[65];return au_plan_hash(&au_expected,plan)&&au_read_state(store,record)&&!strcmp(record->plan,plan);
}
static int aw_live_app(struct au_store *store,const char *app_sha){
#ifdef AU_PRESERVE_LIFECYCLE
    char journal[65];int busy=0;pid_t app=0;
#ifdef AU_REQUIRE_DISABLED
    struct cl_record record;struct sc_ready ready={0};
    if(!cl_store_read(&store->native,&record)||record.phase!=CL_DISABLED||
        !cl_profile(&store->native,&record)||!vc_targets_state(&store->native,0)||
        !vc_backup(&store->native)||!sc_native_profile(&ready,0))return 0;
#endif
    return au_match(store->native.directory,"recovery-profile",au_expected.profile,0600)&&
        au_native(store,&au_expected,journal)&&
        vc_lock_busy(store->native.directory,"worker.lock",1,&busy)&&!busy&&
        sc_one_process("cc2-control",AU_APP,app_sha,&app)&&!sc_cancelled;
#else
    struct cl_record record;struct sc_ready first={0},last={0};pid_t app=0,last_app=0;
    return cl_store_read(&store->native,&record)&&record.phase==CL_ACTIVE&&vc_profile_bound(&record)&&
        cl_profile(&store->native,&record)&&vc_backup(&store->native)&&vc_targets(&store->native)&&
        sc_hash(SC_PREFIX "/opt/bin/run_printer.sh",SC_RUN_SHA)&&
        sc_one_process("cc2-control",AU_APP,app_sha,&app)&&
        sc_native_profile(&first,1)&&aw_fresh_ready()&&aw_fresh_ready()&&sc_native_profile(&last,1)&&
        sc_one_process("cc2-control",AU_APP,app_sha,&last_app)&&
        app==last_app&&first.pid==last.pid&&first.nonce==last.nonce&&!sc_cancelled;
#endif
}
static int aw_prepare(struct au_store *store,int source,int held){
    struct stat st;struct au_record record;char journal[65];
    if(!fstatat(store->directory,"state",&st,AT_SYMLINK_NOFOLLOW)){
        if(!aw_state(store,&record))return 0;
        if(record.phase==AU_ROLLED_BACK){
            /* A retry retains the same snapshots and monotonic generation.
             * Only the exact old app/profile and complete reversed files qualify. */
            if(!au_barrier(held)||!au_native(store,&au_expected,journal)||strcmp(record.journal,journal))return 0;
            for(unsigned i=0;i<AU_COUNT;i++){
                const struct au_file *file=au_expected.files+i;char name[32];
                snprintf(name,sizeof(name),"old.%u",i);
                if(au_current(store,file)!=0||!au_match(store->directory,name,file->before,0600))return 0;
                snprintf(name,sizeof(name),"new.%u",i);
                if(!au_match(store->directory,name,file->after,0600)||
                    !au_match(source,file->name,file->after,file->mode))return 0;
            }
            if(!au_write_state(store,&record,AU_PREPARED))return 0;
        }
    }else if(errno!=ENOENT)return 0;
    return au_prepare(store,&au_expected,source,held);
}
static int aw_check_current(int held){
    struct au_store store;memset(&store,0,sizeof(store));
    store.directory=store.lock=store.native.directory=store.native.lock=-1;
    if(!au_barrier(held)||!au_app(au_expected.new_app)||!cl_store_open(&store.native,AU_COMPONENT))return 0;
    int okay=aw_live_app(&store,au_expected.new_app);
    for(unsigned i=0;okay&&i<AU_COUNT;i++)okay=au_current(&store,au_expected.files+i)==1;
    au_close(&store);return okay;
}
static int aw_any_app(void){
    DIR *directory=opendir("/proc");if(!directory)return 1;
    int found=0;
    for(;;){
        errno=0;struct dirent64 *item=readdir64(directory);
        if(!item){if(errno)found=1;break;}
        char *end=NULL;errno=0;long pid=strtol(item->d_name,&end,10);
        if(errno||!end||*end||pid<=1)continue;
        char path[96],exe[1024];snprintf(path,sizeof(path),"/proc/%ld/exe",pid);
        ssize_t n=readlink(path,exe,sizeof(exe)-1);
        if(n<0){if(errno==ENOENT||errno==ESRCH)continue;found=1;break;}
        if((size_t)n>=sizeof(exe)-1){found=1;break;}exe[n]=0;
        char *base=strrchr(exe,'/');base=base?base+1:exe;
        if(!strcmp(base,"cc2-control")||!strcmp(base,"cc2-control (deleted)")){found=1;break;}
    }
    closedir(directory);return found;
}
static int aw_finish(struct au_store *store,int held){
#ifdef AU_PRESERVE_LIFECYCLE
    struct au_record record;char journal[65];
    if(!au_barrier(held)||!aw_state(store,&record)||!au_app(au_expected.new_app)||
        !au_native(store,&au_expected,journal)||strcmp(record.journal,journal)||
        !aw_live_app(store,au_expected.new_app))return 0;
#else
    struct au_record record;struct cl_record native;struct sc_ready identity={0};char journal[65];pid_t app=0;
    if(!au_barrier(held)||!aw_state(store,&record)||!au_app(au_expected.new_app)||
        !au_native(store,&au_expected,journal)||strcmp(record.journal,journal)||
        !sc_one_process("cc2-control",AU_APP,au_expected.new_app,&app)||
        !cl_store_read(&store->native,&native)||!cl_profile(&store->native,&native)||
        !vc_backup(&store->native)||!vc_targets(&store->native)||!sc_native_profile(&identity,1))return 0;
#endif
    if(record.phase!=AU_APPLIED&&record.phase!=AU_COMMITTED)return 0;
    for(unsigned i=0;i<AU_COUNT;i++)if(au_current(store,au_expected.files+i)!=1)return 0;
    return record.phase==AU_COMMITTED||au_write_state(store,&record,AU_COMMITTED);
}
static int aw_descriptor(struct au_store *store){
    int fd=openat(store->directory,"recovery.json",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);struct stat st;
    char bytes[2049],worker_sha[65],plan_sha[65];
    int okay=fd>=0&&!fstat(fd,&st)&&st.st_size>0&&st.st_size<=2048&&cl_private(fd,0,st.st_size)&&
        cl_io(fd,(unsigned char*)bytes,(size_t)st.st_size,0);
    if(fd>=0)close(fd);
    if(!okay)return 0;
    bytes[st.st_size]=0;ng_json *json=malloc(sizeof(*json));if(!json)return 0;double protocol=0;
    okay=ng_json_parse(json,bytes,(size_t)st.st_size)&&json->v[0].type=='{'&&json->v[0].count==4&&
        ng_number(json,0,"protocol",&protocol)&&protocol==1;
    const char *app=ng_string(json,0,"app_sha256"),*worker=ng_string(json,0,"worker_sha256"),*plan=ng_string(json,0,"plan_sha256");
    okay=okay&&au_hex(app)&&au_hex(worker)&&au_hex(plan)&&!strcmp(app,au_expected.new_app)&&
        au_plan_hash(&au_expected,plan_sha)&&!strcmp(plan,plan_sha);
    if(okay)memcpy(worker_sha,worker,65);
    free(json);if(!okay)return 0;
    return au_match(store->directory,"worker",worker_sha,0700);
}
static int aw_recover(void){
    struct au_store store;struct au_record record;struct au_owner owner;pid_t app=0;
    if(!au_app(au_expected.new_app)||!sc_one_process("cc2-control",AU_APP,au_expected.new_app,&app)||app!=getppid())return 0;
    if(!aw_transaction_open(&store))return 0;
    int okay=aw_state(&store,&record)&&aw_descriptor(&store);
    if(okay&&record.phase==AU_COMMITTED){
        /* Completed app updates must not obstruct a later legitimate native
         * restore/removal. No pending replacement remains to recover. */
        if(aw_owner_read(&store,&owner)&&!aw_owner_alive(&owner)){
            int stale=open(AU_BARRIER,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
            if(stale>=0){if(aw_owner_matches(&owner,stale))rmdir(AU_BARRIER);close(stale);}
        }
        au_close(&store);return 1;
    }
    okay=okay&&cl_store_open(&store.native,AU_COMPONENT)&&aw_owner_read(&store,&owner);
    struct maintenance_barrier own=MAINTENANCE_BARRIER_INIT;int held=-1,borrowed=0;
    if(okay){
        held=open(AU_BARRIER,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        if(held>=0){borrowed=1;okay=aw_owner_matches(&owner,held);}
        else if(errno==ENOENT){okay=maintenance_barrier_acquire(&own);held=own.fd;}
        else okay=0;
    }
    if(okay)okay=au_run(&store,&au_expected,held,0);
    if(borrowed&&held>=0)close(held);
    if(own.fd>=0&&!maintenance_barrier_release(&own))okay=0;
    au_close(&store);return okay;
}
static int aw_retire_inactive(struct au_store *store,int held){
    struct stat st;struct au_record record;struct au_owner owner;char journal[65];
    if(!au_barrier(held)||!aw_descriptor(store)||!aw_live_app(store,au_expected.old_app))return 0;
    for(unsigned i=0;i<AU_COUNT;i++)if(au_current(store,au_expected.files+i)!=0)return 0;
    if(!fstatat(store->directory,"state",&st,AT_SYMLINK_NOFOLLOW)){
        if(!aw_state(store,&record)||record.phase!=AU_ROLLED_BACK||
           !au_native(store,&au_expected,journal)||strcmp(record.journal,journal))return 0;
        for(unsigned i=0;i<AU_COUNT;i++){
            char name[32];snprintf(name,sizeof(name),"old.%u",i);
            if(!au_match(store->directory,name,au_expected.files[i].before,0600))return 0;
        }
    }else if(errno!=ENOENT)return 0;
    if(!fstatat(store->directory,"owner",&st,AT_SYMLINK_NOFOLLOW)){
        if(!aw_owner_read(store,&owner)||aw_owner_alive(&owner))return 0;
    }else if(errno!=ENOENT)return 0;
    char path[1024];int n=snprintf(path,sizeof(path),"%s",AU_DIRECTORY);
    if(n<1||(size_t)n>=sizeof(path))return 0;
    char *name=strrchr(path,'/');if(!name||strcmp(name+1,au_expected.new_app))return 0;
    *name++=0;
    int parent=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);struct stat current,opened;
    int okay=au_directory_private(parent)&&!fstat(store->directory,&opened)&&
        !fstatat(parent,name,&current,AT_SYMLINK_NOFOLLOW)&&S_ISDIR(current.st_mode)&&
        current.st_dev==opened.st_dev&&current.st_ino==opened.st_ino;
    uint64_t nonce=0;int random=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
    okay=okay&&random>=0&&cl_io(random,(unsigned char*)&nonce,sizeof(nonce),0)&&nonce;
    if(random>=0)close(random);
    char archive[128];n=snprintf(archive,sizeof(archive),".retired-%s-%ld-%016llx",name,(long)getpid(),(unsigned long long)nonce);
    okay=okay&&n>0&&(size_t)n<sizeof(archive)&&au_barrier(held);
    if(okay)okay=!syscall(SYS_renameat2,parent,name,parent,archive,1u)&&!fsync(parent);
    if(okay)printf("INACTIVE APP PREPARATION ARCHIVED=%s/%s; native state and backups retained.\n",path,archive);
    if(parent>=0)close(parent);
    return okay;
}
int main(int argc,char **argv){
    if(geteuid()!=0||(getenv("LD_PRELOAD")&&*getenv("LD_PRELOAD"))||
        strcmp(SC_APP_SHA,au_expected.old_app)||strcmp(SC_PROFILE_SHA,au_expected.profile))return 1;
    signal(SIGINT,sc_signal);signal(SIGTERM,sc_signal);signal(SIGHUP,sc_signal);signal(SIGPIPE,SIG_IGN);umask(077);
    if(argc==2&&!strcmp(argv[1],"--recover")){
        if(aw_recover()){puts("APP BINDING RECOVERED: native journal, startup, guard and Canvas unchanged.");return 0;}
        fputs("STOP: app_binding_recovery_incomplete; retain transaction and backups\n",stderr);return 1;
    }
    int prepare=argc==4&&!strcmp(argv[1],"--prepare"),rollback=argc==3&&!strcmp(argv[1],"--rollback"),commit=argc==3&&!strcmp(argv[1],"--commit"),current=argc==3&&!strcmp(argv[1],"--check-current"),retire=argc==3&&!strcmp(argv[1],"--retire-inactive");
    if(!prepare&&!rollback&&!commit&&!current&&!retire)return 2;
    const char *number=argv[prepare?3:2];char *end=NULL;errno=0;long value=strtol(number,&end,10);
    if(errno||!number[0]||!end||*end||value<3||value>1048576)return 2;
    int held=(int)value;
    if(!au_barrier(held))return 1;
    if(current)return aw_check_current(held)?0:1;
    struct au_store store;
    if(!au_open(&store,prepare))return 1;
    int okay=0;
    if(retire)okay=aw_retire_inactive(&store,held);
    else if(prepare){
        int source=open(argv[2],O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
        okay=source>=0&&aw_descriptor(&store)&&aw_live_app(&store,au_expected.old_app)&&
            aw_prepare(&store,source,held)&&aw_owner_save(&store,held);
        if(source>=0)close(source);
    }else if(rollback)okay=!aw_any_app()&&au_run(&store,&au_expected,held,1);
    else okay=aw_finish(&store,held);
    au_close(&store);
    if(!okay){fputs("STOP: qualified_app_binding_action_refused; retain transaction and backups\n",stderr);return 1;}
    if(retire)return 0;
    puts(prepare?"APP BINDING PREPARED: private recovery snapshots committed; application and native state unchanged.":
        rollback?"APP BINDING ROLLED BACK: exact prior app-specific files restored; native journal preserved.":
        "APP BINDING COMMITTED: new app and runtime binding verified; no Canvas transfer performed.");
    return 0;
}
