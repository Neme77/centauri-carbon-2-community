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

    /* After homing or calibration the steppers stay energised and keep the
     * board fan running; they are released after ten stationary idle minutes. */
    const time_t limit=CONTROL_IDLE_MOTORS_SECONDS;
    control_idle_motors idle={0};
    control_idle_sample held={.valid=1,.machine_status=1,.print_state="",.board_fan=1.0,.x=52.5,.y=264.0,.z=50.0};
    control_idle_sample s=held;
    expect(!control_idle_motors_due(&idle,&s,1000),"idle release starts its wait");
    expect(!control_idle_motors_due(&idle,&s,1000+limit-1),"idle release waits the full period");
    expect(control_idle_motors_due(&idle,&s,1000+limit),"idle release is due after the full period");
    expect(!control_idle_motors_due(&idle,&s,1000+limit+1)&&!control_idle_motors_due(&idle,&s,1000+3*limit),"idle release is sent once per stationary period");
    s.x+=0.1;
    expect(!control_idle_motors_due(&idle,&s,1000+3*limit+1),"movement after an unconfirmed release restarts the wait");
    expect(control_idle_motors_due(&idle,&s,1000+4*limit+1),"idle release retries after another full period");

    idle=(control_idle_motors){0};s=held;
    expect(!control_idle_motors_due(&idle,&s,0),"idle release armed before movement");
    s.z-=0.2;
    expect(!control_idle_motors_due(&idle,&s,limit-1)&&!control_idle_motors_due(&idle,&s,2*limit-2),"movement restarts the idle wait");
    expect(control_idle_motors_due(&idle,&s,2*limit-1),"idle release counts from the last movement");

    idle=(control_idle_motors){0};s=held;
    expect(!control_idle_motors_due(&idle,&s,0),"idle release armed before steppers are released");
    s.board_fan=0.0;
    expect(!control_idle_motors_due(&idle,&s,limit),"no release once the board fan has stopped");
    s.board_fan=1.0;
    expect(!control_idle_motors_due(&idle,&s,limit+1)&&control_idle_motors_due(&idle,&s,2*limit+1),"re-enabled steppers start a new wait");

    const char *blockers[]={"stale telemetry","printing","paused print","calibrating","console command",
        "nozzle target","bed target","moving"};
    for(int i=0;i<(int)(sizeof(blockers)/sizeof(blockers[0]));i++){
        idle=(control_idle_motors){0};s=held;
        expect(!control_idle_motors_due(&idle,&s,0),"idle release armed before a blocker");
        if(i==0)s.valid=0;
        else if(i==1)s.machine_status=2;
        else if(i==2){s.machine_status=1;s.print_state="paused";}
        else if(i==3)s.machine_status=5;
        else if(i==4)s.console_busy=1;
        else if(i==5)s.nozzle_target=140.0;
        else if(i==6)s.bed_target=60.0;
        else s.velocity=5.0;
        expect(!control_idle_motors_due(&idle,&s,limit)&&!control_idle_motors_due(&idle,&s,3*limit),blockers[i]);
        s=held;
        expect(!control_idle_motors_due(&idle,&s,3*limit+1),"the wait restarts once the blocker clears");
        expect(control_idle_motors_due(&idle,&s,4*limit+1),"release follows a full period after the blocker");
    }
    return 0;
}
