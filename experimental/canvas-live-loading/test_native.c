#define CC2_CANVAS_LIVE_LOADING_TEST
#include "native_module.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int prefeed_fixture(const void *canvas, int slot);
static _Alignas(8) unsigned char canvas[0x400], protocol[0x800];
static const int slot = 2;
static unsigned char *const status = protocol + PROTOCOL_STATUS;

static void put_int(unsigned char *object, unsigned offset, int32_t value) {
    memcpy(object + offset, &value, sizeof(value));
}

static void put_deadline(int tray, double deadline) {
    memcpy(status + PREFEED_DEADLINE + 8 * tray, &deadline, sizeof(deadline));
}

int receive_motor(const void *owner, int tray, const feeder_motor *motor, int wait) {
    assert(owner == protocol && tray == slot && wait == 0);
    assert(motor->distance == 5 && motor->speed == 100 && motor->acceleration == 80);
    return PREFEED_GRIP;
}

int main(void) {
    const void *owner = protocol;
    memcpy(canvas, &owner, sizeof(owner));
    put_int(canvas, BREAK_LIMIT, 10);
    put_int(canvas, SHORT_PREFEED, 5);
    put_int(canvas, MOTOR_SPEED, 100);
    put_int(canvas, MOTOR_ACCEL, 80);
    canvas[PRINT_FEED_READY] = 1;
    status[CONNECTED] = status[HEAD_PRESENT] = status[INLET_PRESENT + 0] = status[INLET_PRESENT + slot] = 1;
    put_int(status, ACTIVE_TRAY, 0);
    put_int(status, TRAY_STATE, 2);
    put_int(status, TRAY_STATE + 4 * slot, 1);
    put_deadline(slot, 1.0e16);
    assert(prefeed_fixture(canvas, slot) == PREFEED_GRIP);

    canvas[PRINT_FEED_READY] = 0;
    assert(prefeed_fixture(canvas, slot) == PREFEED_SKIP);
    canvas[PRINT_FEED_READY] = 1;
    put_int(canvas, FAILED_READS, 1);
    assert(prefeed_fixture(canvas, slot) == PREFEED_SKIP);
    put_int(canvas, FAILED_READS, 0);
    status[CONNECTED] = 0;
    assert(prefeed_fixture(canvas, slot) == PREFEED_SKIP);
    status[CONNECTED] = 1;
    status[HEAD_PRESENT] = 0;
    assert(prefeed_fixture(canvas, slot) == PREFEED_SKIP);
    status[HEAD_PRESENT] = 1;
    put_int(status, ACTIVE_TRAY, slot);
    assert(prefeed_fixture(canvas, slot) == PREFEED_SKIP);
    put_int(status, ACTIVE_TRAY, -1);
    assert(prefeed_fixture(canvas, slot) == PREFEED_SKIP);
    put_int(status, ACTIVE_TRAY, 0);
    assert(prefeed_fixture(canvas, 4) == PREFEED_SKIP);
    status[INLET_PRESENT] = 0;
    assert(prefeed_fixture(canvas, slot) == PREFEED_SKIP);
    status[INLET_PRESENT] = 1;
    status[INLET_PRESENT + slot] = 0;
    assert(prefeed_fixture(canvas, slot) == PREFEED_SKIP);
    status[INLET_PRESENT + slot] = 1;
    put_deadline(slot, 123.0);
    assert(prefeed_fixture(canvas, slot) == PREFEED_SKIP);
    put_deadline(slot, 1.0e16);
    put_int(canvas, SHORT_PREFEED, 6);
    assert(prefeed_fixture(canvas, slot) == PREFEED_SKIP);
    put_int(canvas, SHORT_PREFEED, 5);
    put_int(canvas, BREAK_COUNT, 10);
    assert(prefeed_fixture(canvas, slot) == PREFEED_SKIP);
    put_int(canvas, BREAK_COUNT, 11);
    assert(prefeed_fixture(canvas, slot) == PREFEED_RUNOUT);
    puts("PASS: ARM native branch, bounded standby feeder arguments, settled-print guards and stock runout");
}
