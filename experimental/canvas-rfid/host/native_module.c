#define _GNU_SOURCE
#include "rfid_wire.h"
#include <pthread.h>
#include <stdlib.h>
#include <sys/mman.h>
#ifndef RF_NATIVE_TEST
#include "sha256.h"
#include "arm_patch.h"
#endif

static pthread_mutex_t rf_lock = PTHREAD_MUTEX_INITIALIZER;
static struct rf_session rf_session;
static rf_native_fn rf_original_parse, rf_original_state;
typedef int (*rf_read_fn)(void *, int);
static rf_read_fn rf_original_read;
typedef void (*rf_reset_fn)(void *);
static rf_reset_fn rf_original_reset;
static int rf_channel = -1, rf_phase, rf_capture_broken;
static uint64_t rf_epoch, rf_last_token, rf_last_poll;
static uint64_t rf_data_sequence;
static uintptr_t rf_data_owner;
static enum rf_kind rf_data_kind;
static unsigned rf_read_active;

static struct rf_wire rf_header(uint64_t token, uint64_t deadline, unsigned phase) {
    struct rf_wire h = {{'C','C','2','R','F','1',0,0},0,0,0,0,0};
    h.pid = (uint32_t)getpid(); h.nonce = rf_epoch;
    h.token = token; h.deadline = deadline; h.phase = phase;
    return h;
}
static void rf_publish(void) {
    struct rf_wire h = rf_header(rf_session.token, rf_session.deadline,
                                 rf_capture_broken ? 0u : (unsigned)rf_phase);
    if (rf_channel < 0 || !rf_write(rf_channel, "ready", &h, sizeof(h))) rf_capture_broken = 1;
}
static void rf_invalidate(void) {
    rf_cancel(&rf_session);
    if (++rf_epoch == 0) rf_capture_broken = 1;
    if (rf_channel >= 0) {
        unlinkat(rf_channel, "request", 0);
        unlinkat(rf_channel, "result", 0);
    }
}
static int rf_string_phase(const void *opaque) {
    const char *data; size_t size;
    if (!opaque) return 0;
    memcpy(&data, opaque, sizeof(data));
    memcpy(&size, (const char *)opaque + sizeof(data), sizeof(size));
    if (!data || size > 16) return 0;
    if ((size == 7 && !memcmp(data, "standby", 7)) ||
        (size == 8 && !memcmp(data, "complete", 8)) ||
        (size == 9 && !memcmp(data, "cancelled", 9))) return 1;
    if ((size == 8 && !memcmp(data, "printing", 8)) ||
        (size == 6 && !memcmp(data, "paused", 6))) return 2;
    return 0;
}
/* The nontrivial native std::string value arrives indirectly. Its ownership
 * remains with the native call chain; never copy or destroy the object. */
void cc2_rfid_state_changed(void *owner, const void *string) {
    int phase = rf_string_phase(string);
    pthread_mutex_lock(&rf_lock);
    rf_phase = 0;
    rf_invalidate();
    uint64_t epoch = rf_epoch;
    rf_publish();
    pthread_mutex_unlock(&rf_lock);
    /* No lock is held while native C++ can call other callbacks or unwind.
     * If it throws, the published capability stays disabled. */
    rf_original_state(owner, string);
    pthread_mutex_lock(&rf_lock);
    if (rf_epoch == epoch) {rf_phase = phase; rf_publish();}
    pthread_mutex_unlock(&rf_lock);
}
/* PrintStats::reset assigns its owned string at +0x6c to standby and emits
 * elegoo:print_stats. At boot that event can precede Canvas's subscription.
 * Observe completion of the existing reset; never invoke an extra reset.
 * A nested/later state callback wins over this outer completion. */
void cc2_rfid_stats_reset(void *owner) {
    pthread_mutex_lock(&rf_lock);
    rf_phase=0;
    rf_invalidate();
    uint64_t epoch=rf_epoch;
    rf_publish();
    pthread_mutex_unlock(&rf_lock);
    rf_original_reset(owner);
    pthread_mutex_lock(&rf_lock);
    if(rf_epoch==epoch) {
        rf_phase=owner?rf_string_phase((const char*)owner+0x6c):0;
        rf_publish();
    }
    pthread_mutex_unlock(&rf_lock);
}
static void rf_request(uint64_t now) {
    if (rf_channel < 0 || rf_capture_broken || rf_phase != 1 || !now || now == rf_last_poll) return;
    rf_last_poll = now;
    struct rf_wire request;
    if (!rf_read(rf_channel, "request", &request, sizeof(request))) return;
    unlinkat(rf_channel, "request", 0);
    const struct rf_wire expected = rf_header(0, 0, 0);
    if (memcmp(request.magic, expected.magic, 8) || request.pid != expected.pid || request.nonce != rf_epoch) return;
    if (request.phase == 2) {
        rf_cancel(&rf_session); unlinkat(rf_channel, "result", 0); rf_publish(); return;
    }
    if (request.phase != 1 || !request.token || request.token == rf_last_token ||
        request.deadline <= now || request.deadline - now > RF_MAX_SCAN_SECONDS) return;
    if (rf_arm(&rf_session, request.token, now, (unsigned)(request.deadline - now))) {
        rf_last_token = request.token;
        unlinkat(rf_channel, "result", 0);
        rf_publish();
    }
}
void cc2_rfid_parse(void *owner, const void *vector) {
    struct rf_route route;
    pthread_mutex_lock(&rf_lock);
    uint64_t now = rf_now();
    rf_request(now);
    struct rf_session *session = !rf_capture_broken && rf_phase == 1 ? &rf_session : NULL;
    uint64_t token = rf_session.token, deadline = rf_session.deadline;
    int prepared = rf_prepare(session, vector, now, &route);
    rf_data_owner = (uintptr_t)owner;
    rf_data_kind = prepared ? route.kind : RF_MALFORMED;
    rf_data_sequence++;
    if (session && rf_session.result.available) {
        struct rf_result result;
        if (rf_take(&rf_session, token, now, &result)) {
            struct rf_wire_result wire;
            memset(&wire, 0, sizeof(wire));
            wire.header = rf_header(token, deadline, (unsigned)rf_phase);
            memcpy(wire.uid, result.uid, sizeof(wire.uid));
            memcpy(wire.tag, result.tag, sizeof(wire.tag));
            wire.kind = (uint32_t)result.kind;
            if (!rf_write(rf_channel, "result", &wire, sizeof(wire))) rf_capture_broken = 1;
            rf_publish();
        }
    }
    pthread_mutex_unlock(&rf_lock);
    if (prepared) rf_original_parse(owner, route.forward);
}
/* Stock's sole caller marks the spool as detected when this returns true.
 * ACK-only success must not expose an older RFIDData object as a new spool. */
int cc2_rfid_read(void *owner, int wait) {
    if (__sync_lock_test_and_set(&rf_read_active, 1)) return 0;
    pthread_mutex_lock(&rf_lock);
    uint64_t before = rf_data_sequence, epoch = rf_epoch;
    pthread_mutex_unlock(&rf_lock);
    int acknowledged = rf_original_read(owner, wait);
    pthread_mutex_lock(&rf_lock);
    int accepted = acknowledged && wait == 1 && rf_epoch == epoch &&
                   rf_data_sequence == before + 1 && rf_data_owner == (uintptr_t)owner &&
                   rf_data_kind == RF_ELEGOO;
    pthread_mutex_unlock(&rf_lock);
    __sync_lock_release(&rf_read_active);
    return accepted;
}

#ifndef RF_NATIVE_TEST
_Static_assert(sizeof(void *) == 4, "qualified ARM32 only");
struct rf_patch {uintptr_t address; unsigned char prefix[8]; uintptr_t replacement;};
static struct rf_patch rf_patches[] = {
    {0x5c7074, {0xf0,0x48,0x2d,0xe9,0x14,0xb0,0x8d,0xe2}, 0},
    {0x5e4df0, {0x10,0x48,0x2d,0xe9,0x08,0xb0,0x8d,0xe2}, 0},
    {0x5c4a44, {0x10,0x48,0x2d,0xe9,0x08,0xb0,0x8d,0xe2}, 0},
    {0x4a4f48, {0x10,0x48,0x2d,0xe9,0x08,0xb0,0x8d,0xe2}, 0}
};
#define RF_PATCH_COUNT (sizeof(rf_patches)/sizeof(rf_patches[0]))
static int rf_enabled(void) {
    int dir = open(RF_COMPONENT, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (dir < 0) return errno == ENOENT ? 0 : -1;
    struct stat st;
    int valid = !rf_fstat(dir, &st) && st.st_uid == 0 && !(st.st_mode & 022);
    int present = !rf_fstatat(dir, "enabled", &st, AT_SYMLINK_NOFOLLOW);
    if (!present && errno == ENOENT && valid) {close(dir); return 0;}
    char marker[19];
    if (valid) valid = rf_read(dir, "enabled", marker, sizeof(marker)) && !memcmp(marker, "CC2-RFID-NATIVE-R2\n", sizeof(marker));
    close(dir); return valid ? 1 : -1;
}
__attribute__((constructor)) static void rf_activate(void) {
    char path[512], digest[65];
    ssize_t length = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (length < 0) return;
    path[length] = 0;
    /* Helpers inherited from a launcher must remain untouched. */
    if (strcmp(path, "/opt/usr/cc2-reactor-runtime-v2/elegoo_printer")) return;
    int enabled = rf_enabled();
    if (!enabled) return;
    if (enabled < 0) _exit(125);
    if (geteuid() != 0) _exit(125);
    int fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    if (fd < 0) _exit(125);
    int valid = sha256_fd(fd, digest); close(fd);
    if (!valid || strcmp(digest, "19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745")) _exit(125);
    for (unsigned i = 0; i < RF_PATCH_COUNT; i++) if (memcmp((const void *)rf_patches[i].address, rf_patches[i].prefix, 8)) _exit(125);
    fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) _exit(125);
    ssize_t got = read(fd, &rf_epoch, sizeof(rf_epoch)); close(fd);
    if (got != (ssize_t)sizeof(rf_epoch) || !rf_epoch) _exit(125);
    rf_channel = rf_directory();
    if (rf_channel < 0) _exit(125);
    unlinkat(rf_channel, "ready", 0); unlinkat(rf_channel, "request", 0); unlinkat(rf_channel, "result", 0);
    long page = sysconf(_SC_PAGESIZE);
    if (page < 4096 || (page & (page - 1))) _exit(125);
    unsigned char *trampolines = mmap(NULL, (size_t)page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (trampolines == MAP_FAILED) _exit(125);
    for (unsigned i = 0; i < RF_PATCH_COUNT; i++) {
        memcpy(trampolines + i * 16, rf_patches[i].prefix, 8);
        rf_jump(trampolines + i * 16 + 8, rf_patches[i].address + 8);
    }
    if (mprotect(trampolines, (size_t)page, PROT_READ | PROT_EXEC)) _exit(125);
    void *entry = trampolines; memcpy(&rf_original_parse, &entry, sizeof(entry));
    entry = trampolines + 16; memcpy(&rf_original_state, &entry, sizeof(entry));
    entry = trampolines + 32; memcpy(&rf_original_read, &entry, sizeof(entry));
    entry = trampolines + 48; memcpy(&rf_original_reset, &entry, sizeof(entry));
    rf_native_fn parse = cc2_rfid_parse, state = cc2_rfid_state_changed;
    memcpy(&rf_patches[0].replacement, &parse, sizeof(parse));
    memcpy(&rf_patches[1].replacement, &state, sizeof(state));
    rf_read_fn read_hook = cc2_rfid_read;
    memcpy(&rf_patches[2].replacement, &read_hook, sizeof(read_hook));
    rf_reset_fn reset_hook=cc2_rfid_stats_reset;
    memcpy(&rf_patches[3].replacement,&reset_hook,sizeof(reset_hook));
    for (unsigned i = 0; i < RF_PATCH_COUNT; i++) {
        uintptr_t base = rf_patches[i].address & ~((uintptr_t)page - 1);
        if (mprotect((void *)base, (size_t)page, PROT_READ | PROT_WRITE | PROT_EXEC)) _exit(125);
    }
    for (unsigned i = 0; i < RF_PATCH_COUNT; i++) rf_jump((void *)rf_patches[i].address, rf_patches[i].replacement);
    for (unsigned i = 0; i < RF_PATCH_COUNT; i++) {
        uintptr_t base = rf_patches[i].address & ~((uintptr_t)page - 1);
        if (mprotect((void *)base, (size_t)page, PROT_READ | PROT_EXEC)) _exit(125);
    }
    rf_publish();
    if (rf_capture_broken) _exit(125);
}
#endif
