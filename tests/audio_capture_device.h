#pragma once
#include "source/runtime/audio_output.h"
#include <map>
#include <deque>
#include <cstring>
// Actual owning PCM storage/explicit completion driver for host software tests.
// This captures samples; it does not claim speakers or Switch audout execution.
class AudioCaptureDevice:public rt::AudioDevice {
public:
    bool ready=true,submitFails=false,pollFails=false,resetFails=false;
    std::map<uint64_t,std::shared_ptr<const rt::AudioBlock>> active;
    std::deque<uint64_t> released;
    std::vector<std::vector<int16_t>> submitted;
    bool Available() const override {return ready;}
    const char* Name() const override {return "Public PCM capture";}
    bool Submit(uint64_t id,std::shared_ptr<const rt::AudioBlock> block) override {
        active[id]=block; // owns even on ambiguous failure
        if(submitFails)return false;
        const auto* samples=static_cast<const int16_t*>(block->Data());
        submitted.emplace_back(samples,samples+block->Bytes()/2);return true;
    }
    Completion Poll(uint64_t& id) override {
        if(pollFails)return Completion::Error;
        if(released.empty())return Completion::None;
        id=released.front();released.pop_front();active.erase(id);return Completion::Released;
    }
    bool Reset() override {
        if(resetFails)return false;
        active.clear();released.clear();return true;
    }
};
