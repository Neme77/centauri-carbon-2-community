#define main cc2_original_main
#include "../src/main.c"
#undef main
#include <assert.h>
static mqtt_client machine;
static console_state command_state;
static int detected, active=-1, connected=1, fail_read, fail_command, sent;
static int fake_rpc(const char *query,char **reply,size_t *length){
    *reply=NULL;*length=0;
    if(strstr(query,"gcode/script")){
        sent++;
        assert(strstr(query,"USED_CANVAS_DEV USED_CANVAS="));
        assert(!strstr(query,"M600")&&!strstr(query,"M104"));
        *reply=strdup(fail_command?"{\"error\":{\"message\":\"refused\"}}":"{\"result\":{}}");
    }else{
        if(fail_read)return -1;
        char value[1024];snprintf(value,sizeof(value),
            "{\"result\":{\"status\":{\"canvas_dev\":{\"active_cid\":%d,\"canvas_lite\":{\"connected\":%d,\"extern_channel\":{\"filament\":{\"type\":\"TPU\",\"nozzle_max_temp\":\"230\"}}}},\"print_stats\":{\"state\":\"standby\"},\"filament_switch_sensor filament_sensor\":{\"filament_detected\":%s}}}}",active,connected,detected?"true":"false");
        *reply=strdup(value);
    }
    assert(*reply);*length=strlen(*reply);return 0;
}
static void select_source(const char *source,int expected){
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    filament_source_response(pair[0],&command_state,&machine,source,strlen(source));
    close(pair[0]);char text[2048];ssize_t n=read(pair[1],text,sizeof(text)-1);assert(n>0);text[n]=0;close(pair[1]);
    char status[20];snprintf(status,sizeof(status),"HTTP/1.1 %d ",expected);assert(strstr(text,status));
}
int main(void){
    machine.connected=machine.registered=machine.have_machine_status=1;
    machine.machine_status=1;machine.last_message=time(NULL);
    char temporary[]="/tmp/cc2-external-feed-test-XXXXXX";assert(mkdtemp(temporary));
    char path[512];snprintf(path,sizeof(path),"%s/settings.json",temporary);external_feed_path=path;external_feed_load();
    console_init(&command_state,"/tmp/not-used");external_uds=fake_rpc;
    select_source("external",409);assert(!sent&&!external_source_selected);
    assert(external_feed_store(1,0));
    select_source("external\nM112",400);assert(!sent);
    detected=1;select_source("external",409);assert(!sent);detected=0;
    active=2;select_source("external",409);assert(!sent);active=-1;
    connected=0;select_source("external",409);assert(!sent);connected=1;
    machine.machine_status=2;select_source("external",409);assert(!sent);machine.machine_status=1;
    machine.last_message=time(NULL)-30;select_source("external",409);assert(!sent);machine.last_message=time(NULL);
    command_state.busy=1;select_source("external",409);assert(!sent);command_state.busy=0;
    fail_read=1;select_source("external",409);assert(!sent);fail_read=0;
    fail_command=1;select_source("external",503);assert(sent==1&&!external_source_selected);fail_command=0;
    select_source("external",200);assert(sent==2&&external_source_selected);
    /* Preference survives page reads and editing after the native job ends. */
    char *reply=NULL;size_t length=0;external_readback state;
    assert(external_read(&reply,&length,&state));assert(!state.disconnected&&state.native_idle&&state.have_sensor);
    assert(state.temperature==230);free(reply);
    char script[700],reason[256];machine.external_verified=2;machine.external_temperature=230;
    assert(control_build_script("external:load",&machine,script,sizeof(script),reason,sizeof(reason)));
    assert(!strcmp(script,"USED_CANVAS_DEV USED_CANVAS=0\nM600 S1 TEMP=230"));
    assert(control_build_script("external:unload",&machine,script,sizeof(script),reason,sizeof(reason)));
    assert(strstr(script,"M600 S2 TEMP=230"));
    machine.external_temperature=0;assert(!control_build_script("external:load",&machine,script,sizeof(script),reason,sizeof(reason)));
    detected=1;select_source("canvas",409);assert(external_source_selected);detected=0;
    select_source("canvas",200);assert(!external_source_selected);
    external_feed_load();assert(external_feed_enabled&&!external_source_selected);
    assert(!unlink(path));assert(!rmdir(temporary));
    console_destroy(&command_state);
    puts("PASS external source opt-in, empty-head/Idle/native guards, acknowledgement, mode-before-load and bounded temperature");return 0;
}
