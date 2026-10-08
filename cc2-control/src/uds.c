#include "uds.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static const char subscription[] = "{\"id\":11,\"method\":\"objects/subscribe\",\"params\":{\"objects\":{\"extruder\":[\"temperature\",\"target\"],\"heater_bed\":[\"temperature\",\"target\"],\"gcode_move\":[\"speed_factor\",\"extrude_factor\",\"homing_origin\"],\"motion_report\":[\"live_velocity\"],\"print_stats\":[\"filename\",\"info\",\"print_duration\",\"total_duration\",\"filament_used\"],\"virtual_sdcard\":[\"progress\"],\"exclude_object\":[\"excluded_objects\",\"current_object\"],\"fan\":[\"speed\",\"rpm\"],\"fan_generic fan1\":[\"speed\",\"rpm\"],\"controller_fan board_cooling_fan\":[\"speed\",\"rpm\"],\"heater_fan heatbreak_cooling_fan\":[\"speed\",\"rpm\"]},\"response_template\":{\"method\":\"cc2_status\"}}}\003"
 "{\"id\":13,\"method\":\"gcode/subscribe_report\",\"params\":{\"response_template\":{\"method\":\"cc2_report\"}}}\003";
static const char heartbeat[]="{\"id\":12,\"method\":\"info\",\"params\":{}}\003";
static double elapsed(struct timespec a,struct timespec b){return (double)(a.tv_sec-b.tv_sec)+(a.tv_nsec-b.tv_nsec)/1e9;}
static struct timespec now_mono(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t;}
static const char *json_string_end(const char *p,const char *end){
    for(++p;p<end;++p){
        if(*p=='\\'){if(++p>=end)return NULL;}
        else if(*p=='"')return p+1;
    }
    return NULL;
}

static const char *json_container_end(const char *p,const char *end){
    int depth=0;
    while(p<end){
        if(*p=='"'){p=json_string_end(p,end);if(!p)return NULL;continue;}
        if(*p=='{'||*p=='[')depth++;
        else if((*p=='}'||*p==']')&&--depth==0)return p+1;
        p++;
    }
    return NULL;
}

static const char *json_skip_space(const char *p,const char *end){
    while(p<end&&isspace((unsigned char)*p))p++;
    return p;
}

/* Returns the start of the value of a top-level member of the object [obj,end). */
static const char *json_member(const char *obj,const char *end,const char *key){
    size_t key_len=strlen(key);
    const char *p=obj+1;
    while(1){
        p=json_skip_space(p,end);
        if(p<end&&*p==',')p=json_skip_space(p+1,end);
        if(p>=end||*p!='"')return NULL;
        const char *key_end=json_string_end(p,end);if(!key_end)return NULL;
        int match=(size_t)(key_end-p-2)==key_len&&memcmp(p+1,key,key_len)==0;
        p=json_skip_space(key_end,end);
        if(p>=end||*p!=':')return NULL;
        p=json_skip_space(p+1,end);
        if(p>=end)return NULL;
        if(match)return p;
        if(*p=='{'||*p=='[')p=json_container_end(p,end);
        else if(*p=='"')p=json_string_end(p,end);
        else while(p<end&&*p!=','&&*p!='}')p++;
        if(!p)return NULL;
    }
}

static const char *json_member_object(const char *obj,const char *end,const char *key,char open,const char **value_end){
    const char *value=json_member(obj,end,key);
    if(!value||*value!=open)return NULL;
    *value_end=json_container_end(value,end);
    return *value_end?value:NULL;
}

static int numeric(const char *o,const char *e,const char *key,double *out){
 const char *p=json_member(o,e,key);if(!p||(!isdigit((unsigned char)*p)&&*p!='-'))return 0;
 char *q;errno=0;double v=strtod(p,&q);
 if(q==p||q>e||errno||!isfinite(v))return 0;
 q=(char *)json_skip_space(q,e);if(q<e&&*q!=','&&*q!='}')return 0;
 *out=v;return 1;
}
static int filename_read(const char *o,const char *e,char *out,size_t cap){
 const char *p=json_member(o,e,"filename");if(!p||*p!='"')return 0;
 const char *end=json_string_end(p,e);if(!end)return 0;
 size_t n=0;for(p++;p<end-1;p++){
  unsigned char ch=(unsigned char)*p;
  if(ch=='\\'){
   if(++p>=end-1)return 0;
   ch=(unsigned char)*p;
   /* Unsupported escapes cannot accidentally match another active file. */
   if(ch!='"'&&ch!='\\'&&ch!='/')return 0;
  }
  if(ch<32||n+1>=cap)return 0;
  out[n++]=(char)ch;
 }
 out[n]=0;return 1;
}
/* Keeps one exclude_object value as raw JSON, an array or string (open) or null,
 * so /api/exclude-objects can repeat it without querying the printer. A value
 * that does not fit is forgotten, which sends that route back to its query. */
static void exclude_value(const char *o,const char *e,const char *key,char open,char *out,size_t cap,int *have,int older){
 const char *v=json_member(o,e,key),*end=NULL;
 if(!v||(older&&*have))return;
 if(*v==open)end=open=='['?json_container_end(v,e):json_string_end(v,e);
 else if(e-v>=4&&!memcmp(v,"null",4))end=v+4;
 size_t n=end?(size_t)(end-v):0;
 if(!end||n>=cap){*have=0;return;}
 memcpy(out,v,n);out[n]=0;*have=1;
}
static const struct {const char *object,*key;enum uds_field field;double min,max;} fields[]={
 {"extruder","temperature",U_ET,-100,1000},{"extruder","target",U_EG,0,1000},
 {"heater_bed","temperature",U_BT,-100,1000},{"heater_bed","target",U_BG,0,1000},
 {"controller_fan board_cooling_fan","speed",U_CF,0,1},
 {"heater_fan heatbreak_cooling_fan","speed",U_HF,0,1},
 {"fan","speed",U_PF,0,1},{"fan_generic fan1","speed",U_AF,0,1},
 {"controller_fan board_cooling_fan","rpm",U_CRPM,0,100000},
 {"heater_fan heatbreak_cooling_fan","rpm",U_HRPM,0,100000},
 {"fan","rpm",U_PRPM,0,100000},{"fan_generic fan1","rpm",U_ARPM,0,100000},
 {"gcode_move","speed_factor",U_SPEED_FACTOR,0,100},
 {"gcode_move","extrude_factor",U_FLOW_FACTOR,0,100},
 {"motion_report","live_velocity",U_LIVE_SPEED,0,10000},
 {"virtual_sdcard","progress",U_PROGRESS,0,1},
 {"print_stats","print_duration",U_DURATION,0,INT_MAX},
 {"print_stats","total_duration",U_TOTAL_DURATION,0,INT_MAX},
 /* Net extruder travel of the current print in mm; retractions can make it dip. */
 {"print_stats","filament_used",U_FILAMENT_USED,-1e6,1e9}
};
/* Preserve a bounded JSON string without decoding Unicode. Reject invalid escapes
 * and controls before reflecting it in an API response; oversize text becomes null. */
static void report_text(const char *o,const char *end,char *out,size_t cap){
 strcpy(out,"null");
 const char *v=json_member(o,end,"message");
 if(!v||*v!='"')return;
 const char *e=json_string_end(v,end);
 if(!e||(size_t)(e-v)>=cap)return;
 for(const char *p=v+1;p<e-1;p++){
  if((unsigned char)*p<32)return;
  if(*p!='\\')continue;
  if(++p>=e-1)return;
  if(*p=='u'){
   for(int i=0;i<4;i++)if(++p>=e-1||!isxdigit((unsigned char)*p))return;
  }else if(!strchr("\"\\/bfnrt",*p))return;
 }
 memcpy(out,v,(size_t)(e-v));out[e-v]=0;
}
static int vendor_report(uds_client *c,const char *root,const char *end){
 const char *e;const char *o=json_member_object(root,end,"report",'{',&e);
 double code,level;
 if(!o||!numeric(o,e,"error_code",&code)||code<0||code>INT_MAX||floor(code)!=code||
    !numeric(o,e,"error_level",&level)||level<0||level>3||floor(level)!=level)return 0;
 /* Ordinary successful output must not overwrite the last warning/error.
  * RESUME (3) is a new event, not a guaranteed global fault clearance. */
 if(code==0&&level==0)return 1;
 char text[sizeof(c->report_message)];report_text(o,e,text,sizeof(text));
 if(c->report_sequence&&c->report_code==(int)code&&c->report_level==(int)level&&
    !strcmp(c->report_message,text))return 1;
 c->report_code=(int)code;c->report_level=(int)level;
 c->report_sequence++;c->report_received=now_mono();
 clock_gettime(CLOCK_REALTIME,&c->report_identity);
 memcpy(c->report_message,text,sizeof(text));
 return 1;
}
int uds_message(uds_client *c,const char *json,size_t length){
 const char *end=json+length,*root=json_skip_space(json,end),*ce,*se;
 c->parse_error="invalid_json";
 if(root>=end||*root!='{'||json_container_end(root,end)!=end)return 0;
 if(vendor_report(c,root,end))return 1;
 double id=0;int initial=numeric(root,end,"id",&id)&&id==11;
 if(initial&&json_member(root,end,"error")){c->parse_error="subscription_error";return 0;}
 const char *container=json_member_object(root,end,initial?"result":"params",'{',&ce);
 if(!initial){
  const char *m=json_member(root,end,"method");
  if(!m||end-m<12||memcmp(m,"\"cc2_status\"",12)){
   if(id==12){
    if(json_member(root,end,"error")){c->parse_error="heartbeat_error";return 0;}
    if(json_member_object(root,end,"result",'{',&ce)){c->last_rx=now_mono();return 1;}
    c->parse_error="invalid_heartbeat";return 0;
   }
   /* Vendor reports and unrelated replies may share this socket. Ignore them
    * without refreshing telemetry or closing an otherwise healthy stream. */
   c->ignored_messages++;return 1;
  }
 }
 c->parse_error="missing_container";
 if(!container)return 0;
 const char *status=json_member_object(container,ce,"status",'{',&se);double event;
 c->parse_error="missing_status_or_eventtime";
 if(!status||!numeric(container,ce,"eventtime",&event)||event<0)return 0;
 int older=c->messages&&event<c->eventtime;
 if(older&&!initial){c->parse_error="out_of_order_event";return 0;}
 /* Initial snapshots can follow an earlier notification with the same time. */
 const char *pe;const char *ps=json_member_object(status,se,"print_stats",'{',&pe);
 if(ps&&(!older||!c->have_filename)&&json_member(ps,pe,"filename")){
  char filename[sizeof(c->filename)];
  int valid=filename_read(ps,pe,filename,sizeof(filename));
  if(!valid||(c->have_filename&&strcmp(filename,c->filename))){
   c->present&=~((UINT32_C(1)<<U_PROGRESS)|(UINT32_C(1)<<U_LAYER)|
       (UINT32_C(1)<<U_DURATION)|(UINT32_C(1)<<U_TOTAL_DURATION)|(UINT32_C(1)<<U_FILAMENT_USED));
  }
  c->have_filename=valid;
  if(valid)strcpy(c->filename,filename);
 }
 for(size_t i=0;i<sizeof(fields)/sizeof(fields[0]);i++){
  const char *oe;const char *o=json_member_object(status,se,fields[i].object,'{',&oe);double v;
  if((!older||!(c->present&(UINT32_C(1)<<fields[i].field)))&&o&&numeric(o,oe,fields[i].key,&v)&&v>=fields[i].min&&v<=fields[i].max){
   c->values[fields[i].field]=v;c->present|=UINT32_C(1)<<fields[i].field;
  }
 }
 const char *ge;const char *g=json_member_object(status,se,"gcode_move",'{',&ge);
 if(g&&(!older||!(c->present&(UINT32_C(1)<<U_Z_OFFSET)))){
  const char *p=json_member(g,ge,"homing_origin");
  if(p){
   c->present&=~(UINT32_C(1)<<U_Z_OFFSET);
   if(*p=='['){
    double z=0;int valid=1;p++;
    for(int axis=0;axis<3;axis++){
     p=json_skip_space(p,ge);char *q;errno=0;z=strtod(p,&q);
     if(q==p||q>=ge||errno||!isfinite(z)){valid=0;break;}
     p=json_skip_space(q,ge);
     if(axis<2){if(p>=ge||*p!=','){valid=0;break;}p++;}
     else if(p>=ge||(*p!=','&&*p!=']'))valid=0;
    }
    if(valid&&*p==','){
     p=json_skip_space(p+1,ge);char *q;errno=0;double e=strtod(p,&q);
     if(q==p||q>=ge||errno||!isfinite(e))valid=0;
     else{p=json_skip_space(q,ge);if(p>=ge||*p!=']')valid=0;}
    }
    if(valid&&fabs(z)<=10){c->values[U_Z_OFFSET]=z;c->present|=UINT32_C(1)<<U_Z_OFFSET;}
   }
  }
 }
 if(ps){
  const char *ie;const char *info=json_member_object(ps,pe,"info",'{',&ie);double layer;
  if((!older||!(c->present&(UINT32_C(1)<<U_LAYER)))&&info&&numeric(info,ie,"current_layer",&layer)&&layer>=0&&layer<=INT_MAX&&floor(layer)==layer){
   c->values[U_LAYER]=layer;c->present|=UINT32_C(1)<<U_LAYER;
  }
 }
 const char *xe;const char *x=json_member_object(status,se,"exclude_object",'{',&xe);
 if(x){
  exclude_value(x,xe,"excluded_objects",'[',c->excluded_objects,sizeof(c->excluded_objects),&c->have_excluded_objects,older);
  exclude_value(x,xe,"current_object",'"',c->current_object,sizeof(c->current_object),&c->have_current_object,older);
 }
 if(!older)c->eventtime=event;
 c->messages++;c->last_rx=now_mono();if(initial)c->ready=1;
 return 1;
}
void uds_init(uds_client *c){memset(c,0,sizeof(*c));c->fd=-1;}
void uds_close(uds_client *c){
 if(c->fd>=0)close(c->fd);
 c->fd=-1;c->ready=0;c->present=0;c->used=c->sent=0;c->have_filename=0;
 c->have_excluded_objects=c->have_current_object=0;
}
static void uds_disconnect(uds_client *c,const char *reason,int error){
 c->disconnects++;c->last_disconnect=reason;c->last_errno=error;uds_close(c);
}
int uds_fresh(const uds_client *c){return c->fd>=0&&c->ready&&elapsed(now_mono(),c->last_rx)<=5;}
int uds_value(const uds_client *c,enum uds_field field,double *out){
 if(!uds_fresh(c)||field<0||field>=U_FIELDS||!(c->present&(UINT32_C(1)<<field)))return 0;
 *out=c->values[field];return 1;
}
void uds_tick(uds_client *c,const char *path){
 struct timespec now=now_mono();
 if(c->fd>=0&&elapsed(now,c->last_rx)>5)uds_disconnect(c,"receive_timeout",0);
 if(c->fd<0){
  if(c->retry.tv_sec&&elapsed(now,c->retry)<5)return;
  c->retry=now;
  struct sockaddr_un a;memset(&a,0,sizeof(a));a.sun_family=AF_UNIX;
  if(strlen(path)>=sizeof(a.sun_path))return;
  strcpy(a.sun_path,path);
  int fd=socket(AF_UNIX,SOCK_STREAM,0);if(fd<0)return;
  if(fcntl(fd,F_SETFL,O_NONBLOCK)<0||connect(fd,(struct sockaddr *)&a,sizeof(a))<0){close(fd);return;}
  c->fd=fd;c->last_rx=now;c->last_ping=now;c->messages=0;c->eventtime=0;c->connections++;
 }
 if(c->sent<sizeof(subscription)-1){
  ssize_t n=send(c->fd,subscription+c->sent,sizeof(subscription)-1-c->sent,MSG_NOSIGNAL);
  if(n>0)c->sent+=(size_t)n;
  else if(n==0||(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR))uds_disconnect(c,"subscribe_send",n<0?errno:0);
  return;
 }
 /* elegoo_printer keeps every request received on this socket in memory (about
  * 1.5 KB each), so a fixed two-second heartbeat exhausted the printer's RAM in
  * about 11 hours. Sensor noise keeps a healthy stream at about two notifications
  * per second even when idle, so only a stream silent for three seconds is probed.
  * The read-only info reply still distinguishes an unchanged cache from a dead
  * peer before the five-second receive timeout. */
 if(elapsed(now,c->last_rx)>=3&&elapsed(now,c->last_ping)>=2){
  ssize_t n=send(c->fd,heartbeat,sizeof(heartbeat)-1,MSG_NOSIGNAL);
  if(n!=(ssize_t)(sizeof(heartbeat)-1)){uds_disconnect(c,"heartbeat_send",n<0?errno:0);return;}
  c->last_ping=now;
 }
}
void uds_process(uds_client *c){
 /* Bound each turn so telemetry cannot starve HTTP/MQTT processing. */
 char buffer[4096];
 for(int turn=0;turn<4&&c->fd>=0;turn++){
  ssize_t n=recv(c->fd,buffer,sizeof(buffer),0);
  if(n<0){if(errno==EINTR)continue;if(errno==EAGAIN||errno==EWOULDBLOCK)return;uds_disconnect(c,"receive_error",errno);return;}
  if(!n){uds_disconnect(c,"peer_closed",0);return;}
  for(ssize_t i=0;i<n;i++){
   if((unsigned char)buffer[i]==3){
    c->input[c->used]=0;
    if(!uds_message(c,c->input,c->used)){uds_disconnect(c,c->parse_error,0);return;}
    c->used=0;
   }else{
    if(c->used+1>=sizeof(c->input)){uds_disconnect(c,"frame_too_large",0);return;}
    c->input[c->used++]=buffer[i];
   }
  }
 }
}
int uds_job_matches(const uds_client *c,const char *filename){
 const char *file=c->filename;
 if(!strncmp(file,"local/",6))file+=6;
 return c->have_filename&&*file&&filename&&!strcmp(file,filename);
}
void uds_overlay(const uds_client *c,mqtt_client *v){
 double n;
#define SET(field,value,flag) if(uds_value(c,field,&n)){v->value=n;v->flag=1;}
 SET(U_ET,extruder_temp,have_extruder_temp) SET(U_EG,extruder_target,have_extruder_target)
 SET(U_BT,bed_temp,have_bed_temp) SET(U_BG,bed_target,have_bed_target)
 /* Preserve the legacy 0..255 fan scale consumed by existing UI code. */
#define FAN(field,value,flag) if(uds_value(c,field,&n)){v->value=n*255;v->flag=1;}
 FAN(U_CF,controller_fan,have_controller_fan) FAN(U_HF,heater_fan,have_heater_fan)
 FAN(U_PF,part_fan,have_part_fan)
 /* fan1 physical identity is not yet verified; expose it only in /api/uds. */
#undef FAN
#undef SET
 /* MQTT retains job identity/state and safety authority in this first stage.
  * Overlay job counters only when both sources agree on the active file. */
 if(uds_job_matches(c,v->filename)){
  if(uds_value(c,U_LAYER,&n))v->current_layer=(int)n;
  if(uds_value(c,U_PROGRESS,&n))v->progress=(int)(n*100);
  if(uds_value(c,U_DURATION,&n))v->print_duration=(long)n;
 }
}
/* Writes the subscribed exclude_object state as an objects/query reply, so a
 * dashboard polling it sends no request to the printer. Returns its length, or
 * -1 while the stream is stale or has not reported both values. */
int uds_exclude_status(const uds_client *c,char *out,size_t cap){
 if(!uds_fresh(c)||!c->have_excluded_objects||!c->have_current_object)return -1;
 int n=snprintf(out,cap,"{\"result\":{\"status\":{\"exclude_object\":{\"excluded_objects\":%s,\"current_object\":%s}}}}",
  c->excluded_objects,c->current_object);
 return n>0&&(size_t)n<cap?n:-1;
}
