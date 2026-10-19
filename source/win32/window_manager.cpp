#include "window_manager.h"

namespace win32 {

WindowManager g_windowManager;

uint32_t WindowManager::RegisterClass(const std::string& className, const WindowClassInfo& info) {
    classes_[className] = info;
    return nextAtom_++;
}

const WindowClassInfo* WindowManager::FindClass(const std::string& className) const {
    auto it = classes_.find(className);
    if (it == classes_.end()) return nullptr;
    return &it->second;
}

uint32_t WindowManager::CreateWindow(const std::string& className, int x, int y, int w, int h,
                                      uint32_t hInstance, uint32_t parentHwnd) {
    if (!FindClass(className)) return 0; // класс не зарегистрирован — реальная Windows тоже откажет

    uint32_t handle = nextHandle_++;
    WindowInfo info;
    info.className = className;
    info.x = x; info.y = y; info.width = w; info.height = h;
    info.hInstance = hInstance;
    info.parentHwnd = parentHwnd;
    windows_[handle] = info;
    lastCreatedWindow_ = handle;
    return handle;
}

const WindowInfo* WindowManager::GetWindow(uint32_t hwnd) const {
    auto it = windows_.find(hwnd);
    if (it == windows_.end()) return nullptr;
    return &it->second;
}

const WindowClassInfo* WindowManager::GetWindowClass(uint32_t hwnd) const {
    const WindowInfo* win = GetWindow(hwnd);
    if (!win) return nullptr;
    return FindClass(win->className);
}

bool WindowManager::MoveWindow(uint32_t hwnd, int x, int y, int w, int h) {
    auto it = windows_.find(hwnd);
    if (it == windows_.end()) return false;
    it->second.x = x;
    it->second.y = y;
    it->second.width = w;
    it->second.height = h;
    return true;
}

} // namespace win32
