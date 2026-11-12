#define _GNU_SOURCE
#include "app_transfer.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/wait.h>
#ifdef NDEBUG
#error Assertions must remain enabled
#endif

static int exact(int fd,unsigned char *p,size_t n) {
    while(n){ssize_t got=read(fd,p,n);if(got<0 && errno==EINTR)continue;
        if(got<=0)return 0;
        p+=got;n-=(size_t)got;}
    return 1;
}
static void send_reply(int fd,unsigned cmd,unsigned char *data,size_t n,int mode) {
    unsigned char packet[15]={0xa5,0x5a,0,0,0};packet[2]=(unsigned char)cmd;packet[4]=(unsigned char)n;
    memcpy(packet+5,data,n);uint16_t crc=cv_crc(data,n);
    packet[n+5]=(unsigned char)(crc>>8);packet[n+6]=(unsigned char)crc;
    if(mode==4)packet[n+6]^=1;
    if(mode==7)packet[4]=0xff;
    if(mode==1){const unsigned char noise[]={3,0xa5,0xa5};assert(write(fd,noise,3)==3);}
    for(size_t i=0;i<n+7;i++) {
        assert(write(fd,packet+i,1)==1);
        if(mode==1){struct timespec pause={0,100000};nanosleep(&pause,NULL);}
    }
}
static void fake_canvas(int fd,unsigned mode) {
    alarm(15);
    unsigned char memory[0x11000],boot[0x4000];
    memset(memory,0x69,sizeof(memory));memcpy(boot,memory,sizeof(boot));
    unsigned erases=0,blocks=0;int injected=0;
    for(;;) {
        unsigned char header[5],payload[1036],reply[8],crc[2];
        assert(exact(fd,header,5));assert(header[0]==0xa5 && header[1]==0x5a);
        size_t n=(size_t)header[3]*256+header[4];assert(n<=sizeof(payload));
        assert(exact(fd,payload,n) && exact(fd,crc,2));
        assert(cv_crc(payload,n)==((uint16_t)crc[0]<<8|crc[1]));
        unsigned cmd=header[2];
        if(cmd==3){assert(n==4);send_reply(fd,3,payload,n,0);continue;}
        if(cmd==2) {
            assert(n==0 && blocks==51);
            assert(ap_image(memory+0x4000,AP_SIZE,mode<=1));
            assert(!memcmp(memory,boot,sizeof(boot)));
            assert(erases==(mode<=1?1u:2u));_exit(0);
        }
        int fault=0;
        if(cmd==0) {
            assert(n==4 && ap_get32(payload)==AP_SIZE);
            memset(memory+0x4000,0xff,AP_ERASE_SIZE);erases++;blocks=0;
            ap_put32(reply,AP_ERASE_SIZE);
            if(!injected && (mode==5 || mode==6)){injected=1;fault=(int)mode;}
            if(fault==5)ap_put32(reply,AP_ERASE_SIZE+2048);
            if(fault==6){struct timespec pause={0,250000000};nanosleep(&pause,NULL);continue;}
            send_reply(fd,0,reply,4,mode==1?1:0);continue;
        }
        assert(cmd==1 && n>=16);
        unsigned off=ap_get32(payload),len=ap_get32(payload+4);
        assert(ap_get32(payload+8)==AP_SIZE && off==blocks*1024 &&
               len>0 && len<=1024 && !(len&3) && off+len<=AP_SIZE && n==len+12);
        memcpy(memory+0x4000+off,payload+12,len);blocks++;
        ap_put32(reply,len);ap_put32(reply+4,off+len);
        if(!injected && blocks==10 && mode>=2 && mode<=7 && mode!=5 && mode!=6) {
            injected=1;fault=(int)mode;
            if(fault==2)ap_put32(reply,len+4);
            if(fault==3)ap_put32(reply+4,off+len-4);
        }
        send_reply(fd,1,reply,8,fault?fault:mode==1?1:0);
    }
}
static void cancel_after_chunk(unsigned off,void *context) {
    if(off>=10240)*(volatile sig_atomic_t*)context=1;
}
static void packet_bounds(void) {
    unsigned char packet[AP_PACKET_MAX],payload[1036]={0};
    for(unsigned cmd=4;cmd<256;cmd++)assert(!ap_packet(packet,cmd,payload,4));
    for(unsigned size=0;size<AP_SIZE+4096;size+=4) {
        ap_put32(payload,size);assert((ap_packet(packet,0,payload,4)!=0)==(size==AP_SIZE));
    }
    for(unsigned off=0;off<AP_SIZE+4096;off+=1) {
        ap_put32(payload,off);ap_put32(payload+4,4);ap_put32(payload+8,AP_SIZE);
        int valid=!(off&3) && off<=AP_SIZE-4;
        assert((ap_packet(packet,1,payload,16)!=0)==valid);
    }
    ap_put32(payload,0);ap_put32(payload+4,1024);ap_put32(payload+8,AP_SIZE);
    assert(ap_packet(packet,1,payload,1036)==AP_PACKET_MAX);
    ap_put32(payload,UINT32_MAX-3);assert(!ap_packet(packet,1,payload,1036));
    ap_put32(payload,0);ap_put32(payload+4,UINT32_MAX);assert(!ap_packet(packet,1,payload,1036));
    assert(!ap_packet(packet,1,payload,SIZE_MAX));
    assert(!ap_packet(packet,0,NULL,4));assert(!ap_packet(NULL,2,NULL,0));
}
int main(int argc,char **argv) {
    assert(argc==2);signal(SIGPIPE,SIG_IGN);packet_bounds();
    unsigned char stock[AP_SIZE],patched[AP_SIZE];char digest[65];
    int input=open(argv[1],O_RDONLY|O_CLOEXEC);assert(input>=0);
    assert(sha256_fd(input,digest) && !strcmp(digest,AP_STOCK_SHA));
    assert(lseek(input,0,SEEK_SET)==0 && exact(input,stock,sizeof(stock)));close(input);
    assert(ap_patch(stock,sizeof(stock),patched));
    assert(!memcmp(stock,patched,AP_OFFSET) && !memcmp(stock+AP_OFFSET+4,patched+AP_OFFSET+4,AP_SIZE-AP_OFFSET-4));
    assert(!ap_image(stock,AP_SIZE-1,0) && !ap_patch(patched,AP_SIZE,stock));
    patched[32]^=1;assert(!ap_image(patched,AP_SIZE,1));patched[32]^=1;
    volatile sig_atomic_t cancel=1;int attempted=0;
    assert(!ap_transfer(-1,patched,1,cv_milliseconds()+1000,&cancel,&attempted,NULL,NULL) && !attempted);
    for(unsigned mode=0;mode<9;mode++) {
        int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
        pid_t child=fork();assert(child>=0);
        if(!child){close(pair[0]);fake_canvas(pair[1],mode);_exit(9);}
        close(pair[1]);int flags=fcntl(pair[0],F_GETFL);
        assert(flags>=0 && !fcntl(pair[0],F_SETFL,flags|O_NONBLOCK));
        attempted=0;cancel=0;uint64_t start=cv_milliseconds();
        int installed=ap_transfer(pair[0],patched,1,start+(mode==6?100:5000),&cancel,&attempted,
                                  mode==8?cancel_after_chunk:NULL,(void*)&cancel);
        assert(installed==(mode<=1) && attempted);
        if(!installed) {
            /* Recovery resynchronization barrier: fresh echoed challenges,
             * followed by a new application-only erase and original transfer.
             * Production recovery orchestration is not implemented here. */
            for(unsigned i=0;i<3;i++) {
                unsigned char challenge[4]={0x73,0x1a,0xbd,(unsigned char)(i+mode*3)};
                assert(cv_ping(pair[0],challenge,1000));
            }
            assert(ap_transfer(pair[0],stock,0,cv_milliseconds()+5000,NULL,&attempted,NULL,NULL));
        }
        unsigned char jump[AP_PACKET_MAX];size_t n=ap_packet(jump,2,NULL,0);
        assert(n==7 && cv_write(pair[0],jump,n,cv_milliseconds()+1000));close(pair[0]);
        int status;while(waitpid(child,&status,0)<0)assert(errno==EINTR);
        assert(WIFEXITED(status) && WEXITSTATUS(status)==0);
        printf("PASS: mode %u — %s; application bounds and unchanged simulated bootloader verified\n",mode,installed?"modified transfer acknowledged":"original restored in simulator");
    }
    puts("PASS: exact pinned images, four-byte patch, bounded packets, complete ACK length/end-offset checks, fragmented replies, bad CRC/header/erase size, lost erase ACK and cancellation.");
    puts("OFFLINE ONLY: socket simulator; no production device worker, durable recovery orchestration or physical readback.");
}
