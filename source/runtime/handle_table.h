#pragma once
// Guest HANDLE values identify references, not objects or thread IDs.
// One process owns the namespace. Numbers are never reused within its lifetime.
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace rt {
enum class ObjectKind { File, Thread, Event, Mutex, Semaphore, Timer };
struct KernelObject {
    explicit KernelObject(ObjectKind type) : type(type) {}
    virtual ~KernelObject() = default;
    const ObjectKind type;
};
struct ThreadObject : KernelObject {
    explicit ThreadObject(uint32_t id) : KernelObject(ObjectKind::Thread), threadId(id) {}
    uint32_t threadId;
};
struct SyncObject : KernelObject {
    explicit SyncObject(ObjectKind type) : KernelObject(type) {}
    bool manualReset = false, signaled = false, abandoned = false;
    int32_t count = 0, maxCount = 0;
    uint32_t ownerThread = 0, recursion = 0;
};
struct TimerObject : SyncObject {
    TimerObject() : SyncObject(ObjectKind::Timer) {}
    bool active=false, apcQueued=false;
    uint64_t dueUs=0, absoluteTime=0, generation=0;
    uint32_t periodMs=0, callbackThread=0, routine=0, data=0;
};
constexpr uint32_t kSynchronize = 0x00100000u;
constexpr uint32_t kHandleInherit = 1, kHandleProtectFromClose = 2;
uint32_t AllObjectAccess(ObjectKind kind);
uint32_t MapObjectAccess(ObjectKind kind,uint32_t access);
struct HandleEntry {
    std::shared_ptr<KernelObject> object;
    uint32_t access=0,flags=0;
};
class HandleTable {
public:
    HandleTable();
    uint32_t Add(std::shared_ptr<KernelObject> object,uint32_t access=0xffffffffu,uint32_t flags=0);
    const HandleEntry* Entry(uint32_t handle) const;
    std::shared_ptr<KernelObject> Get(uint32_t handle) const;
    bool HasAccess(uint32_t handle,uint32_t required) const;
    bool SetFlags(uint32_t handle,uint32_t mask,uint32_t flags);
    bool Close(uint32_t handle);
    uint32_t Duplicate(uint32_t handle);
    std::vector<uint32_t> Aliases(const KernelObject* object) const;
    std::vector<std::shared_ptr<KernelObject>> Objects() const;
    std::vector<std::shared_ptr<TimerObject>> Timers();
    std::shared_ptr<KernelObject> FindNamed(const std::u16string& name);
    void BindName(const std::u16string& name,const std::shared_ptr<KernelObject>& object);
private:
    std::map<uint32_t, HandleEntry> handles_;
    // Single emulated process/session only; names never keep objects alive.
    std::map<std::u16string,std::weak_ptr<KernelObject>> names_;
    std::vector<std::weak_ptr<TimerObject>> timers_;
    uint32_t next_ = 0xD0000100u; // reserved kernel-object range, separate from GUI/find/registry
};
// Process table for v2; isolated fallback for v1 / standalone HLE tests.
HandleTable& Handles();
// Notify every alias of an object. Closing a handle only wakes that handle.
void SignalObject(const KernelObject* object);
bool CloseGuestHandle(uint32_t handle);
} // namespace rt
