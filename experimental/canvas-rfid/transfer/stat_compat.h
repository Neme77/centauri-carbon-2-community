#ifndef CC2_RFID_STAT_COMPAT_H
#define CC2_RFID_STAT_COMPAT_H
#include <stddef.h>
#include <sys/stat.h>
/* glibc 2.33 added direct fstat/fstatat symbols. ARM glibc 2.23 exports
 * the versioned-structure entry points below at GLIBC_2.4 instead.
 * Version 3 is _STAT_VER_LINUX from glibc-2.23's Linux bits/stat.h.
 * Keep the public time32/file-offset32 layout, including overflow refusal;
 * never reinterpret a time64 or stat64 buffer as the legacy structure. */
#if defined(__arm__) && defined(__GLIBC__)
#if defined(__USE_FILE_OFFSET64) || defined(__USE_TIME_BITS64)
#error Qualified legacy stat requires file-offset32/time32
#endif
_Static_assert(sizeof(struct stat) == 88, "qualified ARM stat structure size");
_Static_assert(offsetof(struct stat, st_mode) == 16, "qualified ARM stat mode");
_Static_assert(offsetof(struct stat, st_uid) == 24, "qualified ARM stat owner");
_Static_assert(offsetof(struct stat, st_size) == 44, "qualified ARM stat size");
extern int __fxstat(int, int, struct stat *);
extern int __fxstatat(int, int, const char *, struct stat *, int);
static inline int rf_fstat(int fd, struct stat *buffer) {return __fxstat(3, fd, buffer);}
static inline int rf_fstatat(int fd, const char *name, struct stat *buffer, int flags) {
    return __fxstatat(3, fd, name, buffer, flags);
}
#else
static inline int rf_fstat(int fd, struct stat *buffer) {return fstat(fd, buffer);}
static inline int rf_fstatat(int fd, const char *name, struct stat *buffer, int flags) {
    return fstatat(fd, name, buffer, flags);
}
#endif
#endif
