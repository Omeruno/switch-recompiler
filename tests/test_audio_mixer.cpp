#include "source/runtime/audio_mixer.h"
#include "tests/audio_capture_device.h"
#include <cassert>
#include <cstdio>
#include <limits>
using Mixer=rt::AudioMixer;using Result=rt::AudioOutput::Result;
static std::vector<int16_t> Signal(size_t frames,int left,int right){std::vector<int16_t> v(frames*2);for(size_t n=0;n<frames;++n){v[2*n]=int16_t(left+int(n%17));v[2*n+1]=int16_t(right-int(n%13));}return v;}
static void Check(const std::vector<int16_t>& got,const std::vector<int16_t>& a,size_t startA,const std::vector<int16_t>& b,size_t startB,size_t frames,uint16_t gainL=65535,uint16_t gainR=65535){
    assert(got.size()==frames*2);for(size_t n=0;n<frames;++n)for(unsigned c=0;c<2;++c){
        int64_t expected=0;if((startA+n)*2+c<a.size())expected+=int64_t(a[(startA+n)*2+c])*(c?gainR:gainL)/65535;
        if((startB+n)*2+c<b.size())expected+=b[(startB+n)*2+c];
        expected=std::max<int64_t>(-32768,std::min<int64_t>(32767,expected));assert(got[n*2+c]==expected);
    }
}
static void Release(AudioCaptureDevice& device,rt::AudioOutput& output,uint64_t frames){
    device.playedFrames+=frames;uint64_t remaining=frames;
    for(const auto& item:device.active){const auto count=item.second->Bytes()/4;if(remaining<count)break;device.released.push_back(item.first);remaining-=count;}
    assert(!remaining);assert(output.Poll());
}
int main(){
    AudioCaptureDevice device;rt::AudioOutput output;assert(output.Bind(&device));auto owner=output.Enter();Mixer mixer(output);
    assert(mixer.Begin(owner));const auto a=mixer.CreateSource(),b=mixer.CreateSource();assert(a && b && a!=b);
    auto pa=Signal(1700,20000,-20000),pb=Signal(1100,18000,-18000);const auto originalA=pa;
    unsigned playedA=0,playedB=0,cancelA=0,cancelB=0;
    auto doneA=[&](bool cancel){cancel?++cancelA:++playedA;assert(!mixer.Pump() && !mixer.End() && !mixer.CreateSource());};
    auto doneB=[&](bool cancel){cancel?++cancelB:++playedB;};
    assert(mixer.Write(a,pa,doneA)==Result::Accepted && mixer.Write(b,pb,doneB)==Result::Accepted);
    pa.assign(pa.size(),0);assert(mixer.Pump() && device.active.size()==4);
    uint64_t position=999;assert(mixer.Position(a,position) && !position && !playedA && !playedB);
    for(size_t n=0;n<4;++n)Check(device.submitted[n],originalA,n*256,pb,n*256,256);
    // 100 actually played frames, not whole queued packets. Freeze/revoke/replay
    // preserves each source's exact unplayed offset while pausing A alone.
    device.playedFrames=100;assert(mixer.SetPaused(a,true) && device.active.empty());
    assert(mixer.Position(a,position) && position==100 && mixer.Position(b,position) && position==100);
    assert(!playedA && !playedB && !cancelA && !cancelB);const auto next=device.submitted.size();assert(mixer.Pump());
    const std::vector<int16_t> silence;Check(device.submitted[next],silence,0,pb,100,256);
    Release(device,output,256);assert(mixer.Pump());assert(mixer.Position(a,position) && position==100);
    assert(mixer.Position(b,position) && position==356);
    assert(mixer.SetPaused(a,false));const auto restart=device.submitted.size();assert(mixer.Pump());
    Check(device.submitted[restart],originalA,100,pb,356,256);
    // Changing A's independent stereo gain rebuilds immutable submitted blocks.
    device.playedFrames+=17;assert(mixer.SetGain(a,32768,0));uint16_t l=0,r=1;assert(mixer.Gain(a,l,r) && l==32768 && !r);
    const auto gain=device.submitted.size();assert(mixer.Pump());Check(device.submitted[gain],originalA,117,pb,373,256,32768,0);
    assert(mixer.CloseSource(a) && cancelA==1 && !playedA && !cancelB);
    assert(!mixer.Position(a,position) && mixer.Write(a,originalA)==Result::Invalid);
    // Complete B through actual hardware counters/releases. No early DONE at mix.
    assert(mixer.Pump());while(mixer.Pending(b)){
        assert(!device.active.empty());const auto count=device.active.begin()->second->Bytes()/4;
        Release(device,output,count);assert(mixer.Pump());
    }
    assert(playedB==1 && !cancelB && mixer.Position(b,position) && position==1100);
    assert(mixer.End() && !output.Client() && !device.active.size());

    // Sources with different packet partitions still produce the same samples.
    assert(mixer.Begin(owner));const auto x=mixer.CreateSource(),y=mixer.CreateSource();assert(x!=a && y!=b);
    auto s=Signal(512,-1000,2000),q=Signal(512,3000,-4000);unsigned done=0;
    assert(mixer.Write(x,{s.begin(),s.begin()+46},[&](bool c){assert(!c);++done;})==Result::Accepted);
    assert(mixer.Write(x,{s.begin()+46,s.end()},[&](bool c){assert(!c);++done;})==Result::Accepted);
    assert(mixer.Write(y,q)==Result::Accepted);const auto split=device.submitted.size();assert(mixer.Pump());
    Check(device.submitted[split],s,0,q,0,256);Check(device.submitted[split+1],s,256,q,256,256);
    assert(!done);device.playedFrames+=23;assert(mixer.Pump() && done==1);
    assert(mixer.Position(x,position) && position==23);
    // All-source pause and restart do not consume their sample timeline.
    assert(mixer.SetPaused(x,true) && mixer.SetPaused(y,true));const auto pausedCount=device.submitted.size();assert(mixer.Pump() && device.active.empty() && device.submitted.size()==pausedCount);
    assert(mixer.SetPaused(x,false));assert(mixer.Pump());
    // Finish with confirmed cancel (the second packet's callback permits it here).
    // Already played observer stays retired; release the rest as actually played.
    while(mixer.Pending(x)){auto count=device.active.begin()->second->Bytes()/4;Release(device,output,count);assert(mixer.Pump());}
    assert(done==2);assert(mixer.End());

    // Fixed native storage bounds: sources, packets, actual retained PCM frames.
    assert(mixer.Begin(owner));std::vector<uint64_t> ids;
    for(size_t n=0;n<Mixer::MaxSources;++n){auto id=mixer.CreateSource();assert(id);ids.push_back(id);}assert(!mixer.CreateSource());
    for(size_t n=0;n<Mixer::MaxPackets;++n)assert(mixer.Write(ids[0],Signal(1,1,-1))==Result::Accepted);
    assert(mixer.Write(ids[0],Signal(1,1,-1))==Result::Busy);
    assert(mixer.Write(ids[1],std::vector<int16_t>(Mixer::MaxQueuedFrames*2))==Result::Accepted);
    assert(mixer.Write(ids[1],Signal(1,1,1))==Result::Busy);
    assert(mixer.Write(ids[2],{})==Result::Invalid && mixer.Write(ids[2],{1})==Result::Invalid);
    assert(mixer.Write(ids[2],std::vector<int16_t>((Mixer::MaxQueuedFrames+1)*2))==Result::Invalid);
    assert(mixer.ResetSource(ids[0]) && !mixer.Pending(ids[0]));assert(mixer.End());

    // A release is not proof of samples played. A stalled counter cannot cause
    // early observer calls or unbounded submission/journal growth.
    assert(mixer.Begin(owner));auto id=mixer.CreateSource();unsigned retired=0;
    assert(mixer.Write(id,Signal(1024,50,-50),[&](bool c){assert(c);++retired;})==Result::Accepted && mixer.Pump());
    for(const auto& item:device.active)device.released.push_back(item.first);
    assert(mixer.Pump() && !retired && !device.active.size());const auto held=device.submitted.size();assert(mixer.Pump() && device.submitted.size()==held);
    assert(mixer.End() && retired==1);

    // Actual fault ownership: failed reset/ambiguous append retains native
    // buffers and callbacks until successful revocation, never reports played.
    for(unsigned failure=0;failure<4;++failure){
        assert(mixer.Begin(owner));id=mixer.CreateSource();retired=0;
        assert(mixer.Write(id,Signal(1200,10,-10),[&](bool c){assert(c);++retired;})==Result::Accepted);
        if(failure==0){device.submitFails=true;assert(!mixer.Pump());device.submitFails=false;}
        else{assert(mixer.Pump());if(failure==1)device.pollFails=true;else if(failure==2)device.controlFails=true;else device.resetFails=true;
            assert(failure==1?!mixer.Pump():!mixer.SetPaused(id,true));}
        assert(mixer.Faulted() && !retired && !mixer.Pump());device.resetFails=true;assert(!mixer.End() && !retired && !device.active.empty());
        device.resetFails=device.pollFails=device.controlFails=false;assert(mixer.End() && retired==1 && device.active.empty());
    }
    // Reset A alone after a partial composite preserves B's exact next sample.
    assert(mixer.Begin(owner));auto first=mixer.CreateSource(),second=mixer.CreateSource();unsigned resetDone=0;
    auto va=Signal(800,500,-500),vb=Signal(800,1000,-1000);
    assert(mixer.Write(first,va,[&](bool c){assert(c);++resetDone;})==Result::Accepted && mixer.Write(second,vb)==Result::Accepted && mixer.Pump());
    device.playedFrames+=31;assert(mixer.ResetSource(first) && resetDone==1);
    assert(mixer.Position(first,position) && !position && mixer.Position(second,position) && position==31);
    const auto resetAt=device.submitted.size();assert(mixer.Pump());Check(device.submitted[resetAt],silence,0,vb,31,256);
    assert(mixer.End());
    // Eight simultaneously active sources: wide accumulation before final clamp.
    assert(mixer.Begin(owner));
    for(unsigned n=0;n<8;++n){id=mixer.CreateSource();auto pcm=Signal(65,n%2?7000:6000,n%2?-7000:-6000);
        assert(mixer.Write(id,pcm)==Result::Accepted);}
    const auto eight=device.submitted.size();assert(mixer.Pump() && device.submitted[eight].size()==130);
    for(size_t n=0;n<65;++n){assert(device.submitted[eight][n*2]==32767 && device.submitted[eight][n*2+1]==-32768);}
    assert(mixer.End());
    // A competing exclusive client is refused without stealing its lease.
    assert(output.Claim(owner,77));assert(!mixer.Begin(owner) && output.Client()==77);assert(output.ReleaseClient(owner,77));
    device.controlFails=true;assert(!mixer.Begin(owner) && !output.Client());device.controlFails=false;
    // Nested owner cannot mutate or cancel the suspended outer source's state.
    assert(mixer.Begin(owner));id=mixer.CreateSource();retired=0;assert(mixer.Write(id,Signal(10,1,-1),[&](bool c){assert(c);++retired;})==Result::Accepted && mixer.Pump());
    const auto oldClient=output.Client(),inner=output.Enter();assert(!mixer.Pump() && !mixer.End() && !retired);
    assert(output.Claim(inner,88));assert(mixer.Write(id,Signal(1,0,0))==Result::WrongOwner && output.Client()==88);
    output.Leave(inner,owner,oldClient);assert(!mixer.Pump() && mixer.Faulted() && !retired);assert(mixer.End() && retired==1);
    // Counter regression/overrun is a device fault, not invented source progress.
    assert(mixer.Begin(owner));id=mixer.CreateSource();assert(mixer.Write(id,Signal(10,1,-1))==Result::Accepted && mixer.Pump());
    device.playedFrames+=11;assert(!mixer.Pump() && mixer.Faulted());assert(mixer.End());
    assert(output.Bind(nullptr));
    puts("Audio mixer PASS (8 sources,real stereo sum/clipping/gain,immutable PCM,actual partial-counter credits,independent pause/replay/reset/close,packet partitions/EOF,bounds/reentrancy/stale IDs,release-before-counter and retained fault recovery;native core,not WinMM mixing or hardware sound acceptance)");
}
