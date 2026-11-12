/* Same-profile .13 boot migration. The signed root filesystem supplies the
 * exact source and plan. This worker only updates app-bound files. */
#define _GNU_SOURCE
#define AU_IMMUTABLE_BOOT_BINDING 1
#include "../transfer/app_transfer.h"
#include "app_binding_tx.h"
#include "profile.h"
#include <sys/wait.h>

#ifndef AU_BOOT_PLAN
#error An exact previous and next app binding plan is required
#endif
#include AU_BOOT_PLAN
#ifndef AU_INCOMING
#define AU_INCOMING "/opt/inst/cc2-rfid13/component"
#endif

static int boot_absent(int directory,const char *name){
    struct stat st;
    return fstatat(directory,name,&st,AT_SYMLINK_NOFOLLOW)<0&&errno==ENOENT;
}

static int boot_backup(int directory){
    unsigned char *image=malloc(AP_SIZE);
    int fd=openat(directory,"original-stock-44.bin",O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    int ok=image&&cl_private(fd,0,AP_SIZE)&&cl_io(fd,image,AP_SIZE,0)&&ap_image(image,AP_SIZE,0);
    if(fd>=0)close(fd);
    free(image);
    return ok;
}

static int boot_idle(void){
    pid_t child=fork();
    if(child<0)return 0;
    if(!child){
        execl(AU_APP,AU_APP,"--installer-native-idle",(char *)NULL);
        _exit(127);
    }
    int status;
    pid_t done;
    do{done=waitpid(child,&status,0);}while(done<0&&errno==EINTR);
    return done==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0;
}

int main(int argc,char **argv){
    int rollback=argc==2&&!strcmp(argv[1],"--rollback");
    if(geteuid()!=0||(argc!=1&&!rollback))return 1;
    struct stat st;
    if(lstat(AU_COMPONENT,&st))return !rollback&&errno==ENOENT?0:1;
    umask(077);
    if(mkdir(AU_BARRIER,0700))return 1;
    int held=open(AU_BARRIER,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    int source=-1,ok=0;
    struct au_store store={.directory=-1,.lock=-1,.native={.directory=-1,.lock=-1}};
    struct cl_record native;
    if(!au_barrier(held)||!cl_store_open(&store.native,AU_COMPONENT))goto done;
    if(boot_absent(store.native.directory,"lifecycle")){
        ok=!rollback&&boot_absent(store.native.directory,"enabled")&&
           boot_absent(store.native.directory,"lifecycle.next");
        goto done;
    }
    if(!cl_store_read(&store.native,&native))goto done;
    if(native.phase!=CL_ACTIVE){ok=!rollback;goto done;}
    if(!boot_absent(store.native.directory,"lifecycle.next")||
       !boot_absent(store.native.directory,"activation-request")||
       !cl_profile(&store.native,&native)||!boot_backup(store.native.directory)||
       !cl_digest_path(AU_COMPONENT "/transfer-worker",AU_TRANSFER_SHA)||
       !au_app(au_boot_plan.new_app))goto done;
    /* If a previous boot completed, recheck its durable journal and targets. */
    cl_store_close(&store.native);
    if(!au_open(&store,!rollback)||!cl_store_read(&store.native,&native)||native.phase!=CL_ACTIVE||
       !cl_profile(&store.native,&native)||!boot_backup(store.native.directory))goto done;
    if(rollback){
        ok=boot_idle()&&au_run(&store,&au_boot_plan,held,1);
        if(ok)puts("RFID APP BINDING ROLLED BACK: previous app-bound files restored; active Canvas journal and original backup preserved.");
        goto done;
    }
    source=open(AU_INCOMING,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(!au_directory_private(source))goto done;
    if(boot_absent(store.directory,"state")){
        unsigned current=0;
        for(unsigned i=0;i<AU_COUNT;i++)current+=au_current(&store,au_boot_plan.files+i)==1;
        if(current==AU_COUNT){ok=1;goto done;}
        if(!au_prepare(&store,&au_boot_plan,source,held))goto done;
    }
    ok=au_run(&store,&au_boot_plan,held,0);
    if(ok)puts("RFID APP BINDING VERIFIED: active Canvas journal and original backup preserved.");
done:
    if(source>=0)close(source);
    au_close(&store);
    if(held>=0)close(held);
    if(rmdir(AU_BARRIER))ok=0;
    if(!ok)fputs("STOP: active Canvas app binding not qualified; retain journal and backups.\n",stderr);
    return ok?0:1;
}
