#pragma once
#include "presentation_memory.h"
#include "surface.h"
#include "region.h"
#include <array>

namespace ui {
// Bounded solid primitive, admitted only through the explicit native bridge.
// Default ordinary GDI remains CPU; see G4B2H_ORDINARY_GDI_DISPATCH.md.
// Requires the owning thread's current GL 3.3 core context; bounded caller
// state is restored at handoff. See G4B2_SHARED_CONTEXT.md for preconditions.
class GpuGdiPrototype {
public:
    enum class Mode {Empty,Gpu,CpuFallback};
    static constexpr int MaxSide=512;
    static constexpr size_t MaxCommands=64,MaxInputBytes=4*1024*1024;
    struct Stats {
        uint64_t draws=0,uploads=0,readbacks=0,recoveries=0,checkpoints=0;
        uint64_t submittedCommands=0,batches=0,batchedCommands=0,cpuImports=0;
    };
    explicit GpuGdiPrototype(PresentationAllocator* allocator=nullptr);
    ~GpuGdiPrototype();
    GpuGdiPrototype(const GpuGdiPrototype&)=delete;
    GpuGdiPrototype& operator=(const GpuGdiPrototype&)=delete;
    bool Initialize(int width,int height);
    void Shutdown(); // GL context must be current if device objects still exist.
    // Accepted logical operations. Out-of-target portions clip; malformed copy
    // dimensions/counts fail before changing pixels. Copy owns the input bytes.
    bool Fill(Rect,uint32_t rgb);
    bool Copy(Rect,const uint32_t* rgb,size_t pixels);
    bool ImportCpu(const uint32_t* rgb,size_t pixels); // full image, empty journal only; no readback
    bool FlushGpu(); // False after device failure; logical pixels still recover.
    const Surface* CpuRead(); // Explicit readback/checkpoint, or bounded replay.
    // Call AFTER actual context destruction/loss: performs no GL calls.
    void ContextLost();
    Mode Backend() const {return mode_;}
    const Stats& Counters() const {return stats_;}
    size_t JournalCommands() const {return count_;}
    size_t InputBytes() const {return inputBytes_;}
    void EnableBatching(bool enabled) {batching_=enabled;} // native same-build reference control
private:
    friend class GpuGdiTargets;
    struct Command {Rect rect;uint32_t color=0;bool copy=false;PresentationBuffer<uint32_t> input;};
    bool Room(size_t bytes);
    void Replay();
    void Apply(const Command&);
    void ClearJournal();
    void Recover(bool contextAlive);
    bool OpenDevice();
    void DeleteDevice();
    bool Draw(const Command&);
    bool DrawFillBatch(size_t first,size_t count);
    bool ReadDevice();
    PresentationAllocator* allocator_;
    PresentationBuffer<uint32_t> checkpoint_,scratch_;
    Surface surface_;
    std::array<Command,MaxCommands> commands_;
    size_t count_=0,flushed_=0,inputBytes_=0;
    uint32_t target_=0,upload_=0,fbo_=0,vao_=0,program_=0,batchProgram_=0;
    struct Locations {int operation=-1,image=-1,origin=-1,fill=-1,rects=-1,colors=-1,size=-1;} locations_;
    bool batching_=true;
    Mode mode_=Mode::Empty;
    Stats stats_;
};
} // namespace ui
