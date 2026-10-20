#pragma once
// user32_controls.h — общее для встроенных элементов управления USER32
// (user32_controls.cpp, user32_lists.cpp) и изображений (user32_image.cpp).

#include <string>
#include <vector>

#include "user32.h"

namespace win32 {
namespace user {

struct ListItem {
    std::u16string text;
    uint32_t data = 0;
    bool selected = false;
    int height = 0;
};

// Состояние встроенного элемента (Window::controlState)
struct Ctl {
    uint32_t font = 0;
    int check = 0;
    bool pushed = false, captured = false, defBtn = false;
    uint32_t image = 0;
    // поле ввода
    size_t caret = 0, anchor = 0;
    int scrollX = 0, topLine = 0;
    size_t limit = 30000;
    bool modified = false;
    char16_t passChar = 0;
    std::u16string undo;
    // список
    std::vector<ListItem> items;
    int cur = -1, top = 0, itemH = 0, anchorItem = -1, caretItem = 0;
    bool redraw = true;
    // комбинированный список
    bool dropped = false;
    uint32_t listHwnd = 0;
    uint32_t comboOwner = 0;   // у выпадающего списка — его комбинированный
    int dropHeight = 0;
    // полоса прокрутки (элемент ScrollBar)
    int sbMin = 0, sbMax = 100, sbPage = 0, sbPos = 0, sbTrack = 0;
    bool sbDisabled = false;
};

Ctl* C(Window* w);
uint32_t CtlFont(Window* w);
uint32_t CtlDc(uint32_t hwnd);                         // контекст клиентской области со шрифтом элемента
uint32_t CtlColor(uint32_t hwnd, uint32_t hdc, uint32_t msg); // WM_CTLCOLOR* у родителя
void Notify(uint32_t hwnd, uint32_t code);              // WM_COMMAND родителю
void Redraw(uint32_t hwnd);
void Line(uint32_t hdc, int x0, int y0, int x1, int y1, int sysColor);
void Edge3D(uint32_t hdc, Rect r, int tlO, int brO, int tlI, int brI);
void FocusRect(uint32_t hdc, const Rect& r);
void CtlText(uint32_t hdc, const std::u16string& text, Rect r, uint32_t fmt, bool enabled, uint32_t color);
bool CommonCtlMessage(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, uint32_t& result);

uint32_t ButtonProc(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool unicode);
uint32_t StaticProc(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool unicode);
uint32_t EditProc(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool unicode);
uint32_t ListBoxProc(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool unicode);
uint32_t ComboBoxProc(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool unicode);
uint32_t ScrollBarProc(uint32_t hwnd, uint32_t msg, uint32_t wp, uint32_t lp, bool unicode);

// Иконки и картинки (user32_image.cpp)
uint32_t LoadImageForStatic(uint32_t hInst, uint16_t id, const std::u16string& name, bool bitmap);
void DrawImageHandle(uint32_t hdc, uint32_t handle, int x, int y, bool bitmap);

} // namespace user
} // namespace win32

namespace ui {
// Цвет текста, установленный в контексте (например, обработчиком WM_CTLCOLOR*)
inline uint32_t DcColorOr(uint32_t hdc, uint32_t fallback) {
    DcObj* dc = g_gdi.Dc(hdc);
    return dc ? dc->st.textColor : fallback;
}
} // namespace ui
