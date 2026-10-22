#include "handle_table.h"
#include "files.h"
#include "process.h"
#include "waitable_timer.h"
#include <set>

namespace rt {
uint32_t AllObjectAccess(ObjectKind kind) {
    switch(kind) {
        case ObjectKind::File:return 0x1f01ff;
        case ObjectKind::Thread:return 0x1f03ff; // XP, not Vista THREAD_ALL_ACCESS
        case ObjectKind::Mutex:return 0x1f0001;
        default:return 0x1f0003;
    }
}
uint32_t MapObjectAccess(ObjectKind kind,uint32_t access) {
    uint32_t read=0x20000,write=0x20000,execute=0x120000;
    switch(kind) {
        case ObjectKind::File:read=0x120089;write=0x120116;execute=0x1200a0;break;
        case ObjectKind::Thread:read|=0x48;write|=0x333;break;
        case ObjectKind::Mutex:write|=1;break;
        default:read|=1;write|=2;break;
    }
    uint32_t mapped=access & 0x0fffffffu;
    if(access & 0x80000000u) mapped|=read;
    if(access & 0x40000000u) mapped|=write;
    if(access & 0x20000000u) mapped|=execute;
    if(access & 0x10000000u) mapped|=AllObjectAccess(kind);
    // No token/DACL model yet: MAXIMUM_ALLOWED means supported type rights.
    if(mapped & 0x02000000u) mapped=(mapped & ~0x02000000u)|AllObjectAccess(kind);
    return mapped;
}
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
    handles_[FileTable::kStdIn] = {in,MapObjectAccess(ObjectKind::File,0x80000000),0};
    handles_[FileTable::kStdOut] = {out,MapObjectAccess(ObjectKind::File,0x40000000),0};
    handles_[FileTable::kStdErr] = {err,MapObjectAccess(ObjectKind::File,0x40000000),0};
}
uint32_t HandleTable::Add(std::shared_ptr<KernelObject> object,uint32_t access,uint32_t flags) {
    if (!object || next_ >= 0xDFFFFFFCu) return 0;
    uint32_t handle = next_;
    next_ += 4;
    if(access==0xffffffffu) access=AllObjectAccess(object->type);
    else access=MapObjectAccess(object->type,access);
    if(object->type==ObjectKind::Timer) {
        auto timer=std::static_pointer_cast<TimerObject>(object);
        bool tracked=false;
        for(const auto& existing:timers_) if(existing.lock()==timer) { tracked=true;break; }
        if(!tracked) timers_.push_back(timer);
    }
    handles_.emplace(handle,HandleEntry{std::move(object),access,flags&3});
    return handle;
}
const HandleEntry* HandleTable::Entry(uint32_t handle) const {
    auto it = handles_.find(handle);
    return it==handles_.end() ? nullptr : &it->second;
}
std::shared_ptr<KernelObject> HandleTable::Get(uint32_t handle) const {
    auto* entry=Entry(handle);return entry ? entry->object : nullptr;
}
bool HandleTable::HasAccess(uint32_t handle,uint32_t required) const {
    auto* entry=Entry(handle);return entry && (entry->access&required)==required;
}
bool HandleTable::SetFlags(uint32_t handle,uint32_t mask,uint32_t flags) {
    auto it=handles_.find(handle);if(it==handles_.end()) return false;
    mask&=3;it->second.flags=(it->second.flags&~mask)|(flags&mask);return true;
}
bool HandleTable::Close(uint32_t handle) {
    auto* entry=Entry(handle);
    if(!entry || (entry->flags&kHandleProtectFromClose)) return false;
    // Preserve the existing runtime convention for standard streams.
    if (handle == FileTable::kStdIn || handle == FileTable::kStdOut ||
        handle == FileTable::kStdErr) return true;
    return handles_.erase(handle) != 0;
}
uint32_t HandleTable::Duplicate(uint32_t handle) {
    auto* entry=Entry(handle);
    return entry ? Add(entry->object,entry->access,entry->flags&kHandleInherit) : 0;
}
std::vector<uint32_t> HandleTable::Aliases(const KernelObject* object) const {
    std::vector<uint32_t> result;
    for (const auto& entry : handles_)
        if (entry.second.object.get() == object) result.push_back(entry.first);
    return result;
}
std::vector<std::shared_ptr<KernelObject>> HandleTable::Objects() const {
    std::vector<std::shared_ptr<KernelObject>> result;
    std::set<const KernelObject*> seen;
    for (const auto& entry : handles_)
        if (seen.insert(entry.second.object.get()).second) result.push_back(entry.second.object);
    return result;
}
std::vector<std::shared_ptr<TimerObject>> HandleTable::Timers() {
    std::vector<std::shared_ptr<TimerObject>> result;
    for(auto it=timers_.begin();it!=timers_.end();) {
        if(auto timer=it->lock()) { result.push_back(std::move(timer));++it; }
        else it=timers_.erase(it);
    }
    return result;
}
std::shared_ptr<KernelObject> HandleTable::FindNamed(const std::u16string& name) {
    auto it=names_.find(name);if(it==names_.end()) return nullptr;
    auto object=it->second.lock();if(!object) names_.erase(it);return object;
}
void HandleTable::BindName(const std::u16string& name,const std::shared_ptr<KernelObject>& object) {
    for(auto it=names_.begin();it!=names_.end();)
        if(it->second.expired()) it=names_.erase(it);else ++it;
    names_[name]=object;
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
    auto object=Handles().Get(handle);
    if (!Handles().Close(handle)) return false;
    if(object && object->type==ObjectKind::Timer && Handles().Aliases(object.get()).empty())
        CancelTimer(*std::static_pointer_cast<TimerObject>(object),g_process ? &g_process->Scheduler() : nullptr);
    // Existing pending-close policy: retry and report WAIT_FAILED. Windows
    // leaves closing an active wait handle undefined; never hang the host.
    if (g_process) g_process->Scheduler().Signal(handle, true);
    return true;
}
} // namespace rt
