#pragma once
// window_manager.h
//
// Минимальный менеджер оконных классов/окон. Пока НЕ реализует message loop
// (это следующий шаг) — только регистрацию класса (RegisterClassW) и
// создание окна (CreateWindowExW). Главное, что мы тут сохраняем — адрес
// WndProc из гостевого кода: он понадобится, когда будем реализовывать
// DispatchMessage и реально вызывать оконную процедуру игры.

#include <cstdint>
#include <string>
#include <unordered_map>

namespace win32 {

constexpr uint32_t HWND_BASE = 0xB0000000u;

struct WindowClassInfo {
    uint32_t wndProc = 0;       // адрес WndProc в гостевом коде — ключевое поле
    uint32_t hIcon = 0;
    uint32_t hCursor = 0;
    uint32_t hbrBackground = 0;
};

struct WindowInfo {
    std::string className;
    int x = 0, y = 0, width = 0, height = 0;
    uint32_t hInstance = 0;
    uint32_t parentHwnd = 0; // 0 = нет родителя (top-level окно)
};

class WindowManager {
public:
    // Возвращает ненулевой "атом" при успехе (реальный контракт RegisterClassW)
    uint32_t RegisterClass(const std::string& className, const WindowClassInfo& info);
    const WindowClassInfo* FindClass(const std::string& className) const;

    // Возвращает фейковый HWND, либо 0 — если класс с таким именем не зарегистрирован.
    // parentHwnd=0 — top-level окно (как раньше), ненулевой — child-окно
    // (добавлено при доводке связки CFrameWnd::OnCreateClient/CreateView).
    uint32_t CreateWindow(const std::string& className, int x, int y, int w, int h,
                           uint32_t hInstance, uint32_t parentHwnd = 0);

    const WindowInfo* GetWindow(uint32_t hwnd) const;
    const WindowClassInfo* GetWindowClass(uint32_t hwnd) const; // удобный шорткат для DispatchMessage

    // Обновляет геометрию уже существующего окна. false, если такого HWND нет.
    bool MoveWindow(uint32_t hwnd, int x, int y, int w, int h);

    // Хендл ПОСЛЕДНЕГО созданного окна — для простых однооконных приложений
    // (как сапёр) этого достаточно, чтобы понять, куда слать клики мыши.
    // 0, если ни одного окна ещё не создано.
    uint32_t GetLastCreatedWindow() const { return lastCreatedWindow_; }

private:
    std::unordered_map<std::string, WindowClassInfo> classes_;
    std::unordered_map<uint32_t, WindowInfo> windows_;
    uint32_t nextHandle_ = HWND_BASE;
    uint32_t nextAtom_ = 0xC001; // произвольный, но правдоподобный диапазон ATOM
    uint32_t lastCreatedWindow_ = 0;
};

extern WindowManager g_windowManager;

} // namespace win32
