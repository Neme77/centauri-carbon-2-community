#define _GNU_SOURCE
/* Reuse the previously qualified socket-level discovery regression oracle. */
#define main snapshot_discovery_fixture_main
#include "mqtt_snapshot_discovery_fixture.h"
#undef main
int main(void){
    char *args[]={"snapshot-discovery","fixed",NULL};
    return snapshot_discovery_fixture_main(2,args);
}
