#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>
static int low_space(const char *root,struct statvfs *st){(void)root;memset(st,0,sizeof(*st));st->f_frsize=1024;st->f_bavail=16*1024+16;return 0;}
static int unavailable_space(const char *root,struct statvfs *st){(void)root;(void)st;return -1;}
int main(void){
 upload_statvfs=low_space;assert(upload_has_space("/unused",8192,2));assert(!upload_has_space("/unused",8193,2));
 upload_statvfs=unavailable_space;assert(!upload_has_space("/unused",1,1));
 int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));assert(!upload_space_guard(pair[0],"/unused",1,1));
 char reply[512];ssize_t n=read(pair[1],reply,sizeof(reply)-1);assert(n>0);reply[n]=0;assert(strstr(reply,"507 Insufficient Storage"));close(pair[0]);close(pair[1]);
 gcode_upload_job job={0};clock_gettime(CLOCK_MONOTONIC,&job.started);job.started.tv_sec-=UPLOAD_TOTAL_SECONDS+1;
 assert(!upload_seconds_left(&job));assert(upload_receive(&job,reply,1)==-1&&errno==ETIMEDOUT);
 z_offset_pending=1;z_offset_session=1;clock_gettime(CLOCK_MONOTONIC,&z_offset_started);z_offset_started.tv_sec-=6;
 double value;uds_init(&telemetry);assert(!z_offset_readback(&value));assert(!z_offset_pending&&!z_offset_session&&z_offset_timed_out);
 upload_statvfs=statvfs;
 char root[]="/tmp/cc2-expired-upload-XXXXXX";assert(mkdtemp(root));gcode_internal_root=root;
 assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));gcode_upload_job *expired=calloc(1,sizeof(*expired));assert(expired);
 expired->fd=pair[0];expired->content_length=20;strcpy(expired->storage,"internal");strcpy(expired->name,"expired.gcode");
 clock_gettime(CLOCK_MONOTONIC,&expired->started);expired->started.tv_sec-=UPLOAD_TOTAL_SECONDS+1;
 assert(upload_slot_acquire());gcode_upload_worker(expired);
 n=read(pair[1],reply,sizeof(reply)-1);assert(n>0);reply[n]=0;assert(strstr(reply,"408 Request Timeout"));
 close(pair[1]);assert(!atomic_load(&upload_busy));assert(!rmdir(root));
 FILE *f=tmpfile();assert(f);fputs("Content-Disposition: form-data; filename=\"part.gcode\"; name=\"file\"\r\n\r\nG28",f);rewind(f);
 char field[32],filename[PATH_MAX_LOCAL];long offset;assert(orca_part_headers(f,0,&offset,field,filename));assert(!strcmp(field,"file")&&!strcmp(filename,"part.gcode"));fclose(f);
 return 0;
}
