#include "block_ir.h"
#include "interpreter.h"
#include "../runtime/workload_profile.h"
#include <algorithm>
namespace cpu::ir {
namespace {
struct Reader {
    const IGuestMemory& memory;uint32_t pc;std::array<uint8_t,15> bytes{};unsigned length=0;bool available=true;
    uint8_t Byte(){
        const uint64_t at=uint64_t(pc)+length;
        if(length>=bytes.size() || at>UINT32_MAX || !memory.IsMapped(uint32_t(at))){available=false;return 0;}
        const auto value=memory.Read8(uint32_t(at));bytes[length++]=value;return value;
    }
    uint32_t Dword(){uint32_t v=0;for(unsigned n=0;n<4;++n)v|=uint32_t(Byte())<<(8*n);return v;}
};
uint8_t Bit(unsigned reg){return uint8_t(1u<<reg);}
uint32_t ConditionFlags(uint8_t condition){
    constexpr uint32_t bits[]={FLAG_OF,FLAG_CF,FLAG_ZF,FLAG_CF|FLAG_ZF,FLAG_SF,FLAG_PF,FLAG_SF|FLAG_OF,FLAG_ZF|FLAG_SF|FLAG_OF};
    return bits[condition>>1];
}
bool Decode(Reader& r,Node& n){
    const auto op=r.Byte();n.profileOpcode=op;if(!r.available)return false;
    if(op>=0xb8 && op<=0xbf){n.op=Op::Move;n.dst=op&7;n.immediate=true;n.value=r.Dword();}
    else if(op>=0x40 && op<=0x4f){n.op=op<0x48?Op::Increment:Op::Decrement;n.dst=op&7;}
    else if(op==0x90)n.op=Op::Nop;
    else if(op>=0x91 && op<=0x97){n.op=Op::Exchange;n.src=op&7;}
    else if(op==0xeb || op==0xe9 || (op>=0x70 && op<=0x7f)){
        n.op=op>=0x70 && op<=0x7f?Op::Conditional:Op::Jump;n.kind=op&15;
        const uint32_t rel=op==0xe9?r.Dword():uint32_t(int32_t(int8_t(r.Byte())));
        n.target=r.pc+r.length+rel;
    } else if(op==0x0f){
        const auto second=r.Byte();n.profileOpcode=256+second;if(second<0x80 || second>0x8f)return false;
        n.op=Op::Conditional;n.kind=second&15;const auto rel=r.Dword();n.target=r.pc+r.length+rel;
    } else if((op<0x40 && (op&7)==5) || op==0xa9){
        n.op=op==0xa9?Op::Test:Op::Alu;n.kind=op>>3;n.immediate=true;n.value=r.Dword();
    } else {
        const bool alu=op<0x40 && ((op&7)==1 || (op&7)==3);
        if(!(alu || op==0x81 || op==0x83 || op==0x89 || op==0x8b || op==0x85 || op==0x87 || op==0xc7))return false;
        const auto modrm=r.Byte();if((modrm&0xc0)!=0xc0)return false;
        const uint8_t reg=(modrm>>3)&7,rm=modrm&7;
        if(alu){n.op=Op::Alu;n.kind=op>>3;n.dst=(op&2)?reg:rm;n.src=(op&2)?rm:reg;}
        else if(op==0x81 || op==0x83){n.op=Op::Alu;n.kind=reg;n.dst=rm;n.immediate=true;n.value=op==0x81?r.Dword():uint32_t(int32_t(int8_t(r.Byte())));}
        else if(op==0x89 || op==0x8b){n.op=Op::Move;n.dst=op==0x89?rm:reg;n.src=op==0x89?reg:rm;}
        else if(op==0x85 || op==0x87){n.op=op==0x85?Op::Test:Op::Exchange;n.dst=rm;n.src=reg;}
        else {if(reg)return false;n.op=Op::Move;n.dst=rm;n.immediate=true;n.value=r.Dword();}
    }
    if(!r.available)return false;
    n.length=uint8_t(r.length);
    switch(n.op){
        case Op::Move:n.registersWritten=Bit(n.dst);n.registersRead=n.immediate?0:Bit(n.src);break;
        case Op::Exchange:n.registersWritten=n.registersRead=Bit(n.dst)|Bit(n.src);break;
        case Op::Alu:
            n.registersRead=Bit(n.dst)|(n.immediate?0:Bit(n.src));n.registersWritten=n.kind==7?0:Bit(n.dst);
            n.flagsWrite=FLAGS_ARITH;if(n.kind==2 || n.kind==3)n.flagsRead=FLAG_CF;break;
        case Op::Test:n.registersRead=Bit(n.dst)|(n.immediate?0:Bit(n.src));n.flagsWrite=FLAGS_ARITH;break;
        case Op::Increment:case Op::Decrement:
            n.registersRead=n.registersWritten=Bit(n.dst);n.flagsRead=FLAG_CF;n.flagsWrite=FLAGS_ARITH&~FLAG_CF;break;
        case Op::Conditional:n.flagsRead=ConditionFlags(n.kind);break;
        default:break;
    }
    return true;
}
bool Condition(const CpuState& c,uint8_t cc){
    bool result=false;
    switch(cc>>1){
        case 0:result=c.OF();break;case 1:result=c.CF();break;case 2:result=c.ZF();break;
        case 3:result=c.CF() || c.ZF();break;case 4:result=c.SF();break;case 5:result=c.PF();break;
        case 6:result=c.SF()!=c.OF();break;case 7:result=c.ZF() || c.SF()!=c.OF();break;
    }
    return (cc&1)?!result:result;
}
void Run(const Node& n,CpuState& c){
    const auto a=c.regs[n.dst],b=n.immediate?n.value:c.regs[n.src];uint32_t value=0;
    c.eip+=n.length;
    switch(n.op){
        case Op::Move:c.regs[n.dst]=b;break;
        case Op::Exchange:c.regs[n.dst]=b;c.regs[n.src]=a;break;
        case Op::Increment:case Op::Decrement:
            value=n.op==Op::Increment?a+1:a-1;c.SetLazy(n.op==Op::Increment?LazyOp::Inc:LazyOp::Dec,32,a,1,value);c.regs[n.dst]=value;break;
        case Op::Test:c.SetLazy(LazyOp::Logic,32,0,0,a&b);break;
        case Op::Alu:
            switch(n.kind){
                case 0:value=a+b;c.SetLazy(LazyOp::Add,32,a,b,value);break;
                case 2:{const uint8_t carry=c.CF();value=a+b+carry;c.SetLazy(LazyOp::Adc,32,a,b,value);c.lazyCarryIn=carry;break;}
                case 3:{const uint8_t carry=c.CF();value=a-b-carry;c.SetLazy(LazyOp::Sbb,32,a,b,value);c.lazyCarryIn=carry;break;}
                case 5:case 7:value=a-b;c.SetLazy(LazyOp::Sub,32,a,b,value);break;
                default:value=n.kind==1?a|b:n.kind==4?a&b:a^b;c.SetLazy(LazyOp::Logic,32,0,0,value);break;
            }
            if(n.kind!=7)c.regs[n.dst]=value;
            break;
        case Op::Jump:c.eip=n.target;break;
        case Op::Conditional:if(Condition(c,n.kind))c.eip=n.target;break;
        case Op::Nop:break;
    }
}
}
Block Build(const IGuestMemory& m,uint32_t entry,HostBoundary host,void* context){
    Block block;block.owner_=m.Identity();block.entry_=entry;
    while(block.count_<Block::MaxInstructions){
        const uint64_t pc=uint64_t(entry)+block.bytes_;
        if(pc>UINT32_MAX){block.stop_=Stop::AddressBoundary;break;}
        if(host && host(context,uint32_t(pc))){block.stop_=Stop::HostBoundary;break;}
        Reader reader{m,uint32_t(pc)};Node node;
        if(!Decode(reader,node)){block.stop_=reader.available?Stop::Unsupported:Stop::FetchUnavailable;break;}
        if(block.bytes_+reader.length>Block::MaxBytes){block.stop_=Stop::Capacity;break;}
        node.offset=uint8_t(block.bytes_);block.nodes_[block.count_++]=node;
        std::copy_n(reader.bytes.begin(),reader.length,block.source_.begin()+block.bytes_);block.bytes_+=uint16_t(reader.length);
        if(node.op==Op::Jump || node.op==Op::Conditional){block.stop_=Stop::Branch;break;}
        block.stop_=Stop::Capacity;
    }
    return block;
}
bool Block::Matches(const IGuestMemory& m) const {
    if(owner_!=m.Identity())return false;
    for(unsigned i=0;i<bytes_;++i){const uint64_t at=uint64_t(entry_)+i;
        if(at>UINT32_MAX || !m.IsMapped(uint32_t(at)) || m.Read8(uint32_t(at))!=source_[i])return false;}
    return true;
}
Result Execute(const Block& block,IGuestMemory& m,CpuState& c,uint32_t budget,Checkpoint hook,void* context){
    Result result;
    if(!budget){result.reason=Exit::Budget;return result;}
    // Matches handles owner identity too; preserve its distinct exit for callers.
    if(!block.BelongsTo(m)){result.reason=Exit::WrongOwner;return result;}
    if(!block.Matches(m)){result.reason=Exit::CodeChanged;return result;}
    auto tail=[&](){
        if(block.Boundary()==Stop::HostBoundary)return Exit::HostBoundary;
        if(block.Boundary()==Stop::FetchUnavailable)return Exit::FetchUnavailable;
        if(block.Boundary()==Stop::Unsupported)return Exit::Unsupported;
        return Exit::Tail;
    };
    if(!block.Count()){result.reason=tail();return result;}
    size_t index=0;for(;index<block.Count();++index)if(block.Entry()+block.At(index).offset==c.eip)break;
    if(index==block.Count()){result.reason=Exit::WrongEntry;return result;}
    while(index<block.Count()){
        if(result.executed>=budget){result.reason=Exit::Budget;return result;}
        const auto& node=block.At(index);const auto pc=block.Entry()+node.offset;
        if(hook){
            if(!hook(context,c)){result.reason=Exit::Checkpoint;return result;}
            if(!block.Matches(m)){result.reason=Exit::CodeChanged;return result;}
            if(c.eip!=pc){result.reason=Exit::WrongEntry;return result;}
        }
        // No host call,yield,data write or retained source pointer in this scope.
        AccountIrInstruction(m,pc,node.profileOpcode);
        Run(node,c);++result.executed;++index;

        if(node.op==Op::Jump || node.op==Op::Conditional){result.reason=Exit::Branch;return result;}
    }
    result.reason=tail();return result;
}
} // namespace cpu::ir
