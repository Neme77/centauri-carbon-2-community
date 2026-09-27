#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>
#include <sys/socket.h>
static char response[16384];
static int get(mqtt_client *mqtt,const char *path){
  int pair[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);
  char request[512];int n=snprintf(request,sizeof(request),"GET %s HTTP/1.1\r\nHost: localhost\r\n\r\n",path);
  assert(send_all(pair[1],request,(size_t)n)==0);
  handle_client(pair[0],"./web",mqtt,NULL);close(pair[0]);
  size_t used=0;ssize_t got;
  while((got=recv(pair[1],response+used,sizeof(response)-1-used,0))>0)used+=(size_t)got;
  response[used]=0;close(pair[1]);
  return atoi(strchr(response,' ')+1);
}
static const char *body(void){const char *b=strstr(response,"\r\n\r\n");assert(b);return b+4;}
static void snapshot(mqtt_client *mqtt,const char *json){
  snprintf(mqtt->canvas_snapshot,sizeof(mqtt->canvas_snapshot),"%s",json);
  mqtt->canvas_snapshot_len=strlen(json);
}
static const char *LIVE=
  "{\"id\":2,\"method\":2005,\"result\":{\"canvas_info\":{\"active_canvas_id\":0,\"active_tray_id\":-1,\"auto_refill\":false,"
  "\"canvas_list\":[{\"canvas_id\":0,\"connected\":1,\"tray_list\":["
  "{\"brand\":\"ELEGOO\",\"filament_code\":\"0x0100\",\"filament_color\":\"#000000\",\"filament_name\":\"PETG\",\"filament_type\":\"PETG\",\"max_nozzle_temp\":260,\"min_nozzle_temp\":230,\"status\":1,\"tray_id\":0},"
  "{\"brand\":\"ELEGOO\",\"filament_code\":\"0x0100\",\"filament_color\":\"#F72221\",\"filament_name\":\"PETG\",\"filament_type\":\"PETG\",\"max_nozzle_temp\":260,\"min_nozzle_temp\":230,\"status\":1,\"tray_id\":1},"
  "{\"brand\":\"ELEGOO\",\"filament_code\":\"0x0100\",\"filament_color\":\"#FFF242\",\"filament_name\":\"PETG\",\"filament_type\":\"PETG\",\"max_nozzle_temp\":260,\"min_nozzle_temp\":230,\"status\":1,\"tray_id\":2},"
  "{\"brand\":\"ELEGOO\",\"filament_code\":\"0x0100\",\"filament_color\":\"#FFFFFF\",\"filament_name\":\"PETG\",\"filament_type\":\"PETG\",\"max_nozzle_temp\":260,\"min_nozzle_temp\":230,\"status\":1,\"tray_id\":3}"
  "]}]},\"error_code\":0}}";
int main(void){
 mqtt_client mqtt;memset(&mqtt,0,sizeof(mqtt));mqtt.connected=mqtt.registered=1;
 assert(get(&mqtt,"/server/info")==200);
 assert(strstr(body(),"\"result\":{")&&strstr(body(),"\"klippy_state\":\"ready\""));
 puts("PASS /server/info answers with a Moonraker result object");
 assert(get(&mqtt,"/server/database/item?namespace=lane_data")==200);
 assert(strcmp(body(),"{\"result\":{\"namespace\":\"lane_data\",\"value\":{}}}\n")==0);
 puts("PASS lane_data is empty before the first Canvas snapshot");
 snapshot(&mqtt,LIVE);
 assert(get(&mqtt,"/server/database/item?namespace=lane_data")==200);
 assert(strstr(body(),"\"lane1\":{\"lane\":\"0\",\"material\":\"PETG\",\"color\":\"#000000\",\"nozzle_temp\":260,\"bed_temp\":0}"));
 assert(strstr(body(),"\"lane2\":{\"lane\":\"1\",\"material\":\"PETG\",\"color\":\"#F72221\""));
 assert(strstr(body(),"\"lane3\":{\"lane\":\"2\",\"material\":\"PETG\",\"color\":\"#FFF242\""));
 assert(strstr(body(),"\"lane4\":{\"lane\":\"3\",\"material\":\"PETG\",\"color\":\"#FFFFFF\""));
 assert(!strstr(body(),"lane5"));
 puts("PASS live Canvas snapshot maps tray_id 0-3 to lanes 0-3");
 snapshot(&mqtt,"{\"result\":{\"canvas_info\":{\"canvas_list\":["
   "{\"canvas_id\":0,\"connected\":0,\"tray_list\":[{\"filament_color\":\"#111111\",\"filament_type\":\"ABS\",\"max_nozzle_temp\":270,\"tray_id\":0}]},"
   "{\"canvas_id\":1,\"connected\":1,\"tray_list\":[{\"filament_color\":\"#222222\",\"filament_type\":\"PLA\",\"max_nozzle_temp\":220,\"tray_id\":0},"
   "{\"filament_color\":\"\",\"filament_type\":\"\",\"max_nozzle_temp\":0,\"tray_id\":1}]}]}}}");
 assert(get(&mqtt,"/server/database/item?namespace=lane_data")==200);
 assert(!strstr(body(),"ABS"));
 assert(strstr(body(),"\"lane1\":{\"lane\":\"0\",\"material\":\"PLA\",\"color\":\"#222222\",\"nozzle_temp\":220,\"bed_temp\":0}"));
 assert(strstr(body(),"\"lane2\":{\"lane\":\"1\",\"material\":\"\",\"color\":\"\",\"nozzle_temp\":0,\"bed_temp\":0}"));
 puts("PASS connected Canvas module is used, like the web UI, and empty trays keep an empty material");
 snapshot(&mqtt,"{\"result\":{\"canvas_info\":{\"canvas_list\":[{\"connected\":1,\"tray_list\":[{\"filament_type\":\"P\\\"LA\",\"filament_color\":\"#000000\",\"tray_id\":0}]}]}}}");
 assert(get(&mqtt,"/server/database/item?namespace=lane_data")==200);
 assert(strstr(body(),"\"material\":\"P\\\"LA\""));
 puts("PASS escaped strings stay escaped");
 snapshot(&mqtt,"{\"result\":{\"canvas_info\":{\"canvas_list\":[{\"connected\":1,\"tray_list\":[{\"filament_type\":\"PLA\",\"tray_id\":0}");
 assert(get(&mqtt,"/server/database/item?namespace=lane_data")==200);
 assert(strcmp(body(),"{\"result\":{\"namespace\":\"lane_data\",\"value\":{}}}\n")==0);
 puts("PASS truncated snapshot yields no lanes");
 assert(get(&mqtt,"/server/database/item?namespace=other")==404);
 assert(get(&mqtt,"/server/database/item")==404);
 puts("PASS other database namespaces stay 404");
 puts("PASS all Orca lane_data tests");return 0;
}
