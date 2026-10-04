#include "../src/control.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void expect(int condition,const char *label){
    if(!condition){fprintf(stderr,"FAIL: %s\n",label);exit(1);}
    printf("PASS: %s\n",label);
}

int main(void){
    mqtt_client mqtt; char script[512],reason[256];
    memset(&mqtt,0,sizeof(mqtt));
    mqtt.have_machine_status=1; mqtt.machine_status=1;
    mqtt.connected=mqtt.registered=1; mqtt.last_message=time(NULL);
    expect(control_build_script("home:ALL",&mqtt,script,sizeof(script),reason,sizeof(reason))&&strcmp(script,"G28\nSET_HEATER_TEMPERATURE HEATER=extruder TARGET=0.0")==0,"home switches nozzle off when previous target is unavailable");
    mqtt.have_extruder_target=1; mqtt.extruder_target=205.0;
    expect(control_build_script("home:X",&mqtt,script,sizeof(script),reason,sizeof(reason))&&strcmp(script,"G28 X\nSET_HEATER_TEMPERATURE HEATER=extruder TARGET=205.0")==0,"home restores previous nozzle target");
    expect(control_build_script("screws:measure",&mqtt,script,sizeof(script),reason,sizeof(reason)),"screw measurement accepted while idle");
    expect(strstr(script,"M140 S60\nG180 S3\nM190 S60")!=NULL,"screw measurement heats bed and runs stock nozzle cleaning before probing");
    expect(strstr(script,"M190 S60")<strstr(script,"PROBE SAMPLES=3"),"screw measurement waits for 60 C bed before probing");
    expect(strstr(script,"M104 S0\nM140 S0")!=NULL,"screw measurement switches heaters off after probing");
    expect(strstr(script,"G1 X30 Y30 F12000\nPROBE SAMPLES=3\nG1 Z10 F600\nG1 X230 Y30 F12000\nPROBE SAMPLES=3\nG1 Z10 F600\nG1 X230 Y225 F12000\nPROBE SAMPLES=3\nG1 Z10 F600\nG1 X30 Y225 F12000\nPROBE SAMPLES=3\nG1 Z10 F600\nRESTORE_GCODE_STATE NAME=CC2_SCREW_MEASURE\nM104 S0\nM140 S0")!=NULL,"screw measurement uses corrected points and lifts Z before final heater shutdown");
    expect(strstr(script,"G28")==NULL,"screw measurement avoids final homing that clears measured values");
    expect(strstr(script,"TARGET=205.0")==NULL,"screw measurement does not restore stale pre-clean nozzle target");
    expect(control_build_script("system:heaters_off",&mqtt,script,sizeof(script),reason,sizeof(reason))&&strcmp(script,"TURN_OFF_HEATERS")==0,"heaters-off command");
    const char *off_aliases[]={"heaters:off","system:heaters_off"};
    for(size_t i=0;i<2;i++) {
        expect(control_build_script(off_aliases[i],&mqtt,script,sizeof(script),reason,sizeof(reason)),"heater shutdown accepted with fresh idle state");
        mqtt.last_message=time(NULL)-30;
        expect(!control_build_script(off_aliases[i],&mqtt,script,sizeof(script),reason,sizeof(reason)),"heater shutdown rejected with stale idle state");
        mqtt.last_message=time(NULL);mqtt.connected=0;
        expect(!control_build_script(off_aliases[i],&mqtt,script,sizeof(script),reason,sizeof(reason)),"heater shutdown rejected when disconnected");
        mqtt.connected=1;mqtt.registered=0;
        expect(!control_build_script(off_aliases[i],&mqtt,script,sizeof(script),reason,sizeof(reason)),"heater shutdown rejected when unregistered");
        mqtt.registered=1;mqtt.have_machine_status=0;
        expect(!control_build_script(off_aliases[i],&mqtt,script,sizeof(script),reason,sizeof(reason)),"heater shutdown rejected with unknown state");
        mqtt.have_machine_status=1;mqtt.machine_status=3;
        expect(!control_build_script(off_aliases[i],&mqtt,script,sizeof(script),reason,sizeof(reason)),"heater shutdown rejected while paused");
        expect(control_build_script("system:emergency_stop",&mqtt,script,sizeof(script),reason,sizeof(reason)),"emergency stop remains available while paused");
        mqtt.machine_status=1;
    }
    expect(control_build_script("system:fans_off",&mqtt,script,sizeof(script),reason,sizeof(reason))&&strstr(script,"M106 P2 S0"),"fans-off command");
    expect(control_build_script("system:emergency_stop",&mqtt,script,sizeof(script),reason,sizeof(reason))&&strcmp(script,"M112")==0,"emergency-stop command");
    mqtt.machine_status=2;
    expect(control_build_script("object:exclude:pièce (2)",&mqtt,script,sizeof(script),reason,sizeof(reason))&&strcmp(script,"EXCLUDE_OBJECT NAME=\"pièce (2)\"")==0,"accented object name and parentheses are quoted safely");
    expect(control_build_script("object:exclude:测试 (1)",&mqtt,script,sizeof(script),reason,sizeof(reason)),"Chinese object name accepted");
    expect(!control_build_script("object:exclude:x\"\nM112",&mqtt,script,sizeof(script),reason,sizeof(reason)),"object command injection rejected");
    expect(!control_build_script("object:exclude:bad\xc0\xaf",&mqtt,script,sizeof(script),reason,sizeof(reason)),"overlong UTF-8 object name rejected");

    expect(!control_build_script("system:fans_off",&mqtt,script,sizeof(script),reason,sizeof(reason)),"fans-off blocked while printing");
    expect(!control_build_script("system:heaters_off",&mqtt,script,sizeof(script),reason,sizeof(reason)),"heaters-off blocked while printing");
    expect(!control_build_script("heaters:off",&mqtt,script,sizeof(script),reason,sizeof(reason)),"heaters-off alias blocked while printing");
    expect(control_build_script("system:emergency_stop",&mqtt,script,sizeof(script),reason,sizeof(reason)),"emergency stop remains available while printing");
    mqtt.connected=mqtt.registered=1;mqtt.last_message=time(NULL);
    strcpy(mqtt.filename,"cube.gcode");strcpy(mqtt.print_state,"printing");
    expect(control_build_script("tune:speed:80",&mqtt,script,sizeof(script),reason,sizeof(reason))&&strcmp(script,"M220 S80")==0,"speed override while printing");
    expect(control_build_script("tune:flow:95",&mqtt,script,sizeof(script),reason,sizeof(reason))&&strcmp(script,"M221 S95")==0,"flow override while printing");
    const char *valid[]={"tune:speed:25","tune:speed:200","tune:speed:100","tune:flow:50","tune:flow:150","tune:flow:100"};
    for(size_t i=0;i<sizeof(valid)/sizeof(valid[0]);i++)expect(control_build_script(valid[i],&mqtt,script,sizeof(script),reason,sizeof(reason)),valid[i]);
    const char *invalid[]={"tune:speed:24","tune:speed:201","tune:flow:49","tune:flow:151","tune:speed:nan","tune:flow:inf","tune:speed:80.5","tune:speed:80junk","tune:flow:95:M112","tune:flow:95\nM112","tune:speed:","tune:other:100","tune:speed:-80","tune:speed:999999999999999999999999999999999999999999999999999"};
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)expect(!control_build_script(invalid[i],&mqtt,script,sizeof(script),reason,sizeof(reason)),invalid[i]);
    strcpy(mqtt.print_state,"paused");expect(control_build_script("tune:speed:100",&mqtt,script,sizeof(script),reason,sizeof(reason)),"tuning allowed in pause");
    mqtt.machine_status=1;expect(!control_build_script("tune:speed:100",&mqtt,script,sizeof(script),reason,sizeof(reason)),"tuning blocked idle");mqtt.machine_status=2;
    mqtt.last_message=time(NULL)-30;expect(!control_build_script("tune:speed:100",&mqtt,script,sizeof(script),reason,sizeof(reason)),"tuning blocked stale");mqtt.last_message=time(NULL);
    mqtt.connected=0;expect(!control_build_script("tune:flow:100",&mqtt,script,sizeof(script),reason,sizeof(reason)),"tuning blocked disconnected");mqtt.connected=1;
    mqtt.registered=0;expect(!control_build_script("tune:flow:100",&mqtt,script,sizeof(script),reason,sizeof(reason)),"tuning blocked unregistered");mqtt.registered=1;
    mqtt.filename[0]=0;expect(!control_build_script("tune:speed:100",&mqtt,script,sizeof(script),reason,sizeof(reason)),"tuning blocked without active file");
    return 0;
}
