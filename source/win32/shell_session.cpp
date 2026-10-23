#include "shell_session.h"
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
    g_platform.filterInput=[](void* context,const InputEvent& e) {return static_cast<ShellSession*>(context)->Filter(e);};
    g_platform.shellRevision=[](void* context){return static_cast<ShellSession*>(context)->revision_;};
    ready_=true;Refresh();RepaintDesktop(Region({0,0,W,H}));return true;
}
void ShellSession::PaintBackground(ui::Surface& target,const Region& clip) {
    for(const auto& r:clip.Rects())Copy(target,backgroundSurface_,r);
}
void ShellSession::Refresh() {
    if(shell_.PendingTextInput()!=ui::DesktopShell::TextInput::None) {
        std::u16string output;
        const bool accepted=g_platform.textInput && g_platform.textInput(hle::Utf8PathToWide(shell_.InitialText()),false,false,output);
        shell_.CompleteTextInput(accepted,hle::WideToUtf8(output));
        shell_.CancelPointer();taskPressed_=false;
        std::fill(std::begin(shellButtons_),std::end(shellButtons_),false);
    }
    shell_.SetGuestMinimized(!minimized_.empty());shell_.Render(chromeSurface_,clock_);
    ++revision_;
}
void ShellSession::SetClock(const std::u16string& clock) {
    if(clock_==clock)return;
    clock_=clock;if(ready_)Refresh();
}
const ui::Surface& ShellSession::Compose(const ui::Surface& guest) {
    if(!ready_ || guest.width!=W || guest.height!=H || !guest.Valid())return guest;
    Copy(outputSurface_,guest,{0,0,W,H});
    Copy(outputSurface_,chromeSurface_,{0,680,W,H});
    if(shell_.ActivePanel()!=ui::DesktopShell::Panel::Desktop)Copy(outputSurface_,chromeSurface_,{218,100,1166,630});
    if(shell_.StartOpen())Copy(outputSurface_,chromeSurface_,{0,284,484,680});
    return outputSurface_;
}
bool ShellSession::ShellPoint(int x,int y) const {
    if(y>=680 || shell_.StartOpen())return true;
    if(shell_.ActivePanel()!=ui::DesktopShell::Panel::Desktop && Rect(218,100,1166,630).Contains(x,y))return true;
    return !WindowFromPoint(x,y,false); // exposed native desktop, never guest chrome
}
void ShellSession::ToggleTask() {
    shell_.CancelPointer();keyboard_=false;
    if(!minimized_.empty()) {
        auto list=std::move(minimized_);minimized_.clear();
        // Restore lower owners before owned dialogs and the former foreground.
        for(auto it=list.rbegin();it!=list.rend();++it)if(auto* w=Win(*it))
            if(w->style&WS_MINIMIZE)ShowWindowInternal(*it,9);
    } else {
        auto* desktop=Desktop();if(!desktop)return;
        std::vector<uint32_t> list;
        for(auto hwnd:desktop->children)if(auto* w=Win(hwnd)) {
            if((w->style&WS_VISIBLE) && !(w->style&WS_MINIMIZE)) {
                if(list.size()==64)return; // bounded, no partial group operation
                list.push_back(hwnd);
            }
        }
        if(list.empty()) {
            // Includes a titlebar-minimized app not minimized by our task button.
            for(auto hwnd:desktop->children)if(auto* w=Win(hwnd))if(w->style&WS_MINIMIZE) {
                if(list.size()==64)return;
                list.push_back(hwnd);
            }
            for(auto it=list.rbegin();it!=list.rend();++it)ShowWindowInternal(*it,9);
        } else if(list.size()<=64) {
            minimized_=list;
            for(auto hwnd:list)if(Win(hwnd))ShowWindowInternal(hwnd,6);
        }
    }
    Refresh();
}
bool ShellSession::Filter(const InputEvent& e) {
    using IE=InputEvent;using Shell=ui::DesktopShell;
    if(!ready_ || e.type==IE::Quit)return false;
    const bool guestDrag=guestButtons_[0]||guestButtons_[1]||guestButtons_[2]||S().capture;
    if(e.type==IE::KeyDown || e.type==IE::KeyUp || e.type==IE::Char) {
        if(e.type==IE::Char)return keyboard_;
        const auto vk=e.vk&255;const bool down=e.type==IE::KeyDown;
        if(vk==0x5B) {
            if(down && !guestDrag){shell_.ToggleStart();keyboard_=shell_.StartOpen();Refresh();}
            return true;
        }
        if(!down) {const bool owned=shellKeys_[vk];shellKeys_[vk]=false;return owned;}
        if(!keyboard_)return false;
        shellKeys_[vk]=true;
        switch(vk) {
        case 0x26:shell_.Move(-1);break;case 0x28:shell_.Move(1);break;
        case 0x25:shell_.Page(-1);break;case 0x27:shell_.Page(1);break;
        case 0x09:shell_.NextFocus();break;case 0x0D:shell_.Accept();break;
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
            taskPressed_=Shell::Bounds(Shell::GuestTask).Contains(e.x,e.y);
            shell_.PointerDown(e.x,e.y);
        }
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
