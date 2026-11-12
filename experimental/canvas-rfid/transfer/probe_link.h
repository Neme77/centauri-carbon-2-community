#ifndef CC2_CANVAS_PROBE_LINK_H
#define CC2_CANVAS_PROBE_LINK_H
#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static inline uint64_t cv_milliseconds(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC,&ts) || ts.tv_sec<0) return 0;
    return (uint64_t)ts.tv_sec*1000u+(uint64_t)ts.tv_nsec/1000000u;
}
static inline uint16_t cv_crc(const unsigned char *data,size_t length) {
    uint16_t crc=0xffff;
    for(size_t i=0;i<length;i++) {
        crc^=data[i];
        for(unsigned bit=0;bit<8;bit++) crc=(uint16_t)((crc>>1)^((crc&1)?0x8408:0));
    }
    return crc;
}
/* This executable is structurally incapable of constructing erase/program
 * packets: its only binary commands are PING (3) and application jump (2). */
static inline size_t cv_packet(unsigned char out[11],unsigned command,const unsigned char *payload,size_t length) {
    if(!((command==3 && length==4 && payload) || (command==2 && length==0))) return 0;
    out[0]=0xa5;out[1]=0x5a;out[2]=(unsigned char)command;out[3]=0;out[4]=(unsigned char)length;
    if(length) memcpy(out+5,payload,length);
    uint16_t crc=cv_crc(payload,length);
    out[5+length]=(unsigned char)(crc>>8);out[6+length]=(unsigned char)crc;
    return length+7;
}
static inline int cv_wait(int fd,short events,uint64_t deadline) {
    for(;;) {
        uint64_t now=cv_milliseconds();
        if(!now || now>=deadline) return 0;
        uint64_t remaining=deadline-now;
        int timeout=(int)(remaining>1000?1000:remaining);
        struct pollfd p={fd,events,0};
        int n=poll(&p,1,timeout);
        if(n<0 && errno==EINTR) continue;
        if(n<0 || (n>0 && !(p.revents&events))) return 0;
        if(n>0) return 1;
    }
}
static inline int cv_write(int fd,const unsigned char *bytes,size_t length,uint64_t deadline) {
    size_t used=0;
    while(used<length) {
        if(!cv_wait(fd,POLLOUT,deadline)) return 0;
        ssize_t n=write(fd,bytes+used,length-used);
        if(n<0 && (errno==EINTR || errno==EAGAIN)) continue;
        if(n<=0) return 0;
        used+=(size_t)n;
    }
    return 1;
}
static inline int cv_ping(int fd,const unsigned char challenge[4],unsigned timeout_ms) {
    unsigned char request[11],frame[11];size_t used=0,total=0;
    uint64_t now=cv_milliseconds();
    if(!now || !timeout_ms || timeout_ms>2000) return 0;
    uint64_t deadline=now+timeout_ms;
    if(!cv_write(fd,request,cv_packet(request,3,challenge,4),deadline)) return 0;
    while(cv_wait(fd,POLLIN,deadline)) {
        unsigned char input[128];ssize_t count=read(fd,input,sizeof(input));
        if(count<0 && (errno==EINTR || errno==EAGAIN)) continue;
        if(count<=0) return 0;
        total+=(size_t)count;if(total>32768) return 0;
        for(ssize_t i=0;i<count;i++) {
            unsigned char b=input[i];
            if(used==0) {if(b==0xa5)frame[used++]=b;continue;}
            if(used==1 && b!=0x5a) {used=b==0xa5?1:0;continue;}
            frame[used++]=b;
            if(used==5 && (frame[2]!=3 || frame[3]!=0 || frame[4]!=4)) {used=b==0xa5?1:0;continue;}
            if(used==sizeof(frame)) {
                uint16_t crc=(uint16_t)((uint16_t)frame[9]<<8|frame[10]);
                if(crc==cv_crc(frame+5,4) && !memcmp(frame+5,challenge,4)) return 1;
                used=b==0xa5?1:0;
            }
        }
    }
    return 0;
}
#endif
