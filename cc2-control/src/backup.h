/* Bounded external application backup and recoverable selective restore.
 * Included after app_updates.h: reuses LAN confirmation and existing idle guards. */
#include "backup_format.h"

static const char *bk_names[4]={"preferences","materials","plates","spools"};
static size_t bk_limits[4]={512,8192,PLATES_FILE_MAX,SPOOLS_FILE_MAX};
static struct {
    char token[33];
    char *upload;
    size_t expected,received;
    backup_data *preview;
    uint32_t fingerprints[4];
    long long expires;
} backup_pending;

static const char *bk_target(int category) {
    return category==0?ui_preferences_path:category==1?material_presets_path:category==2?plates_path:spools_path;
}
static int bk_path(char *out,size_t cap,int category,const char *suffix) {
    return snprintf(out,cap,"%s%s",bk_target(category),suffix)<(int)cap;
}
static int bk_parent(const char *path,char *out,size_t cap) {
    const char *slash=strrchr(path,'/');
    size_t length=slash?(size_t)(slash-path):1;
    if(length>=cap)return 0;
    if(slash){memcpy(out,path,length);
        out[length]=0;
        if(!length)snprintf(out,cap,"/");
    }
    else snprintf(out,cap,".");
    struct stat st;
    return !lstat(out,&st)&&S_ISDIR(st.st_mode)&&st.st_uid==geteuid()&&!(st.st_mode&0022);
}
static int bk_sync_parent(const char *path) {
    char parent[PATH_MAX_LOCAL];
    if(!bk_parent(path,parent,sizeof(parent)))return 0;
    int fd=open(parent,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
    if(fd<0)return 0;
    int ok=!fsync(fd);
    if(close(fd))ok=0;
    return ok;
}
static int bk_safe_file(const char *path,int missing) {
    struct stat st;
    if(lstat(path,&st))return missing&&errno==ENOENT;
    return S_ISREG(st.st_mode)&&st.st_uid==geteuid()&&st.st_nlink==1&&!(st.st_mode&0022);
}
static char *bk_read(const char *path,size_t maximum,size_t *length,int *missing) {
    *missing=0;
    *length=0;
    int fd=open(path,O_RDONLY|O_NOFOLLOW);
    if(fd<0){*missing=errno==ENOENT;
        return NULL;
    }
    struct stat st;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||st.st_nlink!=1||
       (st.st_mode&0022)||st.st_size<0||(uint64_t)st.st_size>maximum){close(fd);
        return NULL;
    }
    char *data=malloc((size_t)st.st_size+1);
    if(!data){close(fd);
        return NULL;
    }
    size_t done=0;
    int ok=1;
    while(done<(size_t)st.st_size){ssize_t n=read(fd,data+done,(size_t)st.st_size-done);
        if(n<0&&errno==EINTR)continue;
        if(n<=0){ok=0;
            break;
        }done+=(size_t)n;
    }
    char extra;
    if(ok&&read(fd,&extra,1)!=0)ok=0;
    if(close(fd))ok=0;
    if(!ok){free(data);
        return NULL;
    }data[done]=0;
    *length=done;
    return data;
}
static int bk_write_new(const char *path,const char *data,size_t length) {
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
    if(fd<0)return 0;
    int ok=1;
    size_t done=0;
    while(done<length){ssize_t n=write(fd,data+done,length-done);
        if(n<0&&errno==EINTR)continue;
        if(n<=0){ok=0;
            break;
        }done+=(size_t)n;
    }
    if(fsync(fd))ok=0;
    if(close(fd))ok=0;
    if(!ok)unlink(path);
    return ok;
}
static int bk_unlink(const char *path) {
    struct stat st;
    if(lstat(path,&st))return errno==ENOENT;
    return bk_safe_file(path,0)&&!unlink(path);
}

/* No disk save is needed for export: these encode the authoritative in-memory
 * plate/spool stores, including consumption not yet due to be flushed. */
static int bk_capture(char *data[4],size_t lengths[4]) {
    memset(data,0,sizeof(char *)*4);
    memset(lengths,0,sizeof(size_t)*4);
    data[0]=malloc(512);
    if(!data[0])return 0;
    int n=snprintf(data[0],512,"{\"language\":\"%s\",\"theme\":\"%s\",\"quick1\":\"%s\",\"quick2\":\"%s\",\"quick3\":\"%s\",\"quick4\":\"%s\",\"camera_persistent\":%s}\n",
        preferences_language(),preferences_theme(),preferences_quick_action(0),preferences_quick_action(1),
        preferences_quick_action(2),preferences_quick_action(3),preferences_camera_persistent()?"true":"false");
    if(n<=0||n>=512)return 0;
    lengths[0]=(size_t)n;
    int missing=0;
    data[1]=bk_read(material_presets_path,8192,&lengths[1],&missing);
    if(!data[1]&&missing){data[1]=strdup(default_material_presets);
        lengths[1]=strlen(default_material_presets);
    }
    if(plates_available)data[2]=plates_encode(&plates,&lengths[2]);
    else data[2]=bk_read(plates_path,PLATES_FILE_MAX,&lengths[2],&missing);
    if(spools_available)data[3]=spools_encode_ex(&spools,&lengths[3],1);
    else data[3]=bk_read(spools_path,SPOOLS_FILE_MAX,&lengths[3],&missing);
    return data[1]&&data[2]&&data[3];
}
static int bk_sections(char *data[4],size_t lengths[4]) {
    if(!bk_capture(data,lengths)||!plates_available||!spools_available)return 0;
    /* Also refuse unreadable current data rather than export a misleading backup. */
    backup_data *check=calloc(1,sizeof(*check));
    if(!check)return 0;
    int ok=bk_json(data[0],lengths[0])&&bk_preferences(data[0],data[0]+lengths[0],check->preferences)&&
        bk_json(data[1],lengths[1])&&bk_materials(json_skip_space(data[1],data[1]+lengths[1]),
            data[1]+lengths[1],check->materials)&&
        bk_json(data[2],lengths[2])&&bk_plates(data[2],data[2]+lengths[2],&check->plate_data)&&
        bk_json(data[3],lengths[3])&&bk_spools(data[3],data[3]+lengths[3],&check->spool_data);
    free(check);
    return ok;
}
static void bk_free_sections(char *data[4]) {for(int i=0;i<4;i++)free(data[i]);
}
static char *bk_export(size_t *length) {
    char *parts[4];
    size_t sizes[4];
    int ok=bk_sections(parts,sizes);
    if(!ok){bk_free_sections(parts);
        return NULL;
    }
    char *data=malloc(BK_MAX);
    if(!data){bk_free_sections(parts);
        return NULL;
    }
    json_builder b={data,0,BK_MAX,0};
    json_builder_printf(&b,"{\"model\":\"CC2\",\"firmware\":\"" CC2_COMMUNITY_FIRMWARE_VERSION "\",\"version\":\"" CC2_CONTROL_VERSION "\",\"created\":%lld,\"sections\":{",(long long)time(NULL));
    for(int i=0;i<4;i++)json_builder_printf(&b,"%s\"%s\":%.*s",i?",":"",bk_names[i],(int)sizes[i],parts[i]);
    json_builder_printf(&b,"}}");
    bk_free_sections(parts);
    if(b.failed){free(data);
        return NULL;
    }
    char *result=malloc(b.length+128);
    if(!result){free(data);
        return NULL;
    }
    int written=snprintf(result,b.length+128,"{\"format\":\"cc2-control-backup\",\"schema\":1,\"crc32\":\"%08x\",\"data\":%s}\n",bk_crc(data,b.length),data);
    free(data);
    if(written<=0||(size_t)written>BK_MAX){free(result);
        return NULL;
    }
    *length=(size_t)written;
    return result;
}

/* The journal and old copies have fixed private names. Each new target is staged
 * in its own directory, so renames stay on the target filesystem. A durable
 * PREPARED journal means roll back ALL selected files before serving requests. */
static int bk_directory(char *out,size_t cap) {
    return bk_path(out,cap,0,".restore");
}
static int bk_local_path(char *out,size_t cap,const char *name) {
    char dir[PATH_MAX_LOCAL];
    return bk_directory(dir,sizeof(dir))&&snprintf(out,cap,"%s/%s",dir,name)<(int)cap;
}
static uint32_t bk_paths_crc(void) {
    char paths[PATH_MAX_LOCAL*4+8];
    int n=snprintf(paths,sizeof(paths),"%s\n%s\n%s\n%s",bk_target(0),bk_target(1),bk_target(2),bk_target(3));
    return n>0&&n<(int)sizeof(paths)?bk_crc(paths,(size_t)n):0;
}
static uint32_t bk_old_crc[4];
static int bk_journal(unsigned mask,unsigned present,const char *phase) {
    char path[PATH_MAX_LOCAL],temporary[PATH_MAX_LOCAL],data[160];
    if(!bk_local_path(path,sizeof(path),"journal")||!bk_local_path(temporary,sizeof(temporary),"journal.new")||!bk_unlink(temporary))return 0;
    int n=snprintf(data,sizeof(data),"CC2RESTORE1 %u %u %08x %08x %08x %08x %08x %s\n",mask,present,bk_paths_crc(),bk_old_crc[0],bk_old_crc[1],bk_old_crc[2],bk_old_crc[3],phase);
    return n>0&&n<(int)sizeof(data)&&bk_write_new(temporary,data,(size_t)n)&&!rename(temporary,path)&&bk_sync_parent(path);
}
static int (*bk_replace)(const char *,const char *)=rename;
/* tests inject a commit failure */
static int bk_rollback(unsigned mask,unsigned present) {
    char *data;
    size_t length;
    int missing,ok;
    /* Preflight every old copy and target before making any recovery change. */
    for(int i=0;i<4;i++)if(mask&(1U<<i)) {
        char old[PATH_MAX_LOCAL],name[16],parent[PATH_MAX_LOCAL];
        snprintf(name,sizeof(name),"%d.old",i);
        if(!bk_parent(bk_target(i),parent,sizeof(parent))||!bk_safe_file(bk_target(i),1)||
           !bk_local_path(old,sizeof(old),name))return 0;
        if(present&(1U<<i)){data=bk_read(old,bk_limits[i],&length,&missing);
            if(!data)return 0;
            int valid=bk_crc(data,length)==bk_old_crc[i];
            free(data);
            if(!valid)return 0;
        }
    }
    for(int i=0;i<4;i++)if(mask&(1U<<i)) {
        if(present&(1U<<i)) {
            char old[PATH_MAX_LOCAL],name[16],temporary[PATH_MAX_LOCAL];
            snprintf(name,sizeof(name),"%d.old",i);
            if(!bk_local_path(old,sizeof(old),name)||!bk_path(temporary,sizeof(temporary),i,".cc2-restore-recover")||!bk_unlink(temporary))return 0;
            data=bk_read(old,bk_limits[i],&length,&missing);
            if(!data)return 0;
            ok=bk_crc(data,length)==bk_old_crc[i]&&bk_write_new(temporary,data,length);
            free(data);
            if(!ok||rename(temporary,bk_target(i))||!bk_sync_parent(bk_target(i)))return 0;
        } else if(!bk_unlink(bk_target(i))||!bk_sync_parent(bk_target(i)))return 0;
    }
    return bk_journal(mask,present,"rolledback");
}
static int bk_recover(void) {
    char dir[PATH_MAX_LOCAL],journal[PATH_MAX_LOCAL];
    if(!bk_directory(dir,sizeof(dir))||!bk_local_path(journal,sizeof(journal),"journal"))return 0;
    struct stat st;
    if(lstat(dir,&st))return errno==ENOENT;
    if(!app_update_private(dir))return 0;
    int missing;
    size_t length;
    char *data=bk_read(journal,159,&length,&missing);
    if(!data)return missing;
    unsigned mask,present,crc;
    char phase[16];
    int consumed=0;
    int ok=sscanf(data,"CC2RESTORE1 %u %u %8x %8x %8x %8x %8x %15s%n",&mask,&present,&crc,&bk_old_crc[0],&bk_old_crc[1],&bk_old_crc[2],&bk_old_crc[3],phase,&consumed)==8&&
        consumed>0&&(size_t)consumed+1==length&&data[consumed]=='\n'&&mask>0&&mask<=15&&!(present&~mask)&&crc==bk_paths_crc();
    free(data);
    if(!ok)return 0;
    if(!strcmp(phase,"complete")||!strcmp(phase,"rolledback"))return 1;
    if(strcmp(phase,"prepared"))return 0;
    return bk_rollback(mask,present);
}

static int (*bk_stage_write)(const char *,const char *,size_t)=bk_write_new;
static int bk_transaction(unsigned mask,char *data[4],size_t sizes[4]) {
    char dir[PATH_MAX_LOCAL],parent[PATH_MAX_LOCAL];
    if(!bk_directory(dir,sizeof(dir))||!bk_parent(dir,parent,sizeof(parent))||!bk_recover())return 0;
    if(mkdir(dir,0700)&&errno!=EEXIST)return 0;
    if(!app_update_private(dir)||!bk_sync_parent(dir))return 0;
    /* Only the previous COMPLETE recovery set is replaced; prepared sets are
     * recovered above. No recursive deletion or caller-controlled path exists. */
    char file[PATH_MAX_LOCAL];
    if(!bk_local_path(file,sizeof(file),"journal")||!bk_unlink(file)||!bk_sync_parent(file))return 0;
    unsigned present=0;
    memset(bk_old_crc,0,sizeof(bk_old_crc));
    int prepared=0,ok=1;
    for(int i=0;i<4&&ok;i++)if(mask&(1U<<i)) {
        char old[PATH_MAX_LOCAL],name[16],stage[PATH_MAX_LOCAL];
        snprintf(name,sizeof(name),"%d.old",i);
        if(!bk_parent(bk_target(i),parent,sizeof(parent))||!bk_safe_file(bk_target(i),1)||
           !bk_local_path(old,sizeof(old),name)||!bk_unlink(old)||
           !bk_path(stage,sizeof(stage),i,".cc2-restore-new")||!bk_unlink(stage)){ok=0;
            break;
        }
        size_t n=0;
        int missing=0;
        char *before;
        if(i==2&&plates_available)before=plates_encode(&plates,&n);
        else if(i==3&&spools_available)before=spools_encode(&spools,&n);
        else before=bk_read(bk_target(i),bk_limits[i],&n,&missing);
        if(before){present|=1U<<i;
            bk_old_crc[i]=bk_crc(before,n);
            ok=bk_stage_write(old,before,n);
            free(before);
        }
        else if(!missing)ok=0;
        if(ok)ok=bk_stage_write(stage,data[i],sizes[i])&&bk_sync_parent(stage);
    }
    if(ok){ok=bk_sync_parent(file)&&bk_journal(mask,present,"prepared");
        prepared=ok;
    }
    for(int i=0;i<4&&ok;i++)if(mask&(1U<<i)) {
        char stage[PATH_MAX_LOCAL];
        ok=bk_path(stage,sizeof(stage),i,".cc2-restore-new")&&!bk_replace(stage,bk_target(i))&&bk_sync_parent(bk_target(i));
    }
    if(ok)ok=bk_journal(mask,present,"complete");
    if(!ok&&prepared&&!bk_rollback(mask,present))running=0;
    /* never expose uncertain mixed libraries */
    for(int i=0;i<4;i++) {
        char stage[PATH_MAX_LOCAL];
        if(bk_path(stage,sizeof(stage),i,".cc2-restore-new"))(void)bk_unlink(stage);
    }
    return ok;
}

static void bk_discard(void) {
    free(backup_pending.upload);
    free(backup_pending.preview);
    memset(&backup_pending,0,sizeof(backup_pending));
}
static void bk_tick(void) {
    if(backup_pending.expires&&monotonic_ms()>backup_pending.expires)bk_discard();
}
static int bk_ready(const mqtt_client *mqtt,console_state *console) {
    const char *state=uds_print_state(&telemetry);
    return !app_update_active()&&app_update_ready(mqtt,console)&&!plates_printer_ready(mqtt)&&
        !plates_cal.stage&&!plates.pending[0]&&!plates_reboot_requested&&!z_offset_pending&&
        !atomic_load(&upload_busy)&&!atomic_load(&active_downloads)&&!spools.job.active&&
        state&&strcmp(state,"printing")&&strcmp(state,"paused");
}
static int bk_number_body(const char *body,size_t length,size_t max,size_t *value) {
    if(!length||length>10)return 0;
    size_t n=0;
    for(size_t i=0;i<length;i++){
        if(body[i]<'0'||body[i]>'9'||n>(max-(size_t)(body[i]-'0'))/10)return 0;
        n=n*10+(size_t)(body[i]-'0');
    }
    if(n>max)return 0;
    *value=n;
    return 1;
}
static void bk_preview_response(int fd) {
    const backup_data *p=backup_pending.preview;
    char body[512];
    int n=snprintf(body,sizeof(body),"{\"token\":\"%s\",\"version\":\"%s\",\"created\":%lld,\"sections\":%u,\"plates\":%d,\"meshes\":%d,\"nozzles\":%d,\"spools\":%d,\"events\":%d,\"expires_in\":300}",
        backup_pending.token,p->version,p->created,p->sections,p->plate_data.count,p->plate_data.measure_count,
        p->plate_data.nozzle_count,p->spool_data.count,p->spool_data.log_count);
    respond(fd,200,"OK","application/json; charset=utf-8",body,(size_t)n);
}
static void bk_response(int fd,const char *path,const char *request,const char *body,size_t length,
                         const mqtt_client *mqtt,console_state *console) {
    bk_tick();
    if(!strcmp(path,"/api/backup/export")) {
        size_t n=0;
        char *data=bk_export(&n);
        if(!data){plates_fail(fd,503,"Application data is unavailable or invalid; backup was not created");
            return;
        }
        respond(fd,200,"OK","application/octet-stream",data,n);
        free(data);
        return;
    }
    if(!app_update_authorized(request,mqtt)){plates_fail(fd,401,"LAN code confirmation required or temporarily locked");
        return;
    }
    if(!strcmp(path,"/api/backup/import/cancel")) {
        char token[40];
        if(http_header(request,"X-CC2-Backup",token,sizeof(token))!=1||!backup_pending.token[0]||strcmp(token,backup_pending.token)){
            plates_fail(fd,409,"Restore preview expired or changed");
            return;
        }
        bk_discard();
        plates_reply(fd,200,"{\"cancelled\":true}");
        return;
    }
    if(!bk_ready(mqtt,console)){plates_fail(fd,409,"Restore requires a fresh connected idle cold printer with no pending operation");
        return;
    }
    if(!strcmp(path,"/api/backup/import/start")) {
        size_t size;
        if(!bk_number_body(body,length,BK_MAX,&size)||size<20){plates_fail(fd,400,"Invalid backup size (maximum 512 KiB)");
            return;
        }
        if(backup_pending.expires){plates_fail(fd,409,"Another backup preview is open; cancel it first");
            return;
        }
        unsigned char random[16];
        int entropy=open("/dev/urandom",O_RDONLY);
        ssize_t got=entropy>=0?read(entropy,random,sizeof(random)):-1;
        if(entropy>=0)close(entropy);
        if(got!=(ssize_t)sizeof(random)){plates_fail(fd,503,"Cannot create restore session");
            return;
        }
        backup_pending.upload=malloc(size+1);
        if(!backup_pending.upload){plates_fail(fd,503,"Not enough memory for backup preview");
            return;
        }
        for(int i=0;i<16;i++)snprintf(backup_pending.token+i*2,3,"%02x",random[i]);
        backup_pending.expected=size;
        backup_pending.expires=monotonic_ms()+300000;
        char reply[96];
        snprintf(reply,sizeof(reply),"{\"token\":\"%s\",\"chunk_size\":8192}",backup_pending.token);
        plates_reply(fd,200,reply);
        return;
    }
    char token[40];
    if(http_header(request,"X-CC2-Backup",token,sizeof(token))!=1||!backup_pending.token[0]||strcmp(token,backup_pending.token)){
        plates_fail(fd,409,"Restore preview expired or changed");
        return;
    }
    if(!strcmp(path,"/api/backup/import/chunk")) {
        char offset[24];
        size_t n;
        if(!backup_pending.upload||!length||length>8192||http_header(request,"X-CC2-Offset",offset,sizeof(offset))!=1||
           !bk_number_body(offset,strlen(offset),BK_MAX,&n)||n!=backup_pending.received||
           length>backup_pending.expected-backup_pending.received){plates_fail(fd,400,"Invalid or out-of-order backup chunk");
            return;
        }
        memcpy(backup_pending.upload+backup_pending.received,body,length);
        backup_pending.received+=length;
        backup_pending.expires=monotonic_ms()+300000;
        plates_reply(fd,200,"{\"received\":true}");
        return;
    }
    if(!strcmp(path,"/api/backup/import/finish")) {
        if(!backup_pending.upload||backup_pending.received!=backup_pending.expected){plates_fail(fd,400,"Incomplete backup upload");
            return;
        }
        backup_pending.upload[backup_pending.received]=0;
        backup_pending.preview=calloc(1,sizeof(backup_data));
        int ok=backup_pending.preview&&bk_parse(backup_pending.upload,backup_pending.received,backup_pending.preview);
        if(!ok){bk_discard();
            plates_fail(fd,400,"Invalid, incompatible, corrupt or unsupported application backup");
            return;
        }
        free(backup_pending.upload);
        backup_pending.upload=NULL;
        char *current[4];
        size_t sizes[4];
        ok=bk_capture(current,sizes);
        if(ok)for(int i=0;i<4;i++)backup_pending.fingerprints[i]=bk_crc(current[i],sizes[i]);
        bk_free_sections(current);
        if(!ok){bk_discard();
            plates_fail(fd,503,"Current application libraries cannot be validated");
            return;
        }
        backup_pending.expires=monotonic_ms()+300000;
        bk_preview_response(fd);
        return;
    }
    if(!strcmp(path,"/api/backup/import/restore")) {
        size_t mask;
        if(!backup_pending.preview||!bk_number_body(body,length,15,&mask)||!mask||
           (mask&~backup_pending.preview->sections)){plates_fail(fd,400,"Select only categories present in the verified preview");
            return;
        }
        char *current[4];
        size_t sizes[4];
        int ok=bk_capture(current,sizes);
        if(ok)for(int i=0;i<4;i++)if((mask&(1U<<i))&&bk_crc(current[i],sizes[i])!=backup_pending.fingerprints[i])ok=0;
        bk_free_sections(current);
        if(!ok){plates_fail(fd,409,"Application data changed after preview; upload the backup again");
            return;
        }
        backup_data *p=backup_pending.preview;
        bk_inert(p);
        char *next[4]={NULL,NULL,NULL,NULL};
        size_t lengths[4]={0,0,0,0};
        if(mask&BK_PREFS){next[0]=strdup(p->preferences);
            lengths[0]=strlen(p->preferences);
        }
        if(mask&BK_MATERIALS){next[1]=strdup(p->materials);
            lengths[1]=strlen(p->materials);
        }
        if(mask&BK_PLATES)next[2]=plates_encode(&p->plate_data,&lengths[2]);
        if(mask&BK_SPOOLS)next[3]=spools_encode(&p->spool_data,&lengths[3]);
        for(int i=0;i<4;i++)if((mask&(1U<<i))&&!next[i])ok=0;
        if(ok)ok=bk_ready(mqtt,console)&&bk_transaction((unsigned)mask,next,lengths);
        bk_free_sections(next);
        if(!ok){plates_fail(fd,503,"Restore failed; previous files were retained or recovery is required before service startup");
            return;
        }
        if(mask&BK_PLATES){plates=p->plate_data;
            plates_available=1;
            plates_error="";
            plates_result="";
            plates_pending_stage=0;
            plates_z_valid=0;
        }
        if(mask&BK_SPOOLS){spools=p->spool_data;
            spoolman_epoch++;
            spools_available=1;
            spools_error="";
            spools_dirty=spools_urgent=0;
            spools_unsaved=0;
            spools_revision++;
            spools_canvas_seen=0;
        }
        bk_discard();
        plates_reply(fd,200,"{\"restored\":true,\"physical_verification_required\":true}");
        return;
    }
    plates_fail(fd,404,"Unknown backup operation");
}
