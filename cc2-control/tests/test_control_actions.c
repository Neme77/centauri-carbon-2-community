#include "../src/control.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect(int condition,const char *label){
    if(!condition){fprintf(stderr,"FAIL: %s\n",label);exit(1);}
    printf("PASS: %s\n",label);
}

int main(void){
    mqtt_client mqtt; char script[512],reason[256];
    memset(&mqtt,0,sizeof(mqtt));
    mqtt.have_machine_status=1; mqtt.machine_status=1;
    expect(control_build_script("home:ALL",&mqtt,script,sizeof(script),reason,sizeof(reason))&&strcmp(script,"G28\nSET_HEATER_TEMPERATURE HEATER=extruder TARGET=0.0")==0,"home switches nozzle off when previous target is unavailable");
    mqtt.have_extruder_target=1; mqtt.extruder_target=205.0;
    expect(control_build_script("home:X",&mqtt,script,sizeof(script),reason,sizeof(reason))&&strcmp(script,"G28 X\nSET_HEATER_TEMPERATURE HEATER=extruder TARGET=205.0")==0,"home restores previous nozzle target");
    expect(control_build_script("screws:measure",&mqtt,script,sizeof(script),reason,sizeof(reason)),"screw measurement accepted while idle");
    expect(strstr(script,"M140 S60\nG180 S3\nM190 S60")!=NULL,"screw measurement heats bed and runs stock nozzle cleaning before probing");
    expect(strstr(script,"M190 S60")<strstr(script,"PROBE SAMPLES=3"),"screw measurement waits for 60 C bed before probing");
    expect(strstr(script,"M104 S0\nM140 S0")!=NULL,"screw measurement switches heaters off after probing");
    expect(strstr(script,"TARGET=205.0")==NULL,"screw measurement does not restore stale pre-clean nozzle target");
    expect(control_build_script("system:heaters_off",&mqtt,script,sizeof(script),reason,sizeof(reason))&&strcmp(script,"TURN_OFF_HEATERS")==0,"heaters-off command");
    expect(control_build_script("system:fans_off",&mqtt,script,sizeof(script),reason,sizeof(reason))&&strstr(script,"M106 P2 S0"),"fans-off command");
    expect(control_build_script("system:emergency_stop",&mqtt,script,sizeof(script),reason,sizeof(reason))&&strcmp(script,"M112")==0,"emergency-stop command");
    mqtt.machine_status=2;
    expect(!control_build_script("system:fans_off",&mqtt,script,sizeof(script),reason,sizeof(reason)),"fans-off blocked while printing");
    expect(control_build_script("system:heaters_off",&mqtt,script,sizeof(script),reason,sizeof(reason)),"heaters-off remains available while printing");
    expect(control_build_script("system:emergency_stop",&mqtt,script,sizeof(script),reason,sizeof(reason)),"emergency stop remains available while printing");
    return 0;
}
