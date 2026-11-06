#define RF_NATIVE_TEST
#include "native_module.c"
#include <assert.h>
#ifdef NDEBUG
#error Assertions must remain enabled
#endif
static unsigned char frame[RF_FRAME_SIZE] = {0,0x40,1,7};
static struct rf_vector_view vector;
static int scenario, parsed_present, reads;
static int reset_nested;
static void phase(const char *text);
static void original_reset(void *owner) {
    if(owner) {
        struct {const char *text;size_t length;} state={"standby",7};
        memcpy((char*)owner+0x6c,&state,sizeof(state));
    }
    if(reset_nested)phase("printing");
}
static void original_parse(void *owner, const void *opaque) {
    struct rf_vector_view v;
    (void)owner; memcpy(&v, opaque, sizeof(v));
    assert(v.end - v.begin == RF_FRAME_SIZE);
    parsed_present = v.begin[2];
}
static void original_state(void *owner, const void *string) {(void)owner; (void)string;}
static int original_read(void *owner, int wait) {
    (void)wait; reads++;
    if (scenario == 1) return 1; /* ACK without a frame */
    if (scenario == 2) assert(!cc2_rfid_read(owner, 1));
    cc2_rfid_parse(scenario == 3 ? &vector : owner, &vector);
    if (scenario == 4) cc2_rfid_parse(owner, &vector);
    if (scenario == 5) {pthread_mutex_lock(&rf_lock); rf_invalidate(); pthread_mutex_unlock(&rf_lock);}
    return scenario != 6;
}
static void phase(const char *text) {
    struct {const char *text; size_t length;} string = {text, strlen(text)};
    cc2_rfid_state_changed(&frame, &string);
}
static void request(uint64_t token, unsigned action) {
    struct rf_wire h = rf_header(token, rf_now() + 30, action);
    assert(rf_write(rf_channel, "request", &h, sizeof(h)));
    rf_last_poll = 0;
}
int main(void) {
    vector.begin = frame; vector.end = frame + sizeof(frame); vector.capacity = vector.end;
    rf_original_parse = original_parse; rf_original_state = original_state; rf_original_read = original_read;
    rf_original_reset=original_reset;
    rf_channel = rf_directory(); assert(rf_channel >= 0); rf_epoch = 100;
    unsigned char stats[256]={0};
    assert(rf_phase==0);
    cc2_rfid_stats_reset(stats);assert(rf_phase==1 && !rf_capture_broken);
    reset_nested=1;cc2_rfid_stats_reset(stats);assert(rf_phase==2);
    reset_nested=0;cc2_rfid_stats_reset(NULL);assert(rf_phase==0);
    phase("standby"); assert(rf_phase == 1 && !rf_capture_broken);
    memcpy(frame + 75, "\x36\xee\xee\xee", 4);
    assert(cc2_rfid_read(frame, 1) && parsed_present == 1);
    scenario = 1; assert(!cc2_rfid_read(frame, 1));
    for (scenario = 2; scenario <= 6; scenario++) {
        int accepted = cc2_rfid_read(frame, 1);
        assert(accepted == (scenario == 2));
    }
    scenario = 0;
    assert(!cc2_rfid_read(frame, 0));
    frame[75] = 0; /* Foreign data must never make the native caller reuse old metadata. */
    assert(!cc2_rfid_read(frame, 1) && parsed_present == 0 && frame[2] == 1);
    request(123, 1);
    assert(!cc2_rfid_read(frame, 1));
    struct rf_wire_result result;
    assert(rf_read(rf_channel, "result", &result, sizeof(result)));
    assert(result.kind == RF_FOREIGN && result.header.token == 123);
    assert(!memcmp(result.tag, frame + 11, RF_TAG_SIZE));
    frame[11] = 99; cc2_rfid_parse(frame, &vector);
    assert(rf_read(rf_channel, "result", &result, sizeof(result)) && result.tag[0] != 99);
    request(124, 2); cc2_rfid_parse(frame, &vector);
    assert(!rf_session.token && faccessat(rf_channel, "result", F_OK, 0));
    request(123, 1); cc2_rfid_parse(frame, &vector); assert(!rf_session.token); /* replay */
    request(125, 1); phase("printing"); assert(rf_phase == 2 && !rf_session.token);
    cc2_rfid_parse(frame, &vector); assert(!rf_session.token);
    phase("paused"); assert(rf_phase == 2);
    phase("unknown"); assert(rf_phase == 0);
    phase("cancelled"); assert(rf_phase == 1);
    struct rf_wire h = rf_header(126, rf_now() + 30, 1);
    h.nonce--; assert(rf_write(rf_channel, "request", &h, sizeof(h)));
    rf_last_poll = 0; cc2_rfid_parse(frame, &vector); assert(!rf_session.token);
    h = rf_header(127, rf_now(), 1);
    assert(rf_write(rf_channel, "request", &h, sizeof(h)));
    rf_last_poll = 0; cc2_rfid_parse(frame, &vector); assert(!rf_session.token);
    request(128,1);cc2_rfid_parse(frame,&vector);
    assert(rf_read(rf_channel,"result",&result,sizeof(result)));
    cc2_rfid_stats_reset(stats);
    assert(rf_phase==1 && !rf_session.token && faccessat(rf_channel,"result",F_OK,0));
    /* Host IPC rejects over-permissive files, links, oversized input and nonfiles. */
    assert(rf_write(rf_channel, "probe", &h, sizeof(h)));
    assert(!fchmodat(rf_channel, "probe", 0644, 0)); assert(!rf_read(rf_channel, "probe", &h, sizeof(h)));
    assert(!fchmodat(rf_channel, "probe", 0600, 0));
    assert(!linkat(rf_channel, "probe", rf_channel, "hard", 0));
    assert(!rf_read(rf_channel, "probe", &h, sizeof(h)) && !rf_write(rf_channel, "probe", &h, sizeof(h)));
    assert(!unlinkat(rf_channel, "hard", 0));
    assert(!symlinkat("probe", rf_channel, "symbolic"));
    assert(!rf_read(rf_channel, "symbolic", &h, sizeof(h)) && !rf_write(rf_channel, "symbolic", &h, sizeof(h)));
    assert(!rf_read(rf_channel, "probe", &h, sizeof(h) - 1));
    assert(!mkfifoat(rf_channel, "fifo", 0600)); assert(!rf_read(rf_channel, "fifo", &h, sizeof(h)));
    for (unsigned i = 0; i < 7; i++) {
        const char *names[] = {"probe","symbolic","fifo","ready","request","result","hard"};
        unlinkat(rf_channel, names[i], 0);
    }
    close(rf_channel); assert(!rmdir(RF_CHANNEL));
    assert(reads == 10);
    puts("PASS: native reply ownership, ACK-only stale-metadata refusal, reentrant reads, one-shot raw capture, state invalidation and protected IPC.");
}
