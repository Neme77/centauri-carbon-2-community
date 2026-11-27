/* Does a gzip file unpack to exactly the bytes it claims? cc2_gzip_unpacks_to() decodes the whole compressed payload
 * (RFC 1952 member, RFC 1951 deflate stream: stored, fixed and dynamic Huffman blocks) without keeping the output:
 * every byte goes through a 32 KiB window (the farthest a deflate match may reach back) and into a running CRC-32.
 * It returns 1 only when `data` is one gzip member, its deflate stream decodes without an error and ends exactly where
 * the 8-byte trailer starts, and the output has the length `plain_size` and the CRC-32 `crc`, both also named by the
 * trailer. A copy damaged anywhere (header, payload or trailer), cut off, followed by other bytes or of another file
 * gives 0. The output is never longer than `plain_size` (decoding stops there), so the work is bounded by the plain
 * file, and the whole check needs one allocation of about 40 KiB, freed before it returns. It reads nothing but `data`
 * and keeps no state, so any thread may call it.
 *
 * Like the trailer it relies on CRC-32: any damage of up to 32 bits and all but 1 in 2^32 other changes are found;
 * it is no defence against a deliberate change. tests/test_gzip_check.c checks it against Python's zlib. */
#ifndef CC2_GZIP_CHECK_H
#define CC2_GZIP_CHECK_H
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "crc32.h"

#define CC2_GZC_FAST_BITS 9
#define CC2_GZC_WINDOW 32768u

typedef struct {
    uint16_t fast[1 << CC2_GZC_FAST_BITS]; /* (length << 9) | symbol for codes up to 9 bits; 0: decode slowly */
    uint16_t firstcode[16];
    int maxcode[17];
    uint16_t firstsymbol[16];
    uint8_t size[288];
    uint16_t value[288];
} cc2_gzc_huffman;

typedef struct {
    const unsigned char *in;
    size_t in_length, position; /* position may run up to 4 bytes past in_length (read as zeros, then refused) */
    uint32_t bits;
    int count;
    size_t total, limit;
    uint32_t crc;
    unsigned window_at;
    cc2_gzc_huffman length, distance, codes;
    unsigned char window[CC2_GZC_WINDOW];
} cc2_gzc_state;

static unsigned cc2_gzc_reverse(unsigned value, int bits) {
    unsigned result = 0;
    for (int i = 0; i < bits; i++, value >>= 1) result = result << 1 | (value & 1);
    return result;
}

/* Canonical Huffman code from code lengths (RFC 1951 3.2.2). An over-full code is refused; an incomplete one is kept
   (a single distance code is allowed) and its unused codes are refused when they are read. */
static int cc2_gzc_build(cc2_gzc_huffman *z, const uint8_t *lengths, int count) {
    int sizes[17] = {0}, next_code[16], code = 0, k = 0;
    memset(z->fast, 0, sizeof(z->fast));
    for (int i = 0; i < count; i++) sizes[lengths[i]]++;
    sizes[0] = 0;
    for (int i = 1; i < 16; i++) {
        next_code[i] = code;
        z->firstcode[i] = (uint16_t)code;
        z->firstsymbol[i] = (uint16_t)k;
        code += sizes[i];
        if (sizes[i] && code - 1 >= (1 << i)) return 0;
        z->maxcode[i] = code << (16 - i);
        code <<= 1;
        k += sizes[i];
    }
    z->maxcode[16] = 0x10000;
    for (int i = 0; i < count; i++) {
        int s = lengths[i];
        if (!s) continue;
        int c = next_code[s] - z->firstcode[s] + z->firstsymbol[s];
        z->size[c] = (uint8_t)s;
        z->value[c] = (uint16_t)i;
        if (s <= CC2_GZC_FAST_BITS) {
            for (unsigned j = cc2_gzc_reverse((unsigned)next_code[s], s); j < (1u << CC2_GZC_FAST_BITS); j += 1u << s)
                z->fast[j] = (uint16_t)(s << 9 | i);
        }
        next_code[s]++;
    }
    return 1;
}

/* Keeps at least 25 bits ready; bytes past the end of the stream are read as zeros and counted. */
static void cc2_gzc_fill(cc2_gzc_state *s) {
    while (s->count <= 24) {
        uint32_t byte = s->position < s->in_length ? s->in[s->position] : 0;
        s->position++;
        s->bits |= byte << s->count;
        s->count += 8;
    }
}

static uint32_t cc2_gzc_take(cc2_gzc_state *s, int n) {
    if (s->count < n) cc2_gzc_fill(s);
    uint32_t value = s->bits & ((1u << n) - 1);
    s->bits >>= n;
    s->count -= n;
    return value;
}

static int cc2_gzc_decode(cc2_gzc_state *s, const cc2_gzc_huffman *z) {
    if (s->count < 16) cc2_gzc_fill(s);
    if (s->position > s->in_length + 4) return -1;
    unsigned fast = z->fast[s->bits & ((1u << CC2_GZC_FAST_BITS) - 1)];
    if (fast) {
        int length = (int)(fast >> 9);
        s->bits >>= length;
        s->count -= length;
        return (int)(fast & 511);
    }
    int key = (int)cc2_gzc_reverse(s->bits & 0xffff, 16), length;
    for (length = CC2_GZC_FAST_BITS + 1; key >= z->maxcode[length]; length++) {}
    if (length >= 16) return -1;
    int at = (key >> (16 - length)) - z->firstcode[length] + z->firstsymbol[length];
    if (at < 0 || at >= 288 || z->size[at] != length) return -1;
    s->bits >>= length;
    s->count -= length;
    return z->value[at];
}

static void cc2_gzc_put(cc2_gzc_state *s, unsigned char byte) {
    s->window[s->window_at++] = byte;
    if (s->window_at == CC2_GZC_WINDOW) {
        s->crc = cc2_crc32_update(s->crc, s->window, CC2_GZC_WINDOW);
        s->window_at = 0;
    }
}

static int cc2_gzc_codes(cc2_gzc_state *s) {
    static const uint16_t length_base[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                             31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
    static const uint8_t length_extra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                             2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
    static const uint16_t distance_base[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                               193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
    static const uint8_t distance_extra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                               6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
    for (;;) {
        int symbol = cc2_gzc_decode(s, &s->length);
        if (symbol < 0) return 0;
        if (symbol < 256) {
            if (s->total >= s->limit) return 0;
            s->total++;
            cc2_gzc_put(s, (unsigned char)symbol);
            continue;
        }
        if (symbol == 256) return 1;
        symbol -= 257;
        if (symbol >= 29) return 0;
        size_t length = length_base[symbol] + cc2_gzc_take(s, length_extra[symbol]);
        int code = cc2_gzc_decode(s, &s->distance);
        if (code < 0 || code >= 30) return 0;
        size_t distance = distance_base[code] + cc2_gzc_take(s, distance_extra[code]);
        if (distance > s->total || length > s->limit - s->total) return 0;
        s->total += length;
        unsigned from = (s->window_at - (unsigned)distance) & (CC2_GZC_WINDOW - 1);
        while (length--) {
            unsigned char byte = s->window[from];
            from = (from + 1) & (CC2_GZC_WINDOW - 1);
            cc2_gzc_put(s, byte);
        }
    }
}

static int cc2_gzc_stored(cc2_gzc_state *s) {
    cc2_gzc_take(s, s->count & 7);
    uint32_t length = cc2_gzc_take(s, 16), check = cc2_gzc_take(s, 16);
    if ((length ^ 0xffff) != check) return 0;
    size_t buffered = (size_t)s->count / 8; /* whole bytes still in the bit buffer go back to the input */
    if (s->position - buffered > s->in_length) return 0;
    s->position -= buffered;
    s->bits = 0;
    s->count = 0;
    if (length > s->in_length - s->position || length > s->limit - s->total) return 0;
    s->total += length;
    for (uint32_t i = 0; i < length; i++) cc2_gzc_put(s, s->in[s->position++]);
    return 1;
}

static int cc2_gzc_fixed(cc2_gzc_state *s) {
    uint8_t lengths[288 + 30];
    memset(lengths, 8, 144);
    memset(lengths + 144, 9, 112);
    memset(lengths + 256, 7, 24);
    memset(lengths + 280, 8, 8);
    memset(lengths + 288, 5, 30);
    return cc2_gzc_build(&s->length, lengths, 288) && cc2_gzc_build(&s->distance, lengths + 288, 30);
}

static int cc2_gzc_dynamic(cc2_gzc_state *s) {
    static const uint8_t order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    uint8_t code_lengths[19] = {0}, lengths[286 + 30];
    int literals = (int)cc2_gzc_take(s, 5) + 257, distances = (int)cc2_gzc_take(s, 5) + 1;
    int codes = (int)cc2_gzc_take(s, 4) + 4, total = literals + distances, n = 0;
    if (literals > 286 || distances > 30) return 0;
    for (int i = 0; i < codes; i++) code_lengths[order[i]] = (uint8_t)cc2_gzc_take(s, 3);
    if (!cc2_gzc_build(&s->codes, code_lengths, 19)) return 0;
    while (n < total) {
        int symbol = cc2_gzc_decode(s, &s->codes), repeat;
        uint8_t fill = 0;
        if (symbol < 0 || symbol >= 19) return 0;
        if (symbol < 16) {
            lengths[n++] = (uint8_t)symbol;
            continue;
        }
        if (symbol == 16) {
            if (n == 0) return 0;
            fill = lengths[n - 1];
            repeat = 3 + (int)cc2_gzc_take(s, 2);
        } else if (symbol == 17) {
            repeat = 3 + (int)cc2_gzc_take(s, 3);
        } else {
            repeat = 11 + (int)cc2_gzc_take(s, 7);
        }
        if (repeat > total - n) return 0;
        memset(lengths + n, fill, (size_t)repeat);
        n += repeat;
    }
    if (lengths[256] == 0) return 0; /* a block without an end code cannot end */
    return cc2_gzc_build(&s->length, lengths, literals) && cc2_gzc_build(&s->distance, lengths + literals, distances);
}

static int cc2_gzip_unpacks_to(const unsigned char *data, size_t size, uint32_t crc, size_t plain_size) {
    if (size < 18 || data[0] != 0x1f || data[1] != 0x8b || data[2] != 8 || (data[3] & 0xe0)) return 0;
    size_t end = size - 8, at = 10;
    unsigned flags = data[3];
    if (flags & 4) { /* FEXTRA */
        if (end - at < 2) return 0;
        size_t extra = (size_t)data[at] | (size_t)data[at + 1] << 8;
        at += 2;
        if (end - at < extra) return 0;
        at += extra;
    }
    for (unsigned flag = 8; flag <= 16; flag <<= 1) { /* FNAME, FCOMMENT: text up to a zero byte */
        if (!(flags & flag)) continue;
        while (at < end && data[at]) at++;
        if (at == end) return 0;
        at++;
    }
    if (flags & 2) { /* FHCRC: the low 16 bits of the CRC-32 of the header so far */
        if (end - at < 2) return 0;
        if ((cc2_crc32_update(0, data, at) & 0xffff) != ((uint32_t)data[at] | (uint32_t)data[at + 1] << 8)) return 0;
        at += 2;
    }
    const unsigned char *trailer = data + end;
    uint32_t named_crc = (uint32_t)trailer[0] | (uint32_t)trailer[1] << 8 | (uint32_t)trailer[2] << 16 | (uint32_t)trailer[3] << 24;
    uint32_t named_size = (uint32_t)trailer[4] | (uint32_t)trailer[5] << 8 | (uint32_t)trailer[6] << 16 | (uint32_t)trailer[7] << 24;
    if (named_crc != crc || named_size != (uint32_t)plain_size) return 0;

    cc2_gzc_state *s = malloc(sizeof(*s));
    if (!s) return 0;
    s->in = data + at;
    s->in_length = end - at;
    s->position = 0;
    s->bits = 0;
    s->count = 0;
    s->total = 0;
    s->limit = plain_size;
    s->crc = 0;
    s->window_at = 0;
    int ok, last;
    do {
        last = (int)cc2_gzc_take(s, 1);
        switch (cc2_gzc_take(s, 2)) {
        case 0: ok = cc2_gzc_stored(s); break;
        case 1: ok = cc2_gzc_fixed(s) && cc2_gzc_codes(s); break;
        case 2: ok = cc2_gzc_dynamic(s) && cc2_gzc_codes(s); break;
        default: ok = 0; break;
        }
    } while (ok && !last);
    /* The stream must end in the last byte before the trailer: whole bytes left in the bit buffer go back. */
    if (ok) ok = s->position <= s->in_length + 4 && s->position - (size_t)s->count / 8 == s->in_length &&
                 s->total == plain_size && cc2_crc32_update(s->crc, s->window, s->window_at) == crc;
    free(s);
    return ok;
}

#endif
