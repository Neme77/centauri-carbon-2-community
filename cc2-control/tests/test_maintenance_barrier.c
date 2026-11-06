#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
static char fixture_lock[256];
#define CC2_MAINTENANCE_LOCK fixture_lock
#define CC2_MAINTENANCE_UID geteuid()
#include "../src/maintenance_barrier.h"
int main(void){
    char root[]="/tmp/cc2-maintenance-barrier-XXXXXX";assert(mkdtemp(root));
    assert(snprintf(fixture_lock,sizeof(fixture_lock),"%s/lock",root)>0);
    struct maintenance_barrier held=MAINTENANCE_BARRIER_INIT,other=MAINTENANCE_BARRIER_INIT;
    assert(!maintenance_barrier_active() && !maintenance_request_blocked("POST",0,0));
    assert(maintenance_barrier_acquire(&held));
    assert(maintenance_barrier_active() && maintenance_request_blocked("POST",0,0));
    assert(maintenance_request_blocked("DELETE",0,0));
    assert(!maintenance_request_blocked("GET",0,0) && !maintenance_request_blocked("HEAD",0,0));
    assert(!maintenance_request_blocked("POST",1,0) && !maintenance_request_blocked("POST",0,1));
    assert(!maintenance_barrier_acquire(&held) && !maintenance_barrier_acquire(&other));
    assert(maintenance_barrier_release(&held) && !maintenance_barrier_active());
    assert(!maintenance_barrier_release(&held));
    assert(!symlink("/definitely-absent-maintenance-fixture",fixture_lock));
    assert(maintenance_barrier_active() && !maintenance_barrier_acquire(&held));assert(!unlink(fixture_lock));
    int fd=open(fixture_lock,O_WRONLY|O_CREAT|O_EXCL,0600);assert(fd>=0 && !close(fd));
    assert(maintenance_barrier_active() && !maintenance_barrier_acquire(&held));assert(!unlink(fixture_lock));
    assert(maintenance_barrier_acquire(&held));
    char child[280];assert(snprintf(child,sizeof(child),"%s/foreign",fixture_lock)>0);
    fd=open(child,O_WRONLY|O_CREAT|O_EXCL,0600);assert(fd>=0 && !close(fd));
    assert(!maintenance_barrier_release(&held) && maintenance_barrier_active());
    assert(!unlink(child) && !rmdir(fixture_lock));
    assert(maintenance_barrier_acquire(&held));
    char saved[280];assert(snprintf(saved,sizeof(saved),"%s/saved",root)>0);
    assert(!rename(fixture_lock,saved) && !mkdir(fixture_lock,0700));
    assert(!maintenance_barrier_release(&held) && maintenance_barrier_active());
    assert(!rmdir(fixture_lock) && !rmdir(saved));
    assert(!mkdir(fixture_lock,0700) && !chmod(fixture_lock,0755));
    assert(maintenance_barrier_active() && !maintenance_barrier_acquire(&held));
    assert(!rmdir(fixture_lock) && !rmdir(root));
    puts("PASS: shared installer/maintenance ownership, status/emergency availability, malformed locks and foreign replacement refusal");
    return 0;
}
