#pragma once
#include "user32.h"
#include <array>
#include "../ui/desktop_shell.h"
#include "../ui/presentation_memory.h"

namespace win32::user {
// Worker-owned bridge. The shell is host-owned and is used exclusively by this
// worker until join. Destroy after GuestExecution drains, before GuestSession.
class ShellSession {
public:
    explicit ShellSession(ui::DesktopShell& shell,ui::PresentationAllocator* allocator=nullptr);
    ~ShellSession();
    bool Attach(const std::string& label);
    bool Ready() const {return ready_;}
    enum class HostStatus {Running,Quit,SourceClosed,InputOverflow};
    static constexpr size_t MaxGuestInput=256;
    // Called on the owner native thread between executor quanta. Never invokes
    // a guest procedure; raw guest events wait for its ordinary USER pump.
    HostStatus ServiceHost();
    HostStatus Status() const {return status_;}
    size_t PendingGuestInput() const {return inputCount_;}
    const ui::Surface* TryCompose(const ui::Surface& guest);
    const ui::Surface& Compose(const ui::Surface& guest); // compatibility wrapper; failure is an invalid surface
    uint64_t NativeRevision(); // observe native task completion without rebuilding ownership mask
    ui::WindowSceneView NativeOverlay(); // native-only mask/pixels, no guest CPU barrier
    ui::WindowSceneView Scene(); // optional owned ownership map; empty selects flat fallback
    void SetClock(const std::u16string& clock);
    bool Filter(const InputEvent& event);
    void PaintBackground(ui::Surface& target,const Region& clip);
    ShellSession(const ShellSession&)=delete;
    ShellSession& operator=(const ShellSession&)=delete;
private:
    void Refresh();
    bool ReadHost(int timeoutMs,bool independent=false);
    bool WaitGuest(int timeoutMs,std::vector<InputEvent>& out);
    bool QueueGuest(InputEvent event);
    bool SyncController();
    bool RouteController(const ui::ControllerKeyboard::Batch&,uint64_t stamp=0);
    bool ObserveTask();
    bool PostTask(const std::vector<uint32_t>& handles,bool minimize);
    bool ShellPoint(int x,int y) const;
    void ToggleTask();
    ui::DesktopShell& shell_;
    Platform previous_;
    ui::PresentationBuffer<uint32_t> background_,chrome_,output_,owners_;
    std::array<ui::WindowLayerView,ui::WindowLayers::MaxLayers> sceneLayers_{};
    uint64_t sceneGeneration_=0;
    ui::Surface backgroundSurface_,chromeSurface_,outputSurface_,failedSurface_;
    std::u16string clock_;
    std::vector<uint32_t> minimized_,pendingTask_;
    std::array<InputEvent,MaxGuestInput> guestInput_{};
    size_t inputFirst_=0,inputCount_=0;
    HostStatus status_=HostStatus::Running;
    ui::ControllerKeyboard controller_;
    bool pendingMinimize_=false,reading_=false;
    bool ready_=false,keyboard_=false,taskPressed_=false;
    bool shellButtons_[3]={},guestButtons_[3]={},shellKeys_[256]={};
    int previousWorkBottom_=0;
    uint64_t revision_=0;
};
} // namespace win32::user
