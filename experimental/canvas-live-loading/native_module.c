#define _GNU_SOURCE
#include <stdint.h>

/* Layout of Canvas and CanvasProtocol::get_canvas_status() in the pinned
 * executable. The controller thread has just observed a new inlet edge and
 * marked the tray present when it reaches the printing branch. */
enum {
    PROTOCOL_STATUS = 0xe8,
    MOTOR_SPEED = 0x16c, MOTOR_ACCEL = 0x170, SHORT_PREFEED = 0x178,
    BREAK_LIMIT = 0x264, FAILED_READS = 0x2d8, PRINT_FEED_READY = 0x3bb, BREAK_COUNT = 0x3d0,
    TRAY_STATE = 0xd0, INLET_PRESENT = 0x5c, CONNECTED = 0x6c, PREFEED_DEADLINE = 0x620,
    ACTIVE_TRAY = 0x610, HEAD_PRESENT = 0x61a,
};

typedef struct {
    int16_t distance, speed, acceleration;
} feeder_motor;

enum { PREFEED_SKIP, PREFEED_RUNOUT, PREFEED_GRIP };

static int32_t native_int(const unsigned char *object, unsigned offset) {
    return *(const volatile int32_t *)(object + offset);
}

static unsigned char native_flag(const unsigned char *object, unsigned offset) {
    return *(const volatile unsigned char *)(object + offset);
}

/* Called only from auto_prefeed_filament's existing printing branch. Keep the
 * stock runout decision and its own deadline check. A normal insertion sends
 * a distance-limited command to the inlet motor and needs no later Stop that
 * could interrupt a tool change. */
__attribute__((visibility("hidden")))
int canvas_live_loading_plan(const unsigned char *canvas, int slot, feeder_motor *motor) {
    int32_t breaks = native_int(canvas, BREAK_COUNT);
    if (breaks > native_int(canvas, BREAK_LIMIT)) return PREFEED_RUNOUT;
    if (breaks != 0 || (unsigned)slot >= 4 || native_int(canvas, FAILED_READS) != 0 ||
        native_flag(canvas, PRINT_FEED_READY) != 1) return PREFEED_SKIP;

    const unsigned char *protocol = *(const unsigned char *const *)canvas;
    if (!protocol) return PREFEED_SKIP;
    const unsigned char *status = protocol + PROTOCOL_STATUS;
    int32_t active = native_int(status, ACTIVE_TRAY);
    if ((unsigned)active >= 4 || active == slot || native_flag(status, CONNECTED) != 1 ||
        native_flag(status, HEAD_PRESENT) != 1 || native_flag(status, INLET_PRESENT + active) != 1 ||
        native_int(status, TRAY_STATE + 4 * active) != 2 ||
        native_flag(status, INLET_PRESENT + slot) != 1 ||
        native_int(status, TRAY_STATE + 4 * slot) != 1 ||
        *(const volatile double *)(status + PREFEED_DEADLINE + 8 * slot) != 1.0e16)
        return PREFEED_SKIP;

    int32_t distance = native_int(canvas, SHORT_PREFEED);
    if (distance < 1 || distance > 5) return PREFEED_SKIP;
    *motor = (feeder_motor){(int16_t)distance, (int16_t)native_int(canvas, MOTOR_SPEED),
                           (int16_t)native_int(canvas, MOTOR_ACCEL)};
    return PREFEED_GRIP;
}

#ifndef CC2_CANVAS_LIVE_LOADING_TEST
#include "../../cc2-control/src/sha256.h"
#include <sys/mman.h>

#define NATIVE_PATH "/opt/usr/cc2-reactor-runtime-v2/elegoo_printer"
#define NATIVE_SHA256 "19712b5943b10bb8cb6aab9a4ed498ee962b28538b23c91b7ed003eb9e590745"
#define PREFEED_BRANCH 0x005e023c

_Static_assert(sizeof(void *) == 4 && sizeof(feeder_motor) == 6, "Qualified ARM32 feeder ABI");
__attribute__((visibility("hidden"))) void canvas_live_loading_branch(void);

__attribute__((constructor)) static void canvas_live_loading_activate(void) {
    char path[sizeof(NATIVE_PATH)], digest[65];
    if (geteuid() != 0 ||
        readlink("/proc/self/exe", path, sizeof(path)) != (ssize_t)sizeof(path) - 1 ||
        memcmp(path, NATIVE_PATH, sizeof(path) - 1)) return;

    int fd = open("/proc/self/exe", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    int match = sha256_fd(fd, digest);
    close(fd);
    if (!match || strcmp(digest, NATIVE_SHA256)) return;

    const uint32_t stock[] = {0xe51b3058, 0xe59323d0, 0xe51b3058, 0xe5933264, 0xe1520003, 0xda000011};
    void *site = (void *)PREFEED_BRANCH;
    if (memcmp(site, stock, sizeof(stock))) return;
    long page_size = sysconf(_SC_PAGESIZE);
    if (page_size < 4096 || (page_size & (page_size - 1))) return;
    void *page = (void *)(PREFEED_BRANCH & ~((uintptr_t)page_size - 1));
    if (mprotect(page, (size_t)page_size, PROT_READ | PROT_WRITE | PROT_EXEC)) return;
    uint32_t jump[] = {0xe51ff004, 0};
    void (*branch)(void) = canvas_live_loading_branch;
    memcpy(&jump[1], &branch, sizeof(branch));
    memcpy(site, jump, sizeof(jump));
    __builtin___clear_cache(site, (char *)site + sizeof(jump));
    if (mprotect(page, (size_t)page_size, PROT_READ | PROT_EXEC)) _exit(125);
}
#endif
