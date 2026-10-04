#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>
#include <pthread.h>

static void *peer(void *arg) {
    int fd = *(int *)arg;
    for (int turn = 0; turn < 3; turn++) {
        char request[512]; size_t used = 0;
        while (!used || request[used-1] != 3) {
            ssize_t n = recv(fd, request + used, sizeof(request)-1-used, 0);
            assert(n > 0); used += (size_t)n;
        }
        request[used] = 0;
        unsigned long id = strtoul(strstr(request, "\"id\":") + 5, NULL, 10);
        if (turn == 2) {
            const char *partial = "{\"id\":";
            assert(send_all(fd, partial, strlen(partial)) == 0);
            close(fd); return NULL;
        }
        const char *notification = "{\"method\":\"notify\"}\003{\"id\":12}\003";
        assert(send_all(fd, notification, strlen(notification)) == 0);
        size_t padding = turn ? 40000 : 0;
        char *response = malloc(padding + 128); assert(response);
        int prefix = snprintf(response, padding + 128, "{\"id\":%lu,\"result\":{\"value\":%d,\"pad\":\"", id, turn);
        memset(response + prefix, 'a', padding);
        memcpy(response + prefix + padding, "\"}}\003", 4);
        size_t length = (size_t)prefix + padding + 4;
        assert(send_all(fd, response, 5) == 0);
        assert(send_all(fd, response+5, length-5) == 0);
        free(response);
    }
    return NULL;
}
int main(void) {
    int sockets[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    object_query_fd = sockets[0];
    assert(fcntl(object_query_fd, F_SETFL, O_NONBLOCK) == 0);
    pthread_t thread; assert(pthread_create(&thread, NULL, peer, &sockets[1]) == 0);
    const char *query = "{\"id\":202,\"method\":\"objects/query\",\"params\":{}}\003";
    for (int turn = 0; turn < 2; turn++) {
        char *result = NULL; size_t length = 0;
        assert(uds_query_json(query, &result, &length) == 0);
        assert(result && length && strstr(result, "\"result\""));
        assert(object_query_fd == sockets[0]);
        if (turn) assert(length > 40000 && object_query_capacity > 16384);
        free(result);
    }
    char *result = NULL; size_t length = 0;
    assert(uds_query_json(query, &result, &length) < 0);
    assert(!result && !length && object_query_fd == -1);
    assert(object_query_retry_ms > monotonic_ms());
    assert(uds_query_json(query, &result, &length) < 0);
    assert(pthread_join(thread, NULL) == 0);
    return 0;
}
