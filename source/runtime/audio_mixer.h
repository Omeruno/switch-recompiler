#pragma once
#include "audio_output.h"
#include <deque>
#include <map>
namespace rt {
// Explicit native mixer over the production AudioOutput lease. Existing WinMM/
// MCI clients remain exclusive until migrated to this source contract.
class AudioMixer {
public:
    static constexpr size_t MaxSources=8,MaxPackets=4,MaxQueuedFrames=48000,Quantum=256;
    using Observer=std::function<void(bool)>; // false=played, true=confirmed cancel
    explicit AudioMixer(AudioOutput& output):output_(output){}
    ~AudioMixer();
    AudioMixer(const AudioMixer&)=delete;AudioMixer& operator=(const AudioMixer&)=delete;
    bool Begin(uint64_t owner);
    bool End();
    uint64_t CreateSource();
    AudioOutput::Result Write(uint64_t source,const std::vector<int16_t>&,Observer={});
    bool Pump();
    bool SetPaused(uint64_t source,bool);
    bool SetGain(uint64_t source,uint16_t left,uint16_t right);
    bool Gain(uint64_t source,uint16_t& left,uint16_t& right) const;
    bool ResetSource(uint64_t source);
    bool CloseSource(uint64_t source);
    // Last genuinely observed played-frame position; Pump services the device.
    bool Position(uint64_t source,uint64_t&) const;
    size_t Pending(uint64_t source) const;
    size_t SourceCount() const {return sources_.size();}
    bool Faulted() const {return fault_;}
private:
    struct Packet {uint64_t start=0;std::vector<int16_t> pcm;Observer done;};
    struct Source {uint64_t written=0,scheduled=0,played=0;uint16_t left=65535,right=65535;bool paused=false;std::deque<Packet> packets;};
    struct Span {uint64_t source=0,start=0;size_t frames=0;};
    struct MixBlock {uint64_t start=0;size_t frames=0;bool released=false,cancelled=false;std::vector<Span> spans;};
    struct Guard {bool& busy;bool acquired;explicit Guard(bool& b):busy(b),acquired(!b){if(acquired)busy=true;}~Guard(){if(acquired)busy=false;}explicit operator bool()const{return acquired;}};
    bool Owns() const;
    bool Live() const;
    bool Credit();
    bool Quiesce();
    void Cancel(Source&);
    static int16_t Sample(const Source&,uint64_t frame,unsigned channel);
    AudioOutput& output_;
    std::map<uint64_t,Source> sources_;
    std::deque<std::shared_ptr<MixBlock>> blocks_;
    uint64_t owner_=0,client_=0,nextSource_=1,base_=0,submitted_=0,played_=0;
    bool fault_=false,busy_=false;
};
}
