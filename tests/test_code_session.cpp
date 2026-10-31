#include "source/runtime/guest_session.h"
#include <cassert>
#include <cstdio>
#include <cstring>
static bool Add(void* context,const void* entry,size_t bytes){
    assert(bytes>=8);using Fn=uint32_t(*)(uint32_t,uint32_t);
    *static_cast<uint32_t*>(context)=reinterpret_cast<Fn>(const_cast<void*>(entry))(17,25);return true;
}
int main(){
#if defined(__aarch64__)
    const uint32_t words[]={0x0b010000,0xd65f03c0};
    uint8_t code[sizeof(words)];std::memcpy(code,words,sizeof(code));
#elif defined(__x86_64__)
    const uint8_t code[]={0x89,0xf8,0x01,0xf0,0xc3,0x90,0x90,0x90};
#else
#error This executable storage oracle needs ARM64 or SysV x86_64
#endif
    cpu::CodeLease escaped;cpu::CodeToken old;uint32_t answer=0;
    assert(cpu::GlobalCodeStats().images==0);
    {
        rt::GuestSession outer;
        assert(cpu::GlobalCodeStats().images==0); // Ordinary session is lazy.
        assert(outer.Code().Publish(outer.Memory(),code,sizeof(code),old));
        escaped=outer.Code().Acquire(outer.Memory(),old);
        assert(escaped.Invoke(outer.Memory(),Add,&answer)&&answer==42);
        {
            rt::GuestSession inner;
            assert(!inner.Code().Acquire(inner.Memory(),old));
            assert(!escaped.Invoke(inner.Memory(),Add,&answer));
            cpu::CodeToken nested;
            assert(inner.Code().Publish(inner.Memory(),code,sizeof(code),nested));
            auto lease=inner.Code().Acquire(inner.Memory(),nested);
            assert(lease.Invoke(inner.Memory(),Add,&answer)&&answer==42);
            assert(cpu::GlobalCodeStats().images==2);
        }
        assert(cpu::GlobalCodeStats().images==1);
        assert(escaped.Invoke(outer.Memory(),Add,&answer));
    }
    // Escaped lease retains code,never guest memory or permission to invoke.
    assert(cpu::GlobalCodeStats().images==1);
    {
        rt::GuestSession next;
        assert(!next.Code().Acquire(next.Memory(),old));
        assert(!escaped.Invoke(next.Memory(),Add,&answer));
        escaped={};assert(cpu::GlobalCodeStats().images==0);
        cpu::CodeToken fresh;
        assert(next.Code().Publish(next.Memory(),code,sizeof(code),fresh));
        auto lease=next.Code().Acquire(next.Memory(),fresh);
        assert(lease.Invoke(next.Memory(),Add,&answer)&&answer==42);
    }
    assert(cpu::GlobalCodeStats().images==0);
    std::puts("Code session PASS (lazy allocation,nested owner isolation,real generated execution,retired escaped lease,sequential reuse and complete release;not physical FPS)");
}
