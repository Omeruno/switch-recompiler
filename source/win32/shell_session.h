#pragma once
#include "user32.h"
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
    const ui::Surface& Compose(const ui::Surface& guest);
    void SetClock(const std::u16string& clock);
    bool Filter(const InputEvent& event);
    void PaintBackground(ui::Surface& target,const Region& clip);
    ShellSession(const ShellSession&)=delete;
    ShellSession& operator=(const ShellSession&)=delete;
private:
    void Refresh();
    bool ShellPoint(int x,int y) const;
    void ToggleTask();
    ui::DesktopShell& shell_;
    Platform previous_;
    ui::PresentationBuffer<uint32_t> background_,chrome_,output_;
    ui::Surface backgroundSurface_,chromeSurface_,outputSurface_;
    std::u16string clock_;
    std::vector<uint32_t> minimized_;
    bool ready_=false,keyboard_=false,taskPressed_=false;
    bool shellButtons_[3]={},guestButtons_[3]={},shellKeys_[256]={};
    int previousWorkBottom_=0;
    uint64_t revision_=0;
};
} // namespace win32::user
