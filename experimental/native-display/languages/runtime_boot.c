/* Exact-image constructor. Unqualified until loader and ARM fixtures pass. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <syslog.h>
#include "sha256.h"
#include "native_hook_contract.h"

extern const char *nd_display_get_text(unsigned);
extern const char *nd_display_language_name(void);
extern void nd_display_set_native_language(unsigned);
extern void *nd_display_language_menu(void *);
extern void nd_display_label_text(void *, const char *);
extern void nd_display_label_static(void *, const char *);
_Static_assert(ND_HOOK_COUNT==6,"Unexpected display hook contract");

static int exact_executable(void) {
    int fd=open("/proc/self/exe",O_RDONLY|O_CLOEXEC);
    if(fd<0) return 0;
    struct stat st;char digest[65];
    int ok=fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_size==ND_GUI_SIZE;
    if(ok) ok=sha256_fd(fd,digest)==1 && strcmp(digest,ND_GUI_SHA)==0;
    close(fd);return ok;
}

__attribute__((constructor)) static void install_language_hooks(void) {
    if(!exact_executable()) {
        syslog(LOG_ERR,"cc2-native-display: unsupported executable; original GUI retained");
        return;
    }
    const uintptr_t replacements[ND_HOOK_COUNT]={
        (uintptr_t)nd_display_get_text,(uintptr_t)nd_display_language_name,
        (uintptr_t)nd_display_set_native_language,(uintptr_t)nd_display_language_menu,
        (uintptr_t)nd_display_label_text,(uintptr_t)nd_display_label_static
    };
    long page_size=sysconf(_SC_PAGESIZE);
    if(page_size<=0 || ((unsigned long)page_size & ((unsigned long)page_size-1))) return;
    uintptr_t pages[ND_HOOK_COUNT];unsigned count=0;
    for(unsigned i=0;i<ND_HOOK_COUNT;i++) {
        uintptr_t address=nd_hook_addresses[i];
        if(address%4 || replacements[i]%4 ||
           memcmp((const void *)address,nd_hook_expected[i],8)) return;
        uintptr_t page=address&~((uintptr_t)page_size-1);
        if(address+8>page+(uintptr_t)page_size) return;
        unsigned j=0;while(j<count && pages[j]!=page)j++;
        if(j==count)pages[count++]=page;
    }
    /* No original instruction changes until every affected page is writable. */
    for(unsigned i=0;i<count;i++) {
        if(mprotect((void *)pages[i],(size_t)page_size,PROT_READ|PROT_WRITE)) {
            for(unsigned j=0;j<i;j++)
                if(mprotect((void *)pages[j],(size_t)page_size,PROT_READ|PROT_EXEC)) _exit(126);
            syslog(LOG_ERR,"cc2-native-display: code protection refused; original GUI retained");
            return;
        }
    }
    for(unsigned i=0;i<ND_HOOK_COUNT;i++) {
        uint32_t patch[2]={0xe51ff004u,(uint32_t)replacements[i]};
        memcpy((void *)nd_hook_addresses[i],patch,sizeof(patch));
        __builtin___clear_cache((char *)nd_hook_addresses[i],(char *)nd_hook_addresses[i]+8);
    }
    for(unsigned i=0;i<count;i++)
        if(mprotect((void *)pages[i],(size_t)page_size,PROT_READ|PROT_EXEC)) _exit(126);
    syslog(LOG_INFO,"cc2-native-display: exact-image language hooks installed");
}
