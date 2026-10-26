/* Real file/socket regression for current-print filename recovery. */
#include "../src/panda.c"
#include <assert.h>

typedef struct { int listener; const char *json; } status_fixture;
static void *status_reply(void *arg){
    status_fixture *f=arg;int fd=accept(f->listener,NULL,NULL);assert(fd>=0);
    char request[1024];assert(recv(fd,request,sizeof(request),0)>0);
    http_reply(fd,200,"OK",f->json);close(fd);return NULL;
}

static int recover(const char *root,const char *prefix,const char *json,char *out,size_t cap){
    int fd=socket(AF_INET,SOCK_STREAM,0);assert(fd>=0);
    struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(bind(fd,(struct sockaddr*)&a,sizeof(a))==0);assert(listen(fd,1)==0);
    socklen_t length=sizeof(a);assert(getsockname(fd,(struct sockaddr*)&a,&length)==0);
    status_fixture fixture={fd,json};pthread_t thread;assert(pthread_create(&thread,NULL,status_reply,&fixture)==0);
    int result=print_file_metadata(ntohs(a.sin_port),root,prefix,out,cap);
    assert(pthread_join(thread,NULL)==0);close(fd);return result;
}

int main(void){
    char folder[]="/tmp/cc2-panda-metadata-XXXXXX";assert(mkdtemp(folder));
    const char *name="ECC2_0.4_Long model with spaces_Generic ASA_0.2_3h15m_and_more.gcode";
    char path[512],prefix[64],out[2048],json[1024];
    snprintf(path,sizeof(path),"%s/%s",folder,name);
    FILE *file=fopen(path,"w");assert(file);
    assert(fputs("; filament used [mm] = 0, 45383.49\n; filament_type = PETG;ASA\n",file)>=0);assert(fclose(file)==0);
    memcpy(prefix,name,63);prefix[63]='\0';
    const char *states[]={"printing","paused"};
    for(size_t i=0;i<sizeof(states)/sizeof(states[0]);i++){
        snprintf(json,sizeof(json),"{\"connected\":true,\"last_message_age\":15,\"machine\":{\"status\":2},\"print\":{\"state\":\"%s\",\"filename\":\"%s\"}}",states[i],name);
        assert(recover(folder,prefix,json,out,sizeof(out))==0);
        assert(strstr(out,"\"filament_type\":\"PETG;ASA\""));
        assert(strstr(out,"\"filament_used_mm\":[0.00,45383.49]"));
        assert(strstr(out,name));
    }
    const char *ages[]={"16","-1","null","nan","1e999"};
    for(size_t i=0;i<sizeof(ages)/sizeof(ages[0]);i++){
        snprintf(json,sizeof(json),"{\"connected\":true,\"last_message_age\":%s,\"machine\":{\"status\":2},\"print\":{\"state\":\"printing\",\"filename\":\"%s\"}}",ages[i],name);
        assert(recover(folder,prefix,json,out,sizeof(out))<0);
    }
    const char *inactive[]={"complete","cancelled","standby",""};
    for(size_t i=0;i<sizeof(inactive)/sizeof(inactive[0]);i++){
        snprintf(json,sizeof(json),"{\"connected\":true,\"last_message_age\":0,\"machine\":{\"status\":2},\"print\":{\"state\":\"%s\",\"filename\":\"%s\"}}",inactive[i],name);
        assert(recover(folder,prefix,json,out,sizeof(out))<0);
    }
    snprintf(json,sizeof(json),"{\"connected\":false,\"last_message_age\":0,\"machine\":{\"status\":2},\"print\":{\"state\":\"printing\",\"filename\":\"%s\"}}",name);
    assert(recover(folder,prefix,json,out,sizeof(out))<0);
    snprintf(json,sizeof(json),"{\"connected\":true,\"last_message_age\":0,\"machine\":{\"status\":1},\"print\":{\"state\":\"printing\",\"filename\":\"%s\"}}",name);
    assert(recover(folder,prefix,json,out,sizeof(out))<0);
    snprintf(json,sizeof(json),"{\"connected\":true,\"last_message_age\":0,\"machine\":{\"status\":2},\"print\":{\"state\":\"printing\",\"filename\":\"different.gcode\"}}");
    assert(recover(folder,prefix,json,out,sizeof(out))<0);
    assert(recover(folder,prefix,"{}",out,sizeof(out))<0);
    assert(print_file_metadata(0,folder,"ECC2_0.4_Long",out,sizeof(out))<0);
    /* Exact files stay available without an active print; tiny responses fail closed. */
    assert(print_file_metadata(0,folder,name,out,sizeof(out))==0);
    assert(gcode_file_metadata(folder,name,out,8)<0);
    assert(filament_lengths("1, 2\r",out,sizeof(out))==0&&!strcmp(out,"1.00,2.00"));
    assert(filament_lengths("1,nan",out,sizeof(out))<0);
    assert(filament_lengths("1,2junk",out,sizeof(out))<0);
    assert(filament_lengths("1,2",out,3)<0);
    assert(unlink(path)==0);assert(rmdir(folder)==0);
    puts("PASS Panda metadata: current-print prefix recovery over real socket, fresh active/paused guards, exact lookup, numeric validation and bounded output");
    return 0;
}
