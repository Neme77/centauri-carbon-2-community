#ifndef CC2_RFID_GUARD_H
#define CC2_RFID_GUARD_H
#include <stddef.h>
#include <stdint.h>

#define RF_FRAME_SIZE 191u
#define RF_STOCK_FRAME_SIZE 195u
#define RF_TAG_SIZE 180u
#define RF_MAX_SCAN_SECONDS 120u

enum rf_kind { RF_ABSENT, RF_ELEGOO, RF_FOREIGN, RF_MALFORMED };
struct rf_result {
    uint64_t token;
    uint64_t captured_at;
    unsigned char uid[8];
    unsigned char tag[RF_TAG_SIZE];
    enum rf_kind kind;
    int available;
};
struct rf_session {
    uint64_t token;
    uint64_t deadline;
    int armed;
    struct rf_result result;
};
/* All session access must be serialized by the eventual native integration.
 * No function performs I/O, heap allocation, motion or heater operations. */
int rf_arm(struct rf_session *, uint64_t token, uint64_t now, unsigned seconds);
void rf_cancel(struct rf_session *);
enum rf_kind rf_classify(const unsigned char *, size_t);
int rf_take(struct rf_session *, uint64_t token, uint64_t now, struct rf_result *);
typedef void (*rf_native_fn)(void *, const void *);
struct rf_vector_view {const unsigned char *begin, *end, *capacity;};
struct rf_route {
    unsigned char absent[RF_STOCK_FRAME_SIZE];
    struct rf_vector_view vector;
    const void *forward;
    enum rf_kind kind;
};
/* Prepare while holding the integration lock; forward only after releasing it.
 * A native C++ exception must not leave an integration mutex held. */
int rf_prepare(struct rf_session *, const void *native_vector, uint64_t now, struct rf_route *);
/* Exact borrowed libstdc++ std::vector<uint8_t> ABI: three native pointers.
 * The original vector is never mutated. Foreign frames get a stack-backed
 * const view with the tag-present bit cleared, preserving native ACK handling.
 * This API is a standalone prototype, not an installed hook. */
void rf_dispatch(struct rf_session *, void *owner, const void *native_vector,
                 uint64_t now, rf_native_fn);
#endif
