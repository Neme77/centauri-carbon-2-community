#ifndef CC2_PANDA_H
#define CC2_PANDA_H

#include <stdatomic.h>

typedef struct {
    int port;
    int control_port;
    const char *gcode_root; /* internal G-code folder, for server.files.metadata */
    int server_fd;
    atomic_int started; /* read by the server thread, cleared by panda_stop() */
    void *thread;
} panda_server;

int panda_start(panda_server *server, int port, int control_port, const char *gcode_root);
void panda_stop(panda_server *server);

#endif
