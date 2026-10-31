#include "source/cpu/code_storage.h"
#include "source/runtime/host_fiber.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <vector>
using namespace cpu;
namespace {
using Callback=uint32_t(*)(void*);
using Function=uint32_t(*)(uint32_t,uint32_t,Callback,void*);
std::vector<uint8_t> Image(unsigned kind){
#if defined(__aarch64__)
    const uint32_t add[]={0x0b010000u,0xd65f03c0u};
    const uint32_t xors[]={0x4a010000u,0x11001c00u,0xd65f03c0u};
    // Preserve AAPCS64 x19/LR,call C++ through x2,then return to this mapping.
    const uint32_t calls[]={0xa9bf7bf3u,0x0b010013u,0xaa0203f0u,0xaa0303e0u,0xd63f0200u,0x0b130000u,0xa8c17bf3u,0xd65f03c0u};
    const auto* bytes=reinterpret_cast<const uint8_t*>(kind==0?add:kind==1?xors:calls);
    const size_t size=kind==0?sizeof(add):kind==1?sizeof(xors):sizeof(calls);return {bytes,bytes+size};
#elif defined(__x86_64__)
    if(kind==0)return {0x89,0xf8,0x01,0xf0,0xc3,0x90,0x90,0x90};
    if(kind==1)return {0x89,0xf8,0x31,0xf0,0x83,0xc0,7,0xc3};
    // System V: RBX preserved,aligned stack at callback,rdi=callback context.
    return {0x53,0x89,0xfb,0x01,0xf3,0x48,0x89,0xcf,0xff,0xd2,0x01,0xd8,0x5b,0xc3,0x90,0x90};
#else
#error This native code test supports AArch64 and x86_64
#endif
}
struct Run {
    uint32_t a=0,b=0,result=0;bool writable=false,executable=false;
    Callback callback=nullptr;void* context=nullptr;
    static bool Invoke(void* context,const void* entry,size_t size){auto& r=*static_cast<Run*>(context);assert(size>=8);
#ifdef __linux__
        std::ifstream maps("/proc/self/maps");std::string line;while(std::getline(maps,line)){
            unsigned long long low=0,high=0;char flags[5]={};if(std::sscanf(line.c_str(),"%llx-%llx %4s",&low,&high,flags)!=3)continue;
            const auto address=reinterpret_cast<uintptr_t>(entry);if(address>=low && address<high){r.writable=flags[1]=='w';r.executable=flags[2]=='x';break;}
        }
        assert(!r.writable && r.executable);
#endif
        r.result=reinterpret_cast<Function>(const_cast<void*>(entry))(r.a,r.b,r.callback,r.context);return true;
    }
};
struct Model {unsigned allocated=0,initialized=0,published=0,closed=0,destroyed=0;bool allocateFail=false,initFail=false,publishFail=false,entryFail=false,closeFail=false;size_t granule=4096;};
class ModelRegion final:public CodeRegion {
    std::shared_ptr<Model> model_;std::vector<uint32_t> bytes_;bool ready_=false,closed_=false;
public:
    ModelRegion(std::shared_ptr<Model> m,size_t bytes):model_(std::move(m)),bytes_(bytes/4){++model_->allocated;}
    ~ModelRegion(){++model_->destroyed;}
    size_t Capacity()const override{return bytes_.size()*4;}
    bool Initialize(const uint8_t* bytes,size_t size)override{++model_->initialized;if(model_->initFail)return false;std::memcpy(bytes_.data(),bytes,size);return true;}
    bool Publish()override{++model_->published;if(model_->publishFail)return false;ready_=true;return true;}
    const void* Executable()const override{return ready_ && !model_->entryFail?bytes_.data():nullptr;}
    bool Close()override{++model_->closed;if(model_->closeFail)return false;closed_=true;return true;}
};
class ModelBackend final:public CodeBackend {
    std::shared_ptr<Model> model_;
public:
    explicit ModelBackend(std::shared_ptr<Model> m):model_(std::move(m)){}
    size_t Granule()const override{return model_->granule;}
    std::unique_ptr<CodeRegion> Allocate(size_t bytes)override{if(model_->allocateFail)return {};return std::make_unique<ModelRegion>(model_,bytes);}
};
std::unique_ptr<CodeBackend> Backend(const std::shared_ptr<Model>& m){return std::make_unique<ModelBackend>(m);}
struct FiberCall {
    rt::HostFiber root,fiber;CodeLease lease;IGuestMemory* memory=nullptr;bool entered=false,completed=false;uint32_t result=0;
    static uint32_t Suspend(void* context){auto& self=*static_cast<FiberCall*>(context);self.entered=true;self.fiber.Suspend();return 19;}
    static void Start(void* context){auto& self=*static_cast<FiberCall*>(context);Run run{0xfffffff0u,33};run.callback=&Suspend;run.context=&self;
        assert(self.lease.Invoke(*self.memory,&Run::Invoke,&run));self.result=run.result;self.completed=true;}
};
}
int main(int argc,char** argv){
    const bool fibers=argc==1;assert(argc==1 || (argc==2 && !std::strcmp(argv[1],"--no-fiber")));
    PagedGuestMemory memory,other;const auto add=Image(0),xors=Image(1),calls=Image(2);
    assert(GlobalCodeStats().images==0);
    {
        CodeStore store(memory.Identity(),{8192,2});CodeToken token;
        assert(!store.Publish(other,add.data(),add.size(),token) && store.LastError()==CodeError::WrongOwner && !token);
        assert(!store.Publish(memory,nullptr,8,token) && store.LastError()==CodeError::InvalidInput);
        assert(!store.Publish(memory,add.data(),3,token) && store.LastError()==CodeError::InvalidInput);
        assert(!store.Publish(memory,add.data(),CodeStore::MaxImageBytes+4,token));
        assert(store.Publish(memory,add.data(),add.size(),token));auto lease=store.Acquire(memory,token);assert(lease && !store.Acquire(other,token));
        Run r{0xffffffffu,7};assert(lease.Invoke(memory,&Run::Invoke,&r) && r.result==6);
        assert(!lease.Invoke(other,&Run::Invoke,&r));
        auto copy=lease;assert(store.Stats().pins==2 && store.Stats().images==1);
        assert(store.Retire(token) && !store.Acquire(memory,token) && !lease.Invoke(memory,&Run::Invoke,&r));
        CodeToken second;assert(store.Publish(memory,xors.data(),xors.size(),second));assert(second.serial!=token.serial);
        CodeToken refused;assert(!store.Publish(memory,add.data(),add.size(),refused) && store.LastError()==CodeError::LocalQuota);
        lease={};copy={};assert(store.Stats().images==1);
        auto current=store.Acquire(memory,second);Run v{0xabcdef01u,0x12345678u};assert(current.Invoke(memory,&Run::Invoke,&v) && v.result==((v.a^v.b)+7));
        assert(store.Close()==CodeClose::Pending && !current.Invoke(memory,&Run::Invoke,&v));current={};assert(store.Close()==CodeClose::Drained);
        assert(!store.Publish(memory,add.data(),add.size(),refused) && store.LastError()==CodeError::Closed);
    }
    assert(GlobalCodeStats().images==0);
    // Publish/execute/retire repeatedly,exercising cache publication at potentially
    // reused virtual addresses with different instructions and arithmetic inputs.
    uint32_t random=0x125783;
    {
        CodeStore store(memory.Identity());CodeToken previous;
        for(unsigned pass=0;pass<128;++pass){
            CodeToken token;const auto& bytes=(pass&1)?xors:add;
            assert(store.Publish(memory,bytes.data(),bytes.size(),token));assert(!store.Acquire(memory,previous));
            auto lease=store.Acquire(memory,token);
            for(unsigned n=0;n<64;++n){random=random*1664525u+1013904223u;Run r{random,random^0xa5987341u};
                assert(lease.Invoke(memory,&Run::Invoke,&r) && r.result==((pass&1)?(r.a^r.b)+7:r.a+r.b));}
            assert(store.Retire(token));lease={};assert(store.Stats().images==0);previous=token;
        }
        assert(store.Close()==CodeClose::Drained);
    }
    if(fibers){
        auto store=std::make_unique<CodeStore>(memory.Identity());CodeToken token;assert(store->Publish(memory,calls.data(),calls.size(),token));
        FiberCall call;call.memory=&memory;call.lease=store->Acquire(memory,token);assert(call.fiber.Initialize(&FiberCall::Start,&call));
        assert(call.fiber.Resume(call.root) && call.entered && !call.completed);assert(store->Stats().pins==2);
        assert(store->Retire(token));call.lease={};store.reset();assert(GlobalCodeStats().images==1);
        assert(call.fiber.Resume(call.root) && call.fiber.Finished() && call.completed && call.result==36);
        assert(GlobalCodeStats().images==0);
    }
    {
        CodeStore store(memory.Identity(),{4096,16}),foreign(memory.Identity());CodeToken token,refused;
        assert(store.Publish(memory,add.data(),add.size(),token));auto lease=store.Acquire(memory,token);
        assert(!foreign.Acquire(memory,token));CodeToken invalid=token;invalid.slot=0xffffffffu;assert(!store.Acquire(memory,invalid));
        assert(!store.Publish(memory,xors.data(),xors.size(),refused) && store.LastError()==CodeError::LocalQuota);
        Run run{17,19};assert(lease.Invoke(memory,&Run::Invoke,&run) && run.result==36);
    }
    // Global reservations remain charged while retired leases exist,even across
    // different stores. A new session cannot bypass the process cap.
    {
        CodeStore first(memory.Identity(),{1024*1024,32}),second(memory.Identity(),{1024*1024,32}),third(other.Identity());
        std::vector<CodeLease> held;for(auto* store:{&first,&second})for(unsigned n=0;n<32;++n){CodeToken token;
            assert(store->Publish(memory,add.data(),add.size(),token));held.push_back(store->Acquire(memory,token));assert(store->Retire(token));}
        CodeToken refused;assert(!third.Publish(other,add.data(),add.size(),refused) && third.LastError()==CodeError::GlobalQuota);
        assert(GlobalCodeStats().images==64);held.clear();assert(GlobalCodeStats().images==0);
    }
    for(unsigned stage=0;stage<4;++stage){
        auto model=std::make_shared<Model>();model->allocateFail=stage==0;model->initFail=stage==1;model->publishFail=stage==2;model->entryFail=stage==3;
        CodeStore store(memory.Identity(),{},Backend(model));CodeToken token;assert(!store.Publish(memory,add.data(),add.size(),token) && !token);
        assert(store.Close()==CodeClose::Drained && GlobalCodeStats().images==0 && model->closed==(stage?1u:0u));
        assert(model->destroyed==(stage?1u:0u));
    }
    {
        auto model=std::make_shared<Model>();model->granule=3;CodeStore invalid(memory.Identity(),{},Backend(model));CodeToken token;
        assert(!invalid.Publish(memory,add.data(),add.size(),token) && invalid.LastError()==CodeError::InvalidLimits && !model->allocated);
    }
    // An ambiguous native close is terminal and retained,not retried/destroyed or
    // reported as freed. This one deliberately remains in the bounded global vault.
    auto failed=std::make_shared<Model>();failed->closeFail=true;
    {CodeStore store(memory.Identity(),{},Backend(failed));CodeToken token;assert(store.Publish(memory,add.data(),add.size(),token));
        assert(store.Retire(token) && store.Close()==CodeClose::Retained && store.Close()==CodeClose::Retained);
        assert(store.Stats().retained==1 && failed->closed==1 && failed->destroyed==0);}
    assert(failed->closed==1 && failed->destroyed==0 && GlobalCodeStats().retained==1 && GlobalCodeStats().images==1 && GlobalCodeStats().bytes==4096);
    // Failed publication + failed close must remain charged across destroyed
    // sessions too. Fill the vault;the next session cannot allocate past it.
    std::vector<std::shared_ptr<Model>> failures{failed};
    for(unsigned i=1;i<CodeStore::GlobalImages;++i){
        auto model=std::make_shared<Model>();model->closeFail=true;model->publishFail=true;
        {CodeStore store(memory.Identity(),{},Backend(model));CodeToken token;
            assert(!store.Publish(memory,add.data(),add.size(),token) && !token);
            assert(store.LastError()==CodeError::Publication && store.Close()==CodeClose::Retained);}
        assert(model->closed==1 && model->destroyed==0);failures.push_back(model);
    }
    assert(GlobalCodeStats().images==64 && GlobalCodeStats().retained==64 && GlobalCodeStats().bytes==64*4096);
    {auto model=std::make_shared<Model>();CodeStore store(memory.Identity(),{},Backend(model));CodeToken token;
        assert(!store.Publish(memory,add.data(),add.size(),token) && store.LastError()==CodeError::GlobalQuota && !model->allocated);}
    for(const auto& model:failures)assert(model->closed==1 && model->destroyed==0);
    std::printf("Code storage PASS (8192 real generated arithmetic calls;RW-to-RX publication/cache visibility,owner/token retirement,immutable replacements,global64-image quota,allocation/init/publish/entry faults,sticky retained close;%s;not x86 translation/physical FPS)\n",fibers?"generated native frame survives C++ suspension and store destruction":"native-fiber probe excluded for sanitizer");
}
