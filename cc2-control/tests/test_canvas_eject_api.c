#define main cc2_main_original
#define console_start mock_console_start
#include "../src/main.c"
#undef main
#undef console_start
#include <assert.h>
static char sent[700];
int mock_console_start(console_state *s,const char *script,char *reason,size_t cap){(void)s;(void)reason;(void)cap;snprintf(sent,sizeof(sent),"%s",script);return 0;}
static void record(const char *result){FILE *f=fopen(canvas_eject_state_path,"w");assert(f);fprintf(f,"%ld 2 %s 100.5 1\n",(long)getpid(),result);fclose(f);}
static int action(console_state *c,mqtt_client *m,const char *a){int p[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,p));sent[0]=0;control_response(p[0],c,m,a,strlen(a));char reply[1024];int n=read(p[1],reply,sizeof(reply)-1);assert(n>0);reply[n]=0;close(p[0]);close(p[1]);return atoi(strchr(reply,' ')+1);}
int main(void){
 canvas_eject_state_path="eject-test.state";canvas_eject_cancel_path="eject-test.cancel";
 unlink(canvas_eject_state_path);unlink(canvas_eject_cancel_path);
 console_state c;console_init(&c,"/tmp/unused");mqtt_client m={0};m.connected=m.registered=m.have_machine_status=1;m.machine_status=1;m.last_message=time(NULL);
 assert(action(&c,&m,"canvas:eject:2")==409&&!sent[0]);record("empty");
 assert(action(&c,&m,"canvas:eject:2")==202&&!strcmp(sent,"CANVAS_MOTOR_CONTROL CHANNEL=2 EJECT=1 SPEED=0 DISTANCE=0 TIMEOUT=1"));
 const char *bad[]={"canvas:eject:4","canvas:eject:-1","canvas:eject:2junk","canvas:eject:2\nM112","canvas:eject:nan"};
 for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(action(&c,&m,bad[i])==409&&!sent[0]);
 m.machine_status=2;assert(action(&c,&m,"canvas:eject:2")==409);m.machine_status=1;
 strcpy(m.print_state,"paused");assert(action(&c,&m,"canvas:eject:2")==409);m.print_state[0]=0;
 m.last_message-=30;assert(action(&c,&m,"canvas:eject:2")==409);m.last_message=time(NULL);
 record("running");assert(action(&c,&m,"canvas:eject:2")==409);
 canvas_eject_status s=canvas_eject_read();assert(s.available&&s.running&&s.slot==2&&s.travel==100.5);
 int p[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,p));canvas_eject_cancel_response(p[0]);char buf[512];assert(read(p[1],buf,sizeof(buf))>0);close(p[0]);close(p[1]);assert(!access(canvas_eject_cancel_path,F_OK));
 record("bad\"json");assert(!canvas_eject_read().available);
 FILE *f=fopen(canvas_eject_state_path,"w");fputs("malformed",f);fclose(f);assert(!canvas_eject_read().available);
 f=fopen(canvas_eject_state_path,"w");fprintf(f,"%ld 2 complete nan 1\n",(long)getpid());fclose(f);assert(!canvas_eject_read().available);
 unlink(canvas_eject_state_path);unlink(canvas_eject_cancel_path);console_destroy(&c);
 puts("PASS Canvas capability gate, strict slots, idle/fresh guard, safe stock fallback, shared progress and cancellation");
}
