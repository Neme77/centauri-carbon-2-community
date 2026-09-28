#include "control.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int reject(char *reason,size_t cap,const char *message){snprintf(reason,cap,"%s",message);return 0;}
static int valid_material_name(const char *name){
    size_t n=strlen(name);if(n<1||n>16)return 0;
    for(size_t i=0;i<n;i++)if(!isalnum((unsigned char)name[i])&&name[i]!='-'&&name[i]!='+'&&name[i]!='_')return 0;
    return 1;
}
static int valid_object_name(const char *name){
    size_t n=strlen(name);if(n<1||n>80)return 0;
    for(size_t i=0;i<n;i++){
        unsigned char ch=(unsigned char)name[i];
        if(!isalnum(ch)&&ch!='_'&&ch!='-'&&ch!='.'&&ch!=' ')return 0;
    }
    return 1;
}
/* print_status.enable is sticky on this firmware and may remain true after a
 * completed job. machine_status is the live authority: 1=Idle, 2=Printing. */
static int printing(const mqtt_client *m){return m->have_machine_status&&m->machine_status==2;}

/* The CC2 firmware heats the nozzle to 140 C while executing G28. Unlike the
 * stock display/app path, a raw G28 does not restore the previous heater
 * target afterwards. Always append an explicit restore so manual homing
 * cannot leave the hotend heating indefinitely. */
static double homing_extruder_restore_target(const mqtt_client *m){
    if(m->have_extruder_target&&m->extruder_target>=0.0&&m->extruder_target<=300.0)
        return m->extruder_target;
    return 0.0;
}

int control_build_script(const char *action,const mqtt_client *m,char *script,size_t cap,char *reason,size_t reason_cap){
    char kind[32],a[32],b[32],colour[16]; double value,value2;
    int slot,min_temp,max_temp;
    if(strncmp(action,"object:exclude:",15)==0){
        const char *name=action+15;
        if(!m->have_machine_status||m->machine_status!=2)
            return reject(reason,reason_cap,"Object exclusion requires an active print");
        if(!valid_object_name(name))
            return reject(reason,reason_cap,"Invalid object name");
        int n=snprintf(script,cap,"EXCLUDE_OBJECT NAME=\"%s\"",name);
        if(n<0||(size_t)n>=cap)
            return reject(reason,reason_cap,"Object name exceeds command length");
        return 1;
    }
    /* Stage 2: guarded four-corner measurement, never called by print start. */
    if(strcmp(action,"screws:measure")==0){
        if(printing(m))return reject(reason,reason_cap,"Screw measurement is blocked while printing");
        if(!m->have_machine_status||m->machine_status!=1)
            return reject(reason,reason_cap,"Screw measurement requires the printer to be idle");
        snprintf(script,cap,
            "SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\n"
            "G28\nG90\nG1 Z10 F600\n"
            "G1 X35 Y30 F12000\nPROBE SAMPLES=3\nG1 Z10 F600\n"
            "G1 X225 Y30 F12000\nPROBE SAMPLES=3\nG1 Z10 F600\n"
            "G1 X225 Y225 F12000\nPROBE SAMPLES=3\nG1 Z10 F600\n"
            "G1 X35 Y225 F12000\nPROBE SAMPLES=3\nG1 Z10 F600\n"
            "RESTORE_GCODE_STATE NAME=CC2_SCREW_MEASURE\n"
            "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=%.1f",
            homing_extruder_restore_target(m));
        return 1;
    }
    if(sscanf(action,"zoffset:undo:%lf",&value)==1){
        if(!m->have_machine_status||(m->machine_status!=1&&m->machine_status!=2))
            return reject(reason,reason_cap,"Z offset requires an idle or printing printer");
        if(!m->have_position||strchr(m->homed_axes,'z')==NULL)
            return reject(reason,reason_cap,"Z axis must be homed before live offset adjustment");
        if(fabs(value)<0.0005||fabs(value)>0.5001)
            return reject(reason,reason_cap,"Session Z offset undo exceeds the protected range");
        snprintf(script,cap,"SET_GCODE_OFFSET Z_ADJUST=%+.3f MOVE=1",value);
        return 1;
    }
    if(sscanf(action,"zoffset:adjust:%lf",&value)==1){
        if(!m->have_machine_status||(m->machine_status!=1&&m->machine_status!=2))
            return reject(reason,reason_cap,"Z offset requires an idle or printing printer");
        if(!m->have_position||strchr(m->homed_axes,'z')==NULL)
            return reject(reason,reason_cap,"Z axis must be homed before live offset adjustment");
        if(fabs(value)<0.0005||fabs(value)>0.0501)
            return reject(reason,reason_cap,"Each Z offset adjustment must be 0.01 or 0.05 mm");
        snprintf(script,cap,"SET_GCODE_OFFSET Z_ADJUST=%+.3f MOVE=1",value);
        return 1;
    }
    if(sscanf(action,"%31[^:]:%31[^:]:%lf",kind,a,&value)==3&&strcmp(kind,"move")==0){
        if(printing(m))return reject(reason,reason_cap,"Manual movement is blocked while printing");
        if(!m->have_machine_status||m->machine_status!=1)return reject(reason,reason_cap,"Manual movement requires the printer to be idle");
        if(!m->have_position)return reject(reason,reason_cap,"Current position is not available");
        if(strlen(a)!=1||!strchr("XYZxyz",a[0]))return reject(reason,reason_cap,"Unknown axis");
        char axis=(a[0]>='a'&&a[0]<='z')?(char)(a[0]-32):a[0];
        if(!strchr(m->homed_axes,(char)(axis+32)))return reject(reason,reason_cap,"Axis must be homed first");
        double current=axis=='X'?m->x:(axis=='Y'?m->y:m->z),target=current+value;
        double max=axis=='Y'?266.0:256.0,min=axis=='Z'?-2.0:0.0;
        if(fabs(value)<0.001||fabs(value)>(axis=='Z'?10.0:50.0)||target<min||target>max)
            return reject(reason,reason_cap,"Movement exceeds the protected range");
        snprintf(script,cap,"SAVE_GCODE_STATE NAME=CC2_CONTROL_MOVE\nG91\nG0 %c%.3f F%d\nRESTORE_GCODE_STATE NAME=CC2_CONTROL_MOVE",axis,value,axis=='Z'?600:6000);
        return 1;
    }
    if(sscanf(action,"%31[^:]:%31s",kind,a)==2&&strcmp(kind,"home")==0){
        if(printing(m))return reject(reason,reason_cap,"Homing is blocked while printing");
        if(!m->have_machine_status||m->machine_status!=1)return reject(reason,reason_cap,"Homing requires the printer to be idle");
        if(strcmp(a,"ALL")==0)snprintf(script,cap,"G28\nSET_HEATER_TEMPERATURE HEATER=extruder TARGET=%.1f",homing_extruder_restore_target(m));
        else if(strlen(a)==1&&strchr("XYZ",a[0]))snprintf(script,cap,"G28 %c\nSET_HEATER_TEMPERATURE HEATER=extruder TARGET=%.1f",a[0],homing_extruder_restore_target(m));
        else return reject(reason,reason_cap,"Unknown homing selection");
        return 1;
    }
    if(sscanf(action,"%31[^:]:%31[^:]:%lf",kind,a,&value)==3&&strcmp(kind,"heater")==0){
        if(strcmp(a,"extruder")==0){if(value<0||value>300)return reject(reason,reason_cap,"Nozzle target must be 0..300 C");}
        else if(strcmp(a,"heater_bed")==0){if(value<0||value>120)return reject(reason,reason_cap,"Bed target must be 0..120 C");}
        else return reject(reason,reason_cap,"Unknown heater");
        snprintf(script,cap,"SET_HEATER_TEMPERATURE HEATER=%s TARGET=%.1f",a,value);return 1;
    }
    if(strcmp(action,"heaters:off")==0){snprintf(script,cap,"TURN_OFF_HEATERS");return 1;}
    if(sscanf(action,"fan:%31[^:]:%lf",a,&value)==2){
        if(value<0||value>100)return reject(reason,reason_cap,"Fan value must be 0..100 percent");
        int pwm=(int)lround(value*2.55);
        if(strcmp(a,"part")==0)snprintf(script,cap,"M106 S%d",pwm);
        else if(strcmp(a,"aux")==0)snprintf(script,cap,"M106 P2 S%d",pwm);
        else if(strcmp(a,"box")==0)snprintf(script,cap,"SET_CAVITY_FAN SPEED=%d",pwm);
        else return reject(reason,reason_cap,"Unknown fan");
        return 1;
    }
    if(sscanf(action,"canvas:load:%d",&slot)==1){
        if(printing(m))return reject(reason,reason_cap,"Canvas loading is blocked while printing");
        if(!m->have_machine_status||m->machine_status!=1)return reject(reason,reason_cap,"Canvas loading requires the printer to be idle");
        if(slot<0||slot>3)return reject(reason,reason_cap,"Canvas slot must be 1..4");
        snprintf(script,cap,"CANVAS_LOAD_FILAMENT CHANNEL=%d",slot);return 1;
    }
    if(sscanf(action,"canvas:unload:%d",&slot)==1){
        if(printing(m))return reject(reason,reason_cap,"Canvas unloading is blocked while printing");
        if(!m->have_machine_status||m->machine_status!=1)return reject(reason,reason_cap,"Canvas unloading requires the printer to be idle");
        if(slot<0||slot>3)return reject(reason,reason_cap,"Canvas slot must be 1..4");
        snprintf(script,cap,"CANVAS_UNLOAD_FILAMENT CHANNEL=%d",slot);return 1;
    }
    if(sscanf(action,"canvas:material:%d:%31[^:]:%15[^:]:%d:%d",&slot,a,colour,&min_temp,&max_temp)==5){
        if(printing(m))return reject(reason,reason_cap,"Canvas material editing is blocked while printing");
        if(!m->have_machine_status||m->machine_status!=1)return reject(reason,reason_cap,"Canvas material editing requires the printer to be idle");
        if(slot<0||slot>3)return reject(reason,reason_cap,"Canvas slot must be 1..4");
        if(!valid_material_name(a))return reject(reason,reason_cap,"Material name may contain only letters, numbers, +, - and _");
        if(strlen(colour)!=6||strspn(colour,"0123456789abcdefABCDEF")!=6)return reject(reason,reason_cap,"Canvas colour must be six hexadecimal digits");
        if(min_temp<120||max_temp>320||min_temp>=max_temp)return reject(reason,reason_cap,"Invalid Canvas nozzle temperature range");
        snprintf(script,cap,"CANVAS_SET_FILAMENT_INFO CHANNEL=%d COLOR=0x%s detailed_type=\"%s\" MANUFACTURER=Generic nozzle_max_temp=%d nozzle_min_temp=%d TYPE=\"%s\" CODE=Generic",slot,colour,a,max_temp,min_temp,a);return 1;
    }
    if(sscanf(action,"preset:%31s",a)==1){
        if(printing(m))return reject(reason,reason_cap,"Preheat presets are blocked while printing");
        if(!m->have_machine_status||m->machine_status!=1)return reject(reason,reason_cap,"Preheat requires the printer to be idle");
        if(strcmp(a,"PLA")==0)snprintf(script,cap,"M104 S200\nM140 S60");
        else if(strcmp(a,"PETG")==0)snprintf(script,cap,"M104 S240\nM140 S70");
        else if(strcmp(a,"ABS")==0)snprintf(script,cap,"M104 S250\nM140 S100");
        else return reject(reason,reason_cap,"Unknown material preset");
        return 1;
    }
    if(sscanf(action,"preheat:%lf:%lf",&value,&value2)==2){
        if(printing(m))return reject(reason,reason_cap,"Preheat is blocked while printing");
        if(!m->have_machine_status||m->machine_status!=1)return reject(reason,reason_cap,"Preheat requires the printer to be idle");
        if(value<0||value>300||value2<0||value2>120)return reject(reason,reason_cap,"Preheat targets exceed the protected range");
        snprintf(script,cap,"M104 S%.0f\nM140 S%.0f",value,value2);return 1;
    }
    if(sscanf(action,"extrude:%lf",&value)==1){
        if(printing(m))return reject(reason,reason_cap,"Manual extrusion is blocked while printing");
        if(!m->have_machine_status||m->machine_status!=1)return reject(reason,reason_cap,"Manual extrusion requires the printer to be idle");
        if(!m->have_extruder_temp||m->extruder_temp<170.0)return reject(reason,reason_cap,"Nozzle must be at least 170 C");
        if(fabs(value)<0.1||fabs(value)>25.0)return reject(reason,reason_cap,"Extrusion must be between -25 and 25 mm");
        snprintf(script,cap,"SAVE_GCODE_STATE NAME=CC2_CONTROL_EXTRUDE\nM83\nG1 E%.2f F%d\nRESTORE_GCODE_STATE NAME=CC2_CONTROL_EXTRUDE",value,value<0?600:300);
        return 1;
    }
    if(strcmp(action,"system:motors_off")==0){
        if(printing(m))return reject(reason,reason_cap,"Motors cannot be disabled while printing");
        if(!m->have_machine_status||m->machine_status!=1)return reject(reason,reason_cap,"Motors off requires the printer to be idle");
        snprintf(script,cap,"M84");return 1;
    }
    if(strcmp(action,"system:all_off")==0){
        if(printing(m))return reject(reason,reason_cap,"All off is blocked while printing");
        if(!m->have_machine_status||m->machine_status!=1)return reject(reason,reason_cap,"All off requires the printer to be idle");
        snprintf(script,cap,"TURN_OFF_HEATERS\nM106 S0\nM106 P2 S0\nSET_CAVITY_FAN SPEED=0\nM84");return 1;
    }
    if(strcmp(action,"system:heaters_off")==0){
        snprintf(script,cap,"TURN_OFF_HEATERS");return 1;
    }
    if(strcmp(action,"system:fans_off")==0){
        if(printing(m))return reject(reason,reason_cap,"Fans off is blocked while printing");
        if(!m->have_machine_status||m->machine_status!=1)return reject(reason,reason_cap,"Fans off requires the printer to be idle");
        snprintf(script,cap,"M106 S0\nM106 P2 S0\nSET_CAVITY_FAN SPEED=0");return 1;
    }
    if(strcmp(action,"system:emergency_stop")==0){snprintf(script,cap,"M112");return 1;}
    if(sscanf(action,"light:%31s",a)==1){
        if(strcmp(a,"on")&&strcmp(a,"off"))return reject(reason,reason_cap,"Unknown light state");
        snprintf(script,cap,"SET_PIN PIN=led_pin VALUE=%d",strcmp(a,"on")==0);return 1;
    }
    if(sscanf(action,"tune:%31[^:]:%lf",a,&value)==2){
        if(strcmp(a,"speed")==0){if(value<25||value>200)return reject(reason,reason_cap,"Speed must be 25..200 percent");snprintf(script,cap,"M220 S%.0f",value);return 1;}
        if(strcmp(a,"flow")==0){if(value<50||value>150)return reject(reason,reason_cap,"Flow must be 50..150 percent");snprintf(script,cap,"M221 S%.0f",value);return 1;}
    }
    if(sscanf(action,"print:%31s",b)==1){
        if(strcmp(b,"pause")==0){if(!printing(m))return reject(reason,reason_cap,"No print is active");snprintf(script,cap,"PAUSE");return 1;}
        if(strcmp(b,"resume")==0){snprintf(script,cap,"RESUME");return 1;}
        if(strcmp(b,"cancel")==0){if(!printing(m))return reject(reason,reason_cap,"No print is active");snprintf(script,cap,"CANCEL_PRINT");return 1;}
    }
    return reject(reason,reason_cap,"Unknown or unsupported control action");
}
