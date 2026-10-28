#include "pcm_stream.h"
#include <limits>
namespace rt {
bool PcmStream::Push(const std::vector<int16_t>& data,bool final,std::vector<int16_t>& out) {
    out.clear();if(!format_.Valid() || finished_ || data.size()%format_.channels)return false;
    const auto frames=data.size()/format_.channels;
    if(frames>size_t(format_.rate)*5 || input_>std::numeric_limits<uint64_t>::max()/96000-frames)return false;
    const uint64_t end=input_+frames,target=end*48000/format_.rate;
    if(target-output_>48000*5+8)return false;
    auto sample=[&](uint64_t frame,unsigned channel){
        if(frame<input_)return last_[channel];
        if(frame>=end)return frames?data[(frames-1)*format_.channels+channel]:last_[channel];
        return data[size_t(frame-input_)*format_.channels+channel];
    };
    while(output_<target) {
        const uint64_t at=output_*format_.rate,frame=at/48000,fraction=at%48000;
        if(!final && frame+(interpolate_ && bool(fraction))>=end)break;
        for(unsigned ch=0;ch<2;++ch){const auto c=format_.channels==1?0:ch;const int32_t a=sample(frame,c),b=interpolate_?sample(frame+1,c):a;
            out.push_back(int16_t((int64_t(a)*(48000-int64_t(fraction))+int64_t(b)*int64_t(fraction))/48000));}
        ++output_;
    }
    if(frames)for(unsigned c=0;c<format_.channels;++c)last_[c]=data[(frames-1)*format_.channels+c];
    input_=end;finished_=final;return true;
}
bool PcmStream::Bytes(const std::vector<uint8_t>& bytes,std::vector<int16_t>& out) {
    if(!format_.Valid() || bytes.empty() || bytes.size()%format_.Align())return false;
    std::vector<int16_t> pcm;pcm.reserve(bytes.size()/(format_.bits/8));
    for(size_t n=0;n<bytes.size();n+=format_.bits/8){
        const uint32_t v=format_.bits==8?bytes[n]:uint32_t(bytes[n])|uint32_t(bytes[n+1])<<8;
        pcm.push_back(format_.bits==8?int16_t((int32_t(v)-128)*256):int16_t(int32_t(v)-(v&0x8000?65536:0)));
    }
    return Push(pcm,false,out);
}
}
