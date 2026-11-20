/* cc2_crc32() (src/crc32.h) must be the standard CRC-32 and give the same value as bk_crc() of the backup files
 * (src/backup_format.h) for every input: empty, lengths that are not a multiple of 4, unaligned starts, large buffers,
 * and a value continued over pieces. All 1024 table entries are recomputed from the definition here. */
#ifndef CC2_COMMUNITY_FIRMWARE_VERSION
#define CC2_COMMUNITY_FIRMWARE_VERSION "4.4"
#endif
#define main cc2_application_main
#include "../src/main.c"
#undef main
#include "../src/crc32.h"
#include <assert.h>

/* An independent bitwise reference: reflected polynomial 0xEDB88320, start and end value 0xFFFFFFFF. */
static uint32_t reference(const unsigned char *p, size_t n) {
    uint32_t crc = 0xffffffffU;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int b = 0; b < 8; b++) crc = (crc & 1U) ? (crc >> 1) ^ 0xedb88320U : crc >> 1;
    }
    return crc ^ 0xffffffffU;
}

static void fill(unsigned char *p, size_t n, uint32_t seed) {
    for (size_t i = 0; i < n; i++) { seed = seed * 1664525U + 1013904223U; p[i] = (unsigned char)(seed >> 24); }
}

int main(void) {
    /* The check value of the standard, and a few other published ones. */
    assert(cc2_crc32("123456789", 9) == 0xcbf43926U && bk_crc("123456789", 9) == 0xcbf43926U);
    assert(cc2_crc32("", 0) == 0 && cc2_crc32(NULL, 0) == 0 && bk_crc("", 0) == 0);
    assert(cc2_crc32("a", 1) == 0xe8b7be43U);
    assert(cc2_crc32("The quick brown fox jumps over the lazy dog", 43) == 0x414fa339U);

    /* Every table entry follows from the definition: table 0 is the bitwise CRC of the byte, table k is table k-1
     * pushed through one more zero byte (the table is data, not generated at run time: this proves it). */
    for (unsigned n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1U) ? (c >> 1) ^ 0xedb88320U : c >> 1;
        assert(c == cc2_crc32_table[0][n]);
        for (int t = 1; t < 4; t++) {
            uint32_t before = cc2_crc32_table[t - 1][n];
            assert(cc2_crc32_table[t][n] == ((before >> 8) ^ cc2_crc32_table[0][before & 0xffU]));
        }
    }

    /* Lengths 0..300 (including every remainder modulo 4 and 8) and four start alignments, against the project's own
     * bk_crc and the reference. */
    enum { BIG = 1 << 20 };
    unsigned char *big = malloc(BIG + 8);
    assert(big);
    fill(big, BIG + 8, 12345);
    for (size_t off = 0; off < 4; off++)
        for (size_t n = 0; n <= 300; n++) {
            uint32_t want = bk_crc((const char *)big + off, n);
            assert(cc2_crc32(big + off, n) == want && reference(big + off, n) == want);
        }
    /* Large buffers: about the size of the layer map of 5000 layers (560 124 bytes) and of the largest one (8192 layers). */
    size_t sizes[] = { 560152, 917504, BIG - 1, BIG };
    for (size_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
        assert(cc2_crc32(big + 3, sizes[i]) == bk_crc((const char *)big + 3, sizes[i]));
    /* Bytes that are all ones and all zeros. */
    memset(big, 0xff, 4096); assert(cc2_crc32(big, 4096) == bk_crc((const char *)big, 4096));
    memset(big, 0, 4096); assert(cc2_crc32(big, 4096) == bk_crc((const char *)big, 4096));

    /* A value continued over two or three pieces equals the value over the whole, at every split. */
    fill(big, 200, 777);
    uint32_t whole = cc2_crc32(big, 200);
    for (size_t cut = 0; cut <= 200; cut++) {
        assert(cc2_crc32_update(cc2_crc32_update(0, big, cut), big + cut, 200 - cut) == whole);
        size_t cut2 = cut + (200 - cut) / 2;
        uint32_t c = cc2_crc32_update(0, big, cut);
        c = cc2_crc32_update(c, big + cut, cut2 - cut);
        assert(cc2_crc32_update(c, big + cut2, 200 - cut2) == whole);
    }

    /* Any single flipped bit changes the value (a CRC-32 guarantees it). */
    fill(big, 64, 99);
    uint32_t before = cc2_crc32(big, 64);
    for (size_t byte = 0; byte < 64; byte++)
        for (int bit = 0; bit < 8; bit++) {
            big[byte] ^= (unsigned char)(1U << bit);
            assert(cc2_crc32(big, 64) != before);
            big[byte] ^= (unsigned char)(1U << bit);
        }
    assert(cc2_crc32(big, 64) == before);
    free(big);
    puts("PASS test_crc32: standard check value, bk_crc equal for lengths 0-300, four alignments, large buffers, pieces, bit flips");
    return 0;
}
