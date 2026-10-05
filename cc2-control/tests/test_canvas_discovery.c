#include <time.h>
static time_t clock_now=1000;
static time_t test_time(time_t *out){if(out)*out=clock_now;return clock_now;}
#define time test_time
#include "../src/mqtt.c"
#include <assert.h>
static mqtt_client c;
static int peer;
static void attach(void){
 int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
 c.fd=pair[0];peer=pair[1];c.connected=1;c.last_ping=clock_now;c.last_app_ping=clock_now;
 c.discovery_ready_at=clock_now+15;
 fcntl(peer,F_SETFL,fcntl(peer,F_GETFL,0)|O_NONBLOCK);
}
static void drain(void){char data[8192];while(recv(peer,data,sizeof(data),0)>0){} }
int main(void){
 assert(discovery_due(5,1000,1010));
 assert(!discovery_due(6,1000,1059)&&discovery_due(6,1000,1060));
 assert(!discovery_due(9,0,10000));
 mqtt_init(&c);strcpy(c.serial,"TEST");attach();c.registered=1;
 mqtt_tick(&c);assert(!c.automatic_canvas_attempts&&!c.automatic_registration_attempts);
 clock_now+=15;mqtt_tick(&c);drain();
 assert(c.automatic_canvas_attempts==1&&c.automatic_snapshot_attempts==1&&c.automatic_registration_attempts==1);
 clock_now+=9;mqtt_tick(&c);drain();assert(c.automatic_canvas_attempts==1);
 clock_now++;mqtt_tick(&c);drain();assert(c.automatic_canvas_attempts==2);
 /* Reconnect keeps budgets but still delays each new session. */
 mqtt_close(&c);close(peer);attach();c.registered=1;
 mqtt_tick(&c);assert(c.automatic_canvas_attempts==2);
 clock_now+=15;
 for(int i=0;i<30;i++){mqtt_tick(&c);drain();clock_now+=60;}
 assert(c.automatic_canvas_attempts==9&&c.automatic_snapshot_attempts==9&&c.automatic_registration_attempts==9);
 char status[512];mqtt_discovery_diagnostic(&c,status,sizeof(status));assert(strstr(status,"exhausted; use Sync"));
 /* Exhaustion must survive another transport reset. */
 mqtt_close(&c);close(peer);attach();c.registered=1;clock_now+=60;
 mqtt_tick(&c);drain();assert(c.automatic_canvas_attempts==9&&!c.canvas_request_attempts);
 assert(!mqtt_sync_canvas(&c));drain();assert(c.canvas_request_attempts==1&&!c.automatic_canvas_attempts);
 mqtt_tick(&c);drain();assert(!c.automatic_canvas_attempts);
 clock_now+=15;mqtt_tick(&c);drain();assert(c.automatic_canvas_attempts==1);
 /* A passive Canvas snapshot ends automatic discovery; no budget reset needed. */
 c.canvas_discovery_complete=1;c.have_auto_refill=1;
 clock_now+=60;mqtt_tick(&c);drain();assert(c.automatic_canvas_attempts==1);
 /* Sync can rearm an exhausted registration without an application session. */
 c.registered=0;c.canvas_discovery_complete=0;c.automatic_registration_attempts=9;
 assert(!mqtt_sync_canvas(&c));clock_now+=15;mqtt_tick(&c);drain();assert(c.automatic_registration_attempts==1);
 /* Another client's Canvas reply completes discovery during the settle delay of an
  * unregistered session: registration still follows, Canvas requests do not. */
 mqtt_close(&c);close(peer);mqtt_init(&c);strcpy(c.serial,"TEST");attach();
 c.canvas_discovery_complete=1;
 clock_now+=15;mqtt_tick(&c);drain();
 assert(c.automatic_registration_attempts==1&&!c.automatic_snapshot_attempts&&!c.automatic_canvas_attempts);
 mqtt_discovery_diagnostic(&c,status,sizeof(status));assert(!strstr(status,"Canvas discovery: complete"));
 clock_now+=10;mqtt_tick(&c);drain();assert(c.automatic_registration_attempts==2);
 c.registered=1;clock_now+=60;mqtt_tick(&c);drain();
 assert(c.automatic_registration_attempts==2&&!c.automatic_snapshot_attempts&&!c.automatic_canvas_attempts);
 mqtt_discovery_diagnostic(&c,status,sizeof(status));assert(strstr(status,"Canvas discovery: complete"));
 mqtt_close(&c);close(peer);assert(mqtt_sync_canvas(&c)==-1);
 return 0;
}
