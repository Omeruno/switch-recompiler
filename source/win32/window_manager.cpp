#include "window_manager.h"

namespace win32 {

WindowManager g_windowManager;

uint32_t WindowManager::RegisterClass(const std::string& className, const WindowClassInfo& info) {
    auto stored = info;
    stored.atom = uint16_t(nextAtom_++);
    classes_[className] = stored;
    return stored.atom;
}

const WindowClassInfo* WindowManager::FindClass(const std::string& className) const {
    auto it = classes_.find(className);
    if (it == classes_.end()) return nullptr;
    return &it->second;
}

uint32_t WindowManager::CreateWindow(const std::string& className, int x, int y, int w, int h,
                                      uint32_t hInstance, uint32_t parentHwnd, const std::u16string& title) {
    if (!FindClass(className)) return 0; // класс не зарегистрирован — реальная Windows тоже откажет

    uint32_t handle = nextHandle_++;
    WindowInfo info;
    info.className = className;
    info.title = title;
    info.x = x; info.y = y; info.width = w; info.height = h;
    info.hInstance = hInstance;
    info.parentHwnd = parentHwnd;
    windows_[handle] = info;
    zOrder_.insert(zOrder_.begin(), handle);
    lastCreatedWindow_ = handle;
    return handle;
}

std::string WindowManager::ClassNameFromAtom(uint16_t atom) const {
    for (const auto& entry : classes_) if (entry.second.atom == atom) return entry.first;
    return {};
}

std::vector<uint32_t> WindowManager::Children(uint32_t parent) const {
    std::vector<uint32_t> result;
    for (auto h : zOrder_) {
        const auto* w = GetWindow(h);
        if (w && w->parentHwnd == parent) result.push_back(h);
    }
    return result;
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
