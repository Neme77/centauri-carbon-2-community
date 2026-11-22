#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
/* R11: a staged console job is sent line by line and can be cancelled between two lines. The printer is faked:
 * every gcode/script request is answered with its result; one line can be answered with an error, and a cancel
 * can be asked for while a chosen line runs. */
static char sent_lines[32][160];
static int sent_count, reply_pending, reply_error, reply_mode;
static const char *cancel_at, *error_at, *report_at, *drop_at, *trailing_at, *extra_reply;
static const char *quiet_report="{\"report\":{\"error_code\":0,\"error_level\":0,\"message\":\"z=0.2\"}}\003";
static const char *resume_report="{\"report\":{\"error_code\":702,\"error_level\":3,\"message\":\"resumed\"}}\003";
static const char *report_extra;
/* A report that is torn across two reads: the line named by split_at is answered with its result followed by the first
 * split_pos bytes of split_report; the rest comes in the next read. */
static const char *split_at, *split_report;
static size_t split_pos;
/* What follows the cut report: 0 the rest comes in the next read; 1 the connection closes (EOF); 2 the connection goes
 * quiet (no more data, no EOF); 3 the wait runs out (the clock moves past the deadline); 4 a read error. */
static int split_wait;
static const char *junk,*junk_at,*cur_junk; /* junk goes with the line named by junk_at: the start of a frame that never ends, after what the fake printer sends */
static time_t clock_jump;
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
        reply_pending=!(drop_at&&strstr(sent_lines[sent_count],drop_at)); /* a lost connection: nothing comes back */
        reply_error=error_at&&strstr(sent_lines[sent_count],error_at)!=NULL;
        reply_mode=report_at&&strstr(sent_lines[sent_count],report_at)?1:(split_at&&strstr(sent_lines[sent_count],split_at)?2:0);
        extra_reply=trailing_at&&strstr(sent_lines[sent_count],trailing_at)?"{\"report\":{\"error_code\":704,\"error_level\":1,\"message\":\"late fault\"}}\003":report_extra;
        cur_junk=junk&&strstr(sent_lines[sent_count],junk_at)?junk:NULL;
        if(cancel_at&&strstr(sent_lines[sent_count],cancel_at))atomic_store(cancel_flag,1);
        sent_count++;
    }
    return (ssize_t)length;
}
static const char *later; /* a report that arrives in a read of its own, after the result */
static int fake_poll(struct pollfd *fds,nfds_t count,int timeout){(void)fds;(void)count;(void)timeout;return !(split_wait==2&&!reply_pending&&!later);}
static ssize_t fake_read(int fd,void *data,size_t length){
    (void)fd;
    if(later){size_t m=strlen(later),j=cur_junk?strlen(cur_junk):0;assert(m+j<=length);memcpy(data,later,m);if(j)memcpy((char*)data+m,cur_junk,j);later=NULL;cur_junk=NULL;return (ssize_t)(m+j);}
    if(!reply_pending){
        if(split_wait==4){errno=EIO;return -1;}
        return 0;
    }
    reply_pending=0;
    static char part[512];
    if(reply_mode==2){
        static const char ack[]="{\"id\":101,\"result\":{}}\003";
        size_t k=strlen(split_report);
        assert(split_pos<=k&&sizeof(ack)-1+split_pos<sizeof(part));
        memcpy(part,ack,sizeof(ack)-1);memcpy(part+sizeof(ack)-1,split_report,split_pos);part[sizeof(ack)-1+split_pos]=0;
        if(split_pos<k&&split_wait==0)later=split_report+split_pos;
        if(split_wait==3)clock_jump=1000;
    }
    const char *reply=reply_mode==2?part:reply_error?"{\"error\":{\"message\":\"probe failed\"},\"id\":101}\003":
        reply_mode?"{\"report\":{\"error_code\":704,\"error_level\":1,\"message\":\"Z home failed\"}}\003{\"id\":101,\"result\":{}}\003":
        "{\"report\":\"z=0.1\"}\003{\"id\":101,\"result\":{}}\003";
    if(!reply_error&&!reply_mode&&extra_reply)later=extra_reply;
    size_t n=strlen(reply),j=cur_junk&&!later?strlen(cur_junk):0;
    assert(n+j<=length);
    memcpy(data,reply,n);
    if(j){memcpy((char*)data+n,cur_junk,j);cur_junk=NULL;}
    return (ssize_t)(n+j);
}
static int fake_close(int fd){(void)fd;return 0;}
static time_t fake_time(time_t *t){return time(t)+clock_jump;}
#define socket fake_socket
#define connect fake_connect
#define send fake_send
#define poll fake_poll
#define read fake_read
#define close fake_close
#define time fake_time
#include "../src/console.c"
#undef time
#undef close
#undef read
#undef poll
#undef send

static const char SCRIPT[]="SAVE_GCODE_STATE NAME=T\nM140 S60\nG1 X1 Y1\nPROBE SAMPLES=3\nG1 X2 Y2\nPROBE SAMPLES=3\nRESTORE_GCODE_STATE NAME=T\nM140 S0";
static const char TAIL[]="G90\n" CONSOLE_TAIL_IF_HOMED "G1 Z10 F600\nRESTORE_GCODE_STATE NAME=T\nM104 S0\nM140 S0";
static unsigned homed_start=CONSOLE_AXES_ALL; /* the axes the printer reports as homed when a job is armed */
static console_state state;
/* What console_start does before it starts the worker thread; the worker runs here in the test's own thread. */
static void run(const char *script,const char *tail,const char *cancel,const char *error){
    sent_count=0;reply_pending=0;cancel_at=cancel;error_at=error;cancel_flag=&state.cancel_requested;
    if(tail)console_arm_staged(&state,tail,homed_start);
    pthread_mutex_lock(&state.lock);
    snprintf(state.command,sizeof(state.command),"%s",script);
    state.output[0]=0;state.output_len=0;state.busy=1;state.completed=0;state.success=0;state.generation++;
    state.staged=state.staged_next;state.staged_next=0;state.cancelled=0;state.staged_homed=state.staged_homed_next;state.staged_homed_next=0;
    snprintf(state.cancel_tail,sizeof(state.cancel_tail),"%s",state.cancel_tail_next);
    atomic_store(&state.cancel_requested,0);
    pthread_mutex_unlock(&state.lock);
    console_worker(&state);
}
/* As run(), with a fake printer that answers one line with an error report, loses the connection at one line, or
 * adds a report to every answer. */
static void report_job(const char *script,const char *tail,const char *report,const char *trailing,const char *drop,const char *extra){
    sent_count=0;reply_pending=0;cancel_at=NULL;error_at=NULL;cancel_flag=&state.cancel_requested;
    if(tail)console_arm_staged(&state,tail,homed_start);
    pthread_mutex_lock(&state.lock);
    snprintf(state.command,sizeof(state.command),"%s",script);
    state.output[0]=0;state.output_len=0;state.busy=1;state.completed=0;state.success=0;state.generation++;
    state.staged=state.staged_next;state.staged_next=0;state.cancelled=0;state.staged_homed=state.staged_homed_next;state.staged_homed_next=0;
    snprintf(state.cancel_tail,sizeof(state.cancel_tail),"%s",state.cancel_tail_next);
    atomic_store(&state.cancel_requested,0);
    pthread_mutex_unlock(&state.lock);
    report_at=report;trailing_at=trailing;drop_at=drop;report_extra=extra;
    console_worker(&state);
    report_at=trailing_at=drop_at=report_extra=NULL;
}
/* As run(), with a fake printer that answers the line named by `line` with its result and the first `pos` bytes of an
 * error report; the rest of the report comes in the next read. */
static const char SPLIT_REPORT[]="{\"report\":{\"error_code\":704,\"error_level\":1,\"message\":\"Z home failed\"}}\003";
static void split_job(const char *script,const char *tail,const char *line,size_t pos){
    sent_count=0;reply_pending=0;cancel_at=NULL;error_at=NULL;later=NULL;cancel_flag=&state.cancel_requested;
    if(tail)console_arm_staged(&state,tail,homed_start);
    pthread_mutex_lock(&state.lock);
    snprintf(state.command,sizeof(state.command),"%s",script);
    state.output[0]=0;state.output_len=0;state.busy=1;state.completed=0;state.success=0;state.generation++;
    state.staged=state.staged_next;state.staged_next=0;state.cancelled=0;state.staged_homed=state.staged_homed_next;state.staged_homed_next=0;
    snprintf(state.cancel_tail,sizeof(state.cancel_tail),"%s",state.cancel_tail_next);
    atomic_store(&state.cancel_requested,0);
    pthread_mutex_unlock(&state.lock);
    split_at=line;split_report=SPLIT_REPORT;split_pos=pos;
    console_worker(&state);
    split_at=split_report=NULL;split_pos=0;later=NULL;
}
/* As split_job, with the report cut at `pos` and the rest of the connection as `wait` says (see split_wait). */
static void cut_job(const char *script,const char *tail,const char *line,size_t pos,int wait){
    split_wait=wait;
    split_job(script,tail,line,pos);
    split_wait=0;clock_jump=0;
}
static int sent_line(const char *what){for(int i=0;i<sent_count;i++)if(!strcmp(sent_lines[i],what))return 1;return 0;}
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

    /* The lift of the cancel tail needs referenced axes. Measured on a printer on 20.11.: with unhomed axes the printer
     * service accepted G1 Z10 F600 on the staged path (reported z 0.187 to 10.0, no error). The heater lines, the state
     * restore and G90 always go out; the lift goes out when the axes were homed when the job was armed or its own G28 /
     * G180 S3 has completed before the cancel. */
    {
        static const char NOHOME[]="SAVE_GCODE_STATE NAME=T\nM140 S60\nM190 S60\nG28\nG180 S3\nG1 X1 Y1\nPROBE SAMPLES=3\nM140 S0";
        static const struct { const char *name; unsigned homed; const char *script; const char *cancel; int lift; } cases[]={
            {"homed at the start, cancelled at a probe",CONSOLE_AXES_ALL,NOHOME,"PROBE",1},
            {"unhomed at the start, cancelled while the bed cools (before G28)",0,NOHOME,"M190",0},
            {"cancelled before any line has completed (nothing homed yet)",0,NOHOME,"SAVE_GCODE_STATE",0},
            {"unhomed at the start, cancelled at the probe after the job's own G28",0,NOHOME,"PROBE",1},
            {"unhomed at the start, cancelled during the G28 line itself: the tail runs after it",0,NOHOME,"G28",1},
            {"only X and Y homed, cancelled before the job's G28",CONSOLE_AXES_X|CONSOLE_AXES_Y,NOHOME,"M190",0},
            {"X and Y homed, then the job's own G28 Z",CONSOLE_AXES_X|CONSOLE_AXES_Y,"SAVE_GCODE_STATE NAME=T\nG28 Z\nPROBE SAMPLES=3\nM140 S0","PROBE",1},
            {"X and Y homed, cancelled before G28 Z",CONSOLE_AXES_X|CONSOLE_AXES_Y,"SAVE_GCODE_STATE NAME=T\nM190 S60\nG28 Z\nPROBE SAMPLES=3\nM140 S0","M190",0},
            {"the nozzle cleaning homes first (G180 S3)",0,"SAVE_GCODE_STATE NAME=T\nM190 S60\nG180 S3\nPROBE SAMPLES=3\nM140 S0","PROBE",1},
        };
        for(size_t k=0;k<sizeof(cases)/sizeof(cases[0]);k++){
            homed_start=cases[k].homed;
            run(cases[k].script,TAIL,cases[k].cancel,NULL);
            homed_start=CONSOLE_AXES_ALL;
            if(!(state.cancelled&&!state.success)){fprintf(stderr,"case '%s': not cancelled\n",cases[k].name);abort();}
            if(sent_line("G1 Z10 F600")!=cases[k].lift){fprintf(stderr,"case '%s': lift sent=%d, expected %d\n",cases[k].name,sent_line("G1 Z10 F600"),cases[k].lift);abort();}
            /* whatever the axes: G90, the state restore and both heaters go out, and nothing of the tail is marked */
            assert(sent_line("G90")&&sent_line("RESTORE_GCODE_STATE NAME=T")&&sent_line("M104 S0")&&sent_line("M140 S0"));
            assert(!sent_line("?H G1 Z10 F600"));
        }
    }
    /* The job\'s own G28 is acknowledged but its failure is reported only in a read of its own, and the cancel comes
     * meanwhile: the report is read with the first line of the tail, and the lift is left out. */
    {
        static const char NOHOME[]="SAVE_GCODE_STATE NAME=T\nM140 S60\nG28\nG180 S3\nPROBE SAMPLES=3\nM140 S0";
        homed_start=0;trailing_at="G28";
        run(NOHOME,TAIL,"G28",NULL);
        trailing_at=NULL;homed_start=CONSOLE_AXES_ALL;
        assert(state.cancelled&&!state.success&&strstr(state.output,"late fault"));
        assert(!sent_line("G1 Z10 F600")&&sent_line("G90")&&sent_line("RESTORE_GCODE_STATE NAME=T")&&sent_line("M104 S0")&&sent_line("M140 S0"));
    }
    puts("PASS: the lift of the cancel tail is sent only with homed axes; G90, the restore and the heaters always");

    /* A printer error response is a failure of the job, not a completed measurement: success is 0, the job is
     * finished and not cancelled, the error text stays in the output, and no motion tail is sent after it. */
    run(SCRIPT,TAIL,NULL,"PROBE");
    assert(sent_count==4&&!strcmp(sent_lines[3],"PROBE SAMPLES=3")); /* the lines after the error and the tail stay unsent */
    assert(!state.success&&state.completed&&!state.cancelled&&!state.busy);
    assert(strstr(state.output,"probe failed"));
    assert(strstr(state.output,"[CC2 Control: the printer reported an error; the job was stopped]"));
    assert(!strstr(state.output,"cancelled")&&!strstr(state.output,"connection ended"));
    console_json(&state,json,sizeof(json));
    assert(strstr(json,"\"cancelled\":false"));
    puts("PASS: a printer error response fails the staged job, keeps the error text and sends no tail");

    /* The same for an error report whose request was acknowledged (a failed home or probe can be ACKed). */
    report_job(SCRIPT,TAIL,"PROBE",NULL,NULL,NULL);
    assert(sent_count==4&&!state.success&&state.completed&&!state.cancelled&&!state.busy);
    assert(strstr(state.output,"Z home failed")&&strstr(state.output,"the printer reported an error"));
    puts("PASS: an error report fails the staged job even when its request was acknowledged");

    /* An error report that trails the last result in a read of its own still fails the job. */
    report_job(SCRIPT,TAIL,NULL,"M140 S0",NULL,NULL);
    assert(sent_count==8&&!state.success&&state.completed&&!state.cancelled);
    assert(strstr(state.output,"late fault")&&strstr(state.output,"the printer reported an error"));
    puts("PASS: an error report after the last result fails the job");

    /* An acknowledgement followed by the beginning of an error report, in one read; the rest of the report in the next one.
     * The half frame must survive the end of the line (and of the last line, into the final read): at every split position,
     * also inside "report" and inside "error_code", the job fails with the error text whole. Between two lines the rest comes
     * while the next line is waited for, so that line is the last one sent. */
    const size_t split_len=strlen(SPLIT_REPORT);
    static const char SPLIT_TEXT[]="{\"report\":{\"error_code\":704,\"error_level\":1,\"message\":\"Z home failed\"}}";
    for(size_t pos=0;pos<split_len;pos++){
        /* Between two lines: the first probe is acknowledged and the report has begun, the rest of the frame is waited for
         * before the next line goes out: nothing after the probe is sent, neither the next line nor the tail. (Position 0
         * is a report that comes in a read of its own after the result: it is seen while the next line is waited for, which
         * is then the last one sent.) */
        split_job(SCRIPT,TAIL,"PROBE",pos);
        assert(sent_count==(pos?4:5)&&!sent_line("RESTORE_GCODE_STATE NAME=T")&&!sent_line("G90")&&!sent_line("M104 S0"));
        assert(pos||sent_line("G1 X2 Y2"));
        assert(!pos||!sent_line("G1 X2 Y2"));
        assert(!state.success&&state.completed&&!state.cancelled&&!state.busy);
        assert(strstr(state.output,SPLIT_TEXT)&&strstr(state.output,"the printer reported an error")&&!strstr(state.output,"connection ended"));
        /* After the last line: the rest comes in the final read. */
        split_job(SCRIPT,TAIL,"M140 S0",pos);
        assert(sent_count==8&&!sent_line("G90")&&!sent_line("M104 S0"));
        assert(!state.success&&state.completed&&!state.cancelled&&!state.busy);
        assert(strstr(state.output,SPLIT_TEXT)&&strstr(state.output,"the printer reported an error"));
    }
    puts("PASS: an error report torn across two reads is kept between two lines and into the final read, at every split position");

    /* Neme77 (21.11., dbc05899): an acknowledged line, then the start of an error report that never ends. The connection
     * closes (EOF), goes quiet, the wait runs out or a read fails: that is a transport failure, not a success and not a
     * printer error. The job fails with the received text kept, sends nothing after the line (no tail), and an EOF
     * or read error after a complete report stays as it was. Every cut position inside the report is tried, also
     * inside "error_code". */
    {
        static const int waits[]={1,2,3,4};
        static const char *wait_names[]={"EOF","quiet","wait ran out","read error"};
        for(size_t w=0;w<sizeof(waits)/sizeof(waits[0]);w++){
            for(size_t pos=1;pos<split_len;pos++){
                char prefix[sizeof(SPLIT_TEXT)];
                memcpy(prefix,SPLIT_TEXT,pos);prefix[pos]=0;
                cut_job(SCRIPT,TAIL,"PROBE",pos,waits[w]);
                if(sent_count!=4||sent_line("G1 X2 Y2")||sent_line("RESTORE_GCODE_STATE NAME=T")||sent_line("G90")||sent_line("M104 S0")){
                    fprintf(stderr,"%s at %zu, PROBE: sent %d lines, expected 4 and no tail\n",wait_names[w],pos,sent_count);abort();
                }
                if(state.success||!state.completed||state.cancelled||state.busy){fprintf(stderr,"%s at %zu: job not failed\n",wait_names[w],pos);abort();}
                if(!strstr(state.output,prefix)||!strstr(state.output,"Console connection ended or command timed out")||strstr(state.output,"the printer reported an error")){
                    fprintf(stderr,"%s at %zu: output lacks the received text or the transport message\n",wait_names[w],pos);abort();
                }
                cut_job(SCRIPT,TAIL,"M140 S0",pos,waits[w]);
                if(sent_count!=8||sent_line("G90")||state.success||!state.completed||state.cancelled||state.busy||
                   !strstr(state.output,prefix)||!strstr(state.output,"Console connection ended or command timed out")){
                    fprintf(stderr,"%s at %zu, last line: sent %d, success %d\n",wait_names[w],pos,sent_count,state.success);abort();
                }
            }
        }
    }
    puts("PASS: an acknowledged line with an unfinished report fails the job on EOF, quiet, an expired wait and a read error, with the received text kept");

    /* Neme77's review, the other way round: a printer error read whole stays a printer error when a later frame is cut off
     * and the connection ends; the cut text is kept. Without a printer error it stays a transport failure (above). */
    {
        static const char CUT[]="{\"report\":{\"error_code\":7";
        static const char *msg_printer="the printer reported an error",*msg_lost="Console connection ended or command timed out";
        /* (a) a whole error report with the acknowledgement, then a cut frame, then EOF; between two lines and after the last */
        junk_at="PROBE";junk=CUT;report_job(SCRIPT,TAIL,"PROBE",NULL,NULL,NULL);junk=NULL;
        assert(sent_count==4&&!state.success&&state.completed&&!state.cancelled&&!state.busy);
        assert(strstr(state.output,"Z home failed")&&strstr(state.output,CUT)&&strstr(state.output,msg_printer)&&!strstr(state.output,msg_lost));
        junk_at="M140 S0";junk=CUT;report_job(SCRIPT,TAIL,NULL,"M140 S0",NULL,NULL);junk=NULL;
        assert(sent_count==8&&!state.success&&state.completed&&!state.cancelled);
        assert(strstr(state.output,"late fault")&&strstr(state.output,CUT)&&strstr(state.output,msg_printer)&&!strstr(state.output,msg_lost));
        /* (b) an error response to a line, then a cut frame */
        junk_at="PROBE";junk=CUT;run(SCRIPT,TAIL,NULL,"PROBE");junk=NULL;
        assert(sent_count==4&&!state.success&&state.completed&&!state.cancelled);
        assert(strstr(state.output,"probe failed")&&strstr(state.output,CUT)&&strstr(state.output,msg_printer)&&!strstr(state.output,msg_lost));
        /* (c) no printer error: a cut frame after an acknowledgement is a transport failure */
        junk_at="PROBE";junk=CUT;report_job(SCRIPT,TAIL,NULL,NULL,NULL,NULL);junk=NULL;
        assert(!state.success&&state.completed&&strstr(state.output,msg_lost)&&!strstr(state.output,msg_printer));
    }
    puts("PASS: a printer error read whole stays a printer error when a later frame is cut off; the cut text is kept");

    /* Ordinary reports (error_code 0) and a resume event (level 3) are no failure. */
    report_job(SCRIPT,TAIL,NULL,NULL,NULL,quiet_report);
    assert(state.success&&state.completed&&!strstr(state.output,"the printer reported an error"));
    report_job(SCRIPT,TAIL,NULL,NULL,NULL,resume_report);
    assert(state.success&&state.completed&&!strstr(state.output,"the printer reported an error"));
    puts("PASS: reports with error_code 0 and a level 3 resume event do not fail the job");

    /* A lost connection (no answer) stays its own case: failed, its own message, not a printer error, no tail. */
    report_job(SCRIPT,TAIL,NULL,NULL,"PROBE",NULL);
    assert(sent_count==4&&!state.success&&state.completed&&!state.cancelled&&!state.busy);
    assert(strstr(state.output,"Console connection ended or command timed out"));
    assert(!strstr(state.output,"the printer reported an error"));
    puts("PASS: a lost connection fails the job with its own message and without a printer error");

    /* A cancel keeps its own case after an error elsewhere: the tail runs and the job counts as cancelled. */
    run(SCRIPT,TAIL,"PROBE",NULL);
    assert(state.cancelled&&!state.success&&strstr(state.output,"[CC2 Control: cancelled]")&&!strstr(state.output,"the printer reported an error"));
    puts("PASS: a cancel is neither a printer error nor a lost connection");

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
    console_arm_staged(&state,TAIL,CONSOLE_AXES_ALL);
    char reason[64];
    assert(console_start(&state,"M105",reason,sizeof(reason))==-1&&!state.staged_next);
    pthread_mutex_lock(&state.lock);state.staged=0;pthread_mutex_unlock(&state.lock);
    assert(console_cancel(&state)==-1);
    puts("PASS: cancel only for a running staged job; a refused start disarms the staging");
    pthread_mutex_lock(&state.lock);state.busy=0;pthread_mutex_unlock(&state.lock);
    console_destroy(&state);
    return 0;
}
