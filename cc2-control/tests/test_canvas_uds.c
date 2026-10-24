#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

static char initial[8192];
static void load_fixture(void){
 const char *slash=strrchr(__FILE__,'/');assert(slash);
 char path[4096];int n=snprintf(path,sizeof(path),"%.*s/fixtures/canvas_uds_initial.json",(int)(slash-__FILE__),__FILE__);
 assert(n>0&&(size_t)n<sizeof(path));FILE *file=fopen(path,"rb");assert(file);
 size_t count=fread(initial,1,sizeof(initial)-1,file);assert(count&&feof(file));initial[count]=0;fclose(file);
 /* The transport delivers the JSON frame without trailing newline. */
 while(count&&isspace((unsigned char)initial[count-1]))initial[--count]=0;
}
static void feed(const char *text){assert(uds_message(&telemetry,text,strlen(text)));}
static void read_canvas(mqtt_client *m,char *out,size_t cap){
 int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));uds_motion_light(&telemetry,m);canvas_response(pair[0],m);
 ssize_t n=recv(pair[1],out,cap-1,0);assert(n>0);out[n]=0;close(pair[0]);close(pair[1]);
}
static void expect_color(const char *color){
 char json[UDS_CANVAS_JSON_MAX];assert(uds_canvas_status(&telemetry,json,sizeof(json))>0);assert(strstr(json,color));
}
static void subscription_test(int enabled){
 uds_client c;uds_init(&c);c.canvas_enabled=enabled;
 int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));c.fd=pair[0];
 clock_gettime(CLOCK_MONOTONIC,&c.last_rx);c.last_ping=c.last_rx;
 uds_tick(&c,"/unused");char request[4096];ssize_t n=recv(pair[1],request,sizeof(request)-1,0);
 assert(n>0);request[n]=0;
 assert((strstr(request,"\"canvas_lite\"")!=NULL)==enabled);
 assert(strstr(request,"\"filament_switch_sensor filament_sensor\"")&&strstr(request,"\"bed_mesh\""));
 uds_close(&c);close(pair[1]);
}
int main(void){
 subscription_test(0);subscription_test(1);
 load_fixture();mqtt_client m;mqtt_init(&m);
 strcpy(m.canvas_snapshot,"{\"result\":{\"canvas_info\":{\"canvas_list\":[]}}}");m.canvas_snapshot_len=strlen(m.canvas_snapshot);
 m.have_auto_refill=1;m.auto_refill=0;uds_init(&telemetry);telemetry.canvas_enabled=1;
 int peers[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,peers));telemetry.fd=peers[0];
 char response[6000];read_canvas(&m,response,sizeof(response));assert(strstr(response,"\"source\":\"mqtt\""));
 feed(initial);assert(uds_canvas_available(&telemetry));
 read_canvas(&m,response,sizeof(response));assert(strstr(response,"\"source\":\"uds\""));
 assert(strstr(response,"\"brand\":\"ELEGOO\"")&&strstr(response,"\"filament_color\":\"#F72221\""));
 assert(strstr(response,"\"min_nozzle_temp\":230,\"max_nozzle_temp\":260"));
 assert(!strstr(response,"0xEEEEEEEE")&&!strstr(response,"weight"));
 /* The normalized view must also be compatible with Orca's existing lane parser. */
 char lanes[4096];json_builder builder={lanes,0,sizeof(lanes),0};lane_data_build(&m,&builder);
 assert(!builder.failed&&strstr(lanes,"\"color\":\"#F72221\",\"nozzle_temp\":260"));
 feed("{\"method\":\"cc2_status\",\"params\":{\"eventtime\":2,\"status\":{\"canvas_dev\":{\"auto_plug_in_enable\":1,\"canvas_lite\":{\"channels2\":{\"filament\":{\"color\":\"0x48A7FA\"}}}}}}}");
 assert(telemetry.canvas.auto_refill);expect_color("#48A7FA");
 assert(telemetry.canvas.slots[2].min_temp==230&&!strcmp(telemetry.canvas.slots[2].brand,"\"ELEGOO\""));
 assert(!uds_message(&telemetry,initial+0,0)); /* Empty frame cannot mutate the cache. */
 assert(uds_message(&telemetry,initial,strlen(initial))); /* Older initial fills only missing fields. */
 expect_color("#48A7FA");assert(telemetry.canvas.auto_refill);
 /* A newer delta with an invalid color disables UDS until a complete view arrives. */
 feed("{\"method\":\"cc2_status\",\"params\":{\"eventtime\":3,\"status\":{\"canvas_dev\":{\"canvas_lite\":{\"channels2\":{\"filament\":{\"color\":\"bad\"}}}}}}}");
 assert(!uds_canvas_available(&telemetry));read_canvas(&m,response,sizeof(response));assert(strstr(response,"\"source\":\"mqtt\""));
 /* Reconnect forgets the entire Canvas view, then a real complete initial restores it. */
 uds_close(&telemetry);close(peers[1]);assert(!uds_canvas_available(&telemetry));
 assert(!socketpair(AF_UNIX,SOCK_STREAM,0,peers));telemetry.fd=peers[0];telemetry.messages=0;
 feed(initial);assert(uds_canvas_available(&telemetry));
 telemetry.last_rx.tv_sec-=6;assert(!uds_canvas_available(&telemetry));
 read_canvas(&m,response,sizeof(response));assert(strstr(response,"\"source\":\"mqtt\""));
 uds_close(&telemetry);close(peers[1]);
 /* Notification preceding the initial: retain the new color, fill all missing fields. */
 uds_init(&telemetry);telemetry.canvas_enabled=1;assert(!socketpair(AF_UNIX,SOCK_STREAM,0,peers));telemetry.fd=peers[0];
 feed("{\"method\":\"cc2_status\",\"params\":{\"eventtime\":2,\"status\":{\"canvas_dev\":{\"canvas_lite\":{\"channels2\":{\"filament\":{\"color\":\"#ABCDEF\"}}}}}}}");
 assert(!uds_canvas_available(&telemetry));feed(initial);assert(uds_canvas_available(&telemetry));expect_color("#ABCDEF");
 char tiny[8];assert(uds_canvas_status(&telemetry,tiny,sizeof(tiny))<0);
 /* Complete metadata must not weaken command eligibility or reboot recovery. */
 m.connected=m.registered=m.have_machine_status=1;m.machine_status=1;m.last_message=time(NULL);
 feed("{\"method\":\"cc2_status\",\"params\":{\"eventtime\":4,\"status\":{\"filament_switch_sensor filament_sensor\":{\"filament_detected\":false},\"print_stats\":{\"state\":\"standby\"}}}}");
 read_canvas(&m,response,sizeof(response));
 assert(canvas_slot_policy(&m,1).load&&!canvas_slot_policy(&m,0).load);
 char script[700],reason[256];
 assert(control_build_script("canvas:load:1",&m,script,sizeof(script),reason,sizeof(reason)));
 assert(!strcmp(script,"CANVAS_LOAD_FILAMENT CHANNEL=1"));
 feed("{\"method\":\"cc2_status\",\"params\":{\"eventtime\":5,\"status\":{\"filament_switch_sensor filament_sensor\":{\"filament_detected\":true}}}}");
 read_canvas(&m,response,sizeof(response));
 assert(!canvas_slot_policy(&m,1).load&&!canvas_slot_policy(&m,1).unload);
 assert(canvas_recovery_load_allowed(&m,1)&&!canvas_recovery_load_allowed(&m,0));
 m.machine_status=2;assert(!canvas_recovery_load_allowed(&m,1));
 m.machine_status=1;m.last_message-=60;assert(!canvas_recovery_load_allowed(&m,1));m.last_message=time(NULL);
 /* Disabling the experimental source restores the exact untouched MQTT cache. */
 char original_mqtt[sizeof(m.canvas_snapshot)];memcpy(original_mqtt,m.canvas_snapshot,sizeof(original_mqtt));
 telemetry.canvas_enabled=0;read_canvas(&m,response,sizeof(response));
 assert(strstr(response,"\"source\":\"mqtt\"")&&!m.canvas_uds_ready);
 assert(!memcmp(original_mqtt,m.canvas_snapshot,sizeof(original_mqtt)));
 telemetry.canvas_enabled=1;read_canvas(&m,response,sizeof(response));assert(m.canvas_uds_ready);
 feed("{\"method\":\"cc2_status\",\"params\":{\"eventtime\":6,\"status\":{\"canvas_dev\":{\"canvas_lite\":{\"channels2\":{\"filament\":{\"color\":\"bad\"}}}}}}}");
 assert(!uds_canvas_available(&telemetry));feed(initial);assert(!uds_canvas_available(&telemetry));
 uds_close(&telemetry);close(peers[1]);
 puts("PASS Canvas UDS units, partial deltas, ordering, HTTP/Orca rendering, stale/reconnect and MQTT fallback");
 return 0;
}
