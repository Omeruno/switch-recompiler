#pragma once
namespace ui {
// The context is created on the application thread, then explicitly transferred
// to the guest host thread. Never own EGL and libnx Framebuffer simultaneously.
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
