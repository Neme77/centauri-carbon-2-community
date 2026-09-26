#ifndef CC2_PANDA_H
#define CC2_PANDA_H

typedef struct {
    int port;
    int control_port;
    int server_fd;
    int started;
    void *thread;
} panda_server;

int panda_start(panda_server *server, int port, int control_port);
void panda_stop(panda_server *server);

#endif
