#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
/* The staged console against the frames the printer service really sends for a refused line. Recorded on a printer on
 * 20.11.2026 (SET_HEATER_TEMPERATURE HEATER=cc2_nosuch TARGET=0, console output byte for byte, frame ends are 0x03 on the
 * wire): the acknowledgement of the report subscription (id 100), a report with error_code 0 whose message says
 * "!! [error_code 803] ...", then the error frame with "error" in front of "id":101 and no space anywhere. The other
 * console tests feed invented frames; this one feeds the recorded ones, whole, torn at every position, byte by byte, and
 * without the report in front. A refused line must fail the job, send nothing more and keep the text; the report with
 * error_code 0 must not fail anything by itself. */
#define ACK "{\"id\":100,\"result\":{}}"
#define REPORT "{\"id\":0,\"report\":{\"error_code\":0,\"error_level\":0,\"message\":\"!! [error_code 803] CommandError: The value 'cc2_nosuch' is not valid for HEATER\"}}"
#define ERRFRAME "{\"error\":{\"error\":\"WebRequestError\",\"message\":\"[error_code 803] WebRequestError: [error_code 803] CommandError: The value 'cc2_nosuch' is not valid for HEATER\"},\"id\":101}"
#define OKFRAME "{\"id\":101,\"result\":{}}"
#define BAD_LINE "SET_HEATER_TEMPERATURE HEATER=cc2_nosuch TARGET=0"

static char sent_lines[32][160];
static int sent_count;
static char stream[2048];
static size_t stream_len, stream_pos;
static size_t first_read, later_read; /* read size limits: 0 = everything that is left */
static const char *bad_reply; /* the reply of BAD_LINE as bytes, set per scenario */
static size_t reads_done;
static int ack_pending;       /* the acknowledgement of the subscription rides in front of the first reply */

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
        stream[0]=0;
        if(ack_pending){strcat(stream,ACK "\003");ack_pending=0;}
        strcat(stream,strstr(sent_lines[sent_count],"cc2_nosuch")?bad_reply:OKFRAME "\003");
        stream_len=strlen(stream);stream_pos=0;reads_done=0;
        sent_count++;
    }
    return (ssize_t)length;
}
static int fake_poll(struct pollfd *fds,nfds_t count,int timeout){(void)fds;(void)count;(void)timeout;return 1;}
static ssize_t fake_read(int fd,void *data,size_t length){
    (void)fd;
    size_t left=stream_len-stream_pos;
    if(!left)return 0;
    size_t limit=reads_done==0?first_read:later_read;
    size_t n=limit&&limit<left?limit:left;
    assert(n<=length);
    memcpy(data,stream+stream_pos,n);
    stream_pos+=n;reads_done++;
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

static console_state state;
static const char STOPPED[]="[CC2 Control: the printer reported an error; the job was stopped]\n";
static const char TAIL[]="G90\nRESTORE_GCODE_STATE NAME=T\nM104 S0\nM140 S0";
static int sent_line(const char *what){for(int i=0;i<sent_count;i++)if(!strcmp(sent_lines[i],what))return 1;return 0;}
/* Run a staged job against the fake printer whose answer to BAD_LINE is `reply`, delivered in reads of the given size. */
static void job(const char *script,const char *reply,int with_ack,size_t first,size_t later){
    sent_count=0;reads_done=0;stream_len=stream_pos=0;
    bad_reply=reply;ack_pending=with_ack;first_read=first;later_read=later;
    console_arm_staged(&state,TAIL,CONSOLE_AXES_ALL);
    pthread_mutex_lock(&state.lock);
    snprintf(state.command,sizeof(state.command),"%s",script);
    state.output[0]=0;state.output_len=0;state.busy=1;state.completed=0;state.success=0;state.generation++;
    state.staged=state.staged_next;state.staged_next=0;state.cancelled=0;state.staged_homed=state.staged_homed_next;state.staged_homed_next=0;
    snprintf(state.cancel_tail,sizeof(state.cancel_tail),"%s",state.cancel_tail_next);
    atomic_store(&state.cancel_requested,0);
    pthread_mutex_unlock(&state.lock);
    console_worker(&state);
}
/* The job failed with the printer's error: nothing after the refused line (lines or tail) went out, the text stayed. */
static void expect_refused(const char *what,int lines_sent){
    if(!(!state.success&&state.completed&&!state.cancelled&&!state.busy)){fprintf(stderr,"%s: job not failed (success=%d completed=%d cancelled=%d)\n",what,state.success,state.completed,state.cancelled);abort();}
    if(sent_count!=lines_sent){fprintf(stderr,"%s: %d lines sent, expected %d\n",what,sent_count,lines_sent);abort();}
    if(sent_line("M140 S31")||sent_line("G90")||sent_line("M104 S0")||sent_line("M140 S0")){fprintf(stderr,"%s: a line after the error went out\n",what);abort();}
    if(!strstr(state.output,"cc2_nosuch is not valid")&&!strstr(state.output,"'cc2_nosuch' is not valid for HEATER")){fprintf(stderr,"%s: error text missing\n",what);abort();}
    if(!strstr(state.output,STOPPED)||strstr(state.output,"connection ended")){fprintf(stderr,"%s: message wrong:\n%s\n",what,state.output);abort();}
}

int main(void){
    console_init(&state,"/test");
    const char real[]=REPORT "\003" ERRFRAME "\003";
    const char no_report[]=ERRFRAME "\003";
    const char error_first[]=ERRFRAME "\003" REPORT "\003";
    const char *replies[]={real,no_report,error_first};
    const char *names[]={"recorded frames","error frame without the report","error frame before the report"};

    /* The detection itself, frame by frame: the recorded error frame is a result 0 (error) as soon as its end arrives,
     * the acknowledgement and the report with error_code 0 are nothing. */
    {
        const char wire[]=ACK "\003" REPORT "\003" ERRFRAME "\003";
        const size_t ack_end=sizeof(ACK),report_end=ack_end+sizeof(REPORT),total=sizeof(wire)-1;
        for(int bytewise=0;bytewise<2;bytewise++){
            staged_frame frame={{0},0};staged_seen seen={0,0,0};
            sent_count=0;console_init(&state,"/test");
            int result=-1;
            for(size_t i=0;i<total;i+=(bytewise?1:total)){
                int r=staged_feed(&state,&frame,wire+i,bytewise?1:total,&seen);
                if(r>=0){assert(result<0);result=r;if(bytewise)assert(i==total-1);}
                if(bytewise&&i+1<=report_end)assert(!seen.fault&&result<0); /* nothing before the error frame is an error */
            }
            assert(result==0&&!seen.fault);
        }
        puts("PASS: the recorded frames, whole and byte by byte: the error frame is result 0, the report with error_code 0 is no fault");
    }

    /* The refused line first (the recorded job), and as the second line behind one that went through. */
    static const char ONE[]=BAD_LINE;
    static const char SECOND[]="M140 S45\n" BAD_LINE "\nM140 S31\nM190 S45";
    static const char FIRST_OF_MANY[]=BAD_LINE "\nM140 S31\nM190 S45";
    for(int variant=0;variant<3;variant++){
        const char *script=variant==0?ONE:variant==1?FIRST_OF_MANY:SECOND;
        int expected=variant==2?2:1;
        for(int s=0;s<3;s++){
            /* the acknowledgement of the subscription rides in front of the first reply, except in one run without it */
            for(int with_ack=1;with_ack>=(s==1?0:1);with_ack--){
                char what[160];
                snprintf(what,sizeof(what),"%s, script %d, ack %d, whole",names[s],variant,with_ack);
                job(script,replies[s],with_ack,0,0);
                expect_refused(what,expected);
                if(variant==0&&s==0){ /* the recorded console text, byte for byte */
                    static const char RECORDED[]="> " BAD_LINE "\n" ACK "\n" REPORT "\n" ERRFRAME "\n";
                    if(strncmp(state.output,RECORDED,sizeof(RECORDED)-1)){fprintf(stderr,"console text differs:\n%s\n",state.output);abort();}
                    if(strcmp(state.output+sizeof(RECORDED)-1,STOPPED)){fprintf(stderr,"console text ends differently:\n%s\n",state.output);abort();}
                }
                /* torn at every position into two reads (the first read of each reply, then the rest) */
                size_t bad_total=strlen(ACK "\003")+strlen(replies[s]);
                for(size_t pos=1;pos<bad_total;pos++){
                    snprintf(what,sizeof(what),"%s, script %d, ack %d, torn at %zu",names[s],variant,with_ack,pos);
                    job(script,replies[s],with_ack,pos,0);
                    expect_refused(what,expected);
                }
                /* byte by byte, and in small odd reads */
                static const size_t sizes[]={1,2,3,7,64};
                for(size_t k=0;k<sizeof(sizes)/sizeof(sizes[0]);k++){
                    snprintf(what,sizeof(what),"%s, script %d, ack %d, reads of %zu",names[s],variant,with_ack,sizes[k]);
                    job(script,replies[s],with_ack,sizes[k],sizes[k]);
                    expect_refused(what,expected);
                }
            }
        }
    }
    puts("PASS: a refused line fails the job, sends nothing after it and keeps the text: recorded frames, without the report, error frame first; whole, torn at every position, byte by byte");

    /* The report with error_code 0 for itself is no error: a job whose answers carry it (and the acknowledgement) succeeds. */
    {
        static const char OK_WITH_REPORT[]=REPORT "\003" OKFRAME "\003";
        static const char SCRIPT_OK[]="M140 S45\nM140 S0";
        /* bad_reply is used for lines naming cc2_nosuch only, so run lines that do: the answer is a report and a plain result */
        static const char SCRIPT_BAD[]="SET_HEATER_TEMPERATURE HEATER=cc2_nosuch TARGET=0\nM140 S0";
        (void)SCRIPT_OK;
        for(size_t read_size=0;read_size<=3;read_size++){
            job(SCRIPT_BAD,OK_WITH_REPORT,1,read_size,read_size);
            if(!(state.success&&state.completed&&!state.cancelled&&sent_count==2&&!strstr(state.output,"the printer reported an error"))){
                fprintf(stderr,"report with error_code 0 failed a job (read size %zu):\n%s\n",read_size,state.output);abort();
            }
        }
    }
    puts("PASS: the report with error_code 0 and the 803 text for itself fails nothing");

    console_destroy(&state);
    return 0;
}
