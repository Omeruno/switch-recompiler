#pragma once
#include "executable_catalog.h"
#include "region.h"
#include "surface.h"
#include <string>
#include <vector>

namespace ui {
// Host-owned idle shell. No Process, HWND, guest pointers or GL context.
// Fixed logical coordinates; presentation and pointer use the same FitViewport.
class DesktopShell {
public:
    static constexpr int Width=1280,Height=720,Rows=14,RowBase=100;
    enum Target {None=0,Start=1,Programs=2,Computer=3,Documents=4,
                 MenuPrograms=5,MenuComputer=6,MenuDocuments=7,MenuRun=8,
                 About=9,Exit=10,Close=11,Launch=12,Previous=13,Next=14};
    enum class Panel {Desktop,Programs,About};
    void SetCatalog(const ExecutableCatalog&);
    void SetStatus(const std::string&);
    void Enter(); // Cancel transient presses/menu on return, retain panel/selection.
    void ToggleStart();
    void Move(int delta);
    void Page(int delta);
    void Accept();
    void Back();
    void CancelPointer() {pressed_=None;}
    void PointerMove(int x,int y);
    void PointerDown(int x,int y);
    void PointerUp(int x,int y);
    int HitTest(int x,int y) const;
    static Rect Bounds(int target);
    bool Enabled(int target) const;
    const std::string& LaunchPath() const {return launch_;}
    bool ExitRequested() const {return exit_;}
    Panel ActivePanel() const {return panel_;}
    bool StartOpen() const {return startOpen_;}
    size_t Selection() const {return selected_;}
    size_t Count() const {return files_.size();}
    int Focus() const {return focus_;}
    int Hover() const {return hover_;}
    // Safe copy of labels, bounded inventory and status; no borrowed catalog.
    void Render(Surface&,const std::u16string& clock) const;
private:
    void Activate(int target);
    size_t First() const {return selected_/Rows*Rows;}
    std::vector<int> FocusOrder() const;
    std::vector<std::string> files_;
    std::string launch_,status_;
    size_t selected_=0,errors_=0;
    bool truncated_=false,startOpen_=false,exit_=false;
    Panel panel_=Panel::Desktop;
    int focus_=Programs,menuReturnFocus_=Programs,hover_=None,pressed_=None;
};
} // namespace ui
