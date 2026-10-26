#ifndef CC2_COMMUNITY_FIRMWARE_VERSION
#define CC2_COMMUNITY_FIRMWARE_VERSION "4.4"
#endif
#define main cc2_application_main
#include "../src/main.c"
#undef main
#include <assert.h>

static int rename_count,fail_rename,crash_rename,write_count,fail_write,rpcs;
static int replace_fixture(const char *from,const char *to) {
    rename_count++;
    if(fail_rename&&rename_count==fail_rename){errno=ENOSPC;
        return -1;
    }
    int result=rename(from,to);
    if(!result&&crash_rename&&rename_count==crash_rename)_exit(73);
    return result;
}
static int write_fixture(const char *path,const char *data,size_t length) {
    if(fail_write&&++write_count==fail_write){errno=ENOSPC;
        return 0;
    }
    return bk_write_new(path,data,length);
}
static int no_rpc(const char *query,char **reply,size_t *length) {
    (void)query;
    (void)reply;
    (void)length;
    rpcs++;
    return -1;
}
static void checksum(char *text) {
    const char *end=text+strlen(text),*de,*data=json_member_object(text,end,"data",'{',&de);
    assert(data);
    char hex[9];
    snprintf(hex,sizeof(hex),"%08x",bk_crc(data,(size_t)(de-data)));
    char *where=strstr(text,"\"crc32\":\"");
    assert(where);
    memcpy(where+9,hex,8);
}
static char *read_target(int i) {
    size_t n;
    int missing;
    char *p=bk_read(bk_target(i),bk_limits[i],&n,&missing);
    assert(p);
    return p;
}
static void check_old(char *old[4]) {
    for(int i=0;i<4;i++){char *data=read_target(i);
        assert(!strcmp(data,old[i]));
        free(data);
    }
}
static void seed(void) {
    plates_load();
    spools_load();
    assert(plates_available&&spools_available);
    plate_nozzle *n=&plates.nozzles[0];
    plates.nozzle_count=1;
    snprintf(n->id,sizeof(n->id),"2222222222222222");
    snprintf(n->name,sizeof(n->name),"0.4 Brass");
    n->diameter=.4;
    n->z=.025;
    plate_entry *p=&plates.plates[0];
    plates.count=1;
    snprintf(p->id,sizeof(p->id),"3333333333333333");
    snprintf(p->name,sizeof(p->name),"Textured PEI");
    p->side='A';
    p->z=.01;
    snprintf(p->measure,sizeof(p->measure),"4444444444444444");
    plate_entry *empty=&plates.plates[1];
    plates.count=2;
    snprintf(empty->id,sizeof(empty->id),"5555555555555555");
    snprintf(empty->name,sizeof(empty->name),"Uncalibrated B");
    empty->side='B';
    plate_measure *m=&plates.measures[0];
    plates.measure_count=1;
    snprintf(m->id,sizeof(m->id),"%s",p->measure);
    snprintf(m->plate,sizeof(m->plate),"%s",p->id);
    snprintf(m->nozzle,sizeof(m->nozzle),"%s",n->id);
    m->temp=60;
    m->measured=1700000000;
    m->mesh.x_count=m->mesh.y_count=3;
    m->mesh.x_pps=m->mesh.y_pps=3;
    m->mesh.min_x=m->mesh.min_y=6;
    m->mesh.max_x=m->mesh.max_y=246;
    m->mesh.tension=.2;
    snprintf(m->mesh.algo,sizeof(m->mesh.algo),"bicubic");
    for(int i=0;i<9;i++)m->mesh.points[i]=.001*i;
    snprintf(plates.current,sizeof(plates.current),"%s",p->id);
    snprintf(plates.nozzle,sizeof(plates.nozzle),"%s",n->id);
    assert(!plates_save());
    spool_entry *sp=&spools.spools[0];
    spools.count=1;
    spools.enabled=1;
    snprintf(sp->id,sizeof(sp->id),"1111111111111111");
    snprintf(sp->name,sizeof(sp->name),"PLA matte");
    snprintf(sp->material,sizeof(sp->material),"PLA");
    snprintf(sp->color,sizeof(sp->color),"#abcdef");
    sp->net=1000;
    sp->remaining=998;
    sp->diameter=1.75;
    sp->density=1.24;
    sp->created=1700000000;
    snprintf(spools.slots[1].spool,sizeof(spools.slots[1].spool),"%s",sp->id);
    spools.log_count=1;
    spool_event *e=&spools.log[0];
    e->kind='p';
    e->slot=1;
    e->time=1700000001;
    e->grams=-2;
    snprintf(e->spool,sizeof(e->spool),"%s",sp->id);
    assert(!spools_save());
    sp->remaining=991.875;
    spools_dirty=1;
    spools_unsaved=6.125;
    const char *prefs="{\"language\":\"pl\",\"theme\":\"nord\",\"quick1\":\"home:ALL\",\"quick2\":\"light:toggle\",\"quick3\":\"system:fans_off\",\"quick4\":\"page:bed\",\"camera_persistent\":true}\n";
    assert(bk_write_new(ui_preferences_path,prefs,strlen(prefs)));
    assert(bk_write_new(material_presets_path,default_material_presets,strlen(default_material_presets)));
    const char *secret="PRIVATE-LAN-AND-MQTT-CREDENTIALS";
    assert(bk_write_new(mqtt_config_path,secret,strlen(secret)));
}
static char *api_call(const char *path,const char *extra,const char *body,size_t length,
                      mqtt_client *mqtt,console_state *console,int status) {
    int pair[2];
    assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    char request[1024];
    snprintf(request,sizeof(request),"POST %s HTTP/1.1\r\nHost: localhost\r\n%s\r\n",path,extra);
    bk_response(pair[0],path,request,body,length,mqtt,console);
    shutdown(pair[0],SHUT_WR);
    char *reply=calloc(1,BK_MAX+4096);
    assert(reply);
    size_t used=0;
    ssize_t n;
    while((n=read(pair[1],reply+used,BK_MAX+4095-used))>0)used+=(size_t)n;
    char prefix[32];
    snprintf(prefix,sizeof(prefix),"HTTP/1.1 %d ",status);
    assert(!strncmp(reply,prefix,strlen(prefix)));
    close(pair[0]);
    close(pair[1]);
    return reply;
}
static void api_guards_and_preview(const char *backup,size_t length) {
    mqtt_client mqtt;
    mqtt_init(&mqtt);
    mqtt.connected=mqtt.registered=mqtt.have_machine_status=1;
    mqtt.machine_status=1;
    mqtt.last_message=time(NULL);
    snprintf(mqtt.password,sizeof(mqtt.password),"TEST-CODE");
    mqtt.have_extruder_temp=mqtt.have_extruder_target=mqtt.have_bed_temp=mqtt.have_bed_target=1;
    mqtt.extruder_temp=mqtt.bed_temp=25;
    mqtt.extruder_target=mqtt.bed_target=0;
    console_state console;
    console_init(&console,"/tmp/not-a-printer");
    recovery_console=&console;
    int pair[2];
    assert(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));
    uds_init(&telemetry);
    telemetry.fd=pair[0];
    const char *status="{\"id\":11,\"result\":{\"eventtime\":1,\"status\":{\"print_stats\":{\"state\":\"standby\"}}}}";
    assert(uds_message(&telemetry,status,strlen(status)));
    assert(bk_ready(&mqtt,&console));
    mqtt.machine_status=2;
    assert(!bk_ready(&mqtt,&console));
    mqtt.machine_status=1;
    mqtt.extruder_temp=90;
    assert(!bk_ready(&mqtt,&console));
    mqtt.extruder_temp=25;
    mqtt.last_message-=60;
    assert(!bk_ready(&mqtt,&console));
    mqtt.last_message=time(NULL);
    telemetry.last_rx.tv_sec-=10;
    assert(!bk_ready(&mqtt,&console));
    clock_gettime(CLOCK_MONOTONIC,&telemetry.last_rx);
    console.busy=1;
    assert(!bk_ready(&mqtt,&console));
    console.busy=0;
    z_offset_pending=1;
    assert(!bk_ready(&mqtt,&console));
    z_offset_pending=0;
    atomic_store(&upload_busy,1);
    assert(!bk_ready(&mqtt,&console));
    atomic_store(&upload_busy,0);
    plates_cal.stage=CAL_PROBING;
    assert(!bk_ready(&mqtt,&console));
    plates_cal.stage=0;
    char size[24];
    snprintf(size,sizeof(size),"%zu",length);
    char *response=api_call("/api/backup/import/start","X-CC2-LAN: wrong\r\n",size,strlen(size),&mqtt,&console,401);
    free(response);
    response=api_call("/api/backup/import/start","X-CC2-LAN: TEST-CODE\r\n",size,strlen(size),&mqtt,&console,200);
    char token[33];
    assert(extract_json_string(strstr(response,"\r\n\r\n")+4,"token",token,sizeof(token)));
    free(response);
    char headers[160];
    snprintf(headers,sizeof(headers),"X-CC2-LAN: TEST-CODE\r\nX-CC2-Backup: %s\r\nX-CC2-Offset: 1\r\n",token);
    response=api_call("/api/backup/import/chunk",headers,backup,10,&mqtt,&console,400);
    free(response);
    assert(!backup_pending.received);
    for(size_t offset=0;offset<length;offset+=8192) {
        size_t chunk=length-offset;
        if(chunk>8192)chunk=8192;
        snprintf(headers,sizeof(headers),"X-CC2-LAN: TEST-CODE\r\nX-CC2-Backup: %s\r\nX-CC2-Offset: %zu\r\n",token,offset);
        response=api_call("/api/backup/import/chunk",headers,backup+offset,chunk,&mqtt,&console,200);
        free(response);
    }
    response=api_call("/api/backup/import/finish",headers,"",0,&mqtt,&console,200);
    assert(strstr(response,"\"sections\":15"));
    free(response);
    assert(!backup_pending.upload&&backup_pending.preview);
    /* Edits after preview cannot be silently replaced. Other categories remain selectable. */
    spools.spools[0].remaining-=1;
    response=api_call("/api/backup/import/restore",headers,"8",1,&mqtt,&console,409);
    free(response);
    char *before_material=read_target(1);
    response=api_call("/api/backup/import/restore",headers,"1",1,&mqtt,&console,200);
    free(response);
    char *after_material=read_target(1);
    assert(!strcmp(before_material,after_material));
    free(before_material);
    free(after_material);
    assert(!backup_pending.preview&&!backup_pending.token[0]&&rpcs==0);
    backup_pending.upload=malloc(32);
    backup_pending.expires=monotonic_ms()-1;
    bk_tick();
    assert(!backup_pending.upload);
    uds_close(&telemetry);
    close(pair[1]);
    console_destroy(&console);
    recovery_console=NULL;
    puts("PASS backup API: LAN, hot/printing/stale/busy guards, ordered chunks, server preview, edit conflict, selective restore and expiry");
}
int main(void) {
    char directory[]="./backup-XXXXXX";
    assert(mkdtemp(directory)&&!chdir(directory));
    assert(bk_crc("123456789",9)==0xcbf43926U);
    const char *bad_json[]={"{\"a\":1,\"a\":2}","{\"a\":01}","{\"a\":NaN}","{\"a\":1e999}","{\"a\":1,}","[1,]","{\"a\":]}","{\"a\":\"\\ud800\"}","{\"\\u0061\":1}","{} trailing"};
    for(size_t i=0;i<sizeof(bad_json)/sizeof(bad_json[0]);i++)assert(!bk_json(bad_json[i],strlen(bad_json[i])));
    assert(bk_json("{\"a\":\"\\ud83d\\ude00\"}",20));
    seed();
    plates_uds=no_rpc;
    size_t length;
    char *backup=bk_export(&length);
    assert(backup&&length< BK_MAX);
    assert(!strstr(backup,"PRIVATE-LAN")&&!strstr(backup,"password")&&!strstr(backup,"autosave.cfg"));
    backup_data *data=calloc(1,sizeof(*data));
    assert(data&&bk_parse(backup,length,data));
    assert(data->sections==15&&data->plate_data.count==2&&data->plate_data.measure_count==1);
    assert(data->spool_data.spools[0].remaining==991.875&&data->spool_data.log_count==1);
    char *stored=read_target(3);
    assert(strstr(stored,"998.000"));
    free(stored);
    assert(spools_dirty&&spools_unsaved==6.125);
    assert(plates.current[0]&&spools.slots[1].spool[0]&&rpcs==0);
    char *bad=malloc(length+64);
    assert(bad);
    snprintf(bad,length+64,"{\"schema\":1,%s",backup+1);
    assert(!bk_parse(bad,strlen(bad),data));
    memcpy(bad,backup,length+1);
    char *model=strstr(bad,"\"model\":\"CC2\"");
    assert(model);
    model[11]='3';
    checksum(bad);
    assert(!bk_parse(bad,length,data));
    memcpy(bad,backup,length+1);
    char *firmware=strstr(bad,"\"firmware\":\"4.4\"");
    assert(firmware);
    firmware[14]='3';
    checksum(bad);
    assert(!bk_parse(bad,length,data));
    memcpy(bad,backup,length+1);
    char *ref=strstr(bad,"\"plate\":\"3333333333333333\"");
    assert(ref);
    ref[9]='9';
    checksum(bad);
    assert(!bk_parse(bad,length,data));
    memcpy(bad,backup,length+1);
    bad[length/2]^=1;
    assert(!bk_parse(bad,length,data));
    assert(bk_parse(backup,length,data));
    bk_inert(data);
    assert(!data->plate_data.current[0]&&!data->plate_data.pending[0]&&!data->plate_data.nozzle[0]);
    assert(data->plate_data.plates[0].z==.01&&data->plate_data.measures[0].mesh.points[8]==.008);
    assert(!data->spool_data.enabled&&!data->spool_data.job.active&&!data->spool_data.slots[1].spool[0]);
    assert(!strcmp(data->spool_data.slots[1].last,"1111111111111111")&&data->spool_data.log_count==1);
    char *parts[4]={strdup(data->preferences),strdup(data->materials),NULL,NULL};
    size_t sizes[4]={strlen(parts[0]),strlen(parts[1]),0,0};
    parts[2]=plates_encode(&data->plate_data,&sizes[2]);
    parts[3]=spools_encode(&data->spool_data,&sizes[3]);
    assert(parts[2]&&parts[3]);
    assert(bk_transaction(15,parts,sizes));
    char recovery_path[PATH_MAX_LOCAL];
    assert(bk_local_path(recovery_path,sizeof(recovery_path),"3.old"));
    size_t recovery_length;
    int recovery_missing;
    char *recovery_copy=bk_read(recovery_path,SPOOLS_FILE_MAX,&recovery_length,&recovery_missing);
    assert(recovery_copy&&strstr(recovery_copy,"991.875"));
    free(recovery_copy);
    plates=data->plate_data;
    spools=data->spool_data;
    spools_dirty=0;
    assert(bk_recover());
    assert(rpcs==0);
    char *old[4];
    for(int i=0;i<4;i++)old[i]=read_target(i);
    free(parts[0]);
    parts[0]=strdup("{\"language\":\"it\"}\n");
    sizes[0]=strlen(parts[0]);
    bk_replace=replace_fixture;
    fail_rename=2;
    rename_count=0;
    assert(!bk_transaction(15,parts,sizes));
    assert(bk_recover());
    check_old(old);
    assert(running);
    fail_rename=0;
    bk_stage_write=write_fixture;
    fail_write=2;
    write_count=0;
    assert(!bk_transaction(15,parts,sizes));
    check_old(old);
    assert(bk_recover());
    fail_write=0;
    /* Crash immediately after one target rename; the next process restores ALL. */
    pid_t child=fork();
    assert(child>=0);
    if(!child){rename_count=0;
        crash_rename=1;
        (void)bk_transaction(15,parts,sizes);
        _exit(99);
    }
    int status;
    assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==73);
    assert(bk_recover());
    check_old(old);
    assert(bk_recover());
    assert(!rename(ui_preferences_path,"prefs.original"));
    assert(!symlink(mqtt_config_path,ui_preferences_path));
    assert(!bk_transaction(1,parts,sizes));
    assert(!unlink(ui_preferences_path));
    assert(!rename("prefs.original",ui_preferences_path));
    check_old(old);
    /* Missing old targets are removed, not invented, during startup recovery. */
    assert(!mkdir("missing-targets",0700)&&!chdir("missing-targets"));
    child=fork();
    assert(child>=0);
    if(!child){rename_count=0;
        crash_rename=1;
        (void)bk_transaction(3,parts,sizes);
        _exit(99);
    }
    assert(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==73);
    assert(bk_recover());
    assert(access(ui_preferences_path,F_OK)&&access(material_presets_path,F_OK));
    assert(!chdir(".."));
    api_guards_and_preview(backup,length);
    for(int i=0;i<4;i++)free(old[i]);
    bk_free_sections(parts);
    free(data);
    free(bad);
    free(backup);
    puts("PASS backup: unsaved inventory export, exact meshes, credentials exclusion, pure validation, inert import, failed-write/rename and crash recovery, symlink refusal");
    return 0;
}
