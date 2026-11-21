/* G-code thumbnail reader and sizing for the native Centauri Carbon 2 UI. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    HEADER_READS = 100,
    LINE_SIZE = 256,
    MAX_ENCODED = 1024 * 1024,
    MAX_BLOCK_BYTES = 2 * MAX_ENCODED,
    MAX_DIMENSION = 1024,
    PREVIEW_SIZE = 144,
    IMG_SIZE_MODE_VIRTUAL = 0,
    IMG_SIZE_MODE_REAL = 1
};

static int base64_digit(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static uint32_t read_be32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | p[3];
}

static bool png_header_matches(const char *text, unsigned width, unsigned height)
{
    static const unsigned char signature_and_ihdr[] = {
        137, 'P', 'N', 'G', 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R'
    };
    unsigned char header[24];
    for (size_t i = 0, j = 0; i < sizeof(header); i += 3, j += 4) {
        int a = base64_digit(text[j]), b = base64_digit(text[j + 1]);
        int c = base64_digit(text[j + 2]), d = base64_digit(text[j + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0) return false;
        header[i] = (unsigned char)(a << 2 | b >> 4);
        header[i + 1] = (unsigned char)(b << 4 | c >> 2);
        header[i + 2] = (unsigned char)(c << 6 | d);
    }
    return memcmp(header, signature_and_ihdr, sizeof(signature_and_ihdr)) == 0 &&
           read_be32(header + 16) == width && read_be32(header + 20) == height;
}

static bool read_block(FILE *file, char *text, size_t length)
{
    char line[LINE_SIZE];
    size_t scanned = 0, used = 0;
    bool padded = false;
    while (fgets(line, sizeof(line), file)) {
        size_t size = strlen(line);
        scanned += size;
        if (scanned > MAX_BLOCK_BYTES || line[0] != ';') return false;
        char *payload = line + 1;
        while (*payload == ' ' || *payload == '\t') ++payload;
        char *end = line + size;
        while (end > payload && strchr(" \t\r\n", end[-1])) --end;
        *end = '\0';
        if (strcmp(payload, "thumbnail end") == 0) return used == length;
        size_t count = (size_t)(end - payload);
        if (!count || count > length - used) return false;
        for (size_t i = 0; i < count; ++i) {
            unsigned char value = (unsigned char)payload[i];
            if (value == '=') {
                if (used + i < length - 2) return false;
                padded = true;
            } else if (padded || base64_digit(value) < 0) {
                return false;
            }
        }
        memcpy(text + used, payload, count);
        used += count;
    }
    return false;
}

/* Signature and ownership match the vendor extractor's callers. */
char *cc2_extract_thumbnail(const char *path)
{
    if (!path) return NULL;
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    char line[LINE_SIZE], extra, *text = NULL;
    unsigned width, height, length;
    for (unsigned i = 0; i < HEADER_READS && fgets(line, sizeof(line), file); ++i) {
        if (sscanf(line, "; thumbnail begin %4ux%4u %7u %c", &width, &height, &length, &extra) != 3)
            continue;
        if (!width || !height || width > MAX_DIMENSION || height > MAX_DIMENSION ||
            length < 32 || length > MAX_ENCODED || length % 4)
            break;
        text = malloc(length + 1);
        if (text && (!read_block(file, text, length) || !png_header_matches(text, width, height))) {
            free(text);
            text = NULL;
        }
        if (text) text[length] = '\0';
        break;
    }
    fclose(file);
    return text;
}

/* LVGL 8 entry points in the verified native executable. */
extern int16_t lv_obj_get_self_width(void *image);
extern int16_t lv_obj_get_self_height(void *image);
extern uint16_t lv_img_get_zoom(void *image);
extern void lv_img_set_zoom(void *image, uint16_t zoom);
extern void lv_img_set_size_mode(void *image, uint8_t mode);
extern void lv_img_set_src(void *image, const void *src);

void cc2_set_thumbnail_zoom(void *image, uint16_t zoom)
{
    int16_t width = lv_obj_get_self_width(image);
    int16_t height = lv_obj_get_self_height(image);
    int16_t side = width > height ? width : height;
    if (side > PREVIEW_SIZE) {
        unsigned fit = (unsigned)zoom * PREVIEW_SIZE / (unsigned)side;
        lv_img_set_size_mode(image, IMG_SIZE_MODE_REAL);
        zoom = fit ? (uint16_t)fit : 1;
    }
    lv_img_set_zoom(image, zoom);
}

void cc2_set_thumbnail_src(void *image, const void *src)
{
    lv_img_set_size_mode(image, IMG_SIZE_MODE_VIRTUAL);
    lv_img_set_src(image, src);
    cc2_set_thumbnail_zoom(image, lv_img_get_zoom(image));
}
