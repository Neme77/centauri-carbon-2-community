#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>
#include <sys/socket.h>

static void expect(int condition,const char *msg){if(!condition){fprintf(stderr,"FAIL %s\n",msg);exit(1);}fprintf(stdout,"PASS %s\n",msg);}
static int run_delete(mqtt_client *mqtt,const char *body,const char *auth){
    int pair[2];expect(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0,"create delete socket");
    (void)auth;char request[512];snprintf(request,sizeof(request),"POST /api/gcode-files/delete HTTP/1.1\r\n\r\n");
    gcode_delete_response(pair[0],request,mqtt,body,strlen(body));
    char response[600];ssize_t size=recv(pair[1],response,sizeof(response)-1,0);
    expect(size>0,"receive delete response");response[size]=0;
    close(pair[0]);close(pair[1]);
    return atoi(strchr(response,' ')+1);
}
static int run_upload(mqtt_client *mqtt,const char *name,const char *storage,const char *content,const char *auth){
    int pair[2];expect(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0,"create upload socket");
    (void)auth;char req[1024];int len=snprintf(req,sizeof(req),"POST /api/gcode-files/upload?storage=%s&name=%s HTTP/1.1\r\nHost: test\r\nContent-Length: %zu\r\n\r\n%s",storage,name,strlen(content),content);
    size_t header=(size_t)(strstr(req,"\r\n\r\n")-req)+4;
    char query[512];snprintf(query,sizeof(query),"storage=%s&name=%s",storage,name);
    int owned=gcode_upload_start(pair[0],req,query,(size_t)len,header,mqtt);
    char response[600];ssize_t size=recv(pair[1],response,sizeof(response)-1,0);
    expect(size>0,"receive upload response");response[size]=0;
    if(!owned)close(pair[0]);
    close(pair[1]);
    return atoi(strchr(response,' ')+1);
}
static void test_upload_size_limit(mqtt_client *mqtt){
    const size_t limit=128UL*1024UL*1024UL;
    expect(FILE_UPLOAD_MAX==limit,"upload limit is 128 MiB");
    int pair[2];expect(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0,"create oversized upload socket");
    char request[512];int n=snprintf(request,sizeof(request),
        "POST /api/gcode-files/upload?storage=internal&name=large.gcode HTTP/1.1\r\nContent-Length: %zu\r\n\r\n",limit+1);
    expect(!gcode_upload_start(pair[0],request,"storage=internal&name=large.gcode",(size_t)n,(size_t)n,mqtt),"oversized raw upload rejected before worker starts");
    char response[512];ssize_t got=recv(pair[1],response,sizeof(response)-1,0);expect(got>0,"receive size rejection");response[got]=0;
    expect(strstr(response,"413 Payload Too Large")&&strstr(response,"maximum 128 MiB"),"raw upload reports updated maximum");
    close(pair[0]);close(pair[1]);
    expect(socketpair(AF_UNIX,SOCK_STREAM,0,pair)==0,"create oversized multipart socket");
    n=snprintf(request,sizeof(request),"POST /api/files/local HTTP/1.1\r\nContent-Type: multipart/form-data; boundary=test\r\nContent-Length: %zu\r\n\r\n",limit+8193);
    expect(!orca_upload_start(pair[0],request,(size_t)n,(size_t)n,mqtt),"oversized multipart body rejected before worker starts");
    got=recv(pair[1],response,sizeof(response)-1,0);expect(got>0,"receive multipart size rejection");response[got]=0;
    expect(strstr(response,"413 Payload Too Large")!=NULL,"multipart overhead remains bounded");
    close(pair[0]);close(pair[1]);
}
int main(void){
    char root[]="/tmp/cc2-file-manager-test-XXXXXX";expect(mkdtemp(root)!=NULL,"create test directory");
    gcode_internal_root=root;gcode_usb_root="/tmp/cc2-usb-not-mounted";
    mqtt_client mqtt;memset(&mqtt,0,sizeof(mqtt));strcpy(mqtt.password,"test-code");
    mqtt.connected=mqtt.registered=mqtt.have_machine_status=1;
    mqtt.machine_status=1;mqtt.last_message=time(NULL);
    test_upload_size_limit(&mqtt);
    expect(run_upload(&mqtt,"cube.gcode","internal","; cube test\n","test-code")==201,"upload into internal memory");
    expect(run_upload(&mqtt,"barca%201.gcode","internal","; spaced name\n","test-code")==201,"upload URL-encoded filename with spaces");
    char path[1024];snprintf(path,sizeof(path),"%s/cube.gcode",root);
    struct stat statbuf;expect(stat(path,&statbuf)==0&&statbuf.st_size==12,"uploaded file finalized");
    expect(run_upload(&mqtt,"cube.gcode","internal","duplicate","test-code")==409,"reject duplicate without overwrite");
    expect(run_upload(&mqtt,"no-code.gcode","internal","no-auth","")==201,"upload without access code");
    expect(run_upload(&mqtt,"..%2Fetc.gcode","internal","bad-path","test-code")==400,"reject URL-encoded slash");
    expect(run_upload(&mqtt,"new.gcode","usb","test","test-code")==400,"reject unavailable USB");
    mqtt.machine_status=2;
    expect(run_upload(&mqtt,"new.gcode","internal","test","test-code")==201,"allow upload during print");
    expect(run_delete(&mqtt,"internal\ncube.gcode","test-code")==409,"reject delete during print");
    expect(run_upload(&mqtt,"cube.gcode","internal","replacement","test-code")==409,"reject overwrite during print");
    mqtt.last_message=time(NULL)-60;
    expect(run_upload(&mqtt,"stale.gcode","internal","test","test-code")==409,"reject stale telemetry");
    mqtt.last_message=time(NULL);mqtt.connected=0;
    expect(run_upload(&mqtt,"offline.gcode","internal","test","test-code")==409,"reject disconnected upload");
    mqtt.connected=1;mqtt.machine_status=10;
    expect(run_upload(&mqtt,"active.gcode","internal","test","test-code")==409,"reject other active states");
    mqtt.machine_status=1;
    expect(run_delete(&mqtt,"internal\nno-code.gcode","")==200,"delete without access code");
    expect(run_delete(&mqtt,"internal\nbarca 1.gcode","")==200,"delete filename with spaces");
    expect(run_delete(&mqtt,"internal\n../cube.gcode","test-code")==404,"reject path traversal delete");
    expect(run_delete(&mqtt,"internal\ncube.gcode","test-code")==200,"delete internal G-code");
    expect(stat(path,&statbuf)<0&&errno==ENOENT,"deleted file absent");
    /* /dev/shm is a separate tmpfs mount in this test container: exercise the
       USB mount check on a real mount without touching any user files. */
    gcode_usb_root="/dev/shm";
    struct stat shm_stat,dev_stat;
    if(stat("/dev/shm",&shm_stat)==0&&stat("/dev",&dev_stat)==0&&shm_stat.st_dev!=dev_stat.st_dev){
    expect(run_upload(&mqtt,"cc2-file-manager-smoke-9f78.gcode","usb","G28\n","test-code")==201,"upload to mounted USB-style filesystem");
    expect(run_delete(&mqtt,"usb\ncc2-file-manager-smoke-9f78.gcode","test-code")==200,"delete on mounted USB-style filesystem");
    }else puts("SKIP mounted USB-style test: /dev/shm not separately mounted");
    snprintf(path,sizeof(path),"%s/new.gcode",root);unlink(path);
    rmdir(root);puts("PASS all file mutation tests");return 0;
}
