#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
/* PR review: a printer error during a staged screw measurement must not count as a measurement. The real staged
 * console runs against a faked printer, and its result goes through control_screws_tick() as the main loop does it:
 * a failed session job ends the session (console error, heaters off), a failed one-shot measurement is not recorded
 * and leaves the deferred heater shutdown pending, nothing is reported as finished, and the error text stays in the
 * console output. A cancel and a lost connection are their own cases. */
static char sent_lines[48][160];
static int sent_count, reply_pending;
static const char *cancel_at, *fail_at, *drop_at;
static atomic_int *cancel_flag;
static int fake_socket(int domain,int type,int protocol){(void)domain;(void)type;(void)protocol;return 42;}
static int fake_connect(int fd,const struct sockaddr *address,socklen_t length){(void)fd;(void)address;(void)length;return 0;}
static ssize_t fake_send(int fd,const void *data,size_t length,int flags){
    (void)fd;(void)flags;
    const char *text=data,*script=strstr(text,"\"script\":\"");
    if(script&&sent_count<48){
        script+=10;
        size_t n=strcspn(script,"\"");
        if(n>=sizeof(sent_lines[0]))n=sizeof(sent_lines[0])-1;
        memcpy(sent_lines[sent_count],script,n);sent_lines[sent_count][n]=0;
        reply_pending=!(drop_at&&strstr(sent_lines[sent_count],drop_at));
        if(cancel_at&&strstr(sent_lines[sent_count],cancel_at))atomic_store(cancel_flag,1);
        sent_count++;
    }
    return (ssize_t)length;
}
static int fake_poll(struct pollfd *fds,nfds_t count,int timeout){(void)fds;(void)count;(void)timeout;return 1;}
enum { REPLY_OK, REPLY_ERROR, REPLY_REPORT, REPLY_SPLIT };
static int fail_mode; /* what the line named by fail_at is answered with */
static const char SPLIT_REPORT[]="{\"report\":{\"error_code\":704,\"error_level\":1,\"message\":\"probe failed\"}}\003";
static size_t split_pos; /* REPLY_SPLIT: the result and the first split_pos bytes of the report; the rest comes in the next read */
static const char *later;
static ssize_t fake_read(int fd,void *data,size_t length){
    (void)fd;
    if(later){size_t m=strlen(later);assert(m<=length);memcpy(data,later,m);later=NULL;return (ssize_t)m;}
    if(!reply_pending)return 0;
    reply_pending=0;
    const char *reply="{\"report\":\"// probe at 27.900,25.950 is z=1\"}\003{\"id\":101,\"result\":{}}\003";
    if(fail_at&&sent_count>0&&strstr(sent_lines[sent_count-1],fail_at)){
        if(fail_mode==REPLY_ERROR)reply="{\"error\":{\"message\":\"probe failed\"},\"id\":101}\003";
        if(fail_mode==REPLY_REPORT)reply="{\"report\":{\"error_code\":704,\"error_level\":1,\"message\":\"probe failed\"}}\003{\"id\":101,\"result\":{}}\003";
        if(fail_mode==REPLY_SPLIT){
            static char part[512];
            static const char ack[]="{\"id\":101,\"result\":{}}\003";
            memcpy(part,ack,sizeof(ack)-1);memcpy(part+sizeof(ack)-1,SPLIT_REPORT,split_pos);part[sizeof(ack)-1+split_pos]=0;
            if(split_pos<strlen(SPLIT_REPORT))later=SPLIT_REPORT+split_pos;
            reply=part;
        }
    }
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

#include "../src/control.h"
static int failures;
static void expect(int ok,const char *what){printf("%s: %s\n",ok?"PASS":"FAIL",what);if(!ok)failures++;}
static console_state state;
static mqtt_client m;
static unsigned long generation;
/* What console_start does before it starts the worker thread; the worker runs here in the test's own thread. */
static void run_job(const char *script,const char *tail){
    sent_count=0;reply_pending=0;cancel_flag=&state.cancel_requested;
    if(tail)console_arm_staged(&state,tail,control_axes_homed(&m));
    pthread_mutex_lock(&state.lock);
    snprintf(state.command,sizeof(state.command),"%s",script);
    state.output[0]=0;state.output_len=0;state.busy=1;state.completed=0;state.success=0;state.generation++;
    generation=state.generation;
    state.staged=state.staged_next;state.staged_next=0;state.cancelled=0;state.staged_homed=state.staged_homed_next;state.staged_homed_next=0;
    snprintf(state.cancel_tail,sizeof(state.cancel_tail),"%s",state.cancel_tail_next);
    atomic_store(&state.cancel_requested,0);
    pthread_mutex_unlock(&state.lock);
    console_worker(&state);
}
/* One main-loop tick on what the console reports now. */
static int tick(time_t now){
    int completed,success;
    pthread_mutex_lock(&state.lock);completed=state.completed;success=state.success;pthread_mutex_unlock(&state.lock);
    return control_screws_tick(&m,completed,success,generation,0,now);
}
static void printer_idle(time_t now){
    memset(&m,0,sizeof(m));m.connected=m.registered=1;m.have_machine_status=1;m.machine_status=1;
    m.last_message=now;m.have_position=1;strcpy(m.homed_axes,"xyz");
}
static int sent_has(const char *what){for(int i=0;i<sent_count;i++)if(strstr(sent_lines[i],what))return 1;return 0;}
/* The job stopped at the line the printer failed: nothing after it was sent, neither the rest of the script nor a tail. */
static int stopped_at_probe(void){return sent_count>0&&strstr(sent_lines[sent_count-1],"PROBE")&&!sent_has("RESTORE_GCODE_STATE")&&!sent_has("M104 S0");}

int main(void){
    char script[4096],reason[256];
    time_t t0=time(NULL);
    console_init(&state,"/test");

    /* ---- session: the start completes, a later measurement meets a printer error ---- */
    static const int modes[2]={REPLY_ERROR,REPLY_REPORT};
    static const char *const names[2]={"an error response","an error report"};
    for(int k=0;k<2;k++){
        char what[160];
        printer_idle(t0);control_screws_heaters_off_sent();control_screws_reset();(void)control_screws_take_finished(NULL);
        expect(control_build_script("screws:tram:start",&m,script,sizeof(script),reason,sizeof(reason)),"session start builds");
        control_screws_note_action("screws:tram:start");
        { const char *tail=control_screws_cancel_tail("screws:tram:start");run_job(script,tail);control_screws_note_job(generation); }
        expect(state.success&&tick(t0)==0&&control_screws_session_active(),"the start completes and opens the session");
        control_screws_job job;
        expect(control_screws_take_finished(&job)&&job.kind==CONTROL_SCREWS_JOB_START,"the completed start is reported once");
        control_screws_heaters_off_sent();

        expect(control_build_script("screws:tram:probe:FR",&m,script,sizeof(script),reason,sizeof(reason)),"a measurement of the session builds");
        control_screws_note_action("screws:tram:probe:FR");
        fail_at="PROBE";fail_mode=modes[k];
        { const char *tail=control_screws_cancel_tail("screws:tram:probe:FR");run_job(script,tail);control_screws_note_job(generation); }
        fail_at=NULL;
        snprintf(what,sizeof(what),"%s in a session measurement: the console job failed, not cancelled, error text kept",names[k]);
        expect(!state.success&&state.completed&&!state.cancelled&&strstr(state.output,"probe failed")&&
               strstr(state.output,"the printer reported an error"),what);
        expect(stopped_at_probe(),"no cleanup motion and no tail is sent after the printer error");
        snprintf(what,sizeof(what),"%s: the session ends as a console error with the heaters switched off",names[k]);
        expect(tick(t0+5)&&!control_screws_session_active()&&control_screws_end_reason()==CONTROL_SCREWS_END_CONSOLE&&
               control_screws_heaters_off_pending(),what);
        expect(!control_screws_take_finished(&job),"the failed measurement is not reported as finished");
        expect(!control_screws_measuring(),"no measurement stays pending");
    }

    /* ---- the start itself fails ---- */
    printer_idle(t0);control_screws_heaters_off_sent();control_screws_reset();(void)control_screws_take_finished(NULL);
    expect(control_build_script("screws:tram:start",&m,script,sizeof(script),reason,sizeof(reason)),"a second start builds");
    control_screws_note_action("screws:tram:start");
    fail_at="PROBE";fail_mode=REPLY_ERROR;
    { const char *tail=control_screws_cancel_tail("screws:tram:start");run_job(script,tail);control_screws_note_job(generation); }
    fail_at=NULL;
    expect(tick(t0+1)&&!control_screws_session_active()&&control_screws_end_reason()==CONTROL_SCREWS_END_CONSOLE&&
           control_screws_heaters_off_pending()&&!control_screws_take_finished(NULL),
           "a start that meets a printer error opens no session, records nothing and leaves the heaters to switch off");

    /* ---- one-shot measurement ---- */
    printer_idle(t0);control_screws_heaters_off_sent();control_screws_reset();(void)control_screws_take_finished(NULL);
    expect(control_build_script("screws:measure",&m,script,sizeof(script),reason,sizeof(reason)),"a one-shot measurement builds");
    control_screws_note_action("screws:measure");
    fail_at="PROBE";fail_mode=REPLY_ERROR;
    { const char *tail=control_screws_cancel_tail("screws:measure");run_job(script,tail);control_screws_note_quick(generation); }
    fail_at=NULL;
    expect(!state.success&&state.completed&&!state.cancelled&&strstr(state.output,"probe failed"),"a printer error fails the one-shot job and keeps the error text");
    (void)tick(t0+1);
    expect(!control_screws_take_finished(NULL)&&!control_screws_measuring(),"a one-shot measurement with a printer error is not recorded");
    expect(control_screws_heaters_off_pending(),"its heaters are left to the deferred shutdown (the closing M104 S0 / M140 S0 never ran)");
    expect(stopped_at_probe(),"no cleanup motion is sent after the printer error");

    /* The error report is torn across two reads behind the acknowledgement of the probe (also inside "error_code"): the
     * measurement fails just the same, is not recorded, leaves the heaters to the deferred shutdown and sends no tail. */
    static const size_t splits[]={5,15,25,40,sizeof(SPLIT_REPORT)-2};
    for(size_t k=0;k<sizeof(splits)/sizeof(splits[0]);k++){
        char what[160];
        printer_idle(t0);control_screws_heaters_off_sent();control_screws_reset();(void)control_screws_take_finished(NULL);
        expect(control_build_script("screws:measure",&m,script,sizeof(script),reason,sizeof(reason)),"a one-shot measurement builds for a torn report");
        control_screws_note_action("screws:measure");
        fail_at="PROBE";fail_mode=REPLY_SPLIT;split_pos=splits[k];later=NULL;
        { const char *tail=control_screws_cancel_tail("screws:measure");run_job(script,tail);control_screws_note_quick(generation); }
        fail_at=NULL;later=NULL;
        snprintf(what,sizeof(what),"a report torn after %zu bytes fails the one-shot job and keeps its text",splits[k]);
        expect(!state.success&&state.completed&&!state.cancelled&&strstr(state.output,"\"message\":\"probe failed\"")&&
               strstr(state.output,"the printer reported an error"),what);
        (void)tick(t0+1);
        expect(!control_screws_take_finished(NULL)&&!control_screws_measuring()&&control_screws_heaters_off_pending(),
               "a torn error report is not recorded and leaves the heaters to the deferred shutdown");
        /* The rest of the frame is waited for before the line behind the probe goes out: the probe is the last line sent. */
        expect(stopped_at_probe()&&sent_count==8,
               "after the torn report no further line (no move behind the failed probe) and no tail is sent");
    }
    control_screws_heaters_off_sent();

    /* a one-shot measurement that completes is recorded and needs no shutdown */
    printer_idle(t0);control_screws_heaters_off_sent();(void)control_screws_take_finished(NULL);
    expect(control_build_script("screws:measure",&m,script,sizeof(script),reason,sizeof(reason)),"a one-shot measurement builds again");
    control_screws_note_action("screws:measure");
    { const char *tail=control_screws_cancel_tail("screws:measure");run_job(script,tail);control_screws_note_quick(generation); }
    (void)tick(t0+2);
    expect(state.success&&control_screws_take_finished(NULL)&&!control_screws_heaters_off_pending(),
           "a completed one-shot measurement is still recorded, without a pending shutdown");

    /* ---- cancel: its own case (the cancel tail switches the heaters off itself) ---- */
    printer_idle(t0);control_screws_heaters_off_sent();(void)control_screws_take_finished(NULL);
    expect(control_build_script("screws:measure",&m,script,sizeof(script),reason,sizeof(reason)),"a one-shot measurement builds for the cancel");
    control_screws_note_action("screws:measure");
    cancel_at="PROBE";
    { const char *tail=control_screws_cancel_tail("screws:measure");run_job(script,tail);control_screws_note_quick(generation); }
    cancel_at=NULL;
    control_screws_note_cancel();
    (void)tick(t0+3);
    expect(!state.success&&state.cancelled&&strstr(state.output,"[CC2 Control: cancelled]")&&!strstr(state.output,"the printer reported an error"),
           "a cancelled one-shot measurement is cancelled, not a printer error");
    expect(sent_has("G1 Z10")&&sent_has("RESTORE_GCODE_STATE NAME=CC2_SCREW_MEASURE")&&sent_has("M140 S0"),"the cancel tail still runs");
    expect(!control_screws_take_finished(NULL)&&!control_screws_heaters_off_pending(),"a cancelled one-shot measurement is not recorded and needs no deferred shutdown");

    /* ---- cancel before the axes are homed: the lift of the tail is left out, the heaters and the restore are not ----
     * Measured on a printer on 20.11.: with unhomed axes the printer service accepted G1 Z10 F600 on the staged path (reported
     * z 0.187 to 10.0, no error), so the guard of the console did not hold there. */
    {
        static const char *const what[3]={"unhomed","position not known","homed"};
        static const char *const actions[2]={"screws:measure","screws:tram:start"};
        for(int a=0;a<2;a++)for(int k=0;k<3;k++){
            char label[200];
            printer_idle(t0);control_screws_heaters_off_sent();control_screws_reset();(void)control_screws_take_finished(NULL);
            if(k==0)m.homed_axes[0]=0;
            if(k==1)m.have_position=0;
            expect(control_build_script(actions[a],&m,script,sizeof(script),reason,sizeof(reason)),"a measurement builds for the cancel before homing");
            control_screws_note_action(actions[a]);
            cancel_at="M140 S60"; /* the line before the first homing line of the script */
            { const char *tail=control_screws_cancel_tail(actions[a]);run_job(script,tail);if(a==0)control_screws_note_quick(generation);else control_screws_note_job(generation); }
            cancel_at=NULL;
            control_screws_note_cancel();
            (void)tick(t0+6);
            snprintf(label,sizeof(label),"%s, %s: cancelled before the first homing line",actions[a],what[k]);
            expect(!state.success&&state.cancelled&&!sent_has("G28")&&!sent_has("G180"),label);
            snprintf(label,sizeof(label),"%s, %s: the lift %s sent",actions[a],what[k],k==2?"is":"is not");
            expect(sent_has("G1 Z10")==(k==2),label);
            snprintf(label,sizeof(label),"%s, %s: G90, the restore and both heaters go out",actions[a],what[k]);
            expect(sent_has("G90")&&sent_has("RESTORE_GCODE_STATE")&&sent_has("M104 S0")&&sent_has("M140 S0"),label);
        }
    }
    printer_idle(t0);control_screws_heaters_off_sent();control_screws_reset();(void)control_screws_take_finished(NULL);

    /* ---- lost connection: its own case ---- */
    printer_idle(t0);control_screws_heaters_off_sent();(void)control_screws_take_finished(NULL);
    expect(control_build_script("screws:measure",&m,script,sizeof(script),reason,sizeof(reason)),"a one-shot measurement builds for the lost connection");
    control_screws_note_action("screws:measure");
    drop_at="PROBE";
    { const char *tail=control_screws_cancel_tail("screws:measure");run_job(script,tail);control_screws_note_quick(generation); }
    drop_at=NULL;
    (void)tick(t0+4);
    expect(!state.success&&state.completed&&!state.cancelled&&strstr(state.output,"Console connection ended or command timed out")&&
           !strstr(state.output,"the printer reported an error"),"a lost connection has its own message and is not a printer error");
    expect(!control_screws_take_finished(NULL)&&control_screws_heaters_off_pending(),"a lost connection is not recorded and leaves the shutdown pending");
    control_screws_heaters_off_sent();

    printer_idle(t0);control_screws_reset();(void)control_screws_take_finished(NULL);
    expect(control_build_script("screws:tram:start",&m,script,sizeof(script),reason,sizeof(reason)),"a start builds for the lost connection");
    control_screws_note_action("screws:tram:start");
    drop_at="PROBE";
    { const char *tail=control_screws_cancel_tail("screws:tram:start");run_job(script,tail);control_screws_note_job(generation); }
    drop_at=NULL;
    expect(tick(t0+1)&&!control_screws_session_active()&&control_screws_end_reason()==CONTROL_SCREWS_END_CONSOLE&&
           control_screws_heaters_off_pending()&&!control_screws_take_finished(NULL),
           "a lost connection during a start ends the session like any failed job, nothing recorded");
    console_destroy(&state);
    return failures?1:0;
}
