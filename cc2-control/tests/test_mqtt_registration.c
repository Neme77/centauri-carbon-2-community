#include <time.h>
static time_t clock_now=1000;
static time_t test_time(time_t *out){if(out)*out=clock_now;return clock_now;}
#define time test_time
#include "../src/mqtt.c"
#include <assert.h>
static mqtt_client c;
static int peer;
static void publish(const char *topic,const char *body){
 unsigned char frame[512];size_t n=put_string(frame,sizeof(frame),topic),len=strlen(body);
 assert(n&&n+len<sizeof(frame));memcpy(frame+n,body,len);
 handle_publish(&c,0,frame,n+len);
}
static void drain(void){char data[8192];while(recv(peer,data,sizeof(data),0)>0){} }
int main(void){
 int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
 mqtt_init(&c);c.fd=pair[0];peer=pair[1];c.connected=c.registered=1;
 strcpy(c.serial,"SN");strcpy(c.client_id,"test");
 fcntl(peer,F_SETFL,fcntl(peer,F_GETFL,0)|O_NONBLOCK);
 c.last_ping=clock_now;c.canvas_discovery_complete=1;c.have_auto_refill=1;
 mqtt_tick(&c);drain();assert(c.app_ping_pending_since==1000);
 clock_now+=20;mqtt_tick(&c);drain();assert(c.app_ping_pending_since==1000);
 c.last_message=1000;c.have_machine_status=1;c.machine_status=2;
 unsigned long messages=c.messages;
 publish("elegoo/SN/other/api_response","{\"type\":\"PONG\"}");
 publish("elegoo/OTHER/test/api_response","{\"type\":\"PONG\"}");
 /* Foreign heartbeats neither satisfy the watchdog nor refresh our link. */
 assert(c.app_ping_pending_since==1000&&c.last_message==1000);
 publish("elegoo/SN/test/api_response","{\"type\":\"PONG\"}");
 assert(!c.app_ping_pending_since&&c.last_message==clock_now&&c.messages==messages+1);
 assert(c.have_machine_status&&c.machine_status==2);
 /* A quiet printer still answers its own PINGs. Beyond the dashboard's
  * 30 s expiry, its heartbeat keeps the link fresh without discovery loops. */
 for(int i=0;i<6;i++){
  clock_now+=10;mqtt_tick(&c);drain();
  publish("elegoo/SN/test/api_response","{\"type\":\"PONG\"}");
  assert(c.connected&&c.registered&&c.last_message==clock_now&&!c.registration_recoveries);
 }
 clock_now+=10;mqtt_tick(&c);drain();assert(c.registered&&c.app_ping_pending_since==clock_now);
 c.automatic_registration_attempts=c.automatic_snapshot_attempts=c.automatic_canvas_attempts=9;
 clock_now+=30;mqtt_tick(&c);drain();
 assert(c.connected&&!c.registered&&c.registration_recoveries==1);
 assert(!c.automatic_registration_attempts&&!c.automatic_snapshot_attempts&&!c.automatic_canvas_attempts);
 assert(!c.canvas_discovery_complete&&!c.snapshot_len&&!c.app_ping_pending_since);
 clock_now+=14;mqtt_tick(&c);drain();assert(!c.automatic_registration_attempts);
 clock_now++;mqtt_tick(&c);drain();assert(c.automatic_registration_attempts==1);
 publish("elegoo/SN/test/register_response","{\"client_id\":\"other\",\"error\":\"ok\"}");
 publish("elegoo/OTHER/test/register_response","{\"client_id\":\"test\",\"error\":\"ok\"}");
 assert(!c.registered);
 /* Remaining retries are bounded even if the printer never returns. */
 for(int i=0;i<20;i++){clock_now+=60;mqtt_tick(&c);drain();}
 assert(c.registration_recoveries==1&&c.automatic_registration_attempts==9);
 publish("elegoo/SN/test/register_response","{\"client_id\":\"test\",\"error\":\"ok\"}");
 assert(c.registered);
 mqtt_tick(&c);drain();assert(c.automatic_snapshot_attempts==1&&c.automatic_canvas_attempts==1);
 publish("elegoo/SN/test/api_response","{\"type\":\"PONG\"}");
 assert(!c.app_ping_pending_since);
 mqtt_close(&c);close(peer);assert(!c.app_ping_pending_since);
 puts("PASS: own PONG, stale registration, delayed bounded recovery, foreign registration replies and recovered discovery");
 return 0;
}
