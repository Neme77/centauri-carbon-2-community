#define _GNU_SOURCE
#include "reap_core.h"
#include <assert.h>
#ifdef NDEBUG
#error These qualification tests require active assertions.
#endif
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
static atomic_int handled,closed_count,hold;
static int create_error,allocate_error;
static void pause_short(void){struct timespec t={0,100000};nanosleep(&t,NULL);}
static void handle(void *object,int fd){
    assert(object==(void *)&handled);assert(fd>=0);
    while(atomic_load(&hold))pause_short();
    atomic_fetch_add(&handled,1);
}
static void reject(void *object,int fd){
    assert(object==(void *)&handled);assert(fd>=0);
    atomic_fetch_add(&closed_count,1);
}
static void *allocate(size_t n){return allocate_error?NULL:malloc(n);}
static int create(pthread_t *id,const pthread_attr_t *attr,void *(*fn)(void *),void *arg){
    return create_error?create_error:pthread_create(id,attr,fn,arg);
}
static cc2_reap_ops ops={allocate,free,create,pthread_tryjoin_np,handle,reject};
/* Equivalent to the qualified native stop's joinable/join/clear lifecycle. */
static void native_stop(cc2_thread_vector *v){
    for(pthread_t *p=v->begin;p&&p<v->end;p++){if(*p){assert(!pthread_join(*p,NULL));*p=(pthread_t)0;}}
    v->end=v->begin;
}
int main(void){
    cc2_thread_vector v={0};cc2_reap_stats stats={0};
    allocate_error=1;
    assert(cc2_reap_spawn(&v,&handled,1,&ops,&stats)==ENOMEM);
    assert(!v.begin&&stats.spawned==0&&atomic_load(&handled)==0&&atomic_load(&closed_count)==1);
    allocate_error=0;create_error=EAGAIN;
    assert(cc2_reap_spawn(&v,&handled,2,&ops,&stats)==EAGAIN);
    assert(v.end==v.begin&&stats.spawned==0&&atomic_load(&closed_count)==2);
    create_error=0;
    size_t maximum=0;
    for(int i=0;i<1200;i++){
        assert(!cc2_reap_spawn(&v,&handled,10+i,&ops,&stats));
        if(stats.retained>maximum)maximum=stats.retained;
        while(atomic_load(&handled)<i+1)pause_short();
        /* The handler may have returned before pthread teardown completes.
         * Reaping checks pthread completion, never just the handler counter. */
        pause_short();
    }
    native_stop(&v);
    assert(stats.spawned==1200&&maximum<=CC2_REAP_MAX_RETAINED&&stats.reaped>1100);
    atomic_store(&hold,1);
    for(int i=0;i<CC2_REAP_MAX_RETAINED;i++)assert(!cc2_reap_spawn(&v,&handled,2000+i,&ops,&stats));
    assert(stats.retained==CC2_REAP_MAX_RETAINED);
    assert(cc2_reap_spawn(&v,&handled,3000,&ops,&stats)==EAGAIN);
    assert(v.end-v.begin==CC2_REAP_MAX_RETAINED);
    atomic_store(&hold,0);
    native_stop(&v);
    assert(!cc2_reap_spawn(&v,&handled,4000,&ops,&stats));
    native_stop(&v);
    free(v.begin);
    assert(atomic_load(&closed_count)==3);
    printf("PASS: 1200 real sequential connections; max retained=%zu; allocation/EAGAIN refusal, recovery, bounded busy threads and native-style stop/restart\\n",maximum);
    return 0;
}
