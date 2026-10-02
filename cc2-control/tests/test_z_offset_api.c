#define main cc2_main_original
#define console_start mock_console_start
#include "../src/main.c"
#undef main
#undef console_start
#include <assert.h>
static char sent[700];
int mock_console_start(console_state *s,const char *command,char *reason,size_t cap){
    (void)s;(void)reason;(void)cap;snprintf(sent,sizeof(sent),"%s",command);return 0;
}
static int command(mqtt_client *mqtt,const char *action){
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));sent[0]=0;
    control_response(pair[0],NULL,mqtt,action,strlen(action));
    char response[700];ssize_t n=recv(pair[1],response,sizeof(response)-1,0);assert(n>0);response[n]=0;
    close(pair[0]);close(pair[1]);return atoi(strchr(response,' ')+1);
}
static void offset(double value){
    telemetry.values[U_Z_OFFSET]=value;telemetry.present|=UINT32_C(1)<<U_Z_OFFSET;
    telemetry.ready=1;telemetry.fd=0;clock_gettime(CLOCK_MONOTONIC,&telemetry.last_rx);
}
int main(void){
    mqtt_client mqtt={0};mqtt.have_machine_status=1;mqtt.machine_status=1;
    mqtt.have_position=1;strcpy(mqtt.homed_axes,"xyz");uds_init(&telemetry);
    assert(command(&mqtt,"zoffset:adjust:0.05")==409&&!sent[0]);
    offset(0);assert(command(&mqtt,"zoffset:adjust:0.05")==202);
    assert(strstr(sent,"Z_ADJUST=+0.050"));
    assert(command(&mqtt,"zoffset:adjust:0.05")==409); /* another client cannot outrun readback */
    for(int i=1;i<10;i++){
        offset(i*0.05);assert(command(&mqtt,"zoffset:adjust:0.05")==202);
    }
    offset(0.5);assert(command(&mqtt,"zoffset:adjust:0.01")==409); /* reload does not reset limit */
    assert(command(&mqtt,"zoffset:undo:-0.4")==409); /* undo is not a large arbitrary step */
    assert(command(&mqtt,"zoffset:undo:-0.5")==202);
    offset(0);assert(command(&mqtt,"zoffset:adjust:-0.05")==202);
    offset(-0.5);z_offset_pending=0;z_offset_expected=-0.5;assert(command(&mqtt,"zoffset:adjust:-0.01")==409);
    assert(command(&mqtt,"zoffset:adjust:nan")==409);
    assert(command(&mqtt,"zoffset:adjust:0.01junk")==409);
    telemetry.last_rx.tv_sec-=10;assert(command(&mqtt,"zoffset:adjust:0.01")==409);
    z_offset_session=0;z_offset_pending=0;offset(-0.06);
    assert(command(&mqtt,"zoffset:adjust:-0.02")==202);
    assert(fabs(z_offset_reference+0.06)<0.000001);
    offset(-0.08);assert(command(&mqtt,"zoffset:undo:0.08")==409);
    assert(command(&mqtt,"zoffset:undo:0.02")==202);
    offset(-0.06);double actual;assert(z_offset_readback(&actual)&&!z_offset_pending);
    offset(0.12);assert(z_offset_readback(&actual)&&!z_offset_session);
    assert(command(&mqtt,"zoffset:adjust:0.05")==202);
    assert(fabs(z_offset_reference-0.12)<0.000001);
    offset(0.17);assert(command(&mqtt,"zoffset:undo:-0.05")==202);
    offset(0.12);assert(z_offset_readback(&actual)&&!z_offset_pending);
    mqtt.machine_status=2;strcpy(mqtt.print_state,"printing");
    assert(command(&mqtt,"zoffset:adjust:0.01")==202);
    assert(strstr(sent,"MOVE=1 MOVE_SPEED=5"));
    offset(0.13);assert(z_offset_readback(&actual)&&!z_offset_pending);
    strcpy(mqtt.homed_axes,"");assert(command(&mqtt,"zoffset:adjust:0.01")==409);
    strcpy(mqtt.homed_axes,"xyz");
    uds_client parsed;uds_init(&parsed);parsed.fd=0;
    const char *snapshot="{\"id\":11,\"result\":{\"eventtime\":1,\"status\":{\"gcode_move\":{\"homing_origin\":[0,0,0.23,0]}}}}";
    assert(uds_message(&parsed,snapshot,strlen(snapshot)));double value;
    assert(uds_value(&parsed,U_Z_OFFSET,&value)&&fabs(value-0.23)<0.000001);
    const char *invalid="{\"method\":\"cc2_status\",\"params\":{\"eventtime\":2,\"status\":{\"gcode_move\":{\"homing_origin\":null}}}}";
    assert(uds_message(&parsed,invalid,strlen(invalid)));assert(!uds_value(&parsed,U_Z_OFFSET,&value));
    const char *bad_fourth="{\"method\":\"cc2_status\",\"params\":{\"eventtime\":3,\"status\":{\"gcode_move\":{\"homing_origin\":[0,0,0.2,\"bad\"]}}}}";
    assert(uds_message(&parsed,bad_fourth,strlen(bad_fourth)));assert(!uds_value(&parsed,U_Z_OFFSET,&value));
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    z_offset_pending=0;uds_init(&telemetry);printer_response(pair[0],&mqtt);
    char response[5000];ssize_t n=recv(pair[1],response,sizeof(response)-1,0);assert(n>0);response[n]=0;
    assert(strstr(response,"\"z_offset\":{\"value\":null,\"pending\":false,\"reference\":null,\"adjustment\":null}"));
    close(pair[0]);close(pair[1]);
    puts("PASS: Z offset bounds across clients/reloads, pending readback, strict undo, invalid/stale values");
}
