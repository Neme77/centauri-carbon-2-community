#define _GNU_SOURCE
/* The start page (limit 1 MiB) and the locale files (limit 256 KiB) are read whole and sent. A file above its limit
 * used to get no answer at all: the connection was closed without a status line. It must get an HTTP 500 with a
 * plain-text reason, one log line per kind of file, and a file at exactly the limit must still be served. */
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#define main cc2_main_original
#include "../src/main.c"
#undef main

/* The handlers log to stderr, which the test redirects to a file; a failed check puts the real stderr back first so the reason is visible. */
static int real_stderr = -1;
#define CHECK(condition) do { if (!(condition)) { if (real_stderr >= 0) dup2(real_stderr, 2); \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); fflush(stdout); abort(); } } while (0)

#define INDEX_LIMIT (1024 * 1024)
#define LOCALE_LIMIT 262144

typedef struct { int fd; char *data; size_t used, cap; } reader;
static void *drain(void *arg) {
    reader *r = arg; ssize_t n;
    while ((n = read(r->fd, r->data + r->used, r->cap - r->used)) > 0) r->used += (size_t)n;
    return NULL;
}

typedef struct { int status; size_t body_len; char *text; } answer;
/* Runs the handler on one end of a socket pair while a thread drains the other end (a 1 MiB answer does not fit a socket buffer). */
static answer ask(const char *root, const char *path) {
    int pair[2]; CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    reader r = { pair[1], malloc(INDEX_LIMIT + 4096), 0, INDEX_LIMIT + 4095 };
    CHECK(r.data);
    pthread_t thread; CHECK(pthread_create(&thread, NULL, drain, &r) == 0);
    if (!strcmp(path, "/")) serve_index(pair[0], root, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    else serve_locale(pair[0], root, path, "GET /i18n/ HTTP/1.1\r\nHost: localhost\r\n\r\n");
    close(pair[0]);
    pthread_join(thread, NULL);
    close(pair[1]);
    r.data[r.used] = 0;
    answer a; memset(&a, 0, sizeof(a));
    a.text = r.data;
    if (r.used > 12 && !strncmp(r.data, "HTTP/1.1 ", 9)) {   /* status 0: nothing was answered */
        a.status = atoi(r.data + 9);
        const char *split = strstr(r.data, "\r\n\r\n");
        CHECK(split);
        a.body_len = r.used - (size_t)(split + 4 - r.data);
    }
    return a;
}
static const char *body_of(const answer *a) { return strstr(a->text, "\r\n\r\n") + 4; }

static void *parallel_ask(void *root) {
    answer a = ask((const char *)root, "/");
    CHECK(a.status == 500);
    free(a.text);
    return NULL;
}

static void write_file(const char *path, size_t size, char fill) {
    FILE *file = fopen(path, "wb"); CHECK(file);
    char block[4096]; memset(block, fill, sizeof(block));
    for (size_t left = size; left;) { size_t n = left < sizeof(block) ? left : sizeof(block); CHECK(fwrite(block, 1, n, file) == n); left -= n; }
    CHECK(fclose(file) == 0);
}

static int log_lines(const char *log_path) {
    FILE *file = fopen(log_path, "r"); CHECK(file);
    char line[512]; int count = 0;
    while (fgets(line, sizeof(line), file)) if (strstr(line, "Web UI:") && strstr(line, "larger than")) count++;
    fclose(file);
    return count;
}

int main(void) {
    char root[] = "/tmp/cc2-web-size-XXXXXX"; CHECK(mkdtemp(root));
    char locales[300], path[340], log_path[340];
    snprintf(locales, sizeof(locales), "%s/locales", root); CHECK(mkdir(locales, 0700) == 0);
    snprintf(log_path, sizeof(log_path), "%s/log.txt", root);
    real_stderr = dup(2); CHECK(real_stderr >= 0);
    int log_fd = open(log_path, O_WRONLY | O_CREAT | O_TRUNC, 0600); CHECK(log_fd >= 0);
    CHECK(dup2(log_fd, 2) == 2);                                /* the handlers log to stderr */
    close(log_fd);
    setvbuf(stdout, NULL, _IONBF, 0);
    snprintf(path, sizeof(path), "%s/index.html", root);

    write_file(path, 400, 'a');
    answer ok = ask(root, "/");
    CHECK(ok.status == 200 && ok.body_len == 400);
    free(ok.text);
    puts("PASS a small start page is served");

    write_file(path, INDEX_LIMIT, 'b');
    answer edge = ask(root, "/");
    CHECK(edge.status == 200 && edge.body_len == INDEX_LIMIT);
    free(edge.text);
    puts("PASS a start page of exactly 1 MiB is served");

    write_file(path, INDEX_LIMIT + 1, 'c');
    answer big = ask(root, "/");
    CHECK(big.status == 500);
    CHECK(strstr(body_of(&big), "web/index.html is larger than the 1 MiB limit"));
    CHECK(strstr(big.text, "Content-Type: text/plain"));
    free(big.text);
    answer again = ask(root, "/");
    CHECK(again.status == 500);
    free(again.text);
    CHECK(log_lines(log_path) == 1);
    puts("PASS a start page one byte over 1 MiB gets HTTP 500 with the reason; two requests write one log line");

    write_file(path, 400, 'a');
    answer back = ask(root, "/");
    CHECK(back.status == 200 && back.body_len == 400);       /* the limit does not stay set once the file is small again */
    free(back.text);
    puts("PASS the start page is served again after the file is small again");

    snprintf(path, sizeof(path), "%s/de.json", locales);
    write_file(path, 300, 'x');
    answer small = ask(root, "/i18n/de.json");
    CHECK(small.status == 200 && small.body_len == 300);
    free(small.text);
    puts("PASS a small locale file is served");

    write_file(path, LOCALE_LIMIT, 'x');
    answer ledge = ask(root, "/i18n/de.json");
    CHECK(ledge.status == 200 && ledge.body_len == LOCALE_LIMIT);
    free(ledge.text);
    puts("PASS a locale file of exactly 256 KiB is served");

    write_file(path, LOCALE_LIMIT + 1, 'x');
    answer lbig = ask(root, "/i18n/de.json");
    CHECK(lbig.status == 500);
    CHECK(strstr(body_of(&lbig), "web/locales/de.json is larger than the 256 KiB limit"));
    free(lbig.text);
    answer lagain = ask(root, "/i18n/de.json");
    CHECK(lagain.status == 500);
    free(lagain.text);
    CHECK(log_lines(log_path) == 2);                         /* one more line for the locale kind, not one per request */
    puts("PASS a locale file one byte over 256 KiB gets HTTP 500 with the reason; two requests write one more log line");

    answer missing = ask(root, "/i18n/fr.json");
    CHECK(missing.status == 404 && !strcmp(body_of(&missing), "{}\n"));
    free(missing.text);
    puts("PASS a missing locale file is still 404 with {}");

    /* An empty start page is still the unchanged 503 (a replaced or cut page), not the 500 of the size guard. */
    snprintf(path, sizeof(path), "%s/index.html", root);
    write_file(path, 0, 'a');
    answer empty = ask(root, "/");
    CHECK(empty.status == 503);
    free(empty.text);
    puts("PASS an empty start page is still 503");

    /* Parallel first requests for an oversized page write one log line, not one per thread. */
    write_file(path, INDEX_LIMIT + 1, 'c');
    atomic_store(&index_too_large_logged, 0);
    int before = log_lines(log_path);
    pthread_t workers[8];
    for (int i = 0; i < 8; i++) CHECK(pthread_create(&workers[i], NULL, parallel_ask, root) == 0);
    for (int i = 0; i < 8; i++) pthread_join(workers[i], NULL);
    CHECK(log_lines(log_path) == before + 1);
    puts("PASS eight parallel requests for an oversized start page write one log line");

    /* cleanup */
    char command[420]; snprintf(command, sizeof(command), "rm -rf %s", root);
    CHECK(system(command) == 0);
    puts("PASS test_web_size_limits");
    return 0;
}
