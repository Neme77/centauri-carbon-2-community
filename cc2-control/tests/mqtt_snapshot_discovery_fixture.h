#include <assert.h>
#include <time.h>
static time_t clock_now=1000;
static time_t test_time(time_t *out){if(out)*out=clock_now;return clock_now;}
#define time test_time
#include "../src/mqtt.c"

static mqtt_client c;
static int peer=-1;
static unsigned wire_snapshot,wire_canvas,wire_registration;

static void attach(void){
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    c.fd=pair[0];peer=pair[1];c.connected=1;c.discovery_ready_at=clock_now+15;
    assert(fcntl(peer,F_SETFL,fcntl(peer,F_GETFL,0)|O_NONBLOCK)>=0);
}
static void reset(void){
    if(peer>=0){mqtt_close(&c);close(peer);}
    mqtt_init(&c);strcpy(c.serial,"TEST");strcpy(c.client_id,"test-client");
    clock_now=1000;wire_snapshot=wire_canvas=wire_registration=0;attach();
    c.have_auto_refill=1;
}
static void incoming(const char *topic,const char *payload){
    unsigned char body[4096];size_t n=put_string(body,sizeof(body),topic);
    size_t length=strlen(payload);assert(n&&n+length<sizeof(body));
    memcpy(body+n,payload,length);handle_publish(&c,0x30,body,n+length);
}
static void registered(void){
    incoming("elegoo/TEST/test-client/register_response",
             "{\"client_id\":\"test-client\",\"error\":\"ok\"}");
    assert(c.registered);
}
static void canvas(void){
    incoming("elegoo/TEST/another-client/api_response",
             "{\"method\":2005,\"result\":{\"canvas_info\":{\"canvas_list\":[]}}}");
    assert(c.canvas_discovery_complete);c.have_auto_refill=1;
}
static void snapshot(void){
    incoming("elegoo/TEST/test-client/api_response",
             "{\"method\":1002,\"result\":{\"machine_status\":1}}");
    assert(c.snapshot_len&&c.info_responses);
}
/* Decode actual outgoing MQTT frames, not just internal attempt counters. */
static void drain(void){
    unsigned char data[16384];size_t used=0;
    for(;;){
        ssize_t n=recv(peer,data+used,sizeof(data)-used,0);
        if(n<0){assert(errno==EAGAIN||errno==EWOULDBLOCK);break;}
        assert(n>0);used+=(size_t)n;assert(used<sizeof(data));
    }
    for(size_t at=0;at<used;){
        unsigned char flags=data[at++],byte;size_t size=0,mult=1;
        do{assert(at<used&&mult<=128U*128U*128U);byte=data[at++];size+=(byte&127U)*mult;mult*=128U;}while(byte&128U);
        assert(at+size<=used);
        if((flags>>4)==3){
            assert(size>=2);size_t topic=((size_t)data[at]<<8)|data[at+1];
            size_t start=2+topic+((flags&6)?2:0);assert(start<=size);
            const char *payload=(const char *)data+at+start;size_t length=size-start;
            if(contains_text((const char *)data+at+2,topic,"/api_register"))wire_registration++;
            else if(contains_text(payload,length,"\"method\":1002"))wire_snapshot++;
            else if(contains_text(payload,length,"\"method\":2005"))wire_canvas++;
            else assert(contains_text(payload,length,"\"type\":\"PING\""));
        }
        at+=size;
    }
}
static void tick(unsigned seconds){
    clock_now+=seconds;c.last_ping=clock_now;c.last_app_ping=clock_now;
    c.app_ping_pending_since=0;mqtt_tick(&c);drain();
}
static void early_canvas(int original){
    reset();canvas();tick(15);
    assert(wire_registration==1&&!wire_snapshot&&!wire_canvas);
    registered();tick(0);
    assert(wire_registration==1&&!wire_canvas);
    if(original){assert(!wire_snapshot&&!c.snapshot_len);return;}
    assert(wire_snapshot==1&&c.automatic_snapshot_attempts==1);
    snapshot();tick(60);assert(wire_snapshot==1&&wire_registration==1&&!wire_canvas);
}
int main(int argc,char **argv){
    assert(argc==2);
    if(!strcmp(argv[1],"reproduce")){
        early_canvas(1);puts("PASS reproduced: passive Canvas suppresses general snapshot in original source");
        mqtt_close(&c);close(peer);return 0;
    }
    assert(!strcmp(argv[1],"fixed"));
    /* 1: exact physical failure, including received registration/Canvas replies. */
    early_canvas(0);
    /* 2: registered session still honors settle time; passive Canvas does not win. */
    reset();registered();canvas();tick(14);assert(!wire_snapshot);
    tick(1);assert(wire_snapshot==1&&!wire_canvas&&!wire_registration);
    /* 3: a general snapshot received first does not suppress Canvas discovery. */
    reset();registered();snapshot();tick(15);
    assert(!wire_snapshot&&wire_canvas==1&&!wire_registration);
    canvas();tick(60);assert(wire_canvas==1&&!wire_snapshot);
    /* 4: missing snapshot is retried despite a later Canvas reply, then stops. */
    reset();registered();tick(15);assert(wire_snapshot==1&&wire_canvas==1);
    canvas();tick(9);assert(wire_snapshot==1);tick(1);assert(wire_snapshot==2);
    snapshot();tick(60);assert(wire_snapshot==2&&wire_canvas==1);
    /* 5: retry budgets stay bounded; only explicit Sync rearms exhaustion. */
    reset();registered();canvas();tick(15);
    for(int i=0;i<30;i++)tick(60);
    assert(wire_snapshot==9&&c.automatic_snapshot_attempts==9&&!wire_registration&&!wire_canvas);
    assert(!mqtt_sync_canvas(&c));drain();assert(wire_canvas==1);
    tick(14);assert(wire_snapshot==9);tick(1);assert(wire_snapshot==10&&wire_canvas==1);
    /* 6: reconnect drops stale snapshot but preserves the automatic budget. */
    reset();registered();canvas();tick(15);snapshot();
    mqtt_close(&c);close(peer);peer=-1;
    assert(!c.snapshot_len&&!c.snapshot[0]&&c.automatic_snapshot_attempts==1);
    attach();canvas();tick(15);registered();tick(0);
    assert(wire_snapshot==2&&c.automatic_snapshot_attempts==2);
    /* 7: registration recovery still obtains the snapshot after passive Canvas. */
    snapshot();recover_registration(&c);assert(!c.registered&&!c.snapshot_len);
    canvas();tick(15);registered();tick(0);assert(wire_snapshot==3);
    /* 8: no registration response: no API requests, bounded registration only. */
    reset();canvas();tick(15);for(int i=0;i<30;i++)tick(60);
    assert(wire_registration==9&&!wire_snapshot&&!wire_canvas);
    mqtt_close(&c);close(peer);
    puts("PASS 8 actual MQTT snapshot/discovery scenarios");return 0;
}
