#include "arm64_emitter.h"
#include "interpreter.h"
#include <algorithm>
#include <cstddef>
#include <type_traits>
namespace cpu::arm64 {
namespace {
// Pure flag queries only: no guest callback,yield,memory access or IR/Step
// replay. Keeping the incoming lazy representation avoids a materialize/restore
// conversion that would change the exact CpuState contract.
uint32_t Carry(const CpuState* cpu){return cpu->CF();}
uint32_t Flags(const CpuState* cpu){return cpu->GetEflags();}
constexpr unsigned State=19,Remaining=20;
constexpr size_t Reg(unsigned r){return offsetof(CpuState,regs)+r*4;}
[[maybe_unused]] ir::Exit Tail(const ir::Block& b){
    if(b.Boundary()==ir::Stop::HostBoundary)return ir::Exit::HostBoundary;
    if(b.Boundary()==ir::Stop::FetchUnavailable)return ir::Exit::FetchUnavailable;
    if(b.Boundary()==ir::Stop::Unsupported)return ir::Exit::Unsupported;
    return ir::Exit::Tail;
}
}
class Emitter {
    Image image_;
    bool valid_=true;
    std::array<size_t,ir::Block::MaxInstructions> dispatch_{};
    std::array<size_t,ir::Block::MaxInstructions+2> exits_{};
    size_t exitCount_=0;
    size_t Here()const{return image_.count_;}
    void Word(uint32_t word){if(Here()==Image::MaxWords){valid_=false;return;}image_.words_[image_.count_++]=word;}
    void Move(unsigned dst,unsigned src,bool wide=false){Word((wide?0xaa0003e0u:0x2a0003e0u)|(src<<16)|dst);}
    void Constant(unsigned dst,uint32_t value){Word(0x52800000u|((value&0xffff)<<5)|dst);Word(0x72a00000u|((value>>16)<<5)|dst);}
    void Load(unsigned dst,size_t offset){assert(!(offset&3) && offset/4<4096);Word(0xb9400000u|(uint32_t(offset/4)<<10)|(State<<5)|dst);}
    void Store(unsigned src,size_t offset){assert(!(offset&3) && offset/4<4096);Word(0xb9000000u|(uint32_t(offset/4)<<10)|(State<<5)|src);}
    void Byte(unsigned src,size_t offset){assert(offset<4096);Word(0x39000000u|(uint32_t(offset)<<10)|(State<<5)|src);}
    void Binary(uint32_t opcode,unsigned dst,unsigned a,unsigned b){Word(opcode|(b<<16)|(a<<5)|dst);}
    void Shift(unsigned dst,unsigned src,unsigned bits){Word(0x53007c00u|(bits<<16)|(src<<5)|dst);} // LSR W
    void Patch(size_t at,size_t target,bool conditional){
        if(at>=Here() || at>=Image::MaxWords){valid_=false;return;}
        const int64_t delta=int64_t(target)-int64_t(at);
        const int64_t bound=conditional?(1<<18):(1<<25);
        if(delta < -bound || delta>=bound){valid_=false;return;}
        image_.words_[at]|=conditional?(uint32_t(delta)&0x7ffffu)<<5:uint32_t(delta)&0x3ffffffu;
    }
    void Exit(){assert(exitCount_<exits_.size());exits_[exitCount_++]=Here();Word(0x14000000u);}
    void Call(uintptr_t address){
        Move(0,State,true);
        Word(0xd2800010u|(uint32_t(address&0xffff)<<5)); // MOVZ X16
        for(unsigned n=1;n<4;++n)Word(0xf2800010u|(n<<21)|(uint32_t((uint64_t(address)>>(16*n))&0xffff)<<5));
        Word(0xd63f0200u); // BLR X16
    }
    void Lazy(LazyOp op,bool logic,bool carry){
        Constant(3,uint32_t(op));Byte(3,offsetof(CpuState,lazyOp));
        Constant(3,32);Byte(3,offsetof(CpuState,lazyWidth));
        if(carry)Byte(4,offsetof(CpuState,lazyCarryIn));
        Store(logic?31:0,offsetof(CpuState,lazyA));Store(logic?31:1,offsetof(CpuState,lazyB));Store(2,offsetof(CpuState,lazyRes));
    }
    void Node(const ir::Node& n,uint32_t pc){
        Constant(3,pc+n.length);Store(3,offsetof(CpuState,eip));
        if(n.op==ir::Op::Jump){Constant(3,n.target);Store(3,offsetof(CpuState,eip));return;}
        if(n.op==ir::Op::Conditional){
            Call(reinterpret_cast<uintptr_t>(&Flags));
            const unsigned group=n.kind>>1;
            if(group<6){constexpr uint32_t masks[]={FLAG_OF,FLAG_CF,FLAG_ZF,FLAG_CF|FLAG_ZF,FLAG_SF,FLAG_PF};
                Constant(2,masks[group]);Binary(0x0a000000,2,0,2);
            }else{
                Shift(2,0,7);Shift(3,0,11);Binary(0x4a000000,2,2,3);
                if(group==7){Shift(3,0,6);Binary(0x2a000000,2,2,3);}
                Constant(3,1);Binary(0x0a000000,2,2,3);
            }
            Constant(3,n.target);Constant(4,pc+n.length);
            Word(0x7100005fu); // CMP W2,#0
            Word(0x1a840063u|((n.kind&1)?0u:1u)<<12); // CSEL W3,W3,W4,EQ/NE
            Store(3,offsetof(CpuState,eip));return;
        }
        if(n.op==ir::Op::Nop)return;
        if(n.op==ir::Op::Address){
            Constant(0,n.value);
            if(n.base<8){Load(1,Reg(n.base));Binary(0x0b000000,0,0,1);}
            if(n.index<8){Load(1,Reg(n.index));Word(0x0b010000u|(uint32_t(n.scale)<<10));} // ADD W0,W0,W1,LSL #scale
            Store(0,Reg(n.dst));return;
        }
        const bool increment=n.op==ir::Op::Increment || n.op==ir::Op::Decrement;
        const bool carry=n.op==ir::Op::Alu && (n.kind==2 || n.kind==3);
        if(increment || carry){Call(reinterpret_cast<uintptr_t>(&Carry));Move(4,0);}
        Load(0,Reg(n.dst));
        if(increment)Constant(1,1);else if(n.immediate)Constant(1,n.value);else Load(1,Reg(n.src));
        if(n.op==ir::Op::Move){Store(1,Reg(n.dst));return;}
        if(n.op==ir::Op::Exchange){Store(1,Reg(n.dst));Store(0,Reg(n.src));return;}
        if(increment){
            Binary(n.op==ir::Op::Increment?0x0b000000:0x4b000000,2,0,1);
            Load(3,offsetof(CpuState,eflags));Constant(5,~FLAGS_ARITH);
            Binary(0x0a000000,3,3,5);Binary(0x2a000000,3,3,4);Store(3,offsetof(CpuState,eflags));
            Lazy(n.op==ir::Op::Increment?LazyOp::Inc:LazyOp::Dec,false,false);Store(2,Reg(n.dst));return;
        }
        if(n.op==ir::Op::Test){Binary(0x0a000000,2,0,1);Lazy(LazyOp::Logic,true,false);return;}
        assert(n.op==ir::Op::Alu);
        const uint32_t operations[]={0x0b000000,0x2a000000,0x0b000000,0x4b000000,0x0a000000,0x4b000000,0x4a000000,0x4b000000};
        Binary(operations[n.kind],2,0,1);
        if(carry)Binary(n.kind==2?0x0b000000:0x4b000000,2,2,4);
        const LazyOp kinds[]={LazyOp::Add,LazyOp::Logic,LazyOp::Adc,LazyOp::Sbb,LazyOp::Logic,LazyOp::Sub,LazyOp::Logic,LazyOp::Sub};
        Lazy(kinds[n.kind],kinds[n.kind]==LazyOp::Logic,carry);
        if(n.kind!=7)Store(2,Reg(n.dst));
    }
public:
    Image Run(const ir::Block& block){
        static_assert(std::is_standard_layout<CpuState>::value,"native field offsets require standard layout");
        if(!block.Count())return {};
        // void entry(CpuState* X0,uint32_t first W1,uint32_t count W2).
        // Preserve x19/x20,x29/LR and16-byte stack alignment across flag helpers.
        Word(0xa9be53f3u);Word(0xa9017bfdu);Move(State,0,true);Move(Remaining,2);
        exits_[exitCount_++]=Here();Word(0x34000014u); // CBZ W20,epilogue (imm19)
        for(size_t i=0;i<block.Count();++i){Word(0x7100003fu|(uint32_t(i)<<10));dispatch_[i]=Here();Word(0x54000000u);}
        Exit();
        for(size_t i=0;i<block.Count();++i){
            Patch(dispatch_[i],Here(),true);const auto& node=block.At(i);Node(node,block.Entry()+node.offset);
            if(node.op==ir::Op::Jump || node.op==ir::Op::Conditional){Exit();break;}
            Word(0x71000694u); // SUBS W20,W20,#1
            assert(exitCount_<exits_.size());exits_[exitCount_++]=Here();Word(0x54000000u); // B.EQ epilogue
        }
        const auto end=Here();
        for(size_t i=0;i<exitCount_;++i){const auto at=exits_[i];Patch(at,end,(image_.words_[at]&0x7c000000u)!=0x14000000u);}
        Word(0xa9417bfdu);Word(0xa8c253f3u);Word(0xd65f03c0u);
        if(!valid_)return {};
        return image_;
    }
};
Image Emit(const ir::Block& block){return Emitter{}.Run(block);}
bool Compile(CodeStore& store,const IGuestMemory& memory,const ir::Block& block,Translation& out){
    static_assert(Image::MaxWords*4<=CodeStore::MaxImageBytes,"emitted image must fit storage");
    out={};if(!block.Count() || !block.Matches(memory))return false;
    const auto image=Emit(block);if(!image)return false;
    CodeToken token;if(!store.Publish(memory,reinterpret_cast<const uint8_t*>(image.Words()),image.Bytes(),token))return false;
    out.source_=block;out.token_=token;return true;
}
Result Execute(const Translation& translated,CodeStore& store,IGuestMemory& memory,CpuState& cpu,uint32_t budget,ir::Checkpoint hook,void* context){
    Result result;auto& execution=result.execution;
    if(!budget){execution.reason=ir::Exit::Budget;return result;}
    if(!translated){result.status=Status::Unavailable;return result;}
    // Own the source before callbacks can destroy the caller's translation.
    const auto block=translated.Source();
    if(!block.BelongsTo(memory)){execution.reason=ir::Exit::WrongOwner;return result;}
    if(!block.Matches(memory)){execution.reason=ir::Exit::CodeChanged;return result;}
    if(!block.Count()){result.status=Status::Unavailable;return result;}
    size_t index=0;while(index<block.Count() && block.Entry()+block.At(index).offset!=cpu.eip)++index;
    if(index==block.Count()){execution.reason=ir::Exit::WrongEntry;return result;}
    auto lease=store.Acquire(memory,translated.Token());
    if(!lease){result.status=Status::Unavailable;return result;}
#if !defined(__aarch64__)
    (void)hook;(void)context;
    result.status=Status::ArchitectureUnavailable;return result;
#else
    struct Call {
        CpuState* cpu;uint32_t first,count;
        static bool Run(void* context,const void* entry,size_t){auto& c=*static_cast<Call*>(context);
            using Function=void(*)(CpuState*,uint32_t,uint32_t);
            reinterpret_cast<Function>(const_cast<void*>(entry))(c.cpu,c.first,c.count);return true;
        }
    };
    while(index<block.Count()){
        if(execution.executed==budget){execution.reason=ir::Exit::Budget;return result;}
        const auto pc=block.Entry()+block.At(index).offset;
        if(hook){
            if(!hook(context,cpu)){execution.reason=ir::Exit::Checkpoint;return result;}
            if(!block.Matches(memory)){execution.reason=ir::Exit::CodeChanged;return result;}
            if(cpu.eip!=pc){execution.reason=ir::Exit::WrongEntry;return result;}
        }
        const auto count=hook?1u:std::min<uint32_t>(budget-execution.executed,uint32_t(block.Count()-index));
        Call call{&cpu,uint32_t(index),count};
        // Supported native operations/flag queries do not yield or touch guest
        // memory. Account the known prefix only after a successful invocation.
        if(!lease.Invoke(memory,&Call::Run,&call)){result.status=Status::Unavailable;return result;}
        for(unsigned n=0;n<count;++n){const auto& node=block.At(index+n);AccountNativeInstruction(memory,block.Entry()+node.offset,node.profileOpcode);}
        const auto last=block.At(index+count-1).op;index+=count;execution.executed+=count;
        if(last==ir::Op::Jump || last==ir::Op::Conditional){execution.reason=ir::Exit::Branch;return result;}
    }
    execution.reason=Tail(block);return result;
#endif
}
} // namespace cpu::arm64
