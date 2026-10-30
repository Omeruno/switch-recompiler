#include "source/runtime/audio_audout.h"
#include "source/runtime/audio_mixer.h"
#include <cassert>
#include <deque>
#include <set>
#include <cstdio>
namespace {
struct Transport {
    bool initFail=false,startFail=false,stopFail=false,flushFail=false,containsFail=false,appendFail=false,pollFail=false;
    bool initialized=false,running=false,positionFail=false,volumeFail=false;unsigned appends=0,exits=0;
    uint64_t played=0;float volume=1;
    std::set<AudioOutBuffer*> queued;std::deque<AudioOutBuffer*> released;
    std::weak_ptr<const rt::AudioBlock> mustOwnAtExit;
    bool checkExitOwner=false;
} t;
}
Result audoutInitialize(){if(t.initFail)return 1;t.initialized=true;return 0;}
void audoutExit(){if(t.checkExitOwner)assert(!t.mustOwnAtExit.expired());++t.exits;t.initialized=false;t.queued.clear();t.released.clear();}
u32 audoutGetSampleRate(){return 48000;}u32 audoutGetChannelCount(){return 2;}PcmFormat audoutGetPcmFormat(){return PcmFormat_Int16;}
Result audoutStartAudioOut(){if(t.startFail)return 1;t.running=true;return 0;}Result audoutStopAudioOut(){if(t.stopFail)return 1;t.running=false;return 0;}
Result audoutGetAudioOutPlayedSampleCount(u64* n){if(t.positionFail)return 1;*n=t.played;return 0;}
Result audoutSetAudioOutVolume(float v){if(t.volumeFail)return 1;t.volume=v;return 0;}
Result audoutGetAudioOutVolume(float* v){if(t.volumeFail)return 1;*v=t.volume;return 0;}
Result audoutAppendAudioOutBuffer(AudioOutBuffer* b){assert(b && b->buffer && !(reinterpret_cast<uintptr_t>(b->buffer)&4095) && !(b->buffer_size&4095));t.queued.insert(b);++t.appends;return t.appendFail?1:0;}
Result audoutGetReleasedAudioOutBuffer(AudioOutBuffer** p,u32* n){if(t.pollFail)return 1;*n=0;*p=nullptr;if(!t.released.empty()){*p=t.released.front();t.released.pop_front();*n=1;}return 0;}
Result audoutContainsAudioOutBuffer(AudioOutBuffer* p,bool* contains){if(t.containsFail)return 1;*contains=t.queued.count(p)!=0;return 0;}
Result audoutFlushAudioOutBuffers(bool* flushed){if(t.flushFail)return 1;*flushed=!t.queued.empty();for(auto* p:t.queued)t.released.push_back(p);t.queued.clear();return 0;}
int main(){
    rt::AudoutDevice device;t.initFail=true;assert(!device.Initialize() && !device.Available());t.initFail=false;
    t.startFail=true;assert(!device.Initialize() && !t.initialized);t.startFail=false;assert(device.Initialize() && device.Available());
    rt::AudioOutput output;assert(output.Bind(&device));const auto owner=output.Enter();std::vector<int16_t> pcm={123,-456};
    for(unsigned n=0;n<4;++n)assert(output.Submit(owner,pcm)==rt::AudioOutput::Result::Accepted);
    uint64_t position=0;t.played=123;assert(device.Position(position) && position==123);
    assert(device.SetPaused(true) && !t.running && t.queued.size()==4);
    assert(device.Position(position) && position==123);assert(device.SetPaused(false) && t.running && t.queued.size()==4);
    // Counter restart normalization preserves confirmed samples, no timer estimate.
    t.played=0;assert(device.Position(position) && position==123);t.played=17;assert(device.Position(position) && position==140);
    uint16_t volume=0;assert(device.SetVolume(16384) && device.Volume(volume) && volume==16384);
    t.volumeFail=true;assert(!device.SetVolume(65535) && !device.Volume(volume));t.volumeFail=false;
    t.positionFail=true;assert(!device.SetPaused(true) && t.running);t.positionFail=false;
    assert(t.queued.size()==4 && t.appends==4);assert(output.Submit(owner,pcm)==rt::AudioOutput::Result::Busy && t.appends==4);
    assert(!device.Submit(5000,std::make_shared<const rt::AudioBlock>(pcm)) && t.appends==4);
    for(auto* p:t.queued){const auto* samples=static_cast<const int16_t*>(p->buffer);assert(samples[0]==123 && samples[1]==-456);}
    auto it=t.queued.begin();++it;auto* released=*it;t.queued.erase(it);t.released.push_back(released);assert(output.Poll() && output.Pending()==3);
    assert(output.Submit(owner,pcm)==rt::AudioOutput::Result::Accepted && t.queued.size()==4);
    t.stopFail=true;assert(!output.Reset() && output.Pending()==4 && t.queued.size()==4);
    t.stopFail=false;t.flushFail=true;assert(!output.Reset() && output.Pending()==4);
    t.flushFail=false;t.containsFail=true;assert(!output.Reset() && output.Pending()==4);
    t.containsFail=false;t.startFail=true;assert(output.Reset() && !output.Pending() && !device.Available());
    t.startFail=false;assert(output.Reset() && device.Available());
    t.appendFail=true;assert(output.Submit(owner,pcm)==rt::AudioOutput::Result::DeviceError && t.queued.size()==1 && !device.Available());
    const auto* ambiguous=static_cast<const int16_t*>((*t.queued.begin())->buffer);assert(ambiguous[0]==123 && ambiguous[1]==-456);
    t.appendFail=false;assert(output.Reset() && t.queued.empty() && device.Available());
    assert(output.Submit(owner,pcm)==rt::AudioOutput::Result::Accepted);t.pollFail=true;assert(!output.Poll() && output.Pending()==1);t.pollFail=false;assert(output.Reset());
    // Native mixer through the actual production audout adapter and transport.
    // This observes real submitted descriptor bytes, not an alternate mixer mock.
    {
        rt::AudioMixer mixer(output);assert(mixer.Begin(owner));const auto a=mixer.CreateSource(),b=mixer.CreateSource();
        std::vector<int16_t> one(1024),two(1024);for(unsigned n=0;n<512;++n){one[n*2]=1000+int(n);one[n*2+1]=-1000-int(n);two[n*2]=2000;two[n*2+1]=-2000;}
        unsigned cancelled=0,played=0;
        assert(mixer.Write(a,one,[&](bool c){assert(c);++cancelled;})==rt::AudioOutput::Result::Accepted);
        assert(mixer.Write(b,two,[&](bool c){assert(!c);++played;})==rt::AudioOutput::Result::Accepted);
        assert(mixer.Pump() && t.queued.size()==2 && !played && !cancelled);
        bool first=false,second=false;for(auto* p:t.queued){auto* data=static_cast<const int16_t*>(p->buffer);assert(p->data_size==256*4);
            first|=data[0]==3000 && data[1]==-3000;second|=data[0]==3256 && data[1]==-3256;}
        assert(first && second);
        t.played+=37;assert(mixer.SetPaused(a,true) && t.queued.empty() && t.running);
        uint64_t frames=0;assert(mixer.Position(a,frames) && frames==37 && mixer.Position(b,frames) && frames==37);
        assert(mixer.Pump() && t.queued.size()==2);for(auto* p:t.queued){const auto* data=static_cast<const int16_t*>(p->buffer);assert(data[0]==2000 && data[1]==-2000);}
        assert(mixer.CloseSource(a) && cancelled==1 && !played);assert(mixer.Pump());
        for(auto* p:t.queued){t.released.push_back(p);}t.queued.clear();t.played+=475;
        assert(mixer.Pump() && played==1 && mixer.Position(b,frames) && frames==512 && mixer.End());
        puts("Audout mixer PASS (production descriptor stereo sums,partial native counter,source pause/rebuild/close,independent peer completion;host transport model,not physical sound)");
    }
    assert(output.Bind(nullptr));
    // No queue/reference can conceal an early free before native session close.
    auto block=std::make_shared<const rt::AudioBlock>(pcm);t.mustOwnAtExit=block;t.checkExitOwner=true;
    assert(device.Submit(999,block));block.reset();assert(!t.mustOwnAtExit.expired());device.Shutdown();assert(t.mustOwnAtExit.expired() && !t.initialized);
    t.checkExitOwner=false;
    puts("Audout adapter PASS (production adapter/host transport model: init/start/append/poll/stop/flush/contains/restart faults,4 native slots,actual aligned PCM retention,drained reset,pause retains buffers/native position/counter epochs/volume failures,close-before-free)");
}
