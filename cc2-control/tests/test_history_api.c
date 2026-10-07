#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>
/* HTTP side of auto refill, printer refusals, the print history and the
 * time-lapse relay. A fake printer HTTP service stands in for loopback :80. */
static mqtt_client m;
static int mqtt_peer,listener;
static char upstream_request[4096];
static const char *upstream_reply;
static int upstream_silent,upstream_linger_ms;
static size_t upstream_piece;
static double fetch_seconds;
typedef void (*body_handler)(int,mqtt_client *,const char *,size_t);
static int call(body_handler handler,const char *body,char *reply,size_t cap){
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    handler(pair[0],&m,body,strlen(body));close(pair[0]);
    size_t used=0;ssize_t got;while(used+1<cap&&(got=recv(pair[1],reply+used,cap-1-used,0))>0)used+=(size_t)got;
    reply[used]=0;close(pair[1]);return atoi(strchr(reply,' ')+1);
}
static int get(void (*handler)(int,const mqtt_client *),char *reply,size_t cap){
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    handler(pair[0],&m);close(pair[0]);
    size_t used=0;ssize_t got;while(used+1<cap&&(got=recv(pair[1],reply+used,cap-1-used,0))>0)used+=(size_t)got;
    reply[used]=0;close(pair[1]);return atoi(strchr(reply,' ')+1);
}
static void refresh(int fd,mqtt_client *client,const char *body,size_t length){(void)body;(void)length;history_refresh_response(fd,client);}
/* One MQTT publish from CC2 Control, or "" when nothing was published. */
static void published(char *payload,size_t cap){
    unsigned char packet[2048];size_t n=0,remain=0,rn,multiplier=1;payload[0]=0;
    if(recv(mqtt_peer,packet,1,MSG_DONTWAIT)!=1)return;
    do{assert(n<4&&recv(mqtt_peer,packet+1+n,1,0)==1);remain+=(packet[1+n]&127u)*multiplier;multiplier*=128;n++;}while(packet[n]&0x80);
    rn=n;assert(1+rn+remain<=sizeof(packet));
    assert(recv(mqtt_peer,packet+1+rn,remain,MSG_WAITALL)==(ssize_t)remain);
    size_t topic=((size_t)packet[1+rn]<<8)|packet[2+rn],start=1+rn+2+topic+2,length=1+rn+remain-start;
    assert(length<cap);memcpy(payload,packet+start,length);payload[length]=0;
}
static void *fake_service(void *unused){
    (void)unused;int client=accept(listener,NULL,NULL);if(client<0)return NULL;
    size_t used=0;upstream_request[0]=0;
    while(used<sizeof(upstream_request)-1&&!strstr(upstream_request,"\r\n\r\n")){
        ssize_t n=recv(client,upstream_request+used,sizeof(upstream_request)-1-used,0);if(n<=0)break;
        used+=(size_t)n;upstream_request[used]=0;
    }
    if(upstream_silent){struct timespec wait={2,0};nanosleep(&wait,NULL);}
    else {
        size_t length=strlen(upstream_reply),sent=0,piece=upstream_piece?upstream_piece:length;
        while(sent<length){
            size_t n=length-sent<piece?length-sent:piece;(void)send_all(client,upstream_reply+sent,n);sent+=n;
            if(upstream_piece){struct timespec wait={0,2000000};nanosleep(&wait,NULL);}
        }
        /* Like the printer: the connection stays open after a complete response. */
        if(upstream_linger_ms){struct timespec wait={upstream_linger_ms/1000,(upstream_linger_ms%1000)*1000000L};nanosleep(&wait,NULL);}
    }
    close(client);return NULL;
}
static int fetch(const char *query,int serve,char *reply,size_t cap){
    pthread_t service;if(serve)assert(!pthread_create(&service,NULL,fake_service,NULL));
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    struct timespec started,ended;clock_gettime(CLOCK_MONOTONIC,&started);
    int owned=timelapse_download_start(pair[0],&m,query);if(!owned)close(pair[0]);
    size_t used=0;ssize_t got;while(used+1<cap&&(got=recv(pair[1],reply+used,cap-1-used,0))>0)used+=(size_t)got;
    clock_gettime(CLOCK_MONOTONIC,&ended);
    fetch_seconds=(double)(ended.tv_sec-started.tv_sec)+(double)(ended.tv_nsec-started.tv_nsec)/1e9;
    reply[used]=0;close(pair[1]);
    for(int i=0;owned&&atomic_load(&active_downloads)&&i<5000;i++){struct timespec wait={0,1000000};nanosleep(&wait,NULL);}
    assert(!atomic_load(&active_downloads));
    if(serve)pthread_join(service,NULL);
    return atoi(strchr(reply,' ')+1);
}
int main(void){
    char reply[8192],payload[1024];int pair[2];
    assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    mqtt_init(&m);m.fd=pair[0];mqtt_peer=pair[1];m.connected=1;m.registered=1;
    strcpy(m.serial,"SN");strcpy(m.client_id,"cc2-control-test");strcpy(m.password,"pw&1 \xc3\xbc");
    /* Auto refill: explicit on/off, only once the printer reported the setting. */
    assert(call(canvas_auto_refill_response,"maybe",reply,sizeof(reply))==400);
    assert(call(canvas_auto_refill_response,"on",reply,sizeof(reply))==409);published(payload,sizeof(payload));assert(!*payload);
    m.have_auto_refill=1;
    assert(call(canvas_auto_refill_response," on\n",reply,sizeof(reply))==202&&strstr(reply,"\"method\":2004"));
    published(payload,sizeof(payload));assert(!strcmp(payload,"{\"method\":2004,\"id\":2004,\"params\":{\"auto_refill\":true}}"));
    m.registered=0;assert(call(canvas_auto_refill_response,"off",reply,sizeof(reply))==503);m.registered=1;
    /* /api/canvas reports the setting, null until the printer has. */
    strcpy(m.canvas_snapshot,"{\"result\":{\"canvas_info\":{\"canvas_list\":[]}}}");m.canvas_snapshot_len=strlen(m.canvas_snapshot);
    m.auto_refill=0;assert(get(canvas_response,reply,sizeof(reply))==200&&strstr(reply,"\"auto_refill\":false,\"telemetry\":{"));
    m.have_auto_refill=0;assert(get(canvas_response,reply,sizeof(reply))==200&&strstr(reply,"\"auto_refill\":null"));
    /* /api/printer carries the latest printer refusal of our own requests. */
    assert(get(printer_response,reply,sizeof(reply))==200&&strstr(reply,"\"printer_error\":null,\"camera_viewer\":null}"));
    m.reply_errors=3;m.reply_error_method=1020;m.reply_error_code=1026;m.reply_error_time=time(NULL);
    assert(get(printer_response,reply,sizeof(reply))==200&&strstr(reply,"\"printer_error\":{\"sequence\":3,\"method\":1020,\"code\":1026,\"age\":"));
    uds_init(&telemetry);
    const char *report="{\"report\":{\"error_code\":1264,\"error_level\":2,\"message\":\"Clog\\n\\u00e8\"}}";
    assert(uds_message(&telemetry,report,strlen(report)));
    assert(get(printer_response,reply,sizeof(reply))==200);
    assert(strstr(reply,"\"printer_report\":{\"sequence\":1,\"code\":1264,\"level\":2,\"message\":\"Clog\\n\\u00e8\",\"age\":"));
    uds_close(&telemetry);
    assert(get(printer_response,reply,sizeof(reply))==200&&strstr(reply,"\"printer_report\":{\"sequence\":1,"));
    /* History: nothing cached yet, then a refresh publishes 1036 once. */
    assert(get(history_response,reply,sizeof(reply))==200);
    assert(strstr(reply,"{\"available\":false,\"pending\":false,\"generating\":false,\"deleting\":false,\"age\":-1,\"error_code\":-1,\"reply\":null}"));
    assert(call(refresh,"",reply,sizeof(reply))==202);published(payload,sizeof(payload));assert(!strcmp(payload,"{\"method\":1036,\"id\":1036}"));
    assert(call(refresh,"",reply,sizeof(reply))==202);published(payload,sizeof(payload));assert(!*payload);
    assert(get(history_response,reply,sizeof(reply))==200&&strstr(reply,"\"pending\":true"));
    const char *history="{\"id\":1036,\"method\":1036,\"result\":{\"error_code\":0,\"history_task_list\":["
        "{\"task_id\":\"t-ready\",\"task_status\":1,\"task_name\":\"Part one.gcode\",\"time_lapse_video_status\":2,\"time_lapse_video_url\":\"video/Part one.mp4\"},"
        "{\"task_id\":\"t-frames\",\"task_status\":2,\"task_name\":\"Part two.gcode\",\"time_lapse_video_status\":1,\"time_lapse_video_url\":\"picture/Part two\"},"
        "{\"task_id\":\"t-none\",\"task_name\":\"Part three.gcode\",\"time_lapse_video_status\":0,\"time_lapse_video_url\":\"\"},"
        "{\"task_id\":\"t-escape\",\"task_name\":\"x\",\"time_lapse_video_status\":2,\"time_lapse_video_url\":\"video/a\\\"b.mp4\"},"
        "{\"task_id\":\"t-nested\",\"task_name\":\"x\",\"time_lapse_video_status\":2,\"time_lapse_video_url\":\"video/../x.mp4\"}]}}";
    m.history=strdup(history);m.history_len=strlen(history);m.history_error=0;m.history_received=time(NULL);m.history_requested=0;
    assert(get(history_response,reply,sizeof(reply))==200&&strstr(reply,"\"available\":true,\"pending\":false")&&strstr(reply,history));
    int video=0;char url[512];
    assert(history_task(&m,"t-frames",&video,url,sizeof(url))&&video==1&&!strcmp(url,"picture/Part two"));
    assert(history_task(&m,"t-none",&video,url,sizeof(url))&&video==0&&!*url);
    assert(!history_task(&m,"t-escape",&video,url,sizeof(url))&&!history_task(&m,"t",&video,url,sizeof(url)));
    /* Video names: picture/ entries map to the rendered MP4 under video/. */
    char file[300];
    assert(timelapse_file("picture/ECC2_\xe7\xbb\x84 B.gcode20260704175855",file,sizeof(file))&&!strcmp(file,"video/ECC2_\xe7\xbb\x84 B.gcode20260704175855.mp4"));
    assert(timelapse_file("video/X.mp4",file,sizeof(file))&&!strcmp(file,"video/X.mp4"));
    assert(timelapse_file("X",file,sizeof(file))&&!strcmp(file,"video/X.mp4"));
    const char *unsafe[]={"","picture/","picture/.hidden","video/../x.mp4","picture/a/b","picture/a\\b","picture/a\"b","picture/a\nb"};
    for(size_t i=0;i<sizeof(unsafe)/sizeof(*unsafe);i++)assert(!timelapse_file(unsafe[i],file,sizeof(file)));
    /* History deletion: explicit terminal IDs, idle/fresh state, no overlap. */
    m.machine_status=2;m.have_machine_status=1;m.last_message=time(NULL);
    assert(call(history_delete_response,"t-ready",reply,sizeof(reply))==409);
    m.machine_status=1;m.last_message=time(NULL)-16;
    assert(call(history_delete_response,"t-ready",reply,sizeof(reply))==409);
    m.last_message=time(NULL);
    assert(call(history_delete_response,"../x",reply,sizeof(reply))==400);
    assert(call(history_delete_response,"t-ready\nt-ready",reply,sizeof(reply))==400);
    assert(call(history_delete_response,"t-unknown",reply,sizeof(reply))==409);
    assert(call(history_delete_response,"t-none",reply,sizeof(reply))==409); /* missing terminal status */
    assert(call(history_delete_response,"t-ready\nt-frames",reply,sizeof(reply))==202);
    published(payload,sizeof(payload));assert(!strcmp(payload,"{\"method\":1038,\"id\":1038,\"params\":{\"list\":[\"t-ready\",\"t-frames\"]}}"));
    assert(get(history_response,reply,sizeof(reply))==200&&strstr(reply,"\"deleting\":true"));
    assert(call(history_delete_response,"t-ready",reply,sizeof(reply))==409);
    assert(call(timelapse_generate_response,"t-frames",reply,sizeof(reply))==409&&strstr(reply,"deletion is pending"));
    m.history_delete_requested=0;m.history_received=time(NULL)-61;
    assert(call(history_delete_response,"t-ready",reply,sizeof(reply))==409);
    m.history_received=time(NULL);
    /* Rendering: known task with frames, idle printer, one at a time. */
    assert(call(timelapse_generate_response,"../x",reply,sizeof(reply))==400);
    assert(call(timelapse_generate_response,"t-unknown",reply,sizeof(reply))==404);
    assert(call(timelapse_generate_response,"t-none",reply,sizeof(reply))==404);
    assert(call(timelapse_generate_response,"t-ready",reply,sizeof(reply))==409);
    m.have_machine_status=1;m.machine_status=2;m.last_message=time(NULL);
    assert(call(timelapse_generate_response,"t-frames",reply,sizeof(reply))==409&&strstr(reply,"idle printer"));
    m.machine_status=1;m.last_message=time(NULL)-60;
    assert(call(timelapse_generate_response,"t-frames",reply,sizeof(reply))==409);
    m.last_message=time(NULL);published(payload,sizeof(payload));assert(!*payload);
    assert(call(timelapse_generate_response,"t-frames\n",reply,sizeof(reply))==202&&strstr(reply,"\"method\":1051"));
    published(payload,sizeof(payload));assert(!strcmp(payload,"{\"method\":1051,\"id\":1051,\"params\":{\"url\":\"picture/Part two\"}}"));
    assert(call(timelapse_generate_response,"t-frames",reply,sizeof(reply))==409&&strstr(reply,"already"));
    assert(get(history_response,reply,sizeof(reply))==200&&strstr(reply,"\"generating\":true"));
    m.timelapse_requested=0;
    /* Download relay against a fake printer service on a loopback port. */
    listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);
    struct sockaddr_in address;memset(&address,0,sizeof(address));address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    socklen_t size=sizeof(address);assert(!bind(listener,(struct sockaddr *)&address,sizeof(address))&&!listen(listener,4));
    assert(!getsockname(listener,(struct sockaddr *)&address,&size));
    timelapse_service_port=ntohs(address.sin_port);timelapse_timeout_seconds=1;
    upstream_reply="HTTP/1.1 200 OK\r\nServer: libhv/1.3.4\r\nContent-Length: 5\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n";
    assert(fetch("task=t-ready",1,reply,sizeof(reply))==200);
    assert(strstr(upstream_request,"GET /download?X-Token=pw%261%20%C3%BC&file_name=video%2FPart%20one.mp4 HTTP/1.1\r\n")==upstream_request);
    assert(strstr(reply,"Content-Type: video/mp4\r\n")&&strstr(reply,"Transfer-Encoding: chunked\r\n")&&!strstr(reply,"Content-Length"));
    assert(strstr(reply,"filename*=UTF-8''Part%20one.mp4\r\n")&&!strstr(reply,"pw&1")&&!strstr(reply,"X-Token"));
    assert(!strcmp(strstr(reply,"\r\n\r\n")+4,"5\r\nhello\r\n0\r\n\r\n"));
    upstream_reply="HTTP/1.1 200 OK\r\nContent-Length: 18\r\nTransfer-Encoding: chunked\r\n\r\nnot chunk framing!";
    assert(fetch("task=t-ready",1,reply,sizeof(reply))==200&&strstr(reply,"Content-Length: 18\r\n")&&!strstr(reply,"Transfer-Encoding"));
    assert(!strcmp(strstr(reply,"\r\n\r\n")+4,"not chunk framing!"));
    upstream_reply="HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
    assert(fetch("task=t-ready",1,reply,sizeof(reply))==200&&strstr(reply,"Content-Length: 5\r\n"));
    assert(!strcmp(strstr(reply,"\r\n\r\n")+4,"hello"));
    upstream_reply="HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
    assert(fetch("task=t-ready",1,reply,sizeof(reply))==502);
    upstream_silent=1;assert(fetch("task=t-ready",1,reply,sizeof(reply))==504);upstream_silent=0;
    /* The printer keeps the connection open after the response: the relay ends at the last
     * chunk and its trailer (or after Content-Length bytes), not at the receive timeout. */
    timelapse_timeout_seconds=3;upstream_linger_ms=2500;
    upstream_reply="HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 10\r\nTransfer-Encoding: chunked\r\n\r\n"
        "5;x=1\r\nhello\r\n5\r\nworld\r\n0\r\nX-Trailer: 1\r\n\r\n";
    assert(fetch("task=t-ready",1,reply,sizeof(reply))==200&&fetch_seconds<1.5);
    assert(!strcmp(strstr(reply,"\r\n\r\n")+4,"5;x=1\r\nhello\r\n5\r\nworld\r\n0\r\nX-Trailer: 1\r\n\r\n"));
    upstream_reply="HTTP/1.1 200 OK\r\nContent-Length: 5\r\n\r\nhello";
    assert(fetch("task=t-ready",1,reply,sizeof(reply))==200&&fetch_seconds<1.5);
    assert(!strcmp(strstr(reply,"\r\n\r\n")+4,"hello"));
    upstream_linger_ms=0;upstream_piece=3;
    upstream_reply="HTTP/1.1 200 OK\r\nContent-Length: 10\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n5\r\nworld\r\n0\r\n\r\n";
    assert(fetch("task=t-ready",1,reply,sizeof(reply))==200&&strstr(reply,"Transfer-Encoding: chunked\r\n"));
    assert(!strcmp(strstr(reply,"\r\n\r\n")+4,"5\r\nhello\r\n5\r\nworld\r\n0\r\n\r\n"));
    upstream_piece=0;timelapse_timeout_seconds=1;
    /* body_feed finds the end of the body wherever the stream is split. */
    {
        const char *sample="5;x=1\r\nhello\r\nA\r\n0123456789\r\n0\r\nX-Trailer: 1\r\n\r\nEXTRA";
        size_t all=strlen(sample),body=all-5;body_end e;
        for(size_t split=0;split<=all;split++) {
            memset(&e,0,sizeof(e));e.chunked=1;
            size_t a=body_feed(&e,sample,split),b=e.done?0:body_feed(&e,sample+split,all-split);
            assert(e.done&&a+b==body);
        }
        memset(&e,0,sizeof(e));e.left=5;assert(body_feed(&e,"helloEXTRA",10)==5&&e.done);
        memset(&e,0,sizeof(e));e.left=0;assert(body_feed(&e,"",0)==0&&e.done);
        memset(&e,0,sizeof(e));e.left=-1;assert(body_feed(&e,"abc",3)==3&&!e.done);
        memset(&e,0,sizeof(e));e.chunked=1;assert(body_feed(&e,"fffffffffffffffff\r\n",19)==19&&!e.done&&!e.chunked);
    }
    assert(fetch("task=t-frames",0,reply,sizeof(reply))==409&&fetch("task=t-unknown",0,reply,sizeof(reply))==404);
    assert(fetch("task=t-nested",0,reply,sizeof(reply))==409&&fetch("task=t-escape",0,reply,sizeof(reply))==404);
    assert(fetch("task=",0,reply,sizeof(reply))==400&&fetch("file=x",0,reply,sizeof(reply))==400&&fetch(NULL,0,reply,sizeof(reply))==400);
    assert(fetch("task=t-ready&task=x",0,reply,sizeof(reply))==400);
    atomic_store(&active_downloads,2);
    {int busy[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,busy));assert(!timelapse_download_start(busy[0],&m,"task=t-ready"));
     ssize_t n=recv(busy[1],reply,sizeof(reply)-1,0);assert(n>0);reply[n]=0;assert(atoi(strchr(reply,' ')+1)==503);close(busy[0]);close(busy[1]);}
    assert(atomic_load(&active_downloads)==2);atomic_store(&active_downloads,0);
    close(listener);free(m.history);close(mqtt_peer);close(m.fd);return 0;
}
