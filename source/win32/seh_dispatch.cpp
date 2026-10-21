// SEH registration-chain dispatcher (single guest thread, 32-bit x86).
// Calls actual guest handlers, never host C++ exception handlers.
#include "seh_dispatch.h"
#include "seh_abi.h"
#include "message_loop.h"

namespace win32 {
namespace seh {
namespace {
constexpr uint32_t kContextFlags = 0x10007u; // CONTEXT_i386 | CONTROL | INTEGER | SEGMENTS
constexpr uint32_t kContextSize = 716;
constexpr uint32_t kMaxChainDepth = 64;

// Windows x86 CONTEXT offsets; validated against winnt.h.
constexpr uint32_t kGs=140,kFs=144,kEs=148,kDs=152;
constexpr uint32_t kEdi=156,kEsi=160,kEbx=164,kEdx=168,kEcx=172,kEax=176;
constexpr uint32_t kEbp=180,kEip=184,kCs=188,kEflags=192,kEsp=196,kSs=200;

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
    m.Write32(dst+kEflags,c.eflags);
    m.Write32(dst+kEsp,c.Get(cpu::Reg32::ESP));
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
    c.eflags=m.Read32(src+kEflags);
    c.Get(cpu::Reg32::ESP)=m.Read32(src+kEsp);
    c.lazyOp=cpu::LazyOp::None;
}
} // namespace

DispatchResult Dispatch(cpu::CpuState& state, cpu::IGuestMemory& mem,
                        uint32_t teb, uint32_t code, uint32_t flags,
                        uint32_t exceptionAddress, const uint32_t* params,
                        uint32_t paramCount) {
    if (!teb || !mem.IsRangeMapped(teb,4)) return DispatchResult::InvalidChain;
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
        cpu::CpuState handlerCpu=state;
        handlerCpu.ESP()=exceptionPtr-64; // leave scratch data intact
        // A standard x86 __except_handler3/4 receives these four arguments.
        uint32_t disposition=CallGuestFunctionStdcall(handlerCpu,mem,handler,
            {exceptionPtr,frame,contextPtr,0},teb,teb);
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
} // namespace seh
} // namespace win32
