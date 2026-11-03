#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace rt {
enum class ExecutionEngine : uint8_t {FastRegister,FastMapped,Reference,PortableIr,Native,Unclassified,Count};
// Optional,owning bounded counters. The active profile borrows this host object
// only during an explicit scope; no guest addresses or memory-access inference.
struct EngineCensus {
    static constexpr size_t OpcodeSlots=513,EngineSlots=size_t(ExecutionEngine::Count);
    using Histogram=std::array<uint64_t,OpcodeSlots>;
    std::array<Histogram,EngineSlots> opcodes{};
    std::array<uint64_t,EngineSlots> totals{};
    uint64_t instructionCount=0;
    static const char* Name(size_t engine){
        constexpr const char* names[]={"fast-register","fast-mapped-lea-stack","reference","portable-ir","native-arm64","unclassified"};
        return names[engine<EngineSlots?engine:size_t(ExecutionEngine::Unclassified)];
    }
    void Instruction(size_t opcode,ExecutionEngine engine){
        const auto index=size_t(engine)<EngineSlots?size_t(engine):size_t(ExecutionEngine::Unclassified);
        ++instructionCount;++totals[index];++opcodes[index][opcode<OpcodeSlots?opcode:OpcodeSlots-1];
    }
};
static_assert(sizeof(EngineCensus)<=32*1024,"optional engine census must stay within its fixed storage budget");
// Counts work, not time. No guest addresses, allocation, clocks or I/O.
// One explicitly scoped native execution thread, including its guest fibers.
struct WorkloadProfile {
    static constexpr size_t OpcodeSlots = EngineCensus::OpcodeSlots; // 00..FF, 0F00..0FFF, fetch failure
    static constexpr size_t HostSlots = 4096;
    static constexpr uint64_t DefaultLimit = 1000000;
    std::array<uint64_t, OpcodeSlots> opcodes{};
    std::array<uint64_t, HostSlots> hosts{};
    uint64_t instructionCount=0, hostCount=0, otherHosts=0;
    uint64_t instructionLimit=DefaultLimit, hostLimit=DefaultLimit;
    bool instructionsTruncated=false, hostsTruncated=false;
    EngineCensus* engines=nullptr; // Optional host-only owner;restore before copying a scoped profile.
    void Instruction(size_t opcode,ExecutionEngine engine=ExecutionEngine::Unclassified) {
        if (instructionCount >= instructionLimit) {instructionsTruncated=true;return;}
        ++instructionCount;
        ++opcodes[opcode < OpcodeSlots ? opcode : OpcodeSlots-1];
        if(engines)engines->Instruction(opcode,engine);
    }
    void Host(size_t index) {
        if (hostCount >= hostLimit) {hostsTruncated=true;return;}
        ++hostCount;
        if (index < HostSlots) ++hosts[index]; else ++otherHosts;
    }
};
class EngineCensusScope {
public:
    EngineCensusScope(WorkloadProfile& profile,EngineCensus* census):profile_(profile),previous_(profile.engines){profile_.engines=census;}
    EngineCensusScope(WorkloadProfile& profile,EngineCensus& census):EngineCensusScope(profile,&census){}
    ~EngineCensusScope(){profile_.engines=previous_;}
    EngineCensusScope(const EngineCensusScope&)=delete;
    EngineCensusScope& operator=(const EngineCensusScope&)=delete;
private:
    WorkloadProfile& profile_;EngineCensus* previous_;
};
inline thread_local WorkloadProfile* g_workloadProfile=nullptr;
class WorkloadProfileScope {
public:
    explicit WorkloadProfileScope(WorkloadProfile* profile)
        : previous_(g_workloadProfile) {g_workloadProfile=profile;}
    ~WorkloadProfileScope() {g_workloadProfile=previous_;}
    WorkloadProfileScope(const WorkloadProfileScope&)=delete;
    WorkloadProfileScope& operator=(const WorkloadProfileScope&)=delete;
private:
    WorkloadProfile* previous_;
};
} // namespace rt
