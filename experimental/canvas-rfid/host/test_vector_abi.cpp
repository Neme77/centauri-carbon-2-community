#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#ifdef NDEBUG
#error Test assertions must remain enabled
#endif
extern "C" {
#include "rfid_guard.h"
}
static bool present, throw_native;
static unsigned calls;
static const void *source;
static void native(void *owner, const void *opaque) {
    assert(owner == &calls);
    const auto &v = *static_cast<const std::vector<std::uint8_t> *>(opaque);
    assert(v.size() == RF_FRAME_SIZE);
    assert(v.data() != nullptr);
    assert(v[2] == present);
    if (present) assert(opaque == source);
    ++calls;
    if (throw_native) throw std::runtime_error("native exception fixture");
}
int main() {
    static_assert(sizeof(std::vector<std::uint8_t>) == sizeof(void *) * 3, "unexpected vector ABI");
    std::vector<std::uint8_t> v(RF_FRAME_SIZE, 0);
    v[1] = 0x40; v[2] = 1; v[3] = 17;
    source = &v;
    rf_session session{};
    assert(rf_arm(&session, 1, 100, 30));
    rf_dispatch(&session, &calls, &v, 101, native);
    assert(calls == 1 && v[2] == 1 && session.result.available);
    std::memcpy(v.data() + 75, "\x36\xee\xee\xee", 4);
    present = true;
    rf_dispatch(&session, &calls, &v, 102, native);
    throw_native = true;
    bool caught = false;
    try {rf_dispatch(&session, &calls, &v, 103, native);}
    catch (const std::runtime_error &) {caught = true;}
    assert(caught);
    throw_native = false;
    v[75] = 0; present = false;
    rf_dispatch(&session, &calls, &v, 104, native);
    assert(calls == 4 && v[2] == 1);
    std::puts("PASS: real C++ const-vector callbacks, source identity, sanitized borrowed view and native exception unwinding.");
}
