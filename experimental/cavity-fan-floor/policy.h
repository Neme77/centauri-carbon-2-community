#ifndef CC2_CAVITY_FLOOR_POLICY_H
#define CC2_CAVITY_FLOOR_POLICY_H
#include <math.h>
#include <stdint.h>

/* Pure policy prototype. No printer memory access, hooks, I/O or commands.
 * The native integration must supply a verified per-process nonce and a new
 * generation for every print-state transition, independently of web polling. */
typedef struct {
    uint64_t process_nonce, generation;
    double minimum;
    int enabled;
} cc2_cavity_floor;

static inline void cc2_cavity_floor_clear(cc2_cavity_floor *s) {
    *s=(cc2_cavity_floor){0,0,0,0};
}

static inline int cc2_cavity_floor_request(cc2_cavity_floor *s,
        uint64_t nonce,uint64_t generation,int verified,int printing,double percent) {
    if(!isfinite(percent)||percent<0||percent>100)return 0;
    if(percent==0) {cc2_cavity_floor_clear(s);return 1;}
    if(!verified||!printing||!nonce||!generation)return 0;
    *s=(cc2_cavity_floor){nonce,generation,percent/100.0,1};
    return 1;
}

static inline double cc2_cavity_floor_apply(cc2_cavity_floor *s,
        uint64_t nonce,uint64_t generation,int verified,int printing,
        int native_cooling_mode,double native_speed) {
    /* Loss of the job/native capability discards ownership, rather than
     * reviving an old request on a later job or after a process restart. */
    if(!verified||!printing||!nonce||!generation||nonce!=s->process_nonce||generation!=s->generation) {
        cc2_cavity_floor_clear(s);return native_speed;
    }
    /* Preserve unknown values and native chamber-heating/hold ownership. */
    if(!s->enabled||!native_cooling_mode||!isfinite(native_speed)||native_speed<0||native_speed>1)
        return native_speed;
    return fmax(native_speed,s->minimum);
}
#endif
