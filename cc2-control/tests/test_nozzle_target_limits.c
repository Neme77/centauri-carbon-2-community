#include "../src/control.h"
#include "../src/thermal_limits.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

int main(void) {
    mqtt_client m={0}; char action[128],script[1024],reason[256];
    m.connected=m.registered=m.have_machine_status=1;
    m.machine_status=1; m.last_message=time(NULL);
    const int temperatures[]={300,301,320,350};
    for(size_t i=0;i<sizeof(temperatures)/sizeof(temperatures[0]);i++) {
        int target=temperatures[i];
        snprintf(action,sizeof(action),"heaters:set:%d:60",target);
        assert(control_build_script(action,&m,script,sizeof(script),reason,sizeof(reason)));
        char expected[64];snprintf(expected,sizeof(expected),"HEATER=extruder TARGET=%d.0",target);
        assert(strstr(script,expected));
        snprintf(action,sizeof(action),"heater:extruder:%d",target);
        assert(control_build_script(action,&m,script,sizeof(script),reason,sizeof(reason)));
        assert(strstr(script,expected));
        snprintf(action,sizeof(action),"preheat:%d:60",target);
        assert(control_build_script(action,&m,script,sizeof(script),reason,sizeof(reason)));
        snprintf(expected,sizeof(expected),"M104 S%d\nM140 S60",target);assert(!strcmp(script,expected));
        m.have_extruder_target=1;m.extruder_target=target;
        assert(control_build_script("home:ALL",&m,script,sizeof(script),reason,sizeof(reason)));
        snprintf(expected,sizeof(expected),"G28\nSET_HEATER_TEMPERATURE HEATER=extruder TARGET=%d.0",target);
        assert(!strcmp(script,expected));
        m.machine_status=2;
        snprintf(action,sizeof(action),"heaters:set:%d:60",target);
        assert(control_build_script(action,&m,script,sizeof(script),reason,sizeof(reason)));
        assert(!control_build_script("preheat:350:60",&m,script,sizeof(script),reason,sizeof(reason)));
        assert(!control_build_script("home:ALL",&m,script,sizeof(script),reason,sizeof(reason)));
        m.machine_status=1;
    }
    const char *bad[]={"heaters:set:351:60","heaters:set:nan:60","heaters:set:350:inf",
        "heaters:set:350:121","heater:extruder:351","heater:extruder:nan",
        "preheat:351:60","preheat:nan:60","preheat:350:inf"};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++)
        assert(!control_build_script(bad[i],&m,script,sizeof(script),reason,sizeof(reason)));
    m.extruder_target=NAN;
    assert(control_build_script("home:X",&m,script,sizeof(script),reason,sizeof(reason)));
    assert(strstr(script,"TARGET=0.0"));
    m.last_message=time(NULL)-30;
    assert(!control_build_script("heaters:set:350:60",&m,script,sizeof(script),reason,sizeof(reason)));
    m.last_message=time(NULL);
    assert(control_build_script("pid:extruder:300",&m,script,sizeof(script),reason,sizeof(reason)));
    assert(!control_build_script("pid:extruder:301",&m,script,sizeof(script),reason,sizeof(reason)));
    puts("PASS: 300/301/320/350 manual targets, print guards and homing restoration; over-limit/nonfinite targets refused; PID range unchanged. No hardware accessed.");
    return 0;
}
