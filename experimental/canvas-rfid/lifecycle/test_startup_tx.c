#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
static const char *fault_point;
static unsigned fault_index;
static void test_fault(const char *point,unsigned index) {
    if(fault_point && !strcmp(point,fault_point) && index==fault_index)_exit(91);
}
#define TX_FAULT(point,index) test_fault(point,index)
#include "startup_tx.h"
#include "startup_resume.h"

struct fixture {
    char root[128],component[160],targets[160],paths[3][192];
    struct tx_file files[3];
    struct cl_store store;
};
static const unsigned char old_bytes[3][6]={"old-0","old-1","old-2"};
static const unsigned char new_bytes[3][6]={"new-0","new-1","new-2"};
static void file_write(const char *path,const unsigned char *bytes,size_t length,mode_t mode) {
    int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,mode);assert(fd>=0);
    assert(cl_io(fd,(unsigned char*)bytes,length,1) && !fchmod(fd,mode) && !fsync(fd));assert(!close(fd));
}
static void phase(struct fixture *f,unsigned value) {
    struct cl_record r;assert(cl_store_read(&f->store,&r));uint64_t generation=r.generation;
    r.phase=value;r.generation++;assert(cl_store_commit(&f->store,&r,generation));
}
static void init(struct fixture *f) {
    memset(f,0,sizeof(*f));strcpy(f->root,"/tmp/cc2-startup-tx-XXXXXX");assert(mkdtemp(f->root));
    snprintf(f->component,sizeof(f->component),"%s/component",f->root);
    snprintf(f->targets,sizeof(f->targets),"%s/targets",f->root);
    assert(!mkdir(f->component,0700) && !mkdir(f->targets,0700));
    assert(cl_store_open(&f->store,f->component));
    struct cl_record record={0};record.phase=CL_HOST_STAGED;record.generation=1;
    record.transaction[0]=1;record.plan[0]=2;memcpy(record.boot,"00000000-0000-0000-0000-000000000001",37);
    assert(cl_store_commit(&f->store,&record,0));
    for(unsigned i=0;i<3;i++) {
        snprintf(f->paths[i],sizeof(f->paths[i]),"%s/target-%u",f->targets,i);
        struct tx_file *file=f->files+i;file->path=f->paths[i];file->bytes=new_bytes[i];file->length=5;
        file->old_mode=i==1?0644:0700;file->new_mode=i==1?0600:0755;
        tx_hash(old_bytes[i],5,file->old_sha);tx_hash(new_bytes[i],5,file->new_sha);
        file_write(file->path,old_bytes[i],5,file->old_mode);
    }
}
static void all_state(struct fixture *f,int value) {
    for(unsigned i=0;i<3;i++)assert(tx_current(f->files+i)==value);
}
static void empty_directory(const char *path) {
    DIR *directory=opendir(path);if(!directory){assert(errno==ENOENT);return;}
    /* A 32-bit test executable on Docker's 64-bit overlay filesystem can see
     * inode numbers outside ino_t. That does not change the printer file ABI.
     * Probe the old enumeration once, then use the explicit large-inode API. */
    static int reported;
    if(!reported && sizeof(ino_t)==4) {
        unsigned count=0;int error=0;
        for(;;) {
            errno=0;struct dirent *entry=readdir(directory);
            if(!entry){error=errno;break;}count++;
        }
        printf("DIRECTORY PROBE: legacy inode32 entries=%u errno=%d%s\n",count,error,error==EOVERFLOW?" (EOVERFLOW confirmed)":"");
        rewinddir(directory);reported=1;
    }
    for(;;) {
        errno=0;struct dirent64 *entry=readdir64(directory);
        if(!entry) {
            int error=errno;
            if(error)perror("test fixture readdir64");
            assert(error==0);break;
        }
        if(!strcmp(entry->d_name,".") || !strcmp(entry->d_name,".."))continue;
        assert(!unlinkat(dirfd(directory),entry->d_name,0));
    }
    assert(!closedir(directory));
    int removed=rmdir(path);if(removed)perror(path);assert(removed==0);
}
static void finish(struct fixture *f) {
    char snapshots[192];snprintf(snapshots,sizeof(snapshots),"%s/startup-tx",f->component);
    cl_store_close(&f->store);empty_directory(snapshots);empty_directory(f->targets);
    empty_directory(f->component);assert(!rmdir(f->root));
}
static void crash(struct fixture *f,const char *point,unsigned index,int operation) {
    pid_t child=fork();assert(child>=0);
    if(!child) {
        fault_point=point;fault_index=index;
        if(operation==0)(void)tx_prepare(&f->store,f->files,3);
        else (void)tx_run(&f->store,f->files,3,operation==2);
        _exit(92);
    }
    int status=0;assert(waitpid(child,&status,0)==child && WIFEXITED(status) && WEXITSTATUS(status)==91);
    cl_store_close(&f->store);assert(cl_store_open(&f->store,f->component));
}
int main(void) {
    umask(077);unsigned cases=0;struct fixture f;
    for(unsigned scenario=0;scenario<5;scenario++) {
        init(&f);
        const char *point=scenario==0?"snapshot-written":scenario==4?"ready":"snapshot";
        unsigned index=scenario>=1 && scenario<=3?scenario-1:0;
        crash(&f,point,index,0);all_state(&f,0);
        assert(tx_prepare(&f.store,f.files,3) && tx_run(&f.store,f.files,3,0));all_state(&f,1);
        phase(&f,CL_STOCK_VERIFIED);assert(tx_run(&f.store,f.files,3,1));all_state(&f,0);
        finish(&f);cases++;
    }
    for(unsigned restore=0;restore<2;restore++)for(unsigned index=0;index<3;index++)for(unsigned event=0;event<2;event++) {
        init(&f);assert(tx_prepare(&f.store,f.files,3));
        if(restore){assert(tx_run(&f.store,f.files,3,0));phase(&f,CL_STOCK_VERIFIED);}
        crash(&f,event?"renamed":"written",index,restore?2:1);
        assert(tx_run(&f.store,f.files,3,(int)restore));all_state(&f,restore?0:1);
        assert(tx_run(&f.store,f.files,3,(int)restore));
        finish(&f);cases++;
    }
    for(unsigned scenario=0;scenario<4;scenario++) {
        init(&f);assert(tx_prepare(&f.store,f.files,3));
        if(scenario==0) {
            file_write(f.paths[2],(const unsigned char*)"FOREIGN",7,0700);
            assert(!tx_run(&f.store,f.files,3,0));
            assert(tx_current(f.files)==0 && tx_current(f.files+1)==0);
            unsigned char *data=NULL;size_t length=0;char digest[65],leaf[256];
            tx_hash((const unsigned char*)"FOREIGN",7,digest);
            int parent=tx_parent(f.paths[2],leaf);assert(parent>=0);
            assert(tx_read(parent,leaf,digest,0700,&data,&length) && length==7);free(data);close(parent);
        } else if(scenario==1) {
            assert(!unlink(f.paths[2]) && !symlink(f.paths[0],f.paths[2]));
            assert(!tx_run(&f.store,f.files,3,0) && tx_current(f.files)==0 && tx_current(f.files+1)==0);
        } else if(scenario==2) {
            char path[192];snprintf(path,sizeof(path),"%s/startup-tx/0.old",f.component);
            file_write(path,(const unsigned char*)"BAD",3,0600);
            assert(!tx_run(&f.store,f.files,3,0));all_state(&f,0);
        } else {
            struct tx_file changed[3];memcpy(changed,f.files,sizeof(changed));
            changed[2].bytes=(const unsigned char*)"other";tx_hash(changed[2].bytes,5,changed[2].new_sha);
            assert(!tx_prepare(&f.store,changed,3) && !tx_run(&f.store,changed,3,0));all_state(&f,0);
        }
        finish(&f);cases++;
    }
    init(&f);assert(tx_prepare(&f.store,f.files,3) && tx_run(&f.store,f.files,3,0));
    for(unsigned p=0;p<CL_PHASE_COUNT;p++) {
        if(p==CL_STOCK_VERIFIED || p==CL_HOST_REMOVE_INTENT || p==CL_DISABLED)continue;
        phase(&f,p);assert(!tx_run(&f.store,f.files,3,1));all_state(&f,1);
    }
    phase(&f,CL_STOCK_VERIFIED);assert(tx_run(&f.store,f.files,3,1));all_state(&f,0);
    finish(&f);cases++;
    init(&f);
    unsigned char *large=calloc(TX_MAX_BYTES+1u,1);assert(large);
    char large_sha[65],leaf[256];tx_hash(large,TX_MAX_BYTES+1u,large_sha);
    file_write(f.paths[2],large,TX_MAX_BYTES+1u,f.files[2].old_mode);free(large);
    int parent=tx_parent(f.paths[2],leaf);assert(parent>=0);
    /* The digest is correct: refusal must be due to the bounded file size. */
    assert(!tx_read(parent,leaf,large_sha,f.files[2].old_mode,NULL,NULL));close(parent);
    assert(tx_current(f.files)==0 && tx_current(f.files+1)==0);
    finish(&f);cases++;
    const unsigned char expected_recovery_plan[32]={2};
    for(unsigned scenario=0;scenario<6;scenario++) {
        init(&f);assert(tx_prepare(&f.store,f.files,3));
        if(scenario==0) {
            crash(&f,"renamed",0,1);
            assert(sr_resume(&f.store,f.files,3,expected_recovery_plan));all_state(&f,1);
        } else if(scenario==1) {
            assert(tx_run(&f.store,f.files,3,0));phase(&f,CL_ACTIVE);
            assert(sr_resume(&f.store,f.files,3,expected_recovery_plan));all_state(&f,1);
        } else if(scenario==2) {
            crash(&f,"renamed",0,1);phase(&f,CL_ACTIVE);
            assert(!sr_resume(&f.store,f.files,3,expected_recovery_plan));
            assert(tx_current(f.files)==1 && tx_current(f.files+1)==0);
        } else if(scenario==3) {
            assert(tx_run(&f.store,f.files,3,0));phase(&f,CL_HOST_REMOVE_INTENT);
            crash(&f,"renamed",2,2);
            assert(sr_resume(&f.store,f.files,3,expected_recovery_plan));all_state(&f,0);
            phase(&f,CL_DISABLED);assert(sr_resume(&f.store,f.files,3,expected_recovery_plan));
        } else if(scenario==4) {
            unsigned char foreign_plan[32]={3};
            assert(!sr_resume(&f.store,f.files,3,foreign_plan));all_state(&f,0);
        } else {
            assert(tx_run(&f.store,f.files,3,0));
            file_write(f.paths[2],(const unsigned char*)"FOREIGN",7,0700);
            assert(!sr_resume(&f.store,f.files,3,expected_recovery_plan));
            assert(tx_current(f.files)==1 && tx_current(f.files+1)==1);
        }
        finish(&f);cases++;
    }
    printf("PASS: %u startup transaction scenarios; snapshot/write/rename process exits, resumable apply/restore, exact modes, plan binding, foreign edit/symlink refusal and stock-before-restore gate\n",cases);
    return 0;
}
