#define main cc2_original_main
#include "../src/main.c"
#undef main
#include <assert.h>

static console_state command;
static mqtt_client machine;
static const char completed[]="{\"report\":{\"message\":\"// Mesh Bed Leveling Complete\"}}\n";
static const char ack[]="{\"id\":101,\"result\":{}}\n";
static void setup(const char *output) {
    memset(&plates_cal,0,sizeof(plates_cal));
    plates_cal.stage=CAL_PROBING;plates_cal.side='A';plates_cal.temp=100;
    plates_cal.generation=42;snprintf(plates_cal.plate,sizeof(plates_cal.plate),"old-plate");
    command.generation=42;command.busy=0;command.completed=command.success=1;
    snprintf(command.command,sizeof(command.command),"BED_MESH_CALIBRATE PROFILE=default BED_TEMP=100");
    command.output_len=(size_t)snprintf(command.output,sizeof(command.output),"%s",output);
    plates.measure_count=1;snprintf(plates.measures[0].id,sizeof(plates.measures[0].id),"retained");
}
static void refused(const char *output) {
    setup(output);plates_calibration_tick(&machine,&command);
    assert(plates_cal.stage==CAL_IDLE && !strcmp(plates_cal.result,"failed"));
    assert(!strcmp(plates_cal.error,"probing") && !plates_cal.measure[0]);
    assert(plates.measure_count==1 && !strcmp(plates.measures[0].id,"retained"));
}
int main(void) {
    assert(!pthread_mutex_init(&command.lock,NULL));
    refused(ack); /* the exact regression: accepted RPC, old mesh still in slot */
    refused("{\"report\":{\"error_code\":704,\"error_level\":1,\"message\":\"Z home failed\"}}\n{\"id\":101,\"result\":{}}\n");
    refused("{\"report\":{\"message\":\"// Bed Mesh State: failed\"}}\n{\"id\":101,\"result\":{}}\n");
    refused("{\"result\":{\"status\":{\"bed_mesh\":{\"web_feedback\":{\"result\":\"completed\"}}}}}\n");
    refused("{\"report\":{\"message\":\"// Mesh Bed Leveling Complete\"}}"); /* torn frame */
    refused("{\"report\":{\"message\":\"Mesh Bed Leveling Complete, maybe\"}}\n");
    char both[1024];snprintf(both,sizeof(both),"%s{\"report\":{\"error_code\":704,\"error_level\":1}}\n%s",completed,ack);
    refused(both); /* errors after completion still win */
    snprintf(both,sizeof(both),"{\"report\":{\"error_code\":704,\"error_level\":1}}\n%s%s",completed,ack);refused(both);
    snprintf(both,sizeof(both),"%s{\"id\":101,\"error\":{\"message\":\"failed\"}}\n",completed);refused(both);
    setup(completed);command.generation++;plates_calibration_tick(&machine,&command);
    assert(!plates_cal.stage && !strcmp(plates_cal.result,"failed"));
    setup(completed);snprintf(command.command,sizeof(command.command),"HELP");
    plates_calibration_tick(&machine,&command);assert(!plates_cal.stage && !strcmp(plates_cal.result,"failed"));
    setup(completed);command.output_len=sizeof(command.output)-1;
    plates_calibration_tick(&machine,&command);assert(!plates_cal.stage && !strcmp(plates_cal.result,"failed"));
    setup(completed);command.success=0;plates_calibration_tick(&machine,&command);
    assert(!plates_cal.stage && !strcmp(plates_cal.result,"failed"));
    setup(completed);command.busy=1;plates_calibration_tick(&machine,&command);
    assert(plates_cal.stage==CAL_PROBING);command.busy=0;
    plates_calibration_tick(&machine,&command);assert(plates_cal.stage==CAL_SAVING);
    assert(plates.measure_count==1 && !strcmp(plates.measures[0].id,"retained"));
    command.generation++;plates_calibration_tick(&machine,&command);
    assert(!plates_cal.stage && !strcmp(plates_cal.result,"failed"));
    assert(plates.measure_count==1 && !strcmp(plates.measures[0].id,"retained"));
    /* A fresh completed calibration can legitimately produce the same matrix:
     * success evidence does not depend on a changed old mesh or timestamps. */
    setup(completed);plates_calibration_tick(&machine,&command);assert(plates_cal.stage==CAL_SAVING);
    setup(completed);plates_cal.plate[0]=0;plates_calibration_tick(&machine,&command);
    assert(!plates_cal.stage && !strcmp(plates_cal.result,"done"));
    pthread_mutex_destroy(&command.lock);
    puts("PASS: current-run completion required; ACK-only, failed, stale/foreign, truncated and superseded results preserve previous measurements");
    return 0;
}
