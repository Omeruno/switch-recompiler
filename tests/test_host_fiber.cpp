// Also compiled for Linux AArch64/QEMU: exercises the Switch context switch.
#include "source/runtime/host_fiber.h"
#include <cassert>
#include <cfenv>
#include <cstdio>
#include <cstdint>
struct Run {
    rt::HostFiber fiber;
    int rounding=0;
    unsigned iterations=0, destroyed=0;
    double sum=0;
};
struct Cleanup { unsigned& count; ~Cleanup() { ++count; } };
static void Deep(Run& run,unsigned depth) {
    volatile uint64_t cookie=0x123456789abcdef0ull+depth;
    Cleanup cleanup{run.destroyed};
    if (depth) Deep(run,depth-1);
    else {
        std::fesetround(run.rounding);
        for (unsigned n=0;n<10000;++n) {
#if defined(__aarch64__)
            register uint64_t integer asm("x19")=uint64_t(run.rounding)+n+0x12345678ull;
            register double floating asm("d8")=double(run.rounding)+n+0.25;
            asm volatile("" : "+r"(integer), "+w"(floating));
#endif
            run.sum+=0.5;
            ++run.iterations;
            run.fiber.Suspend();
#if defined(__aarch64__)
            asm volatile("" : "+r"(integer), "+w"(floating));
            assert(integer==uint64_t(run.rounding)+n+0x12345678ull);
            assert(floating==double(run.rounding)+n+0.25);
#endif
            assert(std::fegetround()==run.rounding);
        }
    }
    assert(cookie==0x123456789abcdef0ull+depth);
}
static void Entry(void* argument) { Deep(*static_cast<Run*>(argument),8); }
int main() {
    rt::HostFiber root;
    Run first,second;
    first.rounding=FE_DOWNWARD; second.rounding=FE_UPWARD;
    assert(first.fiber.Initialize(&Entry,&first));
    assert(second.fiber.Initialize(&Entry,&second));
    std::fesetround(FE_TONEAREST);
    for (unsigned n=0;n<=10000;++n) {
        assert(first.fiber.Resume(root));
        assert(std::fegetround()==FE_TONEAREST);
        assert(second.fiber.Resume(root));
        assert(std::fegetround()==FE_TONEAREST);
    }
    assert(first.fiber.Finished() && second.fiber.Finished());
    assert(first.iterations==10000 && second.iterations==10000);
    assert(first.sum==5000 && second.sum==5000);
    assert(first.destroyed==9 && second.destroyed==9);
    assert(!first.fiber.Resume(root));
    puts("PASSED: 20000 native stack switches, FP environment, nested RAII and completion");
}
