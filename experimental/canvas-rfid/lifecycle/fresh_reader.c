/* Loopback observations only. No operation lock, process stop or device I/O.
 * The owning coordinator still verifies app/native identities and owns locks. */
#define _GNU_SOURCE
#include <signal.h>
#include <stdint.h>
#include <time.h>
static volatile sig_atomic_t sc_cancelled;
static uint64_t sc_now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t)||t.tv_sec<0)return 0;return (uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;}
static void cr_signal(int value){(void)value;sc_cancelled=1;}
#include "canvas_observation.h"
#include "readiness_policy.h"
int main(int argc,char **argv){
    if(argc!=2||strcmp(argv[1],"--check")){fprintf(stderr,"STOP: unsupported_readiness_observation\n");return 1;}
    signal(SIGINT,cr_signal);signal(SIGTERM,cr_signal);signal(SIGHUP,cr_signal);signal(SIGPIPE,SIG_IGN);
    char *printer=NULL,*uds=NULL;size_t psize=0,usize=0;
    ng_json *p=malloc(sizeof(*p)),*u=malloc(sizeof(*u));
    int okay=p&&u&&vc_http_body("/api/printer",&printer,&psize)&&vc_http_body("/api/uds",&uds,&usize)&&
        ng_json_parse(p,printer,psize)&&ng_json_parse(u,uds,usize)&&cr_ready(p,u)&&!sc_cancelled;
    free(p);free(u);free(printer);free(uds);
    if(!okay){fprintf(stderr,"STOP: fresh_idle_cold_observation_failed\n");return 1;}
    puts("LIVE GUARD PASS: fresh MQTT/native terminal idle state, no motion, zero heater targets, cold and no recovery/drying.");return 0;
}
