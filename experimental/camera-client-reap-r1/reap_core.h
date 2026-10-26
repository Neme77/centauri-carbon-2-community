#ifndef CC2_REAP_CORE_H
#define CC2_REAP_CORE_H
#include <pthread.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Exact target ABI: vector<std::thread> is three pointers; each element is a
 * pthread_t. The binary-specific adapter validates the executable separately. */
typedef struct { pthread_t *begin, *end, *capacity; } cc2_thread_vector;
typedef struct {
    void *(*allocate_vector)(size_t);
    void (*free_vector)(void *);
    int (*create)(pthread_t *, const pthread_attr_t *, void *(*)(void *), void *);
    int (*tryjoin)(pthread_t, void **);
    void (*handle)(void *, int);
    void (*reject)(void *, int);
} cc2_reap_ops;
typedef struct { unsigned long spawned, reaped, rejected; size_t retained; int last_error; } cc2_reap_stats;
/* Native admission still limits active sockets to four. Allow a small bounded
 * number of threads which have removed their socket but have not yet returned. */
#define CC2_REAP_MAX_RETAINED 8
int cc2_reap_spawn(cc2_thread_vector *, void *, int, const cc2_reap_ops *, cc2_reap_stats *);
#ifdef __cplusplus
}
#endif
#endif
