/* Retire a previous immutable .13 component before the new native boot.
 * No serial access. An unrecognized component stays in the existing boot gate. */
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../transfer/app_transfer.h"
#include "profile.h"

#ifndef RETIRE_PARENT
#define RETIRE_PARENT "/opt/usr"
#endif
#ifndef RETIRE_CURRENT
#define RETIRE_CURRENT "/opt/inst/cc2-rfid13/component/app-integration.json"
#endif
#ifndef RETIRE_LOCK
#define RETIRE_LOCK "/tmp/cc2-control-install.lock"
#endif

#define RETIRE_ROOT RETIRE_PARENT "/cc2-rfid"

static int absent(int dir,const char *name) {
    struct stat st;
    return fstatat(dir,name,&st,AT_SYMLINK_NOFOLLOW)<0&&errno==ENOENT;
}

static int private_read(int dir,const char *name,unsigned char *bytes,size_t cap,size_t *size) {
    int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    struct stat st;
    int ok=fd>=0&&!fstat(fd,&st)&&st.st_size>0&&(uint64_t)st.st_size<=cap&&
        cl_private(fd,0,st.st_size)&&cl_io(fd,bytes,(size_t)st.st_size,0);
    if(ok)*size=(size_t)st.st_size;
    if(fd>=0)close(fd);
    return ok;
}

static int component_hash(int dir,const char *name,char *hash) {
    int fd=openat(dir,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    struct stat st;
    int ok=fd>=0&&!fstat(fd,&st)&&st.st_size>0&&cl_private(fd,0,st.st_size)&&
        (!hash||sha256_fd(fd,hash));
    if(fd>=0)close(fd);
    return ok;
}

static int component_file(int dir,const char *name,const char *expected) {
    char hash[65];
    return component_hash(dir,name,expected?hash:NULL)&&(!expected||!strcmp(hash,expected));
}

static int integration13(int dir,unsigned char *bytes,size_t size) {
    if(memchr(bytes,0,size))return 0;
    bytes[size]=0;
    char app[65],capture[65],coordinator[65],profile[65];
    int end=0;
    int fields=sscanf((const char *)bytes,
        "{\"app_sha256\": \"%64[0-9a-f]\", \"capture_sha256\": \"%64[0-9a-f]\", "
        "\"coordinator_sha256\": \"%64[0-9a-f]\", \"profile_sha256\": \"%64[0-9a-f]\", \"protocol\": 1}\n%n",
        app,capture,coordinator,profile,&end);
    return fields==4&&(size_t)end==size&&strlen(app)==64&&strlen(capture)==64&&
        strlen(coordinator)==64&&strlen(profile)==64&&
        component_file(dir,"capture",capture)&&component_file(dir,"coordinator",coordinator)&&
        component_file(dir,"recovery-profile",profile)&&
        component_file(dir,"transfer-worker",NULL)&&component_file(dir,"libcc2-rfid.so",NULL);
}

static int profile13(int dir,const struct cl_record *record) {
    static const char *const paths[]={
        "bootstrap /etc/init.d/cc2-reactor-launch",
        "entry /opt/inst/cc2-printer-extensions13/launch.sh",
        "recover /opt/inst/cc2-printer-extensions13/recover-worker",
        "marker /opt/inst/cc2-printer-extensions13/rfid-enabled.template",
        "native /opt/usr/cc2-reactor-runtime-v2/elegoo_printer",
        "guard /opt/usr/cc2-rfid/libcc2-rfid.so"
    };
    unsigned char bytes[CL_PROFILE_MAX+1];
    size_t size;
    if(!private_read(dir,"recovery-profile",bytes,CL_PROFILE_MAX,&size)||
       bytes[size-1]!='\n'||memchr(bytes,0,size))return 0;
    if(record){
        char actual[65],expected[65];
        sha256_state hash;
        sha256_init(&hash);sha256_update(&hash,bytes,size);sha256_hex(&hash,actual);
        for(unsigned i=0;i<32;i++)snprintf(expected+2*i,3,"%02x",(unsigned)record->plan[i]);
        if(strcmp(actual,expected))return 0;
    }
    bytes[size]=0;
    unsigned seen=0;
    char *line=(char *)bytes;
    while(*line){
        char *end=strchr(line,'\n'),*space=strchr(line,' ');
        if(!end||!space||end-space<=66||space[65]!=' '||!cl_hex(space+1,64))return 0;
        *end=0;
        unsigned i=0;
        for(;i<6;i++){
            const char *separator=strchr(paths[i],' ');
            if(space-line==separator-paths[i]&&!memcmp(line,paths[i],(size_t)(space-line))&&
               !strcmp(space+66,separator+1))break;
        }
        if(i==6||(seen&(1u<<i)))return 0;
        seen|=1u<<i;
        line=end+1;
    }
    return seen==63;
}

static int empty_payload(int dir) {
    int fd=openat(dir,"payload",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(!cl_private(fd,1,0)){if(fd>=0)close(fd);return 0;}
    DIR *scan=fdopendir(fd);
    if(!scan){close(fd);return 0;}
    int ok;
    for(;;){
        errno=0;
        struct dirent64 *entry=readdir64(scan);
        if(!entry){ok=!errno;break;}
        if(strcmp(entry->d_name,".")&&strcmp(entry->d_name,"..")){ok=0;break;}
    }
    closedir(scan);
    return ok;
}

static int directory_clear(int dir,int dormant) {
    static const char *const initial[]={
        ".", "..", "lifecycle.lock", "app-integration.json", "recovery-profile",
        "capture", "coordinator", "transfer-worker", "libcc2-rfid.so", "payload"
    };
    int copy=dup(dir);
    DIR *scan=copy>=0?fdopendir(copy):NULL;
    if(!scan){if(copy>=0)close(copy);return 0;}
    int ok=1;
    for(;;){
        errno=0;
        struct dirent64 *entry=readdir64(scan);
        if(!entry){ok=!errno;break;}
        const char *name=entry->d_name;
        if(!strcmp(name,"lifecycle.next")||!strcmp(name,"activation-request")||
           (!strncmp(name,".lifecycle-",11)&&strstr(name,".next"))){ok=0;break;}
        if(dormant){
            if(!strcmp(name,"payload")&&!empty_payload(dir)){ok=0;break;}
            size_t i=0;
            for(;i<sizeof(initial)/sizeof(initial[0]);i++)if(!strcmp(name,initial[i]))break;
            if(i==sizeof(initial)/sizeof(initial[0])){ok=0;break;}
        }
    }
    closedir(scan);
    return ok;
}

static int restored(struct cl_store *store,struct cl_record *record) {
    static const unsigned char marker[]="CC2-RFID-NATIVE-R2\n";
    unsigned char bytes[sizeof(marker)];
    size_t size;
    if(!cl_store_read(store,record)||record->phase!=CL_DISABLED)return 0;
    if(!absent(store->directory,"enabled")&&
       (!private_read(store->directory,"enabled",bytes,sizeof(bytes),&size)||
        size!=sizeof(marker)-1||memcmp(bytes,marker,size)))return 0;
    unsigned char *backup=malloc(AP_SIZE);
    int ok=backup&&private_read(store->directory,"original-stock-44.bin",backup,AP_SIZE,&size)&&
        size==AP_SIZE&&ap_image(backup,size,0);
    free(backup);
    return ok;
}

static int entry_count(int dir,size_t expected) {
    int fd=openat(dir,".",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    DIR *scan=fd>=0?fdopendir(fd):NULL;
    if(!scan){if(fd>=0)close(fd);return 0;}
    size_t count=0;
    int ok=0;
    for(;;){
        errno=0;
        struct dirent64 *entry=readdir64(scan);
        if(!entry){ok=!errno&&count==expected;break;}
        if(strcmp(entry->d_name,".")&&strcmp(entry->d_name,"..")&&++count>expected)break;
    }
    closedir(scan);
    return ok;
}

static int legacy_dormant(int dir) {
    static const char *const names[]={
        "activate-on-ready","app-integration.json","boot-dispatch.sh","capture",
        "coordinator","enabled","fresh-guard.sh","fresh-reader","guarded-entry.sh",
        "guarded.sha256","libcc2-rfid.so","payload/0","payload/1","payload/2",
        "payload/3","payload/4","payload/5","payload/6","payload/7","recover-worker",
        "recovery-profile","startup-plan","startup-worker","stock-entry.sh","transfer-worker"
    };
    /* SHA-256 of the concatenated lowercase file digests in names[] order. */
    static const char *const qualified[]={
        "af73c1086c6cfa234a2ca9fcde9acbe5b8ad53d729a5c62e04f9f0eab7ee1f6f", /* 1.2.26 */
        "c734c58aa0517610f72431e651eeb4026b657651236245158967a9acd1906657", /* 1.2.27 */
        "860ed907b532259b7ed6cd8d9a5ed460e370710c31f8d47f1987de782943b8a1"  /* 1.2.28 private */
    };
    /* Seventeen root files, payload directory and the locked lifecycle.lock. */
    if(!entry_count(dir,19))return 0;
    int payload=openat(dir,"payload",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    int ok=cl_private(payload,1,0)&&entry_count(payload,8);
    sha256_state combined;
    sha256_init(&combined);
    char digest[65];
    for(size_t i=0;ok&&i<sizeof(names)/sizeof(names[0]);i++){
        const char *name=names[i];
        int nested=!strncmp(name,"payload/",8);
        ok=component_hash(nested?payload:dir,nested?name+8:name,digest);
        if(ok)sha256_update(&combined,(const unsigned char *)digest,64);
    }
    if(payload>=0)close(payload);
    if(!ok)return 0;
    sha256_hex(&combined,digest);
    for(size_t i=0;i<sizeof(qualified)/sizeof(qualified[0]);i++)
        if(!strcmp(digest,qualified[i]))return 1;
    return 0;
}

int main(void) {
    if(geteuid()!=0)return 1;
    struct stat st;
    if(lstat(RETIRE_ROOT,&st)<0)return errno==ENOENT?0:1;
    umask(077);
    if(mkdir(RETIRE_LOCK,0700))return 1;
    int result=0;
    struct cl_store store;
    if(!cl_store_open(&store,RETIRE_ROOT)){result=1;goto done;}
    unsigned char old[2049],current[2048];
    size_t old_size,new_size;
    if(!private_read(AT_FDCWD,RETIRE_CURRENT,current,sizeof(current),&new_size)){
        result=1;goto close_store;
    }
    if(!private_read(store.directory,"app-integration.json",old,sizeof(old)-1,&old_size))
        goto close_store;
    if(old_size==new_size&&!memcmp(old,current,old_size))goto close_store;
    int dormant=absent(store.directory,"lifecycle");
    struct cl_record record;
    if(!(dormant&&legacy_dormant(store.directory))&&
       (!directory_clear(store.directory,dormant)||
        (!dormant&&!restored(&store,&record))||
        !profile13(store.directory,dormant?NULL:&record)||
        !integration13(store.directory,old,old_size)))goto close_store;
    char archive[]=RETIRE_PARENT "/cc2-rfid-before-update.XXXXXX";
    if(!mkdtemp(archive)){result=1;goto close_store;}
    int parent=open(RETIRE_PARENT,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    int target=open(archive,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(parent<0||target<0||fsync(parent)||renameat(parent,"cc2-rfid",target,"component")){
        result=1;
        (void)rmdir(archive);
    }else if(fsync(target)||fsync(parent)){
        result=1;
    }else{
        printf("RFID_RETIRED=%s/component state=%s\n",archive,dormant?"dormant":"restored");
    }
    if(target>=0)close(target);
    if(parent>=0)close(parent);
close_store:
    cl_store_close(&store);
done:
    if(rmdir(RETIRE_LOCK))result=1;
    return result;
}
