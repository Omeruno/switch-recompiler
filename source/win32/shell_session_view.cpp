#include "shell_session.h"
namespace win32::user {
Rect ShellSession::PresentationRect(){
    const Rect full{0,0,S().screenW,S().screenH};
    if(!viewHwnd_)return full;
    auto* w=Win(viewHwnd_);
    if(w && w->rasterIdentity==viewIdentity_ && IsVisibleChain(w) && !(w->style&WS_MINIMIZE)){
        const auto rect=ScreenRect(w).Intersect(full);if(!rect.Empty())return rect;
    }
    viewHwnd_=0;viewIdentity_=0;++revision_;return full;
}
void ShellSession::EnterFocusedView(){
    // Experimental resident frames have a separate coordinate/ownership path.
    if(g_platform.tryPresent)return;
    auto* w=Win(S().foreground);
    for(unsigned n=0;w && (w->style&WS_CHILD) && n<64;++n)w=Win(w->parent);
    if(w && (w->style&WS_CHILD))return;
    if(!w || w->hwnd==S().desktop || !IsVisibleChain(w) || (w->style&WS_MINIMIZE))return;
    viewHwnd_=w->hwnd;viewIdentity_=w->rasterIdentity;keyboard_=false;
    PresentationRect();
}
}
