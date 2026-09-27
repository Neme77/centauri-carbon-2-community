#include <errno.h>
#include <poll.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define DEFAULT_SOCKET "/tmp/elegoo_uds"

typedef struct {
    const char *name;
    const char *request;
} probe_request;

static const probe_request requests[] = {
    {"endpoints", "{\"id\":1,\"method\":\"list_endpoints\",\"params\":{}}\003"},
    {"help",      "{\"id\":2,\"method\":\"gcode/help\",\"params\":{}}\003"},
    {"info",      "{\"id\":3,\"method\":\"info\",\"params\":{}}\003"},
    {"objects",   "{\"id\":4,\"method\":\"objects/list\",\"params\":{}}\003"},
    {"reports",   "{\"id\":5,\"method\":\"gcode/subscribe_report\",\"params\":{}}\003"}
};

static int write_all(int fd, const char *buffer, size_t length) {
    while (length > 0) {
        ssize_t written = write(fd, buffer, length);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        buffer += (size_t)written;
        length -= (size_t)written;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "endpoints";
    const char *socket_path = argc > 2 ? argv[2] : DEFAULT_SOCKET;
    const char *request = NULL;
    struct sockaddr_un address;
    int fd;
    int complete = 0;

    for (size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); ++i) {
        if (strcmp(mode, requests[i].name) == 0) {
            request = requests[i].request;
            break;
        }
    }
    if (!request) {
        fprintf(stderr, "Usage: %s [endpoints|help|info|objects|reports] [socket-path]\n", argv[0]);
        return 2;
    }

    if (strlen(socket_path) >= sizeof(address.sun_path)) {
        fprintf(stderr, "Socket path too long: %s\n", socket_path);
        return 2;
    }

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return 2;
    }

    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, socket_path, strlen(socket_path) + 1);

    if (connect(fd, (struct sockaddr *)&address,
                offsetof(struct sockaddr_un, sun_path) + strlen(socket_path) + 1) < 0) {
        perror("connect");
        close(fd);
        return 2;
    }

    if (write_all(fd, request, strlen(request)) < 0) {
        perror("write");
        close(fd);
        return 2;
    }

    while (!complete) {
        struct pollfd pfd = {fd, POLLIN, 0};
        char buffer[4096];
        int ready = poll(&pfd, 1, 3000);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("poll");
            close(fd);
            return 2;
        }
        if (ready == 0) {
            fprintf(stderr, "Timed out waiting for a response\n");
            close(fd);
            return 3;
        }

        ssize_t count = read(fd, buffer, sizeof(buffer));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("read");
            close(fd);
            return 2;
        }
        if (count == 0) {
            break;
        }

        for (ssize_t i = 0; i < count; ++i) {
            if ((unsigned char)buffer[i] == 0x03) {
                fputc('\n', stdout);
                complete = 1;
                break;
            }
            fputc((unsigned char)buffer[i], stdout);
        }
    }

    close(fd);
    return complete ? 0 : 4;
}
