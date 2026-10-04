#include "../src/mqtt.c"
#include <assert.h>
#include <pthread.h>
static mqtt_client c;
static int peer, expected, refusal, disconnect_peer;
static void sent(char *out,size_t cap){
    unsigned char packet[4096];size_t rn=0,remain,n;
    assert(recv(peer,packet,1,0)==1&&packet[0]==0x32);
    do{assert(rn<4&&recv(peer,packet+1+rn,1,0)==1);rn++;}while(packet[rn]&128);
    assert(remaining_length(packet+1,rn,&remain,&n));assert(1+rn+remain<=sizeof(packet));
    assert(recv(peer,packet+1+rn,remain,MSG_WAITALL)==(ssize_t)remain);
    size_t topic=((size_t)packet[1+rn]<<8)|packet[2+rn],start=1+rn+2+topic+2,length=1+rn+remain-start;
    assert(length<cap);memcpy(out,packet+start,length);out[length]=0;
}
static void reply(unsigned long id,int error){
    const char *topic="elegoo/SN/test/api_response";char body[128];unsigned char packet[512];
    int n=snprintf(body,sizeof(body),"{\"method\":1019,\"id\":%lu,\"result\":{\"error_code\":%d}}",id,error);
    size_t t=strlen(topic),remain=2+t+(size_t)n;packet[0]=0x30;size_t h=1+put_remaining(packet+1,remain);
    packet[h]=(unsigned char)(t>>8);packet[h+1]=(unsigned char)t;memcpy(packet+h+2,topic,t);memcpy(packet+h+2+t,body,(size_t)n);
    assert(!send_all(peer,packet,h+remain));
}
static void *configure(void *unused){
    (void)unused;char payload[512];sent(payload,sizeof(payload));double id;
    assert(strstr(payload,"\"method\":1019")&&strstr(payload,expected?"\"delay_video\":true":"\"delay_video\":false"));
    assert(number_in(payload,strlen(payload),"id",&id));
    if(disconnect_peer){shutdown(peer,SHUT_RDWR);return NULL;}
    reply((unsigned long)id-1,0); /* A previous request's reply is never enough. */
    struct timespec wait={0,10000000};nanosleep(&wait,NULL);
    reply((unsigned long)id,refusal);return NULL;
}
static void connect_peer(void){
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));c.fd=pair[0];peer=pair[1];
    fcntl(c.fd,F_SETFL,fcntl(c.fd,F_GETFL,0)|O_NONBLOCK);c.connected=c.registered=1;
    strcpy(c.serial,"SN");strcpy(c.client_id,"test");
}
int main(void){
    mqtt_init(&c);connect_peer();char payload[1024];
    for(int enabled=0;enabled<=1;enabled++){
        assert(!mqtt_start_print(&c,"local","test.gcode",NULL,NULL,0,'B',0,enabled));sent(payload,sizeof(payload));
        assert(strstr(payload,enabled?"\"delay_video\":true":"\"delay_video\":false")&&strstr(payload,"\"print_layout\":\"B\""));
        expected=enabled;pthread_t thread;assert(!pthread_create(&thread,NULL,configure,NULL));
        assert(!mqtt_prepare_timelapse(&c,enabled));pthread_join(thread,NULL);
    }
    refusal=1009;pthread_t thread;assert(!pthread_create(&thread,NULL,configure,NULL));
    assert(mqtt_prepare_timelapse(&c,1)<0);pthread_join(thread,NULL);
    assert(c.print_config_error==1009&&c.reply_error_method==1019);
    refusal=0;assert(mqtt_prepare_timelapse(&c,0)<0);sent(payload,sizeof(payload)); /* timeout */
    expected=0;disconnect_peer=1;assert(!pthread_create(&thread,NULL,configure,NULL));
    assert(mqtt_prepare_timelapse(&c,0)<0);pthread_join(thread,NULL);
    mqtt_close(&c);close(peer);
    puts("PASS: print timelapse on/off, correlated calibrated-start acknowledgements, refusal, timeout and disconnect");return 0;
}
