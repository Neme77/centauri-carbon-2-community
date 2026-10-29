#include "status_core.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
int main(void) {
    char out[64];int n;
    const char status[]="ssid=A quoted \\\"network\\\"\nwpa_state=COMPLETED\nfreq=5220\n";
    assert(ns_field(status,sizeof(status)-1,"wpa_state",out,sizeof(out)) && !strcmp(out,"COMPLETED"));
    assert(ns_field(status,sizeof(status)-1,"ssid",out,sizeof(out)) && !strcmp(out,"A quoted \\\"network\\\""));
    assert(!ns_field("wpa_state=COMPLETED\nwpa_state=DISCONNECTED\n",strlen("wpa_state=COMPLETED\nwpa_state=DISCONNECTED\n"),"wpa_state",out,sizeof(out)));
    const char nul[]="wpa_state=COMPLETED\n\0freq=5220\n";
    assert(!ns_field(nul,sizeof(nul)-1,"wpa_state",out,sizeof(out)));
    assert(!ns_field("wpa_state=COMPLETED",19,"wpa_state",out,sizeof(out)));
    assert(!ns_field(status,sizeof(status)-1,"wpa_state",out,3));
    assert(!ns_field("not_wpa_state=COMPLETED\n",strlen("not_wpa_state=COMPLETED\n"),"wpa_state",out,sizeof(out)));
    assert(ns_number("-51",-127,-1,&n) && n==-51);
    assert(!ns_number("-51 dBm",-127,-1,&n));assert(!ns_number("9999999999999999999999999",0,8000,&n));
    assert(!ns_number("0",-127,-1,&n));assert(!ns_number(" 4",0,8000,&n));
    assert(ns_ipv4("192.0.2.103"));assert(ns_ipv4("0.0.0.0"));
    assert(!ns_ipv4("192.0.2.256"));assert(!ns_ipv4("192.0.2.103;reboot"));
    assert(!ns_ipv4("192.0.2"));assert(!ns_ipv4("192.0.2.0000"));assert(!ns_ipv4("192.0.2.1.3"));
    FILE *f=tmpfile();assert(f);ns_json_string(f,"\"\\\n\t<svg>\xff");fflush(f);rewind(f);
    char quoted[128]={0};assert(fread(quoted,1,sizeof(quoted)-1,f)>0);fclose(f);
    assert(!strcmp(quoted,"\"\\\"\\\\\\u000a\\u0009<svg>\\u00ff\""));
    puts("PASS: bounded WPA fields, duplicates, malformed/truncated replies, RSSI, IPv4 and JSON escaping");
    return 0;
}
