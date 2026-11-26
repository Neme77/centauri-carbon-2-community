/* App-side counterpart of the coordinator adapter. Read-only; no activation,
 * repair, migration or implicit fallback on malformed records. */
#ifndef CC2_PANEL_API_SELF_H
#define CC2_PANEL_API_SELF_H
#include "panel-api/panel_process.h"
#include "panel-api/panel_slots.h"
#ifndef PC_ROOT
#define PC_ROOT "/opt/usr/cc2-panel-releases"
#endif
static int panel_api_self(const char *digest){
    int root=open(PC_ROOT,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(root<0)return 0;
    char active[65];int okay=ps_service_ready(root,active)&&!strcmp(active,digest);
    int slot=-1,proc=-1;
    if(okay){
        slot=openat(root,active,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        proc=open("/proc",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        struct pc_process identity;
        okay=slot>=0&&proc>=0&&pc_process_read(slot,proc,getpid(),&identity)&&
            !strcmp(identity.contract.app_sha,digest);
    }
    if(slot>=0)close(slot);
    if(proc>=0)close(proc);
    char last[65];
    okay=okay&&ps_service_ready(root,last)&&!strcmp(active,last);
    close(root);return okay;
}
#endif
