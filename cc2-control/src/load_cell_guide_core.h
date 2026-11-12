#ifndef CC2_LOAD_CELL_GUIDE_CORE_H
#define CC2_LOAD_CELL_GUIDE_CORE_H
#include "canvas_json.h"
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {CELL_OFF,CELL_ZERO,CELL_LOAD,CELL_REVIEW,CELL_APPLIED,CELL_SAVED,CELL_UNKNOWN};
enum {CELL_TARE=1,CELL_START,CELL_ZERO_ACTION,CELL_MEASURE,CELL_ACCEPT,CELL_ABORT,CELL_SAVE,CELL_RESTART,CELL_DISCARD};
typedef struct {
    int understood,registered,foreign,pending,other_pending,have_parameters;
    double counts_per_gram,tare_counts,safety_limit,configured_counts,configured_tare;
} cell_snapshot;
typedef struct {int tare,measurement,accept_prompt,accepted,aborted;double percent,counts,counts_per_gram,capacity;} cell_report;

static inline int cell_number(const char *obj,const char *end,const char *key,double *value){
    const char *p=ccv_json_member(obj,end,key);if(!p)return 0;
    const char *start=p,*stop=p;
    if(*p=='"'){
        const char *se=ccv_json_string_end(p,end);if(!se)return 0;
        start=p+1;stop=se-1;
    }else{
        while(stop<end&&*stop!=','&&*stop!='}'&&!isspace((unsigned char)*stop))stop++;
    }
    char text[64];size_t n=(size_t)(stop-start);if(!n||n>=sizeof(text))return 0;
    memcpy(text,start,n);text[n]=0;
    char *tail;errno=0;double v=strtod(text,&tail);
    if(errno||tail==text||*tail||!isfinite(v))return 0;
    *value=v;return 1;
}
static inline int cell_string_is(const char *obj,const char *end,const char *key,const char *expected){
    const char *p=ccv_json_member(obj,end,key);size_t n=strlen(expected);
    return p&&end-p>=(ptrdiff_t)(n+2)&&p[0]=='"'&&!memcmp(p+1,expected,n)&&p[n+1]=='"';
}
static inline int cell_object_empty(const char *p,const char *end){
    return p&&p<end&&*p=='{'&&ccv_json_skip_space(p+1,end)==end-1&&end[-1]=='}';
}
/* The registry check prevents generic TARE/ACCEPT/ABORT commands from being
 * sent to a probe calibration or another native interactive tool. */
static inline cell_snapshot cell_parse_snapshot(const char *json,size_t length){
    cell_snapshot s={0};const char *end=json+length,*root=ccv_json_skip_space(json,end),*re=NULL,*se=NULL,*ge=NULL,*ce=NULL,*pe=NULL,*le=NULL;
    if(root==end||*root!='{'||!(re=ccv_json_container_end(root,end))||ccv_json_skip_space(re,end)!=end||ccv_json_member(root,re,"error"))return s;
    const char *result=ccv_json_member_object(root,re,"result",'{',&re);
    const char *status=result?ccv_json_member_object(result,re,"status",'{',&se):NULL;
    const char *gcode=status?ccv_json_member_object(status,se,"gcode",'{',&ge):NULL;
    const char *commands=gcode?ccv_json_member_object(gcode,ge,"commands",'{',&ge):NULL;
    const char *config=status?ccv_json_member_object(status,se,"configfile",'{',&ce):NULL;
    const char *pending=config?ccv_json_member(config,ce,"save_config_pending"):NULL;
    if(!commands||!config||!pending)return s;
    if(ce-pending>=5&&!memcmp(pending,"true",4)&&(pending[4]==','||pending[4]=='}'||isspace((unsigned char)pending[4])))s.pending=1;
    else if(ce-pending<6||memcmp(pending,"false",5)||(pending[5]!=','&&pending[5]!='}'&&!isspace((unsigned char)pending[5])))return s;
    const char *names[]={"ABORT","ACCEPT","TARE","CALIBRATE"};
    const char *help[]={"Abort load cell calibration tool","Accept calibration results and apply to load cell","Tare the load cell","Enter the load cell value in grams"};
    int present=0,matching=0;
    for(int i=0;i<4;i++){
        const char *ee,*entry=ccv_json_member(commands,ge,names[i]);
        if(!entry)continue;
        present++;
        if(*entry=='{'&&(ee=ccv_json_container_end(entry,ge))&&cell_string_is(entry,ee,"help",help[i]))matching++;
    }
    s.registered=present==4&&matching==4;s.foreign=present&&!s.registered;
    const char *items=ccv_json_member_object(config,ce,"save_config_pending_items",'{',&pe);
    if(!items){
        const char *v=ccv_json_member(config,ce,"save_config_pending_items");
        if(s.pending||!v||ce-v<4||memcmp(v,"null",4))return s;
    }else if(!cell_object_empty(items,pe)){
        const char *section=ccv_json_member_object(items,pe,"load_cell_probe",'{',&le);
        if(!section){s.other_pending=1;}
        else{
            /* Exactly this section and its two native calibration keys may be saved. */
            const char *p=ccv_json_skip_space(items+1,pe);int sections=0;
            while(p<pe-1){
                if(*p==',')p=ccv_json_skip_space(p+1,pe);
                const char *key_end=*p=='"'?ccv_json_string_end(p,pe):NULL;if(!key_end)return s;
                if((size_t)(key_end-p-2)!=15||memcmp(p+1,"load_cell_probe",15))s.other_pending=1;
                p=ccv_json_skip_space(key_end,pe);if(p==pe||*p!=':')return s;
                p=ccv_json_skip_space(p+1,pe);if(*p!='{')return s;
                p=ccv_json_container_end(p,pe);if(!p)return s;
                p=ccv_json_skip_space(p,pe);sections++;
            }
            int keys=0;p=ccv_json_skip_space(section+1,le);
            while(p<le-1){
                if(*p==',')p=ccv_json_skip_space(p+1,le);
                const char *ke=*p=='"'?ccv_json_string_end(p,le):NULL;if(!ke)return s;
                size_t n=(size_t)(ke-p-2);
                if(!((n==15&&!memcmp(p+1,"counts_per_gram",15))||(n==21&&!memcmp(p+1,"reference_tare_counts",21))))s.other_pending=1;
                p=ccv_json_skip_space(ke,le);if(p==le||*p!=':')return s;
                p=ccv_json_skip_space(p+1,le);
                if(*p=='"')p=ccv_json_string_end(p,le);else while(p<le&&*p!=','&&*p!='}')p++;
                if(!p)return s;
                p=ccv_json_skip_space(p,le);keys++;
            }
            s.have_parameters=sections==1&&keys==2&&cell_number(section,le,"counts_per_gram",&s.counts_per_gram)&&
                cell_number(section,le,"reference_tare_counts",&s.tare_counts)&&s.counts_per_gram>=1&&s.counts_per_gram<=1e9&&
                s.tare_counts>=INT32_MIN&&s.tare_counts<=INT32_MAX;
            if(!s.have_parameters)s.other_pending=1;
        }
        if(!s.pending)s.other_pending=1;
    }else if(s.pending)s.other_pending=1;
    const char *settings=ccv_json_member_object(config,ce,"settings",'{',&pe);
    const char *load=settings?ccv_json_member_object(settings,pe,"load_cell_probe",'{',&le):NULL;
    if(load){
        if(!cell_number(load,le,"safety_limit",&s.safety_limit)||!cell_number(load,le,"counts_per_gram",&s.configured_counts)||
           !cell_number(load,le,"reference_tare_counts",&s.configured_tare))return s;
    }
    s.understood=1;return s;
}
static inline double cell_max_grams(double safety_limit){return isfinite(safety_limit)&&safety_limit>=50?fmin(1000,safety_limit):0;}
static inline int cell_grams_valid(double grams,double safety_limit){return isfinite(grams)&&grams>=50&&grams<=cell_max_grams(safety_limit);}
static inline int cell_step_allowed(int state,int action,int owned,int fresh_idle,int cold){
    if(!fresh_idle)return 0;
    if(action==CELL_ABORT)return owned&&(state==CELL_ZERO||state==CELL_LOAD||state==CELL_REVIEW||state==CELL_UNKNOWN);
    if(!cold)return 0;
    switch(action){
      case CELL_TARE:case CELL_START:return state==CELL_OFF;
      case CELL_ZERO_ACTION:return owned&&(state==CELL_ZERO||state==CELL_LOAD||state==CELL_REVIEW);
      case CELL_MEASURE:return owned&&(state==CELL_LOAD||state==CELL_REVIEW);
      case CELL_ACCEPT:return owned&&state==CELL_REVIEW;
      case CELL_SAVE:return owned&&state==CELL_APPLIED;
      case CELL_RESTART:return owned&&state==CELL_SAVED;
      case CELL_DISCARD:return owned&&(state==CELL_APPLIED||state==CELL_UNKNOWN);
      default:return 0;
    }
}
/* Recognise the native English protocol reports, while the UI translates its
 * own instructions. Unknown output stays visible but cannot advance a step. */
static inline cell_report cell_parse_reports(const char *reports,size_t length){
    cell_report result={0};const char *p=reports,*end=p+length;
    while(p<end){
        if(*p==',')p++;
        const char *se=p<end&&*p=='"'?ccv_json_string_end(p,end):NULL;if(!se)return (cell_report){0};
        char text[1024];size_t used=0;
        for(const char *q=p+1;q<se-1;q++){
            unsigned char c=(unsigned char)*q;
            if(c=='\\'){
                q++;if(q>=se-1)return (cell_report){0};
                if(*q=='n'||*q=='r'||*q=='t')c=' ';
                else if(*q=='"'||*q=='\\'||*q=='/')c=(unsigned char)*q;
                else {c='?';} /* Non-protocol Unicode/errors are displayed, never parsed as calibration. */
            }
            if(used+1>=sizeof(text))return (cell_report){0};
            text[used++]=(char)c;
        }
        text[used]=0;const char *message=!strncmp(text,"// ",3)?text+3:text;
        double percent=0,counts_per_gram=0,capacity=0;long long counts=0;int n=0;
        if(sscanf(message,"Load cell tare value: %lf%% (%lld)%n",&percent,&counts,&n)==2&&n&&message[n]==0){
            if(result.tare||!isfinite(percent)||fabs(percent)>100||counts<INT32_MIN||counts>INT32_MAX)return (cell_report){0};
            result.tare=1;result.percent=percent;result.counts=(double)counts;
        }else if(sscanf(message,"Calibration value: %lf%% (%lld), Counts/gram: %lf, Total capacity: +/- %lfKg%n",&percent,&counts,&counts_per_gram,&capacity,&n)==4&&n&&message[n]==0){
            if(result.measurement||!isfinite(percent)||fabs(percent)>100||counts<INT32_MIN||counts>INT32_MAX||
               !isfinite(counts_per_gram)||counts_per_gram<1||counts_per_gram>1e9||!isfinite(capacity)||capacity<=0)return (cell_report){0};
            result.measurement=1;result.percent=percent;result.counts=(double)counts;result.counts_per_gram=counts_per_gram;result.capacity=capacity;
        }else if(!strcmp(message,"Accept calibration with the ACCEPT command."))result.accept_prompt=1;
        else if(!strcmp(message,"Load cell calibration aborted"))result.aborted=1;
        else if(sscanf(message,"Load cell calibration settings: counts_per_gram: %lf reference_tare_counts: %lld%n",&counts_per_gram,&counts,&n)==2&&n&&message[n]==0){
            if(result.accepted||!isfinite(counts_per_gram)||counts_per_gram<1||counts_per_gram>1e9||counts<INT32_MIN||counts>INT32_MAX)return (cell_report){0};
            result.accepted=1;result.counts_per_gram=counts_per_gram;result.counts=(double)counts;
        }
        p=se;
    }
    return result;
}
#endif
