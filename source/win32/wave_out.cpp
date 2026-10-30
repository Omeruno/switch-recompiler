#include "wave_out.h"
#include "wave_mixer.h"
#include "wave_callbacks.h"
#include "hle_util.h"
#include "user32.h"
#include "../runtime/audio_output.h"
#include "../runtime/pcm_stream.h"
#include <algorithm>
#include <deque>
#include <map>
namespace win32 {namespace {
using namespace hle;
constexpr uint32_t InvalidHandle=5,InvalidParam=11,NoMemory=7,Error=1,StillPlaying=33,Unprepared=34;
constexpr uint32_t Done=1,Prepared=2,InQueue=16;
constexpr uint32_t OpenMessage=0x3bb,CloseMessage=0x3bc,DoneMessage=0x3bd;
struct Header {uint32_t data=0,capacity=0;uint64_t generation=0;bool queued=false;};
struct Retirement {uint32_t header;uint64_t generation;bool cancelled;};
struct Stream {
    rt::Process* process=nullptr;cpu::IGuestMemory* memory=nullptr;
    uint64_t owner=0,nextGeneration=1;
    uint32_t handle=0,callback=0,callbackType=0,targetThread=0,instance=0;
    std::shared_ptr<rt::SyncObject> event;
    rt::PcmFormat format;rt::PcmStream converter;
    uint64_t positionBase=0,writtenFrames=0;bool positionValid=false;
    bool open=true,notifications=true,fault=false;
    std::map<uint32_t,Header> headers;
    std::deque<Retirement> retired;
};
struct Holder {std::shared_ptr<Stream> stream;};
uint64_t nextHandle=0x60000100;
bool Range(cpu::IGuestMemory& m,uint32_t p,size_t n){return p && uint64_t(p)+n<=0x100000000ull && m.IsRangeMapped(p,n);}
std::shared_ptr<Stream> Current(){return rt::g_process?rt::LocalState<Holder>().stream:nullptr;}
bool Notify(Stream& s,uint32_t message,uint32_t header=0) {
    if(!s.notifications || !s.callbackType)return true;
    if(s.callbackType==0x30000)
        return QueueWaveCallback(s.callback,s.handle,message,s.instance,header);
    if(s.callbackType==0x50000){s.event->signaled=true;rt::SignalObject(s.event.get());return true;}
    if(s.callbackType==0x10000){const auto* w=user::Win(s.callback);if(!w || w->threadId!=s.targetThread)return true;}
    auto it=user::S().queues.find(s.targetThread);
    if(it==user::S().queues.end() || it->second.closed)return true;
    if(it->second.posted.size()>=1024)return false;
    return user::PostToThread(s.targetThread,s.callbackType==0x10000?s.callback:0,message,s.handle,header);
}
void Drain(Stream& s) {
    if(!s.open || rt::g_process!=s.process || rt::Audio().Owner()!=s.owner)return;
    while(!s.retired.empty()) {
        const auto event=s.retired.front();s.retired.pop_front();auto it=s.headers.find(event.header);
        if(it==s.headers.end() || it->second.generation!=event.generation || !it->second.queued)continue;
        it->second.queued=false;
        if(!Range(*s.memory,event.header,32)){s.fault=true;continue;}
        const auto flags=s.memory->Read32(event.header+16);
        s.memory->Write32(event.header+16,(flags&~InQueue)|Prepared|Done);
        if(!Notify(s,DoneMessage,event.header))s.fault=true;
    }
}
std::shared_ptr<Stream> Valid(uint32_t handle){auto s=Current();return s && s->open && s->handle==handle && s->owner==rt::Audio().Owner() && rt::Audio().Client()==handle?s:nullptr;}
bool Format(cpu::IGuestMemory& m,uint32_t p,rt::PcmFormat& f){f={m.Read32(p+4),m.Read16(p+2),m.Read16(p+14)};return m.Read16(p)==1 && f.Valid() && m.Read32(p+8)==f.rate*f.Align() && m.Read16(p+12)==f.Align();}
HostCallOutcome Open(cpu::CpuState& c,cpu::IGuestMemory& m) {
    const auto output=Arg(c,m,0),device=Arg(c,m,1),format=Arg(c,m,2),callback=Arg(c,m,3),flags=Arg(c,m,5);
    if(flags!=1 && flags!=0 && flags!=0x10000 && flags!=0x20000 && flags!=0x30000 && flags!=0x50000)return Ret(8);
    if(device && device!=0xffffffffu)return Ret(2);
    PollWaveOut();if(!rt::Audio().Available() || !rt::g_process)return Ret(6);
    if(!Range(m,format,16))return Ret(InvalidParam);
    rt::PcmFormat pcmFormat;if(!Format(m,format,pcmFormat))return Ret(32);
    if(flags==1)return Ret(0);
    if(!Range(m,output,4))return Ret(InvalidParam);
    uint32_t thread=0;
    std::shared_ptr<rt::SyncObject> event;
    if(flags==0x30000) {
        if(!Range(m,callback,1))return Ret(InvalidParam);
    }else if(flags==0x50000) {
        auto object=rt::Handles().Get(callback);
        if(!object || object->type!=rt::ObjectKind::Event || !rt::Handles().HasAccess(callback,2))return Ret(InvalidParam);
        event=std::static_pointer_cast<rt::SyncObject>(object);
    }else if(flags) {
        if(!IsHostLayerEnabled(kUiLayer))return Ret(8);
        if(flags==0x10000){auto* w=user::Win(callback);if(!w)return Ret(InvalidParam);thread=w->threadId;}
        else thread=callback;
        const auto* target=rt::g_process->FindThread(thread);auto queue=user::S().queues.find(thread);
        if(!target || target->status==rt::GuestThreadStatus::Exited || queue==user::S().queues.end() || queue->second.closed)return Ret(InvalidParam);
    }
    if(rt::Audio().Client() || rt::Audio().Pending())return Ret(4);
    if(nextHandle>0x60fffffc)return Ret(NoMemory);
    if(flags==0x30000 && !PrepareWaveCallbacks())return Ret(Error);
    const auto handle=uint32_t(nextHandle);nextHandle+=4;
    auto s=std::make_shared<Stream>();s->process=rt::g_process;s->memory=&m;s->owner=rt::Audio().Owner();s->handle=handle;s->callback=callback;s->callbackType=flags;s->targetThread=thread;s->instance=Arg(c,m,4);s->event=std::move(event);
    if(!rt::Audio().Claim(s->owner,handle))return Ret(4);
    if(!Notify(*s,OpenMessage)){rt::Audio().ReleaseClient(s->owner,handle);return Ret(NoMemory);}
    s->format=pcmFormat;s->converter=rt::PcmStream(pcmFormat,false);
    s->positionValid=rt::Audio().Position(s->owner,handle,s->positionBase);
    rt::LocalState<Holder>().stream=s;m.Write32(output,handle);return Ret(0);
}
HostCallOutcome Prepare(cpu::CpuState& c,cpu::IGuestMemory& m,bool remove) {
    PollWaveOut();auto s=Valid(Arg(c,m,0));if(!s)return Ret(InvalidHandle);
    const auto p=Arg(c,m,1),size=Arg(c,m,2);if(size<32 || !Range(m,p,32))return Ret(InvalidParam);
    auto old=s->headers.find(p);
    if(old!=s->headers.end()) {
        if(old->second.queued)return Ret(StillPlaying);
        if(remove){m.Write32(p+16,m.Read32(p+16)&~(Prepared|InQueue));s->headers.erase(old);}return Ret(0);
    }
    if(remove)return Ret(0);
    const auto data=m.Read32(p),bytes=m.Read32(p+4),flags=m.Read32(p+16);
    if(flags&12)return Ret(8);
    if(!bytes || bytes%s->format.Align() || bytes>uint64_t(s->format.rate)*s->format.Align()*5 || !Range(m,data,bytes))return Ret(InvalidParam);
    if(s->headers.size()>=64 || s->nextGeneration==UINT64_MAX)return Ret(NoMemory);
    s->headers.emplace(p,Header{data,bytes,s->nextGeneration++,false});m.Write32(p+16,(flags&~(Done|InQueue))|Prepared);return Ret(0);
}
HostCallOutcome Write(cpu::CpuState& c,cpu::IGuestMemory& m) {
    PollWaveOut();auto s=Valid(Arg(c,m,0));if(!s)return Ret(InvalidHandle);
    if(s->fault || (s->callbackType==0x30000 && !WaveCallbacksHealthy()))return Ret(Error);
    const auto p=Arg(c,m,1);if(Arg(c,m,2)<32 || !Range(m,p,32))return Ret(InvalidParam);
    auto it=s->headers.find(p);if(it==s->headers.end())return Ret(Unprepared);
    if(it->second.queued)return Ret(StillPlaying);
    const auto data=m.Read32(p),bytes=m.Read32(p+4),flags=m.Read32(p+16);
    if(flags&12)return Ret(8);
    if(data!=it->second.data || !bytes || bytes>it->second.capacity || bytes%s->format.Align() || !Range(m,data,bytes))return Ret(InvalidParam);
    if(!rt::Audio().Available())return Ret(6);
    std::vector<uint8_t> raw(bytes);for(size_t n=0;n<bytes;++n)raw[n]=m.Read8(data+uint32_t(n));
    auto converter=s->converter;std::vector<int16_t> pcm;
    if(!converter.Bytes(raw,pcm) || pcm.empty())return Ret(InvalidParam);
    const auto generation=it->second.generation;std::weak_ptr<Stream> weak=s;
    const auto result=rt::Audio().Submit(s->owner,pcm,[weak,p,generation](bool cancelled){
        if(auto state=weak.lock()) {
            if(state->retired.size()>=rt::AudioOutput::Slots){state->fault=true;return;}
            state->retired.push_back({p,generation,cancelled});
        }
    },s->handle);
    if(result!=rt::AudioOutput::Result::Accepted)return Ret(result==rt::AudioOutput::Result::Busy?NoMemory:Error);
    s->converter=converter;s->writtenFrames+=bytes/s->format.Align();
    it->second.queued=true;m.Write32(p+16,(flags&~Done)|Prepared|InQueue);return Ret(0);
}
HostCallOutcome Reset(cpu::CpuState& c,cpu::IGuestMemory& m,bool close) {
    PollWaveOut();auto s=Valid(Arg(c,m,0));if(!s)return Ret(InvalidHandle);
    if(close)for(const auto& h:s->headers)if(h.second.queued)return Ret(StillPlaying);
    if(!rt::Audio().Reset())return Ret(Error);
    Drain(*s);
    if(!close){s->converter=rt::PcmStream(s->format,false);s->writtenFrames=0;
        s->positionValid=rt::Audio().Position(s->owner,s->handle,s->positionBase);
        for(const auto& h:s->headers){if(!Range(m,h.first,32))return Ret(Error);}
        s->fault=false;return Ret(0);}
    if(!Notify(*s,CloseMessage))return Ret(NoMemory);
    for(const auto& h:s->headers)if(Range(m,h.first,32))m.Write32(h.first+16,m.Read32(h.first+16)&~(Prepared|InQueue));
    rt::Audio().ReleaseClient(s->owner,s->handle);s->open=false;s->headers.clear();rt::LocalState<Holder>().stream.reset();return Ret(0);
}
HostCallOutcome Pause(cpu::CpuState& c,cpu::IGuestMemory& m,bool paused) {
    PollWaveOut();auto s=Valid(Arg(c,m,0));if(!s)return Ret(InvalidHandle);
    return Ret(rt::Audio().Pause(s->owner,s->handle,paused)?0:Error);
}
HostCallOutcome Position(cpu::CpuState& c,cpu::IGuestMemory& m) {
    PollWaveOut();auto s=Valid(Arg(c,m,0));if(!s)return Ret(InvalidHandle);
    const auto p=Arg(c,m,1);if(Arg(c,m,2)<12 || !Range(m,p,12))return Ret(InvalidParam);
    uint64_t frames=0;
    if(!s->positionValid || !rt::Audio().Position(s->owner,s->handle,frames) || frames<s->positionBase)return Ret(Error);
    if(frames-s->positionBase>UINT64_MAX/s->format.rate)return Ret(Error);
    frames=std::min((frames-s->positionBase)*s->format.rate/48000,s->writtenFrames);
    auto type=m.Read32(p);uint64_t value=frames;
    if(type==1)value=frames*1000/s->format.rate;
    else if(type!=2){type=4;value=frames*s->format.Align();}
    m.Write32(p,type);m.Write32(p+4,uint32_t(value));m.Write32(p+8,0);return Ret(0);
}
HostCallOutcome Volume(cpu::CpuState& c,cpu::IGuestMemory& m,bool set) {
    const auto handle=Arg(c,m,0),p=Arg(c,m,1);
    if(handle && handle!=0xffffffffu && !Valid(handle))return Ret(InvalidHandle);
    if(!rt::Audio().VolumeSupported())return Ret(8);
    if(set){if(uint16_t(p)!=uint16_t(p>>16))return Ret(8);
        return Ret(rt::Audio().SetVolume(rt::Audio().Owner(),rt::Audio().Client(),uint16_t(p))?0:Error);}
    if(!Range(m,p,4))return Ret(InvalidParam);
    uint16_t value=0;if(!rt::Audio().Volume(value))return Ret(Error);
    m.Write32(p,uint32_t(value)|uint32_t(value)<<16);return Ret(0);
}
#define FN(name) HostCallOutcome name(cpu::CpuState& c,cpu::IGuestMemory& m)
#define ROUTE(name,op,direct) FN(name){return IsMixedWaveHandle(Arg(c,m,0))?MixedWaveCall(MixedWaveOp::op,c,m):(direct);}
FN(waveOutOpen){return (Arg(c,m,1)==1 || (Arg(c,m,1)==0xffffffffu && MixedWaveSupported()))?MixedWaveCall(MixedWaveOp::Open,c,m):Open(c,m);}
ROUTE(waveOutPrepareHeader,Prepare,Prepare(c,m,false)) ROUTE(waveOutUnprepareHeader,Unprepare,Prepare(c,m,true))
ROUTE(waveOutPause,Pause,Pause(c,m,true)) ROUTE(waveOutRestart,Restart,Pause(c,m,false))
ROUTE(waveOutGetPosition,Position,Position(c,m)) ROUTE(waveOutSetVolume,SetVolume,Volume(c,m,true)) ROUTE(waveOutGetVolume,GetVolume,Volume(c,m,false))
ROUTE(waveOutWrite,Write,Write(c,m)) ROUTE(waveOutReset,Reset,Reset(c,m,false)) ROUTE(waveOutClose,Close,Reset(c,m,true))
#define E(name,bytes) {"WINMM.dll",#name,bytes,CallConv::Stdcall,&name}
const HostFunctionEntry entries[]={E(waveOutOpen,24),E(waveOutPrepareHeader,12),E(waveOutUnprepareHeader,12),E(waveOutWrite,12),E(waveOutReset,4),E(waveOutClose,4),E(waveOutPause,4),E(waveOutRestart,4),E(waveOutGetPosition,12),E(waveOutSetVolume,8),E(waveOutGetVolume,8)};
const bool registered=RegisterHostFunctions(entries,sizeof(entries)/sizeof(entries[0]));
}
void PollWaveOut(){PollMixedWave();rt::Audio().Poll();if(auto s=Current())Drain(*s);}
void EndWaveOutSession(){
    EndWaveCallbacks();
    EndMixedWave();
    auto s=Current();if(!s)return;s->notifications=false;
    if(rt::Audio().Reset())Drain(*s);
    rt::Audio().ReleaseClient(s->owner,s->handle);s->open=false;rt::LocalState<Holder>().stream.reset();
}
} // namespace win32
