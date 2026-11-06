#define _GNU_SOURCE
#include "journal.h"
#include <assert.h>
#include <stdlib.h>
#include <sys/wait.h>

static struct cl_facts facts(void) {
    struct cl_facts f={1,1,1,1,1,1,1,1,1,1,1};return f;
}
static void step(unsigned *phase,enum cl_event event,struct cl_facts *f) {
    unsigned next=999;assert(cl_transition(*phase,event,f,&next));*phase=next;
}
static void policy_tests(void) {
    struct cl_facts f=facts();unsigned p=CL_STOCK;
    step(&p,CL_STAGE_HOST,&f);assert(p==CL_HOST_STAGED);
    assert(cl_boot_action(p,1,1,1,1)==CL_BOOT_GUARDED);
    assert(cl_boot_action(p,1,1,1,0)==CL_BOOT_BLOCK);
    f.new_boot=0;unsigned next=999;assert(!cl_transition(p,CL_VERIFY_HOST_BOOT,&f,&next) && next==999);
    f.new_boot=1;step(&p,CL_VERIFY_HOST_BOOT,&f);step(&p,CL_BEGIN_FILTER,&f);
    assert(cl_boot_action(p,1,1,1,1)==CL_BOOT_RECOVER_STOCK);
    f.complete_ack=0;assert(!cl_transition(p,CL_ACK_FILTER,&f,&next));
    f.complete_ack=1;step(&p,CL_ACK_FILTER,&f);
    assert(cl_boot_action(p,1,1,1,0)==CL_BOOT_BLOCK);
    assert(cl_boot_action(p,1,1,1,1)==CL_BOOT_GUARDED);
    step(&p,CL_VERIFY_FILTER_BOOT,&f);assert(p==CL_ACTIVE);
    assert(!cl_update_allowed(p,0,1) && !cl_update_allowed(p,1,0) && cl_update_allowed(p,1,1));
    assert(!cl_transition(p,CL_REMOVE_HOST,&f,&next));
    step(&p,CL_BEGIN_STOCK,&f);assert(cl_boot_action(p,1,1,1,1)==CL_BOOT_RECOVER_STOCK);
    step(&p,CL_ACK_STOCK,&f);assert(!cl_transition(p,CL_REMOVE_HOST,&f,&next));
    step(&p,CL_VERIFY_STOCK_BOOT,&f);step(&p,CL_REMOVE_HOST,&f);
    step(&p,CL_VERIFY_HOST_REMOVED,&f);assert(p==CL_DISABLED);
    assert(cl_update_allowed(p,0,0));
    p=CL_FILTER_INTENT;step(&p,CL_ACK_STOCK,&f);assert(p==CL_STOCK_ACKED);
    for(unsigned phase=0;phase<CL_PHASE_COUNT+2;phase++) {
        assert(cl_boot_action(phase,0,1,1,1)==CL_BOOT_BLOCK);
        assert(cl_boot_action(phase,1,0,1,1)==CL_BOOT_BLOCK);
        if(phase==CL_FILTER_INTENT || phase==CL_STOCK_INTENT)
            assert(cl_boot_action(phase,1,1,0,1)==CL_BOOT_BLOCK);
        for(unsigned mask=0;mask<2048;mask++) {
            struct cl_facts g={mask&1,mask&2,mask&4,mask&8,mask&16,mask&32,
                               mask&64,mask&128,mask&256,mask&512,mask&1024};
            for(unsigned event=0;event<=CL_VERIFY_HOST_REMOVED;event++) {
                unsigned out=999;
                int okay=cl_transition(phase,(enum cl_event)event,&g,&out);
                if(!okay) {assert(out==999);continue;}
                assert(cl_valid(out) && g.profile && g.backup && g.barrier);
                if(out==CL_FILTER_INTENT) assert(phase==CL_HOST_READY && g.guard && cl_idle(&g));
                if(out==CL_HOST_REMOVE_INTENT) assert(phase==CL_STOCK_VERIFIED && cl_idle(&g));
                if(out==CL_FILTER_ACKED || out==CL_STOCK_ACKED) assert(g.complete_ack);
                if(out==CL_HOST_READY || out==CL_ACTIVE || out==CL_STOCK_VERIFIED)
                    assert(g.new_boot && g.guard && g.canvas && cl_idle(&g));
                if(out==CL_DISABLED) assert(g.new_boot && g.host_removed && g.canvas);
            }
        }
    }
    puts("PASS: lifecycle ordering, reboot evidence, incomplete ACKs, guarded boot and update admission (exhaustive fact combinations)");
}
static void storage_tests(void) {
    char path[]="/tmp/cc2-canvas-lifecycle-XXXXXX";assert(mkdtemp(path));
    struct cl_store s,t;struct cl_record r={0},got;
    r.phase=CL_HOST_STAGED;r.generation=1;r.transaction[0]=7;r.plan[0]=42;
    memcpy(r.boot,"00000000-0000-0000-0000-000000000001",37);
    assert(cl_store_open(&s,path));assert(!cl_store_open(&t,path));
    assert(!cl_store_read(&s,&got));assert(cl_store_commit(&s,&r,0));
    assert(cl_store_read(&s,&got) && got.phase==r.phase && got.generation==1);
    unsigned char wire[CL_PAYLOAD_SIZE];assert(cl_encode(&r,wire));
    assert(!memcmp(wire,"CC2-CANVAS-LC1",14) && wire[14]==0 && wire[15]==0);
    for(unsigned i=0;i<CL_PAYLOAD_SIZE;i++) {
        unsigned char copy[CL_PAYLOAD_SIZE];memcpy(copy,wire,sizeof(copy));copy[i]^=0x80;
        /* Full stored checksum rejects any one-byte payload corruption. */
        int fd=openat(s.directory,"lifecycle",O_WRONLY|O_CLOEXEC);assert(fd>=0);
        assert(pwrite(fd,copy,sizeof(copy),0)==sizeof(copy));close(fd);
        assert(!cl_store_read(&s,&got));
    }
    int fd=openat(s.directory,"lifecycle",O_WRONLY|O_CLOEXEC);assert(fd>=0);
    assert(pwrite(fd,wire,sizeof(wire),0)==sizeof(wire));close(fd);
    assert(cl_store_read(&s,&got));
    r.generation=2;r.phase=CL_HOST_READY;
    assert(!cl_store_commit(&s,&r,0));
    struct cl_facts evidence=facts();
    assert(!cl_advance(&s,CL_VERIFY_HOST_BOOT,&evidence,r.boot));
    const char boot2[]="00000000-0000-0000-0000-000000000002";
    assert(cl_advance(&s,CL_VERIFY_HOST_BOOT,&evidence,boot2));
    memcpy(r.boot,boot2,37);
    assert(!cl_store_commit(&s,&r,1));
    struct cl_record wrong=r;wrong.generation=3;wrong.transaction[0]++;
    assert(!cl_store_commit(&s,&wrong,2));wrong=r;wrong.generation=3;wrong.plan[0]++;
    assert(!cl_store_commit(&s,&wrong,2));
    fd=openat(s.directory,"lifecycle.next",O_WRONLY|O_CREAT|O_EXCL,0600);assert(fd>=0);close(fd);
    r.generation=3;assert(cl_store_commit(&s,&r,2));
    assert(cl_store_read(&s,&got) && got.generation==3);
    assert(!fstatat(s.directory,"lifecycle.next",&(struct stat){0},AT_SYMLINK_NOFOLLOW));
    assert(!unlinkat(s.directory,"lifecycle.next",0));
    assert(!renameat(s.directory,"lifecycle",s.directory,"saved"));
    assert(!symlinkat("saved",s.directory,"lifecycle"));assert(!cl_store_read(&s,&got));
    r.generation=4;assert(!cl_store_commit(&s,&r,3));assert(!unlinkat(s.directory,"lifecycle",0));
    assert(!renameat(s.directory,"saved",s.directory,"lifecycle"));
    assert(!fchmodat(s.directory,"lifecycle",0644,0));assert(!cl_store_read(&s,&got));
    assert(!fchmodat(s.directory,"lifecycle",0600,0));
    /* Reopen after a process exit: advisory lock cannot become an orphan. */
    cl_store_close(&s);assert(cl_store_open(&s,path));assert(cl_store_read(&s,&got));
    assert(got.generation==3);assert(cl_store_commit(&s,&r,3));
    fd=openat(s.directory,"lifecycle",O_WRONLY|O_CLOEXEC);assert(fd>=0);
    assert(!ftruncate(fd,CL_RECORD_SIZE-1));close(fd);assert(!cl_store_read(&s,&got));
    assert(!unlinkat(s.directory,"lifecycle",0));assert(!unlinkat(s.directory,"lifecycle.lock",0));
    cl_store_close(&s);assert(!rmdir(path));
    puts("PASS: private durable journal, stale generation/plan refusal, corruption, truncation, symlink, interrupted staging and lock reopening");
}
int main(void) {
    umask(077);policy_tests();storage_tests();return 0;
}
