/* Shared with the native maintenance coordinator. Use the same atomic mkdir
 * lock as the existing app installer; an unknown/stale object is never cleared
 * merely because a PID appears absent. Read-only status and emergency stop stay
 * available. This is not evidence that the printer itself is idle/cold. */
#ifndef CC2_MAINTENANCE_BARRIER_H
#define CC2_MAINTENANCE_BARRIER_H
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef CC2_MAINTENANCE_LOCK
#define CC2_MAINTENANCE_LOCK "/tmp/cc2-control-install.lock"
#endif
#ifndef CC2_MAINTENANCE_UID
#define CC2_MAINTENANCE_UID 0
#endif
struct maintenance_barrier {int fd;dev_t device;ino_t inode;};
#define MAINTENANCE_BARRIER_INIT {-1,0,0}
static inline int maintenance_barrier_active(void){
    struct stat st;
    return !lstat(CC2_MAINTENANCE_LOCK,&st) || errno!=ENOENT;
}
static inline int maintenance_request_blocked(const char *method,int emergency,int console_emergency){
    return strcmp(method,"GET") && strcmp(method,"HEAD") && !emergency && !console_emergency &&
           maintenance_barrier_active();
}
static inline int maintenance_barrier_acquire(struct maintenance_barrier *held){
    if(!held || held->fd>=0 || geteuid()!=CC2_MAINTENANCE_UID)return 0;
    if(mkdir(CC2_MAINTENANCE_LOCK,0700))return 0;
    int fd=open(CC2_MAINTENANCE_LOCK,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    struct stat st;
    if(fd<0 || fstat(fd,&st) || !S_ISDIR(st.st_mode) || st.st_uid!=CC2_MAINTENANCE_UID || (st.st_mode&07777)!=0700){
        if(fd>=0)close(fd);
        /* Do not guess whether a changed/uncertain pathname is ours. */
        return 0;
    }
    held->fd=fd;held->device=st.st_dev;held->inode=st.st_ino;
    return 1;
}
static inline int maintenance_barrier_release(struct maintenance_barrier *held){
    if(!held || held->fd<0)return 0;
    struct stat st;
    int okay=!lstat(CC2_MAINTENANCE_LOCK,&st) && S_ISDIR(st.st_mode) &&
             st.st_dev==held->device && st.st_ino==held->inode && st.st_uid==CC2_MAINTENANCE_UID &&
             (st.st_mode&07777)==0700;
    /* rmdir refuses nonempty directories. No recursive deletion or stale-lock
     * takeover is permitted; recovery of an uncertain lock is a separate task. */
    if(okay)okay=rmdir(CC2_MAINTENANCE_LOCK)==0;
    close(held->fd);held->fd=-1;return okay;
}
#endif
