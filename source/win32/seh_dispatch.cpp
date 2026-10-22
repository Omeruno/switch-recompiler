// SEH registration-chain dispatcher (single guest thread, 32-bit x86).
// Calls actual guest handlers, never host C++ exception handlers.
#include "seh_dispatch.h"
#include "../runtime/guest_fault.h"
#include "seh_abi.h"
#include "message_loop.h"
#include "../runtime/process.h"
#include <cstring>
#include <cmath>
#include <limits>

namespace win32 {
namespace seh {
namespace {
constexpr uint32_t kContextFlags = 0x1002Fu; // x86 CONTROL | INTEGER | SEGMENTS | FLOATING_POINT | EXTENDED_REGISTERS
constexpr uint32_t kContextSize = 716;
constexpr uint32_t kMaxChainDepth = 64;

// The active handler stack is owned by GuestThread, not process globals.
// Until multi-thread scheduling lands, only the main thread is runnable.
struct ActiveHandlerScope {
    rt::GuestThread* thread;
    bool entered=false;
    ActiveHandlerScope(rt::GuestThread* t,uint32_t frame) : thread(t) {
        if (t && t->activeSehDepth < 16) {
            t->activeSeh[t->activeSehDepth++].frame=frame;
            entered=true;
        }
    }
    ~ActiveHandlerScope() { if (entered) --thread->activeSehDepth; }
};
bool IsActiveHandler(const rt::GuestThread* t,uint32_t frame) {
    if (!t) return false;
    for(uint32_t i=0;i<t->activeSehDepth;++i)
        if(t->activeSeh[i].frame==frame) return true;
    return false;
}

// Windows x86 CONTEXT offsets; validated against winnt.h.
constexpr uint32_t kGs=140,kFs=144,kEs=148,kDs=152;
constexpr uint32_t kEdi=156,kEsi=160,kEbx=164,kEdx=168,kEcx=172,kEax=176;
constexpr uint32_t kEbp=180,kEip=184,kCs=188,kEflags=192,kEsp=196,kSs=200;


// CONTEXT32: FLOATING_SAVE_AREA at +28, ExtendedRegisters (FXSAVE-like)
// at +204, 512 bytes. Our interpreter stores x87 numbers as double, so
// its 80-bit image is converted at the ABI boundary (53-bit precision).
constexpr uint32_t kFloat=28, kExtended=204;
constexpr uint32_t kFxSt=32, kFxXmm=160;
uint64_t ReadU64(cpu::IGuestMemory& m,uint32_t at) {
    return uint64_t(m.Read32(at)) | (uint64_t(m.Read32(at+4))<<32);
}
void WriteU64(cpu::IGuestMemory& m,uint32_t at,uint64_t value) {
    m.Write32(at,static_cast<uint32_t>(value));
    m.Write32(at+4,static_cast<uint32_t>(value>>32));
}
void EncodeExt80(cpu::IGuestMemory& m,uint32_t at,double value) {
    uint64_t raw=0;
    std::memcpy(&raw,&value,8);
    const uint64_t sign=raw>>63, frac=raw&((1ull<<52)-1);
    const unsigned exp=static_cast<unsigned>((raw>>52)&0x7ff);
    uint64_t mant=0; uint16_t exponent=0;
    if(exp==0x7ff) {
        exponent=0x7fff; mant=(1ull<<63)|(frac<<11);
    } else if(exp) {
        exponent=static_cast<uint16_t>(int(exp)-1023+16383);
        mant=((1ull<<52)|frac)<<11;
    } else if(frac) {
        mant=frac<<11;
        int e=16383-1022;
        while(!(mant&(1ull<<63))) { mant<<=1; --e; }
        exponent=static_cast<uint16_t>(e);
    }
    WriteU64(m,at,mant);
    m.Write16(at+8,static_cast<uint16_t>(exponent|(sign<<15)));
}
double DecodeExt80(cpu::IGuestMemory& m,uint32_t at) {
    uint64_t mant=ReadU64(m,at);
    uint16_t extExp=m.Read16(at+8);
    bool negative=(extExp&0x8000)!=0;
    int exp=extExp&0x7fff;
    double result=0;
    if(exp==0x7fff)
        result=(mant&0x7fffffffffffffffull)?std::nan(""):std::numeric_limits<double>::infinity();
    else if(mant)
        result=std::ldexp(static_cast<double>(mant), (exp?exp:1)-16383-63);
    return negative?-result:result;
}
void SaveFloating(cpu::IGuestMemory& m,uint32_t dst,const cpu::CpuState& c) {
    const uint32_t fa=dst+kFloat, fx=dst+kExtended;
    m.Write32(fa+0,c.fpuControl);
    m.Write32(fa+4,c.GetFpuStatusWord());
    m.Write32(fa+8,c.fpuTag);
    m.Write32(fa+12,c.fpuLastIp);
    m.Write32(fa+20,c.fpuLastDp);
    m.Write32(fx+0,uint32_t(c.fpuControl)|(uint32_t(c.GetFpuStatusWord())<<16));
    uint8_t ftw=0;
    for(unsigned i=0;i<8;++i) if(((c.fpuTag>>(2*i))&3)!=3)ftw|=uint8_t(1u<<i);
    m.Write32(fx+4,uint32_t(ftw)|(uint32_t(c.fpuLastOpcode)<<16));
    m.Write32(fx+8,c.fpuLastIp);
    m.Write32(fx+16,c.fpuLastDp);
    m.Write32(fx+24,c.mxcsr);
    m.Write32(fx+28,0x0000ffffu); // MXCSR_MASK
    for(unsigned i=0;i<8;++i) {
        unsigned physical=(c.fpuTop+i)&7u;
        EncodeExt80(m,fa+28+i*10,c.st[physical]);
        EncodeExt80(m,fx+kFxSt+i*16,c.st[physical]);
        for(unsigned j=0;j<16;++j)
            m.Write8(fx+kFxXmm+i*16+j,c.xmm[i].b[j]);
    }
}
void RestoreFloating(cpu::IGuestMemory& m,uint32_t src,cpu::CpuState& c) {
    uint32_t flags=m.Read32(src);
    if(flags&0x8u) {
        const uint32_t fa=src+kFloat;
        c.fpuControl=static_cast<uint16_t>(m.Read32(fa));
        c.fpuStatus=static_cast<uint16_t>(m.Read32(fa+4));
        c.fpuTop=static_cast<uint8_t>((c.fpuStatus>>11)&7);
        c.fpuTag=static_cast<uint16_t>(m.Read32(fa+8));
        c.fpuLastIp=m.Read32(fa+12);
        c.fpuLastDp=m.Read32(fa+20);
        for(unsigned i=0;i<8;++i)
            c.st[(c.fpuTop+i)&7u]=DecodeExt80(m,fa+28+i*10);
    }
    if(flags&0x20u) {
        const uint32_t fx=src+kExtended;
        c.mxcsr=m.Read32(fx+24)&0x0000ffffu;
        for(unsigned i=0;i<8;++i)
            for(unsigned j=0;j<16;++j)
                c.xmm[i].b[j]=m.Read8(fx+kFxXmm+i*16+j);
    }
}

void SaveContext(cpu::IGuestMemory& m, uint32_t dst, const cpu::CpuState& c) {
    for(uint32_t i=0;i<kContextSize;i+=4)m.Write32(dst+i,0);
    m.Write32(dst,kContextFlags);
    m.Write32(dst+kGs,c.segSel[cpu::SEG_GS]);
    m.Write32(dst+kFs,c.segSel[cpu::SEG_FS]);
    m.Write32(dst+kEs,c.segSel[cpu::SEG_ES]);
    m.Write32(dst+kDs,c.segSel[cpu::SEG_DS]);
    m.Write32(dst+kCs,c.segSel[cpu::SEG_CS]);
    m.Write32(dst+kSs,c.segSel[cpu::SEG_SS]);
    m.Write32(dst+kEdi,c.Get(cpu::Reg32::EDI));
    m.Write32(dst+kEsi,c.Get(cpu::Reg32::ESI));
    m.Write32(dst+kEbx,c.Get(cpu::Reg32::EBX));
    m.Write32(dst+kEdx,c.Get(cpu::Reg32::EDX));
    m.Write32(dst+kEcx,c.Get(cpu::Reg32::ECX));
    m.Write32(dst+kEax,c.Get(cpu::Reg32::EAX));
    m.Write32(dst+kEbp,c.Get(cpu::Reg32::EBP));
    m.Write32(dst+kEip,c.eip);
    m.Write32(dst+kEflags,c.GetEflags());
    m.Write32(dst+kEsp,c.Get(cpu::Reg32::ESP));
    SaveFloating(m,dst,c);
}

void RestoreContext(cpu::IGuestMemory& m, uint32_t src, cpu::CpuState& c) {
    c.segSel[cpu::SEG_GS] = static_cast<uint16_t>(m.Read32(src+kGs));
    c.segSel[cpu::SEG_FS] = static_cast<uint16_t>(m.Read32(src+kFs));
    c.segSel[cpu::SEG_ES] = static_cast<uint16_t>(m.Read32(src+kEs));
    c.segSel[cpu::SEG_DS] = static_cast<uint16_t>(m.Read32(src+kDs));
    c.segSel[cpu::SEG_CS] = static_cast<uint16_t>(m.Read32(src+kCs));
    c.segSel[cpu::SEG_SS] = static_cast<uint16_t>(m.Read32(src+kSs));
    c.Get(cpu::Reg32::EDI)=m.Read32(src+kEdi);
    c.Get(cpu::Reg32::ESI)=m.Read32(src+kEsi);
    c.Get(cpu::Reg32::EBX)=m.Read32(src+kEbx);
    c.Get(cpu::Reg32::EDX)=m.Read32(src+kEdx);
    c.Get(cpu::Reg32::ECX)=m.Read32(src+kEcx);
    c.Get(cpu::Reg32::EAX)=m.Read32(src+kEax);
    c.Get(cpu::Reg32::EBP)=m.Read32(src+kEbp);
    c.eip=m.Read32(src+kEip);
    c.SetEflags(m.Read32(src+kEflags));
    c.Get(cpu::Reg32::ESP)=m.Read32(src+kEsp);
    c.lazyOp=cpu::LazyOp::None;
    RestoreFloating(m,src,c);
}
} // namespace

DispatchResult Dispatch(cpu::CpuState& state, cpu::IGuestMemory& mem,
                        uint32_t teb, uint32_t code, uint32_t flags,
                        uint32_t exceptionAddress, const uint32_t* params,
                        uint32_t paramCount) {
    if (!teb || !mem.IsRangeMapped(teb,4)) return DispatchResult::InvalidChain;
    // Nested SEH active-handler state follows the actual FS/TEB owner.
    rt::GuestThread* thread = nullptr;
    if (rt::g_process) {
        auto& process=*rt::g_process;
        rt::GuestThread* current=process.Scheduler().Current();
        if (current && current->teb==teb) thread=current;
        else if (process.MainThread().teb==teb) thread=&process.MainThread();
        // A callback may execute on a temporarily selected worker even
        // when another thread is the last scheduler dispatch.
        else {
            // Resolve the worker from ClientId.UniqueThread at TEB+0x24.
            if (mem.IsRangeMapped(teb+0x24,4)) {
                rt::GuestThread* candidate=process.FindThread(mem.Read32(teb+0x24));
                if (candidate && candidate->teb==teb) thread=candidate;
            }
        }
    }
    uint32_t frame=mem.Read32(teb); // NT_TIB.ExceptionList == FS:[0]
    if(frame==kEndOfChain) return DispatchResult::Unhandled;
    const uint32_t entryEsp=state.Get(cpu::Reg32::ESP);
    // Scratch space below ESP avoids guest heap allocation and remains within
    // the thread's stack. Values persist while guest handler runs.
    if (entryEsp < 2048 || !mem.IsRangeMapped(entryEsp-2048,2048))
        return DispatchResult::InvalidChain;
    const uint32_t exceptionPtr=(entryEsp-2048+15u)&~15u;
    const uint32_t contextPtr=(exceptionPtr+sizeof(ExceptionRecord32)+15u)&~15u;
    if (!mem.IsRangeMapped(exceptionPtr,sizeof(ExceptionRecord32)) ||
        !mem.IsRangeMapped(contextPtr,kContextSize)) return DispatchResult::InvalidChain;
    mem.Write32(exceptionPtr,code);
    mem.Write32(exceptionPtr+4,flags);
    mem.Write32(exceptionPtr+8,0);
    mem.Write32(exceptionPtr+12,exceptionAddress);
    uint32_t count=paramCount<kMaximumExceptionParameters?paramCount:kMaximumExceptionParameters;
    mem.Write32(exceptionPtr+16,count);
    for(uint32_t i=0;i<kMaximumExceptionParameters;++i)
        mem.Write32(exceptionPtr+20+i*4,i<count&&params?params[i]:0);
    SaveContext(mem,contextPtr,state);
    // Keep the dispatcher's scratch zone below its own frame separate from
    // the guest handler's transient stack.
    uint32_t seen[kMaxChainDepth]={};
    unsigned seenCount=0;
    while(frame!=kEndOfChain) {
        if(seenCount==kMaxChainDepth || (frame&3u) ||
           !mem.IsRangeMapped(frame,sizeof(RegistrationRecord32)))
            return DispatchResult::InvalidChain;
        for(unsigned i=0;i<seenCount;++i)
            if(seen[i]==frame)return DispatchResult::InvalidChain;
        seen[seenCount++]=frame;
        uint32_t next=mem.Read32(frame), handler=mem.Read32(frame+4);
        if(!handler || !mem.IsMapped(handler))return DispatchResult::InvalidChain;
        // The currently executing handler is already processing another
        // exception; nested dispatch searches any inner registrations and
        // then continues with older outer frames.
        if (IsActiveHandler(thread,frame)) {
            mem.Write32(exceptionPtr+4,mem.Read32(exceptionPtr+4)|kExceptionNestedCall);
            frame=next;
            continue;
        }
        if (thread && thread->activeSehDepth>=16) return DispatchResult::InvalidChain;
        cpu::CpuState handlerCpu=state;
        handlerCpu.ESP()=exceptionPtr-64; // leave scratch data intact
        ActiveHandlerScope scope(thread,frame);
        uint32_t disposition=CallGuestFunctionStdcall(handlerCpu,mem,handler,
            {exceptionPtr,frame,contextPtr,0},teb,teb);
        if(rt::GuestFaultPending())return DispatchResult::Unhandled;
        if(disposition==static_cast<uint32_t>(Disposition::ContinueExecution)){
            if(flags&kExceptionNoncontinuable)return DispatchResult::Noncontinuable;
            RestoreContext(mem,contextPtr,state);
            return DispatchResult::Handled;
        }
        if(disposition!=static_cast<uint32_t>(Disposition::ContinueSearch))
            return DispatchResult::InvalidChain; // nested/unwind require additional support
        frame=next;
    }
    return DispatchResult::Unhandled;
}

// Controlled target-frame unwind. We prevalidate the full registration
// chain before invoking any handler, so corrupt FS:[0] cannot partially
// discard handlers. Supports target-frame and exit-unwind; collided and
// nested unwind still require separate dispatch.
UnwindResult UnwindToFrame(cpu::CpuState& state, cpu::IGuestMemory& mem,
                           uint32_t teb, uint32_t targetFrame, uint32_t targetIp,
                           uint32_t exceptionRecord, uint32_t returnValue) {
    const bool exitUnwind = targetFrame == 0;
    const uint32_t stopFrame = exitUnwind ? kEndOfChain : targetFrame;
    if (!teb || !mem.IsRangeMapped(teb,4) || !targetIp ||
        (!exitUnwind && (targetFrame&3u)) || !mem.IsMapped(targetIp))
        return UnwindResult::InvalidTarget;
    uint32_t frames[kMaxChainDepth]={};
    uint32_t handlers[kMaxChainDepth]={};
    unsigned count=0;
    uint32_t frame=mem.Read32(teb);
    while(frame!=kEndOfChain && frame!=stopFrame) {
        if(count==kMaxChainDepth || (frame&3u) ||
           !mem.IsRangeMapped(frame,sizeof(RegistrationRecord32)))
            return UnwindResult::InvalidChain;
        for(unsigned i=0;i<count;++i) if(frames[i]==frame)return UnwindResult::InvalidChain;
        uint32_t handler=mem.Read32(frame+4);
        if(!handler || !mem.IsMapped(handler))return UnwindResult::InvalidChain;
        frames[count]=frame;
        handlers[count]=handler;
        ++count;
        frame=mem.Read32(frame);
    }
    if(frame!=stopFrame ||
       (!exitUnwind && !mem.IsRangeMapped(targetFrame,sizeof(RegistrationRecord32))))
        return UnwindResult::InvalidTarget;

    const uint32_t esp=state.ESP();
    if(esp<2048 || !mem.IsRangeMapped(esp-2048,2048))
        return UnwindResult::InvalidChain;
    const uint32_t record=(esp-2048+15u)&~15u;
    const uint32_t context=(record+sizeof(ExceptionRecord32)+15u)&~15u;
    if(!mem.IsRangeMapped(record,sizeof(ExceptionRecord32)) ||
       !mem.IsRangeMapped(context,kContextSize))
        return UnwindResult::InvalidChain;
    if(exceptionRecord) {
        if(!mem.IsRangeMapped(exceptionRecord,sizeof(ExceptionRecord32)))
            return UnwindResult::InvalidChain;
        for(uint32_t offset=0;offset<sizeof(ExceptionRecord32);offset+=4)
            mem.Write32(record+offset,mem.Read32(exceptionRecord+offset));
    } else {
        for(uint32_t offset=0;offset<sizeof(ExceptionRecord32);offset+=4)
            mem.Write32(record+offset,0);
        mem.Write32(record,0xC0000027u); // STATUS_UNWIND
        mem.Write32(record+12,mem.IsRangeMapped(esp,4)?mem.Read32(esp):state.eip);
    }
    mem.Write32(record+4,mem.Read32(record+4)|kExceptionUnwinding|
                (exitUnwind?kExceptionExitUnwind:0u));
    SaveContext(mem,context,state);
    // On x86 DispatcherContext is a pointer to a registration-record
    // pointer; a collided unwind handler can supply where unwinding resumes.
    const uint32_t dispatcherContext = context + kContextSize + 4;
    if (!mem.IsRangeMapped(dispatcherContext,4))
        return UnwindResult::InvalidChain;
    for(unsigned i=0;i<count;) {
        mem.Write32(dispatcherContext,0);
        cpu::CpuState handlerState=state;
        handlerState.ESP()=record-64;
        uint32_t disposition=CallGuestFunctionStdcall(handlerState,mem,handlers[i],
            {record,frames[i],context,dispatcherContext},teb,teb);
        if(rt::GuestFaultPending())return UnwindResult::UnsupportedDisposition;
        if(disposition==static_cast<uint32_t>(Disposition::CollidedUnwind)) {
            uint32_t replacement=mem.Read32(dispatcherContext);
            // Only frames already validated in our current unwind chain
            // are eligible. Refuse loops and backwards redirects.
            unsigned target=i+1;
            while(target<count && frames[target]!=replacement) ++target;
            if(target==count && replacement!=stopFrame)
                return UnwindResult::InvalidChain;
            if(target<=i) return UnwindResult::InvalidChain;
            mem.Write32(record+4,mem.Read32(record+4)|kExceptionCollidedUnwind);
            mem.Write32(teb,replacement);
            i=target;
            continue;
        }
        if(disposition!=static_cast<uint32_t>(Disposition::ContinueSearch))
            return UnwindResult::UnsupportedDisposition;
        ++i;
        // Once the frame's termination handler finishes, FS:[0] advances.
        mem.Write32(teb,i<count?frames[i]:stopFrame);
    }
    // RtlUnwind is non-returning for a valid target. The caller supplies a
    // continuation in its own active stack frame. This does not implement
    // longjmp to an unrelated frame (stack restoration remains TODO).
    state.ESP()=esp+20; // stdcall return address + four arguments
    state.eip=targetIp;
    state.Get(cpu::Reg32::EAX)=returnValue;
    state.lazyOp=cpu::LazyOp::None;
    return UnwindResult::Completed;
}

DispatchResult DispatchCpuFault(cpu::CpuState& state, cpu::IGuestMemory& mem,
                                uint32_t teb, const cpu::CpuException& fault) {
    // NTSTATUS exception records carry two parameters for access violations:
    // [0] is operation (0 read, 1 write), [1] is the inaccessible address.
    uint32_t params[2] = {};
    uint32_t count = 0;
    if (fault.code == cpu::EXC_ACCESS_VIOLATION) {
        params[0] = fault.write ? 1u : 0u;
        params[1] = fault.faultAddress;
        count = 2;
    }
    return Dispatch(state,mem,teb,fault.code,0,fault.eip,params,count);
}

} // namespace seh
} // namespace win32
