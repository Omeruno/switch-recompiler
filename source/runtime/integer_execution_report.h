#pragma once
#include "../cpu/paged_memory.h"
#include <cstdio>
namespace rt {
// Work counts only; no instruction clocks,guest pointers,allocation or I/O loop.
inline void WriteIntegerExecutionReport(FILE* out,const cpu::PagedGuestMemory& memory,bool tracked) {
    const auto& s=memory.IntegerStats();const auto& cache=memory.NativeStats();
    const bool native=memory.NativeExecutionEnabled() && memory.NativeArchitectureAvailable() && !cache.disabled;
    std::fprintf(out,"CPU engine: %s\nCPU counters enabled: %u\nCPU executed x86 steps: %llu\nCPU fast register steps: %llu\nCPU fast memory/LEA/stack steps: %llu\nCPU reference steps: %llu\nCPU portable IR steps: %llu\nCPU native ARM64 steps: %llu\n",
        native?(memory.NativeBatchingRequested()?"experimental budgeted ARM64 batches with interpreter fallback":"experimental ARM64 single-step cache with interpreter fallback"):memory.IntegerFastPathEnabled()?"bounded integer interpreter fast path":"reference interpreter",unsigned(tracked),
        (unsigned long long)(s.fast+s.reference+s.ir+s.native),(unsigned long long)(s.fast-s.mapped),
        (unsigned long long)s.mapped,(unsigned long long)s.reference,(unsigned long long)s.ir,(unsigned long long)s.native);
    std::fprintf(out,"CPU native requested: %u\nCPU native architecture available: %u\nCPU native cache disabled after failure: %u\nCPU native cache requests/hits/misses/compiles/invalidations/evictions/negative/fallbacks/failures: %llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu/%llu\n",
        unsigned(memory.NativeExecutionRequested()),unsigned(memory.NativeArchitectureAvailable()),unsigned(cache.disabled),
        (unsigned long long)cache.requests,(unsigned long long)cache.hits,(unsigned long long)cache.misses,(unsigned long long)cache.compiles,
        (unsigned long long)cache.invalidations,(unsigned long long)cache.evictions,(unsigned long long)cache.negativeHits,(unsigned long long)cache.fallbacks,(unsigned long long)cache.failures);
    std::fprintf(out,"CPU native batch requested: %u\nCPU native multi-instruction calls: %llu\nCPU native instructions in multi calls: %llu\nCPU native largest prefix: %llu\n",unsigned(memory.NativeBatchingRequested()),(unsigned long long)cache.batchCalls,(unsigned long long)cache.batchInstructions,(unsigned long long)cache.maxBatch);
    std::fputs("CPU count scope: all executed x86 instructions in this address space while tracking,including callbacks;does not count HLE calls as x86 instructions. Native dispatch is experimental;no full dynarec,exclusive CPU-time,physical FPS or GPU-speed claim.\n",out);
}
}
