"""Test the exact manual bridge with a harmless pinned shell fixture."""
import hashlib, pathlib, subprocess, tempfile
root=pathlib.Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='cc2-network-bridge-') as temp:
    temp=pathlib.Path(temp)
    script=temp/'manage.sh'
    script.write_text('#!/bin/sh\n[ "$CC2_NETWORK_PANEL_LOCAL_IP" = 192.168.1.107 ] || exit 31\n[ "$1" = start ] || exit 32\n[ -z "${SSH_CONNECTION:-}" ] || exit 33\nsleep 1\nexit 0\n')
    script.chmod(0o600)
    header=(root/'manual_trial.h').read_text().replace('/tmp/cc2-network-compat-r1/manage.sh',str(script)).replace('/tmp/cc2-network-panel-action.log',str(temp/'action.log'))
    (temp/'bridge.h').write_text(header)
    (temp/'network_trial_pins.h').write_text('#define NETWORK_TRIAL_MANAGE_SHA "'+hashlib.sha256(script.read_bytes()).hexdigest()+'"\n')
    source=r"""
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "SHA_HEADER"
typedef int mqtt_client;typedef int console_state;
static int ready=1,response;
static const char *local_address="192.168.1.107";
static int app_update_active(void){return 0;}
static int app_update_ready(const mqtt_client *mqtt,console_state *console){(void)mqtt;(void)console;return ready;}
static void app_update_fail(int fd,int status,const char *reason){(void)fd;(void)reason;response=status;}
static void respond(int fd,int status,const char *reason,const char *type,const char *body,size_t size){(void)fd;(void)reason;(void)type;(void)body;(void)size;response=status;}
static int fixture_getsockname(int fd,struct sockaddr *out,socklen_t *size){(void)fd;(void)size;struct sockaddr_in *a=(struct sockaddr_in*)out;a->sin_family=AF_INET;return inet_pton(AF_INET,local_address,&a->sin_addr)==1?0:-1;}
#define getsockname fixture_getsockname
#include "bridge.h"
#undef getsockname
int main(void){
 assert(network_trial_script_ready());
 network_trial_select(1,"","start; id",9,NULL,NULL);assert(response==400&&!network_trial_busy());
 ready=0;network_trial_select(1,"","start",5,NULL,NULL);assert(response==409&&!network_trial_busy());ready=1;
 local_address="192.168.1.103";network_trial_select(1,"","start",5,NULL,NULL);assert(response==409&&!network_trial_busy());
 local_address="192.168.1.107";network_trial_select(1,"","start",5,NULL,NULL);assert(response==202&&network_trial_busy());
 network_trial_select(1,"","wifi",4,NULL,NULL);assert(response==409);
 for(int i=0;i<100&&network_trial_busy();i++)usleep(50000);
 assert(!network_trial_busy()&&network_trial_result==0);
 network_trial_response(1,NULL,NULL);assert(response==200);
 int f=open(NETWORK_TRIAL_MANAGE,O_WRONLY|O_APPEND);assert(f>=0);assert(write(f,"# modified\n",11)==11);close(f);
 assert(!network_trial_script_ready());
 network_trial_select(1,"","wifi",4,NULL,NULL);assert(response==409&&!network_trial_busy());
 return 0;
}
""".replace('SHA_HEADER',str(root.parents[1]/'cc2-control/src/sha256.h'))
    harness=temp/'test.c';harness.write_text(source)
    binary=temp/'test'
    subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Wpedantic','-Werror','-fsanitize=address,undefined','-fno-pie','-no-pie',str(harness),'-o',str(binary)],check=True,timeout=20)
    subprocess.run([binary],check=True,timeout=10)
print('PASS: manual bridge without LAN-code gate, idle guard, real endpoint, pinned helper, asynchronous fixed argv/environment and coalescing')
