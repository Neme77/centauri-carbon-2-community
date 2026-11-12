/* Native interactive calibration, driven only by explicit user actions. No
 * motion, heating, automatic acceptance, command retries or waiting-state polls.
 * The journal binds ownership to this boot AND the native printer process. */
#ifndef CC2_LOAD_CELL_GUIDE_H
#define CC2_LOAD_CELL_GUIDE_H
#include "load_cell_guide_core.h"
static const char *cell_journal_path="/tmp/cc2-control-load-cell-session";
static const char cell_query[]="{\"id\":203,\"method\":\"objects/query\",\"params\":{\"objects\":{\"gcode\":[\"commands\"],\"configfile\":[\"settings\",\"save_config_pending\",\"save_config_pending_items\"]}}}\003";
static struct {
    int loaded,state,owned,inflight,action,phase;
    char boot[64],owner[448],hash[65];
    double safety,grams,counts,tare;
    plates_exchange query;
} cell={.query={.fd=-1}};

static int cell_file_hash(char digest[65]){
    size_t length=0;char *file=rotation_file(printer_autosave_path,&length);
    if(!file)return 0;
    sha256_state s;sha256_init(&s);sha256_update(&s,(unsigned char *)file,length);sha256_hex(&s,digest);free(file);return 1;
}
static int cell_write_journal(void){
    char record[900];
    int n=snprintf(record,sizeof(record),"CC2CELL1\n%s\n%s\n%s\n%d %d %d %.17g %.17g %.17g %.17g\n",
        cell.boot,cell.owner,cell.hash,cell.state,cell.owned,cell.inflight,cell.safety,cell.grams,cell.counts,cell.tare);
    return n>0&&(size_t)n<sizeof(record)&&rotation_regular(cell_journal_path,1)&&
        !plates_replace_file(cell_journal_path,NULL,record,(size_t)n,0600);
}
static void cell_load(void){
    if(cell.loaded)return;
    cell.loaded=1;
    if(!rotation_regular(cell_journal_path,1)){cell.state=CELL_UNKNOWN;return;}
    size_t length=0;char *record=plates_read_file(cell_journal_path,900,&length);
    if(!record){if(errno!=ENOENT)cell.state=CELL_UNKNOWN;return;}
    int n=0,state=0,owned=0,inflight=0;char boot[64],owner[448],hash[65];double safety=0,grams=0,counts=0,tare=0;
    int fields=sscanf(record,"CC2CELL1\n%63[^\n]\n%447[^\n]\n%64[^\n]\n%d %d %d %lf %lf %lf %lf\n%n",
        boot,owner,hash,&state,&owned,&inflight,&safety,&grams,&counts,&tare,&n);
    if(fields!=10||n!=(int)length||state<CELL_ZERO||state>CELL_UNKNOWN||owned<0||owned>1||inflight<0||inflight>1||
       strlen(hash)!=64||strspn(hash,"0123456789abcdef")!=64||!isfinite(safety)||!isfinite(grams)||!isfinite(counts)||!isfinite(tare)||
       tare<INT32_MIN||tare>INT32_MAX||floor(tare)!=tare||
       (state>=CELL_REVIEW&&state<=CELL_SAVED&&(!cell_grams_valid(grams,safety)||counts<1||counts>1e9))){
        free(record);cell.state=CELL_UNKNOWN;return;
    }
    free(record);char now[64];canvas_unload_program_id program;
    if(!canvas_unload_boot_id(now,sizeof(now))){cell.state=CELL_UNKNOWN;return;}
    if(strcmp(boot,now)){unlink(cell_journal_path);return;}
    cell.state=state;cell.owned=owned;cell.inflight=inflight;cell.safety=safety;cell.grams=grams;cell.counts=counts;cell.tare=tare;
    snprintf(cell.boot,sizeof(cell.boot),"%s",boot);snprintf(cell.owner,sizeof(cell.owner),"%s",owner);snprintf(cell.hash,sizeof(cell.hash),"%s",hash);
    if(inflight||!canvas_unload_find_program(&program)||strcmp(owner,program.identity))cell.state=CELL_UNKNOWN;
}
static int cell_active(void){cell_load();return cell.state!=CELL_OFF;}
static int cell_same_owner(void){
    canvas_unload_program_id program;char boot[64];
    return cell.owned&&canvas_unload_boot_id(boot,sizeof(boot))&&!strcmp(boot,cell.boot)&&
        canvas_unload_find_program(&program)&&!strcmp(program.identity,cell.owner);
}
static int cell_unchanged_file(void){char hash[65];return cell_file_hash(hash)&&!strcmp(hash,cell.hash);}
static int cell_cold(void){
    double nt,ng,bt,bg;
    return uds_value(&telemetry,U_ET,&nt)&&uds_value(&telemetry,U_EG,&ng)&&
        uds_value(&telemetry,U_BT,&bt)&&uds_value(&telemetry,U_BG,&bg)&&
        isfinite(nt)&&isfinite(bt)&&nt>=0&&bt>=0&&nt<=50&&bt<=50&&ng==0&&bg==0;
}
static int cell_other_pending(void){return nx_pending()||rotation_pending()||canvas_unload_pending()||plates.pending[0]||plates_cal.stage;}
static int cell_pending_matches(cell_snapshot s){
    return s.pending&&s.have_parameters&&!s.other_pending&&fabs(s.counts_per_gram-cell.counts)<0.0000011&&s.tare_counts==cell.tare;
}
/* Read back only the two native SAVE_CONFIG keys. Never write calibration
 * coefficients ourselves or infer acceptance from configured defaults. */
static int cell_saved_matches(void){
    size_t length=0;char *file=rotation_file(printer_autosave_path,&length);if(!file)return 0;
    int section=0,found_counts=0,found_tare=0,valid=1;double counts=0,tare=0;
    char *line=file;
    while(line<file+length){
        char *end=memchr(line,'\n',(size_t)(file+length-line));if(!end)end=file+length;
        char *p=line;while(p<end&&isspace((unsigned char)*p))p++;
        if(end-p>=3&&!memcmp(p,"#*#",3)){
            p+=3;while(p<end&&isspace((unsigned char)*p))p++;
            if(p<end&&*p=='['){
                section=end-p>=17&&!memcmp(p,"[load_cell_probe]",17);
                if(section){char *tail=p+17;while(tail<end&&isspace((unsigned char)*tail))tail++;section=tail==end;}
            }
            else if(section){
                const char *keys[]={"counts_per_gram","reference_tare_counts"};
                for(int i=0;i<2;i++){
                    size_t k=strlen(keys[i]);if((size_t)(end-p)<=k||memcmp(p,keys[i],k))continue;
                    char *v=p+k;while(v<end&&isspace((unsigned char)*v))v++;
                    if(v==end||(*v!='='&&*v!=':'))continue;
                    v++;
                    char text[80];size_t size=(size_t)(end-v);if(size>=sizeof(text)){valid=0;continue;}
                    memcpy(text,v,size);text[size]=0;char *tail;errno=0;double value=strtod(text,&tail);
                    while(isspace((unsigned char)*tail))tail++;
                    if(errno||tail==text||*tail||!isfinite(value)){valid=0;continue;}
                    if(!i){if(found_counts++)valid=0;counts=value;}else{if(found_tare++)valid=0;tare=value;}
                }
            }
        }
        line=end<file+length?end+1:end;
    }
    free(file);return valid&&found_counts==1&&found_tare==1&&fabs(counts-cell.counts)<0.0000011&&tare==cell.tare;
}
static void cell_finish(const char *error){
    plates_exchange_close(&cell.query);
    if(cell.inflight){cell.state=CELL_UNKNOWN;(void)cell_write_journal();}
    tools_finish(error);
}
static int cell_reboot_guard(void){
    return rotation_reboot_guard()&&cell_same_owner()&&!cell_other_pending()&&
        (cell.state==CELL_SAVED?cell_saved_matches():cell.state==CELL_APPLIED&&cell_unchanged_file());
}
static void cell_tick(const mqtt_client *m){
    const char *reason=tools_ready(m);
    if(reason||tools.connection!=telemetry.connections||(!cell_cold()&&cell.action!=CELL_ABORT)){
        cell_finish(reason?reason:"Printer state or temperature changed; calibration stopped");return;
    }
    if(cell.phase!=1){
        char *reply=NULL;size_t length=0;
        int result=plates_exchange_step(&cell.query,cell_query,&reply,&length);if(!result)return;
        if(result<0){cell_finish("Cannot verify native calibration ownership");return;}
        cell_snapshot s=cell_parse_snapshot(reply,length);free(reply);
        if(!s.understood||s.foreign||cell_other_pending()){
            cell_finish("Native calibration or pending settings are not compatible");return;
        }
        if(cell.phase==2){
            int ok=0;
            if(cell.action==CELL_START)ok=s.registered&&!s.pending;
            else if(cell.action==CELL_ABORT)ok=!s.registered&&!s.pending;
            else if(cell.action==CELL_ACCEPT)ok=!s.registered&&cell_pending_matches(s);
            else if(cell.action==CELL_SAVE)ok=!s.registered&&cell_pending_matches(s)&&cell_saved_matches();
            else ok=s.registered&&!s.pending;
            if(!ok){cell_finish("Native calibration result could not be verified; do not repeat acceptance");return;}
            cell.inflight=0;
            if(cell.action==CELL_START)cell.state=CELL_ZERO;
            else if(cell.action==CELL_ABORT){
                if(unlink(cell_journal_path)&&errno!=ENOENT){cell.state=CELL_UNKNOWN;cell_finish("Native abort confirmed, but session cleanup failed");return;}
                cell.state=CELL_OFF;cell.owned=0;
            }
            else if(cell.action==CELL_ZERO_ACTION)cell.state=CELL_LOAD;
            else if(cell.action==CELL_MEASURE)cell.state=CELL_REVIEW;
            else if(cell.action==CELL_ACCEPT)cell.state=CELL_APPLIED;
            else if(cell.action==CELL_SAVE)cell.state=CELL_SAVED;
            if(cell.state&&!cell_write_journal()){cell.state=CELL_UNKNOWN;cell_finish("Cannot retain calibration session");return;}
            cell_finish(NULL);return;
        }
        int initial=cell.action==CELL_START||cell.action==CELL_TARE;
        if(initial){
            if(s.registered||s.pending||!cell_max_grams(s.safety_limit)){
                cell_finish("Finish other calibrations first; valid native load-cell limits are required");return;
            }
            cell.safety=s.safety_limit;
        }else if(!cell_same_owner()||
            ((cell.action==CELL_SAVE||cell.action==CELL_RESTART||cell.action==CELL_DISCARD)?s.registered||!cell_pending_matches(s):!s.registered||s.pending)){
            cell_finish("Native calibration session changed; no command sent");return;
        }
        if(cell.action==CELL_MEASURE&&!cell_grams_valid(cell.grams,s.safety_limit)){
            cell_finish("Known load is outside the permitted range");return;
        }
        if((cell.action==CELL_SAVE||cell.action==CELL_DISCARD)&&!cell_unchanged_file()){
            cell_finish("Printer settings changed during calibration; save and restart refused");return;
        }
        if(cell.action==CELL_RESTART||cell.action==CELL_DISCARD){
            plates_mqtt=(mqtt_client *)m;
            tools.busy=0;int ready=cell_reboot_guard();tools.busy=1;
            if(!ready){cell_finish("Calibration restart checks failed");return;}
            reboot_guard=cell_reboot_guard;reboot_pending=1;reboot_launched=0;reboot_due=recovery_clock()+2;reboot_error="none";
            cell_finish(NULL);return;
        }
        char command[128];
        if(cell.action==CELL_START){
            canvas_unload_program_id program;
            if(!canvas_unload_boot_id(cell.boot,sizeof(cell.boot))||!canvas_unload_find_program(&program)||!cell_file_hash(cell.hash)){
                cell_finish("Cannot record calibration ownership and configuration");return;
            }
            snprintf(cell.owner,sizeof(cell.owner),"%s",program.identity);cell.owned=1;cell.state=CELL_ZERO;cell.counts=cell.tare=0;
        }
        if(cell.action!=CELL_TARE){cell.inflight=1;if(!cell_write_journal()){cell.inflight=0;cell.state=CELL_UNKNOWN;cell_finish("Cannot retain calibration session");return;}}
        switch(cell.action){
          case CELL_TARE:snprintf(command,sizeof(command),"LOAD_CELL_TARE");break;
          case CELL_START:snprintf(command,sizeof(command),"LOAD_CELL_CALIBRATE");break;
          case CELL_ZERO_ACTION:snprintf(command,sizeof(command),"TARE");break;
          case CELL_MEASURE:snprintf(command,sizeof(command),"CALIBRATE GRAMS=%.6f",cell.grams);break;
          case CELL_ACCEPT:snprintf(command,sizeof(command),"ACCEPT");break;
          case CELL_ABORT:snprintf(command,sizeof(command),"ABORT");break;
          case CELL_SAVE:snprintf(command,sizeof(command),"SAVE_CONFIG");break;
          default:cell_finish("Invalid calibration step");return;
        }
        cell.phase=1;
        if(nt_start(&tools.call,command,object_query_path,monotonic_ms(),10000)<0)cell_finish(tools.call.error);
        return;
    }
    int result=nt_step(&tools.call,monotonic_ms());if(!result)return;
    if(result<0){cell_finish(tools.call.error);return;}
    cell_report r=cell_parse_reports(tools.call.reports,tools.call.reports_used);
    if(cell.action==CELL_TARE){cell_finish(r.tare?NULL:"Native tool did not confirm tare");return;}
    if(cell.action==CELL_ZERO_ACTION){
        /* This firmware's guided TARE report omits the double argument required
         * by its printf format. Its displayed percent/count are not usable as
         * a numeric baseline. The successful owned native TARE step plus the
         * subsequent valid measurement/prompt establish readiness. Only the
         * correctly supplied ACCEPT report and native pending map can establish
         * the reference tare; require their exact agreement before saving. */
        cell.tare=0;cell.counts=0;
    }else if(cell.action==CELL_MEASURE){
        if(!r.measurement||!r.accept_prompt){cell_finish("Native measurement was not accepted; acceptance remains disabled");return;}
        cell.counts=r.counts_per_gram;
    }else if(cell.action==CELL_ACCEPT){
        if(!r.accepted||fabs(r.counts_per_gram-cell.counts)>0.000011){
            cell_finish("Native acceptance was not confirmed; do not send ACCEPT again");return;
        }
        cell.counts=r.counts_per_gram;cell.tare=r.counts;
    }else if(cell.action==CELL_ABORT&&!r.aborted){cell_finish("Native abort was not confirmed");return;}
    cell.phase=2;
}
static void cell_action(int fd,mqtt_client *m,const char *text){
    cell_load();int action=0,n=0;double grams=0;
    if(!strcmp(text,"cell:tare"))action=CELL_TARE;
    else if(!strcmp(text,"cell:start"))action=CELL_START;
    else if(!strcmp(text,"cell:zero"))action=CELL_ZERO_ACTION;
    else if(!strcmp(text,"cell:accept"))action=CELL_ACCEPT;
    else if(!strcmp(text,"cell:abort"))action=CELL_ABORT;
    else if(!strcmp(text,"cell:save"))action=CELL_SAVE;
    else if(!strcmp(text,"cell:restart"))action=CELL_RESTART;
    else if(!strcmp(text,"cell:discard"))action=CELL_DISCARD;
    else if(sscanf(text,"cell:measure:%lf%n",&grams,&n)==1&&n&&text[n]==0&&cell_grams_valid(grams,cell.safety))action=CELL_MEASURE;
    if(!action){plates_fail(fd,400,"Invalid load-cell calibration action");return;}
    if(action==CELL_ACCEPT&&(!cell_grams_valid(cell.grams,cell.safety)||cell.counts<1||cell.counts>1e9)){
        plates_fail(fd,409,"No verified load-cell measurement is available");return;
    }
    if(!cell_step_allowed(cell.state,action,cell_same_owner(),!tools_ready(m),cell_cold())||cell_other_pending()){
        plates_fail(fd,409,"Load-cell calibration requires a fresh idle printer, cold heaters and the correct session step");return;
    }
    cell.action=action;cell.grams=action==CELL_MEASURE?grams:cell.grams;cell.phase=0;
    tools.mode=TOOL_CELL_GUIDE;tools.phase=0;tools.connection=telemetry.connections;tools.generation++;tools.error="none";
    tools.call.reports[0]=0;tools.call.reports_used=tools.call.reports_count=0;
    tools.busy=1;plates_reply(fd,202,"{\"accepted\":true}");
}
#endif
