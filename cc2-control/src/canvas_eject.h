#ifndef CC2_CANVAS_EJECT_H
#define CC2_CANVAS_EJECT_H
/* Shared with the qualified printer runtime. Readback is an atomic file replace,
 * so a reader sees either the previous complete record or the new one. */
typedef struct { int available,running,slot;long pid;double travel;char result[24]; } canvas_eject_status;
static const char *canvas_eject_state_path="/tmp/cc2-canvas-eject.state";
static const char *canvas_eject_cancel_path="/tmp/cc2-canvas-eject.cancel";
static canvas_eject_status canvas_eject_read(void){
    canvas_eject_status s={0};s.slot=-1;strcpy(s.result,"unavailable");
    FILE *f=fopen(canvas_eject_state_path,"r");if(!f)return s;
    char result[24]={0},extra;long pid;int slot,ready;double travel;
    int n=fscanf(f,"%ld %d %23s %lf %d %c",&pid,&slot,result,&travel,&ready,&extra);fclose(f);
    static const char *states[]={"running","complete","empty","cancelled","disconnected","stale","blocked",
        "fault","stalled","travel_limit","timed_out","command_failed","stop_failed"};
    int valid=0;for(size_t i=0;i<sizeof(states)/sizeof(states[0]);i++)if(!strcmp(result,states[i]))valid=1;
    if(n!=5||!valid||pid<=1||pid>INT_MAX||slot<-1||slot>3||!isfinite(travel)||travel<0||travel>2000||ready!=1||kill((pid_t)pid,0))return s;
    s.available=1;s.pid=pid;s.slot=slot;s.travel=travel;strcpy(s.result,result);s.running=!strcmp(result,"running");return s;
}
static void canvas_eject_response(int fd){
    canvas_eject_status s=canvas_eject_read();char body[256];
    int n=snprintf(body,sizeof(body),"{\"available\":%s,\"running\":%s,\"slot\":%d,\"result\":\"%s\",\"travel\":%.2f}\n",
        s.available?"true":"false",s.running?"true":"false",s.slot,s.result,s.travel);
    respond(fd,200,"OK","application/json",body,(size_t)n);
}
static void canvas_eject_cancel_response(int fd){
    canvas_eject_status s=canvas_eject_read();
    if(!s.available||!s.running){const char *body="{\"error\":\"No Canvas ejection is running\"}\n";respond(fd,409,"Conflict","application/json",body,strlen(body));return;}
    int file=open(canvas_eject_cancel_path,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
    if(file<0){const char *body="{\"error\":\"Could not request Canvas stop\"}\n";respond(fd,500,"Internal Server Error","application/json",body,strlen(body));return;}
    close(file);const char *body="{\"accepted\":true}\n";respond(fd,202,"Accepted","application/json",body,strlen(body));
}
#endif
