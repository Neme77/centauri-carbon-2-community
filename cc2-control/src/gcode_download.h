/* Native attachment download: pin every path segment with openat, stream from
 * the opened regular file, and keep slow clients outside the telemetry loop. */
static atomic_int active_downloads;
typedef struct { int client, file; off_t size; char name[PATH_MAX_LOCAL]; } gcode_download;

static int download_decode(char *out, size_t capacity, const char *in, size_t length) {
    size_t used=0;
    for (size_t i=0;i<length;i++) {
        unsigned char c=(unsigned char)in[i];
        if(c=='%') {
            if(i+2>=length || !isxdigit((unsigned char)in[i+1]) || !isxdigit((unsigned char)in[i+2]))return 0;
            char hex[3]={in[i+1],in[i+2],0}; c=(unsigned char)strtoul(hex,NULL,16);i+=2;
        } else if(c=='+')c=' ';
        if(c<32 || c==127 || used+1>=capacity)return 0;
        out[used++]=(char)c;
    }
    out[used]=0;return used>0;
}

static int download_query(const char *query,char storage[16],char file[PATH_MAX_LOCAL]) {
    int seen_storage=0,seen_file=0;
    if(!query)return 0;
    while(*query) {
        const char *end=strchr(query,'&');if(!end)end=query+strlen(query);
        const char *equals=memchr(query,'=',(size_t)(end-query));if(!equals)return 0;
        size_t key=(size_t)(equals-query),length=(size_t)(end-equals-1);
        if(key==7 && !memcmp(query,"storage",7) && !seen_storage++) {
            if(!download_decode(storage,16,equals+1,length))return 0;
        } else if(key==4 && !memcmp(query,"file",4) && !seen_file++) {
            if(!download_decode(file,PATH_MAX_LOCAL,equals+1,length))return 0;
        } else return 0;
        query=*end?end+1:end;
        if(*end && !*query)return 0;
    }
    return seen_storage==1 && seen_file==1 && safe_relative_path(file) && is_gcode_filename(file);
}

static int download_open(const char *root,const char *relative,struct stat *st) {
    int directory=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    if(directory<0)return -1;
    char path[PATH_MAX_LOCAL];snprintf(path,sizeof(path),"%s",relative);
    char *segment=path;
    for (;;) {
        char *slash=strchr(segment,'/');if(slash)*slash=0;
        int next=openat(directory,segment,O_RDONLY|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK|(slash?O_DIRECTORY:0));
        close(directory);
        if(next<0)return -1;
        if(!slash) {
            if(fstat(next,st)<0 || !S_ISREG(st->st_mode) || st->st_size<0){close(next);return -1;}
            return next;
        }
        directory=next;segment=slash+1;
    }
}

static void *download_worker(void *opaque) {
    gcode_download *job=opaque;
    char encoded[PATH_MAX_LOCAL*3],header[PATH_MAX_LOCAL*3+512],chunk[16384];
    size_t used=0;
    for(const unsigned char *p=(unsigned char *)job->name;*p;p++) {
        if((*p>='a'&&*p<='z')||(*p>='A'&&*p<='Z')||(*p>='0'&&*p<='9')||strchr("-._",*p))encoded[used++]=(char)*p;
        else {snprintf(encoded+used,4,"%%%02X",*p);used+=3;}
    }
    encoded[used]=0;
    int n=snprintf(header,sizeof(header),
        "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %lld\r\n"
        "Content-Disposition: attachment; filename=\"download.gcode\"; filename*=UTF-8''%s\r\n"
        "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",
        (long long)job->size,encoded);
    struct timeval timeout={10,0};setsockopt(job->client,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
    struct timespec started,now;clock_gettime(CLOCK_MONOTONIC,&started);
    off_t remaining=job->size;
    if(n>0 && (size_t)n<sizeof(header) && !send_all(job->client,header,(size_t)n)) {
        while(remaining>0) {
            clock_gettime(CLOCK_MONOTONIC,&now);if(now.tv_sec-started.tv_sec>=300)break;
            size_t want=remaining>(off_t)sizeof(chunk)?sizeof(chunk):(size_t)remaining;
            ssize_t got=read(job->file,chunk,want);
            if(got<0&&errno==EINTR)continue;
            if(got<=0 || send_all(job->client,chunk,(size_t)got))break;
            remaining-=got;
        }
    }
    close(job->file);close(job->client);free(job);atomic_fetch_sub(&active_downloads,1);return NULL;
}

static int gcode_download_start(int fd,const char *query) {
    char storage[16],file[PATH_MAX_LOCAL];const char *root;struct stat st;
    if(!download_query(query,storage,file)||!gcode_storage_root(storage,&root)) {
        const char *error="{\"error\":\"Invalid protected G-code path\"}\n";
        respond(fd,400,"Bad Request","application/json",error,strlen(error));return 0;
    }
    int source=download_open(root,file,&st);
    if(source<0) {
        const char *error="{\"error\":\"G-code file unavailable\"}\n";
        respond(fd,404,"Not Found","application/json",error,strlen(error));return 0;
    }
    int previous=atomic_fetch_add(&active_downloads,1);
    gcode_download *job=NULL;pthread_attr_t attr;pthread_t thread;int initialized=0,launched=0;
    if(previous<2 && (job=malloc(sizeof(*job)))) {
        job->client=fd;job->file=source;job->size=st.st_size;
        const char *name=strrchr(file,'/');snprintf(job->name,sizeof(job->name),"%s",name?name+1:file);
        if(!pthread_attr_init(&attr)) {
            initialized=1;
            if(!pthread_attr_setstacksize(&attr,64*1024) && !pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED))
                launched=!pthread_create(&thread,&attr,download_worker,job);
        }
    }
    if(initialized)pthread_attr_destroy(&attr);
    if(launched)return 1; /* worker owns client from now on */
    free(job);close(source);atomic_fetch_sub(&active_downloads,1);
    const char *error="{\"error\":\"Download capacity reached; retry shortly\"}\n";
    respond(fd,503,"Service Unavailable","application/json",error,strlen(error));return 0;
}
