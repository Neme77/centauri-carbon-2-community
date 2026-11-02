#ifndef CC2_ROTATION_CORE_H
#define CC2_ROTATION_CORE_H
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RD_CONFIG_MAX (256u*1024u)
typedef struct {int present,section;double value;size_t first,last,insert;} rd_entry;
static inline int rd_positive(double value){return isfinite(value)&&value>=0.01&&value<=1000;}
/* No direction changes: the native setter reverses the motor for negative values. */
static inline int rd_calculate(double current,double requested,double measured,double *next){
    if(!rd_positive(current)||!isfinite(requested)||!isfinite(measured)||requested<=0||measured<=0)return 0;
    double value=current*(measured/requested);
    if(!rd_positive(value))return 0;
    *next=value;return 1;
}
static inline const char *rd_space(const char *p,const char *end){while(p<end&&(*p==' '||*p=='\t'||*p=='\r'))p++;return p;}
static inline int rd_decimal(const char *p,const char *end,double *value){
    char number[64];size_t n=(size_t)(end-p);
    if(!n||n>=sizeof(number))return 0;
    memcpy(number,p,n);number[n]=0;
    for(size_t i=0;i<n;i++)if(!isdigit((unsigned char)number[i])&&number[i]!='.')return 0;
    char *tail;errno=0;double v=strtod(number,&tail);
    if(errno||tail==number||*tail||!rd_positive(v))return 0;
    *value=v;return 1;
}
/* 1 = understood, including an absent override; 0 = ambiguous/unsupported input.
 * Native autosave comments are configuration, ordinary comments are not. */
static inline int rd_parse(const char *text,size_t length,int autosave,rd_entry *out){
    memset(out,0,sizeof(*out));out->insert=length;
    if(!text||length>RD_CONFIG_MAX||memchr(text,0,length))return 0;
    int section=0,marker=0;
    for(size_t pos=0;pos<length;){
        const char *line=text+pos,*end=memchr(line,'\n',length-pos);
        size_t next=end?(size_t)(end-text)+1:length;if(!end)end=text+length;
        const char *p=rd_space(line,end);
        if(autosave){
            const char mark[]="#*# <---------------------- SAVE_CONFIG ---------------------->";
            if((size_t)(end-p)>=sizeof(mark)-1&&!memcmp(p,mark,sizeof(mark)-1)&&rd_space(p+sizeof(mark)-1,end)==end){
                if(marker++)return 0;
                pos=next;continue;
            }
            if(end-p>=3&&!memcmp(p,"#*#",3)){
                if(!marker)return 0;
                p=rd_space(p+3,end);
            }else{
                /* Raw settings in autosave have different precedence; do not edit them. */
                if(p<end&&*p!='#'&&*p!=';')return 0;
                pos=next;continue;
            }
        }
        if(p<end&&*p=='['){
            if(section)out->insert=pos;
            section=0;
            const char *close=memchr(p,']',(size_t)(end-p));if(!close)return 0;
            const char *tail=rd_space(close+1,end);if(tail<end&&*tail!='#'&&*tail!=';')return 0;
            if(close-p==9&&!memcmp(p,"[extruder]",10)){
                if(out->section)return 0;
                out->section=section=1;out->insert=length;
            }
            if(!autosave&&close-p>=8&&!memcmp(p,"[include",8))return 0;
        }else if(section&&end-p>=17&&!memcmp(p,"rotation_distance",17)){
            p=rd_space(p+17,end);
            if(p==end||(*p!='='&&*p!=':'))return 0;
            p=rd_space(p+1,end);const char *value_end=p;
            while(value_end<end&&(isdigit((unsigned char)*value_end)||*value_end=='.'))value_end++;
            const char *tail=rd_space(value_end,end);
            if((tail<end&&*tail!='#'&&*tail!=';')||out->present||!rd_decimal(p,value_end,&out->value))return 0;
            out->present=1;out->first=(size_t)(p-text);out->last=(size_t)(value_end-text);
        }
        pos=next;
    }
    return !autosave||marker==1;
}
/* Preserve all other bytes, including meshes and unrelated extruder settings. */
static inline char *rd_rewrite(const char *text,size_t length,double value,size_t *new_length){
    rd_entry entry;if(!rd_positive(value)||!rd_parse(text,length,1,&entry))return NULL;
    char edit[128];size_t first,last;int n;
    if(entry.present){first=entry.first;last=entry.last;n=snprintf(edit,sizeof(edit),"%.10f",value);}
    else{
        first=last=entry.insert;
        n=snprintf(edit,sizeof(edit),"%s%s#*# rotation_distance = %.10f\n",
            first&&text[first-1]!='\n'?"\n":"",entry.section?"":"#*# [extruder]\n",value);
    }
    if(n<0||(size_t)n>=sizeof(edit))return NULL;
    size_t size=length-(last-first)+(size_t)n;if(size>RD_CONFIG_MAX)return NULL;
    char *copy=malloc(size+1);if(!copy)return NULL;
    memcpy(copy,text,first);memcpy(copy+first,edit,(size_t)n);memcpy(copy+first+n,text+last,length-last);
    copy[size]=0;*new_length=size;return copy;
}
#endif
