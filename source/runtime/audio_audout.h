#pragma once
#ifdef __SWITCH__
#include "audio_output.h"
#include <switch.h>
namespace rt {
class AudoutDevice final:public AudioDevice {
public:
    ~AudoutDevice() override {Shutdown();}
    bool Initialize();
    void Shutdown();
    bool Available() const override {return ready_;}
    const char* Name() const override {return "Switch audout PCM";}
    bool Submit(uint64_t,std::shared_ptr<const AudioBlock>) override;
    Completion Poll(uint64_t&) override;
    bool Reset() override;
    bool SetPaused(bool) override;
    bool Position(uint64_t&) override;
    bool VolumeSupported() const override {return hosversionAtLeast(6,0,0);}
    bool SetVolume(uint16_t) override;
    bool Volume(uint16_t&) override;
private:
    struct Slot {AudioOutBuffer native{};uint64_t token=0;std::shared_ptr<const AudioBlock> block;};
    std::array<Slot,AudioOutput::Slots> slots_{};
    bool initialized_=false,ready_=false,paused_=false;
    uint64_t lastRaw_=0,positionOffset_=0;
};
}
#endif
