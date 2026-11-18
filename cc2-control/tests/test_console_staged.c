#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
/* R11: a staged console job is sent line by line and can be cancelled between two lines. The printer is faked:
 * every gcode/script request is answered with its result; one line can be answered with an error, and a cancel
 * can be asked for while a chosen line runs. */
static char sent_lines[32][160];
static int sent_count, reply_pending, reply_error;
static const char *cancel_at, *error_at;
static atomic_int *cancel_flag;
static int fake_socket(int domain,int type,int protocol){(void)domain;(void)type;(void)protocol;return 42;}
static int fake_connect(int fd,const struct sockaddr *address,socklen_t length){(void)fd;(void)address;(void)length;return 0;}
static ssize_t fake_send(int fd,const void *data,size_t length,int flags){
    (void)fd;(void)flags;
    const char *text=data,*script=strstr(text,"\"script\":\"");
    if(script&&sent_count<32){
        script+=10;
        size_t n=strcspn(script,"\"");
        if(n>=sizeof(sent_lines[0]))n=sizeof(sent_lines[0])-1;
        memcpy(sent_lines[sent_count],script,n);sent_lines[sent_count][n]=0;
        reply_pending=1;
        reply_error=error_at&&strstr(sent_lines[sent_count],error_at)!=NULL;
        if(cancel_at&&strstr(sent_lines[sent_count],cancel_at))atomic_store(cancel_flag,1);
        sent_count++;
    }
    return (ssize_t)length;
}
static int fake_poll(struct pollfd *fds,nfds_t count,int timeout){(void)fds;(void)count;(void)timeout;return 1;}
static ssize_t fake_read(int fd,void *data,size_t length){
    (void)fd;
    if(!reply_pending)return 0;
    reply_pending=0;
    const char *reply=reply_error?"{\"error\":{\"message\":\"probe failed\"},\"id\":101}\003":"{\"report\":\"z=0.1\"}\003{\"id\":101,\"result\":{}}\003";
    size_t n=strlen(reply);
    assert(n<=length);
    memcpy(data,reply,n);
    return (ssize_t)n;
}
static int fake_close(int fd){(void)fd;return 0;}
#define socket fake_socket
#define connect fake_connect
#define send fake_send
#define poll fake_poll
#define read fake_read
#define close fake_close
#include "../src/console.c"
#undef close
#undef read
#undef poll
#undef send

static const char SCRIPT[]="SAVE_GCODE_STATE NAME=T\nM140 S60\nG1 X1 Y1\nPROBE SAMPLES=3\nG1 X2 Y2\nPROBE SAMPLES=3\nRESTORE_GCODE_STATE NAME=T\nM140 S0";
static const char TAIL[]="G90\nG1 Z10 F600\nRESTORE_GCODE_STATE NAME=T\nM104 S0\nM140 S0";
static console_state state;
/* What console_start does before it starts the worker thread; the worker runs here in the test's own thread. */
static void run(const char *script,const char *tail,const char *cancel,const char *error){
    sent_count=0;reply_pending=0;cancel_at=cancel;error_at=error;cancel_flag=&state.cancel_requested;
    if(tail)console_arm_staged(&state,tail);
    pthread_mutex_lock(&state.lock);
    snprintf(state.command,sizeof(state.command),"%s",script);
    state.output[0]=0;state.output_len=0;state.busy=1;state.completed=0;state.success=0;state.generation++;
    state.staged=state.staged_next;state.staged_next=0;state.cancelled=0;
    snprintf(state.cancel_tail,sizeof(state.cancel_tail),"%s",state.cancel_tail_next);
    atomic_store(&state.cancel_requested,0);
    pthread_mutex_unlock(&state.lock);
    console_worker(&state);
}
int main(void){
    char json[CONSOLE_OUTPUT_MAX*2+1024];
    console_init(&state,"/test");
    assert(console_cancel(&state)==-1);

    /* Not staged: one request with the whole block, as before. */
    run(SCRIPT,NULL,NULL,NULL);
    assert(sent_count==1&&strstr(sent_lines[0],"PROBE"));
    assert(state.completed&&!state.busy&&!state.cancelled); /* (the faked socket closes, so its success is not judged here) */
    puts("PASS: an unstaged job still goes out as one block");

    /* Staged: every line on its own, in order, the reports of every line in the output. */
    run(SCRIPT,TAIL,NULL,NULL);
    assert(sent_count==8);
    assert(!strcmp(sent_lines[0],"SAVE_GCODE_STATE NAME=T")&&!strcmp(sent_lines[3],"PROBE SAMPLES=3")&&!strcmp(sent_lines[7],"M140 S0"));
    assert(state.success&&state.completed&&!state.busy&&!state.cancelled);
    {int reports=0;for(const char *p=state.output;(p=strstr(p,"z=0.1"));p++)reports++;assert(reports==8);}
    assert(!strstr(state.output,"cancelled"));
    puts("PASS: a staged job sends its lines one by one and keeps all reports");

    /* Cancelled while the first probe runs: that line ends, the rest is left out, the tail runs, the job failed. */
    run(SCRIPT,TAIL,"PROBE",NULL);
    assert(sent_count==4+5);
    assert(!strcmp(sent_lines[3],"PROBE SAMPLES=3"));
    assert(!strcmp(sent_lines[4],"G90")&&!strcmp(sent_lines[5],"G1 Z10 F600")&&!strcmp(sent_lines[6],"RESTORE_GCODE_STATE NAME=T"));
    assert(!strcmp(sent_lines[7],"M104 S0")&&!strcmp(sent_lines[8],"M140 S0"));
    assert(!state.success&&state.completed&&!state.busy&&state.cancelled);
    assert(strstr(state.output,"[CC2 Control: cancelled]"));
    assert(console_cancelled(&state,state.generation)&&!console_cancelled(&state,state.generation+1));
    console_json(&state,json,sizeof(json));
    assert(strstr(json,"\"cancellable\":false,\"cancelling\":false,\"cancelled\":true"));
    puts("PASS: a cancel ends after the running line, runs the tail and fails the job");

    /* The tail runs on past an error (G1 Z10 without homing): the heaters are still switched off. */
    run(SCRIPT,TAIL,"M140 S60","G1 Z10");
    assert(sent_count==2+5&&!strcmp(sent_lines[6],"M140 S0"));
    assert(!state.success&&state.cancelled);
    puts("PASS: the cancel tail runs on past an error");

    /* An error result ends a staged job where a block would have been aborted: no tail, the result counts as seen. */
    run(SCRIPT,TAIL,NULL,"PROBE");
    assert(sent_count==4);
    assert(state.success&&state.completed&&!state.cancelled);
    assert(strstr(state.output,"probe failed"));
    puts("PASS: an error result stops a staged job without the cancel tail");

    /* The next job starts clean. */
    run("M105",NULL,NULL,NULL);
    assert(!state.cancelled&&!state.staged&&sent_count==1);
    console_json(&state,json,sizeof(json));
    assert(strstr(json,"\"cancelled\":false"));

    /* While a staged job runs a cancel is accepted and reported; a second start is refused and disarms. */
    pthread_mutex_lock(&state.lock);state.busy=1;state.staged=1;pthread_mutex_unlock(&state.lock);
    console_json(&state,json,sizeof(json));
    assert(strstr(json,"\"cancellable\":true,\"cancelling\":false"));
    assert(console_cancel(&state)==0);
    console_json(&state,json,sizeof(json));
    assert(strstr(json,"\"cancellable\":true,\"cancelling\":true"));
    console_arm_staged(&state,TAIL);
    char reason[64];
    assert(console_start(&state,"M105",reason,sizeof(reason))==-1&&!state.staged_next);
    pthread_mutex_lock(&state.lock);state.staged=0;pthread_mutex_unlock(&state.lock);
    assert(console_cancel(&state)==-1);
    puts("PASS: cancel only for a running staged job; a refused start disarms the staging");
    pthread_mutex_lock(&state.lock);state.busy=0;pthread_mutex_unlock(&state.lock);
    console_destroy(&state);
    return 0;
}
