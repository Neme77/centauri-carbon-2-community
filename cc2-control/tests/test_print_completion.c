#define main cc2_original_main
#include "../src/main.c"
#undef main
#include <assert.h>
static mqtt_client machine;
static void readback(char *out,size_t capacity){
    int pair[2];assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));printer_response(pair[0],&machine);
    close(pair[0]);ssize_t n=read(pair[1],out,capacity-1);assert(n>0);out[n]=0;close(pair[1]);
}
int main(void){
    char reply[16384];machine.connected=machine.registered=machine.have_machine_status=1;
    machine.machine_status=1;machine.progress=100;machine.last_message=time(NULL);
    uds_init(&telemetry);telemetry.fd=99;telemetry.ready=1;telemetry.have_print_state=telemetry.have_filename=1;
    strcpy(telemetry.print_state,"complete");strcpy(telemetry.filename,"part.gcode");telemetry.present=1u<<U_DURATION;telemetry.values[U_DURATION]=123.5;
    clock_gettime(CLOCK_MONOTONIC,&telemetry.last_rx);readback(reply,sizeof(reply));
    assert(strstr(reply,"\"native\":{\"state\":\"complete\",\"filename\":\"part.gcode\",\"duration\":123.5}"));
    assert(strstr(reply,"\"progress\":100"));/* Native last-job count is not falsified. */
    telemetry.have_filename=0;telemetry.present=0;readback(reply,sizeof(reply));
    assert(strstr(reply,"\"native\":{\"state\":\"complete\",\"filename\":\"\",\"duration\":null}"));
    telemetry.last_rx.tv_sec-=10;readback(reply,sizeof(reply));assert(strstr(reply,"\"native\":null"));
    clock_gettime(CLOCK_MONOTONIC,&telemetry.last_rx);strcpy(telemetry.print_state,"cancelled");readback(reply,sizeof(reply));
    assert(strstr(reply,"\"native\":{\"state\":\"cancelled\""));
    puts("PASS fresh native completion metadata independent of cleared MQTT job; raw counters retained and stale evidence omitted");return 0;
}
