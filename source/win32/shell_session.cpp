#include "shell_session.h"
#include "../runtime/guest_execution.h"
#include <algorithm>
#include <cstring>

namespace win32::user {
namespace {
constexpr int W=ui::DesktopShell::Width,H=ui::DesktopShell::Height;
void View(ui::Surface& s,ui::PresentationBuffer<uint32_t>& pixels) {
    s.width=W;s.height=H;s.pitch=W*4;s.bits=reinterpret_cast<uint8_t*>(pixels.data());
}
void Copy(ui::Surface& dst,const ui::Surface& src,Rect rect) {
    rect=rect.Intersect({0,0,dst.width,dst.height}).Intersect({0,0,src.width,src.height});
    for(int y=rect.top;y<rect.bottom;++y)for(int x=rect.left;x<rect.right;++x)dst.Set(x,y,src.Get(x,y));
}
}
ShellSession::ShellSession(ui::DesktopShell& shell,ui::PresentationAllocator* allocator)
    :shell_(shell),previous_(g_platform),background_(allocator),chrome_(allocator),output_(allocator) {}
ShellSession::~ShellSession() {
    if(!ready_)return;
    g_platform=previous_;S().workBottom=previousWorkBottom_;shell_.EndGuest();
}
bool ShellSession::Attach(const std::string& label) {
    if(ready_ || S().screenW!=W || S().screenH!=H || !S().initialized)return false;
    if(!background_.Resize(size_t(W)*H) || !chrome_.Resize(size_t(W)*H) || !output_.Resize(size_t(W)*H))return false;
    View(backgroundSurface_,background_);View(chromeSurface_,chrome_);View(outputSurface_,output_);
    shell_.SetGuestActive(label);shell_.Render(backgroundSurface_,clock_);
    previousWorkBottom_=S().workBottom;S().workBottom=680;
    g_platform.shellContext=this;
    g_platform.paintDesktop=[](void* context,ui::Surface& target,const Region& clip) {
        static_cast<ShellSession*>(context)->PaintBackground(target,clip);
    };
    // Filtering happens once, at physical observation, including outside USER.
    // The USER pump receives only already-routed guest events from this adapter.
    g_platform.filterInput=nullptr;
    g_platform.wait=[](int timeout,std::vector<InputEvent>& events) {
        return static_cast<ShellSession*>(g_platform.shellContext)->WaitGuest(timeout,events);
    };
    g_platform.shellRevision=[](void* context){return static_cast<ShellSession*>(context)->revision_;};
    ready_=true;Refresh();RepaintDesktop(Region({0,0,W,H}));return true;
}
void ShellSession::PaintBackground(ui::Surface& target,const Region& clip) {
    for(const auto& r:clip.Rects())Copy(target,backgroundSurface_,r);
}
bool ShellSession::QueueGuest(InputEvent e) {
    ObservePhysicalInput(e);
    if(!e.observedNs && S().inputFrames.Enabled())e.observedNs=ui::PerformanceNowNs();
    if(e.type==InputEvent::MouseMove && inputCount_) {
        auto& last=guestInput_[(inputFirst_+inputCount_-1)%MaxGuestInput];
        if(last.type==InputEvent::MouseMove) {last=e;S().inputFrames.Coalesced();return true;}
    }
    if(inputCount_==MaxGuestInput) {status_=HostStatus::InputOverflow;return false;}
    guestInput_[(inputFirst_+inputCount_)%MaxGuestInput]=e;++inputCount_;return true;
}
bool ShellSession::ReadHost(int timeout,bool independent) {
    if(status_!=HostStatus::Running || !previous_.wait)return status_==HostStatus::Running;
    if(reading_)return true;
    struct ReadScope {bool& value;ReadScope(bool& v):value(v){value=true;}~ReadScope(){value=false;}} scope(reading_);
    std::vector<InputEvent> events;const bool alive=previous_.wait(timeout,events);
    for(const auto& e:events) {
        if(e.type==InputEvent::MouseMove || e.type==InputEvent::MouseDown || e.type==InputEvent::MouseUp || e.type==InputEvent::Wheel) {
            S().cursorX=std::max(0,std::min(e.x,S().screenW-1));
            S().cursorY=std::max(0,std::min(e.y,S().screenH-1));
        }
        if(e.type==InputEvent::Quit && independent) {status_=HostStatus::Quit;break;}
        if(!Filter(e) && !QueueGuest(e))break;
    }
    if(status_==HostStatus::Running && shell_.ServiceCopy())Refresh();
    if(!alive && status_==HostStatus::Running)status_=HostStatus::SourceClosed;
    return alive;
}
bool ShellSession::WaitGuest(int timeout,std::vector<InputEvent>& out) {
    if(!inputCount_)ReadHost(timeout);
    while(inputCount_) {
        out.push_back(guestInput_[inputFirst_]);inputFirst_=(inputFirst_+1)%MaxGuestInput;--inputCount_;
    }
    return status_==HostStatus::Running;
}
ShellSession::HostStatus ShellSession::ServiceHost() {
    if(!ready_ || rt::GuestExecution::Active())return status_;
    if(ObserveTask())Refresh();
    ReadHost(0,true);return status_;
}
void ShellSession::Refresh() {
    ObserveTask();
    if(shell_.PendingTextInput()!=ui::DesktopShell::TextInput::None) {
        std::u16string output;
        const bool accepted=g_platform.textInput && g_platform.textInput(hle::Utf8PathToWide(shell_.InitialText()),false,false,output);
        shell_.CompleteTextInput(accepted,hle::WideToUtf8(output));
        shell_.CancelPointer();taskPressed_=false;
        std::fill(std::begin(shellButtons_),std::end(shellButtons_),false);
    }
    shell_.SetGuestTaskPending(!pendingTask_.empty());
    shell_.SetGuestMinimized(!minimized_.empty());shell_.Render(chromeSurface_,clock_);
    ++revision_;
}
void ShellSession::SetClock(const std::u16string& clock) {
    if(clock_==clock)return;
    clock_=clock;if(ready_)Refresh();
}
const ui::Surface& ShellSession::Compose(const ui::Surface& guest) {
    if(!ready_ || guest.width!=W || guest.height!=H || !guest.Valid())return guest;
    if(ObserveTask())Refresh();
    Copy(outputSurface_,guest,{0,0,W,H});
    Copy(outputSurface_,chromeSurface_,{0,680,W,H});
    if(shell_.ActivePanel()!=ui::DesktopShell::Panel::Desktop)Copy(outputSurface_,chromeSurface_,{218,100,1166,630});
    if(shell_.ActivePanel()==ui::DesktopShell::Panel::Desktop && shell_.ActiveDialog()!=ui::DesktopShell::Dialog::None)Copy(outputSurface_,chromeSurface_,{396,196,1034,552});
    if(shell_.StartOpen())Copy(outputSurface_,chromeSurface_,{0,284,484,680});
    return outputSurface_;
}
bool ShellSession::ShellPoint(int x,int y) const {
    if(y>=680 || shell_.StartOpen() || shell_.ModalActive())return true;
    if(shell_.ActivePanel()!=ui::DesktopShell::Panel::Desktop && Rect(218,100,1166,630).Contains(x,y))return true;
    return !WindowFromPoint(x,y,false); // exposed native desktop, never guest chrome
}
bool ShellSession::ObserveTask() {
    const auto old=minimized_.size();
    minimized_.erase(std::remove_if(minimized_.begin(),minimized_.end(),[](uint32_t hwnd) {
        const auto* w=Win(hwnd);return !w || !(w->style&WS_MINIMIZE);
    }),minimized_.end());
    const bool changed=minimized_.size()!=old;
    if(pendingTask_.empty())return changed;
    for(auto hwnd:pendingTask_)if(const auto* w=Win(hwnd)) {
        if(bool(w->style&WS_MINIMIZE)!=pendingMinimize_)return changed;
    }
    minimized_.clear();
    if(pendingMinimize_)for(auto hwnd:pendingTask_)if(const auto* w=Win(hwnd))
        if(w->style&WS_MINIMIZE)minimized_.push_back(hwnd);
    pendingTask_.clear();return true;
}
bool ShellSession::PostTask(const std::vector<uint32_t>& handles,bool minimize) {
    // Validate the entire bounded group before enqueuing. No guest/native yield
    // occurs here; HWND state cannot change between validation and publication.
    for(auto hwnd:handles) {
        const auto* w=Win(hwnd);if(!w || !w->threadId)return false;
        const auto* t=rt::g_process?rt::g_process->FindThread(w->threadId):nullptr;
        if(rt::g_process && (!t || t->status==rt::GuestThreadStatus::Exited))return false;
        if(QueueFor(w->threadId).closed)return false;
    }
    pendingTask_=handles;pendingMinimize_=minimize;
    auto post=[&](uint32_t hwnd) {const auto* w=Win(hwnd);
        return PostToThread(w->threadId,hwnd,WM_SYSCOMMAND,minimize?0xF020:0xF120,0);};
    if(minimize) {for(auto hwnd:handles)post(hwnd);}
    else {for(auto i=handles.rbegin();i!=handles.rend();++i)post(*i);}
    return true;
}
void ShellSession::ToggleTask() {
    shell_.CancelPointer();keyboard_=false;ObserveTask();
    // One outstanding intent; a busy/refusing application does not get a flood
    // of callbacks or a fabricated minimized state from repeated clicks.
    if(!pendingTask_.empty())return;
    if(!minimized_.empty()) {
        std::vector<uint32_t> live;
        for(auto hwnd:minimized_)if(const auto* w=Win(hwnd))if(w->style&WS_MINIMIZE)live.push_back(hwnd);
        if(live.empty())minimized_.clear();else PostTask(live,false);
    } else if(const auto* desktop=Desktop()) {
        std::vector<uint32_t> list;
        for(auto hwnd:desktop->children)if(const auto* w=Win(hwnd)) {
            if((w->style&WS_VISIBLE) && !(w->style&WS_MINIMIZE)) {
                if(list.size()==64)return;
                list.push_back(hwnd);
            }
        }
        if(!list.empty())PostTask(list,true);
        else {
            for(auto hwnd:desktop->children)if(const auto* w=Win(hwnd)) {
                if((w->style&(WS_VISIBLE|WS_MINIMIZE))==(WS_VISIBLE|WS_MINIMIZE)) {
                    if(list.size()==64)return;
                    list.push_back(hwnd);
                }
            }
            if(!list.empty())PostTask(list,false);
        }
    }
    Refresh();
}
bool ShellSession::Filter(const InputEvent& e) {
    using IE=InputEvent;using Shell=ui::DesktopShell;
    if(!ready_ || e.type==IE::Quit)return false;
    const bool guestHeld=guestButtons_[0]||guestButtons_[1]||guestButtons_[2];
    const bool guestDrag=guestHeld || (S().capture && !keyboard_);
    if(e.type==IE::KeyDown || e.type==IE::KeyUp || e.type==IE::Char) {
        if(e.type==IE::Char)return keyboard_;
        const auto vk=e.vk&255;const bool down=e.type==IE::KeyDown;
        if(vk==0x5B) {
            if(down && !guestHeld){shell_.ToggleStart();keyboard_=shell_.StartOpen() || shell_.ModalActive();Refresh();}
            return true;
        }
        if(!down) {const bool owned=shellKeys_[vk];shellKeys_[vk]=false;return owned;}
        if(!keyboard_)return false;
        shellKeys_[vk]=true;
        switch(vk) {
        case 0x26:shell_.Move(-1);break;case 0x28:shell_.Move(1);break;
        case 0x25:shell_.Page(-1);break;case 0x27:shell_.Page(1);break;
        case 0x09:shell_.NextFocus();break;case 0x0D:shell_.Accept();break;
        case 0x5D:shell_.OpenContext();break;
        case 0x1B:shell_.Back();keyboard_=shell_.StartOpen() || shell_.ActivePanel()!=Shell::Panel::Desktop;break;
        default:break;
        }
        Refresh();return true;
    }
    const int button=std::max(0,std::min(2,e.button));
    const bool shellDrag=shellButtons_[0]||shellButtons_[1]||shellButtons_[2];
    if(e.type==IE::MouseDown) {
        const bool owned=!guestDrag && (shellDrag || ShellPoint(e.x,e.y));
        shellButtons_[button]=owned;guestButtons_[button]=!owned;
        if(!owned){keyboard_=false;return false;}
        keyboard_=true;if(button==0) {
            taskPressed_=!shell_.ModalActive() && Shell::Bounds(Shell::GuestTask).Contains(e.x,e.y);
            shell_.PointerDown(e.x,e.y);
        }
        if(button==1)shell_.SecondaryDown(e.x,e.y);
        Refresh();return true;
    }
    if(e.type==IE::MouseUp) {
        const bool owned=shellButtons_[button];shellButtons_[button]=false;guestButtons_[button]=false;
        if(!owned)return false;
        if(button==0) {
            // Task actions require press and release inside the same task button.
            const bool task=Shell::Bounds(Shell::GuestTask).Contains(e.x,e.y) && taskPressed_;
            taskPressed_=false;
            if(task)ToggleTask();else shell_.PointerUp(e.x,e.y);
        }
        if(button==1)shell_.SecondaryUp(e.x,e.y);
        Refresh();return true;
    }
    if(e.type==IE::MouseMove || e.type==IE::Wheel) {
        if(guestDrag)return false;
        if(!shellDrag && !ShellPoint(e.x,e.y))return false;
        if(e.type==IE::MouseMove) {
            const int old=shell_.Hover();shell_.PointerMove(e.x,e.y);
            if(old!=shell_.Hover())Refresh();
        }
        return true;
    }
    return false;
}
} // namespace win32::user
