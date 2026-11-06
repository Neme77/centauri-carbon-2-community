#ifndef CC2_RFID_WIRE_H
#define CC2_RFID_WIRE_H
#define _GNU_SOURCE
#include "rfid_guard.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "stat_compat.h"
#ifndef RF_CHANNEL
#define RF_CHANNEL "/tmp/cc2-rfid-native-r2"
#endif
#ifndef RF_COMPONENT
#define RF_COMPONENT "/opt/usr/cc2-rfid-native-r2"
#endif
struct rf_wire {char magic[8]; uint64_t nonce, token, deadline; uint32_t pid, phase;};
struct rf_wire_result {struct rf_wire header; unsigned char uid[8], tag[RF_TAG_SIZE]; uint32_t kind;};
_Static_assert(sizeof(struct rf_wire) == 40, "wire header ABI");
_Static_assert(sizeof(struct rf_wire_result) == 232, "wire result ABI");
static uint64_t rf_now(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) || ts.tv_sec < 0) return 0;
    return (uint64_t)ts.tv_sec;
}
static int rf_directory(void) {
    struct stat st;
    if (mkdir(RF_CHANNEL, 0700) && errno != EEXIST) return -1;
    int fd = open(RF_CHANNEL, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return -1;
    if (rf_fstat(fd, &st) || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() || (st.st_mode & 077)) {
        close(fd); return -1;
    }
    return fd;
}
/* Only fixed caller-owned names are passed by this component. */
static int rf_read(int dir, const char *name, void *data, size_t size) {
    struct stat st;
    int fd = openat(dir, name, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return 0;
    int valid = !rf_fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == geteuid() &&
                st.st_nlink == 1 && !(st.st_mode & 077) && st.st_size >= 0 && (uint64_t)st.st_size == size;
    size_t used = 0;
    while (valid && used < size) {
        ssize_t got = read(fd, (char *)data + used, size - used);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) {valid = 0; break;}
        used += (size_t)got;
    }
    close(fd); return valid && used == size;
}
static int rf_write(int dir, const char *name, const void *data, size_t size) {
    char temporary[64];
    int n = snprintf(temporary, sizeof(temporary), ".%s-%ld", name, (long)getpid());
    if (n < 1 || (size_t)n >= sizeof(temporary)) return 0;
    int fd = openat(dir, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) return 0;
    size_t used = 0;
    int okay = 1;
    while (used < size) {
        ssize_t count = write(fd, (const char *)data + used, size - used);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {okay = 0; break;}
        used += (size_t)count;
    }
    if (close(fd)) okay = 0;
    struct stat st;
    if (rf_fstatat(dir, name, &st, AT_SYMLINK_NOFOLLOW) == 0 &&
        (!S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_nlink != 1 || (st.st_mode & 077))) okay = 0;
    if (okay && renameat(dir, temporary, dir, name)) okay = 0;
    if (!okay) unlinkat(dir, temporary, 0);
    return okay;
}
#endif
