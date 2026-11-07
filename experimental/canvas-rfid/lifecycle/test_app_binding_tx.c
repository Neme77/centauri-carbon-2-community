/* Test driver only: no live readiness, native process or installer authority. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void au_test_fault(const char *point,unsigned index);
#define AU_FAULT(point,index) au_test_fault(point,index)
#include "app_binding_tx.h"
#ifndef AU_TEST_CONFIG
#error Test fixture configuration is required
#endif
#include AU_TEST_CONFIG
static void au_test_fault(const char *point,unsigned index){
    char key[80];snprintf(key,sizeof(key),"%s:%u",point,index);
    const char *requested=getenv("AU_TEST_CRASH");
    if(requested&&!strcmp(requested,key))_exit(91);
}
int main(int argc,char **argv){
    if(argc!=2)return 2;
    int prepare=!strcmp(argv[1],"--prepare"),apply=!strcmp(argv[1],"--apply"),rollback=!strcmp(argv[1],"--rollback");
    if(!prepare&&!apply&&!rollback)return 2;
    umask(077);
    struct au_store store;
    if(!au_open(&store,prepare))return 1;
    const char *barrier=getenv("AU_TEST_WRONG_FD")?AU_TEST_OTHER:AU_BARRIER;
    int held=open(barrier,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    int source=open(AU_TEST_SOURCE,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);
    int okay=prepare?au_prepare(&store,&test_plan,source,held):au_run(&store,&test_plan,held,rollback);
    if(source>=0)close(source);
    if(held>=0)close(held);
    au_close(&store);
    if(!okay){fputs("REFUSED: application binding transaction\n",stderr);return 1;}
    return 0;
}
