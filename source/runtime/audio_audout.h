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
private:
    struct Slot {AudioOutBuffer native{};uint64_t token=0;std::shared_ptr<const AudioBlock> block;};
    std::array<Slot,AudioOutput::Slots> slots_{};
    bool initialized_=false,ready_=false;
};
}
#endif
