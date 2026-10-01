#include "../src/mqtt.c"
#include <assert.h>

static size_t frame(unsigned char *packet,const char *topic,const char *json){
 unsigned char body[2048];
 size_t n=put_string(body,sizeof(body),topic),length=strlen(json);
 assert(n&&n+length<sizeof(body));
 memcpy(body+n,json,length);n+=length;
 packet[0]=0x30;
 size_t header=1+put_remaining(packet+1,n);
 memcpy(packet+header,body,n);
 return header+n;
}

int main(void){
 mqtt_client c; mqtt_init(&c);
 int peers[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,peers)==0);
 c.fd=peers[0];c.connected=1;
 unsigned char packet[4096];
 size_t n=frame(packet,"elegoo/test/api_status",
  "{\"machine_status\":{\"status\":2},\"extruder\":{\"temperature\":210},\"print_status\":{\"state\":\"printing\",\"filename\":\"test.gcode\"}}");
 /* Actual socket path preserves partial MQTT frames. */
 assert(send_all(peers[1],packet,n-2)==0);assert(mqtt_process(&c)==0);
 assert(c.received_publishes==0);
 assert(send_all(peers[1],packet+n-2,2)==0);assert(mqtt_process(&c)==0);
 assert(c.received_publishes==1&&c.messages==1&&c.machine_status==2);
 assert(c.extruder_temp==210&&!strcmp(c.filename,"test.gcode"));
 size_t diagnostic=c.diagnostic_len;
 c.last_message=123;
 const char *fake="{\"machine_status\":{\"status\":1},\"canvas_info\":{\"canvas_list\":[]}}";
 n=frame(packet,"elegoo/test/client/api_request",fake);
 n+=frame(packet+n,"elegoo/test/api_register","{}");
 assert(send_all(peers[1],packet,n)==0);assert(mqtt_process(&c)==0);
 assert(c.received_publishes==3&&c.skipped_requests==2);
 assert(c.skipped_request_bytes==strlen(fake)+2);
 assert(c.messages==1&&c.last_message==123&&c.machine_status==2);
 assert(c.diagnostic_len==diagnostic&&!c.canvas_discovery_complete);
 /* Similar names must not be mistaken for command topics. */
 n=frame(packet,"elegoo/test/client/api_request_extra","{\"machine_status\":{\"status\":1}}");
 assert(send_all(peers[1],packet,n)==0);assert(mqtt_process(&c)==0);
 assert(c.machine_status==1&&c.skipped_requests==2&&c.messages==2);
 n=frame(packet,"elegoo/test/register_response","{\"error\":\"ok\"}");
 n+=frame(packet+n,"elegoo/test/client/api_response",
  "{\"method\":1002,\"extruder\":{\"temperature\":205},\"canvas_info\":{\"canvas_list\":[]}}");
 assert(send_all(peers[1],packet,n)==0);assert(mqtt_process(&c)==0);
 assert(c.registered&&c.snapshot_len&&c.info_responses==1);
 /* The reused object lookups keep temperature/target and safety flags independent. */
 const char *temperatures="{\"extruder\":{\"target\":215,\"filament_detect_enable\":1,\"filament_detected\":1},\"heater_bed\":{\"temperature\":60,\"target\":65},\"ztemperature_sensor\":{\"temperature\":30}}";
 n=frame(packet,"elegoo/test/api_status",temperatures);
 assert(send_all(peers[1],packet,n)==0);assert(mqtt_process(&c)==0);
 assert(c.extruder_temp==205&&c.extruder_target==215&&c.have_extruder_target);
 assert(c.filament_detect_enabled&&c.filament_detected);
 assert(c.bed_temp==60&&c.bed_target==65&&c.chamber_temp==30);
 assert(c.have_bed_temp&&c.have_bed_target&&c.have_chamber_temp);
 assert(c.canvas_discovery_complete&&c.canvas_snapshot_len&&c.extruder_temp==205);
 assert(c.diagnostic_len>diagnostic&&c.messages==5);
 /* Lifetime counters survive reconnect, like UDS diagnostics. */
 mqtt_close(&c);assert(c.received_publishes==7&&c.skipped_requests==2);
 close(peers[1]);return 0;
}
