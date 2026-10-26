#define _GNU_SOURCE
#include "reap_core.h"
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef struct { void (*handle)(void *, int); void *object; int fd; } arguments;
static void *client_entry(void *opaque) {
    arguments value=*(arguments *)opaque;
    free(opaque);
    value.handle(value.object,value.fd);
    return NULL;
}
static int bounds(const cc2_thread_vector *v,size_t *count,size_t *capacity) {
    uintptr_t b=(uintptr_t)v->begin,e=(uintptr_t)v->end,c=(uintptr_t)v->capacity;
    if(!b) { *count=*capacity=0; return !e&&!c; }
    if(e<b||c<e||b%_Alignof(pthread_t)||(e-b)%sizeof(pthread_t)||(c-b)%sizeof(pthread_t))return 0;
    *count=(e-b)/sizeof(pthread_t);*capacity=(c-b)/sizeof(pthread_t);
    /* The qualified binary can have retained old records before activation,
     * but startup activation should start empty. Bound any malformed read. */
    return *count<=4096&&*capacity<=8192;
}
static int refuse(void *object,int fd,const cc2_reap_ops *ops,cc2_reap_stats *stats,int error) {
    stats->rejected++;stats->last_error=error;
    ops->reject(object,fd);
    return error;
}
int cc2_reap_spawn(cc2_thread_vector *v,void *object,int fd,const cc2_reap_ops *ops,cc2_reap_stats *stats) {
    size_t count,capacity;
    if(!bounds(v,&count,&capacity))return refuse(object,fd,ops,stats,EINVAL);
    size_t retained=0;
    int error=0;
    for(size_t i=0;i<count;i++) {
        pthread_t id=v->begin[i];
        if(!id)continue;
        int rc=ops->tryjoin(id,NULL);
        if(!rc)stats->reaped++;
        else { v->begin[retained++]=id; if(rc!=EBUSY&&!error)error=rc; }
    }
    for(size_t i=retained;i<count;i++)v->begin[i]=(pthread_t)0;
    if(v->begin)v->end=v->begin+retained;
    stats->retained=retained;
    if(error)return refuse(object,fd,ops,stats,error);
    if(retained>=CC2_REAP_MAX_RETAINED)return refuse(object,fd,ops,stats,EAGAIN);
    if(capacity<=retained) {
        pthread_t *replacement=ops->allocate_vector(CC2_REAP_MAX_RETAINED*sizeof(pthread_t));
        if(!replacement)return refuse(object,fd,ops,stats,ENOMEM);
        memset(replacement,0,CC2_REAP_MAX_RETAINED*sizeof(pthread_t));
        if(retained)memcpy(replacement,v->begin,retained*sizeof(pthread_t));
        if(v->begin)ops->free_vector(v->begin);
        v->begin=replacement;v->end=replacement+retained;v->capacity=replacement+CC2_REAP_MAX_RETAINED;
    }
    arguments *args=malloc(sizeof(*args));
    if(!args)return refuse(object,fd,ops,stats,ENOMEM);
    *args=(arguments){ops->handle,object,fd};
    pthread_t id=(pthread_t)0;
    int rc=ops->create(&id,NULL,client_entry,args);
    if(rc) { free(args);return refuse(object,fd,ops,stats,rc); }
    *v->end++=id;
    stats->spawned++;stats->retained=retained+1;stats->last_error=0;
    return 0;
}
