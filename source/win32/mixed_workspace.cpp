#include "mixed_workspace.h"

namespace win32::user {
MixedWorkspace::MixedWorkspace(WindowGpuBindings& b,ShellSession& s,Output o,void* c,ui::PresentationAllocator* a)
    :bindings_(b),shell_(s),output_(o),context_(c),frame_(a) {}
MixedWorkspace::~MixedWorkspace(){Detach();}
bool MixedWorkspace::Attach() {
    if(attached_ || S().presentationFault || !output_ || !shell_.Ready() || !bindings_.PresentationReady() || g_platform.tryPresent)return false;
    previous_=g_platform;fault_=false;hasFrame_=false;frames_=submissions_=redraws_=0;
    g_platform.presentationContext=this;g_platform.presentationIdentity=ui::NewPresentationResourceId();
    g_platform.presentationRevision=[](void* p){return static_cast<MixedWorkspace*>(p)->bindings_.PresentationRevision();};
    g_platform.tryPresent=[](void* p){return static_cast<MixedWorkspace*>(p)->Refresh();};
    attached_=true;return true;
}
void MixedWorkspace::Detach() {
    if(attached_ && g_platform.presentationContext==this) {
        g_platform.presentationContext=previous_.presentationContext;
        g_platform.presentationIdentity=previous_.presentationIdentity;
        g_platform.presentationRevision=previous_.presentationRevision;
        g_platform.tryPresent=previous_.tryPresent;
    }
    attached_=false;hasFrame_=false;frame_.Clear();
}
bool MixedWorkspace::FrameCurrent() const {
    const auto shell=g_platform.shellRevision?g_platform.shellRevision(g_platform.shellContext):0;
    return attached_ && hasFrame_ && bindings_.PresentationReady() && !fault_ &&
        layoutGeneration_==ui::g_gdi.LayoutGeneration() && cpuRevision_==ui::g_gdi.ScreenChangeCounter() && gpuRevision_==bindings_.PresentationRevision() && shellRevision_==shell;
}
NativePresentResult MixedWorkspace::Refresh() {
    const auto result=Present();
    if(result==NativePresentResult::Failed)S().presentationFault=true;
    return result;
}
NativePresentResult MixedWorkspace::Redraw() {
    if(!FrameCurrent())return Refresh();
    if(busy_ || S().presentationFault)return NativePresentResult::Failed;
    busy_=true;const bool ok=output_(context_,bindings_,frame_);busy_=false;
    if(!ok){fault_=true;S().presentationFault=true;return NativePresentResult::Failed;}
    ++frames_;++redraws_;return NativePresentResult::Submitted;
}
NativePresentResult MixedWorkspace::Present() {
    if(!attached_ || fault_ || busy_ || S().presentationFault || ui::g_gdi.CpuAccessFault())return NativePresentResult::Failed;
    if(!bindings_.PresentationReady())return NativePresentResult::Unavailable;
    struct Scope {bool& busy;Scope(bool& b):busy(b){busy=true;}~Scope(){busy=false;}} scope(busy_);
    const auto overlay=shell_.NativeOverlay();
    if(!overlay.owners || !bindings_.SubmitMixed(frame_,&overlay)) {fault_=true;return NativePresentResult::Failed;}
    layoutGeneration_=ui::g_gdi.LayoutGeneration();cpuRevision_=ui::g_gdi.ScreenChangeCounter();gpuRevision_=bindings_.PresentationRevision();
    shellRevision_=g_platform.shellRevision?g_platform.shellRevision(g_platform.shellContext):0;
    hasFrame_=true;++submissions_;
    if(!output_(context_,bindings_,frame_)){fault_=true;return NativePresentResult::Failed;}
    ++frames_;return NativePresentResult::Submitted;
}
} // namespace win32::user
