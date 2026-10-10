#pragma once
#include <cmath>

namespace eject {
enum Result { running, complete, empty, cancelled, disconnected, stale, blocked,
              fault, stalled, travel_limit, timed_out, command_failed, stop_failed };
inline const char *name(Result r) {
    static const char *names[] = {"running","complete","empty","cancelled","disconnected",
        "stale","blocked","fault","stalled","travel_limit","timed_out","command_failed","stop_failed"};
    return names[r];
}
struct Sample {
    bool fresh=false, connected=false, idle=false, head_present=true, present=true, fault=false, moving=false;
    int active=-1;
    double position=0;
};
struct Machine {
    double started=0, last_motion=0, origin=0, progress=0;
    bool moving=false;
    Result result=blocked;
    Result begin(const Sample &s, double now) {
        result=check(s);
        if(result!=running)return result;
        if(!s.present)return result=empty;
        started=last_motion=now;origin=progress=s.position;moving=true;
        return result=running;
    }
    static Result check(const Sample &s) {
        if(!s.fresh)return stale;
        if(!s.connected)return disconnected;
        if(!s.idle||s.head_present||s.active!=-1)return blocked;
        if(s.fault||!std::isfinite(s.position))return fault;
        return running;
    }
    Result update(const Sample &s, double now, bool cancel) {
        if(!moving)return result;
        if(cancel)return result=cancelled;
        result=check(s);if(result!=running)return result;
        if(!s.present)return result=complete;
        if(std::fabs(s.position-origin)>=1200)return result=travel_limit;
        if(now-started>=90)return result=timed_out;
        if(std::fabs(s.position-progress)>=0.5){progress=s.position;last_motion=now;}
        if(now-last_motion>=3)return result=stalled;
        return result=running;
    }
};
// Every path after starting, including exceptions, requests motor stop.
template<class IO> Result run(IO &io) {
    Machine m;Sample s=io.sample();
    Result r=m.begin(s,io.now());
    if(r!=running){io.publish(r,0);return r;}
    struct Cleanup {
        IO &io;bool stopped=false;
        explicit Cleanup(IO &value):io(value){}
        ~Cleanup(){try{if(!stopped)io.stop();}catch(...){}}
    } cleanup(io);
    io.publish(running,0);
    s=io.sample();r=m.update(s,io.now(),io.cancelled());
    if(r==running&&!io.reverse())r=command_failed;
    while(r==running) {
        io.wait();s=io.sample();
        r=m.update(s,io.now(),io.cancelled());
        io.publish(running,std::fabs(s.position-m.origin));
    }
    cleanup.stopped=io.stop();
    // A stop acknowledgement alone cannot establish sensor clearance.
    if(r==complete&&cleanup.stopped){
        io.wait();s=io.sample();
        if(Machine::check(s)!=running||s.present||s.moving)r=fault;
    }
    if(!cleanup.stopped)r=stop_failed;
    io.publish(r,std::fabs(s.position-m.origin));
    return r;
}
}
