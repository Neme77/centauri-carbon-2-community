#include "../src/external_filament.h"
#include "../src/control.h"
#include <assert.h>
#include <stdio.h>
#include <time.h>
static int parse(const char *json,external_readback *s){return external_parse(json,strlen(json),s);}
int main(void){
 external_readback s;
 const char *ok="{\"result\":{\"status\":{\"canvas_dev\":{\"active_cid\":-1,\"canvas_lite\":{\"connected\":0,\"extern_channel\":{\"filament\":{\"type\":\"TPU\"}}}},\"print_stats\":{\"state\":\"standby\"}}}}";
 assert(parse(ok,&s)&&s.disconnected&&s.idle&&s.filament);
 assert(!parse("{}",&s));assert(!parse("{\"error\":{}}",&s));assert(!parse("{\"result\":{\"status\":{\"canvas_dev\":{\"active_cid\":-1}}}}",&s));
 char modified[1024];snprintf(modified,sizeof(modified),"%s",ok);
 char *p=strstr(modified,"\"connected\":0");assert(p);p[strlen("\"connected\":")]='1';
 assert(parse(modified,&s)&&!s.disconnected&&!s.idle);
 snprintf(modified,sizeof(modified),"%s",ok);p=strstr(modified,"standby");memcpy(p,"paused ",7);
 assert(parse(modified,&s)&&!s.idle);
 mqtt_client m={0};m.connected=m.registered=m.have_machine_status=1;m.machine_status=1;m.last_message=time(NULL);
 char command[700],reason[256];
 #define BUILD(a) control_build_script(a,&m,command,sizeof(command),reason,sizeof(reason))
 assert(!BUILD("external:load"));m.external_verified=1;
 assert(BUILD("external:load")&&!strcmp(command,"M600 S1"));
 assert(BUILD("external:unload")&&!strcmp(command,"M600 S2"));
 assert(!BUILD("external:load\nM112"));assert(!BUILD("external:material:0:0:0:ABCDEFjunk"));
 assert(!BUILD("external:material:-1:0:0:ABCDEF"));assert(!BUILD("external:material:0:0:999:ABCDEF"));
 assert(BUILD("external:material:0:0:0:ABCDEF"));
 assert(strstr(command,"EXTERN_FILA=1")&&strstr(command,"TYPE=\"PLA\"")&&strstr(command,"MANUFACTURER=\"ELEGOO\""));assert(!strstr(command,"CHANNEL="));
 m.machine_status=2;assert(!BUILD("external:load"));m.machine_status=1;
 m.last_message-=20;assert(!BUILD("external:load"));m.last_message=time(NULL);
 m.connected=0;assert(!BUILD("external:unload"));m.connected=1;
 assert(!BUILD("canvas:catalog:0:0:0:0:ABCDEF"));
 puts("PASS: native external presence, idle, exact fixed commands, material bounds and stale/busy guards");return 0;
}
