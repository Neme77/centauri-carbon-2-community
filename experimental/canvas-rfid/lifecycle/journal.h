/* Root-private, bounded, atomic lifecycle journal. A missing journal is an
 * error; installation must create it before enabling the immutable boot gate.
 * The checksum detects corruption, not malicious writes by root. */
#ifndef CC2_CANVAS_LIFECYCLE_JOURNAL_H
#define CC2_CANVAS_LIFECYCLE_JOURNAL_H
#include "policy.h"
#include "../host/sha256.h"
#include <sys/file.h>
#include <sys/stat.h>
#define CL_PAYLOAD_SIZE 128u
#define CL_RECORD_SIZE 192u
#define CL_JOURNAL_MAGIC "CC2-CANVAS-LC1"
struct cl_record {
    unsigned phase;
    uint64_t generation;
    unsigned char transaction[16], plan[32];
    char boot[37];
};
struct cl_store { int directory, lock; };
static inline int cl_nonzero(const unsigned char *p, size_t size) {
    unsigned value=0;
    for (size_t i=0;i<size;i++) value |= p[i];
    return value != 0;
}
static inline int cl_boot_id_valid(const char *boot) {
    for (unsigned i=0;i<36;i++) {
        int dash=i==8 || i==13 || i==18 || i==23;
        if (dash ? boot[i]!='-' : !((boot[i]>='0' && boot[i]<='9') || (boot[i]>='a' && boot[i]<='f')))
            return 0;
    }
    return boot[36]==0;
}
static inline int cl_record_valid(const struct cl_record *r) {
    return cl_valid(r->phase) && r->generation && cl_nonzero(r->transaction,16) &&
           cl_nonzero(r->plan,32) && cl_boot_id_valid(r->boot);
}
static inline void cl_put64(unsigned char *p,uint64_t value) {
    for (unsigned i=0;i<8;i++) {p[i]=(unsigned char)value;value>>=8;}
}
static inline uint64_t cl_get64(const unsigned char *p) {
    uint64_t value=0;
    for (unsigned i=0;i<8;i++) value |= (uint64_t)p[i]<<(8*i);
    return value;
}
static inline int cl_encode(const struct cl_record *r,unsigned char data[CL_PAYLOAD_SIZE]) {
    if (!cl_record_valid(r)) return 0;
    memset(data,0,CL_PAYLOAD_SIZE);memcpy(data,CL_JOURNAL_MAGIC,sizeof(CL_JOURNAL_MAGIC)-1);
    data[16]=(unsigned char)r->phase;cl_put64(data+24,r->generation);
    memcpy(data+32,r->transaction,16);memcpy(data+48,r->boot,36);memcpy(data+84,r->plan,32);
    return 1;
}
static inline int cl_decode(const unsigned char data[CL_PAYLOAD_SIZE],struct cl_record *r) {
    unsigned char canonical[CL_PAYLOAD_SIZE];
    memset(r,0,sizeof(*r));r->phase=data[16];r->generation=cl_get64(data+24);
    memcpy(r->transaction,data+32,16);memcpy(r->boot,data+48,36);memcpy(r->plan,data+84,32);
    return cl_encode(r,canonical) && !memcmp(data,canonical,CL_PAYLOAD_SIZE);
}
static inline int cl_private(int fd,int directory,off_t size) {
    struct stat st;
    return fd>=0 && !fstat(fd,&st) && st.st_uid==0 && !(st.st_mode&077) &&
           (directory ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_size==size);
}
static inline int cl_io(int fd,unsigned char *data,size_t size,int writing) {
    size_t done=0;
    while (done<size) {
        ssize_t n=writing ? write(fd,data+done,size-done) : read(fd,data+done,size-done);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) return 0;
        done+=(size_t)n;
    }
    return 1;
}
static inline void cl_store_close(struct cl_store *s) {
    if(s->lock>=0) close(s->lock);
    if(s->directory>=0) close(s->directory);
    s->directory=s->lock=-1;
}
static inline int cl_store_open(struct cl_store *s,const char *path) {
    s->directory=s->lock=-1;
    s->directory=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if (!cl_private(s->directory,1,0)) goto bad;
    s->lock=openat(s->directory,"lifecycle.lock",O_RDWR|O_CREAT|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK,0600);
    if (!cl_private(s->lock,0,0) || flock(s->lock,LOCK_EX|LOCK_NB)) goto bad;
    return 1;
bad:
    cl_store_close(s);return 0;
}
static inline int cl_store_read(struct cl_store *s,struct cl_record *r) {
    unsigned char data[CL_RECORD_SIZE];char expected[65];sha256_state hash;
    int fd=openat(s->directory,"lifecycle",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if (fd<0) return 0;
    int okay=cl_private(fd,0,CL_RECORD_SIZE) && cl_io(fd,data,sizeof(data),0);
    close(fd);if (!okay) return 0;
    sha256_init(&hash);sha256_update(&hash,data,CL_PAYLOAD_SIZE);sha256_hex(&hash,expected);
    return !memcmp(data+CL_PAYLOAD_SIZE,expected,64) && cl_decode(data,r);
}
/* Caller holds lifecycle.lock. Compare-and-swap prevents a stale worker from
 * overwriting another operation. Previous transaction/plan cannot be replaced. */
static inline int cl_store_commit(struct cl_store *s,const struct cl_record *next,uint64_t previous) {
    struct cl_record old;struct stat st;unsigned char data[CL_PAYLOAD_SIZE];char digest[65];
    if (s->directory<0 || s->lock<0 || previous==UINT64_MAX || next->generation!=previous+1 || !cl_encode(next,data)) return 0;
    if (previous) {
        if (!cl_store_read(s,&old) || old.generation!=previous ||
            memcmp(old.transaction,next->transaction,16) || memcmp(old.plan,next->plan,32)) return 0;
    } else if (!fstatat(s->directory,"lifecycle",&st,AT_SYMLINK_NOFOLLOW) || errno!=ENOENT) return 0;
    /* A process exit may leave an uncommitted temporary. A fresh random name
     * allows recovery without deleting or trusting that previous file. */
    uint64_t nonce=0;int random=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
    if(random<0)return 0;
    int entropy=cl_io(random,(unsigned char*)&nonce,sizeof(nonce),0);close(random);
    if(!entropy || !nonce)return 0;
    char temporary[96];int n=snprintf(temporary,sizeof(temporary),".lifecycle-%ld-%016llx.next",(long)getpid(),(unsigned long long)nonce);
    if(n<1 || (size_t)n>=sizeof(temporary))return 0;
    int fd=openat(s->directory,temporary,O_RDWR|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if (fd<0) return 0;
    int okay=cl_io(fd,data,sizeof(data),1) && lseek(fd,0,SEEK_SET)==0 && sha256_fd(fd,digest) &&
             cl_io(fd,(unsigned char*)digest,64,1) && !fsync(fd);
    if(close(fd)) okay=0;
    if(okay && renameat(s->directory,temporary,s->directory,"lifecycle")) okay=0;
    if(okay && fsync(s->directory)) okay=0;
    /* An uncertain commit must stop the executor; never infer success. */
    return okay;
}
/* Boot evidence is derived from persisted UUIDs, not supplied by the browser.
 * Every transition records the current UUID; a verification transition must
 * therefore follow the relevant stage/ACK in a different kernel boot. */
static inline int cl_advance(struct cl_store *s,enum cl_event event,
                             const struct cl_facts *observed,const char *current_boot) {
    struct cl_record old,next;struct cl_facts f;unsigned phase;
    if (!observed || !cl_boot_id_valid(current_boot) || !cl_store_read(s,&old) || old.generation==UINT64_MAX)
        return 0;
    f=*observed;f.new_boot=strcmp(old.boot,current_boot)!=0;
    if (!cl_transition(old.phase,event,&f,&phase)) return 0;
    next=old;next.phase=phase;next.generation++;memcpy(next.boot,current_boot,37);
    return cl_store_commit(s,&next,old.generation);
}
#endif
