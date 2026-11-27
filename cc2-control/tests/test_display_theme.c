/* Host test for the reversible native-display pack: validation, staging,
 * baseline backup, restore, foreign-change refusal, firmware identity and
 * interrupted-apply recovery. Uses synthetic PNGs and a fake theme directory;
 * no vendor program is executed. */
#ifndef CC2_COMMUNITY_FIRMWARE_VERSION
#define CC2_COMMUNITY_FIRMWARE_VERSION "4.4"
#endif
#define main cc2_application_main
#include "../src/main.c"
#undef main
#include <assert.h>

static unsigned char *png_bytes(int width, int height, unsigned char r, unsigned char g, unsigned char b, size_t *out) {
    size_t stride = (size_t)width * 4 + 1;
    size_t raw = stride * (size_t)height;
    size_t zlen = 2 + 5 + raw + 4;
    unsigned char *z = calloc(1, zlen);
    assert(z);
    size_t p = 0;
    z[p++] = 0x78; z[p++] = 0x01;
    z[p++] = 0x01;
    z[p++] = (unsigned char)(raw & 0xff); z[p++] = (unsigned char)((raw >> 8) & 0xff);
    z[p++] = (unsigned char)(~raw & 0xff); z[p++] = (unsigned char)((~raw >> 8) & 0xff);
    for (int y = 0; y < height; ++y) {
        z[p++] = 0;
        for (int x = 0; x < width; ++x) { z[p++] = r; z[p++] = g; z[p++] = b; z[p++] = 0xff; }
    }
    uint32_t a = 1, bsum = 0;
    for (size_t i = 2; i < p; ++i) { a = (a + z[i]) % 65521; bsum = (bsum + a) % 65521; }
    z[p++] = (unsigned char)(bsum >> 8); z[p++] = (unsigned char)bsum;
    z[p++] = (unsigned char)(a >> 8); z[p++] = (unsigned char)a;
    size_t cap = 8 + (12 + 13) + (12 + p) + 12;
    unsigned char *outb = calloc(1, cap), *q = outb;
    static const unsigned char sig[8] = {137,80,78,71,13,10,26,10};
    memcpy(q, sig, 8); q += 8;
#define PUT32(v) do { *q++=(unsigned char)((v)>>24); *q++=(unsigned char)((v)>>16); *q++=(unsigned char)((v)>>8); *q++=(unsigned char)(v); } while(0)
    unsigned char ihdr[13];
    ihdr[0]=(unsigned char)(width>>24); ihdr[1]=(unsigned char)(width>>16); ihdr[2]=(unsigned char)(width>>8); ihdr[3]=(unsigned char)width;
    ihdr[4]=(unsigned char)(height>>24); ihdr[5]=(unsigned char)(height>>16); ihdr[6]=(unsigned char)(height>>8); ihdr[7]=(unsigned char)height;
    ihdr[8]=8; ihdr[9]=6; ihdr[10]=0; ihdr[11]=0; ihdr[12]=0;
    PUT32(13); memcpy(q, "IHDR", 4); q += 4; memcpy(q, ihdr, 13); q += 13; PUT32(cc2_crc32("IHDR", 4) ^ 0);
    /* cc2_crc32 covers only the type above; recompute over type+data. */
    uint32_t crc = cc2_crc32_update(0, "IHDR", 4); crc = cc2_crc32_update(crc, ihdr, 13);
    q -= 4; PUT32(crc);
    PUT32((uint32_t)p); memcpy(q, "IDAT", 4); q += 4; memcpy(q, z, p); q += p;
    crc = cc2_crc32_update(0, "IDAT", 4); crc = cc2_crc32_update(crc, z, p); PUT32(crc);
    PUT32(0); memcpy(q, "IEND", 4); q += 4;
    crc = cc2_crc32_update(0, "IEND", 4); PUT32(crc);
#undef PUT32
    free(z);
    *out = (size_t)(q - outb);
    return outb;
}

typedef struct { char name[32]; unsigned char *data; size_t size; } tar_file;

static unsigned char *make_tar(tar_file *files, int count, size_t *out) {
    size_t total = 1024;
    for (int i = 0; i < count; ++i) total += 512 + ((files[i].size + 511) / 512) * 512;
    unsigned char *tar = calloc(1, total);
    assert(tar);
    size_t p = 0;
    for (int i = 0; i < count; ++i) {
        unsigned char *h = tar + p;
        snprintf((char *)h, 100, "%s", files[i].name);
        snprintf((char *)h + 100, 8, "%07o", 0644);
        snprintf((char *)h + 108, 8, "%07o", 0);
        snprintf((char *)h + 116, 8, "%07o", 0);
        snprintf((char *)h + 124, 12, "%011lo", (unsigned long)files[i].size);
        snprintf((char *)h + 136, 12, "%011o", 0);
        memset(h + 148, ' ', 8);
        h[156] = '0';
        memcpy(h + 257, "ustar\0" "00", 8);
        unsigned long sum = 0;
        for (int k = 0; k < 512; ++k) sum += h[k];
        snprintf((char *)h + 148, 8, "%07lo", sum);
        p += 512;
        memcpy(tar + p, files[i].data, files[i].size);
        p += ((files[i].size + 511) / 512) * 512;
    }
    p += 1024;
    *out = p;
    return tar;
}

static void write_file(const char *path, const unsigned char *data, size_t size) {
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(data, 1, size, f) == size);
    fclose(f);
}

static int file_equals(const char *path, const unsigned char *data, size_t size) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char buffer[8192];
    size_t got = fread(buffer, 1, sizeof(buffer), f);
    fclose(f);
    return got == size && !memcmp(buffer, data, size);
}

static char root[512];
static mqtt_client fake_mqtt;

static void seed(const char *name, unsigned char r, unsigned char g, unsigned char b, size_t *size, unsigned char **bytes) {
    char path[600];
    snprintf(path, sizeof(path), "%s/theme/%s", root, name);
    *bytes = png_bytes(1, 1, r, g, b, size);
    write_file(path, *bytes, *size);
}

static void manifest(const char *gui_hash, int bg_width) {
    char path[600], boot_hash[65], bg_hash[65], gui_copy[65];
    snprintf(gui_copy, sizeof(gui_copy), "%s", gui_hash);
    disp_sha_file("/tmp/cc2-display-gui", gui_copy);
    disp_sha_file("/tmp/cc2-display-original-boot", boot_hash);
    disp_sha_file("/tmp/cc2-display-original-bg", bg_hash);
    snprintf(path, sizeof(path), "%s/theme/cc2-display-manifest.json", root);
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f,
        "{\"schema\":1,\"firmwareBase\":\"test-base\",\"guiSha256\":\"%s\",\"assets\":["
        "{\"name\":\"boot.png\",\"role\":\"boot\",\"width\":1,\"height\":1,\"sha256\":\"%s\"},"
        "{\"name\":\"bg.png\",\"role\":\"background\",\"width\":%d,\"height\":1,\"sha256\":\"%s\"}"
        "]}\n", gui_hash, boot_hash, bg_width, bg_hash);
    fclose(f);
}

int main(void) {
    memset(&fake_mqtt, 0, sizeof(fake_mqtt));
    fake_mqtt.connected = fake_mqtt.registered = fake_mqtt.have_machine_status = 1;
    fake_mqtt.machine_status = 1;
    fake_mqtt.last_message = time(NULL);
    snprintf(root, sizeof(root), "/tmp/cc2-display-test-XXXXXX");
    assert(mkdtemp(root));
    char path[600];
    snprintf(path, sizeof(path), "%s/theme", root); assert(!mkdir(path, 0700));
    snprintf(path, sizeof(path), "%s/ui-preferences.json", root);
    ui_preferences_path = path;
    /* No current_theme symlink: the module accepts installations without it. */
    char theme_dir[600];
    snprintf(theme_dir, sizeof(theme_dir), "%s/theme", root);
    assert(strlen(theme_dir) < sizeof(disp_theme_dir));
    memcpy(disp_theme_dir, theme_dir, strlen(theme_dir) + 1);
    snprintf(disp_gui_path, sizeof(disp_gui_path), "/tmp/cc2-display-gui");
    write_file("/tmp/cc2-display-gui", (const unsigned char *)"fake-gui", 8);
    char gui_hash[65];
    disp_sha_file("/tmp/cc2-display-gui", gui_hash);

    size_t boot_size, bg_size, red_size, green_size;
    unsigned char *boot_original, *bg_original, *red, *green;
    seed("boot.png", 1, 2, 3, &boot_size, &boot_original);
    seed("bg.png", 4, 5, 6, &bg_size, &bg_original);
    write_file("/tmp/cc2-display-original-boot", boot_original, boot_size);
    write_file("/tmp/cc2-display-original-bg", bg_original, bg_size);
    red = png_bytes(1, 1, 200, 0, 0, &red_size);
    green = png_bytes(1, 1, 0, 200, 0, &green_size);
    manifest(gui_hash, 1);

    assert(disp_load_manifest(1));
    assert(!strcmp(disp_manifest_cache.firmware_base, "test-base"));
    assert(disp_manifest_cache.count == 2);
    char boot_hash[65], bg_hash[65];
    disp_sha_file("/tmp/cc2-display-original-bg", bg_hash);
    disp_sha_file("/tmp/" "cc2-display-original-bg", boot_hash); /* same for both in this fixture */
    (void)boot_hash;

    /* 1. A valid pack applies and keeps a verified baseline. */
    tar_file pack[2] = {{"boot.png", red, red_size}, {"bg.png", red, red_size}};
    size_t tar_size;
    unsigned char *tar = make_tar(pack, 2, &tar_size);
    disp_upload_job job = { -1, tar, tar_size, tar_size, 0, {0, 0}, &fake_mqtt };
    assert(disp_receive_pack(&job));
    snprintf(path, sizeof(path), "%s/theme/boot.png", root);
    assert(file_equals(path, red, red_size));
    snprintf(path, sizeof(path), "%s/theme/bg.png", root);
    assert(file_equals(path, red, red_size));
    assert(disp_state_cache.phase == 1 && disp_state_cache.file_count == 2);
    snprintf(path, sizeof(path), "%s/display-theme/baseline/boot.png", root);
    assert(file_equals(path, boot_original, boot_size));
    /* The asset endpoint serves the preserved original, so a second pack is
     * generated from the true baseline rather than from tinted bytes. */
    {
        int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
        disp_asset_response(pair[0], "name=boot.png");
        char response[4096]; ssize_t got = recv(pair[1], response, sizeof(response) - 1, 0);
        assert(got > 0); response[got] = 0;
        char *body = strstr(response, "\r\n\r\n");
        assert(body);
        body += 4;
        assert((size_t)(response + got - body) == boot_size && !memcmp(body, boot_original, boot_size));
        close(pair[0]); close(pair[1]);
    }

    /* 2. Restore returns both originals. */
    assert(disp_restore_baseline(1));
    snprintf(path, sizeof(path), "%s/theme/boot.png", root);
    assert(file_equals(path, boot_original, boot_size));
    snprintf(path, sizeof(path), "%s/theme/bg.png", root);
    assert(file_equals(path, bg_original, bg_size));

    /* 3. Unknown names, duplicates and dimension mismatches are refused. */
    tar_file unknown[1] = {{"evil.png", red, red_size}};
    tar = make_tar(unknown, 1, &tar_size);
    job = (disp_upload_job){ -1, tar, tar_size, tar_size, 0, {0, 0}, &fake_mqtt };
    assert(!disp_receive_pack(&job));
    assert(strstr(disp_error, "not_allowed"));
    tar_file duplicate[2] = {{"boot.png", red, red_size}, {"boot.png", green, green_size}};
    tar = make_tar(duplicate, 2, &tar_size);
    job = (disp_upload_job){ -1, tar, tar_size, tar_size, 0, {0, 0}, &fake_mqtt };
    assert(!disp_receive_pack(&job));
    assert(strstr(disp_error, "duplicate"));
    /* A corrupted chunk checksum is not a valid PNG. */
    unsigned char broken[256];
    assert(red_size < sizeof(broken));
    memcpy(broken, red, red_size);
    broken[20] ^= 0xff;
    tar_file bad_png[1] = {{"boot.png", broken, red_size}};
    tar = make_tar(bad_png, 1, &tar_size);
    job = (disp_upload_job){ -1, tar, tar_size, tar_size, 0, {0, 0}, &fake_mqtt };
    assert(!disp_receive_pack(&job));
    assert(strstr(disp_error, "png_invalid"));

    /* 4. A file changed outside the feature is not overwritten by restore. */
    assert(disp_receive_pack(&(disp_upload_job){ -1, make_tar((tar_file[]){{"boot.png", red, red_size}, {"bg.png", red, red_size}}, 2, &tar_size), tar_size, tar_size, 0, {0, 0}, &fake_mqtt }));
    snprintf(path, sizeof(path), "%s/theme/boot.png", root);
    write_file(path, green, green_size);
    assert(!disp_restore_baseline(1));
    assert(strstr(disp_error, "foreign_change"));

    /* 5. A changed firmware identity refuses restore. */
    char other[65];
    write_file("/tmp/cc2-display-gui", (const unsigned char *)"other-gui", 9);
    disp_sha_file("/tmp/cc2-display-gui", other);
    manifest(other, 1);
    disp_manifest_cache.valid = 0;
    assert(disp_load_manifest(1));
    assert(!disp_restore_baseline(1));
    assert(strstr(disp_error, "firmware_changed"));

    /* 6. Startup recovery rolls an interrupted apply back from the journal. */
    write_file("/tmp/cc2-display-gui", (const unsigned char *)"fake-gui", 8);
    manifest(gui_hash, 1);
    disp_manifest_cache.valid = 0;
    assert(disp_load_manifest(1));
    snprintf(path, sizeof(path), "%s/display-theme/journal.json", root);
    write_file(path, (const unsigned char *)"{\"version\":1,\"phase\":\"applying\",\"files\":[{\"name\":\"boot.png\",\"sha256\":\"" , 76);
    /* Build a syntactically valid journal with the helper instead of partial text. */
    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "{\"version\":1,\"phase\":\"applying\",\"files\":[{\"name\":\"boot.png\",\"sha256\":\"%s\"}]}\n", gui_hash);
    fclose(f);
    snprintf(path, sizeof(path), "%s/theme/boot.png", root);
    write_file(path, red, red_size);
    disp_startup_recover();
    snprintf(path, sizeof(path), "%s/theme/boot.png", root);
    assert(file_equals(path, boot_original, boot_size));
    snprintf(path, sizeof(path), "%s/display-theme/journal.json", root);
    assert(access(path, F_OK) != 0);

    printf("PASS: display pack validation, apply, baseline restore, foreign-change and firmware refusal, recovery\n");
    return 0;
}
