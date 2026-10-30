#include "audio_mixer.h"
#include <algorithm>
#include <limits>
namespace rt {
namespace {uint64_t nextMixerClient=0x2000000000000000ull;}
AudioMixer::~AudioMixer(){End();}
bool AudioMixer::Owns() const {return client_ && output_.Owner()==owner_ && output_.Client()==client_;}
bool AudioMixer::Live() const {return Owns() && !fault_ && output_.Available();}
bool AudioMixer::Begin(uint64_t owner) {
    Guard guard(busy_);if(!guard || client_ || !owner || owner!=output_.Owner() || !output_.Available())return false;
    if(nextMixerClient==UINT64_MAX)return false;
    const auto client=nextMixerClient++;
    if(!output_.Claim(owner,client))return false;
    uint64_t base=0;if(!output_.Position(owner,client,base)){output_.ReleaseClient(owner,client);return false;}
    owner_=owner;client_=client;base_=base;submitted_=played_=0;fault_=false;return true;
}
void AudioMixer::Cancel(Source& source) {
    auto packets=std::move(source.packets);source.packets.clear();source.written=source.scheduled=source.played=0;source.paused=false;
    for(auto& packet:packets)if(packet.done)packet.done(true);
}
bool AudioMixer::End() {
    Guard guard(busy_);if(!guard)return false;
    if(!client_)return true;
    if(!Owns())return false; // an outer lease can be restored after a nested scope
    // Observe already played data before revoking. Recovery may cancel all
    // remaining packets after a fault, but never label them as played.
    if(!fault_ && !Quiesce())fault_=true;
    if(fault_ && !output_.Reset()){fault_=true;return false;}
    for(auto& item:sources_)Cancel(item.second);
    sources_.clear();blocks_.clear();output_.ReleaseClient(owner_,client_);
    owner_=client_=0;submitted_=played_=base_=0;fault_=false;return true;
}
uint64_t AudioMixer::CreateSource() {
    Guard guard(busy_);if(!guard || !Live() || sources_.size()>=MaxSources || nextSource_==UINT64_MAX)return 0;
    const auto id=nextSource_++;sources_.emplace(id,Source{});return id;
}
AudioOutput::Result AudioMixer::Write(uint64_t id,const std::vector<int16_t>& pcm,Observer done) {
    Guard guard(busy_);if(!guard)return AudioOutput::Result::Busy;
    if(!Owns())return AudioOutput::Result::WrongOwner;
    if(!Live())return AudioOutput::Result::DeviceError;
    auto it=sources_.find(id);if(it==sources_.end())return AudioOutput::Result::Invalid;
    auto& s=it->second;const auto frames=pcm.size()/2;
    if(pcm.empty() || pcm.size()%2 || frames>MaxQueuedFrames || frames>UINT64_MAX-s.written)return AudioOutput::Result::Invalid;
    // Retained packets include their already played prefix until their final
    // frame. Bound the actual owning storage, not just the unread suffix.
    size_t retained=0;for(const auto& p:s.packets)retained+=p.pcm.size()/2;
    if(s.packets.size()>=MaxPackets || frames>MaxQueuedFrames-retained)return AudioOutput::Result::Busy;
    s.packets.push_back({s.written,pcm,std::move(done)});s.written+=frames;return AudioOutput::Result::Accepted;
}
bool AudioMixer::Credit() {
    uint64_t raw=0;
    if(!output_.Position(owner_,client_,raw) || raw<base_ || raw-base_<played_ || raw-base_>submitted_){fault_=true;return false;}
    played_=raw-base_;
    for(const auto& block:blocks_){
        if(block->cancelled){fault_=true;return false;}
        const auto elapsed=played_>block->start?std::min<uint64_t>(block->frames,played_-block->start):0;
        for(const auto& span:block->spans){auto it=sources_.find(span.source);if(elapsed && it!=sources_.end())
            it->second.played=std::max(it->second.played,span.start+std::min<uint64_t>(span.frames,elapsed));}
    }
    for(auto& item:sources_){auto& s=item.second;
        while(!s.packets.empty() && s.packets.front().start+s.packets.front().pcm.size()/2<=s.played){
            auto packet=std::move(s.packets.front());s.packets.pop_front();if(packet.done)packet.done(false);
        }
    }
    while(!blocks_.empty() && blocks_.front()->released && blocks_.front()->start+blocks_.front()->frames<=played_)blocks_.pop_front();
    return true;
}
bool AudioMixer::Quiesce() {
    if(!Live())return false;
    if(blocks_.empty() && !output_.Pending())return true;
    // Freeze the hardware cursor before taking credits. Reset confirms every
    // mixed descriptor revoked before source cursors rewind and tails rebuild.
    if(!output_.Pause(owner_,client_,true) || !Credit() || !output_.Reset()){fault_=true;return false;}
    blocks_.clear();submitted_=played_=0;
    for(auto& item:sources_)item.second.scheduled=item.second.played;
    if(!output_.Position(owner_,client_,base_)){fault_=true;return false;}
    return true;
}
int16_t AudioMixer::Sample(const Source& source,uint64_t frame,unsigned channel) {
    for(const auto& packet:source.packets)if(frame>=packet.start && frame-packet.start<packet.pcm.size()/2)
        return packet.pcm[size_t(frame-packet.start)*2+channel];
    return 0;
}
bool AudioMixer::Pump() {
    Guard guard(busy_);if(!guard || !Live())return false;
    if(!output_.Poll() || !Credit()){fault_=true;return false;}
    while(blocks_.size()<AudioOutput::Slots && output_.Pending()<AudioOutput::Slots){
        size_t frames=0;for(const auto& item:sources_)if(!item.second.paused)
            frames=std::max(frames,size_t(std::min<uint64_t>(Quantum,item.second.written-item.second.scheduled)));
        if(!frames)break;
        if(submitted_>UINT64_MAX-frames){fault_=true;return false;}
        auto block=std::make_shared<MixBlock>();block->start=submitted_;block->frames=frames;block->spans.reserve(MaxSources);
        for(const auto& item:sources_)if(!item.second.paused && item.second.written>item.second.scheduled)
            block->spans.push_back({item.first,item.second.scheduled,size_t(std::min<uint64_t>(frames,item.second.written-item.second.scheduled))});
        std::vector<int16_t> pcm(frames*2);
        for(size_t frame=0;frame<frames;++frame)for(unsigned channel=0;channel<2;++channel){
            int32_t sum=0;for(const auto& span:block->spans)if(frame<span.frames){const auto& s=sources_.find(span.source)->second;
                sum+=int32_t(Sample(s,span.start+frame,channel))*(channel?s.right:s.left)/65535;}
            pcm[frame*2+channel]=int16_t(std::max(-32768,std::min(32767,sum)));
        }
        std::weak_ptr<MixBlock> weak=block;
        const auto result=output_.Submit(owner_,pcm,[weak](bool cancelled){if(auto b=weak.lock()){b->cancelled=cancelled;b->released=!cancelled;}},client_);
        if(result!=AudioOutput::Result::Accepted){fault_=true;return false;}
        for(const auto& span:block->spans)sources_.find(span.source)->second.scheduled+=span.frames;
        blocks_.push_back(std::move(block));submitted_+=frames;
    }
    return true;
}
bool AudioMixer::SetPaused(uint64_t id,bool paused) {
    Guard guard(busy_);if(!guard || !Live())return false;
    auto it=sources_.find(id);if(it==sources_.end())return false;
    if(it->second.paused==paused)return true;
    if(!Quiesce())return false;
    it->second.paused=paused;return true;
}
bool AudioMixer::SetGain(uint64_t id,uint16_t left,uint16_t right) {
    Guard guard(busy_);if(!guard || !Live())return false;
    auto it=sources_.find(id);if(it==sources_.end())return false;
    if(it->second.left==left && it->second.right==right)return true;
    if(!Quiesce())return false;
    it->second.left=left;it->second.right=right;return true;
}
bool AudioMixer::Gain(uint64_t id,uint16_t& left,uint16_t& right) const {
    const auto it=sources_.find(id);if(!Live() || it==sources_.end())return false;
    left=it->second.left;right=it->second.right;return true;
}
bool AudioMixer::ResetSource(uint64_t id) {
    Guard guard(busy_);if(!guard || !Live())return false;
    auto it=sources_.find(id);if(it==sources_.end() || !Quiesce())return false;
    Cancel(it->second);return true;
}
bool AudioMixer::CloseSource(uint64_t id) {
    Guard guard(busy_);if(!guard || !Live())return false;
    auto it=sources_.find(id);if(it==sources_.end() || !Quiesce())return false;
    Cancel(it->second);sources_.erase(it);return true;
}
bool AudioMixer::Position(uint64_t id,uint64_t& position) const {
    const auto it=sources_.find(id);if(!Live() || it==sources_.end())return false;
    position=it->second.played;return true;
}
size_t AudioMixer::Pending(uint64_t id) const {const auto it=sources_.find(id);return it==sources_.end()?0:it->second.packets.size();}
}
