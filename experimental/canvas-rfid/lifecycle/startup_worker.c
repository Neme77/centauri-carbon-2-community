/* Boot-only fixed-path transaction resumer. No install/flash/stop commands.
 * The root init delegate pins this executable. Its private data plan is bound
 * by the lifecycle journal through recovery-profile's bootstrap role. */
#define _GNU_SOURCE
#include "startup_resume.h"
#include "profile.h"
#ifndef SW_BASE
#define SW_BASE "/opt/usr/cc2-rfid"
#endif
#ifndef SW_PREFIX
#define SW_PREFIX ""
#endif
#ifndef SW_RUN_SHA
#define SW_RUN_SHA "05e4614779fc59b43c604eda7d85f1e8534b24d3cf284b16226c9c3bbc975118"
#endif
static const char *targets[]={
    SW_PREFIX "/etc/init.d/cc2-reactor-launch",
    SW_PREFIX "/opt/usr/cc2-reactor-runtime-v2/launcher.sh",
    SW_PREFIX "/opt/usr/cc2-reactor-runtime-v2/SHA256SUMS",
    SW_PREFIX "/opt/usr/cc2-network-manager/profile.sh",
    SW_PREFIX "/opt/usr/cc2-network-manager/reactor-entry.sh",
    SW_PREFIX "/opt/usr/cc2-network-manager/activate.sh",
    SW_PREFIX "/opt/usr/cc2-network-manager/SHA256SUMS",
    SW_PREFIX "/opt/usr/cc2-network-manager/startup.sha256"
};
static const char *role_paths[]={
    SW_BASE "/startup-plan",SW_PREFIX "/opt/usr/cc2-reactor-runtime-v2/elegoo_printer",
    SW_BASE "/libcc2-rfid.so",SW_BASE "/guarded-entry.sh",SW_BASE "/recover-worker",SW_BASE "/enabled"
};
static int sw_stop(const char *why){fprintf(stderr,"STOP: startup_resume_%s\n",why);return 125;}
static int sw_private_read(int parent,const char *name,unsigned char *out,size_t maximum,size_t *length){
    int fd=openat(parent,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);if(fd<0)return 0;
    struct stat st;
    int okay=!fstat(fd,&st) && st.st_size>0 && (uint64_t)st.st_size<=maximum &&
             cl_private(fd,0,st.st_size) && cl_io(fd,out,(size_t)st.st_size,0);
    if(okay){*length=(size_t)st.st_size;out[*length]=0;if(memchr(out,0,*length))okay=0;}
    close(fd);return okay;
}
static int sw_roles(struct cl_store *store){
    static const char *roles[]={"bootstrap","native","guard","entry","recover","marker"};
    unsigned char bytes[CL_PROFILE_MAX+1];size_t length=0;
    if(!sw_private_read(store->directory,"recovery-profile",bytes,CL_PROFILE_MAX,&length))return 0;
    unsigned seen=0;char *line=(char*)bytes;
    while(*line){
        char *end=strchr(line,'\n');if(!end)return 0;*end=0;
        char *space=strchr(line,' ');if(!space)return 0;*space++=0;
        unsigned role=6;for(unsigned i=0;i<6;i++)if(!strcmp(line,roles[i]))role=i;
        if(role==6 || (seen&(1u<<role)) || strlen(space)<66 || space[64]!=' ' ||
           !cl_hex(space,64) || strcmp(space+65,role_paths[role]))return 0;
        seen|=1u<<role;line=end+1;
    }
    return seen==63;
}
static int sw_dependency(char **line,const char *role,char digest[65]){
    char *end=strchr(*line,'\n');if(!end)return 0;*end=0;
    size_t prefix=strlen(role);
    if(strlen(*line)!=prefix+65 || strncmp(*line,role,prefix) || (*line)[prefix]!=' ' ||
       !cl_hex(*line+prefix+1,64))return 0;
    memcpy(digest,*line+prefix+1,64);digest[64]=0;*line=end+1;return 1;
}
static int sw_load(struct cl_store *store,struct tx_file files[8],char dispatch[65],char stock[65]){
    unsigned char bytes[4097];size_t length=0;
    if(!sw_private_read(store->directory,"startup-plan",bytes,4096,&length))return 0;
    const char *magic="CC2-STARTUP-PLAN-1\n";size_t magic_length=strlen(magic);
    if(length<magic_length || memcmp(bytes,magic,magic_length))return 0;
    char *line=(char*)bytes+magic_length;
    if(!sw_dependency(&line,"dispatch",dispatch) || !sw_dependency(&line,"stock-entry",stock))return 0;
    int payload=openat(store->directory,"payload",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(!cl_private(payload,1,0)){if(payload>=0)close(payload);return 0;}
    int okay=1;size_t total=0;
    for(unsigned i=0;okay && i<8;i++){
        char *end=strchr(line,'\n');if(!end){okay=0;break;}*end=0;
        unsigned index=0,old_mode=0,new_mode=0;char extra=0,canonical[256];
        struct tx_file *f=files+i;
        int fields=sscanf(line,"%u %o %o %64s %64s%c",&index,&old_mode,&new_mode,f->old_sha,f->new_sha,&extra);
        if(fields!=5 || index!=i || !tx_mode((mode_t)old_mode) || !tx_mode((mode_t)new_mode) ||
           !tx_hex(f->old_sha) || !tx_hex(f->new_sha)){okay=0;break;}
        int n=snprintf(canonical,sizeof(canonical),"%u %04o %04o %s %s",i,old_mode,new_mode,f->old_sha,f->new_sha);
        if(n<1 || (size_t)n>=sizeof(canonical) || strcmp(canonical,line)){okay=0;break;}
        f->path=targets[i];f->old_mode=(mode_t)old_mode;f->new_mode=(mode_t)new_mode;
        char name[16];snprintf(name,sizeof(name),"%u",i);unsigned char *data=NULL;
        okay=tx_read(payload,name,f->new_sha,0600,&data,&f->length);
        f->bytes=data;total+=f->length;if(total>131072u)okay=0;
        line=end+1;
    }
    if(*line)okay=0;
    close(payload);return okay;
}
int main(int argc,char **argv){
    int prepare=argc==2 && !strcmp(argv[1],"--prepare-only");
    if(!prepare && (argc<2 || strcmp(argv[1],"--boot")))return sw_stop("unknown_action");
    if(geteuid()!=0 || (getenv("LD_PRELOAD") && *getenv("LD_PRELOAD")))return sw_stop("environment");
    struct cl_store store;struct cl_record record;struct tx_file files[8];memset(files,0,sizeof(files));
    char dispatch[65],stock[65];int okay=cl_store_open(&store,SW_BASE);
    if(!okay)return sw_stop("locked_or_missing");
    okay=cl_store_read(&store,&record) && cl_profile(&store,&record) && sw_roles(&store) &&
         sw_load(&store,files,dispatch,stock) &&
         cl_digest_path(SW_PREFIX "/opt/bin/run_printer.sh",SW_RUN_SHA) &&
         cl_digest_path(SW_BASE "/boot-dispatch.sh",dispatch) && cl_digest_path(SW_BASE "/stock-entry.sh",stock) &&
         sr_resume(&store,files,8,record.plan) && cl_profile(&store,&record);
    int removed=okay && (record.phase==CL_HOST_REMOVE_INTENT || record.phase==CL_DISABLED);
    if(removed)okay=tx_current(files)==0;
    for(unsigned i=0;i<8;i++)free((void*)files[i].bytes);
    cl_store_close(&store);
    if(!okay)return sw_stop("unresolved_or_changed_plan");
    if(prepare){puts("STARTUP PREPARED: exact file transaction reconciled; no native process or UART started.");return 0;}
    /* A completed removal restores the original delegate last. It is verified
     * by the transaction; never exec that path while it is still this gate. */
    argv[1]=(char*)(removed?targets[0]:SW_BASE "/boot-dispatch.sh");
    execv(argv[1],argv+1);return sw_stop("entry_exec_failed");
}
