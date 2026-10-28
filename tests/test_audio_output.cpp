#include "tests/audio_capture_device.h"
#include <cassert>
#include <cstdio>
using A=rt::AudioOutput;
static std::vector<uint8_t> Wave(uint16_t channels=2,uint16_t bits=16,uint32_t rate=48000){
    std::vector<uint8_t> b;
    auto u16=[&](uint16_t v){b.push_back(uint8_t(v));b.push_back(uint8_t(v>>8));};
    auto u32=[&](uint32_t v){u16(uint16_t(v));u16(uint16_t(v>>16));};
    auto text=[&](const char* s){b.insert(b.end(),s,s+4);};
    text("RIFF");u32(44);text("WAVE");text("fmt ");u32(16);u16(1);u16(channels);u32(rate);u32(rate*channels*(bits/8));u16(channels*(bits/8));u16(bits);text("data");u32(8);u16(1000);u16(uint16_t(-2000));u16(3000);u16(uint16_t(-4000));return b;
}
int main(){
    AudioCaptureDevice device;A output;assert(output.Bind(&device));const auto owner=output.Enter();
    std::vector<int16_t> pcm={1000,-2000,3000,-4000};
    assert(output.Submit(0,pcm)==A::Result::WrongOwner);assert(output.Submit(owner,{1})==A::Result::Invalid);
    for(unsigned n=0;n<4;++n)assert(output.Submit(owner,pcm)==A::Result::Accepted);
    assert(output.Pending()==4 && device.active.size()==4);
    const auto first=device.active.begin()->first;const auto block=device.active.begin()->second;
    assert(!(reinterpret_cast<uintptr_t>(block->Data())&4095) && !(block->Capacity()&4095) && block->Bytes()==8);
    for(size_t n=block->Bytes();n<block->Capacity();++n)assert(static_cast<const uint8_t*>(block->Data())[n]==0);
    pcm[0]=777;assert(static_cast<const int16_t*>(block->Data())[0]==1000);
    assert(output.Submit(owner,pcm)==A::Result::Busy && device.active.begin()->second==block);
    const auto last=device.active.rbegin()->first;device.released.push_back(last);assert(output.Poll() && output.Completed()==1 && output.Pending()==3);
    assert(output.Submit(owner,pcm)==A::Result::Accepted);
    device.released.push_back(last);assert(!output.Poll() && output.Pending()==4); // stale completion cannot free new token
    device.resetFails=true;assert(!output.Reset() && output.Pending()==4 && device.active.size()==4);
    assert(!output.Bind(nullptr) && output.Pending()==4);
    assert(output.Submit(owner,pcm)==A::Result::DeviceError);
    device.resetFails=false;assert(output.Reset() && !output.Pending() && output.Cancelled()==4);
    device.submitFails=true;assert(output.Submit(owner,pcm)==A::Result::DeviceError && !device.active.empty());
    assert(output.Reset());device.submitFails=false;
    assert(output.Submit(owner,pcm)==A::Result::Accepted);device.pollFails=true;assert(!output.Poll() && output.Pending()==1);
    device.pollFails=false;assert(output.Reset());
    const auto inner=output.Enter();assert(output.Submit(owner,pcm)==A::Result::WrongOwner);
    assert(output.Submit(inner,pcm)==A::Result::Accepted);output.Leave(inner,owner);assert(!output.Pending() && output.Owner()==owner);
    assert(output.Submit(owner,std::vector<int16_t>(A::MaxSamples+2))==A::Result::Invalid);
    device.ready=false;assert(output.Submit(owner,pcm)==A::Result::Unavailable);device.ready=true;
    std::vector<bool> retirements;assert(output.Claim(owner,1234));
    assert(output.Submit(owner,pcm)==A::Result::WrongOwner);
    assert(output.Submit(owner,pcm,[&](bool cancelled){retirements.push_back(cancelled);},1234)==A::Result::Accepted);
    device.released.push_back(device.active.begin()->first);assert(output.Poll() && retirements==std::vector<bool>{false});
    assert(output.Submit(owner,pcm,[&](bool cancelled){retirements.push_back(cancelled);},1234)==A::Result::Accepted);
    device.resetFails=true;assert(!output.Reset() && retirements.size()==1);device.resetFails=false;
    assert(output.Reset() && retirements==std::vector<bool>({false,true}));assert(output.Reset() && retirements.size()==2);
    const auto nested=output.Enter();assert(!output.Client());output.Leave(nested,owner,1234);assert(output.Client()==1234);
    assert(output.ReleaseClient(owner,1234));
    assert(output.Bind(nullptr));
    // Queue destruction does not free buffers still owned by a failed-reset driver.
    {A local;assert(local.Bind(&device));auto id=local.Enter();assert(local.Submit(id,pcm)==A::Result::Accepted);device.resetFails=true;}
    assert(device.active.size()==1 && static_cast<const int16_t*>(device.active.begin()->second->Data())[0]==777);
    device.resetFails=false;assert(device.Reset());
    std::vector<int16_t> decoded;const auto wave=Wave();assert(rt::DecodeWave(wave,decoded) && decoded==std::vector<int16_t>({1000,-2000,3000,-4000}));
    auto bad=wave;bad[22]=0;assert(!rt::DecodeWave(bad,decoded)); // zero channels
    bad=wave;bad[20]=3;assert(!rt::DecodeWave(bad,decoded)); // non-PCM
    bad=wave;bad[28]^=1;assert(!rt::DecodeWave(bad,decoded)); // byte rate
    bad=wave;bad[40]=255;assert(!rt::DecodeWave(bad,decoded));
    bad=wave;bad[4]=255;bad[5]=255;bad[6]=255;bad[7]=255;assert(!rt::DecodeWave(bad,decoded));
    for(size_t n=0;n<wave.size();++n){bad.assign(wave.begin(),wave.begin()+n);assert(!rt::DecodeWave(bad,decoded));}
    assert(rt::DecodeWave(Wave(1,8,8000),decoded) && decoded.size()==96 && decoded[0]==decoded[1]);
    assert(rt::DecodeWave(Wave(2,16,24000),decoded) && decoded.size()==8 && decoded[0]==1000 && decoded[1]==-2000);
    assert(!rt::DecodeWave(Wave(2,16,1),decoded));
    (void)first;
    puts("Audio output PASS (owning aligned immutable PCM,4-slot saturation,no in-flight reuse,out-of-order/stale release,submit/poll/reset faults,retained driver ownership,nested owners,bounds,checked stereo/PCM8/16 RIFF)");
}
