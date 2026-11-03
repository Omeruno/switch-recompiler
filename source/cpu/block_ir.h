#pragma once
#include "cpu_state.h"
#include "guest_memory.h"
#include <array>
#include <cstddef>
#include <cassert>
namespace cpu::ir {
// Bounded semantic frontend. No executable allocation or borrowed code pointers.
enum class Op : uint8_t {Nop,Move,Exchange,Alu,Increment,Decrement,Test,Jump,Conditional,Address};
enum class Stop : uint8_t {Unsupported,FetchUnavailable,HostBoundary,Capacity,Branch,AddressBoundary};
struct Node {
    Op op=Op::Nop;
    uint8_t dst=0,src=0,kind=0,length=0,offset=0;
    bool immediate=false;
    uint32_t value=0,target=0,flagsRead=0,flagsWrite=0;
    uint16_t profileOpcode=0;
    uint8_t registersRead=0,registersWritten=0;
    // LEA is pure uint32 address arithmetic,not a guest data access.8=no register.
    uint8_t base=8,index=8,scale=0;
};
class Block {
public:
    static constexpr size_t MaxInstructions=32,MaxBytes=192;
    static_assert(MaxInstructions<=UINT8_MAX && MaxBytes<=UINT8_MAX,"IR offsets/counts must fit their owning representation");
    size_t Count() const {return count_;}
    size_t SourceSize() const {return bytes_;}
    uint32_t Entry() const {return entry_;}
    uint32_t End() const {return entry_+uint32_t(bytes_);}
    Stop Boundary() const {return stop_;}
    const Node& At(size_t index) const {assert(index<count_);return nodes_[index];}
    bool BelongsTo(const IGuestMemory& memory) const {return owner_==memory.Identity();}
    bool Matches(const IGuestMemory& memory) const;
private:
    friend Block Build(const IGuestMemory&,uint32_t,bool(*)(void*,uint32_t),void*);
    std::array<Node,MaxInstructions> nodes_{};
    std::array<uint8_t,MaxBytes> source_{};
    uint64_t owner_=0;
    uint32_t entry_=0;
    uint16_t bytes_=0;
    uint8_t count_=0;
    Stop stop_=Stop::FetchUnavailable;
};
using HostBoundary=bool(*)(void*,uint32_t);
Block Build(const IGuestMemory& memory,uint32_t entry,HostBoundary host=nullptr,void* context=nullptr);
enum class Exit : uint8_t {Tail,Branch,Budget,Unsupported,HostBoundary,FetchUnavailable,CodeChanged,WrongOwner,WrongEntry,Checkpoint};
struct Result {Exit reason=Exit::Unsupported;uint32_t executed=0;};
// Hook runs before each instruction. It may park/yield or mutate source bytes;
// owner/source/EIP are checked again after it returns. false executes no instruction.
using Checkpoint=bool(*)(void*,const CpuState&);
Result Execute(const Block&,IGuestMemory&,CpuState&,uint32_t budget,Checkpoint hook=nullptr,void* context=nullptr);
} // namespace cpu::ir
