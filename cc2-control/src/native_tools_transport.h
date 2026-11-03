#ifndef CC2_NATIVE_TOOLS_TRANSPORT_H
#define CC2_NATIVE_TOOLS_TRANSPORT_H
/* HTTP-loop owned, on-demand only. Fixed total deadline and bounded work per tick. */
#include "canvas_json.h"
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#define NT_REPLY_MAX 16384
typedef struct {
    int fd,connecting,started,done,success;
    long long deadline;
    size_t sent,used,reports_used;
    unsigned reports_count;
    char request[768],input[NT_REPLY_MAX+1],reports[NT_REPLY_MAX+1];
    const char *error;
} nt_call;
static inline void nt_close(nt_call *x){if(x->fd>=0)close(x->fd);x->fd=-1;}
static inline int nt_fail(nt_call *x,const char *error){nt_close(x);x->done=1;x->success=0;x->error=error;return -1;}
static inline int nt_start(nt_call *x,const char *script,const char *path,long long now,int timeout){
    if(x->started&&!x->done)return 0;
    memset(x,0,sizeof(*x));x->fd=-1;x->started=1;x->deadline=now+timeout;
    /* The caller supplies only a known command plus validated decimal literals. */
    if(!script||strlen(script)>220||strpbrk(script,"\"\\\r\n")||strlen(path)>=sizeof(((struct sockaddr_un *)0)->sun_path))
        return nt_fail(x,"Invalid native tool request");
    int n=snprintf(x->request,sizeof(x->request),
        "{\"id\":410,\"method\":\"gcode/subscribe_report\",\"params\":{\"response_template\":{}}}\003"
        "{\"id\":411,\"method\":\"gcode/script\",\"params\":{\"script\":\"%s\"}}\003",script);
    if(n<0||(size_t)n>=sizeof(x->request))return nt_fail(x,"Native tool request too large");
    x->fd=socket(AF_UNIX,SOCK_STREAM,0);if(x->fd<0)return nt_fail(x,"Cannot create native tool socket");
    if(fcntl(x->fd,F_SETFL,O_NONBLOCK)<0||fcntl(x->fd,F_SETFD,FD_CLOEXEC)<0)return nt_fail(x,"Cannot prepare native tool socket");
    struct sockaddr_un address;memset(&address,0,sizeof(address));address.sun_family=AF_UNIX;
    snprintf(address.sun_path,sizeof(address.sun_path),"%s",path);
    if(connect(x->fd,(struct sockaddr *)&address,sizeof(address))<0){
        if(errno!=EINPROGRESS)return nt_fail(x,"Cannot connect to native tool service");
        x->connecting=1;
    }
    return 1;
}
/* Only complete, bounded JSON frames reach here. Reports remain JSON strings,
 * so the browser decodes native Unicode/newlines without HTML interpretation. */
static inline int nt_frame(nt_call *x,const char *frame,size_t length){
    const char *end=frame+length,*root=ccv_json_skip_space(frame,end);
    if(memchr(frame,0,length)||root==end||*root!='{')return nt_fail(x,"Malformed native tool reply");
    const char *re=ccv_json_container_end(root,end);
    if(!re||ccv_json_skip_space(re,end)!=end)return nt_fail(x,"Malformed native tool reply");
    double id;if(!ccv_numeric(root,re,"id",&id))return nt_fail(x,"Native tool reply has no valid ID");
    if(id==410){if(ccv_json_member(root,re,"error"))return nt_fail(x,"Native tool reports unavailable");return 0;}
    if(id==411){
        if(ccv_json_member(root,re,"error")||!ccv_json_member(root,re,"result"))return nt_fail(x,"Native tool command refused");
        nt_close(x);x->done=x->success=1;return 1;
    }
    if(id!=0)return 0;
    const char *report_end=NULL,*report=ccv_json_member_object(root,re,"report",'{',&report_end);
    const char *message=report?ccv_json_member(report,report_end,"message"):NULL;
    if(!message||*message!='"')return 0;
    const char *me=ccv_json_string_end(message,report_end);if(!me)return nt_fail(x,"Malformed native tool report");
    size_t n=(size_t)(me-message);
    if(x->reports_used+n+2>NT_REPLY_MAX)return nt_fail(x,"Native tool output too large");
    if(x->reports_count++)x->reports[x->reports_used++]=',';
    memcpy(x->reports+x->reports_used,message,n);x->reports_used+=n;x->reports[x->reports_used]=0;
    return 0;
}
static inline int nt_step(nt_call *x,long long now){
    if(x->done)return x->success?1:-1;
    if(!x->started||now>=x->deadline)return nt_fail(x,"Native tool command timed out");
    if(x->connecting){
        struct pollfd pfd={x->fd,POLLOUT,0};int ready=poll(&pfd,1,0);
        if(ready<0&&errno!=EINTR)return nt_fail(x,"Native tool connection failed");
        if(ready<=0)return 0;
        int error=0;socklen_t size=sizeof(error);
        if(getsockopt(x->fd,SOL_SOCKET,SO_ERROR,&error,&size)||error)return nt_fail(x,"Native tool connection failed");
        x->connecting=0;
    }
    size_t wanted=strlen(x->request);
    if(x->sent<wanted){
        ssize_t n=send(x->fd,x->request+x->sent,wanted-x->sent,MSG_NOSIGNAL|MSG_DONTWAIT);
        if(n<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))return 0;
        if(n<=0)return nt_fail(x,"Cannot send native tool command");
        x->sent+=(size_t)n;if(x->sent<wanted)return 0;
    }
    for(int turn=0;turn<8;turn++){
        const char *sep=memchr(x->input,3,x->used);
        if(sep){
            size_t n=(size_t)(sep-x->input);int result=nt_frame(x,x->input,n);
            if(result)return result;
            memmove(x->input,x->input+n+1,x->used-n-1);x->used-=n+1;continue;
        }
        if(x->used==NT_REPLY_MAX)return nt_fail(x,"Native tool frame too large");
        ssize_t n=recv(x->fd,x->input+x->used,NT_REPLY_MAX-x->used,MSG_DONTWAIT);
        if(n<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))return 0;
        if(n<=0)return nt_fail(x,"Native tool connection ended before completion");
        x->used+=(size_t)n;
    }
    return 0;
}
#endif
