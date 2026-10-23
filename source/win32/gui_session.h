#pragma once
#include "user32.h"
#include "gdi.h"

namespace win32::user {
// Active USER/GDI globals are a compatibility facade. Exchange their owned
// storage only outside guest execution/presentation; State itself never moves
// because window-layer capture records contain pointers into that object.
void ExchangeGuiState(std::unique_ptr<State>& other);
class GuiSession {
public:
    GuiSession();
    ~GuiSession();
    GuiSession(const GuiSession&)=delete;
    GuiSession& operator=(const GuiSession&)=delete;
private:
    std::unique_ptr<State> previous_;
    ui::Gdi previousGdi_;
    gdi::GdiSystem previousLegacyGdi_;
};
}
