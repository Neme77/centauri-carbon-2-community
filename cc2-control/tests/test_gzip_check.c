/* Runs cc2_gzip_unpacks_to() (src/gzip_check.h) over the cases that tests/test_gzip_check.py writes with Python's zlib.
 * Each case: copy length, CRC-32 and length of the plain file (each 4 bytes, little-endian), the expected answer
 * (1 byte: 1 = the copy unpacks to exactly that file, by zlib), then the copy. Every answer must match. */
#include <stdio.h>
#include <stdlib.h>

#include "../src/gzip_check.h"

static uint32_t read32(FILE *file, int *ok) {
    unsigned char b[4];
    if (fread(b, 1, 4, file) != 4) {
        *ok = 0;
        return 0;
    }
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    FILE *file = fopen(argv[1], "rb");
    if (!file) return 2;
    unsigned long cases = 0, accepted = 0, failures = 0;
    for (;;) {
        int ok = 1;
        uint32_t length = read32(file, &ok);
        if (!ok) break;
        uint32_t crc = read32(file, &ok), plain_size = read32(file, &ok);
        int expected = fgetc(file);
        if (!ok || expected == EOF) return 2;
        unsigned char *data = malloc(length ? length : 1);
        if (!data || fread(data, 1, length, file) != length) return 2;
        int got = cc2_gzip_unpacks_to(data, length, crc, plain_size);
        if (got != expected) {
            fprintf(stderr, "FAIL: case %lu (%u bytes): got %d, zlib says %d\n", cases, length, got, expected);
            failures++;
        }
        accepted += (unsigned long)got;
        cases++;
        free(data);
    }
    fclose(file);
    printf("%lu cases, %lu accepted, %lu wrong\n", cases, accepted, failures);
    return failures ? 1 : 0;
}
