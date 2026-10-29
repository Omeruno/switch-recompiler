#include "source/ui/audio_visualization.h"
#include <cassert>
#include <cmath>
#include <algorithm>
#include <cstdio>
int main(){
    std::array<int16_t,512> pcm{};auto silent=ui::AudioVisualization::Analyze(pcm);for(auto value:silent.bands)assert(value==0);assert(!silent.leftRms && !silent.rightRms);
    for(unsigned n=0;n<256;++n){const auto value=int16_t(16000*std::sin(6.283185307179586*3000*n/48000));pcm[n*2]=value;pcm[n*2+1]=-value;}
    const auto result=ui::AudioVisualization::Analyze(pcm);const auto strongest=std::max_element(result.bands.begin(),result.bands.end())-result.bands.begin();
    assert(strongest==4 && result.bands[4]>0.5f && result.leftRms>0.3f && result.leftRms<0.4f && std::abs(result.leftRms-result.rightRms)<0.0001f);
    assert(result.bands[16]<0.01f);auto same=ui::AudioVisualization::Analyze(pcm);assert(same.bands==result.bands);
    puts("Audio visualization PASS (silence,actual3kHz FFT peak,anti-phase stereo energy,RMS and deterministic paused samples;no clock/random animation)");
}
