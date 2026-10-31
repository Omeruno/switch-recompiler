#include "code_storage.h"
#include <array>
#include <cstdlib>
#include <atomic>
#include <new>
#include <utility>
namespace cpu {
namespace {
// Sole native code-execution owner,including its cooperative fibers. The vault
// and global quota span all sessions; persistent failures cannot grow unbounded.
struct Global {
    CodeGlobalStats stats;
    std::array<std::unique_ptr<CodeRegion>,CodeStore::GlobalImages> retained;
};
Global& Globals(){static Global* global=new Global;return *global;}
uint64_t StoreId(){static std::atomic<uint64_t> next{1};return next.fetch_add(1,std::memory_order_relaxed);}
bool Reserve(size_t bytes){auto& s=Globals().stats;
    if(s.images>=CodeStore::GlobalImages || bytes>CodeStore::GlobalBytes-s.bytes)return false;
    ++s.images;s.bytes+=bytes;return true;
}
void Release(size_t bytes){auto& s=Globals().stats;--s.images;s.bytes-=bytes;}
void Retain(std::unique_ptr<CodeRegion> region){
    auto& g=Globals();for(auto& slot:g.retained)if(!slot){slot=std::move(region);return;}
    // Global reservation accounting makes this unreachable; never free a failed
    // region merely to hide inconsistent accounting.
    std::abort();
}
}
struct CodeArena {
    struct Slot {std::unique_ptr<CodeRegion> region;uint64_t serial=0,refs=0;size_t bytes=0,used=0;bool retired=false,published=false,failed=false;};
    std::array<Slot,CodeStore::MaxImages> slots;
    std::unique_ptr<CodeBackend> backend;
    uint64_t owner=0,store=0,nextSerial=1,strong=1;
    explicit CodeArena(std::unique_ptr<CodeBackend> b,uint64_t o,uint64_t s):backend(std::move(b)),owner(o),store(s){}
    bool Valid(uint32_t index,uint64_t serial)const{return index<slots.size() && slots[index].region && slots[index].serial==serial;}
    void Collect(unsigned i){auto& s=slots[i];if(!s.region || s.refs || !s.retired || s.failed)return;
        if(!s.region->Close()){s.failed=true;++Globals().stats.retained;return;}
        Release(s.bytes);s=Slot{};
    }
    void Unpin(uint32_t index,uint64_t serial){if(Valid(index,serial)){auto& s=slots[index];--s.refs;Collect(index);}}
    void Drop(){if(--strong)return;
        for(unsigned i=0;i<slots.size();++i){auto& s=slots[i];if(!s.region)continue;
            Collect(i);if(s.region)Retain(std::move(s.region));}
        delete this;
    }
    CodeStats Stats()const{CodeStats out;
        for(const auto& s:slots)if(s.region){out.bytes+=s.bytes;++out.images;if(s.retired)++out.retired;else if(s.published)++out.published;
            out.pins+=size_t(s.refs-(s.retired?0:1));if(s.failed)++out.retained;}
        return out;
    }
};
CodeGlobalStats GlobalCodeStats(){return Globals().stats;}
CodeStore::CodeStore(uint64_t owner,CodeLimits limits,std::unique_ptr<CodeBackend> backend)
    :owner_(owner),id_(StoreId()),limits_(limits),backend_(std::move(backend)){}
CodeStore::~CodeStore(){Close();if(arena_)arena_->Drop();}
bool CodeStore::Publish(const IGuestMemory& memory,const uint8_t* bytes,size_t size,CodeToken& out){
    out={};error_=CodeError::None;
    auto fail=[&](CodeError error){error_=error;return false;};
    if(closed_)return fail(CodeError::Closed);
    if(memory.Identity()!=owner_)return fail(CodeError::WrongOwner);
    if(!bytes || !size || size>MaxImageBytes || (size&3))return fail(CodeError::InvalidInput);
    if(!owner_ || !id_ || !limits_.images || limits_.images>MaxImages || !limits_.bytes || limits_.bytes>GlobalBytes)return fail(CodeError::InvalidLimits);
    if(!arena_){if(!backend_)backend_=SystemCodeBackend();if(!backend_)return fail(CodeError::Allocation);
        arena_=new(std::nothrow) CodeArena(std::move(backend_),owner_,id_);if(!arena_)return fail(CodeError::Allocation);}
    auto& a=*arena_;const auto page=a.backend->Granule();
    if(page<4096 || (page&(page-1)) || page>MaxImageBytes)return fail(CodeError::InvalidLimits);
    const auto rounded=(size+page-1)&~(page-1);const auto current=a.Stats();
    if(current.images>=limits_.images || rounded>limits_.bytes-current.bytes)return fail(CodeError::LocalQuota);
    unsigned index=0;while(index<a.slots.size() && a.slots[index].region)++index;
    if(index==a.slots.size() || !a.nextSerial)return fail(CodeError::LocalQuota);
    if(!Reserve(rounded))return fail(CodeError::GlobalQuota);
    auto region=a.backend->Allocate(rounded);
    if(!region){Release(rounded);return fail(CodeError::Allocation);}
    auto& slot=a.slots[index];slot.region=std::move(region);slot.serial=a.nextSerial++;slot.bytes=rounded;slot.used=size;slot.retired=true;
    auto refuse=[&](CodeError error){a.Collect(index);return fail(error);};
    if(slot.region->Capacity()!=rounded)return refuse(CodeError::Allocation);
    if(!slot.region->Initialize(bytes,size))return refuse(CodeError::Initialize);
    if(!slot.region->Publish())return refuse(CodeError::Publication);
    const auto entry=reinterpret_cast<uintptr_t>(slot.region->Executable());
    if(!entry || (entry&3))return refuse(CodeError::Entry);
    slot.refs=1;slot.retired=false;slot.published=true;out={id_,slot.serial,index};return true;
}
CodeLease CodeStore::Acquire(const IGuestMemory& memory,CodeToken token){
    if(closed_ || memory.Identity()!=owner_ || token.store!=id_ || !arena_ || !arena_->Valid(token.slot,token.serial))return {};
    const auto& slot=arena_->slots[token.slot];if(slot.retired || !slot.published)return {};
    return CodeLease(arena_,token.slot,token.serial);
}
bool CodeStore::Retire(CodeToken token){
    if(token.store!=id_ || !arena_ || !arena_->Valid(token.slot,token.serial))return false;
    auto& slot=arena_->slots[token.slot];if(slot.retired)return false;slot.retired=true;arena_->Unpin(token.slot,token.serial);return true;
}
CodeClose CodeStore::Close(){
    closed_=true;if(!arena_)return CodeClose::Drained;
    for(unsigned i=0;i<arena_->slots.size();++i){auto& s=arena_->slots[i];if(s.region && !s.retired){s.retired=true;arena_->Unpin(i,s.serial);}}
    const auto stats=arena_->Stats();return stats.retained?CodeClose::Retained:stats.images?CodeClose::Pending:CodeClose::Drained;
}
CodeStats CodeStore::Stats()const{return arena_?arena_->Stats():CodeStats{};}
CodeLease::CodeLease(CodeArena* arena,uint32_t slot,uint64_t serial):arena_(arena),slot_(slot),serial_(serial){++arena_->strong;++arena_->slots[slot_].refs;}
CodeLease::CodeLease(const CodeLease& other):arena_(other.arena_),slot_(other.slot_),serial_(other.serial_){if(arena_){++arena_->strong;++arena_->slots[slot_].refs;}}
CodeLease& CodeLease::operator=(const CodeLease& other){if(this!=&other){CodeLease copy(other);*this=std::move(copy);}return *this;}
CodeLease::CodeLease(CodeLease&& other)noexcept:arena_(other.arena_),slot_(other.slot_),serial_(other.serial_){other.arena_=nullptr;}
CodeLease& CodeLease::operator=(CodeLease&& other)noexcept{if(this!=&other){Reset();arena_=other.arena_;slot_=other.slot_;serial_=other.serial_;other.arena_=nullptr;}return *this;}
CodeLease::~CodeLease(){Reset();}
void CodeLease::Reset(){if(arena_){auto* a=arena_;arena_=nullptr;a->Unpin(slot_,serial_);a->Drop();}}
bool CodeLease::Invoke(const IGuestMemory& memory,Consumer consumer,void* context)const{
    if(!arena_ || !consumer || arena_->owner!=memory.Identity() || !arena_->Valid(slot_,serial_))return false;
    const auto& slot=arena_->slots[slot_];if(slot.retired || !slot.published)return false;
    CodeLease pin(*this);const auto* entry=slot.region->Executable();const auto bytes=slot.used;
    return consumer(context,entry,bytes);
}
} // namespace cpu
