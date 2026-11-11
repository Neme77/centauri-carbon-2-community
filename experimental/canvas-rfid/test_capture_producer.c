/* Test fixture only: real R2 IPC/capture functions, no vendor program or UART. */
#define RF_NATIVE_TEST
#include "native_module.c"
#include <signal.h>
#include <sys/mman.h>
static volatile sig_atomic_t stop;
static void stopping(int n){(void)n;stop=1;}
static void parse(void *owner,const void *opaque){
    (void)owner;
    struct rf_vector_view view;memcpy(&view,opaque,sizeof(view));
    if(view.end-view.begin!=191)_exit(90);
}
static void state(void *owner,const void *opaque){(void)owner;(void)opaque;}
static void reset(void *owner){
    struct {const char *data;size_t size;} view={"standby",7};
    memcpy((char*)owner+0x6c,&view,sizeof(view));
}
int main(int argc,char **argv){
    if(argc!=3)return 2;
    int fd=open(argv[1],O_RDONLY|O_CLOEXEC);if(fd<0)return 3;
    void *map=mmap(NULL,4096,PROT_READ,MAP_PRIVATE,fd,0);close(fd);if(map==MAP_FAILED)return 4;
    rf_original_parse=parse;rf_original_state=state;rf_original_reset=reset;
    rf_channel=rf_directory();if(rf_channel<0)return 5;
    rf_epoch=123;unsigned char object[256]={0};cc2_rfid_stats_reset(object);
    signal(SIGTERM,stopping);signal(SIGINT,stopping);
    unsigned char absent[191]={0,0x40,0},frame[191]={0,0x40,1};
    for(unsigned i=0;i<8;i++)frame[3+i]=(unsigned char)(0x11+i);
    for(unsigned i=0;i<180;i++)frame[11+i]=(unsigned char)(i*7);
    struct rf_vector_view view={absent,absent+191,absent+191};
    int changed=0,written=0;
    while(!stop){
        FILE *file=fopen(argv[2],"r");char mode[48]={0};
        if(file){if(!fgets(mode,sizeof(mode),file))mode[0]=0;fclose(file);}
        if(!changed && (!strncmp(mode,"unknown",7)||!strncmp(mode,"printing",8))){
            const char *text=!strncmp(mode,"unknown",7)?"unknown":"printing";
            struct {const char *data;size_t size;} value={text,strlen(text)};cc2_rfid_state_changed(object,&value);changed=1;
        }
        if(!changed && !strncmp(mode,"change-state",12) && rf_session.armed){
            struct {const char *data;size_t size;} printing={"printing",8};cc2_rfid_state_changed(object,&printing);changed=1;
        }
        if(strncmp(mode,"no-native-frames",16))cc2_rfid_parse(object,&view);
        if(rf_session.armed && (!strncmp(mode,"foreign",7)||!strncmp(mode,"native",6))){
            if(!strncmp(mode,"native",6))memcpy(frame+75,"\x36\xee\xee\xee",4);
            struct rf_vector_view present={frame,frame+191,frame+191};cc2_rfid_parse(object,&present);
        }
        if(rf_session.armed && !written && (!strncmp(mode,"wrong-token",11)||!strncmp(mode,"invalid-owned",13))){
            struct rf_wire_result result;memset(&result,0,sizeof(result));
            result.header=rf_header(rf_session.token,rf_session.deadline,1);
            memcpy(result.uid,frame+3,8);memcpy(result.tag,frame+11,180);result.kind=RF_FOREIGN;
            if(!strncmp(mode,"wrong-token",11))result.header.token^=1;
            else memset(result.uid,0,8);
            if(!rf_write(rf_channel,"result",&result,sizeof(result)))return 6;
            written=1;
        }
        struct timespec pause={0,20000000};nanosleep(&pause,NULL);
    }
    munmap(map,4096);close(rf_channel);return 0;
}
