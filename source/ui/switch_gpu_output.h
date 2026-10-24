#pragma once
namespace ui {
// The persistent frontend worker creates, binds and owns the context through
// native/guest phases. Initialize releases its initial binding; the same owner
// binds it before rendering. Never own EGL and libnx Framebuffer simultaneously.
class SwitchGpuOutput {
public:
    bool Initialize();
    bool MakeCurrent();
    bool Swap();
    void Shutdown();
    const char* Error() const {return error_;}
private:
    void* display_=nullptr;
    void* surface_=nullptr;
    void* context_=nullptr;
    const char* error_="not initialized";
};
} // namespace ui
