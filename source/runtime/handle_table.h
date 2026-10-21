#pragma once
// Guest HANDLE values identify references, not objects or thread IDs.
// One process owns the namespace. Numbers are never reused within its lifetime.
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace rt {
enum class ObjectKind { File, Thread, Event, Mutex, Semaphore };
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
class HandleTable {
public:
    HandleTable();
    uint32_t Add(std::shared_ptr<KernelObject> object);
    std::shared_ptr<KernelObject> Get(uint32_t handle) const;
    bool Close(uint32_t handle);
    uint32_t Duplicate(uint32_t handle);
    std::vector<uint32_t> Aliases(const KernelObject* object) const;
    std::vector<std::shared_ptr<KernelObject>> Objects() const;
private:
    std::map<uint32_t, std::shared_ptr<KernelObject>> handles_;
    uint32_t next_ = 0xD0000100u; // reserved kernel-object range, separate from GUI/find/registry
};
// Process table for v2; isolated fallback for v1 / standalone HLE tests.
HandleTable& Handles();
// Notify every alias of an object. Closing a handle only wakes that handle.
void SignalObject(const KernelObject* object);
bool CloseGuestHandle(uint32_t handle);
} // namespace rt
