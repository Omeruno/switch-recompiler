#include "handle_table.h"
#include "files.h"
#include "process.h"
#include <set>

namespace rt {
HandleTable::HandleTable() {
    auto in = std::make_shared<OpenFile>();
    in->kind = OpenFile::ConsoleIn;
    in->canRead = true;
    auto out = std::make_shared<OpenFile>();
    out->kind = OpenFile::ConsoleOut;
    out->canWrite = true;
    auto err = std::make_shared<OpenFile>();
    err->kind = OpenFile::ConsoleErr;
    err->canWrite = true;
    handles_[FileTable::kStdIn] = in;
    handles_[FileTable::kStdOut] = out;
    handles_[FileTable::kStdErr] = err;
}
uint32_t HandleTable::Add(std::shared_ptr<KernelObject> object) {
    if (!object || next_ >= 0xDFFFFFFCu) return 0;
    uint32_t handle = next_;
    next_ += 4;
    handles_.emplace(handle, std::move(object));
    return handle;
}
std::shared_ptr<KernelObject> HandleTable::Get(uint32_t handle) const {
    auto it = handles_.find(handle);
    return it == handles_.end() ? nullptr : it->second;
}
bool HandleTable::Close(uint32_t handle) {
    // Preserve the existing runtime convention for standard streams.
    if (handle == FileTable::kStdIn || handle == FileTable::kStdOut ||
        handle == FileTable::kStdErr) return true;
    return handles_.erase(handle) != 0;
}
uint32_t HandleTable::Duplicate(uint32_t handle) { return Add(Get(handle)); }
std::vector<uint32_t> HandleTable::Aliases(const KernelObject* object) const {
    std::vector<uint32_t> result;
    for (const auto& entry : handles_)
        if (entry.second.get() == object) result.push_back(entry.first);
    return result;
}
std::vector<std::shared_ptr<KernelObject>> HandleTable::Objects() const {
    std::vector<std::shared_ptr<KernelObject>> result;
    std::set<const KernelObject*> seen;
    for (const auto& entry : handles_)
        if (seen.insert(entry.second.get()).second) result.push_back(entry.second);
    return result;
}
HandleTable& Handles() {
    if (g_process) return g_process->Handles();
    static HandleTable standalone;
    return standalone;
}
void SignalObject(const KernelObject* object) {
    if (!g_process) return;
    for (uint32_t handle : Handles().Aliases(object))
        g_process->Scheduler().Signal(handle, true);
}
bool CloseGuestHandle(uint32_t handle) {
    if (!Handles().Close(handle)) return false;
    // Existing pending-close policy: retry and report WAIT_FAILED. Windows
    // leaves closing an active wait handle undefined; never hang the host.
    if (g_process) g_process->Scheduler().Signal(handle, true);
    return true;
}
} // namespace rt
