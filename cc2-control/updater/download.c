#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/x509_crt.h"

/* One short-lived, certificate-verified HTTPS client. Never load this TLS
 * library into the resident printer application, and never permit HTTP. */
#define MAX_RESPONSE_HEADERS 16384
static int url(const char *value, char host[100], char path[4096]) {
    if (strncmp(value, "https://", 8) || strlen(value) >= 4196) return 0;
    const char *slash = strchr(value + 8, '/');
    if (!slash || slash == value + 8 || slash - (value + 8) >= 100) return 0;
    memcpy(host, value + 8, (size_t)(slash - value - 8)); host[slash - value - 8] = 0;
    strcpy(path, slash);
    for (const unsigned char *p = (const unsigned char *)value; *p; p++)
        if (*p <= 32 || *p >= 127 || *p == '#' || *p == '\\') return 0;
    if (!strcmp(host, "github.com"))
        return !strncmp(path, "/Neme77/centauri-carbon-2-community/releases/download/", sizeof("/Neme77/centauri-carbon-2-community/releases/download/") - 1);
    if (!strcmp(host, "raw.githubusercontent.com")) {
#ifdef CC2_HTTPS_TEST_DOCUMENT
        if (!strcmp(path, "/Neme77/centauri-carbon-2-community/develop/README.md")) return 1;
#endif
        return !strcmp(path, "/Neme77/centauri-carbon-2-community/develop/cc2-control/update-channel.cc2catalog");
    }
    return !strcmp(host, "release-assets.githubusercontent.com");
}

static int line(mbedtls_ssl_context *ssl, char *out, size_t capacity, size_t *headers) {
    size_t used = 0;
    for (;;) {
        unsigned char c;
        int n = mbedtls_ssl_read(ssl, &c, 1);
        if (n <= 0 || ++*headers > MAX_RESPONSE_HEADERS || used + 1 >= capacity || !c) return 0;
        out[used++] = (char)c;
        if (c == '\n') {
            if (used < 2 || out[used - 2] != '\r') return 0;
            out[used - 2] = 0; return 1;
        }
    }
}

static int copy(mbedtls_ssl_context *ssl, uint64_t count, uint64_t *total, uint64_t maximum) {
    unsigned char buffer[16384];
    if (count > maximum - *total) return 0;
    while (count) {
        size_t want = count > sizeof(buffer) ? sizeof(buffer) : (size_t)count;
        int n = mbedtls_ssl_read(ssl, buffer, want);
        if (n <= 0 || fwrite(buffer, 1, (size_t)n, stdout) != (size_t)n) return 0;
        count -= (unsigned)n; *total += (unsigned)n;
    }
    return 1;
}

static int get(const char *target, const char *ca, uint64_t maximum, char redirect[4196]) {
    /* GitHub's redirect CSP can exceed 8 KiB. Any line that fits inside
     * the existing total-header budget must fit this bounded scratch buffer. */
    char host[100], path[4096], request[4350], text[MAX_RESPONSE_HEADERS];
    if (!url(target, host, path)) return 0;
    mbedtls_net_context net; mbedtls_ssl_context ssl; mbedtls_ssl_config config;
    mbedtls_x509_crt roots; mbedtls_entropy_context entropy; mbedtls_ctr_drbg_context random;
    mbedtls_net_init(&net); mbedtls_ssl_init(&ssl); mbedtls_ssl_config_init(&config);
    mbedtls_x509_crt_init(&roots); mbedtls_entropy_init(&entropy); mbedtls_ctr_drbg_init(&random);
    int ok = 0;
    if (mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy, (const unsigned char *)"cc2-update", 10) ||
        mbedtls_x509_crt_parse_file(&roots, ca) != 0 ||
        mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT)) goto done;
    mbedtls_ssl_conf_authmode(&config, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&config, &roots, NULL);
    mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &random);
    mbedtls_ssl_conf_read_timeout(&config, 15000);
    if (mbedtls_net_connect(&net, host, "443", MBEDTLS_NET_PROTO_TCP) ||
        mbedtls_ssl_setup(&ssl, &config) || mbedtls_ssl_set_hostname(&ssl, host)) goto done;
    mbedtls_ssl_set_bio(&ssl, &net, mbedtls_net_send, mbedtls_net_recv, mbedtls_net_recv_timeout);
    if (mbedtls_ssl_handshake(&ssl) || mbedtls_ssl_get_verify_result(&ssl)) goto done;
    int length = snprintf(request, sizeof(request), "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: CC2-Control-Updater\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n", path, host);
    if (length < 0 || length >= (int)sizeof(request)) goto done;
    for (int sent = 0; sent < length;) {
        int n = mbedtls_ssl_write(&ssl, (unsigned char *)request + sent, (size_t)(length - sent));
        if (n <= 0) goto done;
        sent += n;
    }
    size_t headers = 0;
    int status = 0;
    if (!line(&ssl, text, sizeof(text), &headers) || sscanf(text, "HTTP/1.1 %d", &status) != 1) goto done;
    uint64_t size = 0; int have_size = 0, chunked = 0, location = 0;
    redirect[0] = 0;
    while (line(&ssl, text, sizeof(text), &headers)) {
        if (!text[0]) break;
        char *colon = strchr(text, ':');
        if (!colon || isspace((unsigned char)text[0])) goto done;
        *colon++ = 0; while (*colon == ' ' || *colon == '\t') colon++;
        if (!strcasecmp(text, "Content-Length")) {
            if (have_size++ || !isdigit((unsigned char)*colon)) goto done;
            char *end; size = strtoull(colon, &end, 10);
            if (*end || size > maximum) goto done;
        } else if (!strcasecmp(text, "Transfer-Encoding")) {
            if (chunked++ || strcasecmp(colon, "chunked")) goto done;
        } else if (!strcasecmp(text, "Content-Encoding")) {
            if (strcasecmp(colon, "identity")) goto done;
        } else if (!strcasecmp(text, "Location")) {
            if (location++ || strlen(colon) >= 4196) goto done;
            strcpy(redirect, colon);
        }
    }
    if (text[0] || (chunked && have_size)) goto done;
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
        char next_host[100], next_path[4096];
        ok = location && url(redirect, next_host, next_path) ? 2 : 0; goto done;
    }
    if (status != 200 || location || (!have_size && !chunked)) goto done;
    uint64_t total = 0;
    if (!chunked) ok = copy(&ssl, size, &total, maximum);
    else for (;;) {
        headers = 0;
        if (!line(&ssl, text, sizeof(text), &headers) || !isxdigit((unsigned char)*text)) break;
        char *end; uint64_t count = strtoull(text, &end, 16);
        if (*end || count > maximum - total) break;
        if (!count) {
            headers = 0;
            ok = line(&ssl, text, sizeof(text), &headers) && !text[0]; break;
        }
        if (!copy(&ssl, count, &total, maximum)) break;
        headers = 0;
        if (!line(&ssl, text, sizeof(text), &headers) || text[0]) break;
    }
done:
    mbedtls_ssl_free(&ssl); mbedtls_ssl_config_free(&config); mbedtls_net_free(&net);
    mbedtls_x509_crt_free(&roots); mbedtls_ctr_drbg_free(&random); mbedtls_entropy_free(&entropy);
    return ok;
}

int main(int argc, char **argv) {
    if (argc != 4) return 2;
    char *end; unsigned long maximum = strtoul(argv[3], &end, 10);
    if (*end || maximum < 1 || maximum > 33555540UL || strlen(argv[1]) >= 4196) return 2;
    alarm(180);
    char current[4196], next[4196]; strcpy(current, argv[1]);
    for (unsigned redirects = 0; redirects < 6; redirects++) {
        int result = get(current, argv[2], maximum, next);
        if (result == 1) return fflush(stdout) ? 1 : 0;
        if (result != 2) break;
        strcpy(current, next);
    }
    fputs("HTTPS update download failed: certificate, connection, clock or response rejected\n", stderr);
    return 1;
}
