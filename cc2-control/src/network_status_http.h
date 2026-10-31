/* Experimental integration adapter, included after main.c's JSON/respond
 * helpers. No network editing route and no permanent polling thread. */
#ifndef CC2_NETWORK_HTTP_STATUS_H
#define CC2_NETWORK_HTTP_STATUS_H
#include <pthread.h>
#include <time.h>

int cc2_network_snapshot(FILE *output);
static pthread_mutex_t network_sample_lock=PTHREAD_MUTEX_INITIALIZER;
static char network_sample[8192];
static int network_sample_busy,network_sample_valid;
static long long network_sample_at,network_sample_attempt;
static long long network_http_clock(void){
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    return (long long)now.tv_sec*1000+now.tv_nsec/1000000;
}
static void *network_sample_worker(void *unused){
    (void)unused;
    char data[sizeof(network_sample)]={0};
    FILE *output=fmemopen(data,sizeof(data)-1,"w");
    int ok=0;
    if(output){
        int result=cc2_network_snapshot(output);
        long length=ftell(output);
        int closed=fclose(output);
        ok=result==0 && closed==0 && length>0 && (size_t)length<sizeof(data)-1;
    }
    pthread_mutex_lock(&network_sample_lock);
    network_sample_valid=ok;
    if(ok){memcpy(network_sample,data,sizeof(data));network_sample_at=network_http_clock();}
    network_sample_busy=0;
    pthread_mutex_unlock(&network_sample_lock);
    return NULL;
}
static void network_status_response(int fd){
    char payload[sizeof(network_sample)+512];
    json_builder builder={payload,0,sizeof(payload),0};
    pthread_mutex_lock(&network_sample_lock);
    long long now=network_http_clock();
    /* At most one worker and one attempted sample per three seconds, even if
     * many tabs request simultaneously. Every worker is detached with a small
     * bounded stack; no completed joinable-thread accumulation. */
    if(!network_sample_busy && (!network_sample_attempt || now-network_sample_attempt>=3000)){
        pthread_attr_t attr;
        network_sample_attempt=now;
        if(pthread_attr_init(&attr)==0){
            int configured=pthread_attr_setstacksize(&attr,128*1024)==0 &&
                           pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED)==0;
            if(configured){
                pthread_t thread;network_sample_busy=1;
                if(pthread_create(&thread,&attr,network_sample_worker,NULL)!=0)network_sample_busy=0;
            }
            pthread_attr_destroy(&attr);
        }
    }
    int available=network_sample_valid;
    long long age=available?now-network_sample_at:-1;
    json_builder_printf(&builder,"{\"available\":%s,\"fresh\":%s,\"refreshing\":%s,\"age_ms\":",
                        available?"true":"false",available&&age>=0&&age<8000?"true":"false",network_sample_busy?"true":"false");
    if(available)json_builder_printf(&builder,"%lld",age);else json_builder_printf(&builder,"null");
    json_builder_printf(&builder,",\"snapshot\":%s}\n",available?network_sample:"null");
    pthread_mutex_unlock(&network_sample_lock);
    if(builder.failed){
        const char *error="{\"error\":\"Network status unavailable\"}\n";
        respond(fd,503,"Service Unavailable","application/json; charset=utf-8",error,strlen(error));return;
    }
    respond(fd,200,"OK","application/json; charset=utf-8",payload,builder.length);
}
#endif
