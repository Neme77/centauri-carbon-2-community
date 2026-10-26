#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "monocypher-ed25519.h"
#include "trusted-key.h"
#include "trusted-base.h"

#ifndef CC2_COMMUNITY_FIRMWARE_VERSION
#error Build with the central firmware version
#endif
#ifndef CC2_CONTROL_VERSION
#error Build with the central application version
#endif

#define UPDATE_MAX (32U * 1024U * 1024U)
#define MANIFEST_MAX 1024
#define FILES_MAX 32

typedef struct {
    FILE *file;
    uint64_t size;
    long offset;
    char version[32];
} package;

static int fail(const char *reason) {
    fprintf(stderr, "Update rejected: %s\n", reason);
    return 0;
}

static uint64_t big_endian(const unsigned char *p, unsigned n) {
    uint64_t v = 0;
    while (n--) v = (v << 8) | *p++;
    return v;
}

static int version(const char *s, unsigned v[3]) {
    for (unsigned i = 0; i < 3; i++) {
        unsigned digits = 0;
        v[i] = 0;
        while (isdigit((unsigned char)*s)) {
            if (++digits > 6) return 0;
            v[i] = v[i] * 10 + (unsigned)(*s++ - '0');
        }
        if (!digits) return 0;
        if (i < 2) { if (*s++ != '.') return 0; }
        else if (*s) return 0;
    }
    return 1;
}

static int newer(const char *candidate) {
    unsigned a[3], b[3];
    if (!version(candidate, a) || !version(CC2_CONTROL_VERSION, b)) return 0;
    for (unsigned i = 0; i < 3; i++) if (a[i] != b[i]) return a[i] > b[i];
    return 0;
}

static int hex(const char *s, unsigned char *out, unsigned n) {
    if (strlen(s) != 2 * n) return 0;
    for (unsigned i = 0; i < n; i++) {
        unsigned value = 0;
        for (unsigned j = 0; j < 2; j++) {
            unsigned char c = (unsigned char)s[i * 2 + j];
            if (c >= '0' && c <= '9') c -= '0';
            else if (c >= 'a' && c <= 'f') c -= 'a' - 10;
            else return 0;
            value = value * 16 + c;
        }
        out[i] = (unsigned char)value;
    }
    return 1;
}

static int line(char **p, const char *key, char *value, size_t capacity) {
    size_t k = strlen(key);
    char *end = strchr(*p, '\n');
    if (!end || strncmp(*p, key, k) || (*p)[k] != '=') return 0;
    size_t n = (size_t)(end - *p) - k - 1;
    if (!n || n >= capacity) return 0;
    memcpy(value, *p + k + 1, n); value[n] = 0;
    *p = end + 1;
    return 1;
}

/* Verify before creating or executing anything from the uploaded package.
 * The signature binds the header and canonical manifest; the manifest binds
 * every payload byte through a streamed SHA-512 digest. */
static int verify(const char *path, package *pkg) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    struct stat st;
    if (fd < 0) return fail("cannot open package");
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size < 84 || st.st_size > UPDATE_MAX + 84 + MANIFEST_MAX) {
        close(fd); return fail("invalid package size or type");
    }
    pkg->file = fdopen(fd, "rb");
    if (!pkg->file) { close(fd); return fail("cannot read package"); }
    unsigned char header[20], signature[64], signed_message[20 + MANIFEST_MAX];
    char manifest[MANIFEST_MAX + 1];
    if (fread(header, 1, 20, pkg->file) != 20 || memcmp(header, "CC2UPD1\n", 8)) return fail("invalid header");
    unsigned length = (unsigned)big_endian(header + 8, 4);
    pkg->size = big_endian(header + 12, 8);
    if (!length || length > MANIFEST_MAX || pkg->size < 1024 || pkg->size > UPDATE_MAX ||
        (uint64_t)st.st_size != 84ULL + length + pkg->size) return fail("invalid lengths");
    if (fread(signature, 1, 64, pkg->file) != 64 || fread(manifest, 1, length, pkg->file) != length) return fail("short manifest");
    manifest[length] = 0;
    if (memchr(manifest, 0, length)) return fail("invalid manifest encoding");
    memcpy(signed_message, header, 20); memcpy(signed_message + 20, manifest, length);
    if (crypto_ed25519_check(signature, cc2_update_trusted_key, signed_message, 20 + length)) return fail("signature is not trusted");
    char *cursor = manifest, firmware[32], printer[65], daemon[65], size[32], digest[129];
    unsigned char expected[64], fingerprint[32];
    if (!line(&cursor, "version", pkg->version, sizeof(pkg->version)) ||
        !line(&cursor, "firmware", firmware, sizeof(firmware)) ||
        !line(&cursor, "printer", printer, sizeof(printer)) ||
        !line(&cursor, "daemon", daemon, sizeof(daemon)) ||
        !line(&cursor, "size", size, sizeof(size)) ||
        !line(&cursor, "sha512", digest, sizeof(digest)) || *cursor ||
        !hex(printer, fingerprint, 32) || !hex(daemon, fingerprint, 32) || !hex(digest, expected, 64)) return fail("invalid signed manifest");
    char *end;
    errno = 0;
    unsigned long long declared = strtoull(size, &end, 10);
    if (errno || *end || !isdigit((unsigned char)size[0]) || declared != pkg->size) return fail("payload length mismatch");
    if (strcmp(firmware, CC2_COMMUNITY_FIRMWARE_VERSION)) return fail("incompatible community firmware");
    if (strcmp(printer, CC2_UPDATE_PRINTER_SHA256) || strcmp(daemon, CC2_UPDATE_DAEMON_SHA256))
        return fail("incompatible firmware fingerprints");
    if (!newer(pkg->version)) return fail("version must be newer than the installed application");
    pkg->offset = ftell(pkg->file);
    crypto_sha512_ctx ctx;
    crypto_sha512_init(&ctx);
    unsigned char buffer[16384], actual[64];
    uint64_t remaining = pkg->size;
    while (remaining) {
        size_t n = remaining > sizeof(buffer) ? sizeof(buffer) : (size_t)remaining;
        if (fread(buffer, 1, n, pkg->file) != n) return fail("short payload");
        crypto_sha512_update(&ctx, buffer, n); remaining -= n;
    }
    crypto_sha512_final(&ctx, actual);
    if (crypto_verify64(actual, expected)) return fail("payload checksum mismatch");
    if (fseek(pkg->file, pkg->offset, SEEK_SET)) return fail("cannot rewind payload");
    return 1;
}

static int octal(const unsigned char *p, unsigned n, uint64_t *value) {
    *value = 0;
    unsigned i = 0;
    while (i < n && p[i] == ' ') i++;
    unsigned digits = 0;
    while (i < n && p[i] >= '0' && p[i] <= '7') {
        if (*value > UINT64_MAX / 8) return 0;
        *value = *value * 8 + p[i++] - '0'; digits++;
    }
    while (i < n && (p[i] == 0 || p[i] == ' ')) i++;
    return digits && i == n;
}

static int allowed(const char *name, int *executable) {
    static const char *const files[] = {"cc2-control", "start.sh", "launch.sh", "cc2-control.init",
        "install-on-printer.sh", "restore-on-printer.sh", "SHA256SUMS", "build-info.json",
        "cc2-update-worker", "updater-trusted-key.json", "THIRD_PARTY_NOTICES.txt", "web/index.html", "update-runner.sh", "cc2-https-download", "ca-bundle.pem"};
    *executable = 0;
    for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); i++) if (!strcmp(name, files[i])) {
        *executable = i < 6 || i == 8 || i == 12 || i == 13;
        return 1;
    }
    if (strncmp(name, "web/locales/", 12)) return 0;
    const char *code = name + 12;
    size_t n = strlen(code);
    if (n < 7 || n > 13 || strcmp(code + n - 5, ".json")) return 0;
    for (size_t i = 0; i < n - 5; i++) if (!(code[i] >= 'a' && code[i] <= 'z') && code[i] != '_') return 0;
    return 1;
}

static int directory(int parent, const char *name) {
    if (mkdirat(parent, name, 0700) && errno != EEXIST) return -1;
    int fd = openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    struct stat st;
    if (fd >= 0 && (!fstat(fd, &st) && st.st_uid == geteuid() && !(st.st_mode & 0022))) return fd;
    if (fd >= 0) close(fd);
    return -1;
}

/* Do not delegate archive parsing to unzip/tar. Only the regular files
 * emitted by our packager are accepted; links, extensions and paths are
 * rejected even when a package was signed by a trusted publisher. */
static int extract(package *pkg, const char *stage) {
    int root = open(stage, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    struct stat st;
    if (root < 0 || fstat(root, &st) || st.st_uid != geteuid() || (st.st_mode & 0077)) {
        if (root >= 0) close(root);
        return fail("staging directory must be private and owned by the service");
    }
    int web = -1, locales = -1, ok = 0;
    unsigned count = 0, ends = 0, required = 0;
    uint64_t remaining = pkg->size;
    unsigned char header[512], buffer[16384];
    while (remaining >= 512) {
        if (fread(header, 1, 512, pkg->file) != 512) goto done;
        remaining -= 512;
        int zero = 1;
        for (unsigned i = 0; i < 512; i++) if (header[i]) { zero = 0; break; }
        if (zero) { ends++; continue; }
        if (ends || ++count > FILES_MAX || !memchr(header, 0, 100) || header[345] ||
            (header[156] != 0 && header[156] != '0')) goto done;
        uint64_t size, checksum;
        if (!octal(header + 124, 12, &size) || !octal(header + 148, 8, &checksum)) goto done;
        unsigned sum = 0;
        for (unsigned i = 0; i < 512; i++) sum += i >= 148 && i < 156 ? ' ' : header[i];
        if (sum != checksum || size > UPDATE_MAX || size + ((512 - size % 512) % 512) > remaining) goto done;
        const char *name = (const char *)header;
        int executable, parent = root;
        if (!allowed(name, &executable)) goto done;
        if (!strcmp(name, "cc2-control")) required |= 1;
        if (!strcmp(name, "install-on-printer.sh")) required |= 2;
        if (!strcmp(name, "restore-on-printer.sh")) required |= 4;
        if (!strcmp(name, "SHA256SUMS")) required |= 8;
        if (!strcmp(name, "web/index.html")) required |= 16;
        if (!strcmp(name, "web/locales/en.json")) required |= 32;
        if (!strcmp(name, "start.sh")) required |= 64;
        if (!strcmp(name, "launch.sh")) required |= 128;
        if (!strcmp(name, "cc2-control.init")) required |= 256;
        if (!strcmp(name, "build-info.json")) required |= 512;
        if (!strcmp(name, "cc2-update-worker")) required |= 1024;
        if (!strcmp(name, "update-runner.sh")) required |= 2048;
        if (!strcmp(name, "updater-trusted-key.json")) required |= 4096;
        if (!strcmp(name, "THIRD_PARTY_NOTICES.txt")) required |= 8192;
        if (!strcmp(name, "cc2-https-download")) required |= 16384;
        if (!strcmp(name, "ca-bundle.pem")) required |= 32768;
        if (!strncmp(name, "web/", 4)) {
            if (web < 0) web = directory(root, "web");
            if (web < 0) goto done;
            parent = web; name += 4;
            if (!strncmp(name, "locales/", 8)) {
                if (locales < 0) locales = directory(web, "locales");
                if (locales < 0) goto done;
                parent = locales; name += 8;
            }
        }
        int output = openat(parent, name, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, executable ? 0755 : 0644);
        if (output < 0) goto done;
        uint64_t left = size;
        int failed = 0;
        int arm_binary = !strcmp(name, "cc2-control") || !strcmp(name, "cc2-update-worker") || !strcmp(name, "cc2-https-download");
        if (arm_binary && size < 20) failed = 1;
        while (left && !failed) {
            size_t n = left > sizeof(buffer) ? sizeof(buffer) : (size_t)left;
            if (fread(buffer, 1, n, pkg->file) != n) { failed = 1; break; }
            if (arm_binary && left == size && (memcmp(buffer, "\177ELF\1\1", 6) || buffer[18] != 40 || buffer[19])) {
                failed = 1; break;
            }
            size_t offset = 0;
            while (offset < n) {
                ssize_t written = write(output, buffer + offset, n - offset);
                if (written < 0 && errno == EINTR) continue;
                if (written <= 0) { failed = 1; break; }
                offset += (size_t)written;
            }
            if (failed) break;
            left -= n;
        }
        if (fsync(output)) failed = 1;
        if (close(output)) failed = 1;
        if (failed) goto done;
        unsigned padding = (unsigned)((512 - size % 512) % 512);
        if (padding && fread(header, 1, padding, pkg->file) != padding) goto done;
        remaining -= size + padding;
    }
    ok = remaining == 0 && ends >= 2 && required == 65535;
done:
    if (locales >= 0) close(locales);
    if (web >= 0) close(web);
    close(root);
    return ok ? 1 : fail("invalid, duplicate or unsupported archive entry");
}

static int catalog(const char *path) {
    unsigned char input[8265], message[8200];
    FILE *file = fopen(path, "rb");
    if (!file) return fail("cannot read catalog");
    size_t size = fread(input, 1, sizeof(input), file);
    int complete = feof(file); fclose(file);
    if (!complete || size < 74 || size > 8264 || memcmp(input, "CC2CAT1\n", 8) ||
        input[72] != '{' || memchr(input + 72, 0, size - 72)) return fail("invalid catalog");
    memcpy(message, input, 8); memcpy(message + 8, input + 72, size - 72);
    if (crypto_ed25519_check(input + 8, cc2_update_trusted_key, message, size - 64))
        return fail("catalog signature is not trusted");
    return fwrite(input + 72, 1, size - 72, stdout) == size - 72;
}

int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--catalog")) return catalog(argv[2]) ? 0 : 1;
    if (argc != 3 && argc != 4) { fprintf(stderr, "Usage: cc2-update-worker --verify PACKAGE | --extract PACKAGE PRIVATE-DIR\n"); return 2; }
    int extracting = !strcmp(argv[1], "--extract");
    if ((!extracting && strcmp(argv[1], "--verify")) || argc != (extracting ? 4 : 3)) return 2;
    package pkg = {0};
    int ok = verify(argv[2], &pkg);
    if (ok && extracting) ok = extract(&pkg, argv[3]);
    if (pkg.file) fclose(pkg.file);
    if (!ok) return 1;
    printf("{\"verified\":true,\"version\":\"%s\",\"payload_bytes\":%llu}\n", pkg.version, (unsigned long long)pkg.size);
    return 0;
}
