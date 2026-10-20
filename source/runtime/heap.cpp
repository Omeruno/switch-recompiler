// heap.cpp — куча гостя (см. heap.h)

#include "heap.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace rt {

namespace {
GuestHeap* g_processHeap = nullptr;
}

GuestHeap& ProcessHeap() {
    if (!g_processHeap) {
        std::fprintf(stderr, "[FATAL] куча процесса не создана\n");
        std::exit(1);
    }
    return *g_processHeap;
}
void SetProcessHeap(GuestHeap* heap) { g_processHeap = heap; }

GuestHeap::GuestHeap(cpu::PagedGuestMemory& mem, uint32_t initialArena) : mem_(mem), arenaSize_(initialArena) {}

void GuestHeap::InsertFree(uint32_t addr, uint32_t size) {
    freeByAddr_[addr] = size;
    freeBySize_.emplace(size, addr);
}

void GuestHeap::EraseFree(uint32_t addr) {
    auto it = freeByAddr_.find(addr);
    if (it == freeByAddr_.end()) return;
    auto range = freeBySize_.equal_range(it->second);
    for (auto s = range.first; s != range.second; ++s) {
        if (s->second == addr) { freeBySize_.erase(s); break; }
    }
    freeByAddr_.erase(it);
}

bool GuestHeap::Grow(uint32_t minSize) {
    uint64_t want = static_cast<uint64_t>(minSize) + kAlign;
    uint32_t size = arenaSize_;
    while (size < want && size < 0x40000000u) size *= 2;
    if (size < want) return false;
    uint32_t base = mem_.Allocate(size, cpu::kProtRW);
    if (!base) {
        // меньше не получится, если и под ровно нужный размер места нет
        size = static_cast<uint32_t>((want + 0xFFFF) & ~0xFFFFull);
        base = mem_.Allocate(size, cpu::kProtRW);
        if (!base) return false;
    }
    arenas_[base] = size;
    InsertFree(base, size);
    // следующие арены покрупнее, чтобы их было немного
    if (arenaSize_ < 0x1000000u) arenaSize_ *= 2;
    return true;
}

uint32_t GuestHeap::Alloc(uint32_t size, bool zero) {
    uint64_t need64 = (static_cast<uint64_t>(size ? size : 1) + kAlign - 1) & ~static_cast<uint64_t>(kAlign - 1);
    if (need64 > 0x7FFF0000u) return 0;
    uint32_t need = static_cast<uint32_t>(need64);
    auto it = freeBySize_.lower_bound(need);
    if (it == freeBySize_.end()) {
        if (!Grow(need)) return 0;
        it = freeBySize_.lower_bound(need);
        if (it == freeBySize_.end()) return 0;
    }
    uint32_t addr = it->second, blockSize = it->first;
    EraseFree(addr);
    if (blockSize > need) InsertFree(addr + need, blockSize - need);
    used_[addr] = need;
    requested_[addr] = size;
    bytesInUse_ += need;
    if (zero) {
        for (uint32_t off = 0; off < need;) {
            uint8_t* p = mem_.HostPointer(addr + off);
            uint32_t chunk = cpu::PagedGuestMemory::kPageSize - ((addr + off) & cpu::PagedGuestMemory::kPageMask);
            if (chunk > need - off) chunk = need - off;
            if (p) std::memset(p, 0, chunk);
            off += chunk;
        }
    }
    return addr;
}

bool GuestHeap::Free(uint32_t addr) {
    auto it = used_.find(addr);
    if (it == used_.end()) return false;
    uint32_t size = it->second;
    used_.erase(it);
    requested_.erase(addr);
    bytesInUse_ -= size;
    // слияние с соседями, если они свободны и в той же арене
    auto arena = arenas_.upper_bound(addr);
    --arena;
    uint32_t arenaStart = arena->first, arenaEnd = arena->first + arena->second;
    auto next = freeByAddr_.find(addr + size);
    if (next != freeByAddr_.end() && addr + size < arenaEnd) {
        uint32_t nsize = next->second;
        EraseFree(addr + size);
        size += nsize;
    }
    auto prev = freeByAddr_.lower_bound(addr);
    if (prev != freeByAddr_.begin()) {
        --prev;
        if (prev->first + prev->second == addr && prev->first >= arenaStart) {
            uint32_t paddr = prev->first, psize = prev->second;
            EraseFree(paddr);
            addr = paddr;
            size += psize;
        }
    }
    InsertFree(addr, size);
    return true;
}

uint32_t GuestHeap::Size(uint32_t addr) const {
    auto it = requested_.find(addr);
    return it == requested_.end() ? 0xFFFFFFFFu : it->second;
}

uint32_t GuestHeap::Realloc(uint32_t addr, uint32_t size, bool zeroNew, bool inPlaceOnly) {
    if (!addr) return inPlaceOnly ? 0 : Alloc(size, zeroNew);
    auto it = used_.find(addr);
    if (it == used_.end()) return 0;
    uint32_t oldReq = requested_[addr];
    uint32_t have = it->second;
    uint64_t need64 = (static_cast<uint64_t>(size ? size : 1) + kAlign - 1) & ~static_cast<uint64_t>(kAlign - 1);
    uint32_t need = static_cast<uint32_t>(need64);
    if (need <= have) {
        // уменьшение на месте: хвост отдаём в свободные
        if (have - need >= 64) {
            used_[addr] = need;
            bytesInUse_ -= have - need;
            uint32_t tail = addr + need, tailSize = have - need;
            used_[tail] = tailSize;
            requested_[tail] = tailSize;
            bytesInUse_ += tailSize;
            Free(tail);
        }
        requested_[addr] = size;
        if (zeroNew && size > oldReq) for (uint32_t i = oldReq; i < size; ++i) mem_.Write8(addr + i, 0);
        return addr;
    }
    // попробовать расшириться в соседний свободный блок
    auto next = freeByAddr_.find(addr + have);
    if (next != freeByAddr_.end() && have + next->second >= need) {
        uint32_t nsize = next->second;
        EraseFree(addr + have);
        uint32_t total = have + nsize;
        if (total > need) InsertFree(addr + need, total - need);
        used_[addr] = need;
        bytesInUse_ += need - have;
        requested_[addr] = size;
        if (zeroNew) for (uint32_t i = oldReq; i < size; ++i) mem_.Write8(addr + i, 0);
        return addr;
    }
    if (inPlaceOnly) return 0;
    uint32_t n = Alloc(size, zeroNew);
    if (!n) return 0;
    uint32_t copy = oldReq < size ? oldReq : size;
    std::vector<uint8_t> tmp(copy);
    mem_.ReadBytes(addr, tmp.data(), copy);
    mem_.LoadBytes(n, tmp.data(), copy);
    Free(addr);
    return n;
}

void GuestHeap::Destroy() {
    for (auto& a : arenas_) mem_.Unmap(a.first, a.second);
    arenas_.clear();
    used_.clear();
    requested_.clear();
    freeByAddr_.clear();
    freeBySize_.clear();
    bytesInUse_ = 0;
}

} // namespace rt
