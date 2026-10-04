#include "../src/mqtt.c"
#include <assert.h>
#include <sys/ioctl.h>
/* Replies to our own requests: refusals, auto refill, and the print history
 * that can exceed input[] without being dropped or desynchronising the stream. */
static mqtt_client c;
static int peer;
static void drain(void){int queued=1;while(!ioctl(c.fd,FIONREAD,&queued)&&queued>0)assert(!mqtt_process(&c));}
static void publish(const char *topic,const char *payload,size_t length){
    size_t topic_len=strlen(topic),remain=2+topic_len+length,size=5+remain,used=0;
    unsigned char *packet=malloc(size);assert(packet);
    packet[0]=0x30;size_t h=1+put_remaining(packet+1,remain);
    packet[h]=(unsigned char)(topic_len>>8);packet[h+1]=(unsigned char)topic_len;
    memcpy(packet+h+2,topic,topic_len);memcpy(packet+h+2+topic_len,payload,length);
    while(used<h+remain){size_t part=h+remain-used<8192?h+remain-used:8192;assert(!send_all(peer,packet+used,part));used+=part;drain();}
    free(packet);
}
static void say(const char *topic,const char *payload){publish(topic,payload,strlen(payload));}
static char *history_reply(size_t tasks){
    char *json=malloc(tasks*200+128);size_t n=(size_t)sprintf(json,"{\"id\":1036,\"method\":1036,\"result\":{\"error_code\":0,\"history_task_list\":[");
    for(size_t i=0;i<tasks;i++)n+=(size_t)sprintf(json+n,"%s{\"task_id\":\"t%05zu\",\"task_name\":\"Part %05zu.gcode\",\"task_status\":1,\"time_lapse_video_status\":1,\"time_lapse_video_url\":\"picture/Part %05zu\"}",i?",":"",i,i,i);
    strcpy(json+n,"]}}");return json;
}
/* Reads exactly one queued PUBLISH (QoS 1: packet id after the topic). */
static size_t sent(char *payload,size_t cap){
    unsigned char packet[2048];size_t n=0,remain,rn;
    assert(recv(peer,packet,1,MSG_DONTWAIT)==1&&packet[0]==0x32);
    do{assert(n<4&&recv(peer,packet+1+n,1,0)==1);n++;}while(packet[n]&0x80);
    assert(remaining_length(packet+1,n,&remain,&rn)&&1+rn+remain<=sizeof(packet));
    assert(recv(peer,packet+1+rn,remain,MSG_WAITALL)==(ssize_t)remain);
    size_t topic=((size_t)packet[1+rn]<<8)|packet[2+rn],start=1+rn+2+topic+2,length=1+rn+remain-start;
    assert(length<cap);memcpy(payload,packet+start,length);payload[length]=0;return length;
}
int main(void){
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    mqtt_init(&c);c.fd=pair[0];peer=pair[1];c.connected=1;c.registered=1;
    fcntl(c.fd,F_SETFL,fcntl(c.fd,F_GETFL,0)|O_NONBLOCK);
    strcpy(c.serial,"SN");strcpy(c.client_id,"cc2-control-test");
    const char *own="elegoo/SN/cc2-control-test/api_response";
    /* A printer refusal of our start is recorded; another client's refusal is not. */
    say(own,"{\"id\":1020,\"method\":1020,\"result\":{\"error_code\":1026}}");
    assert(c.reply_errors==1&&c.reply_error_method==1020&&c.reply_error_code==1026&&c.reply_error_time);
    say("elegoo/SN/0cli123/api_response","{\"id\":1,\"method\":1020,\"result\":{\"error_code\":1009}}");
    say("elegoo/SN/cc2-control-test-2/api_response","{\"id\":1,\"method\":1020,\"result\":{\"error_code\":1009}}");
    assert(c.reply_errors==1&&c.reply_error_code==1026);
    /* An accepted auto-refill change schedules a Canvas read-back. */
    say(own,"{\"id\":2004,\"method\":2004,\"result\":{\"error_code\":0}}");
    assert(c.reply_errors==1&&c.canvas_refresh_due);
    /* auto_refill is sticky: status deltas without it keep the last report. */
    assert(!c.have_auto_refill);
    say("elegoo/SN/api_status","{\"method\":6000,\"result\":{\"canvas_info\":{\"active_tray_id\":1,\"auto_refill\":true,\"canvas_list\":[]}}}");
    assert(c.have_auto_refill&&c.auto_refill==1&&c.canvas_active_tray_id==1);
    say("elegoo/SN/api_status","{\"method\":6000,\"result\":{\"canvas_info\":{\"active_tray_id\":2,\"canvas_list\":[]}}}");
    assert(c.have_auto_refill&&c.auto_refill==1&&c.canvas_active_tray_id==2);
    /* A small history reply is kept and not mixed into diagnostics or live state. */
    size_t diagnostic=c.diagnostic_len;unsigned long messages=c.messages;
    c.history_requested=time(NULL);
    char *small=history_reply(3);say(own,small);
    assert(c.history&&c.history_len==strlen(small)&&!strcmp(c.history,small)&&c.history_error==0&&!c.history_requested);
    assert(c.diagnostic_len==diagnostic&&c.messages==messages);free(small);
    /* A 50 KB history is assembled on the heap; the next message still parses. */
    char *large=history_reply(400);assert(strlen(large)>sizeof(c.input));
    c.history_requested=time(NULL);unsigned long oversized=c.oversized_packets;
    say(own,large);
    assert(!c.large&&c.history_len==strlen(large)&&!strcmp(c.history,large)&&!c.history_requested);
    assert(c.oversized_packets==oversized&&!c.oversized_replies&&c.fd==pair[0]&&c.connected);
    say("elegoo/SN/api_status","{\"machine_status\":{\"status\":1}}");assert(c.machine_status==1);
    /* The same size for another client is drained as before and keeps our history. */
    publish("elegoo/SN/0cli123/api_response",large,strlen(large));
    assert(c.oversized_packets==oversized+1&&!c.discard_remaining&&c.history_len==strlen(large));
    say("elegoo/SN/api_status","{\"machine_status\":{\"status\":2}}");assert(c.machine_status==2);
    /* Beyond MQTT_REPLY_MAX our own reply is drained too and the pending request fails. */
    size_t huge=MQTT_REPLY_MAX+1024;char *filler=malloc(huge);memset(filler,' ',huge);memcpy(filler,"{\"method\":1036}",15);
    c.history_requested=time(NULL);publish(own,filler,huge);free(filler);
    assert(c.oversized_replies==1&&c.history_error==-2&&!c.history_requested&&c.history_len==strlen(large));
    say("elegoo/SN/api_status","{\"machine_status\":{\"status\":1}}");assert(c.machine_status==1);
    /* A failed history request keeps the previous list. */
    say(own,"{\"id\":1036,\"method\":1036,\"result\":{\"error_code\":1013}}");
    assert(c.history_error==1013&&c.history_len==strlen(large)&&c.reply_error_method==1036&&c.reply_errors==2);
    /* A finished render clears the in-flight flag and refreshes the history. */
    c.timelapse_requested=time(NULL);
    say(own,"{\"id\":1051,\"method\":1051,\"result\":{\"error_code\":0,\"url\":\"video/Part 1.mp4\"}}");
    assert(!c.timelapse_requested&&c.history_refresh_due);
    free(large);
    /* Requests are published as JSON-RPC on our api_request topic. */
    char payload[1024];
    assert(!mqtt_set_auto_refill(&c,1));sent(payload,sizeof(payload));
    assert(!strcmp(payload,"{\"method\":2004,\"id\":2004,\"params\":{\"auto_refill\":true}}"));
    assert(!mqtt_set_auto_refill(&c,0));sent(payload,sizeof(payload));
    assert(!strcmp(payload,"{\"method\":2004,\"id\":2004,\"params\":{\"auto_refill\":false}}"));
    c.history_requested=0;assert(!mqtt_request_history(&c));sent(payload,sizeof(payload));
    assert(!strcmp(payload,"{\"method\":1036,\"id\":1036}")&&c.history_requested);
    assert(!mqtt_request_history(&c));assert(recv(peer,payload,sizeof(payload),MSG_DONTWAIT)<0); /* one in flight */
    assert(!mqtt_generate_timelapse(&c,"picture/A \"quoted\" part"));sent(payload,sizeof(payload));
    assert(!strcmp(payload,"{\"method\":1051,\"id\":1051,\"params\":{\"url\":\"picture/A \\\"quoted\\\" part\"}}")&&c.timelapse_requested);
    assert(mqtt_generate_timelapse(&c,"bad\nname")&&mqtt_generate_timelapse(&c,""));
    /* Follow-ups requested by replies are sent by the tick, once. */
    c.history_requested=0;c.canvas_refresh_due=1;c.history_refresh_due=1;c.last_app_ping=time(NULL);c.last_ping=time(NULL);
    c.canvas_discovery_complete=1;mqtt_tick(&c);
    sent(payload,sizeof(payload));assert(!strcmp(payload,"{\"method\":2005,\"id\":2005}"));
    sent(payload,sizeof(payload));assert(!strcmp(payload,"{\"method\":1036,\"id\":1036}"));
    assert(!c.canvas_refresh_due&&!c.history_refresh_due);
    /* Unregistered clients publish nothing. */
    c.registered=0;assert(mqtt_set_auto_refill(&c,1)&&mqtt_generate_timelapse(&c,"picture/x"));
    c.history_requested=0;assert(mqtt_request_history(&c));c.registered=1;
    /* Discovery from a status delta leaves auto refill unknown: one Canvas request per session asks. */
    c.have_auto_refill=0;c.auto_refill_probed=0;c.canvas_discovery_complete=1;
    mqtt_tick(&c);sent(payload,sizeof(payload));assert(!strcmp(payload,"{\"method\":2005,\"id\":2005}")&&c.auto_refill_probed);
    mqtt_tick(&c);assert(recv(peer,payload,sizeof(payload),MSG_DONTWAIT)<0);
    /* A known setting becomes unknown on reconnect: a Canvas delta must not
     * suppress the new session's readback or leave the UI with stale state. */
    c.have_auto_refill=1;c.auto_refill=1;
    mqtt_close(&c);close(peer);assert(!c.have_auto_refill);
    assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));c.fd=pair[0];peer=pair[1];
    fcntl(c.fd,F_SETFL,fcntl(c.fd,F_GETFL,0)|O_NONBLOCK);
    c.connected=1;c.registered=1;c.last_app_ping=c.last_ping=time(NULL);
    say("elegoo/SN/api_status","{\"method\":6000,\"result\":{\"canvas_info\":{\"canvas_list\":[]}}}");
    assert(c.canvas_discovery_complete&&!c.have_auto_refill);
    mqtt_tick(&c);sent(payload,sizeof(payload));
    assert(!strcmp(payload,"{\"method\":2005,\"id\":2005}")&&c.auto_refill_probed);
    mqtt_tick(&c);assert(recv(peer,payload,sizeof(payload),MSG_DONTWAIT)<0);
    say("elegoo/SN/api_status","{\"method\":6000,\"result\":{\"canvas_info\":{\"auto_refill\":false,\"canvas_list\":[]}}}");
    assert(c.have_auto_refill&&!c.auto_refill);
    /* A partial own reply is released when the session closes. */
    char *again=history_reply(400);size_t length=strlen(again);
    size_t topic_len=strlen(own),remain=2+topic_len+length;unsigned char head[8];head[0]=0x30;size_t h=1+put_remaining(head+1,remain);
    assert(!send_all(peer,head,h));unsigned char topic[2]={(unsigned char)(topic_len>>8),(unsigned char)topic_len};
    assert(!send_all(peer,topic,2)&&!send_all(peer,own,topic_len)&&!send_all(peer,again,8192));drain();
    assert(c.large&&c.large_len<c.large_need);
    mqtt_close(&c);assert(!c.large&&!c.history_requested&&!c.timelapse_requested&&!c.auto_refill_probed&&c.history);
    free(again);free(c.history);close(peer);return 0;
}
