/* Fixture-only linked clock. Never linked into the production worker.
 * Exercise its unchanged 15-minute expiry check even on a newly booted VM. */
#include <time.h>
int __real_clock_gettime(clockid_t clock,struct timespec *value);
int __wrap_clock_gettime(clockid_t clock,struct timespec *value) {
    int result=__real_clock_gettime(clock,value);
    if(!result && clock==CLOCK_MONOTONIC)value->tv_sec+=901;
    return result;
}
