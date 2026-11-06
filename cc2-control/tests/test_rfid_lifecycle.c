#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "../src/sha256.h"
static char native_root[256];
#define CC2_RFID_NATIVE_ROOT native_root
#define CC2_RFID_OWNER_UID geteuid()
typedef struct {int mode,active,phase,new_boot,requested;uint64_t generation;char coordinator[65];} rfid_runtime;
typedef struct {int unused;} mqtt_client;
typedef struct {int unused;} console_state;
typedef struct {uint32_t pid;} rfid_identity;
static rfid_runtime fixture;
static int safe=1;
static struct {pid_t child;} rfid;
static struct {struct {int fd;} x;} runout={.x={.fd=-1}};
static rfid_runtime rfid_runtime_get(int force){(void)force;return fixture;}
static int rfid_identity_read(rfid_identity *id){id->pid=(uint32_t)getpid();return 0;}
static int rfid_printer_idle_limit(const mqtt_client *m,console_state *c,time_t age){(void)m;(void)c;(void)age;return safe;}
static int plates_late_active(void){return 0;}
static int maintenance_barrier_active(void){return 0;}
static double rfid_now(void){struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return t.tv_sec+t.tv_nsec/1e9;}
static void rfid_cancel(const char *reason){(void)reason;}
static int reply;
static void respond(int fd,int code,const char *reason,const char *type,const char *body,size_t size){
    (void)fd;(void)reason;(void)type;assert(size==strlen(body));reply=code;
}
static void plates_fail(int fd,int code,const char *message){(void)fd;(void)message;reply=code;}
#include "../src/rfid_lifecycle_http.h"
static void copy_program(const char *source,const char *target){
    int input=open(source,O_RDONLY),output=open(target,O_WRONLY|O_CREAT|O_TRUNC,0700);assert(input>=0&&output>=0);
    char bytes[4096];ssize_t size;while((size=read(input,bytes,sizeof(bytes)))>0)assert(write(output,bytes,(size_t)size)==size);
    assert(!size&&!close(input)&&!close(output));
}
int main(int argc,char **argv){
    assert(argc==1||argc==2);const char *program=argc==2?argv[1]:"/bin/true";
    char root[]="/tmp/cc2-rfid-job-XXXXXX";assert(mkdtemp(root));
    assert(snprintf(native_root,sizeof(native_root),"%s",root)>0);
    fixture.mode=1;fixture.phase=1;fixture.generation=3;
    assert(rn_reboot(fixture)&&!rn_automatic(fixture,1)&&rn_blocks());
    fixture.new_boot=1;assert(!strcmp(rn_automatic(fixture,0),"--stage-host"));
    assert(!strcmp(rn_automatic(fixture,1),"--advance"));
    for(int phase=0;phase<11;phase++){
        fixture.phase=phase;
        assert((rn_automatic(fixture,1)!=NULL)==(phase==1||phase==2||phase==4||phase==7||phase==9));
    }
    fixture.phase=3;assert(rn_blocks());fixture.phase=5;assert(!rn_blocks());fixture.phase=10;assert(!rn_blocks());
    fixture.mode=-1;assert(rn_blocks());fixture.mode=0;assert(!rn_blocks());fixture.mode=1;
    fixture.phase=-1;assert(!rn_blocks()&&!rn_automatic(fixture,0));fixture.requested=1;
    assert(rn_blocks()&&!strcmp(rn_automatic(fixture,0),"--stage-host"));fixture.requested=0;
    char helper[512],log[512];assert(snprintf(helper,sizeof(helper),"%s/coordinator",root)>0);
    assert(snprintf(log,sizeof(log),"%s/application-action.log",root)>0);
    copy_program(program,helper);int fd=open(helper,O_RDONLY);assert(fd>=0&&sha256_fd(fd,fixture.coordinator)&&!close(fd));
    fixture.phase=2;
    safe=0;rfid_native_action_response(-1,NULL,NULL,"resume",6);assert(reply==409&&!rfid_native_job.child);
    safe=1;rfid_native_action_response(-1,NULL,NULL,"resume",6);assert(reply==202&&rfid_native_job.child&&rn_blocks());
    assert(!rn_start("--advance"));
    double deadline=rfid_now()+5;
    while(rfid_native_job.child&&rfid_now()<deadline){rfid_native_tick(NULL,NULL);struct timespec pause={0,10000000};nanosleep(&pause,NULL);}
    assert(!rfid_native_job.child&&rfid_native_job.result==0);
    rfid_native_status_response(-1);assert(reply==200);
    fixture.phase=3;rfid_native_action_response(-1,NULL,NULL,"resume",6);assert(reply==409);
    fd=open(helper,O_WRONLY|O_APPEND);assert(fd>=0&&write(fd,"x",1)==1&&!close(fd));assert(!rn_start("--advance"));
    copy_program(program,helper);
    assert(!unlink(log)&&!symlink("/definitely-missing-job-fixture",log));assert(!rn_start("--advance"));
    assert(!unlink(log)&&!unlink(helper)&&!rmdir(root));
    puts("PASS: native app job policy, actual asynchronous pinned child, duplicate refusal, unsafe/hot state, durable-intent refusal and log symlink refusal");
    return 0;
}
