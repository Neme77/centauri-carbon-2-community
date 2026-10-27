#ifndef CC2_EXTERNAL_FEED_STATE_H
#define CC2_EXTERNAL_FEED_STATE_H
/* Separate from schema-2 experiment choices so older application rollbacks
 * retain their original settings. A preference does not attest native mode. */
static int external_feed_enabled, external_source_selected;
static int external_feed_storage_valid=1;
static const char *external_feed_path="/opt/usr/cc2-external-feed.json";
static int external_feed_read(int *enabled,int *source){
    *enabled=*source=0;
    int fd=open(external_feed_path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return errno==ENOENT;
    struct stat st;char body[96],expected[96];
    int valid=!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&!(st.st_mode&0077)&&st.st_size>0&&st.st_size<(off_t)sizeof(body);
    ssize_t n=valid?read(fd,body,sizeof(body)-1):-1;close(fd);
    if(!valid||n!=st.st_size)return 0;
    body[n]=0;
    for(int e=0;e<=1;e++)for(int s=0;s<=e;s++){
        snprintf(expected,sizeof(expected),"{\"schema\":1,\"enabled\":%s,\"external\":%s}\n",e?"true":"false",s?"true":"false");
        if(!strcmp(expected,body)){*enabled=e;*source=s;return 1;}
    }
    return 0;
}
static void external_feed_load(void){
    external_feed_storage_valid=external_feed_read(&external_feed_enabled,&external_source_selected);
    if(!external_feed_storage_valid)external_feed_enabled=external_source_selected=0;
}
static int external_feed_store(int enabled,int source){
    int actual_enabled,actual_source;
    if(!external_feed_storage_valid||source<0||source>enabled||enabled<0||enabled>1||
       !external_feed_read(&actual_enabled,&actual_source)||actual_enabled!=external_feed_enabled||actual_source!=external_source_selected)return 0;
    char temporary[600],body[96];
    if(snprintf(temporary,sizeof(temporary),"%s.new",external_feed_path)>=(int)sizeof(temporary))return 0;
    snprintf(body,sizeof(body),"{\"schema\":1,\"enabled\":%s,\"external\":%s}\n",enabled?"true":"false",source?"true":"false");
    int fd=open(temporary,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);if(fd<0)return 0;
    int ok=write(fd,body,strlen(body))==(ssize_t)strlen(body);
    if(fsync(fd))ok=0;
    if(close(fd))ok=0;
    if(ok&&(!external_feed_read(&actual_enabled,&actual_source)||actual_enabled!=external_feed_enabled||actual_source!=external_source_selected))ok=0;
    if(ok&&rename(temporary,external_feed_path))ok=0;
    if(!ok){unlink(temporary);return 0;}
    external_feed_enabled=enabled;external_source_selected=source;return 1;
}
#endif
