#include "audio_audout.h"
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
    ready_=false;
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
    if(!pending && ready_)return true;
    ready_=false;
    if(R_FAILED(audoutStopAudioOut()))return false;
    bool flushed=false;if(R_FAILED(audoutFlushAudioOutBuffers(&flushed)))return false;
    for(auto& slot:slots_)if(slot.token) {
        bool contains=true;
        if(R_FAILED(audoutContainsAudioOutBuffer(&slot.native,&contains)) || contains)return false;
    }
    bool drained=false;
    for(size_t n=0;n<=slots_.size();++n) {
        AudioOutBuffer* released=nullptr;u32 count=0;
        if(R_FAILED(audoutGetReleasedAudioOutBuffer(&released,&count)))return false;
        if(!count){drained=true;break;}
    }
    if(!drained)return false;
    for(auto& slot:slots_)slot={};
    ready_=R_SUCCEEDED(audoutStartAudioOut());
    return true; // revoked, even when restart failed (Available remains FALSE)
}
} // namespace rt
#endif
