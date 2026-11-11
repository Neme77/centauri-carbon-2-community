/* Private .44 application-transfer core. No device discovery or process control.
 * ACKs confirm the bootloader's replies; they are NOT physical flash readback.
 * Never use this core without the qualified host guard and exclusive UART owner.
 */
#ifndef CC2_CANVAS_APP_TRANSFER_H
#define CC2_CANVAS_APP_TRANSFER_H
#include "probe_link.h"
#include "sha256.h"
#include <signal.h>

#define AP_SIZE 51472u
#define AP_ERASE_SIZE 53248u
#define AP_OFFSET 0x88d4u
#define AP_STOCK_SHA "f1267ee3219dcad73efffa8dc38e30ff0f17c02c0a1674cbead48a99bdff3228"
#define AP_PATCH_SHA "81b14a64745d4a7fb6697630ac7c3c6b5678fc5544af136c218668e3b9de0463"
#define AP_PACKET_MAX 1043u

static inline uint32_t ap_get32(const unsigned char *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static inline void ap_put32(unsigned char *p,uint32_t v) {
    for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));
}
static inline int ap_image(const unsigned char *image,size_t length,int patched) {
    if(!image || length!=AP_SIZE)return 0;
    sha256_state state;char digest[65];sha256_init(&state);
    sha256_update(&state,image,length);sha256_hex(&state,digest);
    if(strcmp(digest,patched?AP_PATCH_SHA:AP_STOCK_SHA))return 0;
    uint32_t stack=ap_get32(image),reset=ap_get32(image+4);
    return stack>=0x20000000 && stack<0x20020000 && !(stack&3) &&
           (reset&1) && (reset&~1u)>=0x08004000 && (reset&~1u)<0x08004000+AP_SIZE;
}
static inline int ap_patch(const unsigned char *stock,size_t length,unsigned char *output) {
    if(!output || output==stock || !ap_image(stock,length,0) ||
       memcmp(stock+AP_OFFSET,"\x41\xf6\xc6\x20",4))return 0;
    memcpy(output,stock,length);memcpy(output+AP_OFFSET,"\x22\xe0\x00\xbf",4);
    return ap_image(output,length,1);
}
/* Only fixed-size application erase, aligned bounded application chunks, ping,
 * and jump are representable. There is no physical-address/bootloader argument.
 */
static inline size_t ap_packet(unsigned char out[AP_PACKET_MAX],unsigned command,
                               const unsigned char *payload,size_t length) {
    int valid=0;
    if(command==0 && payload && length==4)valid=ap_get32(payload)==AP_SIZE;
    if(command==1 && payload && length>=16 && length<=1036) {
        uint32_t off=ap_get32(payload),n=ap_get32(payload+4),total=ap_get32(payload+8);
        valid=total==AP_SIZE && n>0 && n<=1024 && !(off&3) && !(n&3) &&
              off<AP_SIZE && n<=AP_SIZE-off && length==(size_t)n+12;
    }
    if(command==2 && length==0)valid=1;
    if(command==3 && payload && length==4)valid=1;
    if(!out || !valid)return 0;
    out[0]=0xa5;out[1]=0x5a;out[2]=(unsigned char)command;
    out[3]=(unsigned char)(length>>8);out[4]=(unsigned char)length;
    if(length)memcpy(out+5,payload,length);
    uint16_t crc=cv_crc(payload,length);out[5+length]=(unsigned char)(crc>>8);
    out[6+length]=(unsigned char)crc;return length+7;
}
static inline int ap_ack(int fd,unsigned command,unsigned char *reply,size_t length,uint64_t deadline) {
    if(!((command==0 && length==4)||(command==1 && length==8)))return 0;
    unsigned char frame[15];size_t used=0,total=0;
    while(cv_wait(fd,POLLIN,deadline)) {
        /* Read exactly one byte: do not discard any following frame. */
        unsigned char b;ssize_t n=read(fd,&b,1);
        if(n<0 && (errno==EINTR||errno==EAGAIN))continue;
        if(n!=1 || ++total>4096)return 0;
        if(used==0){if(b==0xa5)frame[used++]=b;continue;}
        if(used==1 && b!=0x5a){used=b==0xa5?1:0;continue;}
        frame[used++]=b;
        if(used==5 && (frame[2]!=command || frame[3]!=0 || frame[4]!=length))return 0;
        if(used==length+7) {
            if(cv_crc(frame+5,length)!=((uint16_t)frame[length+5]<<8|frame[length+6]))return 0;
            memcpy(reply,frame+5,length);return 1;
        }
    }
    return 0;
}
static inline uint64_t ap_step_deadline(uint64_t total) {
    uint64_t step=cv_milliseconds()+5000;return step<total?step:total;
}
typedef void (*ap_progress)(unsigned offset,void *context);
/* Caller must durably record a recovery ticket before invoking this function.
 * erase_attempted is set BEFORE sending: a missing ACK may mean erased flash.
 */
static inline int ap_transfer(int fd,const unsigned char *image,int patched,uint64_t deadline,
                             volatile sig_atomic_t *cancel,int *erase_attempted,
                             ap_progress progress,void *context) {
    unsigned char payload[1036],packet[AP_PACKET_MAX],reply[8];
    if(!erase_attempted || !ap_image(image,AP_SIZE,patched) ||
       (cancel && *cancel) || cv_milliseconds()>=deadline)return 0;
    ap_put32(payload,AP_SIZE);size_t size=ap_packet(packet,0,payload,4);
    if(!size)return 0;
    *erase_attempted=1;
    uint64_t step=ap_step_deadline(deadline);
    if(!cv_write(fd,packet,size,step) || !ap_ack(fd,0,reply,4,step) ||
       ap_get32(reply)!=AP_ERASE_SIZE)return 0;
    for(unsigned off=0;off<AP_SIZE;) {
        if((cancel && *cancel) || cv_milliseconds()>=deadline)return 0;
        unsigned n=AP_SIZE-off;if(n>1024)n=1024;
        ap_put32(payload,off);ap_put32(payload+4,n);ap_put32(payload+8,AP_SIZE);
        memcpy(payload+12,image+off,n);size=ap_packet(packet,1,payload,n+12);
        step=ap_step_deadline(deadline);
        if(!size || !cv_write(fd,packet,size,step) || !ap_ack(fd,1,reply,8,step) ||
           ap_get32(reply)!=n || ap_get32(reply+4)!=off+n)return 0;
        off+=n;if(progress)progress(off,context);
    }
    return 1;
}
#endif
