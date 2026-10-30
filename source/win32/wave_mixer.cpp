#include "wave_mixer.h"
#include "wave_callbacks.h"
#include "hle_util.h"
#include "user32.h"
#include "../runtime/audio_mixer.h"
#include "../runtime/pcm_stream.h"
#include <algorithm>
#include <deque>
#include <map>
namespace win32 {namespace {
using namespace hle;
constexpr uint32_t Error=1,InvalidHandle=5,NoDriver=6,NoMemory=7,InvalidParam=11,StillPlaying=33,Unprepared=34;
struct Header{uint32_t data=0,capacity=0;uint64_t generation=0;bool queued=false;};
struct Packet{uint32_t header=0;uint64_t generation=0;std::vector<int16_t> pcm;size_t fed=0,pending=0;bool retired=false,cancelled=false;};
struct Retirement{uint32_t header=0;uint64_t generation=0;};
struct Stream{
    rt::Process* process=nullptr;cpu::IGuestMemory* memory=nullptr;uint64_t owner=0,source=0,nextGeneration=1,written=0;
    uint32_t handle=0,callback=0,callbackType=0,targetThread=0,instance=0;
    uint16_t left=65535,right=65535;bool notifications=true,fault=false,open=true;
    std::shared_ptr<rt::SyncObject> event;rt::PcmFormat format;rt::PcmStream converter;
    std::map<uint32_t,Header> headers;std::deque<std::shared_ptr<Packet>> packets;std::deque<Retirement> retired;
};
struct State{std::unique_ptr<rt::AudioMixer> mixer;std::map<uint32_t,std::shared_ptr<Stream>> streams;uint64_t owner=0,client=0;bool servicing=false;};
State& S(){return rt::LocalState<State>();}
uint64_t nextHandle=0x61000100;
bool Range(cpu::IGuestMemory& m,uint32_t p,size_t n){return p && uint64_t(p)+n<=0x100000000ull && m.IsRangeMapped(p,n);}
std::shared_ptr<Stream> Valid(uint32_t h){if(!rt::g_process)return {};auto it=S().streams.find(h);return it!=S().streams.end() && it->second->open && it->second->owner==rt::Audio().Owner()?it->second:nullptr;}
bool Notify(Stream& s,uint32_t msg,uint32_t header=0){
    if(!s.notifications || !s.callbackType)return true;
    if(s.callbackType==0x30000)return QueueWaveCallback(s.callback,s.handle,msg,s.instance,header);
    if(s.callbackType==0x50000){s.event->signaled=true;rt::SignalObject(s.event.get());return true;}
    if(s.callbackType==0x10000){auto* window=user::Win(s.callback);if(!window || window->threadId!=s.targetThread)return true;}
    const auto it=user::S().queues.find(s.targetThread);if(it==user::S().queues.end() || it->second.closed)return true;
    return it->second.posted.size()<1024 && user::PostToThread(s.targetThread,s.callbackType==0x10000?s.callback:0,msg,s.handle,header);
}
void Retire(Stream& s,Packet& p){
    if(p.retired)return;
    p.retired=true;
    if(s.retired.size()>=4){s.fault=true;return;}s.retired.push_back({p.header,p.generation});
}
void Drain(Stream& s){
    if(!s.open || s.process!=rt::g_process || s.owner!=rt::Audio().Owner())return;
    while(!s.retired.empty()){
        const auto r=s.retired.front();s.retired.pop_front();auto it=s.headers.find(r.header);
        if(it==s.headers.end() || it->second.generation!=r.generation || !it->second.queued)continue;
        it->second.queued=false;
        if(!Range(*s.memory,r.header,32)){s.fault=true;continue;}
        s.memory->Write32(r.header+16,(s.memory->Read32(r.header+16)&~16u)|3u);
        if(!Notify(s,0x3bd,r.header))s.fault=true;
    }
    while(!s.packets.empty() && s.packets.front()->retired)s.packets.pop_front();
}
void CancelQueue(Stream& s){for(auto& p:s.packets)Retire(s,*p);Drain(s);s.packets.clear();s.converter=rt::PcmStream(s.format,false);s.written=0;}
bool Owns(){return S().mixer && S().owner==rt::Audio().Owner() && S().client && S().client==rt::Audio().Client();}
bool CreateBus(){
    auto& state=S();if(state.owner && state.owner!=rt::Audio().Owner())return false;
    if(rt::Audio().Client())return false;
    if(!state.mixer)state.mixer=std::make_unique<rt::AudioMixer>(rt::Audio());
    if(!state.mixer->Begin(rt::Audio().Owner()))return false;
    state.owner=rt::Audio().Owner();state.client=rt::Audio().Client();return true;
}
bool Recover(){
    auto& state=S();if(!state.mixer || state.owner!=rt::Audio().Owner())return false;
    if(state.client && rt::Audio().Client()!=state.client)return false;
    if(!state.mixer->End())return false;
    state.client=0;
    // Successful End is the revocation gate for *all* streams on a device fault.
    for(auto& item:state.streams){CancelQueue(*item.second);item.second->source=0;item.second->fault=true;}
    if(!rt::Audio().Available() && !rt::Audio().Reset())return false;
    if(!CreateBus())return false;
    for(auto& item:state.streams){auto& stream=*item.second;stream.source=state.mixer->CreateSource();
        if(!stream.source || !state.mixer->SetGain(stream.source,stream.left,stream.right))return false;}
    return true;
}
HostCallOutcome Open(cpu::CpuState& c,cpu::IGuestMemory& m){
    const auto out=Arg(c,m,0),fmt=Arg(c,m,2),callback=Arg(c,m,3),flags=Arg(c,m,5);
    if(flags!=1 && flags!=0 && flags!=0x10000 && flags!=0x20000 && flags!=0x30000 && flags!=0x50000)return Ret(8);
    PollMixedWave();if(!MixedWaveSupported() || !rt::g_process)return Ret(NoDriver);
    if(!Range(m,fmt,16))return Ret(InvalidParam);
    rt::PcmFormat format{m.Read32(fmt+4),m.Read16(fmt+2),m.Read16(fmt+14)};
    if(m.Read16(fmt)!=1 || !format.Valid() || m.Read32(fmt+8)!=format.rate*format.Align() || m.Read16(fmt+12)!=format.Align())return Ret(32);
    if(flags==1)return Ret(0);
    if(!Range(m,out,4))return Ret(InvalidParam);
    auto stream=std::make_shared<Stream>();stream->callback=callback;stream->callbackType=flags;stream->instance=Arg(c,m,4);
    if(flags==0x30000){if(!Range(m,callback,1))return Ret(InvalidParam);}
    else if(flags==0x50000){auto object=rt::Handles().Get(callback);
        if(!object || object->type!=rt::ObjectKind::Event || !rt::Handles().HasAccess(callback,2))return Ret(InvalidParam);
        stream->event=std::static_pointer_cast<rt::SyncObject>(object);
    }else if(flags){
        if(!IsHostLayerEnabled(kUiLayer))return Ret(8);
        if(flags==0x10000){auto* w=user::Win(callback);if(!w)return Ret(InvalidParam);stream->targetThread=w->threadId;}else stream->targetThread=callback;
        const auto* thread=rt::g_process->FindThread(stream->targetThread);auto it=user::S().queues.find(stream->targetThread);
        if(!thread || thread->status==rt::GuestThreadStatus::Exited || it==user::S().queues.end() || it->second.closed)return Ret(InvalidParam);
    }
    if(S().streams.size()>=rt::AudioMixer::MaxSources || nextHandle>0x61fffffc)return Ret(NoMemory);
    if(!Owns() && !CreateBus())return Ret(4);
    if(flags==0x30000 && !PrepareWaveCallbacks()){if(S().streams.empty()){S().mixer->End();S().client=0;}return Ret(Error);}
    stream->source=S().mixer->CreateSource();if(!stream->source){if(S().streams.empty()){S().mixer->End();S().client=0;}return Ret(Error);}
    stream->handle=uint32_t(nextHandle);nextHandle+=4;stream->process=rt::g_process;stream->memory=&m;stream->owner=rt::Audio().Owner();stream->format=format;stream->converter=rt::PcmStream(format,false);
    if(!Notify(*stream,0x3bb)){S().mixer->CloseSource(stream->source);if(S().streams.empty()){S().mixer->End();S().client=0;}return Ret(NoMemory);}
    S().streams.emplace(stream->handle,stream);m.Write32(out,stream->handle);return Ret(0);
}
HostCallOutcome Prepare(Stream& s,cpu::CpuState& c,cpu::IGuestMemory& m,bool remove){
    const auto p=Arg(c,m,1);if(Arg(c,m,2)<32 || !Range(m,p,32))return Ret(InvalidParam);
    auto it=s.headers.find(p);if(it!=s.headers.end()){
        if(it->second.queued)return Ret(StillPlaying);
        if(remove){m.Write32(p+16,m.Read32(p+16)&~18u);s.headers.erase(it);}return Ret(0);
    }
    if(remove)return Ret(0);
    const auto data=m.Read32(p),bytes=m.Read32(p+4),flags=m.Read32(p+16);
    if(flags&12)return Ret(8);
    if(!bytes || bytes%s.format.Align() || bytes>uint64_t(s.format.rate)*s.format.Align()*5 || !Range(m,data,bytes))return Ret(InvalidParam);
    if(s.headers.size()>=64 || s.nextGeneration==UINT64_MAX)return Ret(NoMemory);
    s.headers.emplace(p,Header{data,bytes,s.nextGeneration++,false});m.Write32(p+16,(flags&~17u)|2u);return Ret(0);
}
HostCallOutcome Write(const std::shared_ptr<Stream>& s,cpu::CpuState& c,cpu::IGuestMemory& m){
    if(s->fault || !Owns() || S().mixer->Faulted() || (s->callbackType==0x30000 && !WaveCallbacksHealthy()))return Ret(Error);
    const auto p=Arg(c,m,1);if(Arg(c,m,2)<32 || !Range(m,p,32))return Ret(InvalidParam);
    auto it=s->headers.find(p);if(it==s->headers.end())return Ret(Unprepared);if(it->second.queued)return Ret(StillPlaying);
    const auto data=m.Read32(p),bytes=m.Read32(p+4),flags=m.Read32(p+16);if(flags&12)return Ret(8);
    if(data!=it->second.data || !bytes || bytes>it->second.capacity || bytes%s->format.Align() || !Range(m,data,bytes))return Ret(InvalidParam);
    if(s->packets.size()>=4)return Ret(NoMemory);
    std::vector<uint8_t> raw(bytes);m.ReadBytes(data,raw.data(),bytes);auto converter=s->converter;
    auto packet=std::make_shared<Packet>();if(!converter.Bytes(raw,packet->pcm) || packet->pcm.empty() || packet->pcm.size()>rt::AudioOutput::MaxSamples)return Ret(InvalidParam);
    packet->header=p;packet->generation=it->second.generation;s->packets.push_back(packet);s->converter=converter;s->written+=bytes/s->format.Align();
    it->second.queued=true;m.Write32(p+16,(flags&~1u)|18u);return Ret(0); // accepted owning software queue, not a played notification
}
HostCallOutcome Reset(Stream& s,bool close){
    if(close)for(const auto& item:s.headers)if(item.second.queued)return Ret(StillPlaying);
    if(!Owns() || !rt::Audio().Available() || S().mixer->Faulted() || !s.source){if(!Recover())return Ret(Error);}
    if(!S().mixer->ResetSource(s.source))return Ret(Error);
    CancelQueue(s);
    if(!close){for(const auto& h:s.headers)if(!Range(*s.memory,h.first,32))return Ret(Error);s.fault=false;return Ret(0);}
    if(!Notify(s,0x3bc))return Ret(NoMemory);
    if(!S().mixer->CloseSource(s.source))return Ret(Error);
    for(const auto& h:s.headers)if(Range(*s.memory,h.first,32))s.memory->Write32(h.first+16,s.memory->Read32(h.first+16)&~18u);
    s.open=false;S().streams.erase(s.handle);
    if(S().streams.empty()){if(!S().mixer->End())return Ret(Error);S().client=0;}
    return Ret(0);
}
}
bool MixedWaveSupported(){uint64_t frames=0;return rt::g_process && rt::Audio().Available() && rt::Audio().Position(rt::Audio().Owner(),rt::Audio().Client(),frames);}
bool MixedWaveHandleLive(uint32_t h){return bool(Valid(h));}
bool IsMixedWaveHandle(uint32_t h){return h>=0x61000000 && h<0x62000000;}
void PollMixedWave(){
    if(!rt::g_process)return;
    auto& state=S();if(state.servicing || !Owns())return;
    state.servicing=true;
    if(!state.mixer->Faulted()){
        for(auto& item:state.streams){auto s=item.second;if(s->fault)continue;
            for(auto& packet:s->packets){
                while(!packet->retired && packet->fed<packet->pcm.size()/2 && state.mixer->Pending(s->source)<rt::AudioMixer::MaxPackets){
                    const auto frames=std::min<size_t>(4096,packet->pcm.size()/2-packet->fed);
                    std::vector<int16_t> chunk(packet->pcm.begin()+packet->fed*2,packet->pcm.begin()+(packet->fed+frames)*2);
                    std::weak_ptr<Stream> weak=s;std::weak_ptr<Packet> wp=packet;
                    const auto result=state.mixer->Write(s->source,chunk,[weak,wp](bool cancel){if(auto p=wp.lock()){
                        if(p->pending)--p->pending;
                        p->cancelled|=cancel;
                        if(p->fed==p->pcm.size()/2 && !p->pending)if(auto stream=weak.lock())Retire(*stream,*p);
                    }});
                    if(result==rt::AudioOutput::Result::Busy)break;
                    if(result!=rt::AudioOutput::Result::Accepted){s->fault=true;break;}
                    packet->fed+=frames;++packet->pending;
                }
                if(s->fault || state.mixer->Pending(s->source)>=rt::AudioMixer::MaxPackets)break;
            }
        }
        if(!state.mixer->Pump())for(auto& item:state.streams)item.second->fault=true;
    }
    for(auto& item:state.streams)Drain(*item.second);
    state.servicing=false;
}
HostCallOutcome MixedWaveCall(MixedWaveOp op,cpu::CpuState& c,cpu::IGuestMemory& m){
    if(op==MixedWaveOp::Open)return Open(c,m);
    PollMixedWave();auto s=Valid(Arg(c,m,0));if(!s)return Ret(InvalidHandle);
    if(op==MixedWaveOp::Prepare || op==MixedWaveOp::Unprepare)return Prepare(*s,c,m,op==MixedWaveOp::Unprepare);
    if(op==MixedWaveOp::Write)return Write(s,c,m);
    if(op==MixedWaveOp::Reset || op==MixedWaveOp::Close)return Reset(*s,op==MixedWaveOp::Close);
    if(!Owns() || s->fault || S().mixer->Faulted())return Ret(Error);
    if(op==MixedWaveOp::Pause || op==MixedWaveOp::Restart)return Ret(S().mixer->SetPaused(s->source,op==MixedWaveOp::Pause)?0:Error);
    const auto p=Arg(c,m,1);
    if(op==MixedWaveOp::SetVolume){if(!S().mixer->SetGain(s->source,uint16_t(p),uint16_t(p>>16)))return Ret(Error);s->left=uint16_t(p);s->right=uint16_t(p>>16);return Ret(0);}
    if(op==MixedWaveOp::GetVolume){if(!Range(m,p,4))return Ret(InvalidParam);m.Write32(p,uint32_t(s->left)|uint32_t(s->right)<<16);return Ret(0);}
    if(op==MixedWaveOp::Position){
        if(Arg(c,m,2)<12 || !Range(m,p,12))return Ret(InvalidParam);
        uint64_t frames=0;
        if(!S().mixer->Position(s->source,frames) || frames>UINT64_MAX/s->format.rate)return Ret(Error);
        frames=std::min(frames*s->format.rate/48000,s->written);auto type=m.Read32(p);uint64_t value=frames;
        if(type==1)value=frames*1000/s->format.rate;else if(type!=2){type=4;value=frames*s->format.Align();}
        m.Write32(p,type);m.Write32(p+4,uint32_t(value));m.Write32(p+8,0);return Ret(0);
    }
    return Ret(8);
}
void EndMixedWave(){
    if(!rt::g_process)return;
    auto& state=S();if(state.streams.empty())return;
    for(auto& item:state.streams)item.second->notifications=false;
    if(Owns() && state.mixer->End())for(auto& item:state.streams)CancelQueue(*item.second);
    for(auto& item:state.streams)item.second->open=false;
    state.streams.clear();state.client=0;
}
}
