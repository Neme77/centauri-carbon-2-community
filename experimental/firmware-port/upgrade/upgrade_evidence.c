/* Read-only preparation evidence. Never execute retained helpers or open UART.
 * Checksums bind coherent state, not a signature against malicious root edits.
 * Native idle/cold and firmware baseline are checked separately by the caller. */
#define _GNU_SOURCE
#include "../../canvas-rfid/lifecycle/startup_tx.h"
#include "../../canvas-rfid/lifecycle/profile.h"
#include "../../canvas-rfid/transfer/app_transfer.h"
#include "../../network-connections/json.h"
#include <dirent.h>
#ifndef PREPARE_PREFIX
#define PREPARE_PREFIX ""
#endif
#define ROOT PREPARE_PREFIX "/opt/usr/cc2-rfid"
static const char *targets[]={
 PREPARE_PREFIX "/etc/init.d/cc2-reactor-launch",
 PREPARE_PREFIX "/opt/usr/cc2-reactor-runtime-v2/launcher.sh",
 PREPARE_PREFIX "/opt/usr/cc2-reactor-runtime-v2/SHA256SUMS",
 PREPARE_PREFIX "/opt/usr/cc2-network-manager/profile.sh",
 PREPARE_PREFIX "/opt/usr/cc2-network-manager/reactor-entry.sh",
 PREPARE_PREFIX "/opt/usr/cc2-network-manager/activate.sh",
 PREPARE_PREFIX "/opt/usr/cc2-network-manager/SHA256SUMS",
 PREPARE_PREFIX "/opt/usr/cc2-network-manager/startup.sha256"};
static int absent(int dir,const char *name){struct stat st;return fstatat(dir,name,&st,AT_SYMLINK_NOFOLLOW)<0&&errno==ENOENT;}
static int read_bytes(int dir,const char *name,unsigned char *out,size_t max,size_t *size){
 int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);struct stat st;
 int ok=fd>=0&&!fstat(fd,&st)&&st.st_size>0&&(uint64_t)st.st_size<=max&&cl_private(fd,0,st.st_size);
 if(ok){*size=(size_t)st.st_size;ok=cl_io(fd,out,*size,0);}
 if(fd>=0)close(fd);
 return ok;
}
static int file_hash(int dir,const char *name,const char *expected){
 int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);struct stat st;char hash[65];
 int ok=fd>=0&&!fstat(fd,&st)&&st.st_size>0&&(uint64_t)st.st_size<=4*1024*1024&&
  cl_private(fd,0,st.st_size)&&sha256_fd(fd,hash)&&!strcmp(hash,expected);
 if(fd>=0)close(fd);
 return ok;
}
static int pending_clear(int dir){
 DIR *scan=fdopendir(dup(dir));if(!scan)return 0;
 int ok=1;
 for(;;){errno=0;struct dirent *entry=readdir(scan);if(!entry){if(errno)ok=0;break;}
  const char *n=entry->d_name;
  if(!strcmp(n,"activation-request")||!strcmp(n,"lifecycle.next")||
     (!strncmp(n,".lifecycle-",11)&&strstr(n,".next"))){ok=0;break;}
 }
 closedir(scan);return ok;
}
static int metadata(int dir,char profile[65]){
 unsigned char bytes[2049];size_t size=0;
 if(!read_bytes(dir,"app-integration.json",bytes,2048,&size))return 0;
 bytes[size]=0;ng_json *j=malloc(sizeof(*j));if(!j)return 0;
 double protocol=0;
 int ok=ng_json_parse(j,(char*)bytes,size)&&j->v[0].type=='{'&&j->v[0].count==5&&
  ng_number(j,0,"protocol",&protocol)&&protocol==1;
 const char *app=ng_string(j,0,"app_sha256"),*coordinator=ng_string(j,0,"coordinator_sha256"),
  *capture=ng_string(j,0,"capture_sha256"),*p=ng_string(j,0,"profile_sha256");
 ok=ok&&app&&p&&coordinator&&capture&&strlen(app)==64&&tx_hex(app)&&strlen(p)==64&&tx_hex(p)&&
  strlen(coordinator)==64&&tx_hex(coordinator)&&strlen(capture)==64&&tx_hex(capture)&&
  file_hash(dir,"coordinator",coordinator)&&file_hash(dir,"capture",capture)&&file_hash(dir,"recovery-profile",p);
 if(ok)memcpy(profile,p,65);
 free(j);return ok;
}
static int profile_layout(int dir,int immutable){
 static const char *roles[]={"bootstrap","native","guard","entry","recover","marker"};
 static const char *paths[]={PREPARE_PREFIX "/etc/init.d/cc2-reactor-launch",
  PREPARE_PREFIX "/opt/usr/cc2-reactor-runtime-v2/elegoo_printer",ROOT "/libcc2-rfid.so",
  PREPARE_PREFIX "/opt/inst/cc2-printer-extensions13/launch.sh",
  PREPARE_PREFIX "/opt/inst/cc2-printer-extensions13/recover-worker",
  PREPARE_PREFIX "/opt/inst/cc2-printer-extensions13/rfid-enabled.template"};
 unsigned char bytes[CL_PROFILE_MAX+1];size_t size=0;unsigned seen=0;
 if(!read_bytes(dir,"recovery-profile",bytes,CL_PROFILE_MAX,&size)||bytes[size-1]!='\n'||memchr(bytes,0,size))return 0;
 bytes[size]=0;char *line=(char*)bytes;
 while(*line){
  char *end=strchr(line,'\n'),*space=strchr(line,' ');if(!end||!space||space>end)return 0;
  *space=0;unsigned role=6;for(unsigned i=0;i<6;i++)if(!strcmp(line,roles[i]))role=i;
  if(role==6||(seen&(1u<<role))||end-space<67||space[65]!=' ')return 0;
  seen|=1u<<role;space[65]=0;if(!tx_hex(space+1))return 0;
  char *path=space+66;*end=0;
  size_t length=strlen(path);
  if(*path!='/'||length<2||length>512||path[length-1]=='/'||
     strstr(path,"//")||strstr(path,"/../")||strstr(path,"/./")||
     !strcmp(path+length-2,"/.")||(length>=3&&!strcmp(path+length-3,"/..")))return 0;
  for(const char *p=path;*p;p++)if((unsigned char)*p<=32||(unsigned char)*p>=127)return 0;
  if(immutable&&strcmp(path,paths[role]))return 0;
  line=end+1;
 }
 return seen==63;
}
static int restored_legacy(int dir){
 unsigned char bytes[4097];size_t size=0;
 if(!read_bytes(dir,"startup-plan",bytes,4096,&size)||memchr(bytes,0,size))return 0;
 bytes[size]=0;char *line=(char*)bytes;char *end=strchr(line,'\n');
 if(!end)return 0;
 *end=0;if(strcmp(line,"CC2-STARTUP-PLAN-1"))return 0;line=end+1;
 const char *names[]={"dispatch","stock-entry"},*leaves[]={"boot-dispatch.sh","stock-entry.sh"};
 for(unsigned i=0;i<2;i++){
  end=strchr(line,'\n');if(!end)return 0;*end=0;size_t n=strlen(names[i]);
  if(strlen(line)!=n+65||strncmp(line,names[i],n)||line[n]!=' '||!tx_hex(line+n+1)||!file_hash(dir,leaves[i],line+n+1))return 0;
  line=end+1;
 }
 int payload=openat(dir,"payload",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
 int snapshots=openat(dir,"startup-tx",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
 struct tx_file files[8];memset(files,0,sizeof(files));
 int ok=cl_private(payload,1,0)&&cl_private(snapshots,1,0);
 for(unsigned i=0;ok&&i<8;i++){
  end=strchr(line,'\n');if(!end){ok=0;break;}*end=0;
  unsigned index=9,old=0,new_mode=0;char extra=0,canonical[256],leaf[256],name[32];
  struct tx_file *f=files+i;
  int fields=sscanf(line,"%u %o %o %64s %64s%c",&index,&old,&new_mode,f->old_sha,f->new_sha,&extra);
  int n=snprintf(canonical,sizeof(canonical),"%u %04o %04o %s %s",i,old,new_mode,f->old_sha,f->new_sha);
  ok=fields==5&&index==i&&n>0&&(size_t)n<sizeof(canonical)&&!strcmp(line,canonical)&&
   tx_mode((mode_t)old)&&tx_mode((mode_t)new_mode)&&tx_hex(f->old_sha)&&tx_hex(f->new_sha);
  if(!ok)break;
  f->path=targets[i];f->old_mode=(mode_t)old;f->new_mode=(mode_t)new_mode;
  int parent=tx_parent(f->path,leaf);
  ok=parent>=0&&tx_read(parent,leaf,f->old_sha,f->old_mode,NULL,NULL);if(parent>=0)close(parent);
  snprintf(name,sizeof(name),"%u",i);unsigned char *data=NULL;
  ok=ok&&tx_read(payload,name,f->new_sha,0600,&data,&f->length);f->bytes=data;
  snprintf(name,sizeof(name),"%u.old",i);ok=ok&&tx_read(snapshots,name,f->old_sha,0600,NULL,NULL);
  snprintf(name,sizeof(name),"%u.new",i);ok=ok&&tx_read(snapshots,name,f->new_sha,0600,NULL,NULL);
  line=end+1;
 }
 char digest[65];ok=ok&&!*line&&tx_plan(files,8,digest)&&tx_receipt(snapshots,"ready",digest,0)&&tx_receipt(snapshots,"restored",digest,0);
 for(unsigned i=0;i<8;i++)free((void*)files[i].bytes);
 if(payload>=0)close(payload);
 if(snapshots>=0)close(snapshots);
 return ok;
}
int main(int argc,char **argv){
 if(geteuid()!=0||argc!=2||strcmp(argv[1],"--inspect"))return 1;
 int dir=open(ROOT,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
 if(dir<0){if(errno==ENOENT){puts("ABSENT");return 0;}goto refused;}
 int lock=-1,ok=cl_private(dir,1,0)&&pending_clear(dir);char profile[65];
 if(!absent(dir,"lifecycle.lock")){
  lock=openat(dir,"lifecycle.lock",O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
  ok=ok&&cl_private(lock,0,0)&&flock(lock,LOCK_EX|LOCK_NB)==0;
 }
 ok=ok&&metadata(dir,profile);
 if(ok&&absent(dir,"lifecycle")){
  ok=absent(dir,"enabled")&&absent(dir,"startup-tx")&&profile_layout(dir,0);
  if(ok){if(lock>=0)close(lock);close(dir);puts("DORMANT");return 0;}
 }else if(ok){
  unsigned char record_bytes[CL_RECORD_SIZE],image[AP_SIZE];size_t size=0;struct cl_record record;char actual[65],expected[65];
  ok=lock>=0&&read_bytes(dir,"lifecycle",record_bytes,sizeof(record_bytes),&size)&&size==sizeof(record_bytes);
  if(ok){
   tx_hash(record_bytes,CL_PAYLOAD_SIZE,actual);memcpy(expected,record_bytes+CL_PAYLOAD_SIZE,64);expected[64]=0;
   ok=!strcmp(actual,expected)&&cl_decode(record_bytes,&record)&&record.phase==CL_DISABLED;
  }
  if(ok){for(unsigned i=0;i<32;i++)snprintf(expected+2*i,3,"%02x",record.plan[i]);ok=!strcmp(profile,expected);}
  ok=ok&&read_bytes(dir,"original-stock-44.bin",image,sizeof(image),&size)&&size==sizeof(image)&&ap_image(image,size,0);
  if(ok&&!absent(dir,"enabled")){
   unsigned char marker[20];ok=read_bytes(dir,"enabled",marker,sizeof(marker),&size)&&size==19&&!memcmp(marker,"CC2-RFID-NATIVE-R2\n",19);
  }
  int legacy=!absent(dir,"startup-plan");
  ok=ok&&profile_layout(dir,!legacy)&&(!legacy||restored_legacy(dir));
  if(ok){if(lock>=0)close(lock);close(dir);puts("RESTORED");return 0;}
 }
 if(lock>=0)close(lock);
 close(dir);
refused:
 fputs("STOP: Canvas evidence is incomplete, active or inconsistent; keep files and finish restoration\n",stderr);return 1;
}
