#include "host_fiber.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#if defined(__aarch64__)
// Called like an ordinary AAPCS64 function: caller-clobbered registers are
// spilled by the compiler. Preserve the callee-saved integer/FP state and
// floating-point control/status on each native continuation.
extern "C" void srSwapFiber(uint64_t* from, const uint64_t* to);
asm(
".text\n.align 2\n.global srSwapFiber\n.type srSwapFiber, %function\n"
"srSwapFiber:\n"
"stp x19, x20, [x0, #0]\n stp x21, x22, [x0, #16]\n"
"stp x23, x24, [x0, #32]\n stp x25, x26, [x0, #48]\n"
"stp x27, x28, [x0, #64]\n stp x29, x30, [x0, #80]\n"
"mov x16, sp\n str x16, [x0, #96]\n"
"stp d8, d9, [x0, #104]\n stp d10, d11, [x0, #120]\n"
"stp d12, d13, [x0, #136]\n stp d14, d15, [x0, #152]\n"
"mrs x16, fpcr\n str x16, [x0, #168]\n"
"mrs x16, fpsr\n str x16, [x0, #176]\n"
"ldp x19, x20, [x1, #0]\n ldp x21, x22, [x1, #16]\n"
"ldp x23, x24, [x1, #32]\n ldp x25, x26, [x1, #48]\n"
"ldp x27, x28, [x1, #64]\n ldp x29, x30, [x1, #80]\n"
"ldr x16, [x1, #96]\n mov sp, x16\n"
"ldp d8, d9, [x1, #104]\n ldp d10, d11, [x1, #120]\n"
"ldp d12, d13, [x1, #136]\n ldp d14, d15, [x1, #152]\n"
"ldr x16, [x1, #168]\n msr fpcr, x16\n"
"ldr x16, [x1, #176]\n msr fpsr, x16\n ret\n"
".size srSwapFiber, .-srSwapFiber\n");
#endif

namespace rt {
namespace { thread_local HostFiber* entering = nullptr; }
bool HostFiber::Initialize(Entry entry, void* arg, size_t stackSize) {
    if (stack_ || !entry || stackSize<16384) return false;
    stack_.reset(new (std::nothrow) uint8_t[stackSize]);
    if (!stack_) return false;
    stackSize_=stackSize;
    entry_=entry; arg_=arg;
    std::memset(stack_.get(),0xA7,64);
#if defined(__aarch64__)
    context_[11]=reinterpret_cast<uintptr_t>(&Bootstrap);
    context_[12]=(reinterpret_cast<uintptr_t>(stack_.get())+stackSize)&~uintptr_t(15);
    asm volatile("mrs %0, fpcr" : "=r"(context_[21]));
    asm volatile("mrs %0, fpsr" : "=r"(context_[22]));
#else
    if (getcontext(&context_)!=0) return false;
    context_.uc_stack.ss_sp=stack_.get()+64;
    context_.uc_stack.ss_size=stackSize-64;
    context_.uc_link=nullptr;
    makecontext(&context_,&Bootstrap,0);
#endif
    return true;
}
void HostFiber::CheckStack() const {
    for (size_t i=0;i<64;++i) if (stack_[i]!=0xA7) {
        std::fputs("guest host-fiber stack overflow\n",stderr); std::abort();
    }
}
void HostFiber::SwitchTo(HostFiber& other) {
#if defined(__aarch64__)
    srSwapFiber(context_,other.context_);
#else
    if (swapcontext(&context_,&other.context_)!=0) std::abort();
#endif
}
bool HostFiber::Resume(HostFiber& caller) {
    if (!entry_ || finished_ || &caller==this) return false;
    caller_=&caller;
    entering=this;
    started_=true;
    caller.SwitchTo(*this);
    CheckStack();
    if (finished_) stack_.reset(); // only after native frames returned normally
    return true;
}
void HostFiber::Suspend() {
    if (!caller_) std::abort();
    uint8_t marker;
    uintptr_t sp=reinterpret_cast<uintptr_t>(&marker);
    if (sp<reinterpret_cast<uintptr_t>(stack_.get())+4096 ||
        sp>=reinterpret_cast<uintptr_t>(stack_.get())+stackSize_) std::abort();
    CheckStack();
    SwitchTo(*caller_);
}
void HostFiber::Bootstrap() {
    HostFiber* self=entering;
    self->entry_(self->arg_);
    self->finished_=true;
    self->Suspend();
    std::abort(); // a finished continuation may never be resumed
}
} // namespace rt
