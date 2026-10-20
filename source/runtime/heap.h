#pragma once
// heap.h
//
// Куча в памяти гостя (HeapAlloc, malloc, LocalAlloc, GlobalAlloc, ...).
//
// Память берётся у PagedGuestMemory кусками («аренами»), а вся служебная
// информация (какие блоки заняты, какие свободны) хранится на стороне
// хоста: программа может как угодно портить свою память, но не может
// сломать сам аллокатор. Блоки выровнены на 16 байт (SSE-код этого
// ждёт), соседние свободные блоки сливаются.

#include <cstdint>
#include <map>

#include "../cpu/guest_memory.h"

namespace rt {

class GuestHeap {
public:
    explicit GuestHeap(cpu::PagedGuestMemory& mem, uint32_t initialArena = 0x100000);

    // 0 — нет памяти. size 0 даёт минимальный блок (как в Windows).
    uint32_t Alloc(uint32_t size, bool zero = false);
    bool Free(uint32_t addr);
    // Как HeapReAlloc/realloc: может вернуть другой адрес; 0 — не вышло
    // (старый блок при этом остаётся).
    uint32_t Realloc(uint32_t addr, uint32_t size, bool zeroNew = false, bool inPlaceOnly = false);
    // Запрошенный размер блока или 0xFFFFFFFF, если это не наш блок.
    uint32_t Size(uint32_t addr) const;
    bool Owns(uint32_t addr) const { return used_.count(addr) != 0; }

    uint64_t BytesInUse() const { return bytesInUse_; }
    void Destroy(); // освободить все арены (HeapDestroy)

private:
    static constexpr uint32_t kAlign = 16;
    bool Grow(uint32_t minSize);
    void InsertFree(uint32_t addr, uint32_t size);
    void EraseFree(uint32_t addr);

    cpu::PagedGuestMemory& mem_;
    uint32_t arenaSize_;
    std::map<uint32_t, uint32_t> used_;       // адрес -> выделенный (выровненный) размер
    std::map<uint32_t, uint32_t> requested_;  // адрес -> запрошенный размер
    std::map<uint32_t, uint32_t> freeByAddr_; // адрес -> размер
    std::multimap<uint32_t, uint32_t> freeBySize_; // размер -> адрес
    std::map<uint32_t, uint32_t> arenas_;     // начало -> размер
    uint64_t bytesInUse_ = 0;
};

// Куча процесса по умолчанию (GetProcessHeap, malloc).
GuestHeap& ProcessHeap();
void SetProcessHeap(GuestHeap* heap);

} // namespace rt
