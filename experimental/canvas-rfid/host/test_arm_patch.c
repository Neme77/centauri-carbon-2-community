#define _GNU_SOURCE
#include "arm_patch.h"
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#ifdef NDEBUG
#error Assertions must remain enabled
#endif
typedef int (*function)(int);
static function original;
static int replacement(int value) {return original(value) + 100;}
int main(void) {
    /* Exact displaced ARM prologues followed only by synthetic arithmetic. */
    const uint32_t bodies[][4] = {
        {0xe92d48f0,0xe28db014,0xe2800007,0xe8bd88f0},
        {0xe92d4810,0xe28db008,0xe2800007,0xe8bd8810}
    };
    long page = sysconf(_SC_PAGESIZE); assert(page >= 4096);
    for (unsigned i = 0; i < 2; i++) {
        unsigned char *code = mmap(NULL, (size_t)page, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        unsigned char *trampoline = mmap(NULL, (size_t)page, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
        assert(code != MAP_FAILED && trampoline != MAP_FAILED);
        memcpy(code, bodies[i], sizeof(bodies[i]));
        memcpy(trampoline, code, 8); rf_jump(trampoline + 8, (uintptr_t)(code + 8));
        assert(!mprotect(code, (size_t)page, PROT_READ|PROT_EXEC));
        assert(!mprotect(trampoline, (size_t)page, PROT_READ|PROT_EXEC));
        function entry; void *pointer = code; memcpy(&entry, &pointer, sizeof(entry));
        pointer = trampoline; memcpy(&original, &pointer, sizeof(original));
        assert(entry(35) == 42 && original(35) == 42);
        function hook = replacement; uintptr_t target; memcpy(&target, &hook, sizeof(target));
        assert(!mprotect(code, (size_t)page, PROT_READ|PROT_WRITE|PROT_EXEC));
        rf_jump(code, target);
        assert(!mprotect(code, (size_t)page, PROT_READ|PROT_EXEC));
        for (int value = -100; value < 100; value++) assert(entry(value) == value + 107);
        assert(!munmap(code, (size_t)page) && !munmap(trampoline, (size_t)page));
    }
    puts("PASS: actual ARM jump encoding, both displaced prologues, trampoline stack/return ABI on synthetic code only.");
}
