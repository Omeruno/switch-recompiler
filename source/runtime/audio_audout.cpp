#include "audio_audout.h"
#include <algorithm>
#ifdef __SWITCH__
namespace rt {
bool AudoutDevice::Initialize() {
    if(initialized_)return ready_;
    if(R_FAILED(audoutInitialize()))return false;
    initialized_=true;
    if(audoutGetSampleRate()!=48000 || audoutGetChannelCount()!=2 || audoutGetPcmFormat()!=PcmFormat_Int16 || R_FAILED(audoutStartAudioOut())) {
        Shutdown();return false;
    }
    ready_=true;return true;
}
void AudoutDevice::Shutdown() {
    ready_=false;paused_=false;lastRaw_=positionOffset_=0;
    if(initialized_) {
        audoutStopAudioOut();
        // Closing the IAudioOut session revokes its buffers before host owners drop.
        audoutExit();initialized_=false;
    }
    for(auto& slot:slots_)slot={};
}
bool AudoutDevice::Submit(uint64_t token,std::shared_ptr<const AudioBlock> block) {
    if(!ready_ || !token || !block)return false;
    for(auto& slot:slots_)if(!slot.token) {
        slot.token=token;slot.block=std::move(block);
        slot.native.buffer=const_cast<void*>(slot.block->Data());
        slot.native.buffer_size=slot.block->Capacity();slot.native.data_size=slot.block->Bytes();
        if(R_FAILED(audoutAppendAudioOutBuffer(&slot.native))) {
            // Conservatively retain the binding on ambiguous IPC failure.
            ready_=false;return false;
        }
        return true;
    }
    return false;
}
AudioDevice::Completion AudoutDevice::Poll(uint64_t& token) {
    token=0;if(!initialized_)return Completion::None;
    AudioOutBuffer* released=nullptr;u32 count=0;
    if(R_FAILED(audoutGetReleasedAudioOutBuffer(&released,&count))){ready_=false;return Completion::Error;}
    if(!count)return Completion::None;
    if(count==1)for(auto& slot:slots_)if(slot.token && released==&slot.native) {
        token=slot.token;slot={};return Completion::Released;
    }
    ready_=false;return Completion::Error;
}
bool AudoutDevice::Reset() {
    if(!initialized_)return true;
    bool pending=false;for(const auto& slot:slots_)pending|=bool(slot.token);
    if(!pending && ready_ && !paused_)return true;
    ready_=false;
    if(R_FAILED(audoutStopAudioOut()))return false;
    bool flushed=false;if(R_FAILED(audoutFlushAudioOutBuffers(&flushed)))return false;
    // Contains may include buffers in the released queue until they are
    // retrieved. Drain first, retaining every descriptor/PCM owner throughout.
    bool drained=false;
    std::array<bool,AudioOutput::Slots> seen{};
    for(size_t n=0;n<=slots_.size();++n) {
        AudioOutBuffer* released=nullptr;u32 count=0;
        if(R_FAILED(audoutGetReleasedAudioOutBuffer(&released,&count)))return false;
        if(!count){drained=true;break;}
        const auto found=std::find_if(slots_.begin(),slots_.end(),[&](const auto& slot){return slot.token && released==&slot.native;});
        if(count!=1 || found==slots_.end())return false;
        const auto index=size_t(found-slots_.begin());if(seen[index])return false;seen[index]=true;
    }
    if(!drained)return false;
    for(auto& slot:slots_)if(slot.token) {
        bool contains=true;
        if(R_FAILED(audoutContainsAudioOutBuffer(&slot.native,&contains)) || contains)return false;
    }
    for(auto& slot:slots_)slot={};
    ready_=R_SUCCEEDED(audoutStartAudioOut());paused_=false;lastRaw_=positionOffset_=0;
    return true; // revoked, even when restart failed (Available remains FALSE)
}
bool AudoutDevice::Position(uint64_t& frames) {
    if(!initialized_ || !ready_)return false;
    u64 raw=0;if(R_FAILED(audoutGetAudioOutPlayedSampleCount(&raw)))return false;
    // Preserve played frames if a native stop/start resets its raw counter.
    if(raw<lastRaw_)positionOffset_+=lastRaw_;
    lastRaw_=raw;frames=positionOffset_+raw;return true;
}
bool AudoutDevice::SetPaused(bool paused) {
    if(!ready_)return false;
    if(paused==paused_)return true;
    uint64_t before=0;if(!Position(before))return false;
    const auto result=paused?audoutStopAudioOut():audoutStartAudioOut();
    if(R_FAILED(result)){ready_=false;return false;}
    paused_=paused;return Position(before);
}
bool AudoutDevice::SetVolume(uint16_t value) {
    return ready_ && VolumeSupported() && R_SUCCEEDED(audoutSetAudioOutVolume(float(value)/65535.0f));
}
bool AudoutDevice::Volume(uint16_t& value) {
    float volume=0;
    if(!ready_ || !VolumeSupported() || R_FAILED(audoutGetAudioOutVolume(&volume)) || !(volume>=0 && volume<=1))return false;
    value=uint16_t(volume*65535.0f+0.5f);return true;
}
} // namespace rt
#endif
