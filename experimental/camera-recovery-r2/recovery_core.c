#include "recovery_core.h"
#include <string.h>
/* Native JSON map ordering and ETX framing. No session material is included. */
const char cc2_insertion[]="{\"id\":-1,\"method\":\"status_report\",\"result\":{\"level\":\"warning\",\"message\":\"Camera device insertion\"}}\003";
const size_t cc2_insertion_length=sizeof(cc2_insertion)-1;
void cc2_recovery_presence(cc2_recovery *s,int present) {
    s->ready=present;
    if(present) {
        ++s->epoch;
        if(!s->epoch){memset(s->records,0,sizeof(s->records));s->epoch=1;}
    }
}
int cc2_recovery_claim(cc2_recovery *s,cc2_socket_key key) {
    if(!s->ready||!key.inode)return 0;
    for(unsigned i=0;i<CC2_RECOVERY_RECORDS;i++) {
        cc2_record *r=&s->records[i];
        if(r->epoch==s->epoch&&r->attempted&&r->key.device==key.device&&r->key.inode==key.inode) {
            ++s->duplicate;return 0;
        }
    }
    cc2_record *r=&s->records[s->next++%CC2_RECOVERY_RECORDS];
    *r=(cc2_record){key,s->epoch,1};
    return 1;
}
void cc2_recovery_result(cc2_recovery *s,int success) {
    if(success)++s->sent;else ++s->failed;
}
int cc2_recovery_is_insertion(const char *p,size_t n) {
    /* Only suppress the exact native unsolicited event, never arbitrary replies. */
    return n==cc2_insertion_length&&p&&!memcmp(p,cc2_insertion,n);
}
