#include "source/ui/pointer_input.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

static constexpr uint64_t A=1,L=2,B=4,ZR=8;
static void Startup() {
    ui::PointerInputGate gate;gate.Reset(L|A,true);
    auto e=gate.Update(L|A,true);assert(!e.down && !e.up && !e.held && !e.touching);
    e=gate.Update(L|A|B,true);assert(e.down==B && !e.up && e.held==B && !e.touching);
    e=gate.Update(L|B,false);assert(!e.down && !e.up && e.held==B && !e.touching);
    e=gate.Update(0,false);assert(!e.down && e.up==B && !e.held); // no orphan Alt-up/click-up
    e=gate.Update(L|ZR,true);assert(e.down==(L|ZR) && !e.up && e.touching);
    e=gate.Update(L|ZR,true);assert(!e.down && !e.up && e.held==(L|ZR));
    e=gate.Update(0,false);assert(e.up==(L|ZR) && !e.down && !e.touching);
    gate.Reset(0,false);e=gate.Update(A,false);assert(e.down==A && !e.up);
    e=gate.Update(0,false);assert(e.up==A && !e.down);
    // Each launch-held button is released independently; fresh inputs still work.
    gate.Reset(L|A,false);e=gate.Update(L,false);assert(!e.down && !e.up);
    e=gate.Update(L|A,false);assert(e.down==A && e.held==A);
    e=gate.Update(A,false);assert(!e.down && !e.up && e.held==A);
    e=gate.Update(A|L,false);assert(e.down==L && e.held==(A|L));
}
static float Travel(const std::vector<uint64_t>& periods,bool drawing,int32_t stick=32767) {
    ui::PointerInputGate gate;gate.Reset(0,false);
    ui::StickPointerMotion motion;uint64_t now=1000000;motion.ResetTime(now);
    float x=100,y=500;size_t i=0;
    while(now<3000000) {
        now=std::min<uint64_t>(3000000,now+periods[i++%periods.size()]);
        auto edges=gate.Update(drawing?A:0,false);
        if(drawing)assert(edges.held==A);
        motion.Advance(x,y,stick,0,now,10000,10000);
    }
    assert(y==500);return x-100;
}
int main() {
    Startup();
    const float expected=2*ui::StickPointerMotion::MaxPixelsPerSecond;
    // Hover can block on scanout; drawing can poll far more frequently. Both
    // travel the same distance over the same physical time, including jitter.
    for(auto periods:{std::vector<uint64_t>{16667},std::vector<uint64_t>{8000},
        std::vector<uint64_t>{1000},std::vector<uint64_t>{500},
        std::vector<uint64_t>{4000,32000,500,12000}}) {
        assert(std::fabs(Travel(periods,false)-expected)<0.3f);
        assert(std::fabs(Travel(periods,true)-expected)<0.3f);
        assert(std::fabs(Travel(periods,true,18000)-Travel({16667},false,18000))<0.3f);
        assert(std::fabs(Travel(periods,true,5000)-Travel({16667},false,5000))<0.003f);
    }
    ui::StickPointerMotion motion;motion.ResetTime(0);float x=100,y=100;
    assert(!motion.Advance(x,y,4000,-4000,16000,1280,720) && x==100 && y==100);
    // Fractional motion accumulates but does not flood the queue with an
    // unchanged integer cursor position on every tight PeekMessage poll.
    motion.ResetTime(0);unsigned events=0;x=100;y=100;
    for(uint64_t t=100;t<=10000;t+=100)if(motion.Advance(x,y,32767,0,t,1280,720))++events;
    assert(events==5 && std::fabs(x-105.4f)<0.05f);
    x=800;y=600; // touch reposition overrides the high-precision stick state
    assert(!motion.Advance(x,y,32767,0,11000,1280,720));
    assert(std::fabs(x-800.54f)<0.01f && y==600);
    assert(motion.Advance(x,y,32767,0,12000,1280,720) && std::fabs(x-801.08f)<0.01f);
    motion.ResetTime(0);x=1279;y=0;
    assert(!motion.Advance(x,y,32767,32767,16000,1280,720));
    motion.ResetTime(0);x=100;y=100;
    assert(motion.Advance(x,y,32767,-32768,5000000,1280,720));
    assert(std::fabs(x-127)<0.01f && std::fabs(y-127)<0.01f); // long-pause cap
    motion.ResetTime(6000000);assert(!motion.Advance(x,y,32767,0,1000,1280,720));
    motion.ResetTime(0);x=100;y=100;
    assert(motion.Advance(x,y,std::numeric_limits<int32_t>::min(),0,16000,1280,720));
    assert(x>=0 && x<100 && std::isfinite(x));
    assert(!motion.Advance(x,y,32767,0,20000,0,720));
    puts("PASSED: launch-held L/A/touch quarantine with paired new edges; equal hover/drag speed across 0.5-32ms polls, deadzone, fractional events, bounds and pause cap");
}
