#define _POSIX_C_SOURCE 200809L
#include "language_core.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Small, separately versioned preference; no printer or Canvas configuration. */
int nd_language_native(int selected) {
    if (selected < 0 || selected >= ND_LANGUAGES) return -1;
    return selected < ND_NATIVE_LANGUAGES ? selected : 1;
}

static int parse(const char *data, size_t length) {
    if (length != 12 || memcmp(data, "CC2LANG1\n", 9) || data[11] != '\n') return -1;
    if (data[9] < '0' || data[9] > '1' || data[10] < '0' || data[10] > '9') return -1;
    int selected = (data[9]-'0')*10 + data[10]-'0';
    return nd_language_native(selected) >= 0 ? selected : -1;
}

int nd_language_read(struct nd_language *state, const char *path) {
    char data[13];
    struct stat st;
    state->loaded = 1;
    state->selected = -1;
    state->error = 0;
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return 0;
    int valid = !fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_uid == geteuid() &&
                st.st_nlink == 1 && !(st.st_mode & 0022) && st.st_size == 12;
    ssize_t size = valid ? read(fd, data, sizeof(data)) : -1;
    close(fd);
    int selected = size >= 0 ? parse(data, (size_t)size) : -1;
    if (selected < 0) { state->error = 1; return 0; }
    state->selected = selected;
    return 1;
}

int nd_language_save(struct nd_language *state, const char *path, int selected) {
    char temporary[256], data[13];
    if (nd_language_native(selected) < 0 || strlen(path) > 210) return 0;
    int n = snprintf(temporary, sizeof(temporary), "%s.%ld.tmp", path, (long)getpid());
    if (n < 0 || (size_t)n >= sizeof(temporary)) return 0;
    n = snprintf(data, sizeof(data), "CC2LANG1\n%02d\n", selected);
    if (n != 12) return 0;
    int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) { state->error = 1; return 0; }
    int ok = write(fd, data, 12) == 12 && !fsync(fd);
    if (close(fd)) ok = 0;
    if (ok && rename(temporary, path)) ok = 0;
    if (!ok) { unlink(temporary); state->error = 1; return 0; }
    state->loaded = 1;
    state->selected = selected;
    state->error = 0;
    return 1;
}

int nd_language_effective(const struct nd_language *state, int native_language) {
    if (native_language < 0 || native_language >= ND_NATIVE_LANGUAGES) return -1;
    if (state->loaded && nd_language_native(state->selected) == native_language)
        return state->selected;
    return native_language;
}
