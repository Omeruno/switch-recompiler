#include "guest_session.h"
#include "../win32/host_functions.h"
#include "../win32/iat_patcher.h"
#include "../win32/user32.h"

namespace rt {
GuestSession::Context GuestSession::Capture() {
    Context c;c.process=g_process;c.binding=win32::ImportBindingMemory();
    c.guiCpu=win32::user::G().c;c.guiMemory=win32::user::G().m;
    c.exitRequested=win32::g_guestExitRequested;c.exitCode=win32::g_guestExitCode;
    c.crt=win32::g_crtGlobalsBase;c.heapBase=win32::g_heapBase;c.heapNext=win32::g_heapNext;
    c.mfc=win32::g_mfcStateBase;c.teb=win32::g_tebAddr;return c;
}
GuestSession::GuestSession():previous_(Capture()),process_(memory_) {
    g_process=&process_;win32::SetImportBindingMemory(&memory_);
    win32::user::G()={};win32::g_guestExitRequested=false;win32::g_guestExitCode=0;
    win32::g_crtGlobalsBase=win32::g_heapBase=win32::g_heapNext=win32::g_mfcStateBase=win32::g_tebAddr=0;
}
GuestSession::~GuestSession() {
    // GuestExecution has already drained. Host-only state/handles are destroyed
    // with Process, then backing guest memory. No guest callback is run here.
    g_process=previous_.process;win32::SetImportBindingMemory(previous_.binding);
    win32::user::G()={previous_.guiCpu,previous_.guiMemory};
    win32::g_guestExitRequested=previous_.exitRequested;win32::g_guestExitCode=previous_.exitCode;
    win32::g_crtGlobalsBase=previous_.crt;win32::g_heapBase=previous_.heapBase;
    win32::g_heapNext=previous_.heapNext;win32::g_mfcStateBase=previous_.mfc;win32::g_tebAddr=previous_.teb;
}
} // namespace rt
