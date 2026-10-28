#pragma once
#include <cstdint>
#include <vector>
namespace rt {
struct PcmFormat {
    uint32_t rate=48000;uint16_t channels=2,bits=16;
    bool Valid() const {return rate>=8000 && rate<=96000 && (channels==1 || channels==2) && (bits==8 || bits==16);}
    uint16_t Align() const {return channels*(bits/8);}
};
// Linear, continuous phase; one input frame retained across arbitrary chunks.
// A non-final chunk can defer its fractional tail until the next source frame.
class PcmStream {
public:
    explicit PcmStream(PcmFormat format={},bool interpolate=true):format_(format),interpolate_(interpolate){}
    bool Push(const std::vector<int16_t>& input,bool final,std::vector<int16_t>& out);
    bool Bytes(const std::vector<uint8_t>& input,std::vector<int16_t>& out);
    uint64_t InputFrames() const {return input_;}
    uint64_t OutputFrames() const {return output_;}
private:
    PcmFormat format_;
    uint64_t input_=0,output_=0;
    int16_t last_[2]={};
    bool finished_=false,interpolate_=true;
};
}
