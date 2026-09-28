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
    expect(control_build_script("screws:measure",&mqtt,script,sizeof(script),reason,sizeof(reason))&&strstr(script,"SET_HEATER_TEMPERATURE HEATER=extruder TARGET=205.0"),"screw measurement restores previous nozzle target");
    expect(control_build_script("system:heaters_off",&mqtt,script,sizeof(script),reason,sizeof(reason))&&strcmp(script,"TURN_OFF_HEATERS")==0,"heaters-off command");
    expect(control_build_script("system:fans_off",&mqtt,script,sizeof(script),reason,sizeof(reason))&&strstr(script,"M106 P2 S0"),"fans-off command");
    expect(control_build_script("system:emergency_stop",&mqtt,script,sizeof(script),reason,sizeof(reason))&&strcmp(script,"M112")==0,"emergency-stop command");
    mqtt.machine_status=2;
    expect(!control_build_script("system:fans_off",&mqtt,script,sizeof(script),reason,sizeof(reason)),"fans-off blocked while printing");
    expect(control_build_script("system:heaters_off",&mqtt,script,sizeof(script),reason,sizeof(reason)),"heaters-off remains available while printing");
    expect(control_build_script("system:emergency_stop",&mqtt,script,sizeof(script),reason,sizeof(reason)),"emergency stop remains available while printing");
    return 0;
}
