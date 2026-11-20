#define _GNU_SOURCE
/* The start page's validator belongs to the page content, not to its file metadata.
 *
 * Switching between installed releases or firmware images can give different index.html files the same size, modification
 * time, change time, inode and device (an image keeps no change time of its own and numbers inodes by position). A real file
 * system will not produce that on demand, so the test forces it: fstat() inside src/main.c is replaced by a version that
 * reports one fixed identity for every file and keeps only the real size. Update -> rollback with different pages of the same
 * length: after each switch the previous validator must get 200 and the right page. */
#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
static int collide_metadata = 0;
static int forced_fstat(int fd, struct stat *info) {
    int result = fstat(fd, info);
    if (result == 0 && collide_metadata) {
        info->st_dev = 7; info->st_ino = 4242;
        info->st_mtim.tv_sec = 1700000000; info->st_mtim.tv_nsec = 0;
        info->st_ctim = info->st_mtim;
    }
    return result;
}
#define fstat forced_fstat
#define main cc2_main_original
#include "../src/main.c"
#undef main
#undef fstat

typedef struct { int status; char etag[100]; char body[4096]; size_t body_len; } answer;

static answer fetch(const char *root, const char *validator) {
    int pair[2];
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    char request[600];
    if (validator) snprintf(request, sizeof(request), "GET / HTTP/1.1\r\nHost: localhost\r\nIf-None-Match: %s\r\n\r\n", validator);
    else snprintf(request, sizeof(request), "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    serve_index(pair[0], root, request);
    close(pair[0]);
    char text[8192]; size_t used = 0; ssize_t n;
    while (used < sizeof(text) - 1 && (n = read(pair[1], text + used, sizeof(text) - 1 - used)) > 0) used += (size_t)n;
    close(pair[1]);
    text[used] = 0;
    answer result; memset(&result, 0, sizeof(result));
    assert(used > 12 && !strncmp(text, "HTTP/1.1 ", 9));
    result.status = atoi(text + 9);
    const char *tag = strstr(text, "\r\nETag: ");
    if (tag) { tag += 8; const char *end = strstr(tag, "\r\n"); assert(end && (size_t)(end - tag) < sizeof(result.etag)); memcpy(result.etag, tag, (size_t)(end - tag)); }
    const char *split = strstr(text, "\r\n\r\n");
    assert(split);
    result.body_len = used - (size_t)(split + 4 - text);
    memcpy(result.body, split + 4, result.body_len);
    return result;
}

static void install_page(const char *root, char letter) {
    char path[300]; snprintf(path, sizeof(path), "%s/index.html", root);
    char page[200]; memset(page, letter, sizeof(page)); memcpy(page, "<html>", 6);
    FILE *file = fopen(path, "wb"); assert(file);     /* truncated and rewritten in place: the same inode */
    assert(fwrite(page, 1, sizeof(page), file) == sizeof(page));
    assert(fclose(file) == 0);
}
static int is_page(const answer *a, char letter) {
    char page[200]; memset(page, letter, sizeof(page)); memcpy(page, "<html>", 6);
    return a->body_len == sizeof(page) && !memcmp(a->body, page, sizeof(page));
}

int main(void) {
    char root[] = "/tmp/cc2-index-validator-XXXXXX";
    assert(mkdtemp(root));
    collide_metadata = 1;

    install_page(root, 'A');                                   /* release 1 */
    answer first = fetch(root, NULL);
    assert(first.status == 200 && is_page(&first, 'A') && first.etag[0] == '"');
    answer same = fetch(root, first.etag);
    assert(same.status == 304 && same.body_len == 0 && !strcmp(same.etag, first.etag));
    puts("PASS the same page answers 304 to its validator");

    install_page(root, 'B');                                   /* update: other content, same length, same metadata */
    answer updated = fetch(root, first.etag);
    assert(updated.status == 200 && is_page(&updated, 'B') && strcmp(updated.etag, first.etag) != 0);
    puts("PASS update: the previous validator gets 200 and the new page although size, times, inode and device are equal");

    install_page(root, 'A');                                   /* rollback to release 1 */
    answer rolled = fetch(root, updated.etag);
    assert(rolled.status == 200 && is_page(&rolled, 'A') && !strcmp(rolled.etag, first.etag));
    puts("PASS rollback: the previous validator gets 200 and the old page; the old page has its old validator again");

    install_page(root, 'C');                                   /* a third release */
    answer third = fetch(root, rolled.etag);
    assert(third.status == 200 && is_page(&third, 'C') && strcmp(third.etag, first.etag) && strcmp(third.etag, updated.etag));
    answer back = fetch(root, updated.etag);
    assert(back.status == 200 && is_page(&back, 'C'));
    puts("PASS a third page: every earlier validator gets 200");

    assert(strlen(third.etag) == 66);                          /* "<64 hex digits of the SHA-256 of the page>" */
    answer star = fetch(root, "*");
    assert(star.status == 304);
    puts("PASS * still matches");

    collide_metadata = 0;
    char path[300]; snprintf(path, sizeof(path), "%s/index.html", root); unlink(path); rmdir(root);
    return 0;
}
