/* Application backup format v1. Validation without file writes, global-store changes or printer RPCs. */
#define BK_MAX (512U * 1024U)
#define BK_PREFS 1U
#define BK_MATERIALS 2U
#define BK_PLATES 4U
#define BK_SPOOLS 8U

typedef struct {
    unsigned sections;
    char version[32];
    long long created;
    char preferences[512], materials[8193];
    plate_store plate_data;
    spool_store spool_data;
} backup_data;

static uint32_t bk_crc(const char *data, size_t length) {
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < length; i++) {
        crc ^= (unsigned char)data[i];
        for (int b = 0; b < 8; b++) crc = (crc >> 1) ^ (0xedb88320U & (0U - (crc & 1U)));
    }
    return ~crc;
}

/* Strict JSON before existing typed readers: duplicates, nonfinite numbers,
 * malformed/trailing data, excessive recursion and escaped member names fail. */
static const char *bk_string(const char *p, const char *end, int key) {
    if (p >= end || *p++ != '"') return NULL;
    while (p < end) {
        unsigned char ch = (unsigned char)*p++;
        if (ch == '"') return p;
        if (ch < 32) return NULL;
        if (key && !isalnum(ch) && ch != '_') return NULL;
        if (ch == '\\') {
            if (key || p >= end) return NULL;
            ch = (unsigned char)*p++;
            if (strchr("\"\\/bfnrt", ch)) continue;
            if (ch != 'u' || end - p < 4) return NULL;
            unsigned code = 0;
            for (int i = 0; i < 4; i++) {
                ch = (unsigned char)*p++;
                if (!isxdigit(ch)) return NULL;
                code = code * 16U + (unsigned)(isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10);
            }
            if (code >= 0xd800 && code <= 0xdbff) {
                if (end - p < 6 || p[0] != '\\' || p[1] != 'u') return NULL;
                p += 2;
                unsigned low = 0;
                for (int i = 0; i < 4; i++) {
                    ch = (unsigned char)*p++;
                    if (!isxdigit(ch)) return NULL;
                    low = low * 16U + (unsigned)(isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10);
                }
                if (low < 0xdc00 || low > 0xdfff) return NULL;
            } else if (code >= 0xdc00 && code <= 0xdfff) return NULL;
        } else if (ch >= 128) {
            unsigned value, extra;
            if (key) return NULL;
            if (ch >= 0xc2 && ch <= 0xdf) { value = ch & 31U;
                extra = 1;
            }
            else if (ch >= 0xe0 && ch <= 0xef) { value = ch & 15U;
                extra = 2;
            }
            else if (ch >= 0xf0 && ch <= 0xf4) { value = ch & 7U;
                extra = 3;
            }
            else return NULL;
            if ((size_t)(end - p) < extra) return NULL;
            for (unsigned i = 0; i < extra; i++) {
                ch = (unsigned char)*p++;
                if ((ch & 0xc0) != 0x80) return NULL;
                value = (value << 6) | (ch & 63U);
            }
            if ((extra == 2 && value < 0x800) || (extra == 3 && value < 0x10000) ||
                value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return NULL;
        }
    }
    return NULL;
}

static const char *bk_value(const char *p, const char *end, unsigned depth, unsigned *nodes) {
    p = json_skip_space(p, end);
    if (p >= end || depth > 20 || ++*nodes > 50000) return NULL;
    if (*p == '"') return bk_string(p, end, 0);
    if (*p == '{' || *p == '[') {
        int object = *p == '{';
        char close = object ? '}' : ']';
        const char *keys[40];
        size_t lengths[40];
        unsigned count = 0;
        p = json_skip_space(p + 1, end);
        if (p < end && *p == close) return p + 1;
        while (p < end) {
            if (object) {
                const char *ke = bk_string(p, end, 1);
                if (!ke || ke - p > 66 || count >= 40) return NULL;
                size_t len = (size_t)(ke - p);
                for (unsigned i = 0; i < count; i++)
                    if (lengths[i] == len && !memcmp(keys[i], p, len)) return NULL;
                keys[count] = p;
                lengths[count++] = len;
                p = json_skip_space(ke, end);
                if (p >= end || *p++ != ':') return NULL;
            }
            p = bk_value(p, end, depth + 1, nodes);
            if (!p) return NULL;
            p = json_skip_space(p, end);
            if (p < end && *p == close) return p + 1;
            if (p >= end || *p++ != ',') return NULL;
            p = json_skip_space(p, end);
        }
        return NULL;
    }
    if (end - p >= 4 && (!memcmp(p, "true", 4) || !memcmp(p, "null", 4))) return p + 4;
    if (end - p >= 5 && !memcmp(p, "false", 5)) return p + 5;
    const char *start = p;
    if (*p == '-') p++;
    if (p >= end || !isdigit((unsigned char)*p)) return NULL;
    if (*p == '0') p++;
    else while (p < end && isdigit((unsigned char)*p)) p++;
    if (p < end && *p == '.') {
        p++;
        if (p >= end || !isdigit((unsigned char)*p)) return NULL;
        while (p < end && isdigit((unsigned char)*p)) p++;
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
        p++;
        if (p < end && (*p == '+' || *p == '-')) p++;
        if (p >= end || !isdigit((unsigned char)*p)) return NULL;
        while (p < end && isdigit((unsigned char)*p)) p++;
    }
    if (p - start > 48) return NULL;
    char number[49];
    memcpy(number, start, (size_t)(p - start));
    number[p - start] = 0;
    char *tail;
    errno = 0;
    double value = strtod(number, &tail);
    return !errno && !*tail && isfinite(value) ? p : NULL;
}

static int bk_json(const char *text, size_t length) {
    if (!text || !length || length > BK_MAX) return 0;
    unsigned nodes = 0;
    const char *end = text + length;
    const char *p = bk_value(text, end, 0, &nodes);
    return p && json_skip_space(p, end) == end;
}

static int bk_keys(const char *o, const char *end, const char *allowed) {
    if (!o || *o != '{') return 0;
    const char *p = json_skip_space(o + 1, end);
    while (p < end && *p != '}') {
        const char *ke = json_string_end(p, end);
        if (!ke) return 0;
        char key[70];
        int n = snprintf(key, sizeof(key), ",%.*s,", (int)(ke - p - 2), p + 1);
        if (n < 0 || n >= (int)sizeof(key) || !strstr(allowed, key)) return 0;
        p = json_skip_space(ke, end);
        if (p >= end || *p++ != ':') return 0;
        unsigned nodes = 0;
        p = bk_value(p, end, 0, &nodes);
        if (!p) return 0;
        p = json_skip_space(p, end);
        if (p < end && *p == ',') p = json_skip_space(p + 1, end);
    }
    return p < end && *p == '}';
}

static int bk_int(const char *o, const char *end, const char *key, int min, int max, int *out) {
    const char *p = json_member(o, end, key);
    if (!p || (!isdigit((unsigned char)*p) && *p != '-')) return 0;
    char *tail;
    errno = 0;
    long value = strtol(p, &tail, 10);
    if (errno || tail == p || tail > end || (tail < end && *tail != ',' && *tail != '}' && !isspace((unsigned char)*tail)) ||
        value < min || value > max) return 0;
    if (out) *out = (int)value;
    return 1;
}

static int bk_text(const char *o, const char *end, const char *key, char *out, size_t cap) {
    const char *p;
    int n;
    if (!json_member_raw_string(o, end, key, &p, &n) || n < 0 || (size_t)n >= cap || memchr(p, '\\', (size_t)n)) return 0;
    memcpy(out, p, (size_t)n);
    out[n] = 0;
    return 1;
}

typedef int (*bk_item_fn)(const char *, const char *, void *);
static int bk_list(const char *o, const char *end, const char *key, bk_item_fn fn, void *ctx, int max) {
    const char *le, *list = json_member_object(o, end, key, '[', &le);
    if (!list) return 0;
    const char *p = json_skip_space(list + 1, le);
    int count = 0;
    while (p < le && *p != ']') {
        if (*p != '{' || count++ >= max) return 0;
        const char *e = json_container_end(p, le);
        if (!e || !fn(p, e, ctx)) return 0;
        p = json_skip_space(e, le);
        if (p < le && *p == ',') p = json_skip_space(p + 1, le);
    }
    return count;
}

/* Distinguish an empty valid list from validation failure. */
static int bk_array(const char *o, const char *end, const char *key, bk_item_fn fn, void *ctx, int max) {
    const char *le, *list = json_member_object(o, end, key, '[', &le);
    if (!list) return 0;
    const char *p = json_skip_space(list + 1, le);
    return (p < le && *p == ']') || bk_list(o, end, key, fn, ctx, max) > 0;
}

static int bk_plate_taken(const plate_store *s, const char *id) {
    for (int i = 0; i < s->count; i++) if (!strcmp(s->plates[i].id, id)) return 1;
    for (int i = 0; i < s->measure_count; i++) if (!strcmp(s->measures[i].id, id)) return 1;
    for (int i = 0; i < s->nozzle_count; i++) if (!strcmp(s->nozzles[i].id, id)) return 1;
    return 0;
}
typedef struct { plate_store *store;
    int kind;
} bk_plate_context;
static int bk_plate_item(const char *o, const char *end, void *opaque) {
    bk_plate_context *ctx = opaque;
    plate_store *s = ctx->store;
    if (ctx->kind == 0) {
        if (s->nozzle_count >= PLATE_NOZZLES_MAX || !bk_keys(o,end,",id,name,diameter,z_offset,")) return 0;
        plate_nozzle *n = &s->nozzles[s->nozzle_count];
        if (!plates_parse_nozzle(o,end,n) || bk_plate_taken(s,n->id)) return 0;
        for (int i=0;i<s->nozzle_count;i++) if (!strcmp(s->nozzles[i].name,n->name)) return 0;
        s->nozzle_count++;
        return 1;
    }
    if (ctx->kind == 1 || ctx->kind == 2) {
        if (s->count >= PLATES_MAX || !bk_keys(o,end,",id,name,side,z_offset,measure,")) return 0;
        plate_entry *p = &s->plates[s->count];
        plate_measure unused;
        if (!plates_parse_plate(o,end,ctx->kind==2?3:2,p,&unused) || bk_plate_taken(s,p->id) ||
            (ctx->kind==2 && p->measure[0])) return 0;
        for (int i=0;i<s->count;i++) if (!strcmp(s->plates[i].name,p->name)) return 0;
        s->count++;
        return 1;
    }
    if (s->measure_count >= PLATE_MEASURES_MAX || !bk_keys(o,end,",id,plate,temp,nozzle,measured,mesh,")) return 0;
    double measured;
    if (!plates_member_double(o,end,"measured",&measured) || measured<0 || measured>1e11 || floor(measured)!=measured ||
        !bk_int(o,end,"temp",PLATE_TEMP_MIN,PLATE_TEMP_MAX,NULL)) return 0;
    const char *me,*mesh=json_member_object(o,end,"mesh",'{',&me);
    if (!mesh || !bk_keys(mesh,me,",x_count,y_count,min_x,max_x,min_y,max_y,mesh_x_pps,mesh_y_pps,algo,tension,offset,points,") ||
        !bk_int(mesh,me,"x_count",3,PLATE_GRID_MAX,NULL) || !bk_int(mesh,me,"y_count",3,PLATE_GRID_MAX,NULL) ||
        !bk_int(mesh,me,"mesh_x_pps",0,10,NULL) || !bk_int(mesh,me,"mesh_y_pps",0,10,NULL)) return 0;
    plate_measure *m=&s->measures[s->measure_count];
    if (!plates_parse_measure(o,end,m) || bk_plate_taken(s,m->id)) return 0;
    s->measure_count++;
    return 1;
}

static int bk_plates(const char *o,const char *end,plate_store *s) {
    memset(s,0,sizeof(*s));
    if (!bk_keys(o,end,",version,current,pending,nozzle,nozzles,plates,uncalibrated,measures,") ||
        !bk_int(o,end,"version",2,2,NULL) || !plates_parse_id(o,end,"current",s->current) ||
        !plates_parse_id(o,end,"pending",s->pending) || !plates_parse_id(o,end,"nozzle",s->nozzle)) return 0;
    bk_plate_context ctx={s,0};
    if (!bk_array(o,end,"nozzles",bk_plate_item,&ctx,PLATE_NOZZLES_MAX)) return 0;
    ctx.kind=1;
    if (!bk_array(o,end,"plates",bk_plate_item,&ctx,PLATES_MAX)) return 0;
    ctx.kind=2;
    if (!bk_array(o,end,"uncalibrated",bk_plate_item,&ctx,PLATES_MAX)) return 0;
    ctx.kind=3;
    if (!bk_array(o,end,"measures",bk_plate_item,&ctx,PLATE_MEASURES_MAX)) return 0;
    for(int i=0;i<s->measure_count;i++) {
        plate_measure *m=&s->measures[i];
        int plate=0,nozzle=!m->nozzle[0];
        for(int j=0;j<s->count;j++)plate|=!strcmp(m->plate,s->plates[j].id);
        for(int j=0;j<s->nozzle_count;j++)nozzle|=!strcmp(m->nozzle,s->nozzles[j].id);
        if(!plate||!nozzle)return 0;
    }
    int current=!s->current[0],pending=!s->pending[0],nozzle=!s->nozzle[0];
    for(int i=0;i<s->count;i++) {
        plate_entry *p=&s->plates[i];
        int found=!p->measure[0], any=0;
        for(int j=0;j<s->measure_count;j++) {
            plate_measure *m=&s->measures[j];
            if(!strcmp(m->plate,p->id)){any=1;
                if(!strcmp(m->id,p->measure))found=1;
            }
        }
        if(!found||(!p->measure[0]&&(any||!strcmp(s->current,p->id)||!strcmp(s->pending,p->id))))return 0;
        current|=!strcmp(s->current,p->id);
        pending|=!strcmp(s->pending,p->id);
    }
    for(int i=0;i<s->nozzle_count;i++)nozzle|=!strcmp(s->nozzle,s->nozzles[i].id);
    return current&&pending&&nozzle;
}

typedef struct { spool_store *store;
    int kind, slot;
} bk_spool_context;
static int bk_spool_item(const char *o,const char *end,void *opaque) {
    bk_spool_context *ctx=opaque;
    spool_store *s=ctx->store;
    if(ctx->kind==0) {
        if(s->count>=SPOOLS_MAX || !bk_keys(o,end,",id,name,brand,material,color,diameter,density,net,remaining,tare,low,price,note,created,used,archived,spoolman_id,"))return 0;
        spool_entry *p=&s->spools[s->count];
        if(!spools_parse_entry(o,end,p))return 0;
        for(int i=0;i<s->count;i++)if(!strcmp(p->id,s->spools[i].id))return 0;
        s->count++;
        return 1;
    }
    if(ctx->kind==1) {
        if(ctx->slot>=SPOOL_SLOTS || !bk_keys(o,end,",spool,last,question,question_since,question_mm,question_job,seen,") ||
            !bk_int(o,end,"question",0,2,NULL))return 0;
        const char *seen=json_member(o,end,"seen"),*se;
        if(seen&&*seen=='{') {
            se=json_container_end(seen,end);
            if(!se || !bk_keys(seen,se,",status,type,name,color,brand,code,") || !bk_int(seen,se,"status",0,2,NULL))return 0;
        }
        return spools_parse_slot(o,end,&s->slots[ctx->slot++]);
    }
    if(s->log_count>=SPOOL_LOG_MAX || !bk_keys(o,end,",time,spool,slot,kind,grams,mm,job,result,") ||
        !bk_int(o,end,"slot",-1,SPOOL_SLOTS-1,NULL))return 0;
    if(!spools_parse_event(o,end,&s->log[s->log_count]))return 0;
    s->log_count++;
    return 1;
}
static int bk_job_item(const char *o,const char *end,void *unused) {
    (void)unused;
    return bk_keys(o,end,",mm,grams,spool,");
}
static int bk_spools(const char *o,const char *end,spool_store *s) {
    memset(s,0,sizeof(*s));
    if(!bk_keys(o,end,",version,enabled,slots,job,spools,log,spoolman_url,spoolman_basic_auth,") || !bk_int(o,end,"version",1,1,NULL) ||
        !spools_bool_member(o,end,"enabled",&s->enabled))return 0;
    const char *je,*job=json_member_object(o,end,"job",'{',&je);
    if(!job || !bk_keys(job,je,",active,baseline,last_tray,file,uuid,used,duration,between,started,result,slots,") ||
        !bk_int(job,je,"last_tray",-1,3,NULL) ||
        bk_list(job,je,"slots",bk_job_item,NULL,SPOOL_SLOTS)!=SPOOL_SLOTS || !spools_parse_job(job,je,&s->job))return 0;
    bk_spool_context ctx={s,0,0};
    if(!bk_array(o,end,"spools",bk_spool_item,&ctx,SPOOLS_MAX))return 0;
    ctx.kind=1;
    if(bk_list(o,end,"slots",bk_spool_item,&ctx,SPOOL_SLOTS)!=SPOOL_SLOTS)return 0;
    ctx.kind=2;
    if(!bk_array(o,end,"log",bk_spool_item,&ctx,SPOOL_LOG_MAX))return 0;
    for(int i=0;i<SPOOL_SLOTS;i++) {
        const char *id=s->slots[i].spool;
        int found=!*id;
        for(int j=0;j<s->count;j++)found|=!strcmp(id,s->spools[j].id);
        if(!found)return 0;
        for(int j=0;j<i;j++)if(*id&&!strcmp(id,s->slots[j].spool))return 0;
        if(s->job.active&&s->job.spool[i][0]) {
            found=0;
            for(int j=0;j<s->count;j++)found|=!strcmp(s->job.spool[i],s->spools[j].id);
            if(!found)return 0;
        }
    }
    return 1;
}

static int bk_preferences(const char *o,const char *end,char out[512]) {
    if(!bk_keys(o,end,",language,theme,quick1,quick2,quick3,quick4,camera_persistent,"))return 0;
    char lang[9],theme_code[25],quick[4][32];
    int camera;
    if(!bk_text(o,end,"language",lang,sizeof(lang)) ||
        (*lang&&(strlen(lang)!=2||!isalpha((unsigned char)lang[0])||!isalpha((unsigned char)lang[1])||
         !strstr(",en,it,fr,es,hu,cs,pl,ru,zh,",lang))) || !bk_text(o,end,"theme",theme_code,sizeof(theme_code)) ||
        !theme_valid(theme_code,strlen(theme_code)) || !spools_bool_member(o,end,"camera_persistent",&camera))return 0;
    for(int i=0;i<4;i++) {
        char key[]="quick1";
        key[5]=(char)('1'+i);
        if(!bk_text(o,end,key,quick[i],sizeof(quick[i])) || !quick_action_valid(quick[i]))return 0;
    }
    snprintf(out,512,"{\"language\":\"%s\",\"theme\":\"%s\",\"quick1\":\"%s\",\"quick2\":\"%s\",\"quick3\":\"%s\",\"quick4\":\"%s\",\"camera_persistent\":%s}\n",
        lang,theme_code,quick[0],quick[1],quick[2],quick[3],camera?"true":"false");
    return 1;
}

typedef struct { char names[256][17];
    int count;
} bk_material_context;
static int bk_material_item(const char *o,const char *end,void *opaque) {
    bk_material_context *ctx=opaque;
    int nz,bed,min,max;
    if(ctx->count>=256 || !bk_keys(o,end,",name,nozzle,bed,min,max,") ||
        !bk_text(o,end,"name",ctx->names[ctx->count],17))return 0;
    const char *name=ctx->names[ctx->count];
    size_t n=strlen(name);
    if(!n||name[0]==' '||name[n-1]==' '||strspn(name,"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 +_-")!=n)return 0;
    for(int i=0;i<ctx->count;i++)if(!strcmp(name,ctx->names[i]))return 0;
    if(!bk_int(o,end,"nozzle",0,300,&nz)||!bk_int(o,end,"bed",0,120,&bed)||
       !bk_int(o,end,"min",0,350,&min)||!bk_int(o,end,"max",0,350,&max)||min>max)return 0;
    ctx->count++;
    return 1;
}
static int bk_materials(const char *o,const char *end,char out[8193]) {
    if(!o||*o!='['||end-o>8192)return 0;
    /* Reuse the object-array walker through a bounded wrapper. */
    char *wrapper=malloc((size_t)(end-o)+16);
    if(!wrapper)return 0;
    int n=snprintf(wrapper,(size_t)(end-o)+16,"{\"data\":%.*s}",(int)(end-o),o);
    bk_material_context ctx={{{0}},0};
    int ok=n>0&&bk_array(wrapper,wrapper+n,"data",bk_material_item,&ctx,256)&&ctx.count>0;
    free(wrapper);
    if(ok){memcpy(out,o,(size_t)(end-o));
        out[end-o]=0;
    }return ok;
}

static int bk_parse(const char *text,size_t length,backup_data *out) {
    memset(out,0,sizeof(*out));
    if(!bk_json(text,length))return 0;
    const char *end=text+length,*root=json_skip_space(text,end),*de,*data;
    char format[32],checksum[9],model[16],firmware[16];
    if(!bk_keys(root,end,",format,schema,crc32,data,") || !bk_text(root,end,"format",format,sizeof(format)) ||
       strcmp(format,"cc2-control-backup") || !bk_int(root,end,"schema",1,1,NULL) ||
       !bk_text(root,end,"crc32",checksum,sizeof(checksum)) || strlen(checksum)!=8 ||
       strspn(checksum,"0123456789abcdef")!=8 || !(data=json_member_object(root,end,"data",'{',&de)))return 0;
    char actual[9];
    snprintf(actual,sizeof(actual),"%08x",bk_crc(data,(size_t)(de-data)));
    if(strcmp(checksum,actual) || !bk_keys(data,de,",model,firmware,version,created,sections,") ||
       !bk_text(data,de,"model",model,sizeof(model)) || strcmp(model,"CC2") ||
       !bk_text(data,de,"firmware",firmware,sizeof(firmware)) || strcmp(firmware,CC2_COMMUNITY_FIRMWARE_VERSION) ||
       !bk_text(data,de,"version",out->version,sizeof(out->version)) ||
       !*out->version || strspn(out->version,"0123456789.-abcdefghijklmnopqrstuvwxyz")!=strlen(out->version) ||
       !spools_time_member(data,de,"created",&out->created))return 0;
    const char *se,*sections=json_member_object(data,de,"sections",'{',&se);
    if(!sections || !bk_keys(sections,se,",preferences,materials,plates,spools,"))return 0;
    static const char *names[]={"preferences","materials","plates","spools"};
    for(int i=0;i<4;i++) {
        const char *value=json_member(sections,se,names[i]);
        if(!value)continue;
        const char *ve=(*value=='{'||*value=='[')?json_container_end(value,se):NULL;
        if(!ve)return 0;
        int ok=i==0?bk_preferences(value,ve,out->preferences):i==1?bk_materials(value,ve,out->materials):
            i==2?bk_plates(value,ve,&out->plate_data):bk_spools(value,ve,&out->spool_data);
        if(!ok)return 0;
        out->sections|=1U<<i;
    }
    return out->sections!=0;
}

static void bk_inert(backup_data *data) {
    data->plate_data.current[0]=data->plate_data.pending[0]=data->plate_data.nozzle[0]=0;
    spool_store *s=&data->spool_data;
    s->enabled=0;
    memset(&s->job,0,sizeof(s->job));
    s->job.last_tray=-1;
    for(int i=0;i<SPOOL_SLOTS;i++) {
        char previous[17]={0};
        snprintf(previous,sizeof(previous),"%s",s->slots[i].spool[0]?s->slots[i].spool:s->slots[i].last);
        memset(&s->slots[i],0,sizeof(s->slots[i]));
        memcpy(s->slots[i].last,previous,sizeof(previous));
    }
}
