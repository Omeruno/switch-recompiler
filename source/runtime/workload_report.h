#pragma once
#include "workload_profile.h"
#include "../win32/host_functions.h"
#include <cstdio>

namespace rt {
// Bounded text, emitted only after execution; IDs resolved against this build.
inline void WriteWorkloadReport(FILE* out, const WorkloadProfile& p) {
    std::fprintf(out, "workload-v1 instructions=%llu hosts=%llu instruction_limit=%llu host_limit=%llu truncated=%d/%d storage=%zu\n",
        (unsigned long long)p.instructionCount, (unsigned long long)p.hostCount,
        (unsigned long long)p.instructionLimit, (unsigned long long)p.hostLimit,
        p.instructionsTruncated, p.hostsTruncated, sizeof(p));
    for (size_t i=0; i<p.opcodes.size(); ++i) if (p.opcodes[i]) {
        if (i==512) std::fprintf(out, "opcode fetch-failure");
        else if (i>=256) std::fprintf(out, "opcode 0f%02x", unsigned(i-256));
        else std::fprintf(out, "opcode %02x", unsigned(i));
        std::fprintf(out, " %llu\n", (unsigned long long)p.opcodes[i]);
    }
    for (size_t i=0; i<p.hosts.size(); ++i) if (p.hosts[i]) {
        if (i<win32::HostFunctionCount()) {
            const auto& e=win32::GetHostFunction(int(i));
            std::fprintf(out, "host %zu %.64s!%.96s ordinal=%u %llu\n", i,
                e.dllName, e.funcName, unsigned(e.ordinal), (unsigned long long)p.hosts[i]);
        } else std::fprintf(out, "host %zu unknown %llu\n", i, (unsigned long long)p.hosts[i]);
    }
    std::fprintf(out, "host other/dynamic-mfc %llu\n", (unsigned long long)p.otherHosts);
    if(p.engines){const auto& census=*p.engines;
        std::fprintf(out,"engine-census-v1 instructions=%llu profile_instructions=%llu storage=%zu scope=accepted-attempts-while-bound\n",(unsigned long long)census.instructionCount,(unsigned long long)p.instructionCount,sizeof(census));
        for(size_t engine=0;engine<EngineCensus::EngineSlots;++engine){
            std::fprintf(out,"engine %s total=%llu\n",EngineCensus::Name(engine),(unsigned long long)census.totals[engine]);
            for(size_t opcode=0;opcode<EngineCensus::OpcodeSlots;++opcode)if(census.opcodes[engine][opcode])
                std::fprintf(out,"engine-opcode %s %03x %llu\n",EngineCensus::Name(engine),unsigned(opcode),(unsigned long long)census.opcodes[engine][opcode]);
        }
    }else std::fputs("engine-census-v1 disabled-or-unavailable\n",out);
}
} // namespace rt
