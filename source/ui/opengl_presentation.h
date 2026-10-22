#pragma once
#include "presentation.h"

namespace ui {
// Draws to the current GL context's default framebuffer. Context/window ownership
// and swapping stay in the platform adapter. Host builds use an unavailable stub
// unless SR_OPENGL_TEST enables the same implementation for EGL pixel tests.
class OpenGlPresentation final:public PresentationBackend {
public:
    explicit OpenGlPresentation(PresentationAllocator* allocator=nullptr);
    bool Initialize();
    void Shutdown();
    bool Render(const PresentationFrame&,const OutputBuffer&) override;
    void Reset() override;
    bool RequiresCpuOutput() const override {return false;}
    uint64_t LastWrittenBytes() const override {return uploadedBytes_;}
    uint64_t UploadedBytes() const {return totalUploaded_;}
    uint64_t UploadCalls() const {return uploadCalls_;}
    size_t CacheBytes() const;
    uint64_t LayerDraws() const {return layerDraws_;}
    const char* Error() const {return error_;}
private:
    struct Texture {
        unsigned handle=0;
        int width=0,height=0;
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
    const char* error_="not initialized";
};
} // namespace ui
