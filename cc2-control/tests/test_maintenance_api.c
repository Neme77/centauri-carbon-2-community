static char maintenance_test_lock[256];
#define CC2_MAINTENANCE_LOCK maintenance_test_lock
#define CC2_MAINTENANCE_UID geteuid()
#define main cc2_original_main
#define console_emergency_stop maintenance_test_emergency_stop
#include "../src/main.c"
#undef main
#undef console_emergency_stop
#include <assert.h>
static int emergencies;
int maintenance_test_emergency_stop(console_state *c,char *reason,size_t cap){
    (void)c;(void)reason;(void)cap;emergencies++;return 0;
}
static mqtt_client test_model;
static console_state test_console;
static int request(const char *method,const char *path,const char *body,int foreign){
    int listener=socket(AF_INET,SOCK_STREAM,0);assert(listener>=0);
    struct sockaddr_in address;memset(&address,0,sizeof(address));address.sin_family=AF_INET;address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(!bind(listener,(struct sockaddr*)&address,sizeof(address)) && !listen(listener,1));
    socklen_t size=sizeof(address);assert(!getsockname(listener,(struct sockaddr*)&address,&size));service_http_port=ntohs(address.sin_port);
    int client=socket(AF_INET,SOCK_STREAM,0);assert(client>=0 && !connect(client,(struct sockaddr*)&address,sizeof(address)));
    int server=accept(listener,NULL,NULL);assert(server>=0);close(listener);
    char origin[80],message[1024],response[4096];
    snprintf(origin,sizeof(origin),"http://localhost:%d",service_http_port);
    int length=snprintf(message,sizeof(message),"%s %s HTTP/1.1\r\nHost: localhost:%d\r\nOrigin: %s\r\nX-CC2-Request: 1\r\nContent-Length: %zu\r\n\r\n%s",method,path,service_http_port,foreign?"https://foreign.invalid":origin,strlen(body),body);
    assert(length>0 && (size_t)length<sizeof(message));
    handle_client(server,message,(size_t)length,"none",&test_model,&test_console);close(server);
    ssize_t got=read(client,response,sizeof(response)-1);assert(got>0);response[got]=0;close(client);
    int status=0;assert(sscanf(response,"HTTP/1.1 %d",&status)==1);return status;
}
int main(void){
    char root[]="/tmp/cc2-maintenance-api-XXXXXX";assert(mkdtemp(root));
    assert(snprintf(maintenance_test_lock,sizeof(maintenance_test_lock),"%s/install.lock",root)>0);
    mqtt_init(&test_model);console_init(&test_console,"none");
    struct maintenance_barrier held=MAINTENANCE_BARRIER_INIT;
    assert(maintenance_barrier_acquire(&held));
    assert(request("POST","/api/control","light:1",0)==409);
    assert(request("POST","/api/console/command","G28",0)==409);
    assert(request("PUT","/api/preferences","{}",0)==409);
    assert(request("POST","/api/rfid/action","scan",0)==409);
    assert(request("GET","/api/health","",0)==200);
    assert(request("HEAD","/api/health","",0)!=409);
    assert(request("POST","/api/control","system:emergency_stop",0)==202 && emergencies==1);
    assert(request("POST","/api/console/command","M112",0)==202 && emergencies==2);
    assert(request("POST","/api/control","system:emergency_stop",1)==403 && emergencies==2);
    assert(maintenance_barrier_release(&held));
    assert(request("POST","/api/this-route-does-not-exist","",0)==404);
    int file=open(maintenance_test_lock,O_WRONLY|O_CREAT|O_EXCL,0600);assert(file>=0 && !close(file));
    assert(request("POST","/api/control","light:1",0)==409);
    assert(!unlink(maintenance_test_lock) && !rmdir(root));
    puts("PASS: real HTTP installer/maintenance mutation barrier, read-only health, both emergency routes and browser-origin protection");
    return 0;
}
