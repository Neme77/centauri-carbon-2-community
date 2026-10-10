#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>
// Only used after an exact file hash and both overwritten instructions match.
// The qualified entries contain no PC-relative instructions or branches.
inline void *install_hook(void *entry,void *replacement,const unsigned char expected[8]) {
    if(std::memcmp(entry,expected,8))throw std::runtime_error("Canvas hook fingerprint mismatch");
    auto *stub=static_cast<unsigned char *>(mmap(nullptr,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0));
    if(stub==MAP_FAILED)throw std::runtime_error("Canvas trampoline allocation failed");
    const uint32_t jump=0xe51ff004; // ldr pc,[pc,#-4], followed by absolute address
    uint32_t back=reinterpret_cast<uintptr_t>(entry)+8;
    std::memcpy(stub,entry,8);std::memcpy(stub+8,&jump,4);std::memcpy(stub+12,&back,4);
    __builtin___clear_cache(reinterpret_cast<char *>(stub),reinterpret_cast<char *>(stub+16));
    if(mprotect(stub,4096,PROT_READ|PROT_EXEC))throw std::runtime_error("Canvas trampoline protection failed");
    long page=sysconf(_SC_PAGESIZE);uintptr_t start=reinterpret_cast<uintptr_t>(entry)&~(page-1);
    // Covers an entry that straddles a page boundary.
    size_t span=(reinterpret_cast<uintptr_t>(entry)+8-start+page-1)&~(page-1);
    if(mprotect(reinterpret_cast<void *>(start),span,PROT_READ|PROT_WRITE|PROT_EXEC))throw std::runtime_error("Canvas hook protection failed");
    uint32_t target=reinterpret_cast<uintptr_t>(replacement);
    std::memcpy(entry,&jump,4);std::memcpy(static_cast<unsigned char *>(entry)+4,&target,4);
    __builtin___clear_cache(static_cast<char *>(entry),static_cast<char *>(entry)+8);
    if(mprotect(reinterpret_cast<void *>(start),span,PROT_READ|PROT_EXEC))_exit(126);
    return stub;
}
