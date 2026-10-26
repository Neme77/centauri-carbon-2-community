#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

static void response(char *out,size_t cap){
 int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));uds_response(pair[0]);
 ssize_t n=recv(pair[1],out,cap-1,0);assert(n>0);out[n]=0;close(pair[0]);close(pair[1]);
}
static void message(const char *json){assert(uds_message(&telemetry,json,strlen(json)));}
int main(void){
 uds_init(&telemetry);char json[4096];response(json,sizeof(json));
 assert(strstr(json,"\"homed_axes\":null,\"light\":null"));
 int peers[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,peers));telemetry.fd=peers[0];
 message("{\"id\":11,\"result\":{\"eventtime\":1,\"status\":{\"toolhead\":{\"homed_axes\":\"xyz\"},\"output_pin led_pin\":{\"value\":1}}}}");
 response(json,sizeof(json));assert(strstr(json,"\"homed_axes\":\"xyz\",\"light\":true"));
 message("{\"method\":\"cc2_status\",\"params\":{\"eventtime\":2,\"status\":{\"toolhead\":{\"homed_axes\":\"\"},\"output_pin led_pin\":{\"value\":0}}}}");
 response(json,sizeof(json));assert(strstr(json,"\"homed_axes\":\"\",\"light\":false"));
 telemetry.last_rx.tv_sec-=6;response(json,sizeof(json));
 assert(strstr(json,"\"homed_axes\":null,\"light\":null"));
 uds_close(&telemetry);close(peers[1]);response(json,sizeof(json));
 assert(strstr(json,"\"homed_axes\":null,\"light\":null"));
 puts("PASS UDS diagnostics: missing, homed/unhomed, light on/off, stale and disconnect");
 return 0;
}
