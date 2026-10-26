#ifndef CC2_HTTP_GUARD_H
#define CC2_HTTP_GUARD_H
#include <arpa/inet.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* Only headers, never the request body. Duplicate security headers fail closed. */
static int http_header(const char *request,const char *name,char *out,size_t cap){
    const char *p=strchr(request,'\n');int found=0;size_t length=strlen(name);
    if(!p)return -1;
    for(p++;*p;){
        const char *end=strchr(p,'\n');if(!end)return -1;
        const char *last=end;if(last>p&&last[-1]=='\r')last--;
        if(last==p)return found;
        const char *colon=memchr(p,':',(size_t)(last-p));
        if(!colon||isspace((unsigned char)*p))return -1;
        if((size_t)(colon-p)==length&&!strncasecmp(p,name,length)){
            if(found)return -1;
            const char *value=colon+1;while(value<last&&(*value==' '||*value=='\t'))value++;
            while(last>value&&(last[-1]==' '||last[-1]=='\t'))last--;
            size_t n=(size_t)(last-value);if(!n||n>=cap)return -1;
            memcpy(out,value,n);out[n]=0;found=1;
        }
        p=end+1;
    }
    return -1;
}

static int http_browser_allowed(int fd,const char *request,const char *method,const char *path,int port,const char *alias){
    char host[256],origin[300],site[40],marker[32];
    int h=http_header(request,"Host",host,sizeof(host));
    int o=http_header(request,"Origin",origin,sizeof(origin));
    int s=http_header(request,"Sec-Fetch-Site",site,sizeof(site));
    int m=http_header(request,"X-CC2-Request",marker,sizeof(marker));
    if(h!=1||o<0||s<0||m<0)return 0;
    char encoding[64];
    if(http_header(request,"Transfer-Encoding",encoding,sizeof(encoding))!=0)return 0;
    char expected[300],address[INET_ADDRSTRLEN],name[256],authority[280];
    struct sockaddr_in local;socklen_t size=sizeof(local);
    if(getsockname(fd,(struct sockaddr *)&local,&size)||local.sin_family!=AF_INET||
       !inet_ntop(AF_INET,&local.sin_addr,address,sizeof(address)))return 0;
    snprintf(authority,sizeof(authority),"%s:%d",address,port);
    int known=!strcasecmp(host,authority);
    if(gethostname(name,sizeof(name))==0){
        name[sizeof(name)-1]=0;
        snprintf(authority,sizeof(authority),"%s:%d",name,port);known|=!strcasecmp(host,authority);
        snprintf(authority,sizeof(authority),"%s.local:%d",name,port);known|=!strcasecmp(host,authority);
    }
    if(ntohl(local.sin_addr.s_addr)>>24==127){
        snprintf(authority,sizeof(authority),"localhost:%d",port);known|=!strcasecmp(host,authority);
    }
    if(alias&&*alias){snprintf(authority,sizeof(authority),"%s:%d",alias,port);known|=!strcasecmp(host,authority);}
    if(!o&&!s){
        known|=!strcasecmp(host,address);
        if(ntohl(local.sin_addr.s_addr)>>24==127)known|=!strcasecmp(host,"localhost");
    }
    if(!known)return 0; /* Reject DNS rebinding even when Origin equals Host. */
    snprintf(expected,sizeof(expected),"http://%s",host);
    if(o&&strcasecmp(origin,expected))return 0;
    if(s&&!strcmp(site,"cross-site"))return 0;
    int mutation=strcmp(method,"GET")&&strcmp(method,"HEAD")&&strcmp(method,"OPTIONS");
    /* Orca's native OctoPrint upload cannot supply our browser marker.
     * Uploaded jobs still require confirmation before printing. */
    int orca_upload=!strncmp(path,"/api/files/local",16)&&(path[16]==0||path[16]=='?')&&!o&&!s;
    if(mutation&&!orca_upload&&(m!=1||strcmp(marker,"1")))return 0;
    return 1;
}
#endif
