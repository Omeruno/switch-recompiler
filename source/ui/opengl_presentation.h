#pragma once
#include "presentation.h"

namespace ui {
// Draws to the current GL context's default framebuffer. Context/window ownership
// and swapping stay in the platform adapter. Host builds use an unavailable stub
// unless SR_OPENGL_TEST enables the same implementation for EGL pixel tests.
class OpenGlPresentation final:public PresentationBackend {
public:
    explicit OpenGlPresentation(PresentationAllocator* allocator=nullptr):versions_(allocator) {}
    bool Initialize();
    void Shutdown();
    bool Render(const PresentationFrame&,const OutputBuffer&) override;
    void Reset() override;
    bool RequiresCpuOutput() const override {return false;}
    uint64_t LastWrittenBytes() const override {return uploadedBytes_;}
    uint64_t UploadedBytes() const {return totalUploaded_;}
    uint64_t UploadCalls() const {return uploadCalls_;}
    size_t CacheBytes() const {return versions_.size()*sizeof(uint64_t);}
    const char* Error() const {return error_;}
private:
    unsigned texture_=0,cursorTexture_=0,program_=0,vao_=0;
    int width_=0,height_=0;
    int screenUniform_=-1,cursorUniform_=-1,rectUniform_=-1,sizeUniform_=-1,heightUniform_=-1,cursorStateUniform_=-1;
    uint64_t id_=0,generation_=0,uploadedBytes_=0,totalUploaded_=0,uploadCalls_=0;
    PresentationBuffer<uint64_t> versions_;
    const char* error_="not initialized";
};
} // namespace ui
