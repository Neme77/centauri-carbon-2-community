/* Print history (vendor method 1036) and its time-lapse videos.
 *
 * mqtt.c keeps the latest complete 1036 reply to our own request; nothing here
 * polls. Rendering stored frames into an MP4 is method 1051. The rendered file
 * is served by the printer's own HTTP service, which needs the LAN code as a
 * token: browsers never receive it, a worker relays the download from loopback
 * like the G-code download, and shares its two-transfer limit. */
#define TIMELAPSE_NAME_MAX 240
/* The printer's HTTP service (the same loopback port as serial discovery). Tests override both. */
static int timelapse_service_port=80;
static int timelapse_timeout_seconds=15;

static int history_task_id(const char *text,size_t length,char out[72]) {
    if(!length||length>64)return 0;
    for(size_t i=0;i<length;i++) {
        unsigned char ch=(unsigned char)text[i];
        if(!isalnum(ch)&&ch!='-'&&ch!='_')return 0;
    }
    memcpy(out,text,length);out[length]=0;return 1;
}

/* One task of the cached reply, by exact task_id: its video status and raw video URL. */
static int history_task(const mqtt_client *mqtt,const char *id,int *video_status,char *url,size_t url_cap) {
    if(!mqtt->history)return 0;
    const char *end=mqtt->history+mqtt->history_len,*root=json_skip_space(mqtt->history,end);
    const char *root_end,*result_end,*list_end;
    if(root>=end||*root!='{'||!(root_end=json_container_end(root,end)))return 0;
    const char *result=json_member_object(root,root_end,"result",'{',&result_end);
    const char *list=result?json_member_object(result,result_end,"history_task_list",'[',&list_end):NULL;
    if(!list)return 0;
    size_t id_length=strlen(id);
    for(const char *item=json_next_element(list,list_end);item;) {
        const char *item_end=json_container_end(item,list_end),*text;int length;
        if(!item_end)return 0;
        if(json_member_raw_string(item,item_end,"task_id",&text,&length)&&
           (size_t)length==id_length&&!memcmp(text,id,id_length)) {
            if(!json_member_int(item,item_end,"time_lapse_video_status",video_status))*video_status=-1;
            url[0]=0;
            if(json_member_raw_string(item,item_end,"time_lapse_video_url",&text,&length)) {
                /* Escaped names are refused rather than decoded. */
                if(length<0||(size_t)length>=url_cap||memchr(text,'\\',(size_t)length))return 0;
                memcpy(url,text,(size_t)length);url[length]=0;
            }
            return 1;
        }
        item=json_next_element(item_end,list_end);
    }
    return 0;
}

/* "picture/NAME" or "video/NAME.mp4" -> "video/NAME.mp4": the rendered MP4 is
 * served under video/ whichever folder the history entry names. */
static int timelapse_file(const char *url,char *out,size_t cap) {
    const char *name=strchr(url,'/');name=name?name+1:url;
    size_t length=strlen(name);
    if(!length||length>TIMELAPSE_NAME_MAX||name[0]=='.')return 0;
    for(const unsigned char *p=(const unsigned char *)name;*p;p++)
        if(*p<32||*p==127||*p=='/'||*p=='\\'||*p=='"')return 0;
    int mp4=length>=4&&!strcmp(name+length-4,".mp4");
    int written=snprintf(out,cap,"video/%s%s",name,mp4?"":".mp4");
    return written>0&&(size_t)written<cap;
}

static int percent_encode(char *out,size_t cap,const char *in) {
    static const char hex[]="0123456789ABCDEF";
    size_t used=0;
    for(const unsigned char *p=(const unsigned char *)in;*p;p++) {
        if(isalnum(*p)||*p=='-'||*p=='.'||*p=='_'||*p=='~'){if(used+1>=cap)return 0;out[used++]=(char)*p;}
        else{if(used+3>=cap)return 0;out[used++]='%';out[used++]=hex[*p>>4];out[used++]=hex[*p&15];}
    }
    out[used]=0;return 1;
}

static void history_response(int fd,const mqtt_client *mqtt) {
    time_t now=time(NULL);
    int pending=mqtt->history_requested&&now-mqtt->history_requested<10;
    int generating=mqtt->timelapse_requested&&now-mqtt->timelapse_requested<600;
    int deleting=mqtt->history_delete_requested&&now-mqtt->history_delete_requested<10;
    long age=mqtt->history_received?(long)(now-mqtt->history_received):-1;
    size_t cap=(mqtt->history?mqtt->history_len:0)+256;
    char *body=malloc(cap);
    if(!body){const char *error="{\"error\":\"Out of memory\"}\n";respond(fd,503,"Service Unavailable","application/json",error,strlen(error));return;}
    int length=snprintf(body,cap,
        "{\"available\":%s,\"pending\":%s,\"generating\":%s,\"deleting\":%s,\"age\":%ld,\"error_code\":%d,\"reply\":%s}\n",
        mqtt->history?"true":"false",pending?"true":"false",generating?"true":"false",deleting?"true":"false",age,
        mqtt->history_error,mqtt->history?mqtt->history:"null");
    if(length>0&&(size_t)length<cap)respond(fd,200,"OK","application/json; charset=utf-8",body,(size_t)length);
    free(body);
}

/* Delete terminal entries explicitly named by the operator, never file paths.
 * The vendor 1038 handler takes params.list and removes task records from its
 * cache/database. Rendering/printing and stale state fail closed. */
static int history_terminal_task(const mqtt_client *mqtt,const char *id) {
    if(!mqtt->history)return 0;
    const char *end=mqtt->history+mqtt->history_len,*root=json_skip_space(mqtt->history,end);
    const char *root_end,*result_end,*list_end;
    if(root>=end||*root!='{'||!(root_end=json_container_end(root,end)))return 0;
    const char *result=json_member_object(root,root_end,"result",'{',&result_end);
    const char *list=result?json_member_object(result,result_end,"history_task_list",'[',&list_end):NULL;
    if(!list)return 0;
    for(const char *item=json_next_element(list,list_end);item;) {
        const char *item_end=json_container_end(item,list_end),*text;int length,status;
        if(!item_end)return 0;
        if(json_member_raw_string(item,item_end,"task_id",&text,&length)&&
           (size_t)length==strlen(id)&&!memcmp(text,id,(size_t)length))
            return json_member_int(item,item_end,"task_status",&status)&&status>=1&&status<=3;
        item=json_next_element(item_end,list_end);
    }
    return 0;
}
static void history_delete_response(int fd,mqtt_client *mqtt,const char *body,size_t len) {
    const char *error=NULL;int code=400;time_t now=time(NULL);
    if(!mqtt->connected||!mqtt->registered||!mqtt->have_machine_status||mqtt->machine_status!=1||
       !mqtt->last_message||now-mqtt->last_message>15||now<mqtt->last_message) {
        error="History deletion requires fresh Idle printer state";code=409;
    }else if((mqtt->timelapse_requested&&now-mqtt->timelapse_requested<600)||
             (mqtt->history_delete_requested&&now-mqtt->history_delete_requested<10)) {
        error="A history operation is already pending";code=409;
    }else if(!mqtt->history||!mqtt->history_received||now-mqtt->history_received>60) {
        error="Refresh the print history before deleting entries";code=409;
    }
    char ids[50][72],params[3600];size_t count=0,used=9;
    memcpy(params,"{\"list\":[",9);
    if(!error){
        while(len&&isspace((unsigned char)*body)){body++;len--;}
        while(len&&isspace((unsigned char)body[len-1]))len--;
        const char *end=body+len;
        for(const char *line=body;line<end;){
            const char *next=memchr(line,'\n',(size_t)(end-line));if(!next)next=end;
            if(count==50||!history_task_id(line,(size_t)(next-line),ids[count])){error="Invalid history task selection";break;}
            for(size_t i=0;i<count;i++)if(!strcmp(ids[i],ids[count])){error="Duplicate history task ID";break;}
            if(error)break;
            if(!history_terminal_task(mqtt,ids[count])){error="History entry is missing or still active";code=409;break;}
            int n=snprintf(params+used,sizeof(params)-used,"%s\"%s\"",count?",":"",ids[count]);
            if(n<0||(size_t)n>=sizeof(params)-used){error="History selection is too large";break;}
            used+=(size_t)n;count++;line=next<end?next+1:end;
        }
        if(!count&&!error)error="Select at least one history task";
    }
    if(!error){
        memcpy(params+used,"]}",3);
        if(mqtt_delete_history(mqtt,params)){error="MQTT API client is not ready";code=503;}
    }
    char reply[256];
    if(error){int n=snprintf(reply,sizeof(reply),"{\"accepted\":false,\"error\":\"%s\"}\n",error);respond(fd,code,code==400?"Bad Request":code==409?"Conflict":"Service Unavailable","application/json",reply,(size_t)n);}
    else{int n=snprintf(reply,sizeof(reply),"{\"accepted\":true,\"method\":1038,\"count\":%zu}\n",count);respond(fd,202,"Accepted","application/json",reply,(size_t)n);}
}

static void history_refresh_response(int fd,mqtt_client *mqtt) {
    if(mqtt_request_history(mqtt)!=0) {
        const char *error="{\"accepted\":false,\"error\":\"MQTT API client is not ready\"}\n";
        respond(fd,503,"Service Unavailable","application/json; charset=utf-8",error,strlen(error));return;
    }
    const char *ok="{\"accepted\":true,\"method\":1036}\n";
    respond(fd,202,"Accepted","application/json; charset=utf-8",ok,strlen(ok));
}

/* Rendering takes the printer minutes, so it is only started from Idle, one at a time. */
static void timelapse_generate_response(int fd,mqtt_client *mqtt,const char *body,size_t body_len) {
    char id[72],url[512];int video=-1,status=409;time_t now=time(NULL);
    while(body_len&&isspace((unsigned char)*body)){body++;body_len--;}
    while(body_len&&isspace((unsigned char)body[body_len-1]))body_len--;
    const char *error=NULL;
    if(!history_task_id(body,body_len,id)){error="{\"accepted\":false,\"error\":\"Invalid print task\"}\n";status=400;}
    else if(!history_task(mqtt,id,&video,url,sizeof(url))||!url[0]){error="{\"accepted\":false,\"error\":\"Unknown print task; refresh the history\"}\n";status=404;}
    else if(video!=1&&video!=3)error="{\"accepted\":false,\"error\":\"This print has no time-lapse frames to render\"}\n";
    else if(!mqtt->have_machine_status||mqtt->machine_status!=1||!mqtt->last_message||now-mqtt->last_message>15)
        error="{\"accepted\":false,\"error\":\"Rendering a time-lapse video requires an idle printer\"}\n";
    else if(mqtt->history_delete_requested&&now-mqtt->history_delete_requested<10)
        error="{\"accepted\":false,\"error\":\"History deletion is pending\"}\n";
    else if(mqtt->timelapse_requested&&now-mqtt->timelapse_requested<600)
        error="{\"accepted\":false,\"error\":\"A time-lapse video is already being rendered\"}\n";
    else if(mqtt_generate_timelapse(mqtt,url)!=0){error="{\"accepted\":false,\"error\":\"MQTT API client is not ready\"}\n";status=503;}
    if(error){respond(fd,status,status==400?"Bad Request":status==404?"Not Found":status==503?"Service Unavailable":"Conflict","application/json; charset=utf-8",error,strlen(error));return;}
    const char *ok="{\"accepted\":true,\"method\":1051}\n";
    respond(fd,202,"Accepted","application/json; charset=utf-8",ok,strlen(ok));
}

typedef struct { int client; char request[2048]; char name[TIMELAPSE_NAME_MAX*3+16]; } timelapse_download;

static int header_value_contains(const char *headers,const char *name,const char *token) {
    const char *value=find_case_insensitive(headers,name);
    if(!value)return 0;
    value+=strlen(name);
    const char *end=strstr(value,"\r\n");
    char copy[128];size_t length=end?(size_t)(end-value):strlen(value);
    if(length>=sizeof(copy))length=sizeof(copy)-1;
    memcpy(copy,value,length);copy[length]=0;
    return find_case_insensitive(copy,token)!=NULL;
}

/* "1a2b\r\n" or "1a2b;ext\r\n": the body really is chunk-framed. */
static int chunk_framed(const char *body,size_t length) {
    size_t i=0;
    while(i<length&&i<16&&isxdigit((unsigned char)body[i]))i++;
    return i>0&&i<length&&(body[i]=='\r'||body[i]==';');
}

/* The printer keeps the connection open after a complete response despite
 * "Connection: close", so the relay finds the end of the body itself: the last
 * chunk and its trailer, or Content-Length bytes. left<0: unknown, relay to EOF. */
typedef struct { int chunked, state, done; long long left; unsigned long long size; size_t line; } body_end;

/* Returns how many of the bytes belong to the body; done is set at its end. */
static size_t body_feed(body_end *e,const char *data,size_t length) {
    if(!e->chunked) {
        if(e->left<0)return length;
        size_t take=(unsigned long long)e->left<length?(size_t)e->left:length;
        e->left-=(long long)take;e->done=e->left==0;return take;
    }
    size_t i=0;
    while(i<length&&!e->done) {
        if(e->state==2) { /* chunk data */
            size_t take=e->size<length-i?(size_t)e->size:length-i;
            e->size-=take;i+=take;if(!e->size)e->state=3;
            continue;
        }
        unsigned char ch=(unsigned char)data[i++];
        if(e->state<2) { /* 0: size digits, 1: extension */
            if(ch=='\n'){e->state=e->size?2:4;e->line=0;}
            else if(!e->state&&isxdigit(ch)) {
                if(e->size>>40){e->chunked=0;e->left=-1;return length;} /* not a real chunk size */
                e->size=e->size*16+(unsigned)(isdigit(ch)?ch-'0':tolower(ch)-'a'+10);
            } else if(ch!='\r')e->state=1;
        } else if(e->state==3) { /* CRLF after the data */
            if(ch=='\n')e->state=0;
        } else { /* 4: trailer lines until an empty one */
            if(ch=='\n'){if(!e->line)e->done=1;e->line=0;}
            else if(ch!='\r')e->line++;
        }
    }
    return i;
}

static void *timelapse_worker(void *opaque) {
    timelapse_download *job=opaque;
    char buffer[16384],head[1024];size_t used=0;char *body=NULL;int sent=0,status=0;
    struct timeval timeout={timelapse_timeout_seconds,0};
    setsockopt(job->client,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    int upstream=socket(AF_INET,SOCK_STREAM,0);
    if(upstream>=0) {
        struct sockaddr_in address;memset(&address,0,sizeof(address));
        address.sin_family=AF_INET;address.sin_port=htons((unsigned short)timelapse_service_port);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
        setsockopt(upstream,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
        setsockopt(upstream,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
        if(connect(upstream,(struct sockaddr *)&address,sizeof(address))<0||send_all(upstream,job->request,strlen(job->request))){close(upstream);upstream=-1;}
    }
    memset(job->request,0,sizeof(job->request)); /* it carried the LAN code */
    /* A wrong token or name is answered with silence, hence the receive timeout.
     * Read the headers and, for a chunked reply, its first chunk-size line. */
    int te_chunked=0;
    while(upstream>=0&&used<sizeof(buffer)-1) {
        ssize_t got=recv(upstream,buffer+used,sizeof(buffer)-1-used,0);
        if(got<0&&errno==EINTR)continue;
        if(got<=0)break;
        used+=(size_t)got;buffer[used]=0;
        if(!(body=strstr(buffer,"\r\n\r\n")))continue;
        char saved=*body;*body=0;
        te_chunked=header_value_contains(buffer,"\r\nTransfer-Encoding:","chunked");
        *body=saved;
        size_t have=used-(size_t)(body+4-buffer),digits=0;
        while(digits<have&&digits<17&&isxdigit((unsigned char)body[4+digits]))digits++;
        if(!te_chunked||digits<have||digits>=17)break;
    }
    if(body&&sscanf(buffer,"HTTP/%*u.%*u %d",&status)==1&&status==200) {
        *body=0;body+=4;
        size_t pending=used-(size_t)(body-buffer);
        /* The printer sends Content-Length together with Transfer-Encoding: chunked.
         * Chunked takes precedence (RFC 9112) when the body is chunk-framed. */
        int chunked=te_chunked&&chunk_framed(body,pending);
        long long length=-1;const char *declared=find_case_insensitive(buffer,"\r\nContent-Length:");
        if(declared){char *after;long long value=strtoll(declared+17,&after,10);if(after!=declared+17&&value>=0)length=value;}
        char framing[64]="";
        if(chunked)snprintf(framing,sizeof(framing),"Transfer-Encoding: chunked\r\n");
        else if(length>=0)snprintf(framing,sizeof(framing),"Content-Length: %lld\r\n",length);
        int n=snprintf(head,sizeof(head),
            "HTTP/1.1 200 OK\r\nContent-Type: video/mp4\r\n%s"
            "Content-Disposition: attachment; filename=\"timelapse.mp4\"; filename*=UTF-8''%s\r\n"
            "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",
            framing,job->name);
        if(n>0&&(size_t)n<sizeof(head)&&!send_all(job->client,head,(size_t)n)) {
            sent=1;
            body_end end;memset(&end,0,sizeof(end));end.chunked=chunked;end.left=chunked?0:length;
            size_t take=body_feed(&end,body,pending);
            int ok=!take||!send_all(job->client,body,take);
            struct timespec started,now;clock_gettime(CLOCK_MONOTONIC,&started);
            while(ok&&!end.done) {
                clock_gettime(CLOCK_MONOTONIC,&now);if(now.tv_sec-started.tv_sec>=900)break;
                ssize_t got=recv(upstream,buffer,sizeof(buffer),0);
                if(got<0&&errno==EINTR)continue;
                if(got<=0)break;
                take=body_feed(&end,buffer,(size_t)got);
                if(take&&send_all(job->client,buffer,take))break;
            }
        }
    }
    if(!sent) {
        const char *error=status?"{\"error\":\"The printer refused the time-lapse download\"}\n":
                                 "{\"error\":\"The printer time-lapse service did not answer\"}\n";
        respond(job->client,status?502:504,status?"Bad Gateway":"Gateway Timeout","application/json",error,strlen(error));
    }
    if(upstream>=0)close(upstream);
    close(job->client);free(job);atomic_fetch_sub(&active_downloads,1);return NULL;
}

static int timelapse_download_start(int fd,const mqtt_client *mqtt,const char *query) {
    char id[72],url[512],file[TIMELAPSE_NAME_MAX+16],encoded[(TIMELAPSE_NAME_MAX+16)*3+1];
    char token[sizeof(mqtt->password)*3+1];int video=-1;
    const char *error=NULL;int status=409;
    if(!query||strncmp(query,"task=",5)||!history_task_id(query+5,strlen(query+5),id)){error="{\"error\":\"Invalid print task\"}\n";status=400;}
    else if(!history_task(mqtt,id,&video,url,sizeof(url))){error="{\"error\":\"Unknown print task; refresh the history\"}\n";status=404;}
    else if(video!=2||!timelapse_file(url,file,sizeof(file)))error="{\"error\":\"This time-lapse video is not ready\"}\n";
    else if(!mqtt->password[0]||!percent_encode(encoded,sizeof(encoded),file)||!percent_encode(token,sizeof(token),mqtt->password)){
        error="{\"error\":\"Printer credentials are unavailable\"}\n";status=503;}
    if(error){respond(fd,status,status==400?"Bad Request":status==404?"Not Found":status==503?"Service Unavailable":"Conflict","application/json",error,strlen(error));return 0;}
    int previous=atomic_fetch_add(&active_downloads,1);
    timelapse_download *job=NULL;pthread_attr_t attr;pthread_t thread;int initialized=0,launched=0;
    if(previous<2&&(job=malloc(sizeof(*job)))) {
        job->client=fd;
        int n=snprintf(job->request,sizeof(job->request),
            "GET /download?X-Token=%s&file_name=%s HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n",token,encoded);
        if(n>0&&(size_t)n<sizeof(job->request)&&percent_encode(job->name,sizeof(job->name),file+6)&&!pthread_attr_init(&attr)) {
            initialized=1;
            if(!pthread_attr_setstacksize(&attr,64*1024)&&!pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED))
                launched=!pthread_create(&thread,&attr,timelapse_worker,job);
        }
    }
    memset(token,0,sizeof(token));
    if(initialized)pthread_attr_destroy(&attr);
    if(launched)return 1; /* worker owns client from now on */
    if(job){memset(job->request,0,sizeof(job->request));free(job);}
    atomic_fetch_sub(&active_downloads,1);
    const char *busy="{\"error\":\"Download capacity reached; retry shortly\"}\n";
    respond(fd,503,"Service Unavailable","application/json",busy,strlen(busy));return 0;
}
