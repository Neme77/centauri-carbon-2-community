/* A G-code file larger than 2 GiB (FAT32 allows up to 4 GiB - 1) on the 32-bit printer: it can be listed, its
 * footer read and its copy checked against the free space like any other file. Without large file support stat()
 * and the 32-bit ftell() fail with EOVERFLOW above 2 GiB, and such a file could not be started at all. The file is
 * sparse, so the test needs no disk space; on 64-bit hosts it passes either way, it bites on 32-bit ARM (qemu). */
#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

static int failures;
static void expect(int ok, const char *what) { printf("%s: %s\n", ok ? "PASS" : "FAIL", what); if (!ok) failures++; }
static int tiny_statvfs(const char *path, struct statvfs *st) {
    (void)path; memset(st, 0, sizeof(*st)); st->f_frsize = st->f_bsize = 4096; st->f_bavail = st->f_bfree = 1024; return 0;
}
/* A test that waits for something that never comes ends red with a message instead of hanging the suite. */
static void test_too_long(int signal_number) {
    (void)signal_number;
    static const char text[] = "FAIL: the test ran longer than 300 s (an answer that never came?)\n";
    if (write(2, text, sizeof(text) - 1) < 0) {}
    _exit(1);
}

int main(void) {
    signal(SIGALRM, test_too_long); alarm(300);
    char dir[] = "usb-large-XXXXXX"; /* the harnesses share one working directory */
    assert(mkdtemp(dir) && !chdir(dir));
    assert(!mkdir("gcode", 0700) && !mkdir("usb", 0700));
    gcode_internal_root = "gcode"; gcode_usb_root = "usb";
    const unsigned long long size = 2684354560ULL; /* 2.5 GiB */
    int fd = open("usb/big.gcode", O_WRONLY | O_CREAT | O_TRUNC, 0644); assert(fd >= 0);
    static const char head[] = "M140 S55\nG28\n", foot[] = "\n; nozzle_diameter = 0.6\n";
    assert(write(fd, head, sizeof(head) - 1) == (ssize_t)(sizeof(head) - 1));
    assert(lseek(fd, (off_t)(size - (sizeof(foot) - 1)), SEEK_SET) == (off_t)(size - (sizeof(foot) - 1)));
    assert(write(fd, foot, sizeof(foot) - 1) == (ssize_t)(sizeof(foot) - 1));
    assert(!close(fd));

    char path[PATH_MAX_LOCAL * 2]; struct stat info;
    expect(gcode_resolved_path("usb", "big.gcode", path, sizeof(path)) && stat(path, &info) == 0 &&
           (unsigned long long)info.st_size == size, "a 2.5 GiB file can be stat()ed with its full size");
    double bed = -1, nozzle = -1;
    expect(gcode_read_setup("usb", "big.gcode", &bed, &nozzle) == 0 && bed == 55.0 && nozzle == 0.6,
           "its header and its footer (found by seeking near the end) are read");
    upload_statvfs = tiny_statvfs;
    char imported[PATH_MAX_LOCAL];
    expect(gcode_import_usb("big.gcode", imported, NULL, NULL, NULL, 1, NULL, NULL) == GCODE_IMPORT_NO_SPACE,
           "its copy is checked against the free space, not refused as unreadable");
    upload_statvfs = statvfs;
    /* The printer program checks a file with the 32-bit stat() and reports one above 2 GiB - 1 as missing, so it
     * never prints it: the start is refused before any copy, with a reason the page explains. */
    expect(!gcode_size_printable("usb", "big.gcode"), "a 2.5 GiB file is too large for the printer program");
    int pair[2]; assert(!socketpair(AF_UNIX, SOCK_STREAM, 0, pair));
    static const char body[] = "usb\nbig.gcode\n\nA\nsaved\n0\n\n";
    static mqtt_client printer; /* not linked; the space check stays tiny, so no build ever copies 2.5 GiB here */
    setvbuf(stdout, NULL, _IONBF, 0); upload_statvfs = tiny_statvfs;
    int code = gcode_start_run(pair[0], &printer, body, sizeof(body) - 1, NULL, NULL);
    upload_statvfs = statvfs;
    char reply[1024] = {0}; shutdown(pair[0], SHUT_WR); ssize_t got = read(pair[1], reply, sizeof(reply) - 1); (void)got;
    expect(code == 413 && strstr(reply, " 413 ") && strstr(reply, "\"reason\":\"file_too_large_for_printer\""),
           "its start is answered 413 file_too_large_for_printer");
    DIR *internal = opendir("gcode"); int copies = 0; struct dirent *entry;
    while (internal && (entry = readdir(internal))) if (entry->d_name[0] != '.') copies++;
    if (internal) closedir(internal);
    expect(copies == 0, "nothing was copied into internal memory");
    close(pair[0]); close(pair[1]);
    unlink("usb/big.gcode");
    for (int k = 0; k < 2; k++) { /* the edge: 2 GiB - 1 is printable, 2 GiB is not */
        fd = open("usb/edge.gcode", O_WRONLY | O_CREAT | O_TRUNC, 0644); assert(fd >= 0);
        assert(!ftruncate(fd, (off_t)(2147483647LL + k))); assert(!close(fd));
        expect(gcode_size_printable("usb", "edge.gcode") == !k, k ? "exactly 2 GiB is refused" : "2 GiB - 1 is accepted");
    }
    unlink("usb/edge.gcode");
    if (failures) { printf("%d FAIL\n", failures); return 1; }
    puts("PASS USB files over 2 GiB: stat, footer and space check with large file support; start refused before the copy");
    return 0;
}
