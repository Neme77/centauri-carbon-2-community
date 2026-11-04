#define CC2_CAVITY_TEST
#include "native_module.c"
#include <assert.h>

void string_bridge_begin(cavity_state_fn original){
    assert(!mkdir(CC2_CAVITY_COMPONENT,0700));
    int fd=open(CC2_CAVITY_COMPONENT "/enabled",O_WRONLY|O_CREAT|O_EXCL,0600);assert(fd>=0);
    assert(write(fd,CC2_CAVITY_MARKER,sizeof(CC2_CAVITY_MARKER)-1)==(ssize_t)(sizeof(CC2_CAVITY_MARKER)-1));close(fd);
    channel=cavity_directory(1);assert(channel>=0);
    original_state=original;native_ready=1;process_nonce=0x1122334455667788ULL;
}
int string_bridge_printing(void){cavity_identity id;return cavity_snapshot(0,&id)&&id.printing;}
int string_bridge_has_state(void){char text[256];return cavity_read_at(channel,"state",text);}
void string_bridge_end(void){
    unlinkat(channel,"state",0);close(channel);
    assert(!rmdir(CC2_CAVITY_CHANNEL));assert(!unlink(CC2_CAVITY_COMPONENT "/enabled"));assert(!rmdir(CC2_CAVITY_COMPONENT));
}
