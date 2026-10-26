/* Browser-test server: real HTTP router with idle telemetry and full upload first reads. */
#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>
int main(int argc,char **argv){
 (void)argc;int port=atoi(argv[1]);service_http_port=port;gcode_internal_root=argv[2];
 mqtt_client m;mqtt_init(&m);m.connected=m.registered=m.have_machine_status=1;m.machine_status=1;
 console_state console;console_init(&console,"/tmp/unused-test-uds");
 int server=socket(AF_INET,SOCK_STREAM,0);int yes=1;setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
 struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(port);assert(!bind(server,(struct sockaddr*)&a,sizeof(a)));assert(!listen(server,8));
 for(;;){int fd=accept(server,NULL,NULL);if(fd<0)continue;char request[REQUEST_MAX+1]={0};size_t used=0;int full_upload=0;
 struct timeval timeout={5,0};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
 while(used<REQUEST_MAX){ssize_t n=recv(fd,request+used,REQUEST_MAX-used,0);if(n<=0)break;used+=n;request[used]=0;
 full_upload=!strncmp(request,"POST /api/gcode-files/upload?",29);
 if(http_request_complete(request,used)&&(!full_upload||used==REQUEST_MAX))break;}
 m.last_message=time(NULL);if(!handle_client(fd,request,used,argv[3],&m,&console))close(fd);
 }
}
