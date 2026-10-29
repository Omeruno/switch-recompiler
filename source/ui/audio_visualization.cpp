#include "audio_visualization.h"
#include <algorithm>
#include <cmath>
#include <complex>
namespace ui {
AudioVisualization AudioVisualization::Analyze(const std::array<int16_t,512>& input){
    AudioVisualization out;std::array<float,128> energy{};
    constexpr float Pi=3.14159265358979323846f;
    for(unsigned channel=0;channel<2;++channel){
        std::array<std::complex<float>,256> data{};float sum=0;
        for(unsigned n=0;n<256;++n){const float value=input[n*2+channel]/32768.0f;sum+=value*value;data[n]=value*(0.5f-0.5f*std::cos(2*Pi*n/255));}
        if(channel)out.rightRms=std::sqrt(sum/256);else out.leftRms=std::sqrt(sum/256);
        for(unsigned i=1,j=0;i<256;++i){unsigned bit=128;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(data[i],data[j]);}
        for(unsigned length=2;length<=256;length*=2){const auto step=std::polar(1.0f,-2*Pi/length);
            for(unsigned start=0;start<256;start+=length){std::complex<float> w=1;
                for(unsigned n=0;n<length/2;++n){auto a=data[start+n],b=data[start+n+length/2]*w;data[start+n]=a+b;data[start+n+length/2]=a-b;w*=step;}}}
        for(unsigned n=1;n<128;++n)energy[n]+=std::norm(data[n]);
    }
    for(unsigned band=0;band<32;++band){const unsigned first=1+band*127/32,last=1+(band+1)*127/32;float peak=0;
        for(unsigned n=first;n<last && n<128;++n)peak=std::max(peak,std::sqrt(energy[n])/128.0f);
        out.bands[band]=std::min(1.0f,std::log1p(peak*24)/std::log(25.0f));}
    return out;
}
}
