#include "rfid_guard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#ifdef NDEBUG
#error Test assertions must remain enabled
#endif
struct view {const unsigned char *begin, *end, *capacity;};
static unsigned calls;
static int expected_present;
static void *expected_owner;
static void native(void *owner, const void *opaque) {
    struct view v; memcpy(&v, opaque, sizeof(v));
    assert(owner == expected_owner);
    assert(v.end - v.begin == RF_FRAME_SIZE);
    assert(v.begin[2] == expected_present);
    calls++;
}
int main(void) {
    unsigned char frame[RF_FRAME_SIZE] = {0, 0x40, 1};
    frame[3] = 17;
    struct view v = {frame, frame + sizeof(frame), frame + sizeof(frame)};
    struct rf_session session = {0};
    struct rf_result result;
    expected_owner = &session;
    assert(rf_classify(frame, sizeof(frame)) == RF_FOREIGN);
    for (size_t n = 0; n < sizeof(frame); n++) assert(rf_classify(frame, n) == RF_MALFORMED);
    assert(rf_classify(frame, sizeof(frame) + 1) == RF_MALFORMED);
    assert(!rf_arm(&session, 0, 100, 30));
    assert(!rf_arm(&session, 1, 100, 0));
    assert(!rf_arm(&session, 1, 100, 121));
    assert(!rf_arm(&session, 1, UINT64_MAX, 1));
    assert(rf_arm(&session, 41, 100, 30));
    rf_dispatch(&session, expected_owner, &v, 101, native);
    assert(calls == 1 && session.result.available);
    assert(frame[2] == 1);
    assert(!rf_take(&session, 42, 102, &result));
    assert(rf_take(&session, 41, 102, &result));
    assert(result.kind == RF_FOREIGN && result.token == 41);
    assert(!memcmp(result.tag, frame + 11, RF_TAG_SIZE));
    assert(!rf_take(&session, 41, 102, &result));
    assert(rf_arm(&session, 42, 100, 5));
    rf_dispatch(&session, expected_owner, &v, 105, native);
    assert(!session.result.available && !session.token);
    assert(rf_arm(&session, 43, 100, 5));
    rf_dispatch(&session, expected_owner, &v, 101, native);
    frame[11] ^= 1;
    rf_dispatch(&session, expected_owner, &v, 102, native);
    assert(session.result.tag[0] != frame[11]); /* no overwrite by a later scan */
    assert(!rf_take(&session, 43, 105, &result));
    assert(rf_arm(&session, 44, 100, 5));
    rf_cancel(&session);
    rf_dispatch(&session, expected_owner, &v, 101, native);
    assert(!session.result.available);
    memcpy(frame + 75, "\x36\xee\xee\xee", 4);
    assert(rf_classify(frame, sizeof(frame)) == RF_ELEGOO);
    expected_present = 1;
    rf_dispatch(&session, expected_owner, &v, 102, native);
    expected_present = 0;
    v.end = frame + 12;
    rf_dispatch(&session, expected_owner, &v, 102, native);
    v.end = frame; v.begin = NULL; v.capacity = frame;
    rf_dispatch(&session, expected_owner, &v, 102, native);
    puts("PASS: bounded classification, untouched native vectors, one-shot scan token, expiry, cancellation and no stale capture.");
}
