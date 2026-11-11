/* Native .44 serial reply: 195 bytes, including four opaque trailing bytes.
 * Fixture reproduces the exact MCU reply assembly proved offline; no vendor
 * process or UART is executed. Native classification and IPC are real code.
 */
#define RF_NATIVE_TEST
#include "native_module.c"
#include <assert.h>
#ifdef NDEBUG
#error Assertions required
#endif
static unsigned char frame[RF_STOCK_FRAME_SIZE];
static struct rf_vector_view input;
static size_t wanted_size;
static int native_kind;
static void parse(void *owner,const void *opaque){
    (void)owner;struct rf_vector_view v;memcpy(&v,opaque,sizeof(v));
    assert((size_t)(v.end-v.begin)==wanted_size);
    if(native_kind){assert(opaque==&input && !memcmp(v.begin,frame,wanted_size));}
    else{
        assert(v.begin[2]==0 && frame[2]==1);
        assert(!memcmp(v.begin,frame,2));
        assert(!memcmp(v.begin+3,frame+3,wanted_size-3));
    }
}
static int read_original(void *owner,int wait){assert(wait==1);cc2_rfid_parse(owner,&input);return 1;}
static void state_original(void *owner,const void *value){(void)owner;(void)value;}
int main(void){
    rf_original_parse=parse;rf_original_read=read_original;rf_original_state=state_original;
    rf_channel=rf_directory();assert(rf_channel>=0);rf_epoch=101;
    struct {const char *data;size_t length;} state={"standby",7};
    cc2_rfid_state_changed(frame,&state);assert(rf_phase==1);
    for(unsigned mode=0;mode<4;mode++){
        wanted_size=mode&1?RF_STOCK_FRAME_SIZE:RF_FRAME_SIZE;native_kind=mode>=2;
        memset(frame,0,sizeof(frame));frame[1]=0x40;frame[2]=1;
        for(unsigned i=0;i<8;i++)frame[3+i]=(unsigned char)(i+1);
        for(unsigned i=0;i<180;i++)frame[11+i]=(unsigned char)(i*7);
        memcpy(frame+191,"\xaa\xbb\xcc\xdd",4);
        if(native_kind)memcpy(frame+75,"\x36\xee\xee\xee",4);
        input.begin=frame;input.end=frame+wanted_size;input.capacity=frame+sizeof(frame);
        assert(rf_classify(frame,wanted_size)==(native_kind?RF_ELEGOO:RF_FOREIGN));
        struct rf_wire request=rf_header(mode+1,rf_now()+30,1);
        assert(rf_write(rf_channel,"request",&request,sizeof(request)));rf_last_poll=0;
        assert(cc2_rfid_read(frame,1)==native_kind);
        struct rf_wire_result result;assert(rf_read(rf_channel,"result",&result,sizeof(result)));
        assert(result.header.token==mode+1 && result.kind==(uint32_t)(native_kind?RF_ELEGOO:RF_FOREIGN));
        assert(!memcmp(result.uid,frame+3,8) && !memcmp(result.tag,frame+11,180));
        assert(result.tag[179]==frame[190]); /* no opaque trailer in tag data */
    }
    for(unsigned size=192;size<=200;size++)if(size!=195)assert(rf_classify(frame,size)==RF_MALFORMED);
    frame[2]=2;assert(rf_classify(frame,195)==RF_MALFORMED);
    frame[2]=1;memset(frame+3,0,8);assert(rf_classify(frame,195)==RF_MALFORMED);
    frame[3]=1;frame[1]=0x41;assert(rf_classify(frame,195)==RF_MALFORMED);
    unlinkat(rf_channel,"ready",0);unlinkat(rf_channel,"request",0);unlinkat(rf_channel,"result",0);
    close(rf_channel);assert(!rmdir(RF_CHANNEL));
    puts("PASS: actual 195-byte stock shape and 191-byte legacy shape; exact UID/180 bytes, untouched native vectors, foreign suppression, opaque trailer preservation and strict malformed refusal.");
}
