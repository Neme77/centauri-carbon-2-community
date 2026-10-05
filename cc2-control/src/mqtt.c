#include "mqtt.h"

#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define DISCOVERY_LIMIT 9U
#define DISCOVERY_SETTLE_SECONDS 15

static int send_all(int fd, const void *data, size_t size) {
    const unsigned char *p = data;
    while (size) {
        ssize_t n = send(fd, p, size, 0);
        if (n < 0) { if (errno == EINTR) continue; return -1; }
        p += n; size -= (size_t)n;
    }
    return 0;
}

static size_t put_string(unsigned char *out, size_t cap, const char *value) {
    size_t n = strlen(value);
    if (n > 65535 || cap < n + 2) return 0;
    out[0] = (unsigned char)(n >> 8); out[1] = (unsigned char)n;
    memcpy(out + 2, value, n);
    return n + 2;
}

static size_t put_remaining(unsigned char *out, size_t value) {
    size_t n = 0;
    do {
        unsigned char byte = (unsigned char)(value % 128);
        value /= 128;
        if (value) byte |= 0x80;
        out[n++] = byte;
    } while (value && n < 4);
    return n;
}

static int send_connect(mqtt_client *c) {
    unsigned char body[512], packet[520], rem[4];
    size_t n = 0, x;
    body[n++]=0; body[n++]=4; memcpy(body+n,"MQTT",4); n+=4;
    body[n++]=4; body[n++]=0xC2; body[n++]=0; body[n++]=30;
    char client_id[64];
    snprintf(client_id,sizeof(client_id),"cc2-control-%ld",(long)getpid());
    x=put_string(body+n,sizeof(body)-n,client_id); if(!x)return -1; n+=x;
    x=put_string(body+n,sizeof(body)-n,c->username); if(!x)return -1; n+=x;
    x=put_string(body+n,sizeof(body)-n,c->password); if(!x)return -1; n+=x;
    size_t rn=put_remaining(rem,n), p=0;
    packet[p++]=0x10; memcpy(packet+p,rem,rn); p+=rn;
    memcpy(packet+p,body,n); p+=n;
    return send_all(c->fd,packet,p);
}

static int send_subscribe(mqtt_client *c) {
    unsigned char body[768], packet[780], rem[4];
    size_t n=0,x,p=0;
    body[n++]=0; body[n++]=1;
    x=put_string(body+n,sizeof(body)-n,"elegoo/#"); if(!x)return -1; n+=x;
    body[n++]=0;
    size_t rn=put_remaining(rem,n);
    packet[p++]=0x82; memcpy(packet+p,rem,rn); p+=rn;
    memcpy(packet+p,body,n); p+=n;
    return send_all(c->fd,packet,p);
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *end=s+strlen(s);
    while(end>s && isspace((unsigned char)end[-1])) *--end='\0';
    return s;
}

void mqtt_init(mqtt_client *c) {
    memset(c,0,sizeof(*c)); c->fd=-1;
    c->canvas_active_tray_id=-1;
    c->history_error=-1;
    snprintf(c->client_id,sizeof(c->client_id),"cc2-control-%ld",(long)getpid());
    strcpy(c->username,"elegoo");
    strcpy(c->topic,"elegoo/+/api_status");
}

int mqtt_load_config(mqtt_client *c, const char *path) {
    FILE *f=fopen(path,"r");
    if(!f)return -1;
    snprintf(c->config_path,sizeof(c->config_path),"%s",path);
    char line[320];
    while(fgets(line,sizeof(line),f)) {
        char *s=trim(line); if(!*s||*s=='#')continue;
        char *eq=strchr(s,'='); if(!eq)continue; *eq++='\0';
        char *key=trim(s),*value=trim(eq);
        if(!strcmp(key,"mqtt_username")) snprintf(c->username,sizeof(c->username),"%s",value);
        else if(!strcmp(key,"mqtt_password")) snprintf(c->password,sizeof(c->password),"%s",value);
        else if(!strcmp(key,"mqtt_topic")) snprintf(c->topic,sizeof(c->topic),"%s",value);
        else if(!strcmp(key,"mqtt_serial")){snprintf(c->serial,sizeof(c->serial),"%s",value);c->serial_persisted=c->serial[0]!=0;}
    }
    fclose(f);
    return c->password[0] ? 0 : -1;
}

void mqtt_close(mqtt_client *c) {
    if(c->fd>=0)close(c->fd);
    c->fd=-1; c->connected=0; c->registered=0; c->register_sent=0;
    c->snapshot_sent=0; c->input_len=0;c->discard_remaining=0;
    c->last_registration_request=0; c->last_snapshot_request=0;
    c->registration_attempts=0; c->snapshot_request_attempts=0;
    c->canvas_snapshot_len=0; c->canvas_snapshot[0]='\0';
    c->canvas_discovery_complete=0; c->canvas_request_attempts=0;
    c->last_canvas_request=0;
    c->last_app_ping=0;
    /* Replies to requests sent on this session can no longer arrive. */
    free(c->large); c->large=NULL; c->large_len=0; c->large_need=0;
    c->canvas_refresh_due=0; c->history_refresh_due=0; c->auto_refill_probed=0;
    /* A setting reported on the previous session is no longer current. Keep
     * status deltas sticky within a session, but require readback on reconnect. */
    c->have_auto_refill=0;
    c->history_requested=0; c->timelapse_requested=0; c->timelapse_rendering=0; c->history_delete_requested=0;
}

static int mqtt_publish(mqtt_client *c,const char *topic,const char *payload) {
    unsigned char body[14000],packet[14016],rem[4]; size_t n=0,p=0,x;
    x=put_string(body,sizeof(body),topic); if(!x)return -1; n=x;
    body[n++]=0; body[n++]=2; /* QoS 1 packet identifier */
    size_t payload_len=strlen(payload); if(n+payload_len>sizeof(body))return -1;
    memcpy(body+n,payload,payload_len); n+=payload_len;
    size_t rn=put_remaining(rem,n); packet[p++]=0x32;
    memcpy(packet+p,rem,rn);p+=rn;memcpy(packet+p,body,n);p+=n;
    return send_all(c->fd,packet,p);
}

static int send_registration(mqtt_client *c) {
    char topic[192],payload[256];
    snprintf(topic,sizeof(topic),"elegoo/%s/api_register",c->serial);
    snprintf(payload,sizeof(payload),"{\"request_id\":\"%s\",\"client_id\":\"%s\"}",c->client_id,c->client_id);
    if(mqtt_publish(c,topic,payload)<0)return -1;
    c->register_sent=1;
    c->last_registration_request=time(NULL);
    c->registration_attempts++;
    return 0;
}

static int send_snapshot_request(mqtt_client *c) {
    char topic[256],payload[96];
    snprintf(topic,sizeof(topic),"elegoo/%s/%s/api_request",c->serial,c->client_id);
    snprintf(payload,sizeof(payload),"{\"method\":1002,\"id\":1002}");
    if(mqtt_publish(c,topic,payload)<0)return -1;
    c->snapshot_sent=1;
    c->last_snapshot_request=time(NULL);
    c->snapshot_request_attempts++;
    return 0;
}

int mqtt_request_canvas(mqtt_client *c) {
    char topic[256];
    if(!c||c->fd<0||!c->connected||!c->registered||!c->serial[0])return -1;
    snprintf(topic,sizeof(topic),"elegoo/%s/%s/api_request",c->serial,c->client_id);
    /* create_api() registers lambda #46 under 2005; it calls
     * handle_canvas_get_channel_info(). 2003 is material editing. */
    int result=mqtt_publish(c,topic,"{\"method\":2005,\"id\":2005}");
    if(result==0){c->last_canvas_request=time(NULL);c->canvas_request_attempts++;}
    return result;
}

/* Bound each automatic discovery phase independently. Transport reconnects must
 * not restart an exhausted budget. Explicit Sync is the recovery action. */
int mqtt_sync_canvas(mqtt_client *c) {
    if(!c||c->fd<0||!c->connected||!c->serial[0])return -1;
    c->automatic_registration_attempts=0;
    c->automatic_snapshot_attempts=0;
    c->automatic_canvas_attempts=0;
    c->discovery_ready_at=time(NULL)+DISCOVERY_SETTLE_SECONDS;
    return c->registered ? mqtt_request_canvas(c) : 0;
}

static int discovery_due(unsigned int attempts,time_t last,time_t now) {
    return attempts<DISCOVERY_LIMIT && (!last || now-last>=(attempts<6?10:60));
}

size_t mqtt_discovery_diagnostic(const mqtt_client *c,char *out,size_t capacity) {
    const char *state=!c->connected?"disconnected":c->canvas_discovery_complete&&c->registered?"complete":
        (!c->registered&&c->automatic_registration_attempts>=DISCOVERY_LIMIT)||
        (c->registered&&c->automatic_canvas_attempts>=DISCOVERY_LIMIT)?"exhausted; use Sync":
        time(NULL)<c->discovery_ready_at?"settling":"discovering";
    int n=snprintf(out,capacity,
        "Canvas discovery: %s\nMQTT connections: %lu\nAutomatic registration: %u/%u\n"
        "Automatic snapshot: %u/%u\nAutomatic Canvas: %u/%u\n"
        "Session registration/snapshot/Canvas requests: %u/%u/%u\n",
        state,c->mqtt_connections,c->automatic_registration_attempts,DISCOVERY_LIMIT,
        c->automatic_snapshot_attempts,DISCOVERY_LIMIT,c->automatic_canvas_attempts,DISCOVERY_LIMIT,
        c->registration_attempts,c->snapshot_request_attempts,c->canvas_request_attempts);
    return n<0||!capacity?0:(size_t)n<capacity?(size_t)n:capacity-1;
}

static int mqtt_json_escape(char *out, size_t capacity, const char *value) {
    size_t used = 0;
    for (const unsigned char *cursor = (const unsigned char *)value; *cursor; ++cursor) {
        unsigned char ch = *cursor;
        if (ch < 32) return -1;
        if (ch == '"' || ch == '\\') {
            if (used + 2 >= capacity) return -1;
            out[used++] = '\\';
            out[used++] = (char)ch;
        } else {
            if (used + 1 >= capacity) return -1;
            out[used++] = (char)ch;
        }
    }
    if (used >= capacity) return -1;
    out[used] = '\0';
    return 0;
}

int mqtt_start_print(mqtt_client *c, const char *storage_media, const char *filename,
                     const int *tools, const int *trays, size_t slot_count,
                     char print_layout, int bedlevel_force, int timelapse) {
    char topic[256], escaped_filename[1024], payload[2200];
    if (!c || c->fd < 0 || !c->connected || !c->registered || !c->serial[0]) return -1;
    if (!storage_media || (strcmp(storage_media, "local") != 0 &&
                           strcmp(storage_media, "u-disk") != 0)) return -1;
    if (!filename || mqtt_json_escape(escaped_filename, sizeof(escaped_filename), filename) != 0) return -1;
    if (print_layout != 'A' && print_layout != 'B') return -1;
    snprintf(topic, sizeof(topic), "elegoo/%s/%s/api_request", c->serial, c->client_id);
    if (slot_count > 16 || (slot_count && (!tools || !trays))) return -1;
    int length = snprintf(payload, sizeof(payload),
        "{\"method\":1020,\"id\":1020,\"params\":{"
        "\"storage_media\":\"%s\",\"filename\":\"%s\",\"config\":{"
        "\"delay_video\":%s,\"printer_check\":false,\"print_layout\":\"%c\","
        "\"bedlevel_force\":%s,\"slot_map\":[", storage_media, escaped_filename,
        timelapse ? "true" : "false", print_layout, bedlevel_force ? "true" : "false");
    if (length < 0 || (size_t)length >= sizeof(payload)) return -1;
    size_t used = (size_t)length;
    for (size_t index = 0; index < slot_count; ++index) {
        if (tools[index] < 0 || tools[index] > 15 || trays[index] < 0 || trays[index] > 3)
            return -1;
        length = snprintf(payload + used, sizeof(payload) - used,
                          "%s{\"t\":%d,\"canvas_id\":0,\"tray_id\":%d}",
                          index ? "," : "", tools[index], trays[index]);
        if (length < 0 || (size_t)length >= sizeof(payload) - used) return -1;
        used += (size_t)length;
    }
    length = snprintf(payload + used, sizeof(payload) - used, "]}}}");
    if (length < 0 || (size_t)length >= sizeof(payload) - used) return -1;
    return mqtt_publish(c, topic, payload);
}

/* One JSON-RPC request on our registered api_request topic. The id repeats the
 * method, like the 1002/2005 requests, so the reply names what it answers. */
static int publish_request(mqtt_client *c, int method, const char *params) {
    char topic[256], payload[4096];
    if (!c || c->fd < 0 || !c->connected || !c->registered || !c->serial[0]) return -1;
    int length = snprintf(topic, sizeof(topic), "elegoo/%s/%s/api_request", c->serial, c->client_id);
    if (length < 0 || (size_t)length >= sizeof(topic)) return -1;
    length = params ? snprintf(payload, sizeof(payload), "{\"method\":%d,\"id\":%d,\"params\":%s}", method, method, params)
                    : snprintf(payload, sizeof(payload), "{\"method\":%d,\"id\":%d}", method, method);
    if (length < 0 || (size_t)length >= sizeof(payload)) return -1;
    return mqtt_publish(c, topic, payload);
}

/* Set the timelapse configuration for calibrated G-code starts (vendor 1019).
 * A matching acknowledgement is required before the caller starts the file. */
int mqtt_prepare_timelapse(mqtt_client *c,int enabled) {
    char topic[256],payload[192];
    if(!c||c->fd<0||!c->connected||!c->registered||!c->serial[0])return -1;
    if(++c->print_config_id>1000000000UL)c->print_config_id=1;
    unsigned long id=20000UL+c->print_config_id;
    c->print_config_reply_id=0;c->print_config_error=-1;
    int n=snprintf(topic,sizeof(topic),"elegoo/%s/%s/api_request",c->serial,c->client_id);
    if(n<0||(size_t)n>=sizeof(topic))return -1;
    n=snprintf(payload,sizeof(payload),"{\"method\":1019,\"id\":%lu,\"params\":{\"config\":{\"delay_video\":%s}}}",id,enabled?"true":"false");
    if(n<0||(size_t)n>=sizeof(payload)||mqtt_publish(c,topic,payload))return -1;
    struct timespec ts;clock_gettime(CLOCK_MONOTONIC,&ts);
    long long deadline=(long long)ts.tv_sec*1000+ts.tv_nsec/1000000+1500;
    while(c->fd>=0){
        if(c->print_config_reply_id==id)return c->print_config_error==0?0:-1;
        clock_gettime(CLOCK_MONOTONIC,&ts);
        long long left=deadline-((long long)ts.tv_sec*1000+ts.tv_nsec/1000000);
        if(left<=0)return -1;
        struct pollfd p={c->fd,POLLIN,0};int ready=poll(&p,1,(int)left);
        if(ready<0&&errno==EINTR)continue;
        if(ready<=0||!(p.revents&POLLIN)||mqtt_process(c)<0)return -1;
    }
    return -1;
}

int mqtt_delete_history(mqtt_client *c,const char *params) {
    if(publish_request(c,1038,params))return -1;
    c->history_delete_requested=time(NULL);return 0;
}

/* 2004 is SET_AUTO_REFILL in ELEGOO's elegoo-link CC2 adapter: {"auto_refill":bool}. */
int mqtt_set_auto_refill(mqtt_client *c, int enabled) {
    return publish_request(c, 2004, enabled ? "{\"auto_refill\":true}" : "{\"auto_refill\":false}");
}

/* 1036 returns the whole print task list (no paging on LAN); one request in flight is enough. */
int mqtt_request_history(mqtt_client *c) {
    time_t now = time(NULL);
    if (c && c->history_requested && now - c->history_requested < 5) return 0;
    if (publish_request(c, 1036, NULL) != 0) return -1;
    c->history_requested = now;
    return 0;
}

/* 1051 acknowledges a render request; machine state 12 reports its progress. */
int mqtt_generate_timelapse(mqtt_client *c, const char *url) {
    char escaped[600], params[640];
    if (!url || !*url || mqtt_json_escape(escaped, sizeof(escaped), url) != 0) return -1;
    int length = snprintf(params, sizeof(params), "{\"url\":\"%s\"}", escaped);
    if (length < 0 || (size_t)length >= sizeof(params)) return -1;
    if (publish_request(c, 1051, params) != 0) return -1;
    c->timelapse_requested = time(NULL);
    c->timelapse_rendering = 0;
    return 0;
}

int mqtt_connect_local(mqtt_client *c) {
    time_t now=time(NULL); c->last_connect_attempt=now;
    if(!c->password[0])return -1;
    int fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0)return -1;
    struct timeval tv={3,0};
    setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
    setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
    struct sockaddr_in a; memset(&a,0,sizeof(a));
    a.sin_family=AF_INET; a.sin_port=htons(1883); a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(connect(fd,(struct sockaddr*)&a,sizeof(a))<0){close(fd);return -1;}
    c->fd=fd;
    if(send_connect(c)<0){mqtt_close(c);return -1;}
    unsigned char ack[4]; ssize_t n=recv(fd,ack,sizeof(ack),MSG_WAITALL);
    if(n!=4||ack[0]!=0x20||ack[1]!=2||ack[3]!=0){mqtt_close(c);return -1;}
    if(send_subscribe(c)<0){mqtt_close(c);return -1;}
    int flags=fcntl(fd,F_GETFL,0); if(flags>=0)fcntl(fd,F_SETFL,flags|O_NONBLOCK);
    c->connected=1; c->mqtt_connections++; c->discovery_ready_at=now+DISCOVERY_SETTLE_SECONDS; c->last_ping=now; c->last_app_ping=0; c->input_len=0;
    return 0;
}

static const char *object_for(const char *json, size_t len, const char *name, size_t *object_len) {
    char needle[96]; snprintf(needle,sizeof(needle),"\"%s\"",name);
    const char *p=json,*end=json+len;
    while(p<end) {
        const char *hit=strstr(p,needle); if(!hit||hit>=end)return NULL;
        const char *q=hit+strlen(needle); while(q<end&&isspace((unsigned char)*q))q++;
        if(q>=end||*q++!=':'){p=hit+1;continue;}
        while(q<end&&isspace((unsigned char)*q))q++;
        if(q>=end||*q!='{'){p=hit+1;continue;}
        const char *start=q++; int depth=1,in_string=0,escape=0;
        for(;q<end;q++) {
            char ch=*q;
            if(in_string){if(escape)escape=0;else if(ch=='\\')escape=1;else if(ch=='"')in_string=0;continue;}
            if(ch=='"'){in_string=1;continue;} if(ch=='{')depth++; else if(ch=='}'&&!--depth){*object_len=(size_t)(q-start+1);return start;}
        }
        return NULL;
    }
    return NULL;
}

static int number_in(const char *object,size_t len,const char *field,double *value) {
    char needle[96]; snprintf(needle,sizeof(needle),"\"%s\"",field);
    const char *hit=strstr(object,needle); if(!hit||hit>=object+len)return 0;
    const char *p=hit+strlen(needle),*end=object+len;
    while(p<end&&isspace((unsigned char)*p))p++;
    if(p>=end||*p++!=':')return 0;
    while(p<end&&isspace((unsigned char)*p))p++;
    char *after; double v=strtod(p,&after); if(after==p||after>end)return 0;
    *value=v; return 1;
}

static int string_in(const char *object,size_t len,const char *field,char *out,size_t cap) {
    char needle[96];snprintf(needle,sizeof(needle),"\"%s\"",field);
    const char *hit=strstr(object,needle);if(!hit||hit>=object+len)return 0;
    const char *p=hit+strlen(needle),*end=object+len;
    while(p<end&&isspace((unsigned char)*p))p++;
    if(p>=end||*p++!=':')return 0;
    while(p<end&&isspace((unsigned char)*p))p++;
    if(p>=end||*p++!='"')return 0;
    size_t n=0;int escape=0;
    while(p<end&&*p!='"'){
        if(!escape&&*p=='\\'){escape=1;p++;continue;}
        if(n+1<cap)out[n++]=*p;
        escape=0;p++;
    }
    if(p>=end)return 0;
    out[n]='\0';return 1;
}

static int boolean_in(const char *object,size_t len,const char *field,int *value) {
    char needle[96];snprintf(needle,sizeof(needle),"\"%s\"",field);
    const char *hit=strstr(object,needle);if(!hit||hit>=object+len)return 0;
    const char *p=hit+strlen(needle),*end=object+len;
    while(p<end&&isspace((unsigned char)*p))p++;
    if(p>=end||*p++!=':')return 0;
    while(p<end&&isspace((unsigned char)*p))p++;
    if(end-p>=4&&!memcmp(p,"true",4)){*value=1;return 1;}
    if(end-p>=5&&!memcmp(p,"false",5)){*value=0;return 1;}
    return 0;
}

/* First-run discovery: do not depend on unsolicited MQTT publishes.
 * Only loopback is queried, credentials never enter logs, and the complete
 * HTTP exchange has one 500 ms monotonic deadline. No worker or polling thread. */
#ifndef CC2_DISCOVERY_PORT
#define CC2_DISCOVERY_PORT 80
#endif
static long discovery_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

static int discovery_wait(int fd, int writing, long deadline) {
    long left = deadline - discovery_ms();
    if (left <= 0) return -1;
    struct timeval timeout = {left / 1000, (left % 1000) * 1000};
    fd_set set; FD_ZERO(&set); FD_SET(fd, &set);
    return select(fd + 1, writing ? NULL : &set, writing ? &set : NULL,
                  NULL, &timeout) > 0 ? 0 : -1;
}

static int discover_serial_http(mqtt_client *c) {
    char request[640], response[4096], encoded[sizeof(c->password) * 3];
    static const char hex[] = "0123456789ABCDEF";
    size_t used = 0;
    for (const unsigned char *p = (const unsigned char *)c->password; *p; p++) {
        if (used + 3 >= sizeof(encoded)) return -1;
        encoded[used++] = '%'; encoded[used++] = hex[*p >> 4]; encoded[used++] = hex[*p & 15];
    }
    encoded[used] = 0;
    int size = snprintf(request, sizeof(request),
        "GET /system/info?X-Token=%s HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n", encoded);
    if (size <= 0 || (size_t)size >= sizeof(request)) return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    if (fd >= FD_SETSIZE || fcntl(fd, F_SETFL, O_NONBLOCK) < 0) { close(fd); return -1; }
    long deadline = discovery_ms() + 500;
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET; address.sin_port = htons(CC2_DISCOVERY_PORT);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    int result = -1;
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        if (errno != EINPROGRESS || discovery_wait(fd, 1, deadline) < 0) goto done;
        int error = 0; socklen_t length = sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length) < 0 || error) goto done;
    }
    used = 0;
    while (used < (size_t)size) {
        if (discovery_wait(fd, 1, deadline) < 0) goto done;
        ssize_t n = send(fd, request + used, (size_t)size - used, MSG_NOSIGNAL);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n <= 0) goto done;
        used += (size_t)n;
    }
    used = 0;
    for (;;) {
        if (used == sizeof(response) - 1 || discovery_wait(fd, 0, deadline) < 0) goto done;
        ssize_t n = recv(fd, response + used, sizeof(response) - 1 - used, 0);
        if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (n < 0) goto done;
        if (!n) break;
        used += (size_t)n;
    }
    response[used] = 0;
    int status = 0;
    if (sscanf(response, "HTTP/%*u.%*u %d", &status) != 1 || status != 200) goto done;
    const char *body = strstr(response, "\r\n\r\n");
    if (!body) goto done;
    body += 4;
    size_t length = 0;
    const char *info = object_for(body, strlen(body), "system_info", &length);
    char serial[sizeof(c->serial)];
    if (!info || !string_in(info, length, "sn", serial, sizeof(serial)) || !serial[0]) goto done;
    /* Reject truncation and escapes rather than routing requests to an ambiguous topic. */
    const char *field = strstr(info, "\"sn\"");
    if (!field || field >= info + length) goto done;
    field += 4;
    while (field < info + length && isspace((unsigned char)*field)) field++;
    if (field >= info + length || *field++ != ':') goto done;
    while (field < info + length && isspace((unsigned char)*field)) field++;
    if (field >= info + length || *field++ != '"') goto done;
    const char *end = memchr(field, '"', (size_t)(info + length - field));
    if (!end || (size_t)(end - field) != strlen(serial)) goto done;
    for (const char *p = serial; *p; p++)
        if (!isalnum((unsigned char)*p) && *p != '_' && *p != '-') goto done;
    snprintf(c->serial, sizeof(c->serial), "%s", serial);
    if (!c->serial_persisted && c->config_path[0]) {
        FILE *file = fopen(c->config_path, "a");
        if (file) {
            int ok = fprintf(file, "\nmqtt_serial=%s\n", serial) > 0;
            if (fclose(file) != 0) ok = 0;
            if (ok) c->serial_persisted = 1;
        }
    }
    result = 0;
done:
    close(fd);
    memset(request, 0, sizeof(request));
    return result;
}

static void update_pair(const char *object,size_t len,
                        const char *field,double *value,int *have) {
    double v; if(object&&number_in(object,len,field,&v)){*value=v;*have=1;}
}

static void update_state(mqtt_client *c,const char *json,size_t len) {
    size_t temperature_len=0;double temperature;
    const char *extruder=object_for(json,len,"extruder",&temperature_len);
    if(extruder){
        update_pair(extruder,temperature_len,"temperature",&c->extruder_temp,&c->have_extruder_temp);
        update_pair(extruder,temperature_len,"target",&c->extruder_target,&c->have_extruder_target);
        if(number_in(extruder,temperature_len,"filament_detect_enable",&temperature))c->filament_detect_enabled=(int)temperature;
        if(number_in(extruder,temperature_len,"filament_detected",&temperature))c->filament_detected=(int)temperature;
    }
    const char *bed=object_for(json,len,"heater_bed",&temperature_len);
    if(bed){
        update_pair(bed,temperature_len,"temperature",&c->bed_temp,&c->have_bed_temp);
        update_pair(bed,temperature_len,"target",&c->bed_target,&c->have_bed_target);
    }
    const char *chamber=object_for(json,len,"ztemperature_sensor",&temperature_len);
    update_pair(chamber,temperature_len,"temperature",&c->chamber_temp,&c->have_chamber_temp);
    size_t fans_len,fan_len; const char *fans=object_for(json,len,"fans",&fans_len);
    if(fans) {
        const char *o=object_for(fans,fans_len,"controller_fan",&fan_len); double v;
        if(o&&number_in(o,fan_len,"speed",&v)){c->controller_fan=v;c->have_controller_fan=1;}
        o=object_for(fans,fans_len,"heater_fan",&fan_len);
        if(o&&number_in(o,fan_len,"speed",&v)){c->heater_fan=v;c->have_heater_fan=1;}
        o=object_for(fans,fans_len,"fan",&fan_len);
        if(o&&number_in(o,fan_len,"speed",&v)){c->part_fan=v;c->have_part_fan=1;}
        o=object_for(fans,fans_len,"aux_fan",&fan_len);
        if(o&&number_in(o,fan_len,"speed",&v)){c->aux_fan=v;c->have_aux_fan=1;}
        o=object_for(fans,fans_len,"box_fan",&fan_len);
        if(o&&number_in(o,fan_len,"speed",&v)){c->box_fan=v;c->have_box_fan=1;}
    }
    size_t n;const char *o=object_for(json,len,"machine_status",&n);double v;
    if(o){if(number_in(o,n,"status",&v)){c->machine_status=(int)v;c->have_machine_status=1;}
        if(number_in(o,n,"progress",&v)){c->progress=(int)v;c->have_progress=1;}
        if(number_in(o,n,"sub_status",&v)){c->sub_status=(int)v;c->have_sub_status=1;}
        if(number_in(o,n,"sub_status_reason_code",&v))c->sub_status_reason=(int)v;}
    o=object_for(json,len,"print_status",&n);
    if(o){boolean_in(o,n,"enable",&c->print_enabled);string_in(o,n,"filename",c->filename,sizeof(c->filename));
        string_in(o,n,"state",c->print_state,sizeof(c->print_state));string_in(o,n,"uuid",c->uuid,sizeof(c->uuid));
        if(number_in(o,n,"current_layer",&v))c->current_layer=(int)v;
        if(number_in(o,n,"total_layer",&v)||number_in(o,n,"total_layers",&v)||
           number_in(o,n,"total_layer_count",&v)){
            c->total_layers=(int)v;c->have_total_layers=c->total_layers>0;
        }
        if(number_in(o,n,"print_duration",&v)||number_in(o,n,"elapsed_time",&v)||number_in(o,n,"duration",&v))c->print_duration=(long)v;
        if(number_in(o,n,"remaining_time_sec",&v)||number_in(o,n,"remaining_time",&v)||number_in(o,n,"time_remaining",&v))c->remaining_time=(long)v;
        if(number_in(o,n,"total_duration",&v)||number_in(o,n,"estimated_total_time",&v))c->total_duration=(long)v;}
    o=object_for(json,len,"gcode_move",&n);
    if(o){int found=0;if(number_in(o,n,"x",&c->x))found=1;if(number_in(o,n,"y",&c->y))found=1;if(number_in(o,n,"z",&c->z))found=1;if(found)c->have_position=1;
        if(number_in(o,n,"speed",&c->move_speed))c->have_speed=1;
        if(number_in(o,n,"speed_mode",&v)){c->speed_mode=(int)v;c->have_speed_mode=1;}}
    o=object_for(json,len,"tool_head",&n);if(o)string_in(o,n,"homed_axes",c->homed_axes,sizeof(c->homed_axes));
    o=object_for(json,len,"external_device",&n);if(o){boolean_in(o,n,"camera",&c->camera);boolean_in(o,n,"u_disk",&c->u_disk);}
    o=object_for(json,len,"led",&n);if(o&&number_in(o,n,"status",&v))c->led_status=(int)v;
    o=object_for(json,len,"canvas_info",&n);
    if(o){
        if(number_in(o,n,"active_tray_id",&v)){c->canvas_active_tray_id=(int)v;c->have_canvas_active_tray=1;}
        int refill;if(boolean_in(o,n,"auto_refill",&refill)){c->auto_refill=refill;c->have_auto_refill=1;}
    }
    c->last_message=time(NULL); c->messages++;
}

static void serial_from_topic(mqtt_client *c,const char *topic,size_t len) {
    const char prefix[]="elegoo/"; size_t prefix_len=sizeof(prefix)-1;
    if(c->serial[0]||len<=prefix_len||memcmp(topic,prefix,prefix_len))return;
    const char *start=topic+prefix_len,*slash=memchr(start,'/',len-prefix_len);
    if(!slash)return;
    size_t n=(size_t)(slash-start);
    if(n&&n<sizeof(c->serial)){
        memcpy(c->serial,start,n);c->serial[n]='\0';
        if(!c->serial_persisted&&c->config_path[0]){
            FILE *f=fopen(c->config_path,"a");
            if(f){fprintf(f,"\nmqtt_serial=%s\n",c->serial);fclose(f);c->serial_persisted=1;}
        }
    }
}

static int contains_text(const char *data,size_t len,const char *needle) {
    size_t n=strlen(needle);if(n>len)return 0;
    for(size_t i=0;i+n<=len;i++)if(!memcmp(data+i,needle,n))return 1;
    return 0;
}

static void capture_diagnostic(mqtt_client *c,const char *payload,size_t payload_len) {
    if(!payload_len||payload_len>=sizeof(c->diagnostic)-2)return;
    if(c->diagnostic_len+payload_len+1>=sizeof(c->diagnostic))c->diagnostic_len=0;
    memcpy(c->diagnostic+c->diagnostic_len,payload,payload_len);
    c->diagnostic_len+=payload_len;
    c->diagnostic[c->diagnostic_len++]='\n';
    c->diagnostic[c->diagnostic_len]='\0';
}

static int remaining_length(const unsigned char *p,size_t size,size_t *value,size_t *used) {
    size_t multiplier=1,v=0,i=0; unsigned char byte;
    do { if(i>=size||i>=4)return 0; byte=p[i++]; v+=(byte&127)*multiplier; multiplier*=128; } while(byte&128);
    *value=v;*used=i;return 1;
}

static int request_topic(const char *topic,size_t len) {
    static const char *suffixes[]={"/api_request","/api_register"};
    for(size_t i=0;i<sizeof(suffixes)/sizeof(suffixes[0]);i++){
        size_t n=strlen(suffixes[i]);
        if(len>=n&&!memcmp(topic+len-n,suffixes[i],n))return 1;
    }
    return 0;
}

/* The printer answers our api_request on .../<client_id>/api_response only. */
static int own_reply_topic(const mqtt_client *c,const char *topic,size_t len) {
    char suffix[96];int n=snprintf(suffix,sizeof(suffix),"/%s/api_response",c->client_id);
    return n>0&&(size_t)n<sizeof(suffix)&&len>(size_t)n&&!memcmp(topic+len-(size_t)n,suffix,(size_t)n);
}

/* Records a refusal (non-zero result.error_code) of one of our requests and
 * keeps the 1036 print history. Returns 1 when the reply carries no live state. */
static int handle_own_reply(mqtt_client *c,const char *payload,size_t len) {
    double v;size_t n;int method=0,code=-1;
    if(number_in(payload,len,"method",&v))method=(int)v;
    const char *result=object_for(payload,len,"result",&n);
    if(result&&number_in(result,n,"error_code",&v))code=(int)v;
    if(!method||code<0)return 0;
    time_t now=time(NULL);
    if(code){c->reply_error_method=method;c->reply_error_code=code;c->reply_error_time=now;c->reply_errors++;}
    if(method==1019){double id;if(number_in(payload,len,"id",&id)&&id==20000.0+c->print_config_id){c->print_config_reply_id=(unsigned long)id;c->print_config_error=(int)code;}}
    if(method==1038){c->history_delete_requested=0;if(!code)c->history_refresh_due=1;}
    if(method==2004&&!code)c->canvas_refresh_due=1; /* read the new setting back */
    /* The 1051 reply only acknowledges the request; mqtt_tick follows the rendering. */
    if(method==1051&&code){c->timelapse_requested=0;c->timelapse_rendering=0;}
    if(method!=1036)return 0;
    c->history_requested=0;c->history_error=code;c->history_received=now;
    if(!code){
        char *copy=malloc(len+1);
        if(copy){memcpy(copy,payload,len);copy[len]='\0';free(c->history);c->history=copy;c->history_len=len;}
        else c->history_error=-2;
    }
    return 1;
}

static void handle_publish(mqtt_client *c,unsigned char flags,const unsigned char *body,size_t remain) {
    if(remain<2)return;
    size_t topic_len=((size_t)body[0]<<8)|body[1],pos=2+topic_len;
    int qos=(flags>>1)&3; if(qos&&pos+2<=remain)pos+=2;
    const char *topic=(const char*)body+2;
    if(2+topic_len<=remain)serial_from_topic(c,topic,topic_len);
    if(pos>remain)return;
    const char *payload=(const char*)body+pos;size_t payload_len=remain-pos;
    c->received_publishes++;
    /* Wildcard subscriptions echo our requests and other clients'
     * commands. They cannot update printer state or freshness. */
    if(request_topic(topic,topic_len)){
        c->skipped_requests++;
        c->skipped_request_bytes+=(unsigned long)payload_len;
        return;
    }
    if(own_reply_topic(c,topic,topic_len)&&handle_own_reply(c,payload,payload_len))return;
    capture_diagnostic(c,payload,payload_len);
    if(contains_text(payload,payload_len,"\"canvas_info\"")&&
       contains_text(payload,payload_len,"\"canvas_list\"")) {
        if(payload_len<sizeof(c->canvas_snapshot)){
            memcpy(c->canvas_snapshot,payload,payload_len);
            c->canvas_snapshot[payload_len]='\0';c->canvas_snapshot_len=payload_len;
            c->canvas_discovery_complete=1;
        }else c->oversized_snapshots++;
    }
    update_state(c,payload,payload_len);
    if(contains_text(topic,topic_len,"/register_response")) {
        if(contains_text(payload,payload_len,"\"error\":\"ok\"")||
           contains_text(payload,payload_len,"already registered"))c->registered=1;
    }
    if(contains_text(topic,topic_len,"/api_response")&&
       contains_text(payload,payload_len,"\"method\":1002")) {
        if(payload_len<sizeof(c->snapshot)){
            memcpy(c->snapshot,payload,payload_len);c->snapshot[payload_len]='\0';c->snapshot_len=payload_len;c->info_responses++;
        }else c->oversized_snapshots++;
    }
}

static void consume_packets(mqtt_client *c) {
    size_t offset=0;
    while(c->input_len-offset>=2) {
        size_t remain,rn; if(!remaining_length(c->input+offset+1,c->input_len-offset-1,&remain,&rn))break;
        size_t header=1+rn,total=header+remain;
        if(total>sizeof(c->input)){
            /* Our subscription requests QoS 0. Drain large publishes in-place
             * without retaining their payload or reconnecting. */
            if((c->input[offset]&0xf6)!=0x30){mqtt_close(c);return;}
            /* Except a reply to our own request (the print history), kept up to MQTT_REPLY_MAX. */
            const unsigned char *body=c->input+offset+header;
            size_t have=c->input_len-offset-header,topic_len=have>=2?((size_t)body[0]<<8)|body[1]:0;
            if(have<2+topic_len&&header+2+topic_len<=sizeof(c->input))break; /* topic still arriving */
            if(have>=2+topic_len&&own_reply_topic(c,(const char*)body+2,topic_len)){
                if(remain<=MQTT_REPLY_MAX&&(c->large=malloc(remain+1))){
                    memcpy(c->large,body,have);c->large_len=have;c->large_need=remain;c->large_flags=c->input[offset];
                    offset=c->input_len;break;
                }
                c->oversized_replies++;
                if(c->history_requested){c->history_requested=0;c->history_error=-2;c->history_received=time(NULL);}
            }
            c->discard_remaining=total-(c->input_len-offset);
            c->oversized_packets++;offset=c->input_len;break;
        }
        if(c->input_len-offset<total)break;
        if((c->input[offset]>>4)==3)handle_publish(c,c->input[offset],c->input+offset+header,remain);
        offset+=total;
    }
    if(offset){memmove(c->input,c->input+offset,c->input_len-offset);c->input_len-=offset;}
}

int mqtt_process(mqtt_client *c) {
    if(c->fd<0)return -1;
    if(c->large){
        ssize_t n=recv(c->fd,c->large+c->large_len,c->large_need-c->large_len,0);
        if(n==0){mqtt_close(c);return -1;}
        if(n<0){if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)return 0;mqtt_close(c);return -1;}
        c->large_len+=(size_t)n;
        if(c->large_len==c->large_need){
            unsigned char *packet=c->large;size_t size=c->large_need;
            c->large=NULL;c->large_len=0;c->large_need=0;
            packet[size]=0;handle_publish(c,c->large_flags,packet,size);free(packet);
        }
        return 0;
    }
    if(c->discard_remaining){
        size_t want=c->discard_remaining<sizeof(c->input)?c->discard_remaining:sizeof(c->input);
        ssize_t n=recv(c->fd,c->input,want,0);
        if(n>0){c->discard_remaining-=(size_t)n;return 0;}
        if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return 0;
        mqtt_close(c);return -1;
    }
    if(c->input_len==sizeof(c->input)){mqtt_close(c);return -1;}
    ssize_t n=recv(c->fd,c->input+c->input_len,sizeof(c->input)-c->input_len,0);
    if(n==0){mqtt_close(c);return -1;}
    if(n<0){if(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR)return 0;mqtt_close(c);return -1;}
    c->input_len+=(size_t)n; consume_packets(c); return 0;
}

void mqtt_tick(mqtt_client *c) {
    time_t now=time(NULL);
    if(c->fd<0){if(now-c->last_connect_attempt>=5)mqtt_connect_local(c);return;}
    if (c->connected && c->password[0] && !c->serial[0] &&
        (c->serial_discovery_attempts == 0 ||
         now - c->last_serial_discovery >= (c->serial_discovery_attempts < 3 ? 10 : 60))) {
        c->last_serial_discovery = now;
        c->serial_discovery_attempts++;
        (void)discover_serial_http(c);
    }
    /* Delay automatic discovery after every successful MQTT connection, not
     * only cold boot. Heartbeats and passive telemetry continue independently.
     * Another client's Canvas reply can complete Canvas discovery during the
     * delay; registration, which every request needs, still follows. */
    if(now>=c->discovery_ready_at && (!c->canvas_discovery_complete || !c->registered)) {
        if(c->serial[0] && discovery_due(c->automatic_registration_attempts,c->last_registration_request,now)) {
            c->automatic_registration_attempts++;
            (void)send_registration(c);
            c->last_registration_request=now;
        }
        if(c->registered && discovery_due(c->automatic_snapshot_attempts,c->last_snapshot_request,now)) {
            c->automatic_snapshot_attempts++;
            (void)send_snapshot_request(c);
            c->last_snapshot_request=now;
        }
        if(c->registered && discovery_due(c->automatic_canvas_attempts,c->last_canvas_request,now)) {
            c->automatic_canvas_attempts++;
            (void)mqtt_request_canvas(c);
            c->last_canvas_request=now;
        }
    }
    /* The printer renders a requested time-lapse in machine state 12 after
     * acknowledging 1051; list the history again once it leaves that state (or
     * if it never entered it within 30 s, or after 10 minutes in any case). */
    if(c->timelapse_requested){
        if(c->have_machine_status&&c->machine_status==12&&now-c->timelapse_requested<600)c->timelapse_rendering=1;
        else if(c->timelapse_rendering||now-c->timelapse_requested>=30){
            c->timelapse_requested=0;c->timelapse_rendering=0;c->history_refresh_due=1;
        }
    }
    /* Follow-ups requested by replies: read auto refill back, list new videos.
     * Discovery can complete from a status delta that omits auto_refill; one full
     * Canvas reply per session reports it. */
    if(c->registered&&c->canvas_discovery_complete&&!c->have_auto_refill&&!c->auto_refill_probed){
        c->auto_refill_probed=1;c->canvas_refresh_due=1;
    }
    if(c->registered&&c->canvas_refresh_due){c->canvas_refresh_due=0;(void)mqtt_request_canvas(c);}
    if(c->registered&&c->history_refresh_due){c->history_refresh_due=0;(void)mqtt_request_history(c);}
    /* The official CC2 client keeps its *application* registration alive with
     * a JSON PING on its api_request topic.  MQTT PINGREQ only keeps the
     * broker's TCP session alive and is not a substitute for this heartbeat.
     * Do not change request IDs, print commands or registration behaviour
     * in this isolated experiment. */
    if(c->registered && c->serial[0] &&
       (c->last_app_ping==0 || now-c->last_app_ping>=10)) {
        char heartbeat_topic[256];
        int topic_size=snprintf(heartbeat_topic,sizeof(heartbeat_topic),
            "elegoo/%s/%s/api_request",c->serial,c->client_id);
        if(topic_size<0 || (size_t)topic_size>=sizeof(heartbeat_topic) ||
           mqtt_publish(c,heartbeat_topic,"{\"type\":\"PING\"}")<0) {
            mqtt_close(c);
            return;
        }
        c->last_app_ping=now;
    }
    if(now-c->last_ping>=20){unsigned char ping[2]={0xC0,0};if(send(c->fd,ping,2,MSG_NOSIGNAL)!=2){mqtt_close(c);return;}c->last_ping=now;}
}
