#include <sys/socket.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
static int writes,fail_command;
static int fake_socket(int domain,int type,int protocol){(void)domain;(void)type;(void)protocol;return 42;}
static int fake_connect(int fd,const struct sockaddr *address,socklen_t length){(void)fd;(void)address;(void)length;return 0;}
static ssize_t fake_send(int fd,const void *data,size_t length,int flags){(void)fd;(void)data;(void)flags;if(++writes==2&&fail_command){errno=EPIPE;return -1;}return (ssize_t)length;}
static int fake_poll(struct pollfd *fds,nfds_t count,int timeout){(void)fds;(void)count;(void)timeout;return 1;}
static ssize_t fake_read(int fd,void *data,size_t length){(void)fd;(void)data;(void)length;return 0;}
static int fake_close(int fd){(void)fd;return 0;}
#define socket fake_socket
#define connect fake_connect
#define send fake_send
#define poll fake_poll
#define read fake_read
#define close fake_close
#include "../src/console.c"
int main(void){
    static console_state state;console_init(&state,"/test");
    strcpy(state.command,"M112");fail_command=1;console_worker(&state);assert(!atomic_load(&state.emergency_sent));
    fail_command=0;writes=0;console_worker(&state);assert(atomic_load(&state.emergency_sent));
    assert(!console_clear(&state));assert(atomic_load(&state.emergency_sent));console_destroy(&state);
    console_init(&state,"/test");writes=0;strcpy(state.command,"HELP");console_worker(&state);assert(!atomic_load(&state.emergency_sent));
    console_destroy(&state);return 0;
}
