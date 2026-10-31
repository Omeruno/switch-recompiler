#pragma once
#include "../cpu/paged_memory.h"
#include <cstdio>
namespace rt {
// Work counts only; no instruction clocks,guest pointers,allocation or I/O loop.
inline void WriteIntegerExecutionReport(FILE* out,const cpu::PagedGuestMemory& memory,bool tracked) {
    const auto& s=memory.IntegerStats();
    std::fprintf(out,"CPU engine: %s\nCPU counters enabled: %u\nCPU interpreted steps: %llu\nCPU fast register steps: %llu\nCPU fast memory/LEA/stack steps: %llu\nCPU reference steps: %llu\n",
        memory.IntegerFastPathEnabled()?"bounded integer interpreter fast path":"reference interpreter",unsigned(tracked),
        (unsigned long long)(s.fast+s.reference),(unsigned long long)(s.fast-s.mapped),
        (unsigned long long)s.mapped,(unsigned long long)s.reference);
    std::fputs("CPU count scope: all interpreted instructions in this address space while tracking,including callbacks;does not count HLE calls as x86 instructions. No dynarec,exclusive CPU-time,physical FPS or GPU-speed claim.\n",out);
}
}
