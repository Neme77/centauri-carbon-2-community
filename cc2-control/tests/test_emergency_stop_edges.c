/* Regression tests for the direct emergency endpoint; fake firmware sockets only. */
#include "../src/console.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static char path[108];
static int server(int backlog) {
    int fd=socket(AF_UNIX,SOCK_STREAM,0);assert(fd>=0);
    struct sockaddr_un a={.sun_family=AF_UNIX};
    snprintf(a.sun_path,sizeof(a.sun_path),"%s",path);
    unlink(path);
    assert(bind(fd,(struct sockaddr *)&a,sizeof(a))==0);
    assert(listen(fd,backlog)==0);
    return fd;
}
static int connect_one(void) {
    int fd=socket(AF_UNIX,SOCK_STREAM,0);assert(fd>=0);
    struct sockaddr_un a={.sun_family=AF_UNIX};
    snprintf(a.sun_path,sizeof(a.sun_path),"%s",path);
    assert(connect(fd,(struct sockaddr *)&a,sizeof(a))==0);
    return fd;
}
static void pause_ms(long ms) {
    struct timespec interval={ms/1000,(ms%1000)*1000000};
    nanosleep(&interval,NULL);
}
static void full_accept_queue(void) {
    int listener=server(0),filler=connect_one();
    pid_t child=fork();assert(child>=0);
    if(!child) {
        close(listener);close(filler);
        static console_state state;char reason[160];
        console_init(&state,path);
        int rc=console_emergency_stop(&state,reason,sizeof(reason));
        console_destroy(&state);
        _exit(rc==0?0:1);
    }
    int status=0,blocked=1;
    for(int n=0;n<30;n++) {
        pid_t result=waitpid(child,&status,WNOHANG);
        assert(result>=0);
        if(result==child){blocked=0;break;}
        pause_ms(100);
    }
    if(blocked) {
        /* Terminate only this isolated reviewer child, never a device process. */
        assert(kill(child,SIGKILL)==0);
        assert(waitpid(child,&status,0)==child);
    }
    close(filler);close(listener);unlink(path);
    assert(!blocked && WIFEXITED(status) && WEXITSTATUS(status)==1);
    puts("PASS: emergency connection returns failure within 3 seconds with a full Unix-socket accept queue.");
}
static void long_error_reply(void) {
    int listener=server(1);
    pid_t child=fork();assert(child>=0);
    if(!child) {
        int fd=accept(listener,NULL,NULL);if(fd<0)_exit(2);
        char request[256];size_t used=0;
        while(used<sizeof(request) && !memchr(request,3,used)) {
            ssize_t got=read(fd,request+used,sizeof(request)-used);
            if(got<=0)_exit(3);
            used+=(size_t)got;
        }
        char reply[1024];
        size_t length=(size_t)snprintf(reply,sizeof(reply),"{\"id\":112,\"error\":{\"message\":\"");
        memset(reply+length,'x',600);length+=600;
        memcpy(reply+length,"\"}}\003",4);length+=4;
        size_t sent=0;
        while(sent<length) {
            ssize_t count=send(fd,reply+sent,length-sent,MSG_NOSIGNAL);
            if(count<=0)_exit(4);
            sent+=(size_t)count;
        }
        close(fd);close(listener);_exit(0);
    }
    close(listener);
    static console_state state;char reason[160];
    console_init(&state,path);
    int rc=console_emergency_stop(&state,reason,sizeof(reason));
    console_destroy(&state);
    int status=0;
    assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==0);
    unlink(path);
    assert(rc==-1 && strstr(reason,"refused"));
    puts("PASS: a complete firmware error frame over 511 bytes is refused, preserving the caller's M112 fallback.");
}
int main(void) {
    snprintf(path,sizeof(path),"/tmp/cc2-review264-%ld.sock",(long)getpid());
    full_accept_queue();long_error_reply();
    puts("PASS: emergency connection backlog and long error regressions.");
    return 0;
}
