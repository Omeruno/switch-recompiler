#pragma once
#include <algorithm>
#include <cstdint>

namespace ui {

struct PointerInputEdges {
    uint64_t down=0,up=0,held=0;
    bool touching=false;
};

// Launch-selection buttons/touch belong to the launcher until released.
// Produce paired guest edges only for subsequent presses, independently per bit.
class PointerInputGate {
public:
    void Reset(uint64_t held,bool touching) {blocked_=held;accepted_=0;blockedTouch_=touching;}
    PointerInputEdges Update(uint64_t held,bool touching) {
        blocked_&=held;
        const uint64_t accepted=held&~blocked_;
        PointerInputEdges edges{accepted&~accepted_,accepted_&~accepted,accepted,false};
        accepted_=accepted;
        if(!touching)blockedTouch_=false;
        edges.touching=touching && !blockedTouch_;
        return edges;
    }
private:
    uint64_t blocked_=0,accepted_=0;
    bool blockedTouch_=false;
};

// Integrate physical speed, not pixels per poll. Hover scanout, drawing and
// busy PeekMessage loops may all poll at different rates. Bound long pauses.
class StickPointerMotion {
public:
    static constexpr uint64_t MaxStepUs=50000;
    static constexpr float MaxPixelsPerSecond=540.0f; // former 9 px at 60 Hz
    void ResetTime(uint64_t nowUs) {lastUs_=nowUs;positionReady_=false;}
    bool Advance(float& x,float& y,int32_t stickX,int32_t stickY,uint64_t nowUs,int width,int height) {
        const auto elapsed=nowUs>=lastUs_?std::min(nowUs-lastUs_,MaxStepUs):0;
        lastUs_=nowUs;
        if(width<=0 || height<=0)return false;
        const int oldX=static_cast<int>(x),oldY=static_cast<int>(y);
        if(!positionReady_ || x!=publishedX_ || y!=publishedY_) {preciseX_=x;preciseY_=y;positionReady_=true;}
        const double dt=static_cast<double>(elapsed)/1000000.0;
        preciseX_=std::max(0.0,std::min(preciseX_+Axis(stickX)*dt,static_cast<double>(width-1)));
        preciseY_=std::max(0.0,std::min(preciseY_-Axis(stickY)*dt,static_cast<double>(height-1)));
        x=publishedX_=static_cast<float>(preciseX_);y=publishedY_=static_cast<float>(preciseY_);
        return oldX!=static_cast<int>(x) || oldY!=static_cast<int>(y);
    }
private:
    static float Axis(int32_t value) {
        const int64_t magnitude=value<0?-int64_t(value):int64_t(value);
        if(magnitude<=4000)return 0.0f;
        const float t=static_cast<float>(std::min<int64_t>(magnitude,32767)-4000)/(32767-4000);
        return (value<0?-1.0f:1.0f)*t*t*MaxPixelsPerSecond;
    }
    uint64_t lastUs_=0;
    double preciseX_=0,preciseY_=0;
    float publishedX_=0,publishedY_=0;
    bool positionReady_=false;
};
} // namespace ui
