#pragma once
#include "presentation.h"
#include "resident_frame.h"

namespace ui {
// CPU submission and asynchronous GL command intervals, never scanout/input latency.
struct GpuPresentationStats {
    uint64_t frames=0,uploadCpuNs=0,drawCpuNs=0,maxUploadCpuNs=0,maxDrawCpuNs=0;
    uint64_t gpuSamples=0,gpuNs=0,maxGpuNs=0,gpuSkipped=0;
    uint64_t screenUploadBytes=0,ownershipUploadBytes=0,layerUploadBytes=0;
    size_t peakTextureBytes=0;
};
// Draws to the current GL context's default framebuffer. Context/window ownership
// and swapping stay in the platform adapter. Host builds use an unavailable stub
// unless SR_OPENGL_TEST enables the same implementation for EGL pixel tests.
class OpenGlPresentation final:public PresentationBackend {
public:
    explicit OpenGlPresentation(PresentationAllocator* allocator=nullptr);
    bool Initialize();
    void Shutdown();
    bool Render(const PresentationFrame&,const OutputBuffer&) override;
    // Tokens map resident images to scene layers; CPU surface may be null for
    // mapped layers. The caller owns the base/ownership/CPU overlay snapshots.
    bool RenderResident(const PresentationFrame&,const OutputBuffer&,
        const ResidentFrameView&,const uint32_t* tokens,size_t count);
    void Reset() override;
    bool RequiresCpuOutput() const override {return false;}
    uint64_t LastWrittenBytes() const override {return uploadedBytes_;}
    uint64_t UploadedBytes() const {return totalUploaded_;}
    uint64_t UploadCalls() const {return uploadCalls_;}
    size_t CacheBytes() const;
    uint64_t LayerDraws() const {return layerDraws_;}
    const GpuPresentationStats& Timings() const {return timings_;}
    bool TimerAvailable() const {return timerAvailable_;}
    void ResetStats(); // new measurement scope; retains shaders/textures/context
    void PollTimings(); // checks availability only; never waits for GPU completion
    size_t TextureBytes() const;
    const char* Error() const {return error_;}
private:
    bool RenderImpl(const PresentationFrame&,const OutputBuffer&,
        const ResidentFrameView*,const uint32_t*,size_t);
    struct Texture {
        unsigned handle=0;
        int width=0,height=0;
        size_t allocatedBytes=0;
        uint64_t id=0,generation=0;
        PresentationBuffer<uint64_t> versions;
    };
    static void ResetTexture(Texture&);
    bool Upload(Texture&,const Surface&,uint64_t id,uint64_t generation,const PresentationBuffer<uint64_t>*,bool integer=false);
    Texture screen_,ownership_;
    std::array<Texture,WindowLayers::MaxLayers> layers_;
    unsigned cursorTexture_=0,program_=0,vao_=0;
    int screenUniform_=-1,cursorUniform_=-1,rectUniform_=-1,sizeUniform_=-1,heightUniform_=-1,cursorStateUniform_=-1;
    int ownershipUniform_=-1,modeUniform_=-1,layerRectUniform_=-1,layerTokenUniform_=-1;
    uint64_t uploadedBytes_=0,totalUploaded_=0,uploadCalls_=0,layerDraws_=0;
    uint64_t sceneEpoch_=0,frameId_=0,frameGeneration_=0;
    struct TimerSlot {unsigned handle=0;bool pending=false;};
    std::array<TimerSlot,4> timers_{};
    bool timerAvailable_=false;
    GpuPresentationStats timings_;
    const char* error_="not initialized";
};
} // namespace ui
