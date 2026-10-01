#include "../src/uds.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
static void message(uds_client *c,const char *s){assert(uds_message(c,s,strlen(s)));}
int main(void){
 uds_client c;uds_init(&c);int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));c.fd=pair[0];
 message(&c,"{\"method\":\"cc2_status\",\"params\":{\"eventtime\":2,\"status\":{\"gcode_move\":{\"speed_factor\":0.5}}}}");
 assert(!uds_fresh(&c));
 /* An earlier initial snapshot must not overwrite the preceding notification. */
 message(&c,"{\"id\":11,\"result\":{\"eventtime\":1,\"status\":{\"extruder\":{\"temperature\":210,\"target\":210},\"gcode_move\":{\"speed_factor\":1,\"extrude_factor\":1},\"fan\":{\"speed\":0.6,\"rpm\":8700},\"print_stats\":{\"filename\":\"local/cube.gcode\",\"info\":{\"current_layer\":4,\"total_layer\":null},\"print_duration\":60},\"virtual_sdcard\":{\"progress\":0.25}}}}");
 double v;assert(uds_value(&c,U_SPEED_FACTOR,&v)&&v==0.5);
 assert(uds_value(&c,U_FLOW_FACTOR,&v)&&v==1);
 mqtt_client view;memset(&view,0,sizeof(view));strcpy(view.filename,"cube.gcode");view.machine_status=2;view.chamber_temp=26;view.box_fan=25.5;
 uds_overlay(&c,&view);assert(view.extruder_temp==210&&view.part_fan==153);
 assert(view.current_layer==4&&view.progress==25&&view.print_duration==60);
 assert(view.machine_status==2&&view.chamber_temp==26&&view.box_fan==25.5);
 message(&c,"{\"method\":\"cc2_status\",\"params\":{\"eventtime\":3,\"status\":{\"extruder\":{\"temperature\":211},\"gcode_move\":{\"extrude_factor\":0.95},\"print_stats\":{\"info\":{\"total_layer\":null}}}}}");
 assert(uds_value(&c,U_ET,&v)&&v==211);assert(uds_value(&c,U_EG,&v)&&v==210);
 assert(uds_value(&c,U_LAYER,&v)&&v==4);assert(uds_value(&c,U_FLOW_FACTOR,&v)&&v==0.95);
 message(&c,"{\"method\":\"cc2_status\",\"params\":{\"eventtime\":4,\"status\":{\"fan\":{\"speed\":9},\"virtual_sdcard\":{\"progress\":null}}}}");
 assert(uds_value(&c,U_PF,&v)&&v==0.6);assert(uds_value(&c,U_PROGRESS,&v)&&v==0.25);
 strcpy(view.filename,"different.gcode");view.current_layer=90;uds_overlay(&c,&view);assert(view.current_layer==90);
 c.last_rx.tv_sec-=10;assert(!uds_value(&c,U_ET,&v));view.extruder_temp=200;uds_overlay(&c,&view);assert(view.extruder_temp==200);
 message(&c,"{\"id\":12,\"result\":{\"state\":\"ready\"}}");assert(uds_fresh(&c));
 assert(!uds_message(&c,"{broken",7));
 assert(!fcntl(c.fd,F_SETFL,O_NONBLOCK));
 const char *frame="{\"method\":\"cc2_status\",\"params\":{\"eventtime\":5,\"status\":{\"gcode_move\":{\"speed_factor\":1.3}}}}\003";
 assert(write(pair[1],frame,10)==10);uds_process(&c);assert(c.used==10);
 assert(write(pair[1],frame+10,strlen(frame)-10)==(ssize_t)strlen(frame)-10);uds_process(&c);
 assert(uds_value(&c,U_SPEED_FACTOR,&v)&&v==1.3&&c.used==0);
 message(&c,"{\"method\":\"cc2_status\",\"params\":{\"eventtime\":6,\"status\":{\"print_stats\":{\"filename\":\"local/next.gcode\"}}}}");
 assert(!uds_value(&c,U_LAYER,&v)&&!uds_value(&c,U_PROGRESS,&v));
 close(pair[1]);uds_process(&c);assert(c.fd==-1&&!c.ready&&!c.present);
 assert(c.disconnects==1&&!strcmp(c.last_disconnect,"peer_closed"));
 uds_init(&c);assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));c.fd=pair[0];
 assert(!fcntl(c.fd,F_SETFL,O_NONBLOCK));
 const char *bad="{broken\003";
 assert(write(pair[1],bad,strlen(bad))==(ssize_t)strlen(bad));uds_process(&c);
 assert(c.fd==-1&&c.disconnects==1&&!strcmp(c.last_disconnect,"invalid_json"));close(pair[1]);
 uds_init(&c);uds_tick(&c,"/nonexistent/cc2-test-socket");assert(c.fd==-1);
 puts("PASS UDS initial/delta merge, ordering, scaling, job matching, stale fallback and fragmented frames");
 return 0;
}
