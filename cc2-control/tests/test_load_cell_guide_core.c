#include "../src/load_cell_guide_core.h"
#include <assert.h>
int main(void){
    const char none[]="{\"id\":203,\"result\":{\"status\":{\"gcode\":{\"commands\":{\"LOAD_CELL_READ\":{}}},\"configfile\":{\"save_config_pending\":false,\"save_config_pending_items\":{},\"settings\":{\"load_cell_probe\":{\"safety_limit\":\"1200\",\"counts_per_gram\":\"25.0\",\"reference_tare_counts\":\"0\"}}}}}}";
    cell_snapshot s=cell_parse_snapshot(none,strlen(none));assert(s.understood&&!s.registered&&!s.foreign&&!s.pending&&!s.other_pending&&s.safety_limit==1200);
    const char own[]="{\"id\":203,\"result\":{\"status\":{\"gcode\":{\"commands\":{\"ABORT\":{\"help\":\"Abort load cell calibration tool\"},\"ACCEPT\":{\"help\":\"Accept calibration results and apply to load cell\"},\"TARE\":{\"help\":\"Tare the load cell\"},\"CALIBRATE\":{\"help\":\"Enter the load cell value in grams\"}}},\"configfile\":{\"save_config_pending\":false,\"save_config_pending_items\":{}}}}}";
    s=cell_parse_snapshot(own,strlen(own));assert(s.understood&&s.registered&&!s.foreign&&!s.pending);
    const char foreign[]="{\"result\":{\"status\":{\"gcode\":{\"commands\":{\"ACCEPT\":{\"help\":\"Accept probe adjustment\"}}},\"configfile\":{\"save_config_pending\":false,\"save_config_pending_items\":{}}}}}";
    s=cell_parse_snapshot(foreign,strlen(foreign));assert(s.understood&&s.foreign&&!s.registered);
    const char pending[]="{\"result\":{\"status\":{\"gcode\":{\"commands\":{}},\"configfile\":{\"save_config_pending\":true,\"save_config_pending_items\":{\"load_cell_probe\":{\"counts_per_gram\":\"25.125\",\"reference_tare_counts\":\"-120\"}}}}}}";
    s=cell_parse_snapshot(pending,strlen(pending));assert(s.understood&&s.pending&&s.have_parameters&&!s.other_pending&&s.counts_per_gram==25.125&&s.tare_counts==-120);
    const char other[]="{\"result\":{\"status\":{\"gcode\":{\"commands\":{}},\"configfile\":{\"save_config_pending\":true,\"save_config_pending_items\":{\"extruder\":{\"pid_Kp\":\"20\"}}}}}}";
    s=cell_parse_snapshot(other,strlen(other));assert(s.understood&&s.pending&&s.other_pending&&!s.have_parameters);
    assert(!cell_parse_snapshot("{}",2).understood);
    assert(!cell_parse_snapshot("{\"error\":{}}",12).understood);
    assert(cell_grams_valid(50,1200)&&cell_grams_valid(500,1200)&&cell_grams_valid(1000,1200));
    assert(!cell_grams_valid(1200,1200)&&!cell_grams_valid(49.9,1200)&&!cell_grams_valid(NAN,1200)&&!cell_grams_valid(500,0));
    assert(cell_step_allowed(CELL_OFF,CELL_TARE,0,1,1));
    assert(!cell_step_allowed(CELL_ZERO,CELL_ACCEPT,1,1,1)&&!cell_step_allowed(CELL_LOAD,CELL_ACCEPT,1,1,1));
    assert(cell_step_allowed(CELL_REVIEW,CELL_ACCEPT,1,1,1));
    assert(!cell_step_allowed(CELL_REVIEW,CELL_ACCEPT,0,1,1)&&!cell_step_allowed(CELL_REVIEW,CELL_ACCEPT,1,0,1)&&!cell_step_allowed(CELL_REVIEW,CELL_ACCEPT,1,1,0));
    assert(cell_step_allowed(CELL_UNKNOWN,CELL_ABORT,1,1,0)&&!cell_step_allowed(CELL_UNKNOWN,CELL_ABORT,0,1,1));
    assert(!cell_step_allowed(CELL_APPLIED,CELL_ABORT,1,1,1));
    const char zero[]="\"// Load cell tare value: -0.01% (-125)\"";
    cell_report r=cell_parse_reports(zero,strlen(zero));assert(r.tare&&r.counts==-125&&!r.measurement);
    const char measured[]="\"// Calibration value: 2.00% (12375), Counts/gram: 25.00000, Total capacity: +/- 25.00Kg\",\"// Accept calibration with the ACCEPT command.\"";
    r=cell_parse_reports(measured,strlen(measured));assert(r.measurement&&r.accept_prompt&&r.counts_per_gram==25);
    const char incomplete[]="\"// You must use TARE first.\"";r=cell_parse_reports(incomplete,strlen(incomplete));assert(!r.measurement&&!r.accept_prompt&&!r.accepted);
    const char accept[]="\"// Load cell calibration settings: counts_per_gram: 25.000000 reference_tare_counts: -125\"";r=cell_parse_reports(accept,strlen(accept));assert(r.accepted&&r.counts_per_gram==25&&r.counts==-125);
    const char malformed[]="\"// Calibration value: 2.00% (12375), Counts/gram: nan, Total capacity: +/- 25.00Kg\",\"// Accept calibration with the ACCEPT command.\"";
    r=cell_parse_reports(malformed,strlen(malformed));assert(!r.measurement&&!r.accept_prompt);
    /* Every truncated native reply must remain bounded, including cut strings,
     * nested dictionaries and numeric values (run under ASan/UBSan as well). */
    const char *frames[]={none,own,foreign,pending,other};
    for(size_t f=0;f<sizeof(frames)/sizeof(frames[0]);f++)
        for(size_t n=0;n<strlen(frames[f]);n++)assert(!cell_parse_snapshot(frames[f],n).understood);
    const char *reports[]={zero,measured,incomplete,accept,malformed};
    for(size_t f=0;f<sizeof(reports)/sizeof(reports[0]);f++)
        for(size_t n=0;n<strlen(reports[f]);n++)(void)cell_parse_reports(reports[f],n);
    puts("PASS: load-cell registry ownership, pending-config separation, bounded known weights, incomplete ACCEPT refusal and native step reports");return 0;
}
