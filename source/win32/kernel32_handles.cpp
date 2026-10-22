#include "handle_util.h"
namespace win32 { namespace {
using namespace hle;
using Kind=rt::ObjectKind;
// Named A APIs use the runtime ACP (1252), W APIs preserve UTF-16 exactly.
uint16_t AnsiUnit(uint8_t b) {
    static constexpr uint16_t high[]={
        0x20ac,0x81,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,0x2c6,0x2030,0x160,0x2039,0x152,0x8d,0x17d,0x8f,
        0x90,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0x9d,0x17e,0x178};
    return b>=0x80 && b<0xa0 ? high[b-0x80] : b;
}
bool ReadObjectName(cpu::IGuestMemory& m,uint32_t pointer,bool wide,std::u16string& name) {
    if(!pointer) return true;
    const uint32_t stride=wide ? 2 : 1;
    for(uint32_t i=0;i<=260;++i) {
        const uint64_t address=uint64_t(pointer)+i*stride;
        if(address+stride>0x100000000ull || !m.IsRangeMapped(static_cast<uint32_t>(address),stride)) {
            SetLastError(m,87);return false;
        }
        const uint16_t ch=wide ? m.Read16(static_cast<uint32_t>(address)) : AnsiUnit(m.Read8(static_cast<uint32_t>(address)));
        if(!ch) {
            const bool local=name.compare(0,6,u"Local\\")==0;
            const size_t offset=local ? 6 : (name.compare(0,7,u"Global\\")==0 ? 7 : 0);
            if((offset && name.size()==offset) || name.find(u'\\',offset)!=std::u16string::npos) {
                SetLastError(m,123);return false;
            }
            // Default application namespace is the current Local session.
            if(local) name.erase(0,6);
            return true;
        }
        name.push_back(static_cast<char16_t>(ch));
    }
    SetLastError(m,206);return false;
}
HostCallOutcome SyncHandle(cpu::CpuState& c,cpu::IGuestMemory& m,Kind kind,bool wide,bool open) {
    uint32_t flags=0;
    if(open) flags=Arg(c,m,1) ? rt::kHandleInherit : 0;
    else if(!InheritAttribute(m,Arg(c,m,0),flags)) return Ret(0);
    const uint32_t namePtr=Arg(c,m,open ? 2 : ((kind==Kind::Mutex || kind==Kind::Timer) ? 2 : 3));
    if(open && !namePtr) { SetLastError(m,87);return Ret(0); }
    std::u16string name;
    if(!ReadObjectName(m,namePtr,wide,name)) return Ret(0);
    auto object=name.empty() ? nullptr : rt::Handles().FindNamed(name);
    if(object && object->type!=kind) { SetLastError(m,6);return Ret(0); }
    if(open && !object) { SetLastError(m,2);return Ret(0); }
    const uint32_t access=open ? rt::MapObjectAccess(kind,Arg(c,m,0)) : rt::AllObjectAccess(kind);
    if(access & ~rt::AllObjectAccess(kind)) { SetLastError(m,5);return Ret(0); }
    const bool existing=object!=nullptr;
    if(!object) {
        std::shared_ptr<rt::SyncObject> sync=kind==Kind::Timer ?
            std::static_pointer_cast<rt::SyncObject>(std::make_shared<rt::TimerObject>()) :
            std::make_shared<rt::SyncObject>(kind);
        if(kind==Kind::Event) {
            sync->manualReset=Arg(c,m,1)!=0;sync->signaled=Arg(c,m,2)!=0;
        } else if(kind==Kind::Timer) {
            sync->manualReset=Arg(c,m,1)!=0;
        } else if(kind==Kind::Mutex) {
            if(Arg(c,m,1)) {
                sync->ownerThread=rt::g_process ? rt::g_process->CurrentThreadId() : 0x1004;
                sync->recursion=1;
            }
            sync->signaled=!sync->ownerThread;
        } else {
            sync->count=static_cast<int32_t>(Arg(c,m,1));sync->maxCount=static_cast<int32_t>(Arg(c,m,2));
            if(sync->count<0 || sync->maxCount<=0 || sync->count>sync->maxCount) { SetLastError(m,87);return Ret(0); }
        }
        object=sync;
    }
    const uint32_t handle=rt::Handles().Add(object,access,flags);
    if(!handle) { SetLastError(m,8);return Ret(0); }
    if(!existing && !name.empty()) rt::Handles().BindName(name,object);
    if(!open) SetLastError(m,existing ? 183 : 0);
    return Ret(handle);
}
#define SYNC_API(api,kind,wide,open) HostCallOutcome api(cpu::CpuState& c,cpu::IGuestMemory& m) { return SyncHandle(c,m,Kind::kind,wide,open); }
SYNC_API(CreateWaitableTimerA,Timer,false,false) SYNC_API(CreateWaitableTimerW,Timer,true,false)
SYNC_API(OpenWaitableTimerA,Timer,false,true) SYNC_API(OpenWaitableTimerW,Timer,true,true)
SYNC_API(CreateEventA,Event,false,false) SYNC_API(CreateEventW,Event,true,false)
SYNC_API(CreateMutexA,Mutex,false,false) SYNC_API(CreateMutexW,Mutex,true,false)
SYNC_API(CreateSemaphoreA,Semaphore,false,false) SYNC_API(CreateSemaphoreW,Semaphore,true,false)
SYNC_API(OpenEventA,Event,false,true) SYNC_API(OpenEventW,Event,true,true)
SYNC_API(OpenMutexA,Mutex,false,true) SYNC_API(OpenMutexW,Mutex,true,true)
SYNC_API(OpenSemaphoreA,Semaphore,false,true) SYNC_API(OpenSemaphoreW,Semaphore,true,true)
#undef SYNC_API
HostCallOutcome GetHandleInformation(cpu::CpuState& c,cpu::IGuestMemory& m) {
    const auto* entry=rt::Handles().Entry(Arg(c,m,0));
    if(!entry) { SetLastError(m,6);return Ret(0); }
    const uint32_t output=Arg(c,m,1);
    if(!output || !m.IsRangeMapped(output,4)) { SetLastError(m,87);return Ret(0); }
    m.Write32(output,entry->flags);return Ret(1);
}
HostCallOutcome SetHandleInformation(cpu::CpuState& c,cpu::IGuestMemory& m) {
    if(!rt::Handles().SetFlags(Arg(c,m,0),Arg(c,m,1),Arg(c,m,2))) { SetLastError(m,6);return Ret(0); }
    return Ret(1);
}
#define ENTRY(api,bytes) {"KERNEL32.dll",#api,bytes,CallConv::Stdcall,&api}
const HostFunctionEntry entries[]={
    ENTRY(CreateWaitableTimerA,12),ENTRY(CreateWaitableTimerW,12),ENTRY(OpenWaitableTimerA,12),ENTRY(OpenWaitableTimerW,12),
    ENTRY(CreateEventA,16),ENTRY(CreateEventW,16),ENTRY(CreateMutexA,12),ENTRY(CreateMutexW,12),
    ENTRY(CreateSemaphoreA,16),ENTRY(CreateSemaphoreW,16),ENTRY(OpenEventA,12),ENTRY(OpenEventW,12),
    ENTRY(OpenMutexA,12),ENTRY(OpenMutexW,12),ENTRY(OpenSemaphoreA,12),ENTRY(OpenSemaphoreW,12),
    ENTRY(GetHandleInformation,8),ENTRY(SetHandleInformation,12),
};
#undef ENTRY
const bool registered=RegisterHostFunctions(entries,sizeof(entries)/sizeof(entries[0]));
} }
