#include "code_storage.h"
#include <cstring>
#include <new>
#ifdef __SWITCH__
#include <switch.h>
#elif defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif
namespace cpu {
namespace {
void Poison(void* memory,size_t bytes){
#if defined(__aarch64__)
    const uint32_t trap=0xd4200000u;for(size_t at=0;at<bytes;at+=4)std::memcpy(static_cast<uint8_t*>(memory)+at,&trap,4);
#else
    std::memset(memory,0xcc,bytes);
#endif
}
#if defined(__SWITCH__)
class PlatformRegion final:public CodeRegion {
    Jit jit_{};bool allocated_=false,initialized_=false,published_=false,closeFailed_=false,ambiguous_=false;
public:
    bool Open(size_t bytes){allocated_=R_SUCCEEDED(jitCreate(&jit_,bytes));return allocated_;}
    size_t Capacity()const override{return allocated_?jit_.size:0;}
    bool Initialize(const uint8_t* data,size_t bytes)override{
        if(!allocated_ || initialized_ || published_ || bytes>jit_.size)return false;
        if(R_FAILED(jitTransitionToWritable(&jit_))){ambiguous_=true;return false;}
        void* rw=jitGetRwAddr(&jit_);if(!rw)return false;Poison(rw,jit_.size);std::memcpy(rw,data,bytes);
        __builtin___clear_cache(static_cast<char*>(rw),static_cast<char*>(rw)+jit_.size);initialized_=true;return true;
    }
    bool Publish()override{
        if(!allocated_ || !initialized_ || published_)return false;
        if(R_FAILED(jitTransitionToExecutable(&jit_))){ambiguous_=true;return false;}
        void* rx=jitGetRxAddr(&jit_);if(!rx){ambiguous_=true;return false;}
        __builtin___clear_cache(static_cast<char*>(rx),static_cast<char*>(rx)+jit_.size);published_=true;return true;
    }
    const void* Executable()const override{return published_?jitGetRxAddr(const_cast<Jit*>(&jit_)):nullptr;}
    bool Close()override{
        if(!allocated_)return true;
        if(closeFailed_ || ambiguous_)return false;
        published_=false;
        if(R_FAILED(jitClose(&jit_))){closeFailed_=true;return false;}
        allocated_=false;return true;
    }
};
class PlatformBackend final:public CodeBackend {
public:
    size_t Granule()const override{return 0x1000;}
    std::unique_ptr<CodeRegion> Allocate(size_t bytes)override{
        auto region=std::unique_ptr<PlatformRegion>(new(std::nothrow) PlatformRegion);
        if(!region || !region->Open(bytes))return {};
        return region;
    }
};
#elif defined(__linux__)
class PlatformRegion final:public CodeRegion {
    void* memory_=nullptr;size_t bytes_=0;bool initialized_=false,published_=false,closeFailed_=false;
public:
    bool Open(size_t bytes){void* p=mmap(nullptr,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        if(p==MAP_FAILED)return false;
        memory_=p;bytes_=bytes;return true;}
    size_t Capacity()const override{return bytes_;}
    bool Initialize(const uint8_t* data,size_t bytes)override{
        if(!memory_ || initialized_ || published_ || bytes>bytes_)return false;
        Poison(memory_,bytes_);std::memcpy(memory_,data,bytes);initialized_=true;return true;
    }
    bool Publish()override{
        if(!memory_ || !initialized_ || published_)return false;
        __builtin___clear_cache(static_cast<char*>(memory_),static_cast<char*>(memory_)+bytes_);
        if(mprotect(memory_,bytes_,PROT_READ|PROT_EXEC))return false;
        __builtin___clear_cache(static_cast<char*>(memory_),static_cast<char*>(memory_)+bytes_);published_=true;return true;
    }
    const void* Executable()const override{return published_?memory_:nullptr;}
    bool Close()override{
        if(!memory_)return true;
        if(closeFailed_)return false;
        published_=false;
        if(munmap(memory_,bytes_)){closeFailed_=true;return false;}
        memory_=nullptr;return true;
    }
};
class PlatformBackend final:public CodeBackend {
public:
    size_t Granule()const override{const long value=sysconf(_SC_PAGESIZE);return value>0?size_t(value):0;}
    std::unique_ptr<CodeRegion> Allocate(size_t bytes)override{
        auto region=std::unique_ptr<PlatformRegion>(new(std::nothrow) PlatformRegion);
        if(!region || !region->Open(bytes))return {};
        return region;
    }
};
#endif
}
std::unique_ptr<CodeBackend> SystemCodeBackend(){
#if defined(__SWITCH__) || defined(__linux__)
    return std::unique_ptr<CodeBackend>(new(std::nothrow) PlatformBackend);
#else
    return {};
#endif
}
} // namespace cpu
