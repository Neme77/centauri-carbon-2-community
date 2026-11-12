#include "rfid_guard.h"
#include <string.h>
#include <stdint.h>


void rf_cancel(struct rf_session *session) {
    if (session) memset(session, 0, sizeof(*session));
}
int rf_arm(struct rf_session *session, uint64_t token, uint64_t now, unsigned seconds) {
    if (!session) return 0;
    rf_cancel(session);
    if (!token || !seconds || seconds > RF_MAX_SCAN_SECONDS || now > UINT64_MAX - seconds) return 0;
    session->token = token;
    session->deadline = now + seconds;
    session->armed = 1;
    return 1;
}
enum rf_kind rf_classify(const unsigned char *frame, size_t size) {
    static const unsigned char magic[4] = {0x36, 0xee, 0xee, 0xee};
    if (!frame || (size != RF_FRAME_SIZE && size != RF_STOCK_FRAME_SIZE) || frame[1] != 0x40) return RF_MALFORMED;
    if (frame[2] == 0) return RF_ABSENT;
    if (frame[2] != 1) return RF_MALFORMED;
    unsigned nonzero = 0;
    for (unsigned i = 3; i < 11; i++) nonzero |= frame[i];
    if (!nonzero) return RF_MALFORMED;
    return memcmp(frame + 75, magic, 4) ? RF_FOREIGN : RF_ELEGOO;
}
int rf_take(struct rf_session *session, uint64_t token, uint64_t now, struct rf_result *out) {
    if (!session || !out) return 0;
    if (now >= session->deadline) {rf_cancel(session); return 0;}
    if (!token || token != session->token || !session->result.available) return 0;
    *out = session->result;
    rf_cancel(session);
    return 1;
}
int rf_prepare(struct rf_session *session, const void *native_vector, uint64_t now, struct rf_route *route) {
    if (!native_vector || !route) return 0;
    struct rf_vector_view vector;
    memcpy(&vector, native_vector, sizeof(vector));
    uintptr_t first = (uintptr_t)vector.begin, last = (uintptr_t)vector.end;
    uintptr_t capacity = (uintptr_t)vector.capacity;
    size_t size = 0;
    int valid_view = last >= first && capacity >= last && (!last || first) && last - first <= 65536u;
    if (valid_view) size = (size_t)(last - first);
    enum rf_kind kind = valid_view ? rf_classify(vector.begin, size) : RF_MALFORMED;
    route->kind = kind;
    if (session && session->token && now >= session->deadline) rf_cancel(session);
    if (session && session->armed && !session->result.available && (kind == RF_ELEGOO || kind == RF_FOREIGN)) {
        session->result.token = session->token;
        session->result.captured_at = now;
        session->result.kind = kind;
        memcpy(session->result.uid, vector.begin + 3, 8);
        memcpy(session->result.tag, vector.begin + 11, RF_TAG_SIZE);
        session->result.available = 1;
        session->armed = 0; /* one result per explicit scan */
    }
    if (kind == RF_ELEGOO) {
        route->forward = native_vector;
        return 1;
    }
    memset(route->absent, 0, sizeof(route->absent));
    route->absent[1] = 0x40;
    size_t forward_size = RF_FRAME_SIZE;
    if (valid_view && (size == RF_FRAME_SIZE || size == RF_STOCK_FRAME_SIZE)) {
        memcpy(route->absent, vector.begin, size);
        route->absent[2] = 0;
        forward_size = size;
    }
    route->vector.begin = route->absent;
    route->vector.end = route->absent + forward_size;
    route->vector.capacity = route->vector.end;
    route->forward = &route->vector;
    return 1;
}
void rf_dispatch(struct rf_session *session, void *owner, const void *native_vector,
                 uint64_t now, rf_native_fn original) {
    struct rf_route route;
    if (original && rf_prepare(session, native_vector, now, &route)) original(owner, route.forward);
}
