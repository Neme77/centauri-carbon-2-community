/* Canvas activation ordering. No I/O, process control or firmware transport here.
 * The coordinator must durably commit an intent BEFORE executing its side effect.
 * An ACK proves protocol completion, never physical flash readback. */
#ifndef CC2_CANVAS_LIFECYCLE_POLICY_H
#define CC2_CANVAS_LIFECYCLE_POLICY_H
#include <stdint.h>

enum cl_phase {
    CL_STOCK, CL_HOST_STAGED, CL_HOST_READY, CL_FILTER_INTENT,
    CL_FILTER_ACKED, CL_ACTIVE, CL_STOCK_INTENT, CL_STOCK_ACKED,
    CL_STOCK_VERIFIED, CL_HOST_REMOVE_INTENT, CL_DISABLED, CL_PHASE_COUNT
};
enum cl_event {
    CL_STAGE_HOST, CL_VERIFY_HOST_BOOT, CL_BEGIN_FILTER, CL_ACK_FILTER,
    CL_VERIFY_FILTER_BOOT, CL_BEGIN_STOCK, CL_ACK_STOCK,
    CL_VERIFY_STOCK_BOOT, CL_REMOVE_HOST, CL_VERIFY_HOST_REMOVED
};
enum cl_boot { CL_BOOT_BLOCK, CL_BOOT_STOCK, CL_BOOT_GUARDED, CL_BOOT_RECOVER_STOCK };
struct cl_facts {
    unsigned profile, backup, barrier, idle, cold, guard, new_boot;
    unsigned canvas, complete_ack, host_staged, host_removed;
};
static inline int cl_valid(unsigned phase) { return phase < CL_PHASE_COUNT; }
static inline int cl_idle(const struct cl_facts *f) { return f->idle && f->cold; }
/* Missing storage, an unqualified plan and malformed records are not STOCK. */
static inline enum cl_boot cl_boot_action(unsigned phase, int journal_valid,
                                          int profile, int backup, int guard_available) {
    if (!journal_valid || !cl_valid(phase) || !profile) return CL_BOOT_BLOCK;
    switch (phase) {
    case CL_FILTER_INTENT: case CL_STOCK_INTENT:
        return backup ? CL_BOOT_RECOVER_STOCK : CL_BOOT_BLOCK;
    case CL_HOST_STAGED: case CL_HOST_READY:
    case CL_FILTER_ACKED: case CL_ACTIVE: case CL_STOCK_ACKED:
        return guard_available ? CL_BOOT_GUARDED : CL_BOOT_BLOCK;
    default: return CL_BOOT_STOCK;
    }
}
/* Only a coordinated app update preserving the exact qualified native profile
 * may run while active. A base firmware update/removal requires verified stock. */
static inline int cl_update_allowed(unsigned phase, int coordinated_app,
                                    int exact_native_preserved) {
    if (!cl_valid(phase)) return 0;
    if (phase == CL_STOCK || phase == CL_DISABLED) return 1;
    return phase == CL_ACTIVE && coordinated_app && exact_native_preserved;
}
static inline int cl_transition(unsigned phase, enum cl_event event,
                                 const struct cl_facts *f, unsigned *next) {
    if (!cl_valid(phase) || !f || !next || !f->profile || !f->backup || !f->barrier)
        return 0;
    unsigned result = CL_PHASE_COUNT;
    switch (event) {
    case CL_STAGE_HOST:
        if ((phase == CL_STOCK || phase == CL_DISABLED) && cl_idle(f) && f->host_staged)
            result = CL_HOST_STAGED;
        break;
    case CL_VERIFY_HOST_BOOT:
        if (phase == CL_HOST_STAGED && f->new_boot && f->guard && f->canvas && cl_idle(f))
            result = CL_HOST_READY;
        break;
    case CL_BEGIN_FILTER:
        if (phase == CL_HOST_READY && f->guard && cl_idle(f)) result = CL_FILTER_INTENT;
        break;
    case CL_ACK_FILTER:
        if (phase == CL_FILTER_INTENT && f->complete_ack) result = CL_FILTER_ACKED;
        break;
    case CL_VERIFY_FILTER_BOOT:
        if (phase == CL_FILTER_ACKED && f->new_boot && f->guard && f->canvas && cl_idle(f))
            result = CL_ACTIVE;
        break;
    case CL_BEGIN_STOCK:
        if ((phase == CL_ACTIVE || phase == CL_FILTER_ACKED) && f->guard && cl_idle(f))
            result = CL_STOCK_INTENT;
        break;
    case CL_ACK_STOCK:
        /* Recovery may restore stock after either transfer intent. */
        if ((phase == CL_FILTER_INTENT || phase == CL_STOCK_INTENT) && f->complete_ack)
            result = CL_STOCK_ACKED;
        break;
    case CL_VERIFY_STOCK_BOOT:
        if (phase == CL_STOCK_ACKED && f->new_boot && f->guard && f->canvas && cl_idle(f))
            result = CL_STOCK_VERIFIED;
        break;
    case CL_REMOVE_HOST:
        if (phase == CL_STOCK_VERIFIED && cl_idle(f)) result = CL_HOST_REMOVE_INTENT;
        break;
    case CL_VERIFY_HOST_REMOVED:
        if (phase == CL_HOST_REMOVE_INTENT && f->new_boot && f->host_removed && f->canvas && cl_idle(f))
            result = CL_DISABLED;
        break;
    }
    if (result == CL_PHASE_COUNT) return 0;
    *next = result;
    return 1;
}
#endif
