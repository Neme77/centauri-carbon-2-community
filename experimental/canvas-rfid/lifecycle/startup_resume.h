/* Fixed-profile boot resume, called before recovery-profile validation.
 * Caller already verified the compiled worker identity and holds lifecycle.lock.
 * This does not stage a new transaction or authorize fresh installation/removal.
 * Every payload/path/hash and expected recovery-plan digest comes from the
 * qualified builder, never from a browser request or an untrusted state file. */
#ifndef CC2_CANVAS_STARTUP_RESUME_H
#define CC2_CANVAS_STARTUP_RESUME_H
#include "startup_tx.h"
static inline int sr_resume(struct cl_store *store,const struct tx_file *files,unsigned count,
                            const unsigned char recovery_plan[32]) {
    struct cl_record record;char plan[65];
    if(!store || store->directory<0 || store->lock<0 || !recovery_plan || !cl_store_read(store,&record) ||
       memcmp(record.plan,recovery_plan,32) || !tx_plan(files,count,plan))return 0;
    /* Require the already prepared private transaction directory. Boot cannot
     * manufacture an empty transaction or treat missing backups as a clean state. */
    int snapshots=openat(store->directory,"startup-tx",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(!cl_private(snapshots,1,0)){if(snapshots>=0)close(snapshots);return 0;}
    int okay=tx_receipt(snapshots,"ready",plan,0),all_new=1,all_old=1;
    for(unsigned i=0;okay && i<count;i++) {
        char name[32];int current=tx_current(files+i);
        if(current<0){okay=0;break;}
        if(current!=1)all_new=0;
        if(current!=0)all_old=0;
        snprintf(name,sizeof(name),"%u.old",i);
        okay=tx_read(snapshots,name,files[i].old_sha,0600,NULL,NULL);
        snprintf(name,sizeof(name),"%u.new",i);
        if(okay)okay=tx_read(snapshots,name,files[i].new_sha,0600,NULL,NULL);
    }
    if(!okay){close(snapshots);return 0;}
    if(record.phase==CL_HOST_REMOVE_INTENT) {
        close(snapshots);
        /* This phase is durable permission to finish a removal which began
         * after stock Canvas verification and fresh idle/cold authorization. */
        return tx_run(store,files,count,1);
    }
    if(record.phase==CL_DISABLED) {
        okay=all_old && tx_receipt(snapshots,"restored",plan,0);
        close(snapshots);return okay;
    }
    if(all_new && tx_receipt(snapshots,"applied",plan,0)) {
        close(snapshots);return 1;
    }
    close(snapshots);
    /* A mixed target set is allowed only while its original installation is
     * durably staged. Any foreign byte/mode or later lifecycle phase blocks. */
    if(record.phase!=CL_HOST_STAGED && record.phase!=CL_FILTER_ACKED)return 0;
    return tx_run(store,files,count,0);
}
#endif
