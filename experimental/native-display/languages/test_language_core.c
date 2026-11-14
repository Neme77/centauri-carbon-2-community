#define _POSIX_C_SOURCE 200809L
#include "language_core.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    char directory[]="/var/tmp/cc2-display-language-XXXXXX";
    assert(mkdtemp(directory));
    char path[256], linkpath[256];
    assert(snprintf(path,sizeof(path),"%s/preference",directory)>0);
    assert(snprintf(linkpath,sizeof(linkpath),"%s/link",directory)>0);
    struct nd_language state={0}, restored={0};
    assert(!nd_language_read(&state,path));
    assert(nd_language_effective(&state,4)==4);
    for(int selected=0;selected<ND_LANGUAGES;selected++) {
        assert(nd_language_save(&state,path,selected));
        memset(&restored,0,sizeof(restored));
        assert(nd_language_read(&restored,path));
        int native=nd_language_native(selected);
        assert(native>=0 && native<ND_NATIVE_LANGUAGES);
        assert(nd_language_effective(&restored,native)==selected);
        assert(nd_language_effective(&restored,(native+1)%ND_NATIVE_LANGUAGES)==(native+1)%ND_NATIVE_LANGUAGES);
    }
    assert(!nd_language_save(&state,path,14));
    assert(!nd_language_save(&state,path,-1));
    assert(nd_language_effective(&state,14)==-1);
    assert(nd_language_effective(&state,-1)==-1);
    assert(!symlink(path,linkpath));
    assert(!nd_language_read(&restored,linkpath));
    assert(restored.selected==-1);
    assert(!unlink(linkpath));
    assert(!link(path,linkpath));
    assert(!nd_language_read(&restored,path));
    assert(!unlink(linkpath));
    const char *bad[]={"", "CC2LANG1\n14\n", "CC2LANG1\n99\n", "CC2LANG1\n-1\n", "CC2LANG1\n11\nextra", "CC2LANG2\n11\n"};
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        int fd=open(path,O_WRONLY|O_TRUNC);
        assert(fd>=0);
        assert(write(fd,bad[i],strlen(bad[i]))==(ssize_t)strlen(bad[i]));
        assert(!close(fd));
        assert(!nd_language_read(&restored,path));
        assert(nd_language_effective(&restored,1)==1);
    }
    assert(nd_language_save(&state,path,11));
    int previous=state.selected;
    assert(!nd_language_save(&state,"/path-that-does-not-exist/cc2-language",12));
    assert(state.selected==previous);
    assert(nd_language_read(&restored,path) && restored.selected==11);
    assert(!unlink(path));
    assert(!rmdir(directory));
    puts("PASS: all 14 locale selections/reloads, native bounds, damaged preferences, symlink/hardlink refusal and failed-save preservation.");
    return 0;
}
