#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

static int light_peer, light_mode, light_power;
static mqtt_client light_client;
static void light_reply(const char *client,unsigned long id,int code){
    char topic[128],body[192];unsigned char packet[512];
    snprintf(topic,sizeof(topic),"elegoo/SN/%s/api_response",client);
    snprintf(body,sizeof(body),"{\"method\":1029,\"id\":%lu,\"result\":{\"error_code\":%d}}",id,code);
    size_t t=strlen(topic),b=strlen(body),remaining=2+t+b;
    packet[0]=0x30;size_t h=1;
    do{unsigned char digit=remaining%128;remaining/=128;if(remaining)digit|=128;packet[h++]=digit;}while(remaining);
    packet[h++]=(unsigned char)(t>>8);packet[h++]=(unsigned char)t;
    memcpy(packet+h,topic,t);h+=t;memcpy(packet+h,body,b);h+=b;
    assert(send(light_peer,packet,h,0)==(ssize_t)h);
}
static void *light_printer(void *unused){
    (void)unused;unsigned char packet[1024];size_t remaining=0,multiplier=1,h=1;
    assert(recv(light_peer,packet,1,MSG_WAITALL)==1&&packet[0]==0x32);
    do{assert(recv(light_peer,packet+h,1,MSG_WAITALL)==1);remaining+=(packet[h]&127)*multiplier;multiplier*=128;}while(packet[h++]&128);
    assert(h+remaining<sizeof(packet));
    assert(recv(light_peer,packet+h,remaining,MSG_WAITALL)==(ssize_t)remaining);
    size_t t=((size_t)packet[h]<<8)|packet[h+1],start=h+2+t+2;
    assert(t==strlen("elegoo/SN/test/api_request")&&!memcmp(packet+h+2,"elegoo/SN/test/api_request",t));
    packet[h+remaining]=0;const char *payload=(char *)packet+start;
    unsigned long id;const char *id_text=strstr(payload,"\"id\":");
    assert(strstr(payload,"\"method\":1029")&&id_text&&sscanf(id_text,"\"id\":%lu",&id)==1);
    assert(strstr(payload,light_power?"\"power\":1":"\"power\":0")&&!strstr(payload,"SET_PIN"));
    if(light_mode==2)return NULL; /* Missing acknowledgement. */
    if(light_mode==3){shutdown(light_peer,SHUT_RDWR);return NULL;}
    light_reply("other",(unsigned long)id,0);
    light_reply("test",(unsigned long)id-1,0);
    if(light_mode==4)return NULL; /* Foreign/stale acknowledgements only. */
    light_reply("test",(unsigned long)id,light_mode==1?1009:0);
    return NULL;
}
static void light_connect(void){
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    mqtt_init(&light_client);light_client.fd=pair[0];light_peer=pair[1];
    fcntl(light_client.fd,F_SETFL,fcntl(light_client.fd,F_GETFL,0)|O_NONBLOCK);
    light_client.connected=light_client.registered=1;
    strcpy(light_client.serial,"SN");strcpy(light_client.client_id,"test");
}
static void action(const char *text,int success){
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    console_state console={0};assert(!pthread_mutex_init(&console.lock,NULL));
    control_response(pair[0],&console,&light_client,text,strlen(text));
    char response[1024];ssize_t n=recv(pair[1],response,sizeof(response)-1,0);assert(n>0);response[n]=0;
    assert(strstr(response,success?"202 Accepted":"400 Bad Request"));
    assert(!console.generation&&!console.busy);pthread_mutex_destroy(&console.lock);
    close(pair[0]);close(pair[1]);
}
int main(void){
    light_connect();pthread_t thread;
    for(light_power=0;light_power<=1;light_power++){
        light_mode=0;assert(!pthread_create(&thread,NULL,light_printer,NULL));
        action(light_power?"light:on":"light:off",1);pthread_join(thread,NULL);
        assert(light_client.light_error==0);
    }
    action("light:off junk",0);action("light:toggle",0);
    light_power=0;
    for(light_mode=1;light_mode<=4;light_mode++){
        light_client.registered=1;
        unsigned long recoveries=light_client.registration_recoveries;
        assert(!pthread_create(&thread,NULL,light_printer,NULL));
        assert(mqtt_set_light(&light_client,0)<0);pthread_join(thread,NULL);
        if(light_mode==1)assert(light_client.light_error==1009&&light_client.registered&&light_client.registration_recoveries==recoveries);
        if(light_mode==2||light_mode==4)assert(!light_client.registered&&light_client.registration_recoveries==recoveries+1);
        if(light_mode==3){mqtt_close(&light_client);close(light_peer);light_connect();}
    }
    assert(mqtt_set_light(&light_client,2)<0);
    light_client.registered=0;assert(mqtt_set_light(&light_client,0)<0);
    assert(mqtt_set_light(NULL,0)<0);
    mqtt_close(&light_client);close(light_peer);
    puts("PASS: native light HTTP route, on/off, strict actions, own correlated ACK, refusal, timeout, disconnect and unavailable connection");
    return 0;
}
