#include "policy.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    cc2_cavity_floor s={0};
    assert(cc2_cavity_floor_apply(&s,7,1,1,1,1,.1)==.1);
    assert(cc2_cavity_floor_request(&s,7,1,1,1,60));
    assert(cc2_cavity_floor_apply(&s,7,1,1,1,1,.1)==.6);
    assert(cc2_cavity_floor_apply(&s,7,1,1,1,1,.8)==.8);
    assert(cc2_cavity_floor_apply(&s,7,1,1,1,0,.1)==.1);
    assert(cc2_cavity_floor_request(&s,7,1,1,1,0));
    assert(cc2_cavity_floor_apply(&s,7,1,1,1,1,.1)==.1);
    for(int requested=1;requested<=100;requested++) {
        assert(cc2_cavity_floor_request(&s,7,1,1,1,requested));
        for(int speed=0;speed<=100;speed++) {
            double original=speed/100.0;
            double result=cc2_cavity_floor_apply(&s,7,1,1,1,1,original);
            assert(result>=original&&result>=requested/100.0&&result<=1);
        }
    }
    assert(!cc2_cavity_floor_request(&s,7,1,1,1,NAN));
    assert(!cc2_cavity_floor_request(&s,7,1,1,1,INFINITY));
    assert(!cc2_cavity_floor_request(&s,7,1,1,1,101));
    assert(!cc2_cavity_floor_request(&s,7,1,1,1,-1));
    assert(!cc2_cavity_floor_request(&s,7,1,1,0,50));
    assert(!cc2_cavity_floor_request(&s,7,1,0,1,50));
    assert(cc2_cavity_floor_request(&s,7,1,1,1,50));
    assert(cc2_cavity_floor_apply(&s,7,2,1,1,1,.1)==.1&&!s.enabled);
    assert(cc2_cavity_floor_request(&s,7,2,1,1,50));
    assert(cc2_cavity_floor_apply(&s,8,2,1,1,1,.1)==.1&&!s.enabled);
    assert(cc2_cavity_floor_request(&s,8,2,1,1,50));
    assert(cc2_cavity_floor_apply(&s,8,2,1,0,1,.1)==.1&&!s.enabled);
    assert(cc2_cavity_floor_request(&s,8,2,1,1,50));
    assert(cc2_cavity_floor_apply(&s,8,2,0,1,1,.1)==.1&&!s.enabled);
    assert(cc2_cavity_floor_request(&s,8,2,1,1,50));
    assert(isnan(cc2_cavity_floor_apply(&s,8,2,1,1,1,NAN)));
    puts("PASS: opt-in per-job minimum, automatic release, native increases/hold preserved, invalid and foreign requests refused. Pure prototype only; no native integration or hardware accessed.");
    return 0;
}
