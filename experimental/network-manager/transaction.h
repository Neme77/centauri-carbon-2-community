#ifndef CC2_NETWORK_TRANSACTION_H
#define CC2_NETWORK_TRANSACTION_H
#include <stdint.h>
#include <string.h>

/* Pure transaction policy; no network syscalls or persistence. The independent
 * worker must persist its rollback journal BEFORE invoking begin, take guards
 * from trusted local observations, and perform/acknowledge every action.
 * This is not an enabled connection editor or an automatic quality policy. */
typedef enum { NT_WIFI=1,NT_ETHERNET=2 } nt_mode;
typedef enum { NT_IDLE,NT_APPLYING,NT_PENDING,NT_COMMITTING,NT_RESTORING,NT_FAILED } nt_phase;
typedef enum { NT_NONE,NT_EXPIRED,NT_APPLY_FAILED,NT_SAVE_FAILED,NT_CANCELLED,NT_CLOCK_INVALID } nt_reason;
typedef enum { NT_OK,NT_DENIED,NT_BUSY,NT_UNCHANGED } nt_result;
typedef struct {
    int authorized,fresh,idle,cold,targets_zero,component_ready,target_ready;
    int other_operation,journal_ready,rollback_available;
} nt_guards;
typedef struct {
    nt_mode current,previous,target;
    nt_phase phase;
    nt_reason reason;
    uint64_t started,deadline;
    char token[33],last_token[33];
} nt_transaction;
static void nt_init(nt_transaction *t,nt_mode mode){memset(t,0,sizeof(*t));t->current=mode;t->previous=mode;t->target=mode;}
static int nt_token_valid(const char *token){
    if(!token || strlen(token)!=32)return 0;
    for(int i=0;i<32;i++)if(!((token[i]>='0'&&token[i]<='9')||(token[i]>='a'&&token[i]<='f')))return 0;
    return 1;
}
static int nt_matches(const nt_transaction *t,const char *token){
    if(!nt_token_valid(token))return 0;
    unsigned diff=0;for(int i=0;i<32;i++)diff|=(unsigned char)t->token[i]^(unsigned char)token[i];
    return diff==0;
}
static void nt_restore(nt_transaction *t,nt_reason reason){t->phase=NT_RESTORING;t->reason=reason;}
static void nt_tick(nt_transaction *t,uint64_t now){
    if(t->phase!=NT_APPLYING&&t->phase!=NT_PENDING&&t->phase!=NT_COMMITTING)return;
    if(now<t->started)nt_restore(t,NT_CLOCK_INVALID);
    else if(now>=t->deadline)nt_restore(t,NT_EXPIRED);
}
static nt_result nt_begin(nt_transaction *t,nt_mode target,const nt_guards *g,const char *token,uint64_t now){
    if(t->phase!=NT_IDLE)return NT_BUSY;
    if((t->current!=NT_WIFI&&t->current!=NT_ETHERNET)||(target!=NT_WIFI&&target!=NT_ETHERNET)||!g||!g->authorized||!g->fresh||!g->idle||!g->cold||
       !g->targets_zero||g->other_operation||!g->journal_ready||!g->rollback_available||!g->target_ready||
       (target==NT_ETHERNET&&!g->component_ready)||!nt_token_valid(token)||!strcmp(token,t->last_token)||
       now>UINT64_MAX-120000)return NT_DENIED;
    if(target==t->current)return NT_UNCHANGED;
    t->previous=t->current;t->target=target;t->phase=NT_APPLYING;t->reason=NT_NONE;
    t->started=now;t->deadline=now+120000;
    memcpy(t->token,token,33);memcpy(t->last_token,token,33);return NT_OK;
}
static nt_result nt_applied(nt_transaction *t,const char *token,int success,uint64_t now){
    nt_tick(t,now);
    if(t->phase!=NT_APPLYING||!nt_matches(t,token))return NT_DENIED;
    if(!success)nt_restore(t,NT_APPLY_FAILED);else t->phase=NT_PENDING;
    return NT_OK;
}
static nt_result nt_confirm(nt_transaction *t,const char *token,int authorized,
                            nt_mode observed_route,nt_mode request_interface,int healthy,uint64_t now){
    nt_tick(t,now);
    if(t->phase!=NT_PENDING||!nt_matches(t,token)||!authorized||!healthy||
       observed_route!=t->target||request_interface!=t->target)return NT_DENIED;
    /* Confirmation permits a journaled save; success is acknowledged separately.
     * A failed/late save must still restore the previous configuration. */
    t->phase=NT_COMMITTING;return NT_OK;
}
static nt_result nt_saved(nt_transaction *t,const char *token,int success,uint64_t now){
    nt_tick(t,now);
    if(t->phase!=NT_COMMITTING||!nt_matches(t,token))return NT_DENIED;
    if(!success){nt_restore(t,NT_SAVE_FAILED);return NT_OK;}
    t->current=t->target;t->phase=NT_IDLE;t->deadline=0;t->token[0]=0;return NT_OK;
}
static nt_result nt_cancel(nt_transaction *t,const char *token){
    if((t->phase!=NT_APPLYING&&t->phase!=NT_PENDING)||!nt_matches(t,token))return NT_DENIED;
    nt_restore(t,NT_CANCELLED);return NT_OK;
}
static nt_result nt_restored(nt_transaction *t,const char *token,int verified){
    if(t->phase!=NT_RESTORING||!nt_matches(t,token))return NT_DENIED;
    if(!verified){t->phase=NT_FAILED;return NT_OK;}
    t->current=t->previous;t->target=t->previous;t->phase=NT_IDLE;t->deadline=0;t->token[0]=0;return NT_OK;
}
#endif
