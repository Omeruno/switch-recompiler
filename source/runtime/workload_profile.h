#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace rt {
// Counts work, not time. No guest addresses, allocation, clocks or I/O.
// One explicitly scoped native execution thread, including its guest fibers.
struct WorkloadProfile {
    static constexpr size_t OpcodeSlots = 513; // 00..FF, 0F00..0FFF, fetch failure
    static constexpr size_t HostSlots = 4096;
    static constexpr uint64_t DefaultLimit = 1000000;
    std::array<uint64_t, OpcodeSlots> opcodes{};
    std::array<uint64_t, HostSlots> hosts{};
    uint64_t instructionCount=0, hostCount=0, otherHosts=0;
    uint64_t instructionLimit=DefaultLimit, hostLimit=DefaultLimit;
    bool instructionsTruncated=false, hostsTruncated=false;
    void Instruction(size_t opcode) {
        if (instructionCount >= instructionLimit) {instructionsTruncated=true;return;}
        ++instructionCount;
        ++opcodes[opcode < OpcodeSlots ? opcode : OpcodeSlots-1];
    }
    void Host(size_t index) {
        if (hostCount >= hostLimit) {hostsTruncated=true;return;}
        ++hostCount;
        if (index < HostSlots) ++hosts[index]; else ++otherHosts;
    }
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
