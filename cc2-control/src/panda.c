#include "panda.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define PANDA_REQUEST_MAX 8192
#define PANDA_BODY_MAX 8192

typedef struct { panda_server *server; int fd; } panda_client;

static uint32_t rol32(uint32_t value, unsigned int count) {
    return (value << count) | (value >> (32U - count));
}

static void sha1(const unsigned char *data, size_t len, unsigned char out[20]) {
    uint64_t bits = (uint64_t)len * 8U;
    size_t padded = ((len + 9U + 63U) / 64U) * 64U;
    unsigned char *message = calloc(1, padded);
    uint32_t h0=0x67452301U,h1=0xefcdab89U,h2=0x98badcfeU,h3=0x10325476U,h4=0xc3d2e1f0U;
    if (!message) { memset(out,0,20); return; }
    memcpy(message,data,len); message[len]=0x80;
    for (int i=0;i<8;i++) message[padded-1U-(size_t)i]=(unsigned char)(bits>>(i*8));
    for (size_t off=0;off<padded;off+=64) {
        uint32_t w[80];
        for(int i=0;i<16;i++) w[i]=((uint32_t)message[off+i*4]<<24)|((uint32_t)message[off+i*4+1]<<16)|((uint32_t)message[off+i*4+2]<<8)|message[off+i*4+3];
        for(int i=16;i<80;i++) w[i]=rol32(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
        uint32_t a=h0,b=h1,c=h2,d=h3,e=h4;
        for(int i=0;i<80;i++) {
            uint32_t f,k;
            if(i<20){f=(b&c)|((~b)&d);k=0x5a827999U;}
            else if(i<40){f=b^c^d;k=0x6ed9eba1U;}
            else if(i<60){f=(b&c)|(b&d)|(c&d);k=0x8f1bbcdcU;}
            else {f=b^c^d;k=0xca62c1d6U;}
            uint32_t t=rol32(a,5)+f+e+k+w[i]; e=d;d=c;c=rol32(b,30);b=a;a=t;
        }
        h0+=a;h1+=b;h2+=c;h3+=d;h4+=e;
    }
    free(message);
    uint32_t h[5]={h0,h1,h2,h3,h4};
    for(int i=0;i<5;i++){out[i*4]=(unsigned char)(h[i]>>24);out[i*4+1]=(unsigned char)(h[i]>>16);out[i*4+2]=(unsigned char)(h[i]>>8);out[i*4+3]=(unsigned char)h[i];}
}

static void base64(const unsigned char *src, size_t len, char *dst, size_t cap) {
    static const char table[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t o=0;
    for(size_t i=0;i<len;i+=3){uint32_t v=(uint32_t)src[i]<<16;if(i+1<len)v|=(uint32_t)src[i+1]<<8;if(i+2<len)v|=src[i+2];if(o+4>=cap)break;dst[o++]=table[(v>>18)&63];dst[o++]=table[(v>>12)&63];dst[o++]=i+1<len?table[(v>>6)&63]:'=';dst[o++]=i+2<len?table[v&63]:'=';}
    dst[o]='\0';
}

static int send_all(int fd,const void *data,size_t len){const unsigned char *p=data;while(len){ssize_t n=send(fd,p,len,0);if(n<=0)return -1;p+=n;len-=(size_t)n;}return 0;}

static int local_printer_json(int port,char *body,size_t cap){
    int fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0)return -1;
    struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=htons((unsigned short)port);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    struct timeval tv={2,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
    if(connect(fd,(struct sockaddr*)&a,sizeof(a))<0){close(fd);return -1;}
    const char req[]="GET /api/printer HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
    if(send_all(fd,req,sizeof(req)-1)<0){close(fd);return -1;}
    size_t used=0;while(used+1<cap){ssize_t n=recv(fd,body+used,cap-used-1,0);if(n<=0)break;used+=(size_t)n;}close(fd);body[used]='\0';
    char *start=strstr(body,"\r\n\r\n");if(!start)return -1;start+=4;memmove(body,start,strlen(start)+1);return 0;
}

static void json_value(const char *json,const char *section,const char *key,const char *fallback,char *out,size_t cap){
    const char *p=strstr(json,section);if(!p){snprintf(out,cap,"%s",fallback);return;}p=strstr(p,key);if(!p){snprintf(out,cap,"%s",fallback);return;}p=strchr(p,':');if(!p){snprintf(out,cap,"%s",fallback);return;}p++;while(*p==' '||*p=='\t')p++;
    size_t n=0;if(*p=='\"'){p++;while(p[n]&&p[n]!='\"'&&n+1<cap)n++;}else while(p[n]&&strchr(",}\r\n",p[n])==NULL&&n+1<cap)n++;
    memcpy(out,p,n);out[n]='\0';
}

static int moonraker_status(int control_port,char *out,size_t cap,int notification,int id){
    char source[PANDA_BODY_MAX],et[32],eg[32],bt[32],bg[32],progress[32],state[64];
    if(local_printer_json(control_port,source,sizeof(source))<0)snprintf(source,sizeof(source),"{}");
    json_value(source,"\"extruder\"","\"temperature\"","0",et,sizeof(et));json_value(source,"\"extruder\"","\"target\"","0",eg,sizeof(eg));
    json_value(source,"\"heater_bed\"","\"temperature\"","0",bt,sizeof(bt));json_value(source,"\"heater_bed\"","\"target\"","0",bg,sizeof(bg));
    json_value(source,"\"machine\"","\"progress\"","0",progress,sizeof(progress));json_value(source,"\"print\"","\"state\"","standby",state,sizeof(state));
    if (!state[0]) snprintf(state,sizeof(state),"standby");
    double fraction=strtod(progress,NULL)/100.0;
    if(notification)return snprintf(out,cap,"{\"jsonrpc\":\"2.0\",\"method\":\"notify_status_update\",\"params\":[{\"webhooks\":{\"state\":\"ready\"},\"virtual_sdcard\":{\"progress\":%.5f},\"print_stats\":{\"state\":\"%s\"},\"extruder\":{\"temperature\":%s,\"target\":%s},\"heater_bed\":{\"temperature\":%s,\"target\":%s},\"gcode_macro _KNOMI_STATUS\":{}},0.0]}",fraction,state,et,eg,bt,bg);
    return snprintf(out,cap,"{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":{\"status\":{\"webhooks\":{\"state\":\"ready\"},\"virtual_sdcard\":{\"progress\":%.5f},\"print_stats\":{\"state\":\"%s\"},\"extruder\":{\"temperature\":%s,\"target\":%s},\"heater_bed\":{\"temperature\":%s,\"target\":%s},\"gcode_macro _KNOMI_STATUS\":{}},\"eventtime\":0.0}}",id,fraction,state,et,eg,bt,bg);
}

static int ws_send(int fd,unsigned char opcode,const char *data,size_t len){unsigned char h[10];size_t hn=0;h[hn++]=0x80|opcode;if(len<126)h[hn++]=(unsigned char)len;else if(len<=65535){h[hn++]=126;h[hn++]=(unsigned char)(len>>8);h[hn++]=(unsigned char)len;}else return -1;return send_all(fd,h,hn)||send_all(fd,data,len)?-1:0;}

static int ws_read(int fd,char *out,size_t cap,int *opcode){unsigned char h[2];if(recv(fd,h,2,MSG_WAITALL)!=2)return -1;*opcode=h[0]&15;uint64_t len=h[1]&127;if(len==126){unsigned char e[2];if(recv(fd,e,2,MSG_WAITALL)!=2)return -1;len=((uint64_t)e[0]<<8)|e[1];}else if(len==127)return -1;unsigned char mask[4]={0};if(h[1]&128)if(recv(fd,mask,4,MSG_WAITALL)!=4)return -1;if(len+1>cap)return -1;if(recv(fd,out,(size_t)len,MSG_WAITALL)!=(ssize_t)len)return -1;for(size_t i=0;i<len;i++)out[i]^=mask[i&3];out[len]='\0';return (int)len;}

static const char *header_value(char *request,const char *name){char *p=strstr(request,name);if(!p)return NULL;p+=strlen(name);while(*p==' '||*p=='\t')p++;char *e=strstr(p,"\r\n");if(e)*e='\0';return p;}

static void http_reply(int fd,int code,const char *status,const char *body){char h[512];size_t len=strlen(body);int n=snprintf(h,sizeof(h),"HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nAccess-Control-Allow-Origin: *\r\nConnection: close\r\n\r\n",code,status,len);if(n>0)send_all(fd,h,(size_t)n);send_all(fd,body,len);}

static void *handle_client(void *arg){
    panda_client *client=arg;int fd=client->fd;int control_port=client->server->control_port;free(client);
    struct timeval tv={3,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
    char req[PANDA_REQUEST_MAX];ssize_t n=recv(fd,req,sizeof(req)-1,0);if(n<=0){close(fd);return NULL;}req[n]='\0';
    char path[512]="/";sscanf(req,"%*15s %511s",path);
    if(strcmp(path,"/websocket")!=0){
        if(strncmp(path,"/server/info",12)==0)http_reply(fd,200,"OK","{\"result\":{\"klippy_connected\":true,\"klippy_state\":\"ready\",\"components\":[\"websocket\"]}}\n");
        else if(strncmp(path,"/printer/info",13)==0)http_reply(fd,200,"OK","{\"result\":{\"state\":\"ready\",\"state_message\":\"Printer is ready\",\"hostname\":\"CC2\"}}\n");
        else if(strncmp(path,"/printer/objects/list",21)==0)http_reply(fd,200,"OK","{\"result\":{\"objects\":[\"webhooks\",\"virtual_sdcard\",\"print_stats\",\"extruder\",\"heater_bed\",\"gcode_macro _KNOMI_STATUS\"]}}\n");
        else if(strncmp(path,"/printer/objects/query",22)==0){char body[4096];moonraker_status(control_port,body,sizeof(body),0,0);http_reply(fd,200,"OK",body);}
        else http_reply(fd,404,"Not Found","{\"error\":{\"code\":404,\"message\":\"Read-only Panda compatibility endpoint\"}}\n");
        close(fd);return NULL;
    }
    const char *key=header_value(req,"Sec-WebSocket-Key:");if(!key){close(fd);return NULL;}char joined[256],accept[64],response[512];unsigned char digest[20];snprintf(joined,sizeof(joined),"%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11",key);sha1((unsigned char*)joined,strlen(joined),digest);base64(digest,sizeof(digest),accept,sizeof(accept));int rn=snprintf(response,sizeof(response),"HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: %s\r\n\r\n",accept);if(send_all(fd,response,(size_t)rn)<0){close(fd);return NULL;}
    tv.tv_sec=1;setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));int subscribed=0;
    while(1){char message[4096];int opcode=0;int got=ws_read(fd,message,sizeof(message),&opcode);if(got>0&&opcode==1){int id=1001;char *ip=strstr(message,"\"id\"");if(ip){ip=strchr(ip,':');if(ip)id=atoi(ip+1);}if(strstr(message,"printer.objects.query")||strstr(message,"printer.objects.subscribe")){char body[4096];moonraker_status(control_port,body,sizeof(body),0,id);if(ws_send(fd,1,body,strlen(body))<0)break;subscribed=1;}else{char body[256];int len=snprintf(body,sizeof(body),"{\"jsonrpc\":\"2.0\",\"id\":%d,\"error\":{\"code\":-32601,\"message\":\"Read-only Panda compatibility endpoint\"}}",id);if(ws_send(fd,1,body,(size_t)len)<0)break;}}else if(got>0&&opcode==8)break;else if(got>0&&opcode==9){if(ws_send(fd,10,message,(size_t)got)<0)break;}else if(got<0&&errno!=EAGAIN&&errno!=EWOULDBLOCK)break;
        if(subscribed){char body[4096];moonraker_status(control_port,body,sizeof(body),1,0);if(ws_send(fd,1,body,strlen(body))<0)break;}
    }
    close(fd);return NULL;
}

static void *server_thread(void *arg){panda_server *s=arg;while(s->started){int fd=accept(s->server_fd,NULL,NULL);if(fd<0){if(errno==EINTR)continue;if(!s->started)break;continue;}panda_client *c=malloc(sizeof(*c));if(!c){close(fd);continue;}c->server=s;c->fd=fd;pthread_t thread;if(pthread_create(&thread,NULL,handle_client,c)!=0){free(c);close(fd);}else pthread_detach(thread);}return NULL;}

int panda_start(panda_server *s,int port,int control_port){memset(s,0,sizeof(*s));s->server_fd=-1;if(port==0)return 0;int fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return -1;int reuse=1;setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_ANY);a.sin_port=htons((unsigned short)port);if(bind(fd,(struct sockaddr*)&a,sizeof(a))<0||listen(fd,4)<0){close(fd);return -1;}pthread_t *thread=malloc(sizeof(*thread));if(!thread){close(fd);return -1;}s->port=port;s->control_port=control_port;s->server_fd=fd;s->started=1;s->thread=thread;if(pthread_create(thread,NULL,server_thread,s)!=0){s->started=0;close(fd);free(thread);s->thread=NULL;return -1;}return 0;}

void panda_stop(panda_server *s){if(!s||!s->started)return;s->started=0;shutdown(s->server_fd,SHUT_RDWR);close(s->server_fd);pthread_join(*(pthread_t*)s->thread,NULL);free(s->thread);s->thread=NULL;s->server_fd=-1;}
