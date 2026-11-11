#define CC2_CAVITY_TEST
#include "native_module.c"
#include <assert.h>

static unsigned native_calls;
static void fake_original(void *self,void *argument) {(void)self;(void)argument;native_calls++;}
static void event(void *self,const char *value) {
    struct {const char *data;size_t length;char reserve[16];} string={value,strlen(value),{0}};
    cc2_cavity_state_changed(self,&string);
}
static cavity_identity current(void) {
    char text[256];cavity_identity id;
    assert(cavity_read_at(channel,"state",text)&&cavity_identity_parse(text,&id));
    return id;
}
static void request_minimum(cavity_identity id,unsigned percent) {
    char text[128];int n=snprintf(text,sizeof(text),"CC2CF1 %ld %016llx %u %u\n",id.pid,id.nonce,id.generation,percent);
    assert(cavity_write_at(channel,"request",text,(size_t)n));
}
static void marker(void) {
    int fd=open(CC2_CAVITY_COMPONENT "/enabled",O_WRONLY|O_CREAT|O_EXCL,0600);assert(fd>=0);
    assert(write(fd,CC2_CAVITY_MARKER,sizeof(CC2_CAVITY_MARKER)-1)==(ssize_t)(sizeof(CC2_CAVITY_MARKER)-1));assert(!close(fd));
}
int main(void) {
    assert(!mkdir(CC2_CAVITY_COMPONENT,0700));marker();
    channel=cavity_directory(1);assert(channel>=0);
    original_state=fake_original;process_nonce=0x1122334455667788ULL;native_ready=1;
    char owner[8]={0};
    event(owner,"printing");cavity_identity first=current();assert(first.printing);
    assert(cc2_cavity_minimum(owner,.1)==.1);
    request_minimum(first,60);assert(cc2_cavity_minimum(owner,.1)==.6);
    assert(cc2_cavity_minimum(owner,.8)==.8);
    assert(cc2_cavity_minimum(owner+1,.1)==.1);
    event(owner,"printing");assert(current().generation==first.generation);
    assert(cc2_cavity_minimum(owner,.1)==.6);
    event(owner,"paused");assert(!current().printing&&cc2_cavity_minimum(owner,.1)==.1);
    event(owner,"printing");cavity_identity second=current();assert(second.generation!=first.generation);
    assert(cc2_cavity_minimum(owner,.1)==.1);
    request_minimum(second,60);
    cavity_identity wrong=second;wrong.nonce++;request_minimum(wrong,60);assert(cc2_cavity_minimum(owner,.1)==.1);
    wrong=second;wrong.pid++;request_minimum(wrong,60);assert(cc2_cavity_minimum(owner,.1)==.1);
    wrong=second;wrong.generation++;request_minimum(wrong,60);assert(cc2_cavity_minimum(owner,.1)==.1);
    request_minimum(second,0);assert(cc2_cavity_minimum(owner,.1)==.1);
    request_minimum(second,101);assert(cc2_cavity_minimum(owner,.1)==.1);
    request_minimum(second,60);assert(cc2_cavity_minimum(owner,.1)==.6);
    assert(!fchmodat(channel,"request",0644,0));assert(cc2_cavity_minimum(owner,.1)==.1);
    assert(!fchmodat(channel,"request",0600,0));
    assert(!linkat(channel,"request",channel,"request-link",0));assert(cc2_cavity_minimum(owner,.1)==.1);
    assert(!unlinkat(channel,"request-link",0));assert(!unlinkat(channel,"request",0));
    assert(!symlinkat("/nonexistent",channel,"request"));assert(cc2_cavity_minimum(owner,.1)==.1);
    assert(!unlinkat(channel,"request",0));assert(!mkfifoat(channel,"request",0600));
    assert(cc2_cavity_minimum(owner,.1)==.1);assert(!unlinkat(channel,"request",0));
    request_minimum(second,60);
    assert(!unlink(CC2_CAVITY_COMPONENT "/enabled"));assert(cc2_cavity_minimum(owner,.1)==.1);marker();
    assert(isnan(cc2_cavity_minimum(owner,NAN)));
    assert(cc2_cavity_minimum(owner,1.2)==1.2);
    char canonical[128];double value;
    snprintf(canonical,sizeof(canonical),"CC2CF1 %ld %016llx %u 4294967356\n",second.pid,second.nonce,second.generation);
    assert(!cavity_request_parse(canonical,&second,&value));
    event(owner,"complete");assert(!current().printing&&cc2_cavity_minimum(owner,.1)==.1);
    event(owner,"printing");assert(cc2_cavity_minimum(owner,.1)==.1);
    event(owner,"unexpected");assert(!current().printing);
    assert(native_calls==7);
    unlinkat(channel,"state",0);unlinkat(channel,"request",0);close(channel);
    assert(!rmdir(CC2_CAVITY_CHANNEL));assert(!unlink(CC2_CAVITY_COMPONENT "/enabled"));assert(!rmdir(CC2_CAVITY_COMPONENT));
    puts("PASS: native entry wrapper with dummy events, atomic IPC, native higher speed, new job/pause/end ownership, disabled/foreign/unsafe requests; no vendor executable run.");
    return 0;
}
