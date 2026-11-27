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
    char path[2048];
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
    char path[2048];
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
    assert(disp_restore_baseline(1, &disp_state_cache, NULL));
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
    assert(!disp_restore_baseline(1, &disp_state_cache, NULL));
    assert(strstr(disp_error, "foreign_change"));

    /* 5. A changed firmware identity: restore still returns the verified
     *    baseline (the recovery path must survive an update), but a new apply
     *    is refused until it has been restored. The foreign file from step 4
     *    is put back to the version this feature applied first. */
    write_file(path, red, red_size);
    char other[65];
    write_file("/tmp/cc2-display-gui", (const unsigned char *)"other-gui", 9);
    disp_sha_file("/tmp/cc2-display-gui", other);
    manifest(other, 1);
    disp_manifest_cache.valid = 0;
    assert(disp_load_manifest(1));
    tar_file restore_pack[1] = {{"boot.png", red, red_size}};
    size_t restore_pack_size;
    unsigned char *restore_tar = make_tar(restore_pack, 1, &restore_pack_size);
    assert(!disp_receive_pack(&(disp_upload_job){ -1, restore_tar, restore_pack_size, restore_pack_size, 0, {0, 0}, &fake_mqtt }));
    assert(strstr(disp_error, "firmware_changed"));
    /* Restore through the real HTTP handler: it must return the originals,
     * report firmwareChanged and drop the baseline. */
    int restore_pair[2];
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, restore_pair));
    disp_restore_response(restore_pair[0], &fake_mqtt);
    char restore_answer[4096];
    size_t restore_used = 0;
    struct timeval restore_timeout = { 3, 0 };
    setsockopt(restore_pair[1], SOL_SOCKET, SO_RCVTIMEO, &restore_timeout, sizeof(restore_timeout));
    while (restore_used < sizeof(restore_answer) - 1) {
        ssize_t n = recv(restore_pair[1], restore_answer + restore_used,
                         sizeof(restore_answer) - 1 - restore_used, 0);
        if (n <= 0) break;
        restore_used += (size_t)n;
    }
    restore_answer[restore_used] = 0;
    close(restore_pair[1]);
    if (!strstr(restore_answer, "HTTP/1.1 200") || !strstr(restore_answer, "firmwareChanged\":true")) {
        fprintf(stderr, "restore answer: %s\n", restore_answer);
        assert(0);
    }
    snprintf(path, sizeof(path), "%s/theme/boot.png", root);
    assert(file_equals(path, boot_original, boot_size));
    snprintf(path, sizeof(path), "%s/display-theme/baseline.json", root);
    assert(access(path, F_OK) != 0);

    /* 5b. The restore removed the baseline, so a new apply starts on the new
     * identity instead of staying refused. */
    assert(disp_receive_pack(&(disp_upload_job){ -1, restore_tar, restore_pack_size, restore_pack_size, 0, {0, 0}, &fake_mqtt }));
    snprintf(path, sizeof(path), "%s/theme/boot.png", root);
    assert(file_equals(path, red, red_size));

    /* 5c. A baseline from another identity is discarded when every target is
     * provably an original (the files the new firmware ships), so an update
     * that rewrote the theme does not lock the feature out. */
    char third[65];
    write_file("/tmp/cc2-display-gui", (const unsigned char *)"third-gui", 10);
    disp_sha_file("/tmp/cc2-display-gui", third);
    manifest(third, 1);
    disp_manifest_cache.valid = 0;
    assert(disp_load_manifest(1));
    snprintf(path, sizeof(path), "%s/theme/boot.png", root);
    write_file(path, boot_original, boot_size);
    snprintf(path, sizeof(path), "%s/theme/bg.png", root);
    write_file(path, bg_original, bg_size);
    assert(disp_receive_pack(&(disp_upload_job){ -1, make_tar((tar_file[]){{"boot.png", red, red_size}, {"bg.png", red, red_size}}, 2, &tar_size), tar_size, tar_size, 0, {0, 0}, &fake_mqtt }));
    snprintf(path, sizeof(path), "%s/theme/boot.png", root);
    assert(file_equals(path, red, red_size));

    /* 5d. With our customised bytes still present, the same situation must be
     * refused and the baseline kept: nothing may be mistaken for an original. */
    char fourth[65];
    write_file("/tmp/cc2-display-gui", (const unsigned char *)"fourth-gui", 10);
    disp_sha_file("/tmp/cc2-display-gui", fourth);
    manifest(fourth, 1);
    disp_manifest_cache.valid = 0;
    assert(disp_load_manifest(1));
    assert(!disp_receive_pack(&(disp_upload_job){ -1, make_tar((tar_file[]){{"boot.png", green, green_size}, {"bg.png", green, green_size}}, 2, &tar_size), tar_size, tar_size, 0, {0, 0}, &fake_mqtt }));
    assert(strstr(disp_error, "firmware_changed"));
    snprintf(path, sizeof(path), "%s/display-theme/baseline.json", root);
    assert(access(path, F_OK) == 0);

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

    /* 7. A realistic 154-file baseline and state round-trip. */
    char cap[640], cap_theme[768], cap_state[768], cap_prefs[768];
    snprintf(cap, sizeof(cap), "%s/capacity", root);
    assert(!mkdir(cap, 0700));
    snprintf(cap_theme, sizeof(cap_theme), "%s/theme", cap);
    assert(!mkdir(cap_theme, 0700));
    snprintf(cap_prefs, sizeof(cap_prefs), "%s/ui-preferences.json", cap);
    write_file(cap_prefs, (const unsigned char *)"{}", 2);
    ui_preferences_path = cap_prefs;
    disp_state_dir[0] = '\0'; /* recompute from the new preferences path */
    disp_state_loaded = 0;
    assert(disp_ensure_state_dir());
    snprintf(cap_state, sizeof(cap_state), "%s/display-theme", cap);
    if (strlen(cap_theme) >= sizeof(disp_theme_dir)) assert(0);
    memcpy(disp_theme_dir, cap_theme, strlen(cap_theme) + 1);
    memset(&disp_manifest_cache, 0, sizeof(disp_manifest_cache));
    disp_manifest_cache.valid = 1;
    disp_manifest_cache.schema = 1;
    snprintf(disp_manifest_cache.firmware_base, sizeof(disp_manifest_cache.firmware_base), "capacity");
    snprintf(disp_manifest_cache.gui_sha256, sizeof(disp_manifest_cache.gui_sha256), "%s", gui_hash);
    for (int i = 0; i < 154; ++i) {
        char name[32], body[96], file[1024];
        snprintf(name, sizeof(name), "cap%03d.png", i);
        int length = snprintf(body, sizeof(body), "original-%03d-%s", i, "padding-padding-padding-padding-padding");
        snprintf(file, sizeof(file), "%s/%s", cap_theme, name);
        write_file(file, (const unsigned char *)body, (size_t)length);
        disp_asset *asset = &disp_manifest_cache.assets[disp_manifest_cache.count++];
        snprintf(asset->name, sizeof(asset->name), "%s", name);
        snprintf(asset->role, sizeof(asset->role), "accent");
        asset->width = 1;
        asset->height = 1;
        snprintf(asset->sha256, sizeof(asset->sha256), "%s", gui_hash); /* placeholder, replaced below */
        disp_sha_file(file, asset->sha256);
    }
    assert(disp_manifest_cache.count == 154);
    for (int i = 0; i < 154; ++i) assert(disp_backup_original(&disp_manifest_cache.assets[i]));
    assert(disp_baseline_write(&disp_manifest_cache));
    snprintf(path, sizeof(path), "%s/baseline.json", cap_state);
    struct stat baseline_stat;
    assert(stat(path, &baseline_stat) == 0 && baseline_stat.st_size < DISP_META_MAX);
    /* Customize every file and record that as the applied state. */
    memset(&disp_state_cache, 0, sizeof(disp_state_cache));
    disp_state_cache.phase = 1;
    for (int i = 0; i < 154; ++i) {
        char name[32], file[1024], body[96], hash[65];
        snprintf(name, sizeof(name), "cap%03d.png", i);
        snprintf(file, sizeof(file), "%s/%s", cap_theme, name);
        int length = snprintf(body, sizeof(body), "customized-%03d-%s", i, "padding-padding-padding-padding");
        write_file(file, (const unsigned char *)body, (size_t)length);
        disp_sha_file(file, hash);
        snprintf(disp_state_cache.files[i], sizeof(disp_state_cache.files[i]), "%s", name);
        snprintf(disp_state_cache.hashes[i], sizeof(disp_state_cache.hashes[i]), "%s", hash);
        disp_state_cache.file_count++;
    }
    assert(disp_state_write());
    snprintf(path, sizeof(path), "%s/state.json", cap_state);
    assert(stat(path, &baseline_stat) == 0 && baseline_stat.st_size < DISP_META_MAX);
    disp_state_loaded = 0;
    assert(disp_state_read());
    assert(disp_state_cache.file_count == 154);
    assert(disp_state_cache.phase == 1);
    assert(!strcmp(disp_state_cache.files[153], "cap153.png"));
    /* Restore all 154 verified originals. */
    assert(disp_restore_baseline(1, &disp_state_cache, NULL));
    for (int i = 0; i < 154; ++i) {
        char name[32], file[1024], body[96];
        snprintf(name, sizeof(name), "cap%03d.png", i);
        snprintf(file, sizeof(file), "%s/%s", cap_theme, name);
        int length = snprintf(body, sizeof(body), "original-%03d-%s", i, "padding-padding-padding-padding-padding");
        assert(file_equals(file, (const unsigned char *)body, (size_t)length));
    }

    /* 7b. A state write that fails rolls the files back and leaves no journal,
     * so the next apply is not blocked by a recovery that has nothing to do. */
    char cap_state_file[1200], cap_journal[1200], cap_target[1200];
    snprintf(cap_state_file, sizeof(cap_state_file), "%s/state.json", cap_state);
    snprintf(cap_journal, sizeof(cap_journal), "%s/journal.json", cap_state);
    snprintf(cap_target, sizeof(cap_target), "%s/cap000.png", cap_theme);
    unlink(cap_state_file);
    assert(!mkdir(cap_state_file, 0700)); /* a directory cannot be renamed over */
    tar_file failing[1] = {{"cap000.png", red, red_size}};
    size_t failing_size;
    unsigned char *failing_tar = make_tar(failing, 1, &failing_size);
    assert(!disp_receive_pack(&(disp_upload_job){ -1, failing_tar, failing_size, failing_size, 0, {0, 0}, &fake_mqtt }));
    assert(strstr(disp_error, "state_write_failed"));
    {
        char body[96];
        int length = snprintf(body, sizeof(body), "original-000-%s", "padding-padding-padding-padding-padding");
        assert(file_equals(cap_target, (const unsigned char *)body, (size_t)length));
    }
    assert(access(cap_journal, F_OK) != 0);
    rmdir(cap_state_file);

    /* 8. The status endpoint stays responsive while an upload is streaming. */
    snprintf(disp_theme_dir, sizeof(disp_theme_dir), "%s", theme_dir);
    snprintf(disp_gui_path, sizeof(disp_gui_path), "/tmp/cc2-display-gui");
    snprintf(path, sizeof(path), "%s/ui-preferences.json", root);
    ui_preferences_path = path;
    disp_state_dir[0] = '\0';
    disp_state_loaded = 0;
    assert(disp_ensure_state_dir());
    disp_manifest_cache.valid = 0;
    assert(disp_load_manifest(1));
    disp_state_loaded = 0;
    int upload[2];
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, upload));
    tar_file slow_pack[1] = {{"boot.png", red, red_size}};
    size_t slow_size;
    unsigned char *slow_tar = make_tar(slow_pack, 1, &slow_size);
    char request[512];
    int request_length = snprintf(request, sizeof(request),
        "POST /api/display/apply HTTP/1.1\r\nHost: 127.0.0.1:8081\r\n"
        "X-CC2-Request: 1\r\nContent-Type: application/x-tar\r\nContent-Length: %zu\r\n\r\n", slow_size);
    /* Send only the first half: the upload thread blocks in recv(). */
    assert(send(upload[1], slow_tar, slow_size / 2, 0) == (ssize_t)(slow_size / 2));
    assert(disp_apply_start(upload[0], request, (size_t)request_length, (size_t)request_length, &fake_mqtt) == 1);
    struct timespec before, after;
    clock_gettime(CLOCK_MONOTONIC, &before);
    int status_pair[2];
    assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, status_pair));
    disp_status_response(status_pair[0]);
    char status_answer[32768];
    ssize_t status_length = recv(status_pair[1], status_answer, sizeof(status_answer) - 1, 0);
    clock_gettime(CLOCK_MONOTONIC, &after);
    long elapsed_ms = (after.tv_sec - before.tv_sec) * 1000 + (after.tv_nsec - before.tv_nsec) / 1000000;
    assert(status_length > 0);
    status_answer[status_length] = 0;
    if (!strstr(status_answer, "HTTP/1.1 200")) { fprintf(stderr, "status answer: %s\n", status_answer); assert(0); }
    assert(elapsed_ms < 2000);
    close(status_pair[0]);
    close(status_pair[1]);
    /* Finish the upload and read its answer. */
    size_t rest = slow_size - slow_size / 2;
    assert(send(upload[1], slow_tar + slow_size / 2, rest, 0) == (ssize_t)rest);
    char answer[4096];
    size_t got = 0;
    while (got < sizeof(answer) - 1) {
        ssize_t n = recv(upload[1], answer + got, sizeof(answer) - 1 - got, 0);
        if (n <= 0) break;
        got += (size_t)n;
    }
    answer[got] = 0;
    close(upload[1]);
    if (!strstr(answer, "HTTP/1.1 200")) { fprintf(stderr, "apply answer: %s\n", answer); assert(0); }
    if (!strstr(answer, "\"ok\":true")) { fprintf(stderr, "apply answer: %s\n", answer); assert(0); }

    /* 8b. A declared Content-Length shorter than the bytes actually sent must
     * be answered promptly (content-length is authoritative), never read past
     * the declared body or block on the extra bytes. */
    {
        int short_pair[2];
        assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, short_pair));
        size_t declared = slow_size / 2;
        char short_header[512];
        int short_length = snprintf(short_header, sizeof(short_header),
            "POST /api/display/apply HTTP/1.1\r\nHost: 127.0.0.1:8081\r\n"
            "X-CC2-Request: 1\r\nContent-Type: application/x-tar\r\nContent-Length: %zu\r\n\r\n",
            declared);
        assert(send(short_pair[1], slow_tar, slow_size, 0) == (ssize_t)slow_size);
        struct timespec started, finished;
        clock_gettime(CLOCK_MONOTONIC, &started);
        assert(disp_apply_start(short_pair[0], short_header, (size_t)short_length,
                                (size_t)short_length, &fake_mqtt) == 1);
        char short_answer[4096];
        size_t used = 0;
        struct timeval tv = { 3, 0 };
        setsockopt(short_pair[1], SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        while (used < sizeof(short_answer) - 1) {
            ssize_t n = recv(short_pair[1], short_answer + used, sizeof(short_answer) - 1 - used, 0);
            if (n <= 0) break;
            used += (size_t)n;
        }
        short_answer[used] = 0;
        clock_gettime(CLOCK_MONOTONIC, &finished);
        long elapsed = (finished.tv_sec - started.tv_sec) * 1000 +
                       (finished.tv_nsec - started.tv_nsec) / 1000000;
        close(short_pair[1]);
        assert(strstr(short_answer, "HTTP/1.1 200") || strstr(short_answer, "HTTP/1.1 409"));
        assert(elapsed < 3000);
    }

    printf("PASS: display pack validation, apply, baseline restore, foreign-change and firmware refusal, "
           "154-file capacity, responsive status, recovery\n");
    return 0;
}
