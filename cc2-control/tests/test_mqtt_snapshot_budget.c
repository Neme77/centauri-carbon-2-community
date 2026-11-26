#define _GNU_SOURCE
/* Reuse the previously qualified socket-level snapshot retry-budget oracle. */
#define main snapshot_budget_fixture_main
#include "mqtt_snapshot_budget_fixture.h"
#undef main
int main(void){
    char *args[]={"snapshot-budget","fixed",NULL};
    return snapshot_budget_fixture_main(2,args);
}
