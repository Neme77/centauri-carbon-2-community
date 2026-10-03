#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>
static int recovery_request(const char *body) {
    int pair[2];char reply[512];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    recovery_reboot_response(pair[0],body,strlen(body));
    ssize_t n=recv(pair[1],reply,sizeof(reply)-1,0);assert(n>0);reply[n]=0;
    int status=atoi(strchr(reply,' ')+1);close(pair[0]);close(pair[1]);return status;
}
static pid_t failed_launch(void){errno=ENOENT;return -1;}
static int utility_status,launch_count;
static pid_t mock_utility(void){launch_count++;pid_t child=fork();if(child==0)_exit(utility_status);return child;}
static void reap_mock(void){
    for(int i=0;i<1000&&reboot_pid;i++){struct timespec delay={0,1000000};nanosleep(&delay,NULL);recovery_tick();}
    assert(!reboot_pid);
}
static void downloads(const char *query,int expected,off_t expected_size) {
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    int owned=gcode_download_start(pair[0],query);if(!owned)close(pair[0]);
    char chunk[16384],header[4096];size_t h=0;off_t bytes=0;int finished_header=0;ssize_t got;
    while((got=read(pair[1],chunk,sizeof(chunk)))>0) {
        for(ssize_t i=0;i<got;i++) {
            if(!finished_header) {
                assert(h+1<sizeof(header));header[h++]=chunk[i];header[h]=0;
                if(h>=4&&!memcmp(header+h-4,"\r\n\r\n",4))finished_header=1;
            } else bytes++;
        }
    }
    close(pair[1]);assert(got==0);assert(atoi(strchr(header,' ')+1)==expected);
    if(expected==200){assert(bytes==expected_size);assert(strstr(header,"Content-Disposition: attachment"));assert(strstr(header,"filename*=UTF-8''"));}
    for(int i=0;owned&&atomic_load(&active_downloads)&&i<1000;i++){struct timespec wait={0,1000000};nanosleep(&wait,NULL);}
    assert(!owned||!atomic_load(&active_downloads));
}
int main(void) {
    static console_state console;console_init(&console,"/not-present");recovery_console=&console;
    assert(recovery_request("REBOOT_AFTER_EMERGENCY")==409);assert(recovery_request("reboot")==400);
    atomic_store(&console.emergency_sent,1);assert(console_clear(&console)==0);assert(recovery_available());
    assert(recovery_request("REBOOT_AFTER_EMERGENCY")==202);assert(recovery_request("REBOOT_AFTER_EMERGENCY")==409);
    reboot_launcher=failed_launch;reboot_due=recovery_clock()-1;recovery_tick();
    assert(!reboot_pending&&!strcmp(reboot_error,"launch_failed"));assert(recovery_request("REBOOT_AFTER_EMERGENCY")==202);
    reboot_launcher=mock_utility;utility_status=1;reboot_due=recovery_clock()-1;recovery_tick();reap_mock();
    assert(!reboot_pending&&!strcmp(reboot_error,"reboot_failed"));
    assert(recovery_request("REBOOT_AFTER_EMERGENCY")==202);
    utility_status=0;reboot_due=recovery_clock()-1;recovery_tick();reap_mock();
    assert(reboot_pending&&launch_count==2);recovery_tick();assert(launch_count==2);
    reboot_pending=0;reboot_launcher=launch_printer_reboot;console_destroy(&console);
    console_init(&console,"/not-present");assert(!recovery_available());
    char root[]="/tmp/cc2-download-XXXXXX";assert(mkdtemp(root));gcode_internal_root=root;gcode_usb_root=root;
    char file[1024];snprintf(file,sizeof(file),"%s/part ü.gcode",root);
    int fd=open(file,O_WRONLY|O_CREAT,0600);assert(fd>=0);assert(write(fd,"G28\n",4)==4);close(fd);
    downloads("storage=internal&file=part%20%C3%BC.gcode",200,4);downloads("file=part%20%C3%BC.gcode&storage=usb",200,4);
    const char *invalid[]={"storage=internal&file=..%2Foutside.gcode","storage=internal&file=%00.gcode","storage=internal&file=part%ZZ.gcode","storage=internal&storage=usb&file=part.gcode","storage=internal&file=part.gcode&","storage=unknown&file=part.gcode","storage=internal&file=not.cfg"};
    for(size_t i=0;i<sizeof(invalid)/sizeof(*invalid);i++)downloads(invalid[i],400,0);
    downloads("storage=internal&file=missing.gcode",404,0);
    snprintf(file,sizeof(file),"%s/link.gcode",root);assert(!symlink("/etc/passwd",file));downloads("storage=internal&file=link.gcode",404,0);unlink(file);
    snprintf(file,sizeof(file),"%s/link",root);assert(!symlink("/tmp",file));downloads("storage=internal&file=link/outside.gcode",404,0);unlink(file);
    snprintf(file,sizeof(file),"%s/pipe.gcode",root);assert(!mkfifo(file,0600));downloads("storage=internal&file=pipe.gcode",404,0);unlink(file);
    atomic_store(&active_downloads,2);downloads("storage=internal&file=part%20%C3%BC.gcode",503,0);assert(atomic_load(&active_downloads)==2);atomic_store(&active_downloads,0);
    snprintf(file,sizeof(file),"%s/large.gcode",root);fd=open(file,O_WRONLY|O_CREAT,0600);assert(fd>=0);assert(!ftruncate(fd,128L*1024*1024));close(fd);
    downloads("storage=internal&file=large.gcode",200,128L*1024*1024);unlink(file);
    snprintf(file,sizeof(file),"%s/part ü.gcode",root);unlink(file);rmdir(root);console_destroy(&console);return 0;
}
