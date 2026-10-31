#pragma once
#include "guest_memory.h"
#include <cstddef>
#include <cstdint>
#include <memory>
namespace cpu {
// Single native owner with cooperative fibers;not thread-safe. Backend methods
// must not yield or reenter the store. Source IR freshness is a separate check.
// Trusted host machine code only. No guest pointer or writable alias is exported.
class CodeRegion {
public:
    virtual ~CodeRegion()=default;
    virtual size_t Capacity() const=0;
    virtual bool Initialize(const uint8_t*,size_t)=0;
    virtual bool Publish()=0;
    virtual const void* Executable() const=0;
    // One release attempt. On failure the entire region is retained for process
    // lifetime: libnx can have partially unmapped aliases and is not retry-safe.
    virtual bool Close()=0;
};
class CodeBackend {
public:
    virtual ~CodeBackend()=default;
    virtual size_t Granule() const=0;
    virtual std::unique_ptr<CodeRegion> Allocate(size_t)=0;
};
std::unique_ptr<CodeBackend> SystemCodeBackend();
struct CodeToken {uint64_t store=0,serial=0;uint32_t slot=0;explicit operator bool() const{return store && serial;}};
struct CodeLimits {size_t bytes=1024*1024,images=16;};
enum class CodeError {None,Closed,WrongOwner,InvalidInput,InvalidLimits,LocalQuota,GlobalQuota,Allocation,Initialize,Publication,Entry};
enum class CodeClose {Drained,Pending,Retained};
struct CodeStats {size_t bytes=0,images=0,published=0,retired=0,pins=0,retained=0;};
struct CodeGlobalStats {size_t bytes=0,images=0,retained=0;};
CodeGlobalStats GlobalCodeStats();
struct CodeArena;
class CodeLease {
public:
    using Consumer=bool(*)(void*,const void*,size_t);
    CodeLease()=default;
    CodeLease(const CodeLease&);
    CodeLease& operator=(const CodeLease&);
    CodeLease(CodeLease&&) noexcept;
    CodeLease& operator=(CodeLease&&) noexcept;
    ~CodeLease();
    explicit operator bool() const {return arena_!=nullptr;}
    // Entry is borrowed only inside this synchronous consumer. A private pin
    // survives consumer retirement/destruction/yield. Caller must not retain it.
    bool Invoke(const IGuestMemory&,Consumer,void*) const;
private:
    friend class CodeStore;
    CodeLease(CodeArena*,uint32_t,uint64_t);
    void Reset();
    CodeArena* arena_=nullptr;uint32_t slot_=0;uint64_t serial_=0;
};
class CodeStore {
public:
    static constexpr size_t MaxImages=32,MaxImageBytes=65536,GlobalImages=64,GlobalBytes=4*1024*1024;
    explicit CodeStore(uint64_t owner,CodeLimits limits={},std::unique_ptr<CodeBackend> backend={});
    ~CodeStore();
    CodeStore(const CodeStore&)=delete;CodeStore& operator=(const CodeStore&)=delete;
    bool Publish(const IGuestMemory&,const uint8_t*,size_t,CodeToken&);
    CodeLease Acquire(const IGuestMemory&,CodeToken);
    bool Retire(CodeToken);
    CodeClose Close();
    CodeStats Stats() const;
    CodeError LastError() const {return error_;}
private:
    uint64_t owner_=0,id_=0;CodeLimits limits_;std::unique_ptr<CodeBackend> backend_;
    CodeArena* arena_=nullptr;CodeError error_=CodeError::None;bool closed_=false;
};
} // namespace cpu
