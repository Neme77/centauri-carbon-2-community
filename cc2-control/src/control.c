#include "control.h"
#include "console.h"
#include "thermal_limits.h"
#include "canvas_state.h"
#include "external_filament_catalog.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int reject(char *reason,size_t cap,const char *message){snprintf(reason,cap,"%s",message);return 0;}
static int valid_material_name(const char *name){
    size_t n=strlen(name);if(n<1||n>16||name[0]==' '||name[n-1]==' ')return 0;
    for(size_t i=0;i<n;i++)if(!isalnum((unsigned char)name[i])&&name[i]!=' '&&name[i]!='-'&&name[i]!='+'&&name[i]!='_')return 0;
    return 1;
}
/* A product line or maker for a Canvas tray: ASCII words, quoted in the script, so no quotes or colons. */
static int valid_filament_label(const char *s){
    size_t n=strlen(s);if(n<1||n>32||s[0]==' '||s[n-1]==' ')return 0;
    for(size_t i=0;i<n;i++)if(!isalnum((unsigned char)s[i])&&!strchr(" +-_.",s[i]))return 0;
    return 1;
}
static int valid_object_name(const char *name){
    size_t n=strlen(name);if(n<1||n>80)return 0;
    for(size_t i=0;i<n;){
        unsigned char ch=(unsigned char)name[i++];
        if(ch<128){
            if(!isalnum(ch)&&ch!='_'&&ch!='-'&&ch!='.'&&ch!=' '&&ch!='('&&ch!=')')return 0;
            continue;
        }
        unsigned int value;size_t extra;
        if(ch>=0xc2&&ch<=0xdf){value=ch&31;extra=1;}
        else if(ch>=0xe0&&ch<=0xef){value=ch&15;extra=2;}
        else if(ch>=0xf0&&ch<=0xf4){value=ch&7;extra=3;}
        else return 0;
        if(extra>n-i)return 0;
        for(size_t k=0;k<extra;k++){
            unsigned char next=(unsigned char)name[i++];
            if((next&0xc0)!=0x80)return 0;
            value=(value<<6)|(next&63);
        }
        if((extra==1&&value<0x80)||(extra==2&&value<0x800)||(extra==3&&value<0x10000)||
           value>0x10ffff||(value>=0xd800&&value<=0xdfff)||(value>=0x80&&value<=0x9f)||
           value==0x2028||value==0x2029)return 0;
    }
    return 1;
}
/* print_status.enable is sticky on this firmware and may remain true after a
 * completed job. machine_status is the live authority: 1=Idle, 2=Printing. */
static int printing(const mqtt_client *m){return m->have_machine_status&&m->machine_status==2;}

/* Fixed screw-tramming corners. IDs only; coordinates never come from the request. */
static const struct { char id[4]; int x, y; } SCREW_CORNERS[] = {
    {"FL", 30, 30},
    {"FR", 230, 30},
    {"RR", 230, 225},
    {"RL", 30, 225},
};
#define SCREW_CORNER_COUNT ((int)(sizeof(SCREW_CORNERS)/sizeof(SCREW_CORNERS[0])))

static struct {
    int active, start_done, job_pending, xy_seen;
    unsigned long job_generation;
    time_t last, job_done, bed_renew_at, started;
    control_screws_job job;
} screws_session;
/* Kept outside the session: it must survive the session being cleared. */
static int screws_heaters_off_pending;
/* The job the last built tram script runs; copied into the session once queued. */
static control_screws_job screws_next;
/* The session's last successfully completed job, reported once to the history. */
static control_screws_job screws_finished;
static int screws_finished_ready;
/* The one-shot screws:measure runs without a session; tracked on its own. */
static struct { int pending; unsigned long generation; control_screws_job job; } screws_quick;

void control_screws_note_quick(unsigned long generation){
    screws_quick.pending=1;
    screws_quick.generation=generation;
    screws_quick.job=(control_screws_job){CONTROL_SCREWS_JOB_QUICK,-1,0,time(NULL)};
}
int control_screws_take_finished(control_screws_job *job){
    if(!screws_finished_ready)return 0;
    screws_finished_ready=0;
    if(job)*job=screws_finished;
    return 1;
}
const char *control_screws_corner_id(int corner){
    return corner>=0&&corner<SCREW_CORNER_COUNT?SCREW_CORNERS[corner].id:NULL;
}
int control_screws_corner_at(double x,double y){
    for(int i=0;i<SCREW_CORNER_COUNT;i++)
        if(lround(x)==SCREW_CORNERS[i].x&&lround(y)==SCREW_CORNERS[i].y)return i;
    return -1;
}

void control_screws_reset(void){memset(&screws_session,0,sizeof(screws_session));}
void control_screws_invalidate(void){control_screws_reset();}
int control_screws_session_active(void){return screws_session.active;}
int control_screws_heaters_off_pending(void){return screws_heaters_off_pending;}
void control_screws_heaters_off_sent(void){screws_heaters_off_pending=0;}
/* A queued action that sets or clears heater targets hands the heaters back to
 * the user (or starts a new session), so a deferred shutdown must not undo it. */
void control_screws_note_action(const char *action){
    static const char *const owners[]={"heaters:","heater:","pid:","preset:","preheat:","screws:",
                                       "system:heaters_off","system:all_off"};
    if(!action)return;
    for(size_t i=0;i<sizeof(owners)/sizeof(owners[0]);i++)
        if(!strncmp(action,owners[i],strlen(owners[i]))){screws_heaters_off_pending=0;return;}
}
/* The firmware switches an idle bed off 15 minutes after the last command (see
 * plates_cal_keep_bed in plates.h). Between re-probes nothing else is sent, so a
 * session that waits longer would measure a cooling bed. */
int control_screws_bed_renew_due(time_t now){
    return screws_session.active&&screws_session.start_done&&!screws_session.job_pending&&
           screws_session.bed_renew_at&&now>=screws_session.bed_renew_at;
}
void control_screws_bed_renewed(time_t now,int delivered){
    if(!screws_session.active)return;
    screws_session.bed_renew_at=now+(delivered?SCREWS_BED_RENEW_S:30);
}
static int tram_end_with_heaters_off(void){
    control_screws_invalidate();
    screws_heaters_off_pending=1;
    return 1;
}
static int tram_xy_homed(const mqtt_client *m){
    return m&&m->have_position&&strchr(m->homed_axes,'x')&&strchr(m->homed_axes,'y');
}
int control_screws_xy_homed(const mqtt_client *m){
    return screws_session.active&&tram_xy_homed(m);
}
int control_screws_is_tram(const char *action){return action&&!strncmp(action,"screws:tram:",12);}
void control_screws_note_job(unsigned long generation){
    if(!screws_session.active)return;
    screws_session.job_pending=1;
    screws_session.job_generation=generation;
    screws_session.job=screws_next;
    screws_session.job.started=screws_session.started;
}
static int tram_idle(const mqtt_client *m,char *reason,size_t cap){
    time_t now=time(NULL);
    if(printing(m))return reject(reason,cap,"Screw tramming is blocked while printing");
    if(!m->connected||!m->registered||!m->have_machine_status||m->machine_status!=1||
       m->last_message<=0||now<m->last_message||now-m->last_message>15)
        return reject(reason,cap,"Screw tramming requires the printer to be idle with fresh telemetry");
    return 1;
}
static int tram_script_ok(int n,size_t cap,char *reason,size_t reason_cap){
    if(n<0||(size_t)n>=cap||(size_t)n>=CONSOLE_COMMAND_MAX)
        return reject(reason,reason_cap,"Screw tramming command exceeds buffer capacity");
    return 1;
}
static int append_tram_corners(char *script,size_t cap,size_t used,int only,int twice,
                               char *reason,size_t reason_cap){
    int from=only<0?0:only,to=only<0?SCREW_CORNER_COUNT:only+1;
    for(int i=from;i<to;i++){
        int n=snprintf(script+used,cap-used,
            "G1 Z10 F600\nG1 X%d Y%d F12000\nPROBE SAMPLES=3\nG1 Z10 F600\n%s",
            SCREW_CORNERS[i].x,SCREW_CORNERS[i].y,twice?"PROBE SAMPLES=3\nG1 Z10 F600\n":"");
        if(!tram_script_ok(n,cap-used,reason,reason_cap))return 0;
        used+=(size_t)n;
    }
    if(used>=CONSOLE_COMMAND_MAX)return reject(reason,reason_cap,"Screw tramming command exceeds buffer capacity");
    return 1;
}
/* Every session measurement settles for SCREWS_SETTLE_S before its first probe,
 * the start too: on a 02.01.01.13 printer the start, probing right after M190,
 * read up to 10 um away from the settled re-probes, which repeat within 2 um.
 * The dwell is G4 P<milliseconds>: Klipper's G4 reads only P. With G4 S30 the
 * printer did not wait (single-corner re-probes, including G28 Z and three
 * probes, completed within 20 s of each other). */
/* Full G28 (and the home:Z path) heat the nozzle to 140 C and leave that
 * target. G28 Z after a hot G28 is short, but the same restore is applied
 * fail-closed: probe while the nozzle is still at the homing temperature,
 * then switch it off and keep the bed at 60 C. */
static int tram_close_script(char *script,size_t cap,char *reason,size_t reason_cap){
    size_t used=strlen(script);
    int n=snprintf(script+used,cap>used?cap-used:0,
        "RESTORE_GCODE_STATE NAME=CC2_SCREW_TRAM\n"
        "M104 S0\n"
        "M140 S60\n");
    if(!tram_script_ok(n,cap>used?cap-used:0,reason,reason_cap))return 0;
    if(used+(size_t)n>=CONSOLE_COMMAND_MAX)
        return reject(reason,reason_cap,"Screw tramming command exceeds buffer capacity");
    return 1;
}
/* The session's own G28 / G28 Z / PROBE make the printer report Manual homing
 * (10) or Auto leveling (5) instead of Idle, and the report trails the console
 * job by a poll. Those two states are accepted while the session's own job runs
 * and for a short grace period after it completed; nothing else is. */
static int tram_own_job(time_t now){
    if(screws_session.job_pending)return 1;
    return screws_session.job_done&&now>=screws_session.job_done&&
           now-screws_session.job_done<=SCREWS_STATUS_GRACE_S;
}
int control_screws_tick(const mqtt_client *m,int console_completed,int console_success,
                        unsigned long generation,int emergency,time_t now){
    if(m&&printing(m))screws_heaters_off_pending=0; /* A print owns the heaters now. */
    if(screws_quick.pending&&(emergency||(console_completed&&generation==screws_quick.generation))){
        screws_quick.pending=0;
        if(!emergency&&console_success){screws_finished=screws_quick.job;screws_finished_ready=1;}
    }
    if(!screws_session.active)return 0;
    if(emergency||!m||!m->connected||!m->registered)return tram_end_with_heaters_off();
    if(printing(m)){
        control_screws_invalidate();
        return 0;
    }
    if(m->last_message<=0||now<m->last_message||now-m->last_message>15)return tram_end_with_heaters_off();
    if(screws_session.job_pending&&console_completed&&generation==screws_session.job_generation){
        screws_session.job_pending=0;
        if(!console_success)return tram_end_with_heaters_off();
        screws_finished=screws_session.job;
        screws_finished_ready=1;
        screws_session.start_done=1;
        screws_session.job_done=now;
        screws_session.last=now;
        /* The job's closing M140 S60 restarted the firmware's bed idle count. */
        screws_session.bed_renew_at=now+SCREWS_BED_RENEW_S;
    }
    int own=tram_own_job(now);
    if(!m->have_machine_status)return tram_end_with_heaters_off();
    if(m->machine_status!=1&&!(own&&(m->machine_status==10||m->machine_status==5)))
        return tram_end_with_heaters_off();
    if(screws_session.last&&now>=screws_session.last&&
       now-screws_session.last>SCREWS_SESSION_TIMEOUT_S)return tram_end_with_heaters_off();
    if(tram_xy_homed(m))screws_session.xy_seen=1;
    else if(screws_session.xy_seen&&!own)return tram_end_with_heaters_off();
    return 0;
}

/* The CC2 firmware heats the nozzle to 140 C while executing G28. Unlike the
 * stock display/app path, a raw G28 does not restore the previous heater
 * target afterwards. Always append an explicit restore so manual homing
 * cannot leave the hotend heating indefinitely. */
static double homing_extruder_restore_target(const mqtt_client *m){
    if(m->have_extruder_target&&m->extruder_target>=0.0&&m->extruder_target<=CC2_NOZZLE_TARGET_MAX)
        return m->extruder_target;
    return 0.0;
}

int control_build_script(const char *action,const mqtt_client *m,char *script,size_t cap,char *reason,size_t reason_cap){
    char kind[32],a[32],b[32],colour[16]; double value,value2;
    int slot,min_temp,max_temp;
    if(!strncmp(action,"grille:",7)){
        time_t now=time(NULL);
        if(strcmp(action,"grille:open")&&strcmp(action,"grille:close"))
            return reject(reason,reason_cap,"Invalid exhaust grille action");
        if(!m->connected||!m->registered||!m->have_machine_status||m->machine_status!=1||
           m->last_message<=0||now<m->last_message||now-m->last_message>15)
            return reject(reason,reason_cap,"Exhaust grille controls require fresh idle telemetry");
        int n=snprintf(script,cap,"%s",!strcmp(action,"grille:open")?"EXHAUST_GRILLE_OPEN":"EXHAUST_GRILLE_CLOSE");
        if(n<0||(size_t)n>=cap)return reject(reason,reason_cap,"Exhaust grille command exceeds buffer capacity");
        return 1;
    }
    if(!strncmp(action,"external:",9)||!strncmp(action,"canvas:catalog:",15)){
        time_t now=time(NULL);
        if(!m->connected||!m->registered||!m->have_machine_status||m->machine_status!=1||
           m->last_message<=0||now<m->last_message||now-m->last_message>15)
            return reject(reason,reason_cap,"Filament controls require fresh idle telemetry");
        int external=!strncmp(action,"external:",9);
        if(external&&!m->external_verified)
            return reject(reason,reason_cap,"External filament requires native confirmation that Canvas is disconnected and the printer is idle");
        if(!strcmp(action,"external:load")||!strcmp(action,"external:unload")){
            int load=!strcmp(action,"external:load");
            if(m->external_verified==2){
                if(!isfinite(m->external_temperature)||m->external_temperature<120||m->external_temperature>CC2_NOZZLE_TARGET_MAX)
                    return reject(reason,reason_cap,"External material requires a valid native maximum nozzle temperature");
                snprintf(script,cap,"USED_CANVAS_DEV USED_CANVAS=0\nM600 S%d TEMP=%.0f",load?1:2,m->external_temperature);
            }else snprintf(script,cap,"M600 S%d",load?1:2);
            return 1;
        }
        int brand,type,item,end=0;char hex[7];int parsed;
        if(external)parsed=sscanf(action,"external:material:%d:%d:%d:%6[0-9a-fA-F]%n",&brand,&type,&item,hex,&end);
        else parsed=sscanf(action,"canvas:catalog:%d:%d:%d:%d:%6[0-9a-fA-F]%n",&slot,&brand,&type,&item,hex,&end);
        if(parsed!=(external?4:5)||!end||action[end]||strlen(hex)!=6)
            return reject(reason,reason_cap,"Invalid filament material selection");
        if(!external&&(!canvas_source_valid(m)||canvas_reported_status(m,slot)<0))
            return reject(reason,reason_cap,"Canvas material editing requires a known connected slot");
        const external_material *selected=NULL;
        for(size_t i=0;i<sizeof(external_materials)/sizeof(external_materials[0]);i++){
            const external_material *candidate=&external_materials[i];
            if(candidate->brand==brand&&candidate->type==type&&candidate->item==item){selected=candidate;break;}
        }
        if(!selected||selected->min<120||selected->max>CC2_NOZZLE_TARGET_MAX||selected->min>=selected->max)
            return reject(reason,reason_cap,"Selected material exceeds the protected nozzle temperature range");
        char channel[32],code[20];
        if(external)snprintf(channel,sizeof(channel),"EXTERN_FILA=1");
        else snprintf(channel,sizeof(channel),"CHANNEL=%d",slot);
        if(brand==0)snprintf(code,sizeof(code),"0x%04X",selected->code);
        else snprintf(code,sizeof(code),"Generic");
        int n=snprintf(script,cap,"CANVAS_SET_FILAMENT_INFO %s COLOR=0x%s detailed_type=\"%s\" MANUFACTURER=\"%s\" nozzle_max_temp=%d nozzle_min_temp=%d TYPE=\"%s\" CODE=%s",
            channel,hex,selected->name,selected->maker,selected->max,selected->min,selected->material,code);
        if(n<0||(size_t)n>=cap)return reject(reason,reason_cap,"Filament command exceeds buffer capacity");
        return 1;
    }
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
    if(strncmp(action,"pid:",4)==0) {
        time_t now=time(NULL);int end=0;
        if(!m->connected||!m->registered||!m->have_machine_status||m->machine_status!=1||
           m->last_message<=0||now<m->last_message||now-m->last_message>15)
            return reject(reason,reason_cap,"PID calibration requires fresh idle telemetry");
        if(strcmp(action,"pid:save")==0){snprintf(script,cap,"SAVE_CONFIG");return 1;}
        if(sscanf(action,"pid:%31[^:]:%lf%n",a,&value,&end)!=2||action[end]||!isfinite(value))
            return reject(reason,reason_cap,"Invalid PID calibration request");
        int hotend=!strcmp(a,"extruder"),bed=!strcmp(a,"heater_bed");
        if((!hotend&&!bed)||value<(hotend?150:40)||value>(hotend?300:120))
            return reject(reason,reason_cap,"PID target outside heater limits");
        snprintf(script,cap,"PID_CALIBRATE HEATER=%s TARGET=%.1f\nTURN_OFF_HEATERS",a,value);
        return 1;
    }
    if(!strncmp(action,"screws:tram:",12)){
        if(!tram_idle(m,reason,reason_cap))return 0;
        if(!strcmp(action,"screws:tram:start")){
            int n=snprintf(script,cap,
                "SAVE_GCODE_STATE NAME=CC2_SCREW_TRAM\n"
                "M140 S60\n"
                "G28\n"
                "G180 S3\n"
                "M190 S60\n"
                "G4 P%d\n"
                "G90\n",SCREWS_SETTLE_S*1000);
            if(!tram_script_ok(n,cap,reason,reason_cap))return 0;
            if(!append_tram_corners(script,cap,(size_t)n,-1,0,reason,reason_cap))return 0;
            if(!tram_close_script(script,cap,reason,reason_cap))return 0;
            control_screws_reset();
            screws_session.active=1;
            screws_session.last=time(NULL);
            screws_session.started=screws_session.last;
            screws_next=(control_screws_job){CONTROL_SCREWS_JOB_START,-1,0,0};
            return 1;
        }
        if(!strcmp(action,"screws:tram:check")||!strcmp(action,"screws:tram:check:2")){
            int twice=!strcmp(action,"screws:tram:check:2");
            if(!screws_session.active)
                return reject(reason,reason_cap,"Screw tramming session is not active");
            if(!tram_xy_homed(m))
                return reject(reason,reason_cap,"Screw tramming re-probe requires homed X and Y");
            int n=snprintf(script,cap,
                "SAVE_GCODE_STATE NAME=CC2_SCREW_TRAM\n"
                "G28 Z\n"
                "G4 P%d\n"
                "G90\n",SCREWS_SETTLE_S*1000);
            if(!tram_script_ok(n,cap,reason,reason_cap))return 0;
            if(!append_tram_corners(script,cap,(size_t)n,-1,twice,reason,reason_cap))return 0;
            if(!tram_close_script(script,cap,reason,reason_cap))return 0;
            screws_session.last=time(NULL);
            screws_next=(control_screws_job){CONTROL_SCREWS_JOB_CHECK,-1,twice,0};
            return 1;
        }
        if(!strncmp(action,"screws:tram:probe:",18)){
            if(!screws_session.active)
                return reject(reason,reason_cap,"Screw tramming session is not active");
            if(!tram_xy_homed(m))
                return reject(reason,reason_cap,"Screw tramming re-probe requires homed X and Y");
            const char *rest=action+18;
            char id[8];int end=0,twice=0,corner=-1;
            if(sscanf(rest,"%7[^:]%n",id,&end)!=1||end<1)
                return reject(reason,reason_cap,"Unknown screw corner");
            if(rest[end]==0)twice=0;
            else if(!strcmp(rest+end,":2"))twice=1;
            else return reject(reason,reason_cap,"Unknown screw corner");
            for(int i=0;i<SCREW_CORNER_COUNT;i++)if(!strcmp(SCREW_CORNERS[i].id,id)){corner=i;break;}
            if(corner<0)return reject(reason,reason_cap,"Unknown screw corner");
            int n=snprintf(script,cap,
                "SAVE_GCODE_STATE NAME=CC2_SCREW_TRAM\n"
                "G28 Z\n"
                "G4 P%d\n"
                "G90\n",SCREWS_SETTLE_S*1000);
            if(!tram_script_ok(n,cap,reason,reason_cap))return 0;
            if(!append_tram_corners(script,cap,(size_t)n,corner,twice,reason,reason_cap))return 0;
            if(!tram_close_script(script,cap,reason,reason_cap))return 0;
            screws_session.last=time(NULL);
            screws_next=(control_screws_job){CONTROL_SCREWS_JOB_PROBE,corner,twice,0};
            return 1;
        }
        if(!strcmp(action,"screws:tram:end")){
            /* Start/probe/check already RESTORE. Repeating it here would fail
             * if the name is gone, or re-apply the pre-job motion state. */
            int n=snprintf(script,cap,"M104 S0\nM140 S0");
            if(!tram_script_ok(n,cap,reason,reason_cap))return 0;
            control_screws_invalidate();
            return 1;
        }
        return reject(reason,reason_cap,"Unknown screw tramming action");
    }
    /* Stage 2: guarded four-corner measurement, never called by print start. */
    if(strcmp(action,"screws:measure")==0){
        if(printing(m))return reject(reason,reason_cap,"Screw measurement is blocked while printing");
        if(!m->have_machine_status||m->machine_status!=1)
            return reject(reason,reason_cap,"Screw measurement requires the printer to be idle");
        control_screws_invalidate();
        /* Match the CC2 stock levelling preparation: heat the bed, run the
         * firmware-owned nozzle cleaning sequence (G180 S3), then wait for the
         * bed at 60 C before measuring.  The stock calibration completion path
         * switches both heaters off, so do the same here instead of restoring
         * the 140 C probing target left by G28/G180. */
        snprintf(script,cap,
            "SAVE_GCODE_STATE NAME=CC2_SCREW_MEASURE\n"
            "M140 S60\n"
            "G180 S3\n"
            "M190 S60\n"
            "G90\nG1 Z10 F600\n"
            "G1 X30 Y30 F12000\nPROBE SAMPLES=3\nG1 Z10 F600\n"
            "G1 X230 Y30 F12000\nPROBE SAMPLES=3\nG1 Z10 F600\n"
            "G1 X230 Y225 F12000\nPROBE SAMPLES=3\nG1 Z10 F600\n"
            "G1 X30 Y225 F12000\nPROBE SAMPLES=3\nG1 Z10 F600\n"
            "RESTORE_GCODE_STATE NAME=CC2_SCREW_MEASURE\n"
            "M104 S0\nM140 S0");
        return 1;
    }
    if(sscanf(action,"zoffset:undo:%lf",&value)==1){
        if(!m->have_machine_status||(m->machine_status!=1&&m->machine_status!=2))
            return reject(reason,reason_cap,"Z offset requires an idle or printing printer");
        if(!m->have_position||strchr(m->homed_axes,'z')==NULL)
            return reject(reason,reason_cap,"Z axis must be homed before live offset adjustment");
        if(fabs(value)<0.0005||fabs(value)>0.5001)
            return reject(reason,reason_cap,"Session Z offset undo exceeds the protected range");
        snprintf(script,cap,"SET_GCODE_OFFSET Z_ADJUST=%+.3f MOVE=1 MOVE_SPEED=5",value);
        return 1;
    }
    if(sscanf(action,"zoffset:adjust:%lf",&value)==1){
        if(!m->have_machine_status||(m->machine_status!=1&&m->machine_status!=2))
            return reject(reason,reason_cap,"Z offset requires an idle or printing printer");
        if(!m->have_position||strchr(m->homed_axes,'z')==NULL)
            return reject(reason,reason_cap,"Z axis must be homed before live offset adjustment");
        if(fabs(value)<0.0005||fabs(value)>0.0501)
            return reject(reason,reason_cap,"Each Z offset adjustment must be 0.01 or 0.05 mm");
        snprintf(script,cap,"SET_GCODE_OFFSET Z_ADJUST=%+.3f MOVE=1 MOVE_SPEED=5",value);
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
    if(strncmp(action,"heaters:set:",12)==0){
        time_t now=time(NULL);int end=0;
        if(!m->connected||!m->registered||!m->have_machine_status||
           (m->machine_status!=1&&m->machine_status!=2)||m->last_message<=0||
           now<m->last_message||now-m->last_message>15)
            return reject(reason,reason_cap,"Temperature targets require fresh idle or printing telemetry");
        if(sscanf(action,"heaters:set:%lf:%lf%n",&value,&value2,&end)!=2||action[end]||
           !isfinite(value)||!isfinite(value2)||value<0||value>CC2_NOZZLE_TARGET_MAX||value2<0||value2>120)
            return reject(reason,reason_cap,"Invalid temperature targets");
        snprintf(script,cap,"SET_HEATER_TEMPERATURE HEATER=extruder TARGET=%.1f\nSET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=%.1f",value,value2);
        return 1;
    }
    if(sscanf(action,"%31[^:]:%31[^:]:%lf",kind,a,&value)==3&&strcmp(kind,"heater")==0){
        if(strcmp(a,"extruder")==0){if(!isfinite(value)||value<0||value>CC2_NOZZLE_TARGET_MAX)return reject(reason,reason_cap,"Nozzle target must be 0..350 C");}
        else if(strcmp(a,"heater_bed")==0){if(value<0||value>120)return reject(reason,reason_cap,"Bed target must be 0..120 C");}
        else return reject(reason,reason_cap,"Unknown heater");
        snprintf(script,cap,"SET_HEATER_TEMPERATURE HEATER=%s TARGET=%.1f",a,value);return 1;
    }
    if(strcmp(action,"heaters:off")==0||strcmp(action,"system:heaters_off")==0){
        time_t now=time(NULL);
        if(!m->connected||!m->registered||!m->have_machine_status||m->machine_status!=1||
           m->last_message<=0||now<m->last_message||now-m->last_message>15)
            return reject(reason,reason_cap,"Heaters off requires the printer to be idle with fresh telemetry");
        control_screws_invalidate();
        snprintf(script,cap,"TURN_OFF_HEATERS");return 1;
    }
    if(sscanf(action,"fan:%31[^:]:%lf",a,&value)==2){
        if(value<0||value>100)return reject(reason,reason_cap,"Fan value must be 0..100 percent");
        int pwm=(int)lround(value*2.55);
        if(strcmp(a,"part")==0)snprintf(script,cap,"M106 S%d",pwm);
        else if(strcmp(a,"aux")==0)snprintf(script,cap,"M106 P2 S%d",pwm);
        else if(strcmp(a,"box")==0)snprintf(script,cap,"SET_CAVITY_FAN SPEED=%d",pwm);
        else return reject(reason,reason_cap,"Unknown fan");
        return 1;
    }
    if(!strncmp(action,"canvas:recover-load:",20)){
        if(strlen(action)!=21||action[20]<'0'||action[20]>'3')
            return reject(reason,reason_cap,"Invalid Canvas recovery slot");
        slot=action[20]-'0';
        if(!canvas_recovery_load_allowed(m,slot))
            return reject(reason,reason_cap,"Canvas recovery requires fresh idle telemetry, a present slot and an unassigned head");
        snprintf(script,cap,"CANVAS_LOAD_FILAMENT CHANNEL=%d",slot);return 1;
    }
    if(sscanf(action,"canvas:load:%d",&slot)==1){
        if(printing(m))return reject(reason,reason_cap,"Canvas loading is blocked while printing");
        if(!m->have_machine_status||m->machine_status!=1)return reject(reason,reason_cap,"Canvas loading requires the printer to be idle");
        if(slot<0||slot>3)return reject(reason,reason_cap,"Canvas slot must be 1..4");
        int end=0;
        if(sscanf(action,"canvas:load:%d%n",&slot,&end)!=1||action[end])return reject(reason,reason_cap,"Invalid Canvas action");
        canvas_slot_view view=canvas_slot_policy(m,slot);
        if(!view.load)return reject(reason,reason_cap,canvas_reason_name(view.reason_load));
        snprintf(script,cap,"CANVAS_LOAD_FILAMENT CHANNEL=%d",slot);return 1;
    }
    if(sscanf(action,"canvas:unload:%d",&slot)==1){
        if(printing(m))return reject(reason,reason_cap,"Canvas unloading is blocked while printing");
        if(!m->have_machine_status||m->machine_status!=1)return reject(reason,reason_cap,"Canvas unloading requires the printer to be idle");
        if(slot<0||slot>3)return reject(reason,reason_cap,"Canvas slot must be 1..4");
        int end=0;
        if(sscanf(action,"canvas:unload:%d%n",&slot,&end)!=1||action[end])return reject(reason,reason_cap,"Invalid Canvas action");
        canvas_slot_view view=canvas_slot_policy(m,slot);
        if(!view.unload)return reject(reason,reason_cap,canvas_reason_name(view.reason_unload));
        snprintf(script,cap,"CANVAS_UNLOAD_FILAMENT CHANNEL=%d",slot);return 1;
    }
    /* Stock display reset_multi_filament_set resets the four channels by
     * sending empty material fields through CANVAS_SET_FILAMENT_INFO. */
    if(!strcmp(action,"canvas:reset-all")){
        time_t now=time(NULL);
        if(!m->connected||!m->registered||m->last_message<=0||now<m->last_message||now-m->last_message>15)
            return reject(reason,reason_cap,"Printer telemetry must be connected and fresh");
        if(printing(m))return reject(reason,reason_cap,"Canvas material editing is blocked while printing");
        if(!m->have_machine_status||m->machine_status!=1)return reject(reason,reason_cap,"Canvas material editing requires the printer to be idle");
        size_t used=0;
        for(int channel=0;channel<4;channel++){
            int n=snprintf(script+used,cap-used,
                "%sCANVAS_SET_FILAMENT_INFO CHANNEL=%d COLOR= detailed_type=\"\" MANUFACTURER= nozzle_max_temp= nozzle_min_temp= TYPE=\"\" CODE=",
                channel?"\n":"",channel);
            if(n<0||(size_t)n>=cap-used)return reject(reason,reason_cap,"Canvas reset command is too long");
            used+=(size_t)n;
        }
        return 1;
    }
    /* Optionally followed by a spool's product line and maker, so the tray names the spool as the inventory does. */
    char line[48],maker[48];int short_end=-1,long_end=-1;
    int fields=sscanf(action,"canvas:material:%d:%31[^:]:%15[^:]:%d:%d%n:%47[^:]:%47[^:]%n",&slot,a,colour,&min_temp,&max_temp,&short_end,line,maker,&long_end);
    size_t action_len=strlen(action);
    if((fields==5&&short_end>=0&&(size_t)short_end==action_len)||(fields==7&&long_end>=0&&(size_t)long_end==action_len)){
        if(printing(m))return reject(reason,reason_cap,"Canvas material editing is blocked while printing");
        if(!m->have_machine_status||m->machine_status!=1)return reject(reason,reason_cap,"Canvas material editing requires the printer to be idle");
        if(slot<0||slot>3)return reject(reason,reason_cap,"Canvas slot must be 1..4");
        if(!valid_material_name(a))return reject(reason,reason_cap,"Material name may contain only letters, numbers, internal spaces, +, - and _");
        if(strlen(colour)!=6||strspn(colour,"0123456789abcdefABCDEF")!=6)return reject(reason,reason_cap,"Canvas colour must be six hexadecimal digits");
        if(min_temp<120||max_temp>CC2_NOZZLE_TARGET_MAX||min_temp>=max_temp)return reject(reason,reason_cap,"Invalid Canvas nozzle temperature range");
        if(fields==7&&(!valid_filament_label(line)||!valid_filament_label(maker)))
            return reject(reason,reason_cap,"Filament line and maker may contain only letters, numbers, spaces, +, -, _ and .");
        snprintf(script,cap,"CANVAS_SET_FILAMENT_INFO CHANNEL=%d COLOR=0x%s detailed_type=\"%s\" MANUFACTURER=\"%s\" nozzle_max_temp=%d nozzle_min_temp=%d TYPE=\"%s\" CODE=Generic",
            slot,colour,fields==7?line:a,fields==7?maker:"Generic",max_temp,min_temp,a);return 1;
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
        if(!isfinite(value)||!isfinite(value2)||value<0||value>CC2_NOZZLE_TARGET_MAX||value2<0||value2>120)return reject(reason,reason_cap,"Preheat targets exceed the protected range");
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
    /* X/Y TMC2209 chopper mode, both steppers together (CoreXY). The native
     * SET_TMC_FIELD treats VALUE=0 as "not given" (error 803), so the zero
     * writes use values whose masked field is 0: en_spreadcycle is one bit
     * (2 -> 0) and tpwmthrs 20 bits (0x100000 -> 0). Both reverse writes are
     * ordinary non-zero values. DUMP_TMC puts the read-back in the console.
     * StealthChop loses steps at high speed and acceleration (a hybrid
     * tpwmthrs threshold does not prevent it), so Quiet always caps the
     * toolhead first and Normal only lifts the cap after SpreadCycle is back. */
    if(!strcmp(action,"motors:quiet")||!strcmp(action,"motors:normal")){
        time_t now=time(NULL);int quiet=action[7]=='q';
        if(printing(m))return reject(reason,reason_cap,"Motor mode cannot change while printing");
        if(!m->connected||!m->registered||!m->have_machine_status||m->machine_status!=1||
           m->last_message<=0||now<m->last_message||now-m->last_message>15)
            return reject(reason,reason_cap,"Motor mode requires the printer to be idle with fresh telemetry");
        static const char *const steppers[]={"stepper_x","stepper_y"};
        int n=quiet?snprintf(script,cap,"M400\nSET_VELOCITY_LIMIT VELOCITY=%s ACCEL=%s",
                             CONTROL_QUIET_VELOCITY,CONTROL_QUIET_ACCEL):0;
        if(n<0||(size_t)n>=cap)return reject(reason,reason_cap,"Motor mode command is too long");
        size_t used=(size_t)n;
        for(int i=0;i<2;i++){
            n=quiet?snprintf(script+used,cap-used,
                    "\nSET_TMC_FIELD STEPPER=%s FIELD=tpwmthrs VALUE=%s"
                    "\nSET_TMC_FIELD STEPPER=%s FIELD=en_spreadcycle VALUE=%s",
                    steppers[i],CONTROL_TPWMTHRS_ZERO,steppers[i],CONTROL_SPREADCYCLE_ZERO):
                snprintf(script+used,cap-used,
                    "%sSET_TMC_FIELD STEPPER=%s FIELD=en_spreadcycle VALUE=1"
                    "\nSET_TMC_FIELD STEPPER=%s FIELD=tpwmthrs VALUE=%s",
                    i?"\n":"",steppers[i],steppers[i],CONTROL_TPWMTHRS_MAX);
            if(n<0||(size_t)n>=cap-used)return reject(reason,reason_cap,"Motor mode command is too long");
            used+=(size_t)n;
        }
        if(!quiet){
            n=snprintf(script+used,cap-used,"\nSET_VELOCITY_LIMIT VELOCITY=%s ACCEL=%s",
                       CONTROL_NORMAL_VELOCITY,CONTROL_NORMAL_ACCEL);
            if(n<0||(size_t)n>=cap-used)return reject(reason,reason_cap,"Motor mode command is too long");
            used+=(size_t)n;
        }
        n=snprintf(script+used,cap-used,"\nDUMP_TMC STEPPER=stepper_x\nDUMP_TMC STEPPER=stepper_y");
        if(n<0||(size_t)n>=cap-used)return reject(reason,reason_cap,"Motor mode command is too long");
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
    if(strcmp(action,"system:fans_off")==0){
        if(printing(m))return reject(reason,reason_cap,"Fans off is blocked while printing");
        if(!m->have_machine_status||m->machine_status!=1)return reject(reason,reason_cap,"Fans off requires the printer to be idle");
        snprintf(script,cap,"M106 S0\nM106 P2 S0\nSET_CAVITY_FAN SPEED=0");return 1;
    }
    if(strcmp(action,"system:emergency_stop")==0){control_screws_invalidate();snprintf(script,cap,"M112");return 1;}
    if(sscanf(action,"light:%31s",a)==1){
        if(strcmp(a,"on")&&strcmp(a,"off"))return reject(reason,reason_cap,"Unknown light state");
        return reject(reason,reason_cap,"Light control requires the native printer API");
    }
    if(!strncmp(action,"tune:pressure-advance:",22)){
        const char *number=action+22;char *end=NULL;
        if(!*number||strspn(number,"0123456789.")!=strlen(number))
            return reject(reason,reason_cap,"Pressure Advance must be a decimal number");
        const char *decimal=strchr(number,'.');
        if(decimal&&strlen(decimal+1)>6)
            return reject(reason,reason_cap,"Pressure Advance supports at most six decimal places");
        value=strtod(number,&end);
        if(!end||*end||!isfinite(value)||value<0||value>1)
            return reject(reason,reason_cap,"Pressure Advance must be 0..1 seconds");
        time_t now=time(NULL);
        if(!m->connected||!m->registered||!m->have_machine_status||m->last_message<=0||now<m->last_message||now-m->last_message>15)
            return reject(reason,reason_cap,"Printer telemetry must be connected and fresh");
        if(m->machine_status!=1&&(!printing(m)||!m->filename[0]||
            (strcmp(m->print_state,"printing")&&strcmp(m->print_state,"paused"))))
            return reject(reason,reason_cap,"Pressure Advance requires Idle or an active/paused print");
        int n=snprintf(script,cap,"SET_PRESSURE_ADVANCE EXTRUDER=extruder ADVANCE=%.6f",value);
        if(n<0||(size_t)n>=cap)return reject(reason,reason_cap,"Tuning command exceeds buffer size");
        return 1;
    }
    if(strncmp(action,"tune:",5)==0){
        const char *number=NULL;int is_speed=0;
        if(strncmp(action,"tune:speed:",11)==0){number=action+11;is_speed=1;}
        else if(strncmp(action,"tune:flow:",10)==0)number=action+10;
        else return reject(reason,reason_cap,"Unknown tuning action");
        if(!*number||strspn(number,"0123456789")!=strlen(number))
            return reject(reason,reason_cap,"Tuning value must be a whole percentage");
        value=strtod(number,NULL);
        if(!isfinite(value)||value<(is_speed?25:50)||value>(is_speed?200:150))
            return reject(reason,reason_cap,is_speed?"Speed must be 25..200 percent":"Flow must be 50..150 percent");
        time_t now=time(NULL);
        if(!m->connected||!m->registered||m->last_message<=0||now<m->last_message||now-m->last_message>15)
            return reject(reason,reason_cap,"Printer telemetry must be connected and fresh");
        if(!printing(m)||!m->filename[0]||(strcmp(m->print_state,"printing")&&strcmp(m->print_state,"paused")))
            return reject(reason,reason_cap,"Tuning requires an active or paused print");
        int n=snprintf(script,cap,"M%d S%.0f",is_speed?220:221,value);
        if(n<0||(size_t)n>=cap)return reject(reason,reason_cap,"Tuning command exceeds buffer size");
        return 1;
    }
    if(sscanf(action,"print:%31s",b)==1){
        if(strcmp(b,"pause")==0){if(!printing(m))return reject(reason,reason_cap,"No print is active");snprintf(script,cap,"PAUSE");return 1;}
        if(strcmp(b,"resume")==0){snprintf(script,cap,"RESUME");return 1;}
        if(strcmp(b,"cancel")==0){if(!printing(m))return reject(reason,reason_cap,"No print is active");snprintf(script,cap,"CANCEL_PRINT");return 1;}
    }
    return reject(reason,reason_cap,"Unknown or unsupported control action");
}

int control_motor_mode_restore_due(int *seen_print,int quiet,int machine_status,const char *print_state){
    int job=machine_status==2||(print_state&&(!strcmp(print_state,"printing")||!strcmp(print_state,"paused")));
    if(!quiet){*seen_print=0;return 0;}
    if(job){*seen_print=1;return 0;}
    if(!*seen_print||machine_status!=1)return 0;
    *seen_print=0;
    return 1;
}

int control_idle_motors_due(control_idle_motors *s,const control_idle_sample *in,time_t now){
    /* The board fan runs while a stepper is enabled or a heater has a target,
     * so with both targets at zero it reports energised steppers. Unknown
     * telemetry, any job (a paused print included) or a running console
     * command restarts the wait, and so does any movement. */
    int idle=in->valid&&in->machine_status==1&&!in->console_busy&&
        strcmp(in->print_state,"printing")!=0&&strcmp(in->print_state,"paused")!=0&&
        in->nozzle_target<=0.0&&in->bed_target<=0.0&&in->velocity<0.1&&in->board_fan>0.0;
    if(!idle){s->holding=0;return 0;}
    if(!s->holding||fabs(in->x-s->x)>0.01||fabs(in->y-s->y)>0.01||fabs(in->z-s->z)>0.01){
        s->holding=1;s->released=0;s->since=now;s->x=in->x;s->y=in->y;s->z=in->z;
        return 0;
    }
    if(s->released||now-s->since<CONTROL_IDLE_MOTORS_SECONDS)return 0;
    s->released=1;
    return 1;
}
