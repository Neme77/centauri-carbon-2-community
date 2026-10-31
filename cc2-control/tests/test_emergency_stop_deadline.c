/* Fake firmware only. No production printer socket is contacted. */
#include "../src/console.h"
#include <assert.h>
#include <errno.h>
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
static void pause_ms(long ms) {
    struct timespec t={ms/1000,(ms%1000)*1000000};
    while(nanosleep(&t,&t)<0 && errno==EINTR) {}
}
static long long now_ms(void) {
    struct timespec t;assert(clock_gettime(CLOCK_MONOTONIC,&t)==0);
    return (long long)t.tv_sec*1000+t.tv_nsec/1000000;
}
static void send_reply(int fd,const char *data,size_t length) {
    while(length) {
        ssize_t n=send(fd,data,length,MSG_NOSIGNAL);
        if(n<0 && (errno==EPIPE || errno==ECONNRESET)) return;
        assert(n>0);data+=n;length-=(size_t)n;
    }
}
static pid_t firmware(int scenario) {
    int s=socket(AF_UNIX,SOCK_STREAM,0);assert(s>=0);
    struct sockaddr_un a={.sun_family=AF_UNIX};
    snprintf(a.sun_path,sizeof(a.sun_path),"%s",path);unlink(path);
    assert(bind(s,(struct sockaddr*)&a,sizeof(a))==0 && listen(s,1)==0);
    pid_t child=fork();assert(child>=0);
    if(!child) {
        int fd=accept(s,NULL,NULL);assert(fd>=0);close(s);
        char request[256];size_t used=0;
        while(!memchr(request,3,used)) {
            ssize_t n=read(fd,request+used,sizeof(request)-used);
            assert(n>0);used+=(size_t)n;assert(used<sizeof(request));
        }
        if(scenario==0) pause_ms(2500); /* silent peer */
        if(scenario==1) send_reply(fd,"{\"id\":112",9); /* partial then EOF */
        if(scenario==2) {
            char data[9000];memset(data,'x',sizeof(data));
            send_reply(fd,data,sizeof(data)); /* bounded buffer overflow */
        }
        if(scenario==3 || scenario==4) {
            const char *parts[]={"{\"id\":112,","\"result\":{}","}\003"};
            if(scenario==4) parts[1]="\"error\":{\"message\":\"no\"}";
            for(int i=0;i<3;i++){send_reply(fd,parts[i],strlen(parts[i]));pause_ms(20);}
        }
        if(scenario==5) { /* readiness and EINTR cannot reset the deadline */
            for(int i=0;i<25;i++){send_reply(fd,"x",1);pause_ms(100);}
        }
        close(fd);_exit(0);
    }
    close(s);return child;
}
static void alarm_handler(int sig) {(void)sig;}
int main(void) {
    snprintf(path,sizeof(path),"/tmp/cc2-deadline-%ld.sock",(long)getpid());
    struct sigaction action={0};action.sa_handler=alarm_handler;
    sigemptyset(&action.sa_mask);assert(sigaction(SIGALRM,&action,NULL)==0);
    static console_state state;console_init(&state,path);
    for(int scenario=0;scenario<6;scenario++) {
        atomic_store(&state.emergency_sent,0);
        pid_t child=firmware(scenario);char reason[160];
        long long start=now_ms();alarm(1);
        int rc=console_emergency_stop(&state,reason,sizeof(reason));
        long long elapsed=now_ms()-start;alarm(0);
        assert(rc==(scenario==3?0:-1));
        assert(atomic_load(&state.emergency_sent)==1);
        assert(elapsed<2400);
        if(scenario==0 || scenario==5) assert(elapsed>=1800);
        int status;assert(waitpid(child,&status,0)==child);
        assert(WIFEXITED(status) && WEXITSTATUS(status)==0);
        unlink(path);
    }
    console_destroy(&state);
    puts("PASS: silent and trickling peers, interrupted waits, fragmented success/error, truncated and oversized replies; total deadline enforced");
}
