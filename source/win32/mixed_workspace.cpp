#include "mixed_workspace.h"

namespace win32::user {
MixedWorkspace::MixedWorkspace(WindowGpuBindings& b,ShellSession& s,Output o,void* c,ui::PresentationAllocator* a)
    :bindings_(b),shell_(s),output_(o),context_(c),frame_(a) {}
MixedWorkspace::~MixedWorkspace(){Detach();}
bool MixedWorkspace::Attach() {
    if(attached_ || S().presentationFault || !output_ || !shell_.Ready() || !bindings_.PresentationReady() || g_platform.tryPresent)return false;
    previous_=g_platform;fault_=false;frames_=0;
    g_platform.presentationContext=this;g_platform.presentationIdentity=ui::NewPresentationResourceId();
    g_platform.presentationRevision=[](void* p){return static_cast<MixedWorkspace*>(p)->bindings_.PresentationRevision();};
    g_platform.tryPresent=[](void* p){return static_cast<MixedWorkspace*>(p)->Present();};
    attached_=true;return true;
}
void MixedWorkspace::Detach() {
    if(attached_ && g_platform.presentationContext==this) {
        g_platform.presentationContext=previous_.presentationContext;
        g_platform.presentationIdentity=previous_.presentationIdentity;
        g_platform.presentationRevision=previous_.presentationRevision;
        g_platform.tryPresent=previous_.tryPresent;
    }
    attached_=false;frame_.Clear();
}
NativePresentResult MixedWorkspace::Present() {
    if(!attached_ || fault_)return NativePresentResult::Failed;
    if(!bindings_.PresentationReady())return NativePresentResult::Unavailable;
    const auto overlay=shell_.NativeOverlay();
    if(!overlay.owners || !bindings_.SubmitMixed(frame_,&overlay) || !output_(context_,bindings_,frame_)) {
        fault_=true;return NativePresentResult::Failed;
    }
    ++frames_;return NativePresentResult::Submitted;
}
} // namespace win32::user
