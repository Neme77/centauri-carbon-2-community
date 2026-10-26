#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "reap_core.h"
#include <cassert>
#ifdef NDEBUG
#error These qualification tests require active assertions.
#endif
#include <cstdlib>
#include <new>
#include <thread>
#include <vector>
#include <atomic>
#include <cstdio>
#include <cerrno>
#include <chrono>
static std::atomic<int> calls{0};
static void handle(void *,int){calls++;}
static void reject(void *,int){assert(false);}
static void *allocate(size_t n){return ::operator new(n,std::nothrow);}
static void release(void *p){::operator delete(p);}
int main(){
    static_assert(sizeof(std::thread)==sizeof(pthread_t));
    static_assert(sizeof(std::vector<std::thread>)==sizeof(cc2_thread_vector));
    std::vector<std::thread> native;
    cc2_reap_ops ops={allocate,release,pthread_create,pthread_tryjoin_np,handle,reject};
    cc2_reap_stats stats{};
    for(int i=0;i<600;i++){
        assert(!cc2_reap_spawn(reinterpret_cast<cc2_thread_vector *>(&native),nullptr,i,&ops,&stats));
        while(calls.load()<i+1)std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(i%10==0){for(auto &t:native)if(t.joinable())t.join();native.clear();}
    }
    for(auto &t:native)if(t.joinable())t.join();
    native.clear();
    assert(calls==600&&native.capacity()<=CC2_REAP_MAX_RETAINED);
    puts("PASS: real libstdc++ vector/std::thread join, clear, allocation and destruction with raw pthread workers");
}
