#include "native_cache.h"
#include <new>
#include <cerrno>
namespace cpu {
namespace {
struct Probe {
    uint16_t mapped=0;std::array<uint8_t,16> bytes{};
    static Probe Read(const IGuestMemory& memory,uint32_t pc){Probe p;
        for(unsigned n=0;n<p.bytes.size();++n){const uint64_t at=uint64_t(pc)+n;
            if(at<=UINT32_MAX && memory.IsMapped(uint32_t(at))){p.mapped|=uint16_t(1u<<n);p.bytes[n]=memory.Read8(uint32_t(at));}}
        return p;
    }
    bool operator==(const Probe& other)const{return mapped==other.mapped && bytes==other.bytes;}
};
size_t Index(const ir::Block& block,uint32_t pc){
    if(pc<block.Entry() || uint64_t(pc)>=uint64_t(block.Entry())+block.SourceSize())return block.Count();
    for(size_t n=0;n<block.Count();++n)if(block.Entry()+block.At(n).offset==pc)return n;
    return block.Count();
}
}
struct NativeCache::State {
    struct Entry {bool used=false,negative=false;uint32_t pc=0,visits=0;uint64_t age=0;Probe probe;arm64::Translation translated;};
    std::array<Entry,Entries> entries;
    uint64_t clock=0;
};
NativeCache::NativeCache(IGuestMemory& memory,CodeStore& storage):memory_(memory),storage_(storage){bound_=memory_.BindNative(this,&Dispatch);}
NativeCache::~NativeCache(){Close();}
void NativeCache::Retire(size_t index){auto& entry=state_->entries[index];if(entry.translated)storage_.Retire(entry.translated.Token());entry=State::Entry{};}
void NativeCache::Close(){
    if(closed_)return;
    closed_=true;if(bound_)memory_.UnbindNative(this);
    if(state_)for(size_t i=0;i<Entries;++i)Retire(i);
    state_.reset();bound_=false;
}
void NativeCache::Disable(){
    memory_.NativeStats().disabled=true;++memory_.NativeStats().failures;
    if(state_)for(size_t i=0;i<Entries;++i)Retire(i);
}
bool NativeCache::Dispatch(void* self,CpuState& cpu,uint32_t budget,uint32_t& count,uint8_t& length,uint8_t& opcode){return static_cast<NativeCache*>(self)->Step(cpu,budget,count,length,opcode);}
bool NativeCache::Step(CpuState& cpu,uint32_t budget,uint32_t& count,uint8_t& length,uint8_t& opcode){
    count=0;if(!budget)return false;
    // Allocation/JIT syscalls are implementation work,not guest CRT errno.
    struct ErrnoScope {int saved=errno;~ErrnoScope(){errno=saved;}} savedErrno;
    auto& stats=memory_.NativeStats();++stats.requests;
    auto fallback=[&](){++stats.fallbacks;return false;};
    if(closed_ || !bound_ || stats.disabled || !IGuestMemory::NativeArchitectureAvailable())return fallback();
    if(!state_){state_.reset(new(std::nothrow) State);if(!state_){Disable();return fallback();}}
    auto& state=*state_;
    // No guest callbacks/yields occur in this scope. Entry references cannot be
    // evicted while an emitted prefix is active. G8 validates source before use.
    auto run=[&](State::Entry& entry,size_t node,bool hit){
        const auto& n=entry.translated.Source().At(node);const uint8_t len=n.length;
        const uint8_t op=n.profileOpcode>=256?0x0f:uint8_t(n.profileOpcode);
        const auto result=arm64::Execute(entry.translated,storage_,memory_,cpu,budget);
        if(result.status==arm64::Status::Complete && result.execution.executed>0){
            entry.age=++state.clock;if(hit)++stats.hits;length=len;opcode=op;count=result.execution.executed;
            if(count>1){++stats.batchCalls;stats.batchInstructions+=count;}
            if(count>stats.maxBatch)stats.maxBatch=count;
            return true;
        }
        if(result.status==arm64::Status::Complete && result.execution.reason==ir::Exit::MemoryRefusal)return false;
        if(result.status==arm64::Status::Complete && result.execution.reason==ir::Exit::CodeChanged){
            ++stats.invalidations;const auto i=size_t(&entry-state.entries.data());Retire(i);
        }else Disable();
        return false;
    };
    for(auto& entry:state.entries)if(entry.translated){
        const auto node=Index(entry.translated.Source(),cpu.eip);
        if(node<entry.translated.Source().Count()){if(run(entry,node,true))return true;return fallback();}
    }
    ++stats.misses;
    size_t index=Entries;
    for(size_t n=0;n<Entries;++n)if(state.entries[n].used && state.entries[n].pc==cpu.eip){index=n;break;}
    if(index==Entries){
        for(size_t n=0;n<Entries;++n)if(!state.entries[n].used){index=n;break;}
        if(index==Entries){
            // Cold/negative admission must not constantly displace hot code.
            // If every slot is compiled,the oldest publication is retired.
            index=0;for(size_t n=1;n<Entries;++n){
                const bool candidate=bool(state.entries[n].translated),chosen=bool(state.entries[index].translated);
                if((chosen && !candidate) || (candidate==chosen && state.entries[n].age<state.entries[index].age))index=n;
            }
            ++stats.evictions;Retire(index);
        }
        auto& entry=state.entries[index];entry.used=true;entry.pc=cpu.eip;entry.probe=Probe::Read(memory_,cpu.eip);
    }
    auto& entry=state.entries[index];entry.age=++state.clock;
    const auto probe=Probe::Read(memory_,cpu.eip);
    if(!(probe==entry.probe)){++stats.invalidations;entry.probe=probe;entry.negative=false;entry.visits=0;}
    if(entry.negative){++stats.negativeHits;return fallback();}
    if(++entry.visits<HotVisits)return fallback();
    const auto block=ir::Build(memory_,cpu.eip);
    if(!block.Count()){entry.negative=true;return fallback();}
    if(!arm64::Compile(storage_,memory_,block,entry.translated)){Disable();return fallback();}
    ++stats.compiles;
    if(run(entry,0,false))return true;
    return fallback();
}
} // namespace cpu
