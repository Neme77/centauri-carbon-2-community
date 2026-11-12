/* Low-level startup-file transaction for the qualified coordinator, NOT a CLI.
 * Caller holds lifecycle.lock and supplies a qualified fixed target list after
 * fresh idle/cold checks. No paths or byte payloads may come from browser input.
 * Stage all backup/new bytes durably before changing any startup target. */
#ifndef CC2_CANVAS_STARTUP_TX_H
#define CC2_CANVAS_STARTUP_TX_H
#include "journal.h"
#define TX_MAX_FILES 8u
#define TX_MAX_BYTES 65536u
#ifndef TX_FAULT
#define TX_FAULT(point,index) ((void)0)
#endif
struct tx_file {
    const char *path;
    const unsigned char *bytes;
    size_t length;
    char old_sha[65],new_sha[65];
    mode_t old_mode,new_mode;
};
static inline int tx_mode(mode_t mode) {
    return mode==0600 || mode==0644 || mode==0700 || mode==0755;
}
static inline int tx_hex(const char *text) {
    for(unsigned i=0;i<64;i++)if(!((text[i]>='0'&&text[i]<='9')||(text[i]>='a'&&text[i]<='f')))return 0;
    return text[64]==0;
}
static inline void tx_hash(const unsigned char *bytes,size_t length,char out[65]) {
    sha256_state hash;sha256_init(&hash);sha256_update(&hash,bytes,length);sha256_hex(&hash,out);
}
static inline int tx_parent(const char *path,char leaf[256]) {
    if(!path || path[0]!='/' || strlen(path)>512)return -1;
    char copy[513];memcpy(copy,path,strlen(path)+1);
    char *last=strrchr(copy,'/');
    if(!last || !last[1] || strlen(last+1)>=256)return -1;
    strcpy(leaf,last+1);*last=0;
    int directory=open("/",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(directory<0)return -1;
    if(last==copy)return directory;
    char *part=copy+1;
    while(*part) {
        char *end=strchr(part,'/');if(end)*end=0;
        if(!*part || !strcmp(part,".") || !strcmp(part,"..")){close(directory);return -1;}
        int next=openat(directory,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        close(directory);if(next<0)return -1;directory=next;
        if(!end)break;
        part=end+1;
    }
    if(!strcmp(leaf,".") || !strcmp(leaf,"..")){close(directory);return -1;}
    return directory;
}
static inline int tx_read(int directory,const char *name,const char *expected,mode_t mode,
                          unsigned char **bytes,size_t *length) {
    int fd=openat(directory,name,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK);
    if(fd<0)return 0;
    struct stat st;char digest[65];
    int okay=!fstat(fd,&st) && S_ISREG(st.st_mode) && st.st_uid==0 && st.st_nlink==1 &&
             (st.st_mode&07777)==mode && st.st_size>=0 && (uint64_t)st.st_size<=TX_MAX_BYTES && sha256_fd(fd,digest) && !strcmp(digest,expected);
    if(okay && bytes) {
        *length=(size_t)st.st_size;*bytes=malloc(*length?*length:1);
        okay=*bytes && lseek(fd,0,SEEK_SET)==0 && cl_io(fd,*bytes,*length,0);
        if(!okay){free(*bytes);*bytes=NULL;}
    }
    close(fd);return okay;
}
static inline int tx_plan(const struct tx_file *files,unsigned count,char digest[65]) {
    if(!files || !count || count>TX_MAX_FILES)return 0;
    sha256_state hash;sha256_init(&hash);
    for(unsigned i=0;i<count;i++) {
        const struct tx_file *f=files+i;char leaf[256],line[800],actual[65];
        if(!f->bytes || f->length>TX_MAX_BYTES || !tx_mode(f->old_mode) || !tx_mode(f->new_mode) ||
           !tx_hex(f->old_sha) || !tx_hex(f->new_sha))return 0;
        int directory=tx_parent(f->path,leaf);if(directory<0)return 0;close(directory);
        for(unsigned j=0;j<i;j++)if(!strcmp(f->path,files[j].path))return 0;
        tx_hash(f->bytes,f->length,actual);if(strcmp(actual,f->new_sha))return 0;
        int n=snprintf(line,sizeof(line),"%u %04o %04o %s %s %s\n",i,(unsigned)f->old_mode,(unsigned)f->new_mode,f->old_sha,f->new_sha,f->path);
        if(n<1 || (size_t)n>=sizeof(line))return 0;
        sha256_update(&hash,(const unsigned char*)line,(size_t)n);
    }
    sha256_hex(&hash,digest);return 1;
}
static inline int tx_directory(struct cl_store *store) {
    if(mkdirat(store->directory,"startup-tx",0700) && errno!=EEXIST)return -1;
    if(fsync(store->directory))return -1;
    int directory=openat(store->directory,"startup-tx",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(!cl_private(directory,1,0)){if(directory>=0)close(directory);return -1;}
    return directory;
}
static inline int tx_temporary(char *out,size_t size,unsigned index) {
    uint64_t nonce=0;int fd=open("/dev/urandom",O_RDONLY|O_CLOEXEC);
    if(fd<0)return 0;
    int okay=cl_io(fd,(unsigned char*)&nonce,sizeof(nonce),0);close(fd);
    if(!okay || !nonce)return 0;
    int n=snprintf(out,size,".cc2-rfid-%ld-%u-%016llx.next",(long)getpid(),index,(unsigned long long)nonce);
    return n>0 && (size_t)n<size;
}
static inline int tx_save(int directory,const char *name,const unsigned char *bytes,size_t length,const char *digest) {
    struct stat st;
    if(!fstatat(directory,name,&st,AT_SYMLINK_NOFOLLOW))return tx_read(directory,name,digest,0600,NULL,NULL);
    if(errno!=ENOENT)return 0;
    char temporary[96];if(!tx_temporary(temporary,sizeof(temporary),0))return 0;
    int fd=openat(directory,temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);
    if(fd<0)return 0;
    int okay=cl_io(fd,(unsigned char*)bytes,length,1) && !fsync(fd);
    if(close(fd))okay=0;
    if(okay)TX_FAULT("snapshot-written",0);
    if(okay && (!fstatat(directory,name,&st,AT_SYMLINK_NOFOLLOW) || errno!=ENOENT))okay=0;
    if(okay && renameat(directory,temporary,directory,name))okay=0;
    return okay && !fsync(directory);
}
static inline int tx_receipt(int directory,const char *name,const char digest[65],int write_it) {
    unsigned char bytes[65];char content_sha[65];memcpy(bytes,digest,64);bytes[64]='\n';
    tx_hash(bytes,sizeof(bytes),content_sha);
    return write_it?tx_save(directory,name,bytes,sizeof(bytes),content_sha):tx_read(directory,name,content_sha,0600,NULL,NULL);
}
static inline int tx_prepare(struct cl_store *store,const struct tx_file *files,unsigned count) {
    if(!store || store->directory<0 || store->lock<0)return 0;
    char plan[65];if(!tx_plan(files,count,plan))return 0;
    int snapshots=tx_directory(store);if(snapshots<0)return 0;
    /* A prepared plan is resumable, but never relabel it as a new plan. */
    struct stat st;
    if(!fstatat(snapshots,"ready",&st,AT_SYMLINK_NOFOLLOW)) {
        int okay=tx_receipt(snapshots,"ready",plan,0);close(snapshots);return okay;
    }
    if(errno!=ENOENT){close(snapshots);return 0;}
    int okay=1;
    for(unsigned i=0;okay && i<count;i++) {
        char leaf[256],name[32];unsigned char *old=NULL;size_t length=0;
        int parent=tx_parent(files[i].path,leaf);
        okay=parent>=0 && tx_read(parent,leaf,files[i].old_sha,files[i].old_mode,&old,&length);
        if(parent>=0)close(parent);
        if(okay) {
            snprintf(name,sizeof(name),"%u.old",i);okay=tx_save(snapshots,name,old,length,files[i].old_sha);
            if(okay){snprintf(name,sizeof(name),"%u.new",i);okay=tx_save(snapshots,name,files[i].bytes,files[i].length,files[i].new_sha);}
        }
        free(old);if(okay)TX_FAULT("snapshot",i);
    }
    if(okay)okay=tx_receipt(snapshots,"ready",plan,1);
    if(okay)TX_FAULT("ready",0);
    close(snapshots);return okay;
}
static inline int tx_current(const struct tx_file *file) {
    char leaf[256];int parent=tx_parent(file->path,leaf);if(parent<0)return -1;
    int value=tx_read(parent,leaf,file->new_sha,file->new_mode,NULL,NULL)?1:
              tx_read(parent,leaf,file->old_sha,file->old_mode,NULL,NULL)?0:-1;
    close(parent);return value;
}
static inline int tx_replace(const struct tx_file *file,unsigned index,int snapshots,int restore) {
    char source[32],leaf[256],temp[96];unsigned char *bytes=NULL;size_t length=0;
    snprintf(source,sizeof(source),"%u.%s",index,restore?"old":"new");
    const char *digest=restore?file->old_sha:file->new_sha;
    if(!tx_read(snapshots,source,digest,0600,&bytes,&length))return 0;
    int parent=tx_parent(file->path,leaf);if(parent<0){free(bytes);return 0;}
    int fd=tx_temporary(temp,sizeof(temp),index)?openat(parent,temp,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600):-1;
    int okay=fd>=0 && cl_io(fd,bytes,length,1) && !fchmod(fd,restore?file->old_mode:file->new_mode) && !fsync(fd);
    free(bytes);if(fd>=0 && close(fd))okay=0;
    if(okay)TX_FAULT("written",index);
    if(okay && tx_current(file)<0)okay=0;
    if(okay && renameat(parent,temp,parent,leaf))okay=0;
    if(okay && fsync(parent))okay=0;
    if(okay)TX_FAULT("renamed",index);
    /* Retain an uncertain temporary file; never remove an unexplained path. */
    close(parent);return okay;
}
static inline int tx_run(struct cl_store *store,const struct tx_file *files,unsigned count,int restore) {
    char plan[65];struct cl_record record;
    if(!store || store->directory<0 || store->lock<0)return 0;
    if(!tx_plan(files,count,plan) || !cl_store_read(store,&record))return 0;
    if(restore) {
        if(record.phase!=CL_STOCK_VERIFIED && record.phase!=CL_HOST_REMOVE_INTENT && record.phase!=CL_DISABLED)return 0;
    } else if(record.phase!=CL_HOST_STAGED && record.phase!=CL_FILTER_ACKED)return 0;
    int snapshots=tx_directory(store);if(snapshots<0)return 0;
    int okay=tx_receipt(snapshots,"ready",plan,0);
    /* Validate EVERY target and snapshot before writing the first target. */
    for(unsigned i=0;okay && i<count;i++) {
        char name[32];snprintf(name,sizeof(name),"%u.old",i);
        okay=tx_current(files+i)>=0 && tx_read(snapshots,name,files[i].old_sha,0600,NULL,NULL);
        snprintf(name,sizeof(name),"%u.new",i);
        if(okay)okay=tx_read(snapshots,name,files[i].new_sha,0600,NULL,NULL);
    }
    if(okay && fsync(snapshots))okay=0;
    for(unsigned pos=0;okay && pos<count;pos++) {
        unsigned i=restore?count-pos-1:pos;
        int current=tx_current(files+i);
        if(current<0){okay=0;break;}
        if(current!=(restore?0:1))okay=tx_replace(files+i,i,snapshots,restore);
    }
    if(okay)okay=tx_receipt(snapshots,restore?"restored":"applied",plan,1);
    close(snapshots);return okay;
}
#endif
