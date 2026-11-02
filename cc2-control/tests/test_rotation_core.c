#include "../src/rotation_core.h"
#include <assert.h>
int main(void){
    const char *config="[extruder]\nmicrosteps=16\ngear_ratio=52:10\nrotation_distance=28.8\n[heater_bed]\n";
    rd_entry e;assert(rd_parse(config,strlen(config),0,&e)&&e.present&&fabs(e.value-28.8)<1e-10);
    double value;assert(rd_calculate(28.8,100,95,&value)&&fabs(value-27.36)<1e-10);
    assert(rd_calculate(28.8,100,105,&value)&&fabs(value-30.24)<1e-10);
    assert(!rd_calculate(28.8,0,100,&value)&&!rd_calculate(28.8,100,0,&value));
    assert(!rd_calculate(-28.8,100,100,&value)&&!rd_calculate(NAN,100,100,&value));
    assert(!rd_calculate(28.8,INFINITY,100,&value)&&!rd_calculate(28.8,100,INFINITY,&value));
    const char *autosave="#*# <---------------------- SAVE_CONFIG ---------------------->\n#*#\n#*# [bed_mesh default]\n#*# points = 0, 1, 2\n#*# [extruder]\n#*# pid_Kp = 21.7\n#*# rotation_distance = 29.2 # keep comment\n#*# [probe]\n#*# z_offset = -0.5\n";
    assert(rd_parse(autosave,strlen(autosave),1,&e)&&e.present&&e.value==29.2);
    size_t length;char *next=rd_rewrite(autosave,strlen(autosave),27.36,&length);assert(next);
    assert(strstr(next,"#*# rotation_distance = 27.3600000000 # keep comment"));
    assert(strstr(next,"#*# points = 0, 1, 2")&&strstr(next,"#*# pid_Kp = 21.7")&&strstr(next,"#*# z_offset = -0.5"));
    assert(rd_parse(next,length,1,&e)&&fabs(e.value-27.36)<1e-10);free(next);
    const char *absent="#*# <---------------------- SAVE_CONFIG ---------------------->\n#*# [extruder]\n#*# pid_Kp = 21.7\n#*# [probe]\n#*# z_offset = -0.5\n";
    next=rd_rewrite(absent,strlen(absent),28.8,&length);assert(next);
    assert(strstr(next,"#*# rotation_distance = 28.8000000000\n#*# [probe]"));free(next);
    const char *none="#*# <---------------------- SAVE_CONFIG ---------------------->\n#*# [bed_mesh default]\n#*# points = 0, 1, 2\n";
    next=rd_rewrite(none,strlen(none),28.8,&length);assert(next&&rd_parse(next,length,1,&e)&&e.present);free(next);
    const char *bad[]={"[extruder]\nrotation_distance=28.8\n", "#*# [extruder]\n#*# rotation_distance=28.8\n",
        "#*# <---------------------- SAVE_CONFIG ---------------------->\n#*# [extruder]\n#*# rotation_distance=28\n#*# rotation_distance=29\n",
        "#*# <---------------------- SAVE_CONFIG ---------------------->\n#*# [extruder]\n#*# rotation_distance=-28.8\n",
        "#*# <---------------------- SAVE_CONFIG ---------------------->\n#*# [extruder]\n#*# rotation_distance=nan\n",
        "#*# <---------------------- SAVE_CONFIG ---------------------->\n#*# [extruder]\n#*# rotation_distance=28x\n",
        "#*# <---------------------- SAVE_CONFIG ---------------------->\n#*# [extruder]\n#*# [extruder]\n"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++)assert(!rd_rewrite(bad[i],strlen(bad[i]),28.8,&length));
    const char *included="[include other.cfg]\n[extruder]\nrotation_distance=28.8";assert(!rd_parse(included,strlen(included),0,&e));
    puts("PASS: calibration formula, real stock/autosave layouts, precise extruder-only edits, meshes/PID/Z preservation, ambiguous/invalid input refusal");return 0;
}
