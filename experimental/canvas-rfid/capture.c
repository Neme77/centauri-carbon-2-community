#define _GNU_SOURCE
#include "rfid_wire.h"
#include "sha256.h"
#include <signal.h>
#include <stdlib.h>
#include <sys/wait.h>

#ifndef CT_MODULE
#define CT_MODULE "/opt/usr/cc2-rfid-native-r2/libcc2-rfid-native-r2.so"
#endif
#ifndef CT_MODULE_SHA
#define CT_MODULE_SHA "3ab8811b4a7e7ca1d3e4f6f32d2708227fb9c6e09a82650e3d8d14ae85d98ea7"
#endif
#ifndef CT_EXE_SHA
#define CT_EXE_SHA "19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745"
#endif
#ifndef CT_GUARD
#define CT_GUARD "/opt/usr/cc2-rfid-native-r2/manage.sh"
#endif
#ifndef CT_GUARD_SHA
#define CT_GUARD_SHA "f1a38d4729d07e9cb86a1f495a1564764e808f97847f4c48b75d7ca0b4b05b0d"
#endif
#ifndef CT_GUARD_NATIVE
#define CT_GUARD_NATIVE 0
#endif
#ifndef CT_GUARD_ARGUMENT
#if CT_GUARD_NATIVE
#define CT_GUARD_ARGUMENT "--check-active"
#else
#define CT_GUARD_ARGUMENT "check"
#endif
#endif
#ifndef CT_LOGROOT
#define CT_LOGROOT "/opt/usr/cc2-rfid-capture-r1a"
#endif
#ifndef CT_SCAN_SECONDS
#define CT_SCAN_SECONDS 60
#endif
#ifndef CT_RETRY_MILLISECONDS
#define CT_RETRY_MILLISECONDS 2000
#endif
_Static_assert(CT_SCAN_SECONDS>=1 && CT_SCAN_SECONDS<=RF_MAX_SCAN_SECONDS,"bounded explicit scan window");
static volatile sig_atomic_t ct_interrupted;
static void ct_signal(int n){(void)n;ct_interrupted=1;}
static int ct_fail(const char *reason){fprintf(stderr,"STOP: %s\n",reason);return 1;}
static int ct_digest(const char *path,const char *expected){
    char digest[65];int fd=open(path,O_RDONLY|O_CLOEXEC);
    if(fd<0)return 0;
    int valid=sha256_fd(fd,digest);close(fd);
    return valid && !strcmp(digest,expected);
}
static int ct_header(const struct rf_wire *h){
    return !memcmp(h->magic,"CC2RF1\0\0",8) && h->nonce && h->pid>1 && h->phase<=2;
}
static int ct_existing_channel(void){
    struct stat st;
    /* Status must not create a channel for an absent native module. */
    if(rf_fstatat(AT_FDCWD,RF_CHANNEL,&st,AT_SYMLINK_NOFOLLOW) || !S_ISDIR(st.st_mode) || st.st_uid!=0 || (st.st_mode&077))return -1;
    return rf_directory();
}
static int ct_ready(int dir,struct rf_wire *h){return rf_read(dir,"ready",h,sizeof(*h)) && ct_header(h);}
static int ct_native(const struct rf_wire *h){
    char path[96],line[2048];
    int n=snprintf(path,sizeof(path),"/proc/%u/exe",h->pid);
    if(n<1 || (size_t)n>=sizeof(path) || !ct_digest(path,CT_EXE_SHA) || !ct_digest(CT_MODULE,CT_MODULE_SHA))return 0;
    n=snprintf(path,sizeof(path),"/proc/%u/maps",h->pid);
    if(n<1 || (size_t)n>=sizeof(path))return 0;
    FILE *file=fopen(path,"r");if(!file)return 0;
    int mapped=0,bad=0;
    while(fgets(line,sizeof(line),file))if(strstr(line,CT_MODULE)){mapped=1;if(strstr(line,"(deleted)"))bad=1;}
    if(ferror(file))bad=1;
    fclose(file);return mapped && !bad;
}
static void ct_sleep(unsigned milliseconds){
    struct timespec duration={(time_t)(milliseconds/1000),(long)(milliseconds%1000)*1000000};
    while(!ct_interrupted && nanosleep(&duration,&duration) && errno==EINTR){}
}
static int ct_guard_once(unsigned attempt,int *stale){
    *stale=0;
    if(!ct_digest(CT_GUARD,CT_GUARD_SHA))return 0;
    int dir=open(CT_LOGROOT,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    struct stat st;
    if(dir<0)return 0;
    if(rf_fstat(dir,&st) || st.st_uid!=0 || (st.st_mode&077)){close(dir);return 0;}
    struct timespec stamp;
    if(clock_gettime(CLOCK_MONOTONIC,&stamp)){close(dir);return 0;}
    char name[96];int length=snprintf(name,sizeof(name),"guard-%ld-%llu-%09lu-%u.log",(long)getpid(),(unsigned long long)stamp.tv_sec,(unsigned long)stamp.tv_nsec,attempt);
    if(length<1 || (size_t)length>=sizeof(name)){close(dir);return 0;}
    int log=openat(dir,name,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    close(dir);if(log<0)return 0;
    pid_t child=fork();
    if(child<0){close(log);return 0;}
    if(!child){
        signal(SIGINT,SIG_DFL);signal(SIGTERM,SIG_DFL);
        if(setsid()<0 || dup2(log,STDOUT_FILENO)<0 || dup2(log,STDERR_FILENO)<0)_exit(127);
        close(log);
#if CT_GUARD_NATIVE
        execl(CT_GUARD,CT_GUARD,CT_GUARD_ARGUMENT,(char*)NULL);
#else
        execl("/bin/sh","sh",CT_GUARD,CT_GUARD_ARGUMENT,(char*)NULL);
#endif
        _exit(127);
    }
    uint64_t deadline=rf_now()+45;int status=0,finished=0;
    while(!ct_interrupted && rf_now()<deadline){
        pid_t result=waitpid(child,&status,WNOHANG);
        if(result==child){finished=1;break;}
        if(result<0 && errno!=EINTR)break;
        ct_sleep(20);
    }
    if(!finished){
#if CT_GUARD_NATIVE
        /* The configured coordinator may own the shared installer directory.
         * Let its cooperative cleanup release that directory; killing it here
         * would recreate the stale-lock failure fixed in the earlier trial. */
        uint64_t next=0;
        for(;;){
            if(rf_now()>=next){
                if(kill(-child,SIGTERM)&&errno!=ESRCH)fprintf(stderr,"guard_cancel_group_failed\n");
                if(kill(child,SIGTERM)&&errno!=ESRCH)fprintf(stderr,"guard_cancel_failed\n");
                next=rf_now()+1;
            }
            pid_t done=waitpid(child,&status,WNOHANG);
            if(done==child||(done<0&&errno!=EINTR))break;
            struct timespec pause={0,20000000};nanosleep(&pause,NULL);
        }
#else
        /* Only this freshly forked guard and its private process group. */
        if(kill(-child,SIGKILL) && errno!=ESRCH)fprintf(stderr,"guard_group_cleanup_failed\n");
        if(kill(child,SIGKILL) && errno!=ESRCH)fprintf(stderr,"guard_cleanup_failed\n");
        while(waitpid(child,&status,0)<0 && errno==EINTR){}
#endif
    }
    char message[8193];ssize_t count=pread(log,message,sizeof(message)-1,0);
    int okay=finished && WIFEXITED(status) && WEXITSTATUS(status)==0;
    if(count>=0 && count<(ssize_t)sizeof(message)-1){
        message[count]=0;
        if(!okay)*stale=!strcmp(message,"STOP: stale_telemetry\n");
        if(count && fwrite(message,1,(size_t)count,okay?stdout:stderr)!=(size_t)count)okay=0;
    }else okay=0;
    close(log);return okay;
}
static int ct_guard(void){
    for(unsigned n=1;n<=3 && !ct_interrupted;n++){
        int stale=0;
        if(ct_guard_once(n,&stale))return 1;
        if(!stale || n==3)return 0;
        puts("Waiting briefly for fresh telemetry; all Idle/cold guards remain active.");fflush(stdout);
        ct_sleep(CT_RETRY_MILLISECONDS);
    }
    return 0;
}
static int ct_same(const struct rf_wire *a,const struct rf_wire *b){return a->pid==b->pid && a->nonce==b->nonce;}
static int ct_safe_endpoint(int dir,const char *name,size_t size){
    struct stat st;
    if(rf_fstatat(dir,name,&st,AT_SYMLINK_NOFOLLOW))return errno==ENOENT;
    return S_ISREG(st.st_mode) && st.st_uid==0 && st.st_nlink==1 && !(st.st_mode&077) && st.st_size>=0 && (uint64_t)st.st_size==size;
}
static void ct_cancel_own(int dir,const struct rf_wire *owner,uint64_t token){
    struct rf_wire ready,pending;
    if(!ct_ready(dir,&ready) || !ct_same(&ready,owner))return;
    if(rf_read(dir,"request",&pending,sizeof(pending)) && ct_same(&pending,owner) && pending.token==token){
        unlinkat(dir,"request",0);
    }
    if(ready.token==token && ready.phase==1){
        struct rf_wire cancel=*owner;
        cancel.token=token;cancel.phase=2;
        if(!rf_write(dir,"request",&cancel,sizeof(cancel)))fprintf(stderr,"capture_cancel_not_queued\n");
    }
}
static void ct_hex(const unsigned char *bytes,size_t size){for(size_t i=0;i<size;i++)printf("%02x",bytes[i]);}
int main(int argc,char **argv){
    if(geteuid()!=0)return ct_fail("root_required");
    if(argc!=2 || (strcmp(argv[1],"--status") && strcmp(argv[1],"--check") && strcmp(argv[1],"--scan")))return ct_fail("expected_status_check_or_scan");
    signal(SIGINT,ct_signal);signal(SIGTERM,ct_signal);signal(SIGPIPE,SIG_IGN);
    int dir=ct_existing_channel();struct rf_wire initial;
    if(dir<0)return ct_fail("native_channel_absent_or_unsafe");
    if(!ct_ready(dir,&initial) || !ct_native(&initial)){close(dir);return ct_fail("qualified_R2_host_not_running");}
    if(!strcmp(argv[1],"--status")){
        printf("RFID_PID=%u PHASE=%u CAPTURE_READY=%u\n",initial.pid,initial.phase,initial.phase==1);close(dir);return 0;
    }
    if(initial.phase!=1){close(dir);return ct_fail("native_not_idle_capture_disabled");}
    if(!ct_guard()){close(dir);return ct_fail("fresh_idle_cold_guard_failed");}
    struct rf_wire current;
    if(!ct_ready(dir,&current) || !ct_same(&current,&initial) || current.phase!=1 || ct_interrupted){close(dir);return ct_fail("native_changed_capture_not_armed");}
    if(!strcmp(argv[1],"--check")){puts("CAPTURE CHECK PASS: qualified R2, fresh Idle/cold; no scan armed, no serial access.");close(dir);return 0;}
    if(mkdirat(dir,"scan-lock",0700)){close(dir);return ct_fail("capture_session_locked");}
    struct stat lock_stat,latest_stat;
    int locked=!rf_fstatat(dir,"scan-lock",&lock_stat,AT_SYMLINK_NOFOLLOW) && S_ISDIR(lock_stat.st_mode) && lock_stat.st_uid==0 && !(lock_stat.st_mode&077);
    int result=1;uint64_t token=0;
    if(!locked){ct_fail("capture_lock_unsafe");goto done;}
    if(!ct_ready(dir,&current) || !ct_same(&current,&initial) || current.phase!=1 || ct_interrupted){ct_fail("native_changed_capture_not_armed");goto done;}
    if(!ct_safe_endpoint(dir,"request",sizeof(struct rf_wire)) || !ct_safe_endpoint(dir,"result",sizeof(struct rf_wire_result))){ct_fail("unsafe_capture_endpoint");goto done;}
    {
        int random=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
        if(random<0){ct_fail("random_source_unavailable");goto done;}
        ssize_t count=read(random,&token,sizeof(token));close(random);
        if(count!=(ssize_t)sizeof(token) || !token){ct_fail("invalid_scan_token");token=0;goto done;}
        struct rf_wire request=initial;
        request.token=token;request.deadline=rf_now()+CT_SCAN_SECONDS;request.phase=1;
        if(!rf_write(dir,"request",&request,sizeof(request))){ct_fail("capture_request_failed");goto done;}
        int native_request_accepted=0,result_record_seen=0;
        printf("SCAN ARMED: host capture window open; present the tag to the Canvas reader. Waiting up to %u seconds; no settings will be applied.\n",(unsigned)CT_SCAN_SECONDS);fflush(stdout);
        while(!ct_interrupted && rf_now()<request.deadline){
            if(!ct_ready(dir,&current) || !ct_same(&current,&initial) || current.phase!=1){ct_fail("native_state_changed_scan_cancelled");goto done;}
            if(!native_request_accepted && current.token==token && current.deadline==request.deadline){
                native_request_accepted=1;
                puts("NATIVE CAPTURE REQUEST ACCEPTED: matching token/deadline observed in R2 capability; this does not establish tag presence.");fflush(stdout);
            }
            struct rf_wire_result capture;
            int have_record=rf_read(dir,"result",&capture,sizeof(capture));
            if(have_record)result_record_seen=1;
            if(have_record && ct_header(&capture.header) && ct_same(&capture.header,&initial) &&
               capture.header.token==token && capture.header.deadline==request.deadline && capture.header.phase==1){
                unsigned nonzero=0;for(unsigned i=0;i<8;i++)nonzero|=capture.uid[i];
                int native_magic=!memcmp(capture.tag+64,"\x36\xee\xee\xee",4);
                if(!nonzero || !((capture.kind==RF_ELEGOO && native_magic) || (capture.kind==RF_FOREIGN && !native_magic))){ct_fail("invalid_owned_capture");goto done;}
                printf("RFID_RESULT={\"kind\":\"%s\",\"uid_hex\":\"",capture.kind==RF_ELEGOO?"native_elegoo":"foreign");
                ct_hex(capture.uid,sizeof(capture.uid));printf("\",\"pages_hex\":\"");ct_hex(capture.tag,sizeof(capture.tag));puts("\"}");fflush(stdout);
                result=0;goto done;
            }
            ct_sleep(100);
        }
        struct rf_wire pending;
        int own_pending=rf_read(dir,"request",&pending,sizeof(pending)) && ct_header(&pending) &&
                        ct_same(&pending,&initial) && pending.token==token && pending.deadline==request.deadline && pending.phase==1;
        printf("RFID_DIAGNOSTIC={\"native_request_accepted\":%s,\"own_request_still_pending\":%s,\"result_record_seen\":%s,\"valid_owned_result_seen\":false}\n",
               native_request_accepted?"true":"false",own_pending?"true":"false",result_record_seen?"true":"false");fflush(stdout);
        ct_fail(ct_interrupted?"scan_interrupted":"scan_deadline_expired");
    }
done:
    if(token)ct_cancel_own(dir,&initial,token);
    if(locked && !rf_fstatat(dir,"scan-lock",&latest_stat,AT_SYMLINK_NOFOLLOW) && latest_stat.st_dev==lock_stat.st_dev && latest_stat.st_ino==lock_stat.st_ino){
        if(unlinkat(dir,"scan-lock",AT_REMOVEDIR))fprintf(stderr,"capture_lock_retained_for_review\n");
    }
    close(dir);return result;
}
