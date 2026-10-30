#include "desktop_shell.h"
#include <algorithm>

namespace ui {
DesktopShell::NativeWindow& DesktopShell::CurrentWindow() {
    return dialog_!=Dialog::None?(DocumentWindow()?documentWindow_:modalWindow_):panelWindows_[size_t(panel_)];
}
const DesktopShell::NativeWindow& DesktopShell::CurrentWindow() const {
    return dialog_!=Dialog::None?(DocumentWindow()?documentWindow_:modalWindow_):panelWindows_[size_t(panel_)];
}
Rect DesktopShell::PanelBounds() const {return nativeMinimized_ || panel_==Panel::Desktop?Rect{}:panelWindows_[size_t(panel_)].bounds;}
Rect DesktopShell::DialogBounds() const {return nativeMinimized_ || dialog_==Dialog::None?Rect{}:CurrentWindow().bounds;}
Rect DesktopShell::ContentBounds() const {
    const auto& r=panelWindows_[size_t(panel_)].bounds;return {r.left+235,r.top+118,r.right-4,r.bottom-30};
}
Rect DesktopShell::StatusBounds() const {
    const auto& r=panelWindows_[size_t(panel_)].bounds;return {r.left+4,r.bottom-30,r.right-4,r.bottom-4};
}
int DesktopShell::VisibleRows() const {
    const int height=ContentBounds().Height();return std::max(1,std::min(Rows,panel_==Panel::Explorer?2*(height/72):height/36));
}
int DesktopShell::VisibleTextRows() const {return std::max(1,std::min(32,(documentWindow_.bounds.Height()-210)/28));}
Rect DesktopShell::DocumentViewport() const {
    const auto& r=documentWindow_.bounds;return {r.left+20,r.top+84,r.right-22,r.bottom-54};
}
bool DesktopShell::NativeMaximized() const {return CurrentWindow().maximized;}
Rect DesktopShell::DialogLayout(Rect r) const {
    if(r.Empty())return {};
    const auto& w=dialog_==Dialog::None?documentWindow_.bounds:CurrentWindow().bounds;
    const int dx=w.left-396,dy=w.top-196,dw=w.Width()-632,dh=w.Height()-350;
    if(r.right>=1000)r.right+=dw;
    if(r.top>=440) {r.top+=dh;r.bottom+=dh;}
    return r.Offset(dx,dy);
}
Rect DesktopShell::ControlBounds(int target) const {
    const auto& w=panelWindows_[size_t(panel_)].bounds;
    const auto caption=[](Rect rect,int slot){return Rect(rect.right-32-slot*26,rect.top+4,rect.right-8-slot*26,rect.top+26);};
    if(dialog_==Dialog::MediaPlayer){
        const auto& d=documentWindow_.bounds;const int left=d.left+14,right=d.right-14,bottom=d.bottom;
        if(target==MediaSeek)return {d.left+20,bottom-136,d.right-20,bottom-116};
        if(target==MediaChoose)return {left,bottom-48,left+112,bottom-12};
        if(target==MediaEffect)return {left+122,bottom-48,left+254,bottom-12};
        if(target==MediaVolumeDown)return {right-230,bottom-48,right-184,bottom-12};
        if(target==MediaVolumeUp)return {right-174,bottom-48,right-128,bottom-12};
        if(target==CancelDialog)return {right-112,bottom-48,right,bottom-12};
    }
    if(target==CancelDialog){const auto& d=CurrentWindow().bounds;return {d.right-188,d.bottom-42,d.right-22,d.bottom-6};}
    if(dialog_==Dialog::MediaPlayer && target>=MediaPlay && target<=MediaForward){
        const auto& d=documentWindow_.bounds;const int width=(d.Width()-42-32)/5,x=d.left+20+(target-MediaPlay)*(width+8);
        return {x,d.bottom-104,x+width,d.bottom-64};
    }
    if(target==NativeTask)return guestActive_?Rect(476,684,786,716):Rect(158,684,468,716);
    if(target==Close || target==Maximize || target==Minimize)return caption(w,target==Close?0:target==Maximize?1:2);
    if(target==DialogClose || target==DialogMaximize || target==DialogMinimize)
        return caption(CurrentWindow().bounds,target==DialogClose?0:target==DialogMaximize?1:2);
    if(target>=TextLineBase && target<TextLineBase+32) {
        if(target>=TextLineBase+VisibleTextRows())return {};
        const auto& d=documentWindow_.bounds;const int y=d.top+104+(target-TextLineBase)*28;
        return {d.left+20,y,d.right-22,y+28};
    }
    const auto r=Bounds(target);
    if((target>=ControllerSettings && target<=BindingReset) || target==ConfirmDelete || target==CancelDialog || (target>=PreferenceSort && target<=PreferenceSave) ||
       (target>=TextEdit && target<=TextDiscard) || (target>=MediaPlay && target<=MediaForward) || target==MediaChoose || target==MediaVolumeDown || target==MediaVolumeUp || target==ImageFit || target==ImageActual ||
       (target>=TreeOpen && target<=TreeCollapse) || (target>=TreeRowBase && target<TreeRowBase+7))return DialogLayout(r);
    if(target>=BrowserRowBase && target<BrowserRowBase+Rows) {
        if(target>=BrowserRowBase+VisibleRows())return {};
        const auto content=ContentBounds();const int col=(target-BrowserRowBase)%2,row=(target-BrowserRowBase)/2;
        const int step=std::min(352,(content.Width()-18)/2),x=content.left+9+col*step,y=content.top+8+row*72;
        return {x,y,x+step-8,y+68};
    }
    if(target>=RowBase && target<RowBase+Rows) {
        if(target>=RowBase+VisibleRows())return {};
        const auto content=ContentBounds();const int y=content.top+8+(target-RowBase)*36;
        return {content.left+9,y,content.right-8,y+34};
    }
    switch(target) {
    case Launch:case OpenWith:case NewFolder:case Rename:case Copy:case Paste:case Delete:case Properties:
    case QuickComputer:case QuickDocuments:case QuickC:case QuickS:case QuickR:
    case NavBack:case NavUp:case FolderTreeButton:case FileActions:case FolderActions:case Sort:
        return r.Offset(w.left-156,w.top-8);
    case Address:return {w.left+60,w.top+91,w.right-100,w.top+115};
    case Refresh:return {w.right-96,w.top+91,w.right-6,w.top+115};
    case Previous:return {w.right-88,w.bottom-27,w.right-50,w.bottom-7};
    case Next:return {w.right-46,w.bottom-27,w.right-8,w.bottom-7};
    default:return r; // desktop, Start and taskbar retain physical logical positions
    }
}
int DesktopShell::WindowHit(int x,int y) const {
    const bool dialog=dialog_!=Dialog::None;
    const auto r=dialog?DialogBounds():PanelBounds();
    if(r.Empty() || !r.Contains(x,y))return None;
    const std::array<int,3> buttons=dialog?std::array<int,3>{DialogClose,DialogMaximize,DialogMinimize}:std::array<int,3>{Close,Maximize,Minimize};
    for(int target:buttons)
        if(Enabled(target) && ControlBounds(target).Contains(x,y))return target;
    const bool resizable=!dialog || DocumentWindow();
    if(!CurrentWindow().maximized && resizable) {
        const bool l=x<r.left+5,rt=x>=r.right-5,t=y<r.top+5,b=y>=r.bottom-5;
        if(l && t)return ResizeBase+3;
        if(rt && t)return ResizeBase+4;
        if(l && b)return ResizeBase+6;
        if(rt && b)return ResizeBase+7;
        if(l)return ResizeBase;
        if(rt)return ResizeBase+1;
        if(t)return ResizeBase+2;
        if(b)return ResizeBase+5;
    }
    if(!CurrentWindow().maximized && y<r.top+(dialog?34:30))return TitleBar;
    return None;
}
void DesktopShell::CancelPointer() {
    clickIdentity_.clear();
    if(DraggingWindow()) {
        auto& w=dragDialog_?(DocumentWindow()?documentWindow_:modalWindow_):panelWindows_[size_t(panel_)];
        w.bounds=w.normal=dragOriginal_;dragTarget_=None;ReflowFocus();++visualRevision_;
    }
    pressed_=None;secondaryTarget_=-2;
}
void DesktopShell::ReflowFocus() {
    if(dialog_==Dialog::None && panel_==Panel::Explorer)FocusBrowserSelection();
    else if(dialog_==Dialog::None && panel_==Panel::Programs && !files_.empty())focus_=RowBase+int(selected_%VisibleRows());
    imagePanX_=std::max(0,std::min(imagePanX_,std::max(0,image_.Width()-DocumentViewport().Width())));
    imagePanY_=std::max(0,std::min(imagePanY_,std::max(0,image_.Height()-DocumentViewport().Height())));
}
void DesktopShell::ToggleNativeMaximize() {
    CancelPointer();auto& w=CurrentWindow();
    if(w.maximized){w.bounds=w.normal;w.maximized=false;}
    else {w.normal=w.bounds;w.bounds={0,0,Width,680};w.maximized=true;}
    ReflowFocus();++visualRevision_;
}
void DesktopShell::ToggleNativeTask() {
    CancelPointer();CloseContext();startOpen_=false;nativeMinimized_=!nativeMinimized_;
    if(nativeMinimized_)focus_=NativeTask;
    else if(TextDialog())focus_=TextEdit;
    else if(dialog_!=Dialog::None)focus_=CancelDialog;
    else if(panel_==Panel::Explorer)FocusBrowserSelection();
    else focus_=Close;
    ++visualRevision_;
}
} // namespace ui
