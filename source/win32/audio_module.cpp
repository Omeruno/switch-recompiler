#include "hle_util.h"
#include "wave_mixer.h"
#include "../runtime/audio_output.h"
#include "../runtime/resources.h"
#include <algorithm>
#include <cstring>
namespace win32 {namespace {
using namespace hle;
bool Range(cpu::IGuestMemory& m,uint32_t p,size_t n){return p && uint64_t(p)+n<=0x100000000ull && m.IsRangeMapped(p,n);}
HostCallOutcome Failure(cpu::IGuestMemory& m,uint32_t error){SetLastError(m,error);return Ret(0);}
HostCallOutcome Play(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide) {
    const auto sound=Arg(c,m,0),module=Arg(c,m,1),flags=Arg(c,m,2);
    if(!rt::g_process)return Failure(m,6);
    if(rt::Audio().Client())return Failure(m,170);
    if(!sound)return rt::Audio().Reset()?Ret(1):Failure(m,1117);
    // Bounded asynchronous resource/memory playback; no invented sync/loop success.
    if(!(flags&1) || (flags&~uint32_t(1|2|4|16|0x40000)))return Failure(m,50);
    if(!rt::Audio().Poll())return Failure(m,1117);
    if(!rt::Audio().Available())return Failure(m,6);
    std::vector<uint8_t> bytes;
    if((flags&0x40000)==0x40000) {
        const auto* mod=module?rt::g_process->FindByHandle(module):rt::g_process->MainModule();
        rt::ResId id;
        if(sound<65536)id=rt::ResId::Num(uint16_t(sound));
        else {
            std::u16string name;const unsigned stride=wide?2:1;bool ended=false;
            for(size_t n=0;n<256;++n){const uint64_t at=uint64_t(sound)+n*stride;if(at>0xffffffff || !Range(m,uint32_t(at),stride))return Failure(m,998);
                const auto ch=wide?m.Read16(uint32_t(at)):m.Read8(uint32_t(at));if(!ch){ended=true;break;}if(!wide && ch>127)return Failure(m,1113);name.push_back(char16_t(ch));}
            if(!ended)return Failure(m,206);
            id=rt::ResId::Str(name);
        }
        rt::ResourceData data;
        if(!mod || !rt::FindResource(mod,rt::ResId::Str(u"WAVE"),id,data) || data.size>1024*1024)return Failure(m,1814);
        const auto* raw=rt::ResourcePtr(mod,data);if(!raw)return Failure(m,13);
        bytes.assign(raw,raw+data.size);
    }else if(flags&4) {
        if(!Range(m,sound,12))return Failure(m,998);
        const uint64_t size=uint64_t(m.Read32(sound+4))+8;
        if(size<12 || size>1024*1024)return Failure(m,13);
        if(!Range(m,sound,size))return Failure(m,998);
        bytes.resize(size_t(size));for(size_t n=0;n<bytes.size();++n)bytes[n]=m.Read8(sound+uint32_t(n));
    }else return Failure(m,50);
    if((flags&16) && rt::Audio().Pending())return Failure(m,170);
    std::vector<int16_t> pcm;if(!rt::DecodeWave(bytes,pcm))return Failure(m,13);
    if(rt::Audio().Pending() && !rt::Audio().Reset())return Failure(m,1117);
    const auto result=rt::Audio().Submit(rt::Audio().Owner(),pcm);
    if(result==rt::AudioOutput::Result::Accepted)return Ret(1);
    return Failure(m,result==rt::AudioOutput::Result::Busy?170:result==rt::AudioOutput::Result::DeviceError?1117:6);
}
HostCallOutcome Caps(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide) {
    const auto device=Arg(c,m,0),output=Arg(c,m,1),size=Arg(c,m,2);
    if(IsMixedWaveHandle(device) && !MixedWaveHandleLive(device))return Ret(2);
    const bool mixed=device==1 || IsMixedWaveHandle(device) || (device==0xffffffffu && MixedWaveSupported());
    if(device && device!=0xffffffffu && !mixed)return Ret(2);
    if(mixed && !MixedWaveSupported())return Ret(6);
    rt::Audio().Poll();if(!rt::Audio().Available())return Ret(6);
    const size_t length=wide?84:52,count=std::min(size_t(size),length);
    if(!count || !Range(m,output,count))return Ret(11);
    std::vector<uint8_t> bytes(length);bytes[4]=0;bytes[5]=0;bytes[6]=1; // driver version1.0
    const char* name=mixed?"PCM software mixer":rt::Audio().Name();for(size_t n=0;n<31 && name[n];++n)bytes[8+n*(wide?2:1)]=uint8_t(name[n]);
    const size_t formats=wide?72:40;bytes[formats]=0xff;bytes[formats+1]=0xff;bytes[formats+2]=0x0f; // 11/22/44/48/96k mono/stereo PCM8/16
    bytes[formats+4]=2;bytes[formats+8]=mixed?12:rt::Audio().VolumeSupported()?4:0; // WAVECAPS_VOLUME, no stereo balance
    for(size_t n=0;n<count;++n)m.Write8(output+uint32_t(n),bytes[n]);
    return Ret(0);
}
#define FN(name) HostCallOutcome name(cpu::CpuState& c,cpu::IGuestMemory& m)
FN(PlaySoundW){return Play(c,m,true);} FN(PlaySoundA){return Play(c,m,false);}
FN(waveOutGetNumDevs){(void)c;(void)m;rt::Audio().Poll();return Ret(rt::Audio().Available()?(MixedWaveSupported()?2:1):0);}
FN(waveOutGetDevCapsA){return Caps(c,m,false);} FN(waveOutGetDevCapsW){return Caps(c,m,true);}
#define E(name,bytes) {"WINMM.dll",#name,bytes,CallConv::Stdcall,&name}
const HostFunctionEntry entries[]={E(PlaySoundW,12),E(PlaySoundA,12),E(waveOutGetNumDevs,0),E(waveOutGetDevCapsA,12),E(waveOutGetDevCapsW,12)};
const bool registered=RegisterHostFunctions(entries,sizeof(entries)/sizeof(entries[0]));
}}
