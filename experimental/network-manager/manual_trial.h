/* Private manual-switch trial bridge. The qualified scripts own all network
 * changes and their independent watchdog; no signal-quality selection exists. */
#ifndef CC2_NETWORK_MANUAL_TRIAL_H
#define CC2_NETWORK_MANUAL_TRIAL_H
#include <sys/wait.h>
#include "network_trial_pins.h"
#define NETWORK_TRIAL_MANAGE "/tmp/cc2-network-compat-r1/manage.sh"
static pid_t network_trial_child;
static int network_trial_result=-1;
static int network_trial_script_ready(void){
    int script=open(NETWORK_TRIAL_MANAGE,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(script<0)return 0;
    struct stat st;char digest[65];
    int ok=!fstat(script,&st)&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&
        !(st.st_mode&0022)&&st.st_size>0&&st.st_size<32768&&
        sha256_fd(script,digest)&&!strcmp(digest,NETWORK_TRIAL_MANAGE_SHA);
    close(script);return ok;
}
static void network_trial_reap(void){
    if(network_trial_child>0){
        int status;pid_t done=waitpid(network_trial_child,&status,WNOHANG);
        if(done==network_trial_child){network_trial_result=WIFEXITED(status)?WEXITSTATUS(status):128;network_trial_child=0;}
        else if(done<0&&errno==ECHILD){network_trial_result=128;network_trial_child=0;}
    }
}
static int network_trial_busy(void){network_trial_reap();return network_trial_child>0;}
static void network_trial_response(int fd,const mqtt_client *mqtt,console_state *console){
    char text[256];int n=snprintf(text,sizeof(text),
        "{\"available\":%s,\"busy\":%s,\"ready\":%s,\"last_exit\":%d,\"temporary\":true,\"ethernet_seconds\":180}\n",
        network_trial_script_ready()?"true":"false",network_trial_busy()?"true":"false",
        app_update_ready(mqtt,console)?"true":"false",network_trial_result);
    respond(fd,200,"OK","application/json",text,(size_t)n);
}
static void network_trial_select(int fd,const char *request,const char *body,size_t length,
                                 mqtt_client *mqtt,console_state *console){
    /* The network page intentionally has no LAN-code gate. The shared HTTP
     * host/origin/request guards, idle/cold checks and wired proof still apply. */
    (void)request;
    const char *action=NULL;
    if(length==5&&!memcmp(body,"start",5))action="start";
    else if(length==8&&!memcmp(body,"ethernet",8))action="ethernet";
    else if(length==4&&!memcmp(body,"wifi",4))action="restore";
    if(!action){app_update_fail(fd,400,"Unknown manual network selection");return;}
    if(network_trial_busy()||app_update_active()||!app_update_ready(mqtt,console)){
        app_update_fail(fd,409,"Printer must be connected, idle and cold; another operation may be running");return;
    }
    if(!network_trial_script_ready()){app_update_fail(fd,409,"Qualified temporary network trial is not staged");return;}
    struct sockaddr_in local;socklen_t size=sizeof(local);char address[INET_ADDRSTRLEN];
    if(getsockname(fd,(struct sockaddr*)&local,&size)||local.sin_family!=AF_INET||
       !inet_ntop(AF_INET,&local.sin_addr,address,sizeof(address))){
        app_update_fail(fd,409,"Cannot identify the connection used by this panel");return;
    }
    if(strcmp(action,"restore")&&strcmp(address,"192.168.1.107")){
        app_update_fail(fd,409,"Open the panel at the qualified wired address 192.168.1.107");return;
    }
    int log=open("/tmp/cc2-network-panel-action.log",O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);
    struct stat logst;
    if(log<0||fstat(log,&logst)||!S_ISREG(logst.st_mode)||logst.st_uid!=geteuid()||(logst.st_mode&0077)){
        if(log>=0)close(log);
        app_update_fail(fd,503,"Private network action log unavailable");return;
    }
    long maximum=sysconf(_SC_OPEN_MAX);if(maximum<0)maximum=65536;
    char endpoint[80];snprintf(endpoint,sizeof(endpoint),"CC2_NETWORK_PANEL_LOCAL_IP=%s",address);
    pid_t child=fork();
    if(child==0){
        int null=open("/dev/null",O_RDONLY);
        if(null<0||dup2(null,0)<0||dup2(log,1)<0||dup2(log,2)<0)_exit(127);
        for(int i=3;i<maximum;i++)close(i);
        char *args[]={"sh",NETWORK_TRIAL_MANAGE,(char*)action,NULL};
        char *environment[]={"PATH=/usr/sbin:/usr/bin:/sbin:/bin",endpoint,NULL};
        execve("/bin/sh",args,environment);_exit(127);
    }
    close(log);
    if(child<0){app_update_fail(fd,503,"Cannot start the manual network action");return;}
    network_trial_child=child;network_trial_result=-1;
    const char *reply="{\"accepted\":true,\"temporary\":true}\n";
    respond(fd,202,"Accepted","application/json",reply,strlen(reply));
}
#endif
