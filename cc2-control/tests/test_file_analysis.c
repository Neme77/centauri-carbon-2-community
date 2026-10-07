#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>
static void read_reply(int fd,char *reply,size_t capacity){
 size_t used=0;while(used+1<capacity){struct pollfd p={fd,POLLIN,0};assert(poll(&p,1,60000)>0);ssize_t n=read(fd,reply+used,capacity-used-1);if(!n)break;assert(n>0);used+=(size_t)n;}reply[used]=0;
}
int main(void){
 char root[]="/tmp/cc2-analysis-XXXXXX";assert(mkdtemp(root));gcode_internal_root=root;
 char path[512];snprintf(path,sizeof(path),"%s/large.gcode",root);FILE *f=fopen(path,"w");assert(f);
 char block[32768];memset(block,' ',sizeof(block));for(size_t i=0;i<sizeof(block);i+=16)memcpy(block+i,"G1 X1 Y1 E0.1\n  ",16);
 for(int i=0;i<3400;i++)assert(fwrite(block,1,sizeof(block),f)==sizeof(block));
 fputs("\n; total layer number: 227\nT3\n",f);assert(!fclose(f));
 /* Active-job fallback returns immediately and shares the existing worker. */
 struct timespec begin,end;clock_gettime(CLOCK_MONOTONIC,&begin);
 assert(active_gcode_total_layers("large.gcode")==0);
 clock_gettime(CLOCK_MONOTONIC,&end);
 assert((end.tv_sec-begin.tv_sec)+(end.tv_nsec-begin.tv_nsec)/1e9<0.5);
 mqtt_client m;memset(&m,0,sizeof(m));strcpy(m.filename,"large.gcode");
 for(int i=0;i<60000;i++) {
  int peers[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,peers));
  clock_gettime(CLOCK_MONOTONIC,&begin);printer_response(peers[0],&m);clock_gettime(CLOCK_MONOTONIC,&end);
  assert((end.tv_sec-begin.tv_sec)+(end.tv_nsec-begin.tv_nsec)/1e9<0.5);
  close(peers[0]);char response[8192];read_reply(peers[1],response,sizeof(response));close(peers[1]);
  if(strstr(response,"\"total_layers\":227"))break;
  assert(i<59999);struct timespec pause={0,1000000};nanosleep(&pause,NULL);
 }
 assert(active_gcode_total_layers("missing.gcode")==0);
 assert(active_gcode_total_layers("")==0); /* discard results of a previous job */
 pthread_mutex_lock(&analysis_mutex);int pending=active_analysis_pending;pthread_mutex_unlock(&analysis_mutex);
 while(pending){struct timespec pause={0,1000000};nanosleep(&pause,NULL);pthread_mutex_lock(&analysis_mutex);pending=active_analysis_pending;pthread_mutex_unlock(&analysis_mutex);}
 assert(active_gcode_total_layers("")==0);
 int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
 const char *body="internal\nlarge.gcode";struct timespec a,b;clock_gettime(CLOCK_MONOTONIC,&a);
 assert(analysis_start(pair[0],1,body,strlen(body)));clock_gettime(CLOCK_MONOTONIC,&b);
 assert((b.tv_sec-a.tv_sec)+(b.tv_nsec-a.tv_nsec)/1e9<0.5);
 /* Main loop can continue serving HTTP while the worker scans the large file. */
 int health[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,health));
 const char *ok="{\"ok\":true}";respond(health[0],200,"OK","application/json",ok,strlen(ok));close(health[0]);
 char reply[8192];read_reply(health[1],reply,sizeof(reply));assert(strstr(reply,"200 OK"));close(health[1]);
 read_reply(pair[1],reply,sizeof(reply));assert(strstr(reply,"\"layers\":227"));close(pair[1]);
 assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));assert(analysis_start(pair[0],0,body,strlen(body)));
 read_reply(pair[1],reply,sizeof(reply));assert(strstr(reply,"\"tools\":[3]"));close(pair[1]);
 /* Replacement at the same filename invalidates both cached responses. */
 f=fopen(path,"w");assert(f);fputs("; total layer number: 12\nT1\n",f);fclose(f);
 assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));assert(analysis_start(pair[0],1,body,strlen(body)));
 read_reply(pair[1],reply,sizeof(reply));assert(strstr(reply,"\"layers\":12"));close(pair[1]);
 assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));assert(analysis_start(pair[0],0,body,strlen(body)));
 read_reply(pair[1],reply,sizeof(reply));assert(strstr(reply,"\"tools\":[1]"));close(pair[1]);
 /* Repeated request returns the same full response from the bounded cache. */
 assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));assert(analysis_start(pair[0],0,body,strlen(body)));
 read_reply(pair[1],reply,sizeof(reply));assert(strstr(reply,"\"tools\":[1]"));close(pair[1]);
 unlink(path);rmdir(root);
 puts("PASS: 106 MiB asynchronous scan, responsive main path, EOF tool/layer discovery, cache invalidation and reuse");return 0;
}
