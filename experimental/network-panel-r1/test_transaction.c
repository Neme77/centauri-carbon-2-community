#include "transaction.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
static const char *a="0123456789abcdef0123456789abcdef";
static const char *b="abcdef0123456789abcdef0123456789";
static nt_guards ready(void){nt_guards g={1,1,1,1,1,1,1,0,1,1};return g;}
static void begin(nt_transaction *t){nt_init(t,NT_WIFI);nt_guards g=ready();assert(nt_begin(t,NT_ETHERNET,&g,a,1000)==NT_OK);}
int main(void){
    nt_transaction t;nt_guards g=ready();
    memset(&t,0,sizeof(t));assert(nt_begin(&t,NT_ETHERNET,&g,a,1000)==NT_DENIED);
    /* Each missing safety observation independently denies the operation. */
    size_t required[]={offsetof(nt_guards,authorized),offsetof(nt_guards,fresh),offsetof(nt_guards,idle),
        offsetof(nt_guards,cold),offsetof(nt_guards,targets_zero),offsetof(nt_guards,component_ready),
        offsetof(nt_guards,target_ready),offsetof(nt_guards,journal_ready),offsetof(nt_guards,rollback_available)};
    for(size_t i=0;i<sizeof(required)/sizeof(required[0]);i++){
        nt_init(&t,NT_WIFI);g=ready();*(int*)((char*)&g+required[i])=0;
        assert(nt_begin(&t,NT_ETHERNET,&g,a,1000)==NT_DENIED&&t.phase==NT_IDLE);
    }
    nt_init(&t,NT_WIFI);g=ready();g.other_operation=1;assert(nt_begin(&t,NT_ETHERNET,&g,a,1000)==NT_DENIED);
    g=ready();assert(nt_begin(&t,(nt_mode)3,&g,a,1000)==NT_DENIED); /* Auto not implemented. */
    assert(nt_begin(&t,NT_ETHERNET,&g,"short",1000)==NT_DENIED);
    assert(nt_begin(&t,NT_ETHERNET,&g,a,UINT64_MAX-3)==NT_DENIED);
    assert(nt_begin(&t,NT_WIFI,&g,a,1000)==NT_UNCHANGED);
    begin(&t);assert(nt_begin(&t,NT_ETHERNET,&g,b,1001)==NT_BUSY);
    assert(nt_confirm(&t,a,1,NT_ETHERNET,NT_ETHERNET,1,1001)==NT_DENIED); /* apply not acknowledged */
    assert(nt_applied(&t,b,1,1001)==NT_DENIED);
    assert(nt_applied(&t,a,1,1100)==NT_OK&&t.phase==NT_PENDING);
    assert(nt_confirm(&t,b,1,NT_ETHERNET,NT_ETHERNET,1,1200)==NT_DENIED); /* stale tab */
    assert(nt_confirm(&t,a,0,NT_ETHERNET,NT_ETHERNET,1,1200)==NT_DENIED);
    assert(nt_confirm(&t,a,1,NT_WIFI,NT_ETHERNET,1,1200)==NT_DENIED);
    assert(nt_confirm(&t,a,1,NT_ETHERNET,NT_WIFI,1,1200)==NT_DENIED); /* wrong interface */
    assert(nt_confirm(&t,a,1,NT_ETHERNET,NT_ETHERNET,0,1200)==NT_DENIED);
    assert(nt_confirm(&t,a,1,NT_ETHERNET,NT_ETHERNET,1,1200)==NT_OK&&t.phase==NT_COMMITTING);
    assert(t.current==NT_WIFI); /* confirmation alone has not persisted a new mode */
    assert(nt_saved(&t,a,1,1300)==NT_OK&&t.phase==NT_IDLE&&t.current==NT_ETHERNET);
    assert(nt_confirm(&t,a,1,NT_ETHERNET,NT_ETHERNET,1,1400)==NT_DENIED);
    assert(nt_begin(&t,NT_WIFI,&g,a,2000)==NT_DENIED); /* no nonce reuse */
    begin(&t);assert(nt_applied(&t,a,0,1100)==NT_OK&&t.phase==NT_RESTORING&&t.reason==NT_APPLY_FAILED);
    assert(nt_restored(&t,b,1)==NT_DENIED);
    assert(nt_restored(&t,a,0)==NT_OK&&t.phase==NT_FAILED);
    assert(nt_begin(&t,NT_ETHERNET,&g,b,2000)==NT_BUSY); /* failed restore is not "done" */
    begin(&t);assert(nt_cancel(&t,a)==NT_OK&&t.reason==NT_CANCELLED);
    assert(nt_restored(&t,a,1)==NT_OK&&t.current==NT_WIFI&&t.phase==NT_IDLE);
    begin(&t);assert(nt_applied(&t,a,1,1100)==NT_OK);nt_tick(&t,121000);
    assert(t.phase==NT_RESTORING&&t.reason==NT_EXPIRED); /* browser can disappear */
    assert(nt_confirm(&t,a,1,NT_ETHERNET,NT_ETHERNET,1,121001)==NT_DENIED);
    begin(&t);nt_tick(&t,999);assert(t.phase==NT_RESTORING&&t.reason==NT_CLOCK_INVALID);
    begin(&t);assert(nt_applied(&t,a,1,1100)==NT_OK);
    assert(nt_confirm(&t,a,1,NT_ETHERNET,NT_ETHERNET,1,1200)==NT_OK);
    assert(nt_saved(&t,a,0,1300)==NT_OK&&t.phase==NT_RESTORING&&t.reason==NT_SAVE_FAILED);
    assert(nt_restored(&t,a,1)==NT_OK&&t.current==NT_WIFI);
    begin(&t);assert(nt_applied(&t,a,1,1100)==NT_OK);
    assert(nt_confirm(&t,a,1,NT_ETHERNET,NT_ETHERNET,1,1200)==NT_OK);
    assert(nt_saved(&t,a,1,121000)==NT_DENIED&&t.phase==NT_RESTORING); /* late commit */
    puts("PASS: transaction guards, stale tabs, interface proof, journaled commit, deadlines and failed restoration");
    return 0;
}
