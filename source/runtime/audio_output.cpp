#include "audio_output.h"
#include <algorithm>
#include <cstring>
#include <limits>
namespace rt {
AudioBlock::AudioBlock(const std::vector<int16_t>& pcm) {
    bytes_=pcm.size()*sizeof(int16_t);padded_=(bytes_+4095)&~size_t(4095);
    storage_.resize(padded_+4095);
    offset_=(4096-(reinterpret_cast<uintptr_t>(storage_.data())&4095))&4095;
    std::memcpy(storage_.data()+offset_,pcm.data(),bytes_);
}
AudioOutput::~AudioOutput(){Reset();} // device retains ownership if revocation fails
bool AudioOutput::Bind(AudioDevice* device) {
    if(device_==device)return true;
    if(!Reset())return false;
    device_=device;fault_=false;return true;
}
uint64_t AudioOutput::Enter() {
    Reset();client_=0;
    owner_=nextOwner_==std::numeric_limits<uint64_t>::max()?0:nextOwner_++;
    return owner_;
}
void AudioOutput::Leave(uint64_t owner,uint64_t previous,uint64_t previousClient) {
    if(owner_!=owner)return;
    Reset();owner_=previous;client_=previousClient;
}
size_t AudioOutput::Pending() const {size_t n=0;for(const auto& t:tickets_)n+=bool(t.id);return n;}
bool AudioOutput::Reset() {
    if(device_ && !device_->Reset()){fault_=true;return false;}
    auto retired=std::move(tickets_);tickets_={};
    for(auto& t:retired)if(t.id){++cancelled_;if(t.done)t.done(true);}
    fault_=false;return true;
}
bool AudioOutput::Poll() {
    if(!device_)return true;
    for(size_t n=0;n<=Slots;++n) {
        uint64_t id=0;const auto status=device_->Poll(id);
        if(status==AudioDevice::Completion::None)return !fault_;
        if(status==AudioDevice::Completion::Error){fault_=true;return false;}
        auto found=std::find_if(tickets_.begin(),tickets_.end(),[&](const Ticket& t){return id && t.id==id;});
        if(found==tickets_.end()){fault_=true;return false;}
        auto retired=std::move(*found);*found={};++completed_;
        if(retired.done)retired.done(false);
    }
    fault_=true;return false;
}
bool AudioOutput::Claim(uint64_t owner,uint64_t client) {
    if(!owner || owner!=owner_ || !client || client_ || Pending())return false;
    client_=client;return true;
}
bool AudioOutput::ReleaseClient(uint64_t owner,uint64_t client) {
    if(owner!=owner_ || !client || client_!=client)return false;
    client_=0;return true;
}
AudioOutput::Result AudioOutput::Submit(uint64_t owner,const std::vector<int16_t>& pcm,std::function<void(bool)> done,uint64_t client) {
    if(!owner || owner!=owner_ || (client_ && client!=client_))return Result::WrongOwner;
    if(pcm.empty() || pcm.size()%2 || pcm.size()>MaxSamples)return Result::Invalid;
    if(!Poll())return Result::DeviceError;
    if(!Available())return Result::Unavailable;
    auto slot=std::find_if(tickets_.begin(),tickets_.end(),[](const Ticket& t){return !t.id;});
    if(slot==tickets_.end())return Result::Busy;
    if(nextTicket_==std::numeric_limits<uint64_t>::max())return Result::DeviceError;
    const auto id=nextTicket_++;
    auto block=std::make_shared<const AudioBlock>(pcm);
    if(!device_->Submit(id,block)){fault_=true;return Result::DeviceError;}
    slot->id=id;slot->block=std::move(block);slot->done=std::move(done);++accepted_;return Result::Accepted;
}
AudioOutput& Audio(){static AudioOutput output;return output;}
namespace {
uint16_t U16(const std::vector<uint8_t>& b,size_t at){return uint16_t(b[at])|uint16_t(b[at+1])<<8;}
uint32_t U32(const std::vector<uint8_t>& b,size_t at){return uint32_t(U16(b,at))|uint32_t(U16(b,at+2))<<16;}
}
bool DecodeWave(const std::vector<uint8_t>& bytes,std::vector<int16_t>& out) {
    out.clear();constexpr size_t MaxInput=1024*1024;
    if(bytes.size()<12 || bytes.size()>MaxInput || std::memcmp(bytes.data(),"RIFF",4) || std::memcmp(bytes.data()+8,"WAVE",4))return false;
    const uint64_t end=uint64_t(U32(bytes,4))+8;
    if(end<12 || end>bytes.size())return false;
    bool fmt=false,data=false;uint16_t channels=0,bits=0,align=0;uint32_t rate=0;size_t start=0,length=0;
    for(size_t pos=12;pos<end;) {
        if(end-pos<8)return false;
        const auto count=U32(bytes,pos+4);const uint64_t next=uint64_t(pos)+8+count+(count&1);
        if(next>end)return false;
        if(!std::memcmp(bytes.data()+pos,"fmt ",4)) {
            if(fmt || count<16 || U16(bytes,pos+8)!=1)return false;
            channels=U16(bytes,pos+10);rate=U32(bytes,pos+12);align=U16(bytes,pos+20);bits=U16(bytes,pos+22);
            if((channels!=1 && channels!=2) || (bits!=8 && bits!=16) || rate<8000 || rate>96000 ||
               align!=channels*(bits/8) || U32(bytes,pos+16)!=uint64_t(rate)*align)return false;
            fmt=true;
        }else if(!std::memcmp(bytes.data()+pos,"data",4)) {
            if(data)return false;
            data=true;start=pos+8;length=count;
        }
        pos=size_t(next);
    }
    if(!fmt || !data || !length || length%align)return false;
    const size_t frames=length/align;
    const uint64_t outputFrames=uint64_t(frames)*48000/rate;
    if(!outputFrames || outputFrames>AudioOutput::MaxFrames)return false;
    out.resize(size_t(outputFrames)*2);
    for(size_t frame=0;frame<outputFrames;++frame) {
        const auto source=std::min(size_t(uint64_t(frame)*rate/48000),frames-1);
        for(unsigned channel=0;channel<2;++channel) {
            const auto at=start+source*align+(channels==1?0:channel)*(bits/8);
            const int32_t sample=bits==8?(int32_t(bytes[at])-128)*256:int32_t(U16(bytes,at))-(U16(bytes,at)&0x8000?65536:0);
            out[frame*2+channel]=int16_t(sample);
        }
    }
    return true;
}
} // namespace rt
