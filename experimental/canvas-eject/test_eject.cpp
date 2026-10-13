#include "eject.h"
#include <cassert>
#include <cstdio>
#include <stdexcept>
#include <vector>
struct Mock {
    eject::Sample s;double time=0;int moves=0,stops=0,polls=0;
    bool cancel=false,stop_ok=true,throw_on_wait=false,lose=false;
    eject::Result published=eject::blocked;
    Mock(){s.fresh=s.connected=s.idle=s.present=true;s.head_present=false;}
    eject::Sample sample(){return s;}
    double now(){return time;}
    void wait(){if(throw_on_wait)throw std::runtime_error("transport");time+=0.1;polls++;s.position-=1;if(polls==4)s.present=false;if(lose)s.fresh=false;}
    bool cancelled(){return cancel;}
    bool reverse(){moves++;return true;}
    bool stop(){stops++;return stop_ok;}
    void publish(eject::Result r,double){published=r;}
};
struct StopMock {
    eject::Sample s;
    double time=0;int polls=0,requests=0;
    bool ack=true,advance=false,connected=true,fresh=true;
    int moving_until=0;
    StopMock(){s.sequence=10;s.fresh=s.connected=true;s.moving=false;}
    bool request_stop(){requests++;return ack;}
    eject::Sample sample(){return s;}
    double now(){return time;}
    void wait(){
        time+=0.1;polls++;
        if(advance){s.sequence++;s.moving=polls<=moving_until;s.connected=connected;s.fresh=fresh;}
    }
};
int main(){
    // This older but still-fresh stationary frame must not confirm a stop.
    StopMock old;assert(!eject::stop_and_confirm(old));assert(old.requests==1&&old.polls>=10);
    StopMock next;next.advance=true;assert(eject::stop_and_confirm(next));assert(next.polls==1);
    StopMock moving;moving.advance=true;moving.moving_until=3;
    assert(eject::stop_and_confirm(moving));assert(moving.polls==4);
    StopMock disconnected;disconnected.advance=true;disconnected.connected=false;
    assert(!eject::stop_and_confirm(disconnected));
    StopMock stale;stale.advance=true;stale.fresh=false;assert(!eject::stop_and_confirm(stale));
    StopMock no_ack;no_ack.ack=false;assert(!eject::stop_and_confirm(no_ack));assert(!no_ack.polls);
    Mock ok;assert(eject::run(ok)==eject::complete);assert(ok.moves==1&&ok.stops==1);
    Mock absent;absent.s.present=false;assert(eject::run(absent)==eject::empty);assert(absent.moves==0&&absent.stops==0);
    Mock head;head.s.head_present=true;assert(eject::run(head)==eject::blocked);assert(head.moves==0);
    Mock active;active.s.active=2;assert(eject::run(active)==eject::blocked);
    Mock print;print.s.idle=false;assert(eject::run(print)==eject::blocked);
    Mock bad;bad.s.fault=true;assert(eject::run(bad)==eject::fault);
    Mock cancel;cancel.cancel=true;assert(eject::run(cancel)==eject::cancelled);assert(cancel.moves==0&&cancel.stops==1);
    Mock lost;lost.lose=true;assert(eject::run(lost)==eject::stale);assert(lost.stops==1);
    Mock stop;stop.stop_ok=false;assert(eject::run(stop)==eject::stop_failed);assert(stop.stops==2);
    Mock ex;ex.throw_on_wait=true;try{eject::run(ex);assert(false);}catch(const std::runtime_error &){}assert(ex.stops==1);
    eject::Machine m;Mock d;assert(m.begin(d.s,0)==eject::running);assert(m.update(d.s,3,false)==eject::stalled);
    m.begin(d.s,0);d.s.position=-1200;assert(m.update(d.s,1,false)==eject::travel_limit);
    d.s.position=0;m.begin(d.s,0);d.s.position=-1;assert(m.update(d.s,90,false)==eject::timed_out);
    d.s.fresh=false;assert(m.update(d.s,91,false)==eject::stale);
    d.s.fresh=true;d.s.connected=false;assert(m.update(d.s,92,false)==eject::disconnected);
    puts("PASS sensor eject, empty/nozzle/print guards, cancellation, disconnect, stall, limits and exception cleanup");
}
