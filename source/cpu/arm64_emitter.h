#pragma once
#include "block_ir.h"
#include "code_storage.h"
namespace cpu::arm64 {
// Process-local trusted host code,not a serialized cache or guest instruction
// buffer. Immutable public access;all arithmetic is emitted,never replayed IR.
class Image {
public:
    static constexpr size_t MaxWords=4096;
    const uint32_t* Words() const {return words_.data();}
    size_t Bytes() const {return count_*sizeof(uint32_t);}
    explicit operator bool() const {return count_!=0;}
private:
    friend class Emitter;
    std::array<uint32_t,MaxWords> words_{};
    size_t count_=0;
};
Image Emit(const ir::Block&);
class Translation {
public:
    explicit operator bool() const {return bool(token_);}
    CodeToken Token() const {return token_;} // Owner may explicitly retire it.
    const ir::Block& Source() const {return source_;}
private:
    friend bool Compile(CodeStore&,const IGuestMemory&,const ir::Block&,Translation&);
    ir::Block source_;
    CodeToken token_;
};
// Failure clears output,without changing existing publications or guest state.
// Old tokens remain owned by the store until explicit retirement/Close.
bool Compile(CodeStore&,const IGuestMemory&,const ir::Block&,Translation&);
enum class Status {Complete,Unavailable,ArchitectureUnavailable};
struct Result {Status status=Status::Complete;ir::Result execution;};
// No automatic reference replay. Unavailable exits preserve the executed prefix;
// caller falls back at the current EIP. Checkpoint can yield/retire/destroy store;
// memory must remain alive. Source is copied before any callback. Sole native
// owner only. Without checkpoints,the native prefix is one synchronous call.
// ABI v2 returns the dynamic committed count;checked read refusal commits no
// destination/EIP/fault/count for that node and leaves interpreter fault handling.
Result Execute(const Translation&,CodeStore&,IGuestMemory&,CpuState&,uint32_t budget,
               ir::Checkpoint hook=nullptr,void* context=nullptr);
} // namespace cpu::arm64
