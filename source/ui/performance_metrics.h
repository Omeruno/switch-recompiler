#pragma once
#include <array>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace ui {
// Real monotonic host time: deliberately independent of fast-forward guest time.
inline uint64_t PerformanceNowNs() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
struct DurationCounter {
    uint64_t count=0,totalNs=0,maxNs=0;
    void Add(uint64_t ns) {++count;totalNs+=ns;maxNs=std::max(maxNs,ns);}
};
enum class RasterStage {Pixel,Stroke,Shape,Fill,Blit,Flood,Text,Readback,Count};
inline const char* RasterStageName(RasterStage s) {
    constexpr const char* names[]={"pixel","stroke","shape","fill","blit","flood","text","readback"};
    return names[size_t(s)];
}
class RasterMetrics {
public:
    using Clock=uint64_t(*)();
    explicit RasterMetrics(Clock clock=&PerformanceNowNs):clock_(clock) {}
    // Session boundary only: raster operations are synchronous and never yield.
    void Reset(bool enabled=false) {enabled_=enabled;depth_=0;stages_={};}
    bool Enabled() const {return enabled_;}
    const DurationCounter& Stage(RasterStage s) const {return stages_[size_t(s)];}
    class Scope {
    public:
        Scope(RasterMetrics& metrics,RasterStage stage):m_(metrics),stage_(stage),active_(metrics.enabled_) {
            if(active_ && m_.depth_++==0)start_=m_.clock_();
        }
        ~Scope() {
            if(active_ && --m_.depth_==0) {
                const auto end=m_.clock_();m_.stages_[size_t(stage_)].Add(end>=start_?end-start_:0);
            }
        }
        Scope(const Scope&)=delete;
        Scope& operator=(const Scope&)=delete;
    private:
        RasterMetrics& m_;RasterStage stage_;bool active_;uint64_t start_=0;
    };
private:
    Clock clock_;bool enabled_=false;unsigned depth_=0;
    std::array<DurationCounter,size_t(RasterStage::Count)> stages_{};
};
inline RasterMetrics g_rasterMetrics;

// Correlates removed platform input with a later changed frame accepted by the
// output adapter. This is NOT proof of causal drawing or input-to-photon time.
class InputFrameMetrics {
public:
    static constexpr size_t Capacity=64;
    struct Counters {
        DurationCounter queueWait,observedToFrame,removedToFrame;
        uint64_t coalesced=0,dropped=0,invalid=0,outputFrames=0;
    };
    void Reset(bool enabled=false) {enabled_=enabled;pending_={};next_=0;lastSerial_=0;counters_={};}
    bool Enabled() const {return enabled_;}
    const Counters& Stats() const {return counters_;}
    size_t Pending() const {size_t count=0;for(const auto& p:pending_)if(p.observed)++count;return count;}
    void Coalesced() {if(enabled_)++counters_.coalesced;}
    void Removed(uint64_t observed,uint64_t now,uint64_t screenChanges) {
        if(!enabled_ || !observed)return;
        if(observed>now){++counters_.invalid;return;}
        counters_.queueWait.Add(now-observed);
        auto& p=pending_[next_];if(p.observed)++counters_.dropped;
        p={observed,now,screenChanges,lastSerial_};next_=(next_+1)%Capacity;
    }
    // Call only AFTER a successful output queue/swap. Failed/no-op frames must
    // leave samples pending; a cursor-only redraw cannot consume a GDI sample.
    void OutputQueued(uint64_t now,uint64_t screenChanges,uint64_t contentSerial) {
        if(!enabled_)return;
        bool consumed=false;
        for(auto& p:pending_)if(p.observed && p.changes!=screenChanges && p.serial!=contentSerial && now>=p.removed) {
            counters_.observedToFrame.Add(now-p.observed);
            counters_.removedToFrame.Add(now-p.removed);p={};consumed=true;
        }
        lastSerial_=contentSerial;
        if(consumed)++counters_.outputFrames;
    }
private:
    struct PendingInput {uint64_t observed=0,removed=0,changes=0,serial=0;};
    bool enabled_=false;size_t next_=0;uint64_t lastSerial_=0;Counters counters_;
    std::array<PendingInput,Capacity> pending_{};
};
} // namespace ui
