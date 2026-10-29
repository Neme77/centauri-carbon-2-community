#define _GNU_SOURCE
#include "status_core.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <poll.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#ifdef CC2_NETWORK_STATUS_TEST
#define NS_WPA_SOCKET CC2_NETWORK_STATUS_TEST
#else
#define NS_WPA_SOCKET "/opt/usr/wifi/sockets/wlan0"
#endif

/* Read-only qualification tool for the future network panel. It never loads
 * modules, opens Wi-Fi configuration, sends IP traffic, or changes routes.
 * The only transmitted datagrams are STATUS / SIGNAL_POLL to native local WPA.
 * Do not add command-line path overrides to a production root helper. */
typedef struct {
    const char *name;int exists,up,carrier,speed,prefix,address_count;
    char ip[INET_ADDRSTRLEN],operstate[32],driver[64],vendor[16],product[16];
    unsigned long long rx,tx;
    int counters;
} ns_interface;

static int ns_read(const char *path,char *out,size_t capacity) {
    int fd=open(path,O_RDONLY|O_CLOEXEC);ssize_t n;
    if(fd<0)return 0;
    n=read(fd,out,capacity-1);
    if(n<0){close(fd);return 0;}
    char extra;ssize_t more=read(fd,&extra,1);close(fd);
    if(more!=0){out[0]=0;return 0;}
    out[n]=0;return 1;
}
static int ns_sys(const char *name,const char *field,char *out,size_t capacity) {
    char path[256];snprintf(path,sizeof(path),"/sys/class/net/%s/%s",name,field);
    if(!ns_read(path,out,capacity))return 0;
    out[strcspn(out,"\r\n")]=0;return 1;
}
static int ns_prefix(const struct sockaddr *mask) {
    if(!mask || mask->sa_family!=AF_INET)return -1;
    unsigned n=ntohl(((const struct sockaddr_in*)mask)->sin_addr.s_addr);
    int prefix=0,zero=0;
    for(int bit=31;bit>=0;--bit) {
        if(n&(1u<<bit)){if(zero)return -1;prefix++;}else zero=1;
    }
    return prefix;
}
static void ns_collect_interface(ns_interface *out,const char *name,struct ifaddrs *all) {
    char value[128];memset(out,0,sizeof(*out));out->name=name;
    out->carrier=-1;out->speed=-1;out->prefix=-1;
    out->exists=if_nametoindex(name)!=0;
    for(struct ifaddrs *i=all;i;i=i->ifa_next) {
        if(strcmp(i->ifa_name,name))continue;
        out->up=(i->ifa_flags&IFF_UP)!=0;
        if(i->ifa_addr && i->ifa_addr->sa_family==AF_INET) {
            out->address_count++;
            if(out->address_count==1) {
                inet_ntop(AF_INET,&((struct sockaddr_in*)i->ifa_addr)->sin_addr,out->ip,sizeof(out->ip));
                if(!ns_ipv4(out->ip))out->ip[0]=0;
                out->prefix=ns_prefix(i->ifa_netmask);
            }
        }
    }
    /* Never silently choose one address when a previous experiment left aliases. */
    if(out->address_count!=1){out->ip[0]=0;out->prefix=-1;}
    ns_sys(name,"operstate",out->operstate,sizeof(out->operstate));
    if(ns_sys(name,"carrier",value,sizeof(value)))ns_number(value,0,1,&out->carrier);
    if(out->carrier==1 && ns_sys(name,"speed",value,sizeof(value)))ns_number(value,1,100000,&out->speed);
    if(ns_sys(name,"statistics/rx_bytes",value,sizeof(value))) {
        char *end;errno=0;out->rx=strtoull(value,&end,10);
        if(!errno && end!=value && !*end && value[0]!='-' && ns_sys(name,"statistics/tx_bytes",value,sizeof(value))) {
            errno=0;out->tx=strtoull(value,&end,10);out->counters=!errno&&end!=value&&!*end&&value[0]!='-';
        }
    }
    char link[256],path[256];snprintf(path,sizeof(path),"/sys/class/net/%s/device/driver",name);
    ssize_t length=readlink(path,link,sizeof(link)-1);
    if(length>0 && (size_t)length<sizeof(link)-1) {
        link[length]=0;const char *base=strrchr(link,'/');base=base?base+1:link;
        if(strlen(base)<sizeof(out->driver))strcpy(out->driver,base);
    }
    ns_sys(name,"device/../idVendor",out->vendor,sizeof(out->vendor));
    ns_sys(name,"device/../idProduct",out->product,sizeof(out->product));
}
static long long ns_millis(void) {
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);
    return (long long)now.tv_sec*1000+now.tv_nsec/1000000;
}
typedef struct {
    const char *error,*first_error;
    int number,bytes,attempts;
    long long elapsed;
} ns_wpa_result;
static int ns_wpa_once(const char *command,char *out,size_t capacity,ns_wpa_result *result) {
    /* Abstract client address avoids creating files in shared /tmp. Each call
     * uses a separate socket; replies are connected to the reviewed peer. */
    int fd=socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC|SOCK_NONBLOCK,0);
    if(fd<0){result->error="socket";result->number=errno;return 0;}
    struct sockaddr_un local={.sun_family=AF_UNIX},peer={.sun_family=AF_UNIX};
    int n=snprintf(local.sun_path+1,sizeof(local.sun_path)-1,"cc2-net-status-%ld-%lld",(long)getpid(),ns_millis());
    if(strlen(NS_WPA_SOCKET)>=sizeof(peer.sun_path)){result->error="path_length";close(fd);return 0;}
    strcpy(peer.sun_path,NS_WPA_SOCKET);
    if(n<0 || (size_t)n>=sizeof(local.sun_path)-1){result->error="path_length";close(fd);return 0;}
    if(bind(fd,(struct sockaddr*)&local,(socklen_t)(offsetof(struct sockaddr_un,sun_path)+1+n))<0){result->error="bind";result->number=errno;close(fd);return 0;}
    if(connect(fd,(struct sockaddr*)&peer,sizeof(peer))<0){result->error="connect";result->number=errno;close(fd);return 0;}
    if(send(fd,command,strlen(command),MSG_NOSIGNAL)!=(ssize_t)strlen(command)){result->error="send";result->number=errno;close(fd);return 0;}
    struct pollfd wait={.fd=fd,.events=POLLIN};
    int ready=poll(&wait,1,150);
    if(ready<=0){result->error=ready==0?"timeout":"poll";result->number=ready<0?errno:0;close(fd);return 0;}
    ssize_t got=recv(fd,out,capacity-1,MSG_TRUNC|MSG_DONTWAIT);
    int saved=errno;close(fd);
    if(got<0){result->error="receive";result->number=saved;out[0]=0;return 0;}
    result->bytes=(int)got;
    if(got==0 || (size_t)got>=capacity-1){result->error=got==0?"empty_reply":"oversized_reply";out[0]=0;return 0;}
    result->error="none";
    out[got]=0;return (int)got;
}
static int ns_wpa(const char *command,char *out,size_t capacity,ns_wpa_result *result) {
    long long started=ns_millis();int length=0;
    memset(result,0,sizeof(*result));result->first_error="none";
    for(int attempt=1;attempt<=2;attempt++) {
        result->attempts=attempt;result->number=0;result->bytes=0;out[0]=0;
        length=ns_wpa_once(command,out,capacity,result);
        if(attempt==1)result->first_error=result->error;
        if(length>0 || attempt==2)break;
        int transient=!strcmp(result->error,"timeout") || result->number==EINTR || result->number==EAGAIN;
        if(!transient)break;
        /* A repeated STATUS/SIGNAL_POLL is read-only. Keep the first error in
         * the result instead of hiding an intermittent failure behind success. */
        poll(NULL,0,20);
    }
    result->elapsed=ns_millis()-started;return length;
}
static void ns_wpa_result_json(FILE *out,const ns_wpa_result *result) {
    fprintf(out,"{\"error\":");ns_json_string(out,result->error);
    fprintf(out,",\"first_error\":");ns_json_string(out,result->first_error);
    fprintf(out,",\"errno\":%d,\"bytes\":%d,\"attempts\":%d,\"elapsed_ms\":%lld}",
            result->number,result->bytes,result->attempts,result->elapsed);
}
static void ns_interface_json(FILE *out,const ns_interface *i) {
    fprintf(out,"{\"name\":");ns_json_string(out,i->name);
    fprintf(out,",\"present\":%s,\"enabled\":%s,\"carrier\":",i->exists?"true":"false",i->up?"true":"false");
    if(i->carrier<0)fprintf(out,"null");else fprintf(out,"%s",i->carrier?"true":"false");
    fprintf(out,",\"operstate\":");ns_json_string(out,i->operstate);
    fprintf(out,",\"address\":");if(*i->ip)ns_json_string(out,i->ip);else fprintf(out,"null");
    fprintf(out,",\"prefix\":");if(i->prefix<0)fprintf(out,"null");else fprintf(out,"%d",i->prefix);
    fprintf(out,",\"address_count\":%d,\"address_mode\":\"unknown\",\"link_mbps\":",i->address_count);
    if(i->speed<0)fprintf(out,"null");else fprintf(out,"%d",i->speed);
    fprintf(out,",\"driver\":");ns_json_string(out,i->driver);
    fprintf(out,",\"usb_vendor\":");ns_json_string(out,i->vendor);
    fprintf(out,",\"usb_product\":");ns_json_string(out,i->product);
    fprintf(out,",\"rx_bytes\":");if(i->counters)fprintf(out,"%llu",i->rx);else fprintf(out,"null");
    fprintf(out,",\"tx_bytes\":");if(i->counters)fprintf(out,"%llu",i->tx);else fprintf(out,"null");
    fputc('}',out);
}
static const char *ns_route_source(char *ip,size_t capacity) {
    struct sockaddr_in peer={.sin_family=AF_INET,.sin_port=htons(9)},source;
    int fd=socket(AF_INET,SOCK_DGRAM|SOCK_CLOEXEC,0);if(fd<0)return "unavailable";
    /* UDP connect performs a kernel route lookup only. No send, DNS, ARP or
     * public reachability probe. This includes policy rules, unlike /proc/net/route. */
    inet_pton(AF_INET,"1.1.1.1",&peer.sin_addr);
    socklen_t length=sizeof(source);
    int ok=connect(fd,(struct sockaddr*)&peer,sizeof(peer))==0 && getsockname(fd,(struct sockaddr*)&source,&length)==0;
    close(fd);
    if(!ok || !inet_ntop(AF_INET,&source.sin_addr,ip,(socklen_t)capacity))return "unavailable";
    return "kernel_lookup";
}
static const char *ns_mode(void) {
    char value[16];
    int fd=open("/tmp/cc2-network-compat-r1.mode",O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
    if(fd<0)return errno==ENOENT?"native":"unknown";
    struct stat st;ssize_t n;
    if(fstat(fd,&st)<0 || !S_ISREG(st.st_mode) || st.st_uid!=0 || st.st_nlink!=1 ||
       (st.st_mode&0777)!=0600){close(fd);return "unknown";}
    n=read(fd,value,sizeof(value));close(fd);
    if(n==5&&!memcmp(value,"wifi\n",5))return "wifi";
    if(n==9&&!memcmp(value,"ethernet\n",9))return "ethernet";
    return "unknown";
}
static void ns_process(FILE *out,const char *name) {
    DIR *dir=opendir("/proc");struct dirent *item;int count=0,loaded=0;long pid=0;
    if(dir)while((item=readdir(dir))) {
        char *end;long candidate=strtol(item->d_name,&end,10);
        if(!*item->d_name || *end || candidate<=0)continue;
        char path[128],comm[64];snprintf(path,sizeof(path),"/proc/%ld/comm",candidate);
        if(!ns_read(path,comm,sizeof(comm)))continue;
        comm[strcspn(comm,"\n")]=0;if(strcmp(comm,name))continue;
        count++;pid=candidate;
        snprintf(path,sizeof(path),"/proc/%ld/maps",candidate);
        FILE *maps=fopen(path,"r");if(!maps)continue;char line[1024];
        while(fgets(line,sizeof(line),maps))if(strstr(line,"/libcc2-network-compat-r1.so"))loaded=1;
        fclose(maps);
    }
    if(dir)closedir(dir);
    fprintf(out,"{\"count\":%d,\"pid\":",count);
    if(count==1)fprintf(out,"%ld",pid);else fprintf(out,"null");
    fprintf(out,",\"compatibility_mapped\":%s}",count==1&&loaded?"true":"false");
}
int cc2_network_snapshot(FILE *output) {
    struct ifaddrs *all=NULL;int addresses=getifaddrs(&all)==0;
    ns_interface wifi,ethernet;
    ns_collect_interface(&wifi,"wlan0",all);ns_collect_interface(&ethernet,"cc2lan0",all);
    if(all)freeifaddrs(all);
    char wpa[4096]={0},ssid[256]={0},state[64]={0},security[128]={0},frequency[32]={0};
    ns_wpa_result status_result,signal_result={.error="not_requested",.first_error="not_requested"};
    int length=ns_wpa("STATUS",wpa,sizeof(wpa),&status_result);
    int wpa_ok=length>0 && ns_field(wpa,(size_t)length,"wpa_state",state,sizeof(state));
    if(length>0&&!wpa_ok){status_result.error="malformed_reply";if(status_result.attempts==1)status_result.first_error="malformed_reply";}
    if(wpa_ok) {
        ns_field(wpa,(size_t)length,"ssid",ssid,sizeof(ssid));
        ns_field(wpa,(size_t)length,"key_mgmt",security,sizeof(security));
        ns_field(wpa,(size_t)length,"freq",frequency,sizeof(frequency));
    }
    int rssi=0,have_rssi=0,freq=0,have_freq=ns_number(frequency,2000,8000,&freq);
    if(wpa_ok && !strcmp(state,"COMPLETED")) {
        char signal[512],value[32];int received=ns_wpa("SIGNAL_POLL",signal,sizeof(signal),&signal_result);
        if(received>0 && ns_field(signal,(size_t)received,"RSSI",value,sizeof(value)))have_rssi=ns_number(value,-127,-1,&rssi);
    }
    char source[INET_ADDRSTRLEN]={0};const char *lookup=ns_route_source(source,sizeof(source));
    const char *selected=*source&&*wifi.ip&&!strcmp(source,wifi.ip)?"wifi":
                         *source&&*ethernet.ip&&!strcmp(source,ethernet.ip)?"ethernet":"unknown";
    fprintf(output,"{\"schema\":1,\"read_only\":true,\"addresses_readable\":%s,\"sample_uptime_ms\":%lld,\"wifi\":",addresses?"true":"false",ns_millis());
    ns_interface_json(output,&wifi);fprintf(output,",\"ethernet\":");ns_interface_json(output,&ethernet);
    fprintf(output,",\"wireless\":{\"control_available\":%s,\"state\":",wpa_ok?"true":"false");ns_json_string(output,state);
    fprintf(output,",\"ssid\":");ns_json_string(output,ssid);fprintf(output,",\"security\":");ns_json_string(output,security);
    fprintf(output,",\"frequency_mhz\":");if(have_freq)fprintf(output,"%d",freq);else fprintf(output,"null");
    fprintf(output,",\"signal_dbm\":");if(have_rssi)fprintf(output,"%d",rssi);else fprintf(output,"null");
    fprintf(output,",\"status_read\":");ns_wpa_result_json(output,&status_result);
    fprintf(output,",\"signal_read\":");ns_wpa_result_json(output,&signal_result);
    fprintf(output,"},\"route\":{\"selected\":");ns_json_string(output,selected);
    fprintf(output,",\"source\":");if(*source)ns_json_string(output,source);else fprintf(output,"null");
    fprintf(output,",\"method\":");ns_json_string(output,lookup);fprintf(output,",\"destination\":\"1.1.1.1\",\"internet\":\"untested\"},\"native_mode\":");ns_json_string(output,ns_mode());
    fprintf(output,",\"services\":{\"camera\":");ns_process(output,"ai_camera");fprintf(output,",\"printer\":");ns_process(output,"elegoo_printer");
    fprintf(output,"},\"capabilities\":{\"network_changes\":false,\"automatic_selection\":false}}\n");
    return ferror(output)?1:0;
}

#ifndef CC2_NETWORK_STATUS_LIBRARY
int main(int argc,char **argv) {
    if(argc!=1){fprintf(stderr,"Usage: %s (read-only; no arguments)\n",argv[0]);return 2;}
    return cc2_network_snapshot(stdout);
}
#endif
