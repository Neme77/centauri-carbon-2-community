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
 struct timespec saved_rx=c.last_rx;unsigned long saved_messages=c.messages;
 message(&c,"{\"id\":0,\"report\":{\"message\":\"vendor report\"}}");
 message(&c,"{\"method\":\"other_notification\",\"params\":{}}");
 assert(c.ignored_messages==2&&c.messages==saved_messages&&c.fd>=0);
 assert(c.last_rx.tv_sec==saved_rx.tv_sec&&c.last_rx.tv_nsec==saved_rx.tv_nsec);
 assert(uds_value(&c,U_SPEED_FACTOR,&v)&&v==0.5);
 /* Native reports must neither refresh sensors nor claim a current fault. */
 message(&c,"{\"id\":0,\"report\":{\"error_code\":1264,\"error_level\":2,\"message\":\"Clog\\n\\u00e8 <script>\"}}");
 assert(c.report_sequence==1&&c.report_code==1264&&c.report_level==2);
 assert(!strcmp(c.report_message,"\"Clog\\n\\u00e8 <script>\""));
 assert(c.messages==saved_messages&&c.last_rx.tv_sec==saved_rx.tv_sec&&c.last_rx.tv_nsec==saved_rx.tv_nsec);
 message(&c,"{\"report\":{\"error_code\":0,\"error_level\":0,\"message\":\"ok\"}}");
 assert(c.report_sequence==1&&c.report_code==1264);
 message(&c,"{\"report\":{\"error_code\":1.5,\"error_level\":2}}");
 message(&c,"{\"report\":{\"error_code\":803,\"error_level\":9}}");
 assert(c.report_sequence==1);
 message(&c,"{\"report\":{\"error_code\":9999,\"error_level\":1,\"message\":\"bad\\q\"}}");
 assert(c.report_sequence==2&&c.report_code==9999&&!strcmp(c.report_message,"null"));
 char huge_report[1300];memset(huge_report,'x',sizeof(huge_report));
 const char *prefix="{\"report\":{\"error_code\":803,\"error_level\":2,\"message\":\"";
 memcpy(huge_report,prefix,strlen(prefix));strcpy(huge_report+1200,"\"}}");message(&c,huge_report);
 assert(c.report_sequence==3&&!strcmp(c.report_message,"null"));
 message(&c,"{\"report\":{\"error_code\":0,\"error_level\":3,\"message\":\"Resume\"}}");
 assert(c.report_sequence==4&&c.report_level==3);
 message(&c,"{\"id\":13,\"error\":{\"message\":\"unsupported endpoint\"}}");
 assert(c.fd>=0&&c.report_sequence==4);
 assert(!uds_message(&c,"{\"id\":11,\"error\":{}}",strlen("{\"id\":11,\"error\":{}}")));
 assert(!strcmp(c.parse_error,"subscription_error"));
 assert(!uds_message(&c,"{\"id\":12,\"error\":{}}",strlen("{\"id\":12,\"error\":{}}")));
 assert(!strcmp(c.parse_error,"heartbeat_error"));
 assert(!fcntl(c.fd,F_SETFL,O_NONBLOCK));
 const char *frame="{\"method\":\"cc2_status\",\"params\":{\"eventtime\":5,\"status\":{\"gcode_move\":{\"speed_factor\":1.3}}}}\003";
 assert(write(pair[1],frame,10)==10);uds_process(&c);assert(c.used==10);
 assert(write(pair[1],frame+10,strlen(frame)-10)==(ssize_t)strlen(frame)-10);uds_process(&c);
 assert(uds_value(&c,U_SPEED_FACTOR,&v)&&v==1.3&&c.used==0);
 message(&c,"{\"method\":\"cc2_status\",\"params\":{\"eventtime\":6,\"status\":{\"print_stats\":{\"filename\":\"local/next.gcode\"}}}}");
 assert(!uds_value(&c,U_LAYER,&v)&&!uds_value(&c,U_PROGRESS,&v));
 close(pair[1]);uds_process(&c);assert(c.fd==-1&&!c.ready&&!c.present);
 assert(c.disconnects==1&&!strcmp(c.last_disconnect,"peer_closed"));
 assert(c.report_sequence==4&&!strcmp(c.report_message,"\"Resume\""));
 uds_init(&c);assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));c.fd=pair[0];
 assert(!fcntl(c.fd,F_SETFL,O_NONBLOCK));
 const char *bad="{broken\003";
 assert(write(pair[1],bad,strlen(bad))==(ssize_t)strlen(bad));uds_process(&c);
 assert(c.fd==-1&&c.disconnects==1&&!strcmp(c.last_disconnect,"invalid_json"));close(pair[1]);
 uds_init(&c);uds_tick(&c,"/nonexistent/cc2-test-socket");assert(c.fd==-1);
 /* elegoo_printer keeps every request in memory: only a silent stream is probed. */
 uds_init(&c);assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));c.fd=pair[0];
 assert(!fcntl(c.fd,F_SETFL,O_NONBLOCK)&&!fcntl(pair[1],F_SETFL,O_NONBLOCK));
 clock_gettime(CLOCK_MONOTONIC,&c.last_rx);c.last_ping=c.last_rx;
 char out[4096];ssize_t got;
 uds_tick(&c,"/unused");got=read(pair[1],out,sizeof(out)-1);assert(got>0);out[got]=0;
 assert(strstr(out,"objects/subscribe")&&strstr(out,"gcode/subscribe_report")&&!strstr(out,"\"method\":\"info\""));
 c.last_ping.tv_sec-=10;uds_tick(&c,"/unused");assert(read(pair[1],out,sizeof(out)-1)<0);
 c.last_rx.tv_sec-=3;uds_tick(&c,"/unused");got=read(pair[1],out,sizeof(out)-1);assert(got>0);out[got]=0;
 assert(strstr(out,"\"method\":\"info\""));
 uds_tick(&c,"/unused");assert(read(pair[1],out,sizeof(out)-1)<0);
 message(&c,"{\"id\":12,\"result\":{\"state\":\"ready\"}}");c.last_ping.tv_sec-=10;
 uds_tick(&c,"/unused");assert(read(pair[1],out,sizeof(out)-1)<0);
 c.last_rx.tv_sec-=6;uds_tick(&c,"/unused");
 assert(c.fd==-1&&!strcmp(c.last_disconnect,"receive_timeout"));close(pair[1]);
 /* exclude_object values are kept as raw JSON, so /api/exclude-objects needs no query. */
 uds_init(&c);assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));c.fd=pair[0];
 char live[1024];assert(uds_exclude_status(&c,live,sizeof(live))<0);
 message(&c,"{\"method\":\"cc2_status\",\"params\":{\"eventtime\":5,\"status\":{\"exclude_object\":{\"current_object\":\"B\"}}}}");
 message(&c,"{\"id\":11,\"result\":{\"eventtime\":4,\"status\":{\"exclude_object\":{\"excluded_objects\":[\"A \\\"x\\\"\"],\"current_object\":\"A\"}}}}");
 assert(!strcmp(c.current_object,"\"B\"")&&!strcmp(c.excluded_objects,"[\"A \\\"x\\\"\"]"));
 assert(uds_exclude_status(&c,live,sizeof(live))>0);
 assert(!strcmp(live,"{\"result\":{\"status\":{\"exclude_object\":{\"excluded_objects\":[\"A \\\"x\\\"\"],\"current_object\":\"B\"}}}}"));
 message(&c,"{\"method\":\"cc2_status\",\"params\":{\"eventtime\":6,\"status\":{\"exclude_object\":{\"current_object\":null}}}}");
 assert(!strcmp(c.current_object,"null")&&uds_exclude_status(&c,live,10)<0);
 char big[9100];int length=snprintf(big,sizeof(big),"{\"method\":\"cc2_status\",\"params\":{\"eventtime\":7,\"status\":{\"exclude_object\":{\"excluded_objects\":[\"");
 memset(big+length,'a',8300);length+=8300;
 length+=snprintf(big+length,sizeof(big)-(size_t)length,"\"]}}}}");
 assert(uds_message(&c,big,(size_t)length)&&!c.have_excluded_objects&&uds_exclude_status(&c,live,sizeof(live))<0);
 uds_close(&c);assert(!c.have_current_object);close(pair[1]);
 puts("PASS UDS initial/delta merge, ordering, scaling, job matching, stale fallback, fragmented frames, silence-only probes and exclude_object state");
 return 0;
}
