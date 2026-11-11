#ifndef CC2_RFID_ARM_PATCH_H
#define CC2_RFID_ARM_PATCH_H
#include <stdint.h>
#include <string.h>
static void rf_jump(void *where, uintptr_t target) {
    uint32_t words[2] = {0xe51ff004, (uint32_t)target};
    memcpy(where, words, sizeof(words));
    __builtin___clear_cache((char *)where, (char *)where + 8);
}
#endif
