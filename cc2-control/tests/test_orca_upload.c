#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>
#include <sys/socket.h>
static int upload(mqtt_client *mqtt,const char *name,const char *file,int do_print,int custom_header){
  int pair[2];assert(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0);
  char body[4096];int bn=snprintf(body,sizeof(body),
    "--test123\r\nContent-Disposition: form-data; name=\"select\"\r\n\r\nfalse\r\n"
    "--test123\r\nContent-Disposition: form-data; name=\"file\"; filename=\"%s\"\r\nContent-Type: application/octet-stream\r\n\r\n%s\r\n"
    "--test123\r\nContent-Disposition: form-data; name=\"print\"\r\n\r\n%s\r\n--test123--\r\n",name,file,do_print?"true":"false");
  assert(bn>0&&bn<(int)sizeof(body));
  char request[REQUEST_MAX+1];int hdr=snprintf(request,sizeof(request),"POST /api/files/local HTTP/1.1\r\nHost: localhost\r\nContent-Type: %s\r\nContent-Length: %d\r\n\r\n",custom_header?"application/octet-stream":"multipart/form-data; boundary=test123",bn);
  assert(hdr>0&&hdr+bn<(int)sizeof(request));
  memcpy(request+hdr,body,(size_t)bn);
  int owns=orca_upload_start(pair[0],request,(size_t)hdr+(size_t)bn,(size_t)hdr,mqtt);
  char response[1500];ssize_t n=recv(pair[1],response,sizeof(response)-1,0);
  assert(n>0);response[n]=0;
  int status=atoi(strchr(response,' ')+1);
  if(owns){assert(strstr(response,"Content-Type: application/json"));}
  else close(pair[0]);
  close(pair[1]);return status;
}
int main(void){
 char path[]="/tmp/cc2-orca-upload-XXXXXX";assert(mkdtemp(path));gcode_internal_root=path;
 mqtt_client mqtt;memset(&mqtt,0,sizeof(mqtt));mqtt.connected=mqtt.registered=mqtt.have_machine_status=1;
 mqtt.machine_status=1;mqtt.last_message=time(NULL);
 assert(upload(&mqtt,"cube.gcode","G28\nG1 X10\n",0,0)==201);
 char file[1024];snprintf(file,sizeof(file),"%s/cube.gcode",path);
 FILE *f=fopen(file,"rb");assert(f);char buf[128]={0};size_t n=fread(buf,1,sizeof(buf),f);fclose(f);assert(n==11&&!memcmp(buf,"G28\nG1 X10\n",11));
 puts("PASS multipart file saved with exact contents and multiple fields");
 assert(upload(&mqtt,"cube.gcode","NEW\n",0,0)==201);
 f=fopen(file,"rb");assert(f);n=fread(buf,1,sizeof(buf),f);fclose(f);assert(n==11&&!memcmp(buf,"G28\nG1 X10\n",11));
 snprintf(file,sizeof(file),"%s/cube (1).gcode",path);
 f=fopen(file,"rb");assert(f);n=fread(buf,1,sizeof(buf),f);fclose(f);assert(n==4&&!memcmp(buf,"NEW\n",4));
 puts("PASS a taken name is saved as a copy and the original is untouched");
 {char name[PATH_MAX_LOCAL];
  assert(orca_copy_name(name,"Part.GCODE",12)&&!strcmp(name,"Part (12).GCODE"));
  assert(orca_copy_name(name,"Part.gcode",0)&&!strcmp(name,"Part.gcode"));
  char longest[NAME_MAX+1];memset(longest,'a',NAME_MAX-6);memcpy(longest+NAME_MAX-6,".gcode",7);
  assert(upload(&mqtt,longest,"G28\n",0,0)==201);
  assert(!orca_copy_name(name,longest,1));
  assert(upload(&mqtt,longest,"G28\n",0,0)==409);
  snprintf(file,sizeof(file),"%s/%s",path,longest);unlink(file);}
 puts("PASS copy names keep the extension and never exceed NAME_MAX");
 assert(upload(&mqtt,"../bad.gcode","bad",0,0)==400);
 puts("PASS path traversal filename rejected");
 assert(upload(&mqtt,"print.gcode","G28\n",1,0)==201);
 snprintf(file,sizeof(file),"%s/print.gcode",path);assert(access(file,F_OK)==0);
 assert(strcmp(orca_pending_filename,"print.gcode")==0);
 assert(orca_pending_generation>0);
 assert(upload(&mqtt,"pending-conflict.gcode","G28\n",1,0)==409);
 snprintf(file,sizeof(file),"%s/pending-conflict.gcode",path);assert(access(file,F_OK)!=0);
 puts("PASS print=true saves file and queues Canvas confirmation without starting");
 assert(upload(&mqtt,"bad.gcode","G28\n",0,1)==415);
 puts("PASS non-multipart rejected");
 pthread_mutex_lock(&orca_pending_mutex);orca_pending_filename[0]=0;pthread_mutex_unlock(&orca_pending_mutex);
 assert(upload(&mqtt,"print.gcode","G28\n",1,0)==201);
 assert(strcmp(orca_pending_filename,"print (1).gcode")==0);
 puts("PASS upload-and-print of a taken name confirms the saved copy");
 mqtt.machine_status=2;
 assert(upload(&mqtt,"busy.gcode","G28\n",0,0)==201);
 snprintf(file,sizeof(file),"%s/busy.gcode",path);assert(access(file,F_OK)==0);unlink(file);
 assert(upload(&mqtt,"cube.gcode","REPLACE\n",0,0)==201);
 snprintf(file,sizeof(file),"%s/cube (2).gcode",path);assert(access(file,F_OK)==0);
 pthread_mutex_lock(&orca_pending_mutex);orca_pending_filename[0]=0;pthread_mutex_unlock(&orca_pending_mutex);
 assert(upload(&mqtt,"second-print.gcode","G28\n",1,0)==409);
 assert(!orca_pending_filename[0]);
 snprintf(file,sizeof(file),"%s/second-print.gcode",path);assert(access(file,F_OK)!=0);
 mqtt.last_message=time(NULL)-60;
 assert(upload(&mqtt,"stale.gcode","G28\n",0,0)==409);
 mqtt.last_message=time(NULL);mqtt.connected=0;
 assert(upload(&mqtt,"offline.gcode","G28\n",0,0)==409);
 mqtt.connected=1;mqtt.machine_status=10;
 assert(upload(&mqtt,"active.gcode","G28\n",0,0)==409);
 puts("PASS upload-only during print allowed and copies a taken name; print requests rejected");
 const char *left[]={"cube.gcode","cube (1).gcode","cube (2).gcode","print.gcode","print (1).gcode"};
 for(size_t i=0;i<sizeof(left)/sizeof(left[0]);i++){snprintf(file,sizeof(file),"%s/%s",path,left[i]);assert(unlink(file)==0);}
 assert(rmdir(path)==0);
 puts("PASS all Orca adapter tests");return 0;
}
