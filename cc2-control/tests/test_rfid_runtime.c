#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include "../src/sha256.h"
static char runtime_root[256];
static char api_root[256];
#define CC2_RFID_NATIVE_ROOT runtime_root
#define CC2_BASE_API_ROOT api_root
#define CC2_RFID_OWNER_UID geteuid()
#define CC2_RFID_HELPER "legacy-capture-fixture"
#define CC2_RFID_HELPER_SHA "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#ifdef CC2_PANEL_ABI2
static int panel_binding_valid=1;
static int fixture_panel_binding(const char *digest){return panel_binding_valid&&digest&&strlen(digest)==64;}
#define CC2_RFID_PANEL_ABI_SELF fixture_panel_binding
#endif
#include "../src/rfid_runtime.h"
static void put(const char *name,const void *data,size_t size){
    char path[512];assert(snprintf(path,sizeof(path),"%s/%s",runtime_root,name)>0);
    int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);assert(fd>=0);
    assert(!fchmod(fd,0600)&&write(fd,data,size)==(ssize_t)size&&!close(fd));
}
static void remove_file(const char *name){char path[512];assert(snprintf(path,sizeof(path),"%s/%s",runtime_root,name)>0);assert(!unlink(path));}
static void put_metadata(const void *data,size_t size){
    char path[512];assert(snprintf(path,sizeof(path),"%s/app-integration.json",api_root)>0);
    int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);assert(fd>=0);
    assert(!fchmod(fd,0600)&&write(fd,data,size)==(ssize_t)size&&!close(fd));
}
static void remove_metadata(void){char path[512];assert(snprintf(path,sizeof(path),"%s/app-integration.json",api_root)>0);assert(!unlink(path));}
static void journal(unsigned phase,int corrupt){
    unsigned char bytes[192]={0};memcpy(bytes,"CC2-CANVAS-LC1",14);bytes[16]=(unsigned char)phase;bytes[24]=1;bytes[32]=1;
    memcpy(bytes+48,"00000000-0000-0000-0000-000000000001",36);memset(bytes+84,0xbb,32);
    sha256_state h;char digest[65];sha256_init(&h);sha256_update(&h,bytes,128);sha256_hex(&h,digest);memcpy(bytes+128,digest,64);
    if(corrupt)bytes[128]^=1;
    put("lifecycle",bytes,sizeof(bytes));
}
int main(void){
    char parent[]="/tmp/cc2-rfid-runtime-XXXXXX";assert(mkdtemp(parent));
    assert(snprintf(runtime_root,sizeof(runtime_root),"%s/native",parent)>0);
    assert(snprintf(api_root,sizeof(api_root),"%s/api",parent)>0);
    assert(rfid_runtime_get(1).mode==0&&rfid_runtime_allowed(1));
    assert(!mkdir(runtime_root,0700));
    /* An existing RFID namespace without the separate API is unresolved,
     * even when its journal is absent. Never fall back to legacy metadata. */
    assert(rfid_runtime_get(1).mode==-1&&!rfid_runtime_allowed(1));
    assert(!mkdir(api_root,0700));
    assert(rfid_runtime_get(1).mode==-1);
    assert(rr_self());
    char metadata[1024];int length=snprintf(metadata,sizeof(metadata),
        "{\"protocol\":1,\"app_sha256\":\"%s\",\"coordinator_sha256\":\"%064x\",\"capture_sha256\":\"%064x\",\"profile_sha256\":\"%s\"}",
        rfid_runtime_self_sha,1,2,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    assert(length>0&&(size_t)length<sizeof(metadata));
    put("app-integration.json",metadata,(size_t)length);
    assert(rfid_runtime_get(1).mode==-1); /* old namespace is not an API fallback */
    put_metadata(metadata,(size_t)length);
    rfid_runtime mode=rfid_runtime_get(1);assert(mode.mode==1&&!mode.active&&mode.phase==-1);
    assert(!mode.requested);
    char activation[192];int count=snprintf(activation,sizeof(activation),"CC2-RFID-ACTIVATE-1\n%s\n%s\n",mode.app,mode.profile);
    assert(count>0&&(size_t)count<sizeof(activation));put("activate-on-ready",activation,(size_t)count);
    assert(rfid_runtime_get(1).requested);
    activation[0]='X';put("activate-on-ready",activation,(size_t)count);assert(!rfid_runtime_get(1).requested);
    activation[0]='C';put("activate-on-ready",activation,(size_t)count);
    char helper[512],digest[65];assert(rfid_runtime_helper(helper,sizeof(helper),digest,0));
    char expected_helper[512];assert(snprintf(expected_helper,sizeof(expected_helper),"%s/capture",api_root)>0);
    assert(!strcmp(helper,expected_helper)&&!rfid_runtime_helper(helper,sizeof(helper),digest,1));
    journal(5,0);mode=rfid_runtime_get(1);assert(mode.mode==1&&mode.active&&mode.phase==5&&mode.generation==1);
    assert(!mode.requested);remove_file("activate-on-ready");
    assert(rfid_runtime_allowed(1)&&rfid_runtime_helper(helper,sizeof(helper),digest,1));
    journal(3,0);assert(!rfid_runtime_allowed(1)&&rfid_runtime_get(1).phase==3);
    journal(5,1);mode=rfid_runtime_get(1);assert(!mode.active&&mode.phase==-2&&!rfid_runtime_allowed(1));
    journal(254,0);assert(rfid_runtime_get(1).phase==-2);
    remove_metadata();assert(rfid_runtime_get(1).mode==-1); /* existing ledger forbids legacy fallback */
    put_metadata(metadata,(size_t)length);
    char path[512];assert(snprintf(path,sizeof(path),"%s/app-integration.json",api_root)>0);
    assert(!chmod(path,0644)&&rfid_runtime_get(1).mode==-1&&!chmod(path,0600));
    assert(!chmod(api_root,0755)&&rfid_runtime_get(1).mode==-1&&!chmod(api_root,0700));
    char alias[512];assert(snprintf(alias,sizeof(alias),"%s/api-saved",parent)>0);
    assert(!rename(api_root,alias)&&!symlink(alias,api_root));
    assert(rfid_runtime_get(1).mode==-1);assert(!unlink(api_root)&&!rename(alias,api_root));
    assert(snprintf(alias,sizeof(alias),"%s/metadata-hardlink",parent)>0);
    assert(!link(path,alias));assert(rfid_runtime_get(1).mode==-1);assert(!unlink(alias));
    metadata[0]='[';put_metadata(metadata,(size_t)length);assert(rfid_runtime_get(1).mode==-1);metadata[0]='{';
    char changed[1024];memcpy(changed,metadata,(size_t)length+1);char *app=strstr(changed,rfid_runtime_self_sha);assert(app);app[0]=app[0]=='0'?'1':'0';
    put_metadata(changed,(size_t)length);assert(rfid_runtime_get(1).mode==-1);
    remove_metadata();assert(!symlink("/definitely-absent-runtime-fixture",path));
    assert(rfid_runtime_get(1).mode==-1);assert(!unlink(path));remove_file("lifecycle");
    assert(rfid_runtime_get(1).mode==-1); /* missing journal does not waive the API guard */
    char protocol2[1024];int n2=snprintf(protocol2,sizeof(protocol2),
        "{\"protocol\":2,\"panel_api\":1,\"activation_sha256\":\"%064x\",\"coordinator_sha256\":\"%064x\",\"capture_sha256\":\"%064x\",\"profile_sha256\":\"%s\"}",
        3,1,2,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    assert(n2>0&&(size_t)n2<sizeof(protocol2));put_metadata(protocol2,(size_t)n2);
#ifdef CC2_PANEL_ABI2
    mode=rfid_runtime_get(1);assert(mode.mode==1&&mode.metadata_protocol==2&&!strcmp(mode.app,rfid_runtime_self_sha));
    assert(strcmp(mode.activation,mode.app));
    count=snprintf(activation,sizeof(activation),"CC2-RFID-ACTIVATE-1\n%s\n%s\n",mode.activation,mode.profile);
    assert(count>0&&(size_t)count<sizeof(activation));put("activate-on-ready",activation,(size_t)count);
    assert(rfid_runtime_get(1).requested);remove_file("activate-on-ready");
    panel_binding_valid=0;assert(rfid_runtime_get(1).mode==-1);panel_binding_valid=1;
    char *api=strstr(protocol2,"\"panel_api\":1");assert(api);api[strlen("\"panel_api\":")]='2';
    put_metadata(protocol2,(size_t)n2);assert(rfid_runtime_get(1).mode==-1);
#else
    assert(rfid_runtime_get(1).mode==-1); /* ABI1 app must refuse protocol 2 */
#endif
    remove_metadata();remove_file("app-integration.json");
    assert(!rmdir(api_root)&&rfid_runtime_get(1).mode==-1&&!rmdir(runtime_root));
    assert(rfid_runtime_get(1).mode==0&&rfid_runtime_helper(helper,sizeof(helper),digest,1)&&!strcmp(helper,CC2_RFID_HELPER));
    assert(!rmdir(parent));
    puts("PASS: separate API metadata, native evidence preserved, app/ABI binding, private paths, phase/corruption refusal and no fallback from an existing namespace");
    return 0;
}
