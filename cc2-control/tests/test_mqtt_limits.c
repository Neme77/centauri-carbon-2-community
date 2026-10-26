#include "../src/mqtt.c"
#include <assert.h>
static unsigned char packet[65536];
static size_t message(const char *topic,const char *prefix,size_t payload){
 unsigned char body[65500];size_t n=put_string(body,sizeof(body),topic);assert(n);
 memset(body+n,' ',payload);memcpy(body+n,prefix,strlen(prefix));n+=payload;
 packet[0]=0x30;size_t h=1+put_remaining(packet+1,n);memcpy(packet+h,body,n);return h+n;
}
int main(void){
 mqtt_client c;mqtt_init(&c);int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));c.fd=pair[0];c.connected=1;
 size_t n=message("elegoo/test/api_request","{}",40000);assert(!send_all(pair[1],packet,n));
 assert(!mqtt_process(&c));assert(c.discard_remaining&&c.oversized_packets==1);
 while(c.discard_remaining){assert(!mqtt_process(&c));}
 assert(c.fd==pair[0]&&c.connected);
 n=message("elegoo/test/api_status","{\"machine_status\":{\"status\":1}}",60);assert(!send_all(pair[1],packet,n));assert(!mqtt_process(&c));assert(c.machine_status==1);
 strcpy(c.canvas_snapshot,"{}");c.canvas_snapshot_len=2;
 n=message("elegoo/test/api_status","{\"canvas_info\":{\"canvas_list\":[]}}",10000);assert(!send_all(pair[1],packet,n));assert(!mqtt_process(&c));
 assert(c.oversized_snapshots==1&&c.canvas_snapshot_len==2&&!strcmp(c.canvas_snapshot,"{}"));
 strcpy(c.snapshot,"{}");c.snapshot_len=2;
 n=message("elegoo/test/api_response","{\"method\":1002}",13000);assert(!send_all(pair[1],packet,n));assert(!mqtt_process(&c));assert(c.oversized_snapshots==2&&c.snapshot_len==2);
 mqtt_close(&c);assert(!c.discard_remaining);close(pair[1]);return 0;
}
