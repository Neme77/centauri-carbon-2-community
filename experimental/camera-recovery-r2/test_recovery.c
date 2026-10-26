#include "camera_recovery.c"
#include <assert.h>
#include <sys/wait.h>
#ifdef NDEBUG
#error Assertions are required
#endif
/* Offset by four: ARM field offsets align host mutex/shared_ptr fixtures. */
static _Alignas(16) unsigned char server_storage[0x220];
static unsigned char control_storage[0x80];
static void *fixture_server;
static arm_shared *slot0,*slot1;
static int clients[2],forwarded,start_count,connect_during_start;
static arm_string event;
static arm_shared *assign(arm_shared *destination,arm_shared *source) {
    *destination=*source;memset(source,0,sizeof(*source));return destination;
}
static void accept_native(void *object,arm_shared *destination) {
    assert(object==fixture_server);
    unsigned index=destination==slot0?0:1;
    arm_shared source={&clients[index],(void *)1};
    assert(published(destination,&source)==destination);
    assert(!source.object&&!source.control);
}
static int transmit(void *object,const arm_string *message) {
    int fd;memcpy(&fd,object,sizeof(fd));++forwarded;
    return send(fd,message->data,message->length,MSG_NOSIGNAL)==(ssize_t)message->length;
}
static int broadcast(void *object,const arm_string *message) {
    assert(object==fixture_server);assert(!pthread_mutex_lock(server_mutex(object)));
    int result=0;
    if(slot0->object)result|=broadcast_client(slot0->object,message);
    /* Fixed ARM slots are eight bytes apart; host shared_ptrs use 16. */
    if(sizeof(void *)==4&&slot1->object)result|=broadcast_client(slot1->object,message);
    assert(!pthread_mutex_unlock(server_mutex(object)));return result;
}
static void start_native(void *control) {
    assert(control==control_storage);++start_count;
    assert(!state.ready&&initial_calls==1&&initial_returns==0);
    if(connect_during_start)accepting(fixture_server,slot0);
}
static void expect_frame(int fd) {
    char buf[512];ssize_t n=recv(fd,buf,sizeof(buf),MSG_DONTWAIT);
    assert(n==(ssize_t)cc2_insertion_length&&!memcmp(buf,cc2_insertion,(size_t)n));
}
static void expect_empty(int fd){char c;assert(recv(fd,&c,1,MSG_DONTWAIT)==-1&&(errno==EAGAIN||errno==EWOULDBLOCK));}
static void setup(int p[2]) {
    memset(server_storage,0,sizeof(server_storage));fixture_server=server_storage+4;
    slot0=(arm_shared *)((unsigned char *)fixture_server+0xb4);
    slot1=(arm_shared *)((unsigned char *)fixture_server+0xbc);
    assert(!pthread_mutex_init(server_mutex(fixture_server),NULL));
    memset(&state,0,sizeof(state));__atomic_store_n(&server,NULL,__ATOMIC_SEQ_CST);
    initial_calls=initial_returns=connections=0;forwarded=start_count=connect_during_start=0;
    assert(!socketpair(AF_UNIX,SOCK_STREAM,0,p));clients[0]=p[0];clients[1]=-1;
    memset(control_storage,0,sizeof(control_storage));memcpy(control_storage+0x34,&fixture_server,sizeof(fixture_server));
    native_assign=assign;native_accept=accept_native;native_send=transmit;native_broadcast=broadcast;native_start=start_native;
    event=(arm_string){cc2_insertion,(uint32_t)cc2_insertion_length,{0}};
}
static void finish(int p[2]){close(p[0]);close(p[1]);assert(!pthread_mutex_destroy(server_mutex(fixture_server)));}
static void *startup_thread(void *unused){(void)unused;initial_start(control_storage);return NULL;}
static void *publish_thread(void *unused){(void)unused;accepting(fixture_server,slot0);return NULL;}
int main(void){
    cc2_recovery s={0};cc2_socket_key a={1,42},b={1,43};
    assert(!cc2_recovery_claim(&s,a));cc2_recovery_presence(&s,1);
    assert(cc2_recovery_claim(&s,a)&&!cc2_recovery_claim(&s,a));assert(cc2_recovery_claim(&s,b));
    cc2_recovery_presence(&s,0);assert(!cc2_recovery_claim(&s,b));
    cc2_recovery_presence(&s,1);assert(cc2_recovery_claim(&s,a));
    s.epoch=~0u;cc2_recovery_presence(&s,1);assert(s.epoch==1&&cc2_recovery_claim(&s,a));
    assert(cc2_recovery_is_insertion(cc2_insertion,cc2_insertion_length));
    assert(!cc2_recovery_is_insertion(cc2_insertion,cc2_insertion_length-1));
    int p[2];
    /* Initial startup without a physical insertion event: R1 regression. */
    setup(p);accepting(fixture_server,slot0);expect_empty(p[1]);
    initial_start(control_storage);expect_frame(p[1]);assert(state.ready&&state.sent==1&&start_count==1);
    assert(initial_calls==1&&initial_returns==1&&connections==1);finish(p);
    setup(p);initial_start(control_storage);assert(state.sent==0);accepting(fixture_server,slot0);
    expect_frame(p[1]);assert(state.sent==1);broadcast(fixture_server,&event);expect_empty(p[1]);finish(p);
    setup(p);connect_during_start=1;initial_start(control_storage);expect_frame(p[1]);assert(state.sent==1);finish(p);
    setup(p);accepting(fixture_server,slot0);inserted(fixture_server,&event);expect_frame(p[1]);
    assert(!initial_calls&&state.sent==1);finish(p);
    /* Ordinary replies remain untouched. */
    setup(p);accepting(fixture_server,slot0);arm_string ordinary={"ordinary",8,{0}};
    assert(broadcast(fixture_server,&ordinary));char buf[8];assert(recv(p[1],buf,8,0)==8&&!memcmp(buf,"ordinary",8));finish(p);
    /* Reused fd/object but a new inode receives a new announcement. */
    setup(p);initial_start(control_storage);accepting(fixture_server,slot0);expect_frame(p[1]);
    int old=p[0];close(p[0]);close(p[1]);assert(!socketpair(AF_UNIX,SOCK_STREAM,0,p)&&p[0]==old);
    clients[0]=p[0];accepting(fixture_server,slot0);expect_frame(p[1]);assert(state.sent==2);finish(p);
    setup(p);initial_start(control_storage);close(p[1]);accepting(fixture_server,slot0);
    assert(state.failed==1);accepting(fixture_server,slot0);assert(state.failed==1);
    close(p[0]);assert(!pthread_mutex_destroy(server_mutex(fixture_server)));
    /* Publish/broadcast synchronization, not merely pre-publication ordering. */
    for(unsigned i=0;i<200;i++) {
        setup(p);pthread_t t1,t2;assert(!pthread_create(&t1,NULL,startup_thread,NULL));
        assert(!pthread_create(&t2,NULL,publish_thread,NULL));assert(!pthread_join(t1,NULL));assert(!pthread_join(t2,NULL));
        expect_frame(p[1]);expect_empty(p[1]);assert(state.sent==1);finish(p);
    }
    if(sizeof(void *)==4) {
        int q[2];setup(p);assert(!socketpair(AF_UNIX,SOCK_STREAM,0,q));clients[1]=q[0];
        accepting(fixture_server,slot0);initial_start(control_storage);expect_frame(p[1]);
        accepting(fixture_server,slot1);expect_frame(q[1]);assert(state.sent==2);
        broadcast(fixture_server,&event);expect_empty(p[1]);expect_empty(q[1]);
        close(q[0]);close(q[1]);finish(p);
    }
    for(int mode=0;mode<3;mode++){
        pid_t child=fork();assert(child>=0);
        if(!child){if(mode==0){setenv("CC2_CAMERA_RECOVERY_TRIAL","1",1);activate();}
            else if(mode==1)(void)branch(0x7d098,0x1000001);
            else (void)branch(0x7d098,0x70000000);
            _exit(99);}
        int status;assert(waitpid(child,&status,0)==child);assert(WIFEXITED(status)&&WEXITSTATUS(status)==126);
    }
    puts("PASS: direct startup without insertion, 200 publication races, readiness ordering, target dual slots, inode/FD reuse, ordinary replies and refusal guards");
    return 0;
}
