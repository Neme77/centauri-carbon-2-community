#include "../src/uds.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

static const char initial[]="{\"id\":11,\"result\":{\"eventtime\":1,\"status\":{\"extruder\":{\"temperature\":24,\"target\":0},\"print_stats\":{\"state\":\"standby\"}}}}";
static const char update[]="{\"method\":\"cc2_status\",\"params\":{\"eventtime\":2,\"status\":{\"extruder\":{\"temperature\":25}}}}\003";

static void setup(uds_client *c,int pair[2]){
 assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
 /* Intentionally leave the descriptor blocking: deadline receive must itself
  * be nonblocking, including after consuming the last queued frame. */
 uds_init(c);c->fd=pair[0];
 assert(uds_message(c,initial,strlen(initial))&&uds_fresh(c));
 c->sent=65535;c->last_rx.tv_sec-=6;
 clock_gettime(CLOCK_MONOTONIC,&c->retry);
}
static void queue(int fd,const char *frame){
 size_t length=strlen(frame);assert(send(fd,frame,length,0)==(ssize_t)length);
}
int main(void){
 uds_client c;int pair[2];double value;
 setup(&c,pair);queue(pair[1],update);
 uds_tick(&c,"/nonexistent-uds-test");
 assert(c.fd==pair[0]&&c.disconnects==0&&c.messages==2&&uds_fresh(&c));
 assert(uds_value(&c,U_ET,&value)&&value==25);
 uds_tick(&c,"/nonexistent-uds-test");assert(c.messages==2);
 uds_close(&c);close(pair[1]);

 setup(&c,pair);uds_tick(&c,"/nonexistent-uds-test");
 assert(c.fd<0&&c.disconnects==1&&!strcmp(c.last_disconnect,"receive_timeout")&&!uds_fresh(&c));close(pair[1]);

 const char *unusable[]={
  "{\"method\":\"other_notification\",\"params\":{}}\003",
  "{\"id\":11,\"result\":",
  "{\"method\":\"cc2_report\",\"params\":{\"response\":\"ok\"}}\003"
 };
 for(size_t i=0;i<sizeof(unusable)/sizeof(unusable[0]);i++){
  setup(&c,pair);queue(pair[1],unusable[i]);uds_tick(&c,"/nonexistent-uds-test");
  assert(c.fd<0&&c.disconnects==1&&!strcmp(c.last_disconnect,"receive_timeout")&&!uds_fresh(&c));close(pair[1]);
 }
 setup(&c,pair);queue(pair[1],"{broken\003");uds_tick(&c,"/nonexistent-uds-test");
 assert(c.fd<0&&c.disconnects==1&&!strcmp(c.last_disconnect,"invalid_json"));close(pair[1]);

 setup(&c,pair);close(pair[1]);uds_tick(&c,"/nonexistent-uds-test");
 assert(c.fd<0&&c.disconnects==1&&!strcmp(c.last_disconnect,"peer_closed"));
 puts("PASS: queued status consumed once; silent, ignored, partial, report-only, malformed and closed peers remain rejected");
 return 0;
}
