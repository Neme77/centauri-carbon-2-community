#define main cc2_main_original
#define console_start mock_console_start
#include "../src/main.c"
#undef main
#undef console_start
#include <assert.h>
#include "rfid_fixture.h"
static char sent[700];static int queue_busy;
int mock_console_start(console_state *s,const char *command,char *reason,size_t cap){
    (void)s;if(queue_busy){snprintf(reason,cap,"busy");return -1;}snprintf(sent,sizeof(sent),"%s",command);return 0;
}
static void capability(uint64_t epoch,uint32_t phase){
    unsigned char bytes[40]={0};memcpy(bytes,"CC2RF1\0\0",8);uint32_t pid=123;
    memcpy(bytes+8,&epoch,8);memcpy(bytes+32,&pid,4);memcpy(bytes+36,&phase,4);
    int fd=open(CC2_RFID_READY,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);assert(fd>=0);assert(write(fd,bytes,40)==40);close(fd);
}
static void fresh(mqtt_client *m){
    m->connected=m->registered=m->have_machine_status=1;m->machine_status=1;m->last_message=time(NULL);
    char data[]="{\"id\":11,\"result\":{\"eventtime\":1,\"status\":{\"print_stats\":{\"state\":\"standby\"},\"extruder\":{\"temperature\":25,\"target\":0},\"heater_bed\":{\"temperature\":22,\"target\":0}}}}";
    assert(uds_message(&telemetry,data,strlen(data)));
}
static int action(mqtt_client *m,console_state *c,const char *body){
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));sent[0]=0;
    rfid_action_response(pair[0],m,c,body,strlen(body));char reply[2048];ssize_t n=read(pair[1],reply,sizeof(reply)-1);assert(n>0);reply[n]=0;
    close(pair[0]);close(pair[1]);return atoi(strchr(reply,' ')+1);
}
static void preview(void){
    rfid.valid=1;rfid.child=0;rfid.cancelled=0;rfid.state="preview";rfid.generation=7;rfid.expires=rfid_now()+120;
    assert(rfid_identity_read(&rfid.identity));assert(rfid_decode(tag_fixture,&rfid.preview));
}
static void output_fixture(mqtt_client *m,console_state *console,const char *text,int code){
    int pipes[2];assert(!pipe(pipes));pid_t pid=fork();assert(pid>=0);
    if(!pid){close(pipes[0]);size_t n=strlen(text);if(write(pipes[1],text,n)!=(ssize_t)n)_exit(99);close(pipes[1]);_exit(code);}
    close(pipes[1]);assert(fcntl(pipes[0],F_SETFL,O_NONBLOCK)==0);
    rfid.child=pid;rfid.fd=pipes[0];rfid.valid=0;rfid.used=0;rfid.output[0]=0;rfid.cancelled=0;rfid.cancel_sent=0;rfid.deadline=rfid_now()+5;
    assert(rfid_identity_read(&rfid.identity));
    for(unsigned i=0;i<100&&rfid.child;i++){rfid_tick(m,console);usleep(10000);}assert(!rfid.child);
}
int main(void){
    rfid_preview p;assert(rfid_decode(tag_fixture,&p));assert(!strcmp(p.material,"PETG")&&!strcmp(p.color,"78D64B")&&p.nozzle_min==230&&p.nozzle_max==250&&p.bed_min==60&&p.bed_max==70&&p.diameter==175&&p.length==320);
    assert(!rfid_decode("RFID_RESULT={}",&p));char altered[1024];snprintf(altered,sizeof(altered),"%s trailing",tag_fixture);assert(!rfid_decode(altered,&p));
    snprintf(altered,sizeof(altered),"%s",tag_fixture);char *hex=strstr(altered,"pages_hex\":\"")+12;assert(hex);hex[32]='0';assert(!rfid_decode(altered,&p));
    mqtt_client m={0};console_state console={0};assert(!pthread_mutex_init(&console.lock,NULL));uds_init(&telemetry);
    int uds[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,uds));telemetry.fd=uds[0];fresh(&m);capability(42,1);
    assert(rfid_idle(&m,&console));
    preview();assert(action(&m,&console,"apply:7:2")==202);assert(strstr(sent,"CHANNEL=2")&&strstr(sent,"0x78D64B")&&strstr(sent,"PETG")&&strstr(sent,"Anycubic"));assert(!strstr(sent,"M104")&&!strstr(sent,"M140"));
    assert(action(&m,&console,"apply:7:2")==409&&!sent[0]);
    const char *bad[]={"apply:6:2","apply:7:4","apply:7:-1","apply:7:2 trailing","apply:7:2\nM112","apply:7:2:extra"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++){preview();assert(action(&m,&console,bad[i])>=400&&!sent[0]);}
    preview();rfid.expires=rfid_now()-1;assert(action(&m,&console,"apply:7:2")==409&&!sent[0]);
    preview();m.machine_status=2;assert(action(&m,&console,"apply:7:2")==409&&!sent[0]);fresh(&m);
    preview();capability(43,1);assert(action(&m,&console,"apply:7:2")==409&&!sent[0]);
    preview();console.busy=1;assert(action(&m,&console,"apply:7:2")==409&&!sent[0]);console.busy=0;
    preview();m.last_message=time(NULL)-6;assert(action(&m,&console,"apply:7:2")==409&&!sent[0]);fresh(&m);
    preview();m.last_message=time(NULL)-6;rfid_tick(&m,&console);assert(rfid.valid);fresh(&m);
    preview();m.last_message=time(NULL)-16;rfid_tick(&m,&console);assert(!rfid.valid);fresh(&m);
    preview();queue_busy=1;assert(action(&m,&console,"apply:7:2")==409&&!sent[0]&&!rfid.valid);queue_busy=0;
    preview();assert(action(&m,&console,"cancel")==200&&!rfid.valid&&!sent[0]);
    snprintf(altered,sizeof(altered),"SCAN ARMED: fixture\n%s\n",tag_fixture);output_fixture(&m,&console,altered,0);assert(rfid.valid);
    output_fixture(&m,&console,altered,1);assert(!rfid.valid);
    output_fixture(&m,&console,"RFID_RESULT={}\n",0);assert(!rfid.valid);
    char doubled[2048];snprintf(doubled,sizeof(doubled),"%s\n%s\n",tag_fixture,tag_fixture);output_fixture(&m,&console,doubled,0);assert(!rfid.valid);
    output_fixture(&m,&console,"STOP: scan_deadline_expired\n",1);assert(!rfid.valid);
    assert(!strcmp(rfid.error,"scan_deadline_expired"));
    output_fixture(&m,&console,"STOP: fresh_idle_cold_observation_failed\nLIVE GUARD PASS: recovered fixture\nSTOP: scan_deadline_expired\n",1);
    assert(!strcmp(rfid.error,"scan_deadline_expired"));
    snprintf(doubled,sizeof(doubled),"STOP: fresh_idle_cold_observation_failed\nLIVE GUARD PASS: recovered fixture\n%s\n",tag_fixture);
    output_fixture(&m,&console,doubled,0);assert(rfid.valid);

    char oversized[13000];memset(oversized,'X',sizeof(oversized)-1);oversized[sizeof(oversized)-1]=0;
    output_fixture(&m,&console,oversized,0);assert(!rfid.valid&&!strcmp(rfid.state,"output_limit"));
    /* Cancellation before SCAN ARMED must let the bounded manager release its
     * lock instead of killing it. The dummy child records graceful cleanup. */
    char cleanup[4096];snprintf(cleanup,sizeof(cleanup),"%s.cleanup",CC2_RFID_READY);
    int guardpipe[2];assert(!pipe(guardpipe));pid_t guard=fork();assert(guard>=0);
    if(!guard){close(guardpipe[0]);usleep(150000);int mark=open(cleanup,O_WRONLY|O_CREAT|O_EXCL,0600);if(mark<0)_exit(99);close(mark);close(guardpipe[1]);_exit(0);}
    close(guardpipe[1]);assert(!fcntl(guardpipe[0],F_SETFL,O_NONBLOCK));
    rfid.child=guard;rfid.fd=guardpipe[0];rfid.valid=0;rfid.used=0;rfid.output[0]=0;rfid.cancelled=0;rfid.cancel_sent=0;rfid.deadline=rfid_now()+5;
    assert(rfid_identity_read(&rfid.identity));rfid_cancel("cancelled");assert(rfid.child&&!rfid.cancel_sent);
    for(unsigned i=0;i<100&&rfid.child;i++){rfid_tick(&m,&console);usleep(10000);}
    assert(!rfid.child&&!rfid.valid&&!access(cleanup,F_OK));assert(!unlink(cleanup));
    /* Actual host fork/fexecve path; ARM tests above cover the same state machine
     * with dummy pipe producers. No binfmt registration is required. */
#ifndef __arm__
    assert(action(&m,&console,"scan")==200);
    for(unsigned i=0;i<100&&rfid.child;i++){rfid_tick(&m,&console);usleep(10000);}assert(!rfid.child&&rfid.valid);
#endif
    char helper_path[512];int verified=rfid_helper_open(helper_path);assert(verified>=0);close(verified);
    char helper_copy[4096];snprintf(helper_copy,sizeof(helper_copy),"%s.saved",CC2_RFID_HELPER);
    assert(!rename(CC2_RFID_HELPER,helper_copy));assert(!symlink(helper_copy,CC2_RFID_HELPER));
    assert(action(&m,&console,"scan")==409);assert(!unlink(CC2_RFID_HELPER));assert(!rename(helper_copy,CC2_RFID_HELPER));
    uds_close(&telemetry);close(uds[1]);unlink(CC2_RFID_READY);pthread_mutex_destroy(&console.lock);
    puts("PASS: captured PETG decode, owned one-shot preview, selected slot, expiry/epoch/state/queue guards, no heat/load, actual child output and malformed/duplicate/failing results");return 0;
}
