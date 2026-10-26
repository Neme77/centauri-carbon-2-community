#ifndef CC2_RECOVERY_CORE_H
#define CC2_RECOVERY_CORE_H
#include <stdint.h>
#include <stddef.h>
#define CC2_RECOVERY_RECORDS 8
typedef struct { uint64_t device, inode; } cc2_socket_key;
typedef struct { cc2_socket_key key; unsigned epoch; int attempted; } cc2_record;
typedef struct {
    int ready;
    unsigned epoch, next;
    unsigned long sent, duplicate, failed;
    cc2_record records[CC2_RECOVERY_RECORDS];
} cc2_recovery;
/* Caller serializes all operations. No thread creation, polling or credentials. */
void cc2_recovery_presence(cc2_recovery *, int);
int cc2_recovery_claim(cc2_recovery *, cc2_socket_key);
void cc2_recovery_result(cc2_recovery *, int);
int cc2_recovery_is_insertion(const char *, size_t);
extern const char cc2_insertion[];
extern const size_t cc2_insertion_length;
#endif
