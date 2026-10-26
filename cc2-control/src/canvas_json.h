#ifndef CC2_CANVAS_JSON_H
#define CC2_CANVAS_JSON_H
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
static inline const char *ccv_json_string_end(const char *p,const char *end){
    for(++p;p<end;++p){
        if(*p=='\\'){if(++p>=end)return NULL;}
        else if(*p=='"')return p+1;
    }
    return NULL;
}

static inline const char *ccv_json_container_end(const char *p,const char *end){
    int depth=0;
    while(p<end){
        if(*p=='"'){p=ccv_json_string_end(p,end);if(!p)return NULL;continue;}
        if(*p=='{'||*p=='[')depth++;
        else if((*p=='}'||*p==']')&&--depth==0)return p+1;
        p++;
    }
    return NULL;
}

static inline const char *ccv_json_skip_space(const char *p,const char *end){
    while(p<end&&isspace((unsigned char)*p))p++;
    return p;
}

/* Returns the start of the value of a top-level member of the object [obj,end). */
static inline const char *ccv_json_member(const char *obj,const char *end,const char *key){
    size_t key_len=strlen(key);
    const char *p=obj+1;
    while(1){
        p=ccv_json_skip_space(p,end);
        if(p<end&&*p==',')p=ccv_json_skip_space(p+1,end);
        if(p>=end||*p!='"')return NULL;
        const char *key_end=ccv_json_string_end(p,end);if(!key_end)return NULL;
        int match=(size_t)(key_end-p-2)==key_len&&memcmp(p+1,key,key_len)==0;
        p=ccv_json_skip_space(key_end,end);
        if(p>=end||*p!=':')return NULL;
        p=ccv_json_skip_space(p+1,end);
        if(p>=end)return NULL;
        if(match)return p;
        if(*p=='{'||*p=='[')p=ccv_json_container_end(p,end);
        else if(*p=='"')p=ccv_json_string_end(p,end);
        else while(p<end&&*p!=','&&*p!='}')p++;
        if(!p)return NULL;
    }
}

static inline const char *ccv_json_member_object(const char *obj,const char *end,const char *key,char open,const char **value_end){
    const char *value=ccv_json_member(obj,end,key);
    if(!value||*value!=open)return NULL;
    *value_end=ccv_json_container_end(value,end);
    return *value_end?value:NULL;
}

static inline int ccv_numeric(const char *o,const char *e,const char *key,double *out){
 const char *p=ccv_json_member(o,e,key);if(!p||(!isdigit((unsigned char)*p)&&*p!='-'))return 0;
 char *q;errno=0;double v=strtod(p,&q);
 if(q==p||q>e||errno||!isfinite(v))return 0;
 q=(char *)ccv_json_skip_space(q,e);if(q<e&&*q!=','&&*q!='}')return 0;
 *out=v;return 1;
}

#endif
