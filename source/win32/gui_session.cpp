#include "gui_session.h"
#include <utility>

namespace win32::user {
GuiSession::GuiSession():previous_(std::make_unique<State>()) {
    ExchangeGuiState(previous_);
    std::swap(ui::g_gdi,previousGdi_);
    std::swap(gdi::g_gdi,previousLegacyGdi_);
    S().screenW=previous_->screenW;S().screenH=previous_->screenH;
    // EnableUi is also called before GuestSession by native frontends.
    if(previous_->initialized)Init();
}
GuiSession::~GuiSession() {
    // Guest continuations and presentation are already drained. No guest
    // destruction callback runs here; discard host objects while memory lives.
    std::swap(ui::g_gdi,previousGdi_);
    std::swap(gdi::g_gdi,previousLegacyGdi_);
    ExchangeGuiState(previous_);
}
}
