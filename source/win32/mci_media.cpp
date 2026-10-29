#include "mci_media.h"
#include "hle_util.h"
#include "user32.h"
#include "../runtime/media_player.h"
#include "../runtime/audio_output.h"
#include "../runtime/guest_execution.h"
#include <algorithm>
namespace win32 {namespace {
using namespace hle;
constexpr uint32_t BadId=257,Unsupported=274,NotReady=276,BadParam=273;
struct MediaState {
    rt::MediaPlayer player;uint32_t id=0,timeFormat=0,notify=0;
    std::string alias;bool notificationFailed=false;
};
uint32_t nextId=0x7100;
MediaState& State(){return rt::LocalState<MediaState>();}
bool Range(cpu::IGuestMemory& m,uint32_t p,size_t n){return p && uint64_t(p)+n<=0x100000000ull && m.IsRangeMapped(p,n);}
bool String(cpu::IGuestMemory& m,uint32_t p,bool wide,std::string& out){
    out.clear();std::u16string text;if(!p)return false;
    for(unsigned n=0;n<4096;++n){const auto at=uint64_t(p)+n*(wide?2:1);if(at>0xffffffff || !Range(m,uint32_t(at),wide?2:1))return false;
        const auto c=wide?m.Read16(uint32_t(at)):m.Read8(uint32_t(at));
        if(!c){out=wide?WideToUtf8(text):out;return true;}
        if(wide)text.push_back(char16_t(c));else out.push_back(char(c));}
    return false;
}
std::string Lower(std::string s){for(auto& c:s)if(c>='A' && c<='Z')c=char(c+32);return s;}
bool Target(uint32_t window){
    if(!IsHostLayerEnabled(kUiLayer))return false;
    auto* w=user::Win(window);if(!w)return false;
    const auto queue=user::S().queues.find(w->threadId);
    return queue!=user::S().queues.end() && !queue->second.closed && queue->second.posted.size()<1024;
}
bool Notify(uint32_t window,uint32_t id,uint32_t reason){
    if(!window)return true;
    auto* w=user::Win(window);if(!w)return true;
    const auto queue=user::S().queues.find(w->threadId);
    if(queue==user::S().queues.end() || queue->second.closed)return true;
    if(queue->second.posted.size()>=1024){State().notificationFailed=true;return false;}
    return user::PostToThread(w->threadId,window,0x3b9,reason,id);
}
uint64_t Frames(uint32_t value){
    auto& s=State();if(s.timeFormat!=9)return uint64_t(value)*48;
    if(value>s.player.SourceFrameCount() || !s.player.SampleRate())return UINT64_MAX;
    if(value==s.player.SourceFrameCount())return s.player.Duration();
    // Source samples, not the device's resampled 48k frames. Round upward to
    // the first representable output frame; EOF is the exact duration boundary.
    return (uint64_t(value)*48000+s.player.SampleRate()-1)/s.player.SampleRate();
}
uint32_t Units(uint64_t frames){
    auto& s=State();if(s.timeFormat!=9)return uint32_t(frames/48);
    if(frames>=s.player.Duration())return uint32_t(s.player.SourceFrameCount());
    return uint32_t(frames*s.player.SampleRate()/48000);
}
HostCallOutcome Command(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide){
    if(!rt::g_process)return Ret(NotReady);
    PollMciMedia();auto& s=State();const auto device=Arg(c,m,0),command=Arg(c,m,1),flags=Arg(c,m,2),p=Arg(c,m,3);
    if(s.notificationFailed){s.notificationFailed=false;return Ret(264);}
    if(p && !Range(m,p,4))return Ret(BadParam);
    const auto callback=p?m.Read32(p):0;if((flags&1) && !Target(callback))return Ret(BadParam);
    if(command==0x803){ // MCI_OPEN: actual decoder/file admission, not a stub device
        if(flags&~uint32_t(1|2|0x200|0x400|0x1000|0x2000))return Ret(Unsupported);
        if(!Range(m,p,20) || !(flags&0x200))return Ret(BadParam);
        if(s.id)return Ret(265);
        std::string path,type,alias;
        if(flags&0x2000){
            if(flags&0x1000){const auto id=m.Read32(p+8);if(id!=520 && id!=522)return Ret(Unsupported);}
            else if(!String(m,m.Read32(p+8),wide,type))return Ret(BadParam);
            type=Lower(type);if(!type.empty() && type!="mpegvideo" && type!="waveaudio")return Ret(Unsupported);
        }else if(flags&0x1000)return Ret(BadParam);
        if(!String(m,m.Read32(p+12),wide,path) || path.empty())return Ret(BadParam);
        if((flags&0x400) && (!String(m,m.Read32(p+16),wide,alias) || alias.empty()))return Ret(BadParam);
        if(!rt::Audio().Available())return Ret(NotReady);
        if(nextId==0xffff)return Ret(264);
        if(!s.player.Open(rt::FileSystem(),path))return Ret(NotReady);
        s.id=nextId++;s.timeFormat=0;s.alias=Lower(alias);m.Write32(p+4,s.id);
        if(flags&1)Notify(callback,s.id,1);
        return Ret(0);
    }
    if(!s.id || (device!=s.id && device!=0xffff))return Ret(BadId);
    if(command==0x80b){ // MCI_GETDEVCAPS, one file-backed audio device
        if((flags&~uint32_t(1|2|0x100)) || !(flags&0x100))return Ret(Unsupported);
        if(!Range(m,p,12))return Ret(BadParam);
        uint32_t value=0;
        switch(m.Read32(p+8)){
        case 1:case 3:case 6:case 7:case 9:value=0;break;
        case 2:case 5:case 8:value=1;break;
        case 4:value=s.player.Mp3()?520:522;break;
        default:return Ret(Unsupported);}
        m.Write32(p+4,value);if(flags&1)Notify(callback,s.id,1);return Ret(0);
    }
    if(command==0x814){ // MCI_STATUS
        if((flags&~uint32_t(1|2|0x100)) || !(flags&0x100))return Ret(Unsupported);
        if(!Range(m,p,16))return Ret(BadParam);
        uint32_t value=0;
        switch(m.Read32(p+8)){
        case 1:value=Units(s.player.Duration());break;
        case 2:if(s.player.Status()==rt::MediaPlayer::State::Error)return Ret(NotReady);value=Units(s.player.Position());break;
        case 3:value=1;break;
        case 4:value=s.player.Status()==rt::MediaPlayer::State::Playing?526:s.player.Status()==rt::MediaPlayer::State::Paused?529:s.player.Status()==rt::MediaPlayer::State::Error?524:525;break;
        case 5:value=s.player.Loaded()?1:0;break;
        case 7:value=rt::Audio().Available() && s.player.Status()!=rt::MediaPlayer::State::Error;break;
        default:return Ret(Unsupported);}
        m.Write32(p+4,value);if(flags&1)Notify(callback,s.id,1);
        return Ret(0);
    }
    if(command==0x80d){ // MCI_SET TIME_FORMAT only
        if((flags&~uint32_t(1|2|0x400)) || !(flags&0x400))return Ret(Unsupported);
        if(!Range(m,p,12) || (m.Read32(p+4)!=0 && m.Read32(p+4)!=9))return Ret(Unsupported);
        s.timeFormat=m.Read32(p+4);if(flags&1)Notify(callback,s.id,1);
        return Ret(0);
    }
    if(command==0x806){ // MCI_PLAY [FROM] [NOTIFY or WAIT]
        if(flags&~uint32_t(1|2|4) || (flags&3)==3)return Ret(Unsupported);
        if((flags&2) && (!rt::GuestExecution::Active() || !rt::g_process->Scheduler().Current()))return Ret(Unsupported);
        if((flags&4) && (!Range(m,p,12) || !s.player.Seek(Frames(m.Read32(p+4)))))return Ret(BadParam);
        if(s.notify){Notify(s.notify,s.id,2);s.notify=0;} // superseded
        if(!s.player.Play())return Ret(NotReady);
        if(flags&1)s.notify=callback;
        if(flags&2){
            auto* thread=rt::g_process->Scheduler().Current();if(!thread || !rt::GuestExecution::Active())return Ret(Unsupported);
            while(s.player.Status()==rt::MediaPlayer::State::Playing && rt::GuestExecution::Checkpoint()){
                rt::g_process->Scheduler().Sleep(*thread,1);rt::GuestExecution::Yield();PollMciMedia();}
            if(!rt::GuestExecution::Checkpoint())return Ret(286);
            if(s.player.Status()==rt::MediaPlayer::State::Error)return Ret(NotReady);
        }
        return Ret(0);
    }
    if(command==0x807){ // MCI_SEEK TO/START/END
        if(flags&~uint32_t(1|2|8|0x100|0x200))return Ret(Unsupported);
        const auto targets=flags&(8|0x100|0x200);if(!targets || (targets&(targets-1)))return Ret(BadParam);
        uint64_t position=0;if(targets==0x200)position=s.player.Duration();
        else if(targets==8){if(!Range(m,p,8))return Ret(BadParam);position=Frames(m.Read32(p+4));}
        if(s.notify){Notify(s.notify,s.id,4);s.notify=0;}
        if(!s.player.Stop() || !s.player.Seek(position))return Ret(NotReady);
        if(flags&1)Notify(callback,s.id,1);
        return Ret(0);
    }
    if(flags&~uint32_t(1|2))return Ret(Unsupported);
    bool success=false;
    if(command==0x809)success=s.player.Pause();
    else if(command==0x855)success=s.player.Status()==rt::MediaPlayer::State::Paused && s.player.Play();
    else if(command==0x808 || command==0x804){
        if(!(command==0x804?s.player.Close():s.player.Stop()))return Ret(NotReady);
        if(s.notify){Notify(s.notify,s.id,4);s.notify=0;}
        success=true;
    }else return Ret(Unsupported);
    if(!success)return Ret(NotReady);
    if(flags&1)Notify(callback,s.id,1);
    if(command==0x804){s.id=0;s.alias.clear();}
    return Ret(0);
}
HostCallOutcome DeviceId(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide){std::string name;if(!String(m,Arg(c,m,0),wide,name))return Ret(0);auto& s=State();return Ret(s.id && !s.alias.empty() && Lower(name)==s.alias?s.id:0);}
HostCallOutcome ErrorString(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide){
    const auto code=Arg(c,m,0),p=Arg(c,m,1),size=Arg(c,m,2);const char* text=nullptr;
    switch(code){case 0:text="No error";break;case BadId:text="Invalid media device";break;case Unsupported:text="Unsupported media command or format";break;case NotReady:text="Audio file or device not ready";break;case BadParam:text="Invalid media parameters";break;case 264:text="Media device IDs exhausted";break;case 265:text="Media device already open";break;case 286:text="Media operation aborted";break;default:return Ret(0);}
    if(!size || size>4096 || !Range(m,p,size_t(size)*(wide?2:1)))return Ret(0);
    size_t n=0;while(text[n] && n+1<size){if(wide)m.Write16(p+uint32_t(n*2),uint16_t(text[n]));else m.Write8(p+uint32_t(n),uint8_t(text[n]));++n;}
    if(wide)m.Write16(p+uint32_t(n*2),0);else m.Write8(p+uint32_t(n),0);return Ret(1);
}
#define F(name) HostCallOutcome name(cpu::CpuState& c,cpu::IGuestMemory& m)
F(mciSendCommandA){return Command(c,m,false);}F(mciSendCommandW){return Command(c,m,true);}
F(mciGetDeviceIDA){return DeviceId(c,m,false);}F(mciGetDeviceIDW){return DeviceId(c,m,true);}
F(mciGetErrorStringA){return ErrorString(c,m,false);}F(mciGetErrorStringW){return ErrorString(c,m,true);}
#define E(name,n) {"WINMM.dll",#name,n,CallConv::Stdcall,&name}
const HostFunctionEntry entries[]={E(mciSendCommandA,16),E(mciSendCommandW,16),E(mciGetDeviceIDA,4),E(mciGetDeviceIDW,4),E(mciGetErrorStringA,12),E(mciGetErrorStringW,12)};
const bool registered=RegisterHostFunctions(entries,sizeof(entries)/sizeof(entries[0]));
}
void PollMciMedia(){if(!rt::g_process)return;auto& s=State();s.player.Tick();
    if(s.notify && (s.player.Status()==rt::MediaPlayer::State::Finished || s.player.Status()==rt::MediaPlayer::State::Error)){
        Notify(s.notify,s.id,s.player.Status()==rt::MediaPlayer::State::Finished?1:8);s.notify=0;}}
void EndMciMedia(){if(!rt::g_process)return;auto& s=State();s.notify=0;s.player.Close();s.id=0;s.alias.clear();}
}
