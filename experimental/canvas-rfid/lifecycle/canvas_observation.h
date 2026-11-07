/* Bounded loopback observation only. No commands, redirects or serial access. */
#ifndef CC2_CANVAS_OBSERVATION_H
#define CC2_CANVAS_OBSERVATION_H
#include "../../../cc2-control/src/connections/json.h"
#include <arpa/inet.h>
#include <poll.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#ifndef VC_PORT
#define VC_PORT 8081
#endif
static inline int vc_canvas_json(const char *body,size_t size){
    ng_json *j=malloc(sizeof(*j));if(!j)return 0;
    int available=0;double machine=0,error=0,active=0;
    int okay=ng_json_parse(j,body,size)&&j->v[0].type=='{'&&ng_bool(j,0,"available",&available)&&available&&
        ng_number(j,0,"machine_status",&machine)&&machine==1;
    const char *source=okay?ng_string(j,0,"source"):NULL;
    if(!source||strcmp(source,"mqtt"))okay=0;
    int response=ng_member(j,ng_member(j,0,"telemetry"),"result");
    int info=ng_member(j,response,"canvas_info");
    int list=ng_member(j,info,"canvas_list");
    okay=okay&&ng_number(j,response,"error_code",&error)&&error==0&&
        ng_number(j,info,"active_canvas_id",&active)&&active>=0&&active<=3&&floor(active)==active&&
        list>=0&&j->v[list].type=='[';
    unsigned matches=0;
    if(okay)for(int at=list+1;at<j->v[list].next;at=j->v[at].next){
        double id=0,connected=0;
        if(!ng_number(j,at,"canvas_id",&id)||!ng_number(j,at,"connected",&connected)){okay=0;break;}
        if(id==active){matches++;if(connected!=1)okay=0;}
    }
    free(j);return okay&&matches==1;
}
static inline int vc_wait(int fd,short events,uint64_t deadline){
    for(;;){
        uint64_t now=sc_now();if(!now||sc_cancelled||now>=deadline)return 0;
        uint64_t remaining=deadline-now;struct pollfd p={.fd=fd,.events=events};
        int result=poll(&p,1,(int)(remaining>100?100:remaining));
        if(result>0)return !(p.revents&POLLNVAL);
        if(result<0&&errno!=EINTR)return 0;
    }
}
static inline int vc_http_body(const char *endpoint,char **output,size_t *body_size){
    *output=NULL;*body_size=0;
    if(strcmp(endpoint,"/api/canvas")&&strcmp(endpoint,"/api/printer")&&strcmp(endpoint,"/api/uds")&&
       strcmp(endpoint,"/api/rfid/lifecycle/restore-readiness")&&
       strcmp(endpoint,"/api/rfid/lifecycle/host-restore-readiness"))return 0;
    char *bytes=malloc(32769);if(!bytes)return 0;
    int fd=socket(AF_INET,SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC,0);
    if(fd<0){free(bytes);return 0;}
    uint64_t now=sc_now(),deadline=now+3000u;
    struct sockaddr_in peer={.sin_family=AF_INET,.sin_port=htons(VC_PORT)};
    peer.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    int okay=now&&!sc_cancelled&&(connect(fd,(struct sockaddr*)&peer,sizeof(peer))==0||errno==EINPROGRESS)&&vc_wait(fd,POLLOUT,deadline);
    int error=0;socklen_t length=sizeof(error);
    if(okay)okay=!getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length)&&!error;
    char request[160];int n=snprintf(request,sizeof(request),"GET %s HTTP/1.0\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n\r\n",endpoint,VC_PORT);
    if(n<1||(size_t)n>=sizeof(request))okay=0;
    size_t sent=0,used=0;int eof=0;
    while(okay&&sent<(size_t)n){
        ssize_t got=send(fd,request+sent,(size_t)n-sent,MSG_NOSIGNAL);
        if(got>0)sent+=(size_t)got;
        else if(got<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))okay=vc_wait(fd,POLLOUT,deadline);
        else okay=0;
    }
    while(okay&&used<32768){
        ssize_t got=recv(fd,bytes+used,32768-used,0);
        if(got>0)used+=(size_t)got;
        else if(!got){eof=1;break;}
        else if(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK)okay=vc_wait(fd,POLLIN,deadline);
        else okay=0;
    }
    close(fd);bytes[used]=0;
    if(!eof||used==32768||memchr(bytes,0,used)||
       (strncmp(bytes,"HTTP/1.1 200 ",13)&&strncmp(bytes,"HTTP/1.0 200 ",13)))okay=0;
    char *body=okay?strstr(bytes,"\r\n\r\n"):NULL;
    if(!body||body-bytes>2048)okay=0;
    size_t declared=0;unsigned counts=0;
    if(okay){
        char *line=strstr(bytes,"\r\n");if(!line)okay=0;
        else line+=2;
        while(okay&&line<body){
            char *end=strstr(line,"\r\n");if(!end||end>body){okay=0;break;}
            if(!strncasecmp(line,"Transfer-Encoding:",18)){okay=0;break;}
            if(!strncasecmp(line,"Content-Length:",15)){
                char *value=line+15;while(value<end&&(*value==' '||*value=='\t'))value++;
                if(value==end||*value<'0'||*value>'9'){okay=0;break;}
                errno=0;char *last=NULL;unsigned long parsed=strtoul(value,&last,10);
                if(errno||last!=end||parsed>32768){okay=0;break;}
                declared=(size_t)parsed;counts++;
            }
            line=end+2;
        }
        body+=4;
        if(counts!=1||declared!=used-(size_t)(body-bytes))okay=0;
    }
    if(okay){memmove(bytes,body,declared);bytes[declared]=0;*output=bytes;*body_size=declared;}
    else free(bytes);
    return okay;
}
static inline int vc_canvas_http(void){
    char *body=NULL;size_t size=0;
    int okay=vc_http_body("/api/canvas",&body,&size)&&vc_canvas_json(body,size);
    free(body);return okay;
}
static inline int vc_native_ready(int require_canvas){
    const char *endpoint=require_canvas?"/api/rfid/lifecycle/host-restore-readiness":
        "/api/rfid/lifecycle/restore-readiness";
    const char ready[]="{\"ready\":true}";
    char *body=NULL;size_t size=0;
    int okay=vc_http_body(endpoint,&body,&size)&&size==sizeof(ready)-1&&
        !memcmp(body,ready,size);
    free(body);return okay;
}
#endif
