#ifndef CC2_NETWORK_STATUS_CORE_H
#define CC2_NETWORK_STATUS_CORE_H
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

/* All input is length-delimited. Duplicate fields and truncated replies are
 * unknown, never an excuse to guess a connected state. No credential keys. */
static int ns_field(const char *text, size_t length, const char *key,
                    char *out, size_t capacity) {
    size_t start=0, keylen=strlen(key); int found=0;
    if (!capacity) return 0;
    out[0]=0;
    if (memchr(text,0,length)) return 0;
    while(start<length) {
        const char *end=memchr(text+start,'\n',length-start);
        if(!end) { out[0]=0; return 0; }
        size_t n=(size_t)(end-text)-start;
        if(n>keylen && !memcmp(text+start,key,keylen) && text[start+keylen]=='=') {
            size_t v=n-keylen-1;
            if(found || v>=capacity) { out[0]=0; return 0; }
            memcpy(out,text+start+keylen+1,v);out[v]=0;found=1;
        }
        start=(size_t)(end-text)+1;
    }
    return found;
}
static int ns_number(const char *value,int low,int high,int *out) {
    char *end;long n;
    if(!*value || (*value!='-' && (*value<'0'||*value>'9')))return 0;
    errno=0;n=strtol(value,&end,10);
    if(errno || *end || n<low || n>high)return 0;
    *out=(int)n;return 1;
}
static int ns_ipv4(const char *text) {
    const char *p=text;
    for(int i=0;i<4;i++) {
        int value=0,count=0;
        while(*p>='0'&&*p<='9') {
            value=value*10+(*p++-'0');if(++count>3 || value>255)return 0;
        }
        if(!count || (i<3 ? *p++!='.' : *p!=0))return 0;
    }
    return 1;
}
/* WPA encodes non-ASCII SSID octets as printable escapes. Keep that display
 * representation, escape JSON metacharacters, never interpolate HTML. */
static void ns_json_string(FILE *out,const char *s) {
    fputc('"',out);
    for(const unsigned char *p=(const unsigned char*)s;*p;p++) {
        if(*p=='"'||*p=='\\')fprintf(out,"\\%c",*p);
        else if(*p<32 || *p>=127)fprintf(out,"\\u%04x",(unsigned)*p);
        else fputc(*p,out);
    }
    fputc('"',out);
}
#endif
