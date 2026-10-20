// kernel32_memory.cpp — KERNEL32: кучи (Heap*, Local*, Global*) и
// виртуальная память (Virtual*).

#include <map>
#include <memory>

#include "hle_util.h"
#include "../runtime/heap.h"

namespace win32 {

namespace {

using namespace hle;

constexpr uint32_t HEAP_ZERO_MEMORY = 0x08;
constexpr uint32_t HEAP_REALLOC_IN_PLACE_ONLY = 0x10;

// HANDLE кучи -> куча. Хендл — адрес маленького зарезервированного региона,
// чтобы он был уникальным и «настоящим» указателем, как в Windows.
std::map<uint32_t, rt::GuestHeap*>& Heaps() {
    static std::map<uint32_t, rt::GuestHeap*> heaps;
    return heaps;
}
std::vector<std::unique_ptr<rt::GuestHeap>>& OwnedHeaps() {
    static std::vector<std::unique_ptr<rt::GuestHeap>> owned;
    return owned;
}
uint32_t g_processHeapHandle = 0;

uint32_t ProcessHeapHandle(cpu::IGuestMemory& m) {
    if (!g_processHeapHandle) {
        static rt::GuestHeap* heap = nullptr;
        if (!heap) {
            OwnedHeaps().push_back(std::make_unique<rt::GuestHeap>(m));
            heap = OwnedHeaps().back().get();
            rt::SetProcessHeap(heap);
        }
        g_processHeapHandle = m.Allocate(0x1000, cpu::kProtRead);
        Heaps()[g_processHeapHandle] = heap;
    }
    return g_processHeapHandle;
}

rt::GuestHeap* HeapFromHandle(cpu::IGuestMemory& m, uint32_t h) {
    ProcessHeapHandle(m);
    auto it = Heaps().find(h);
    return it == Heaps().end() ? nullptr : it->second;
}

// Блок мог быть выделен из любой кучи (программы иногда путают хендлы):
// ищем владельца, начиная с указанной.
rt::GuestHeap* OwnerOf(cpu::IGuestMemory& m, uint32_t h, uint32_t p) {
    rt::GuestHeap* heap = HeapFromHandle(m, h);
    if (heap && heap->Owns(p)) return heap;
    for (auto& kv : Heaps()) if (kv.second->Owns(p)) return kv.second;
    return nullptr;
}

HostCallOutcome K32_GetProcessHeap(cpu::CpuState&, cpu::IGuestMemory& m) { return Ret(ProcessHeapHandle(m)); }

HostCallOutcome K32_HeapCreate(cpu::CpuState&, cpu::IGuestMemory& m) {
    ProcessHeapHandle(m);
    OwnedHeaps().push_back(std::make_unique<rt::GuestHeap>(m, 0x10000));
    uint32_t h = m.Allocate(0x1000, cpu::kProtRead);
    Heaps()[h] = OwnedHeaps().back().get();
    return Ret(h);
}
HostCallOutcome K32_HeapDestroy(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t h = Arg(c, m, 0);
    rt::GuestHeap* heap = HeapFromHandle(m, h);
    if (!heap || h == g_processHeapHandle) return Ret(0);
    heap->Destroy();
    Heaps().erase(h);
    return Ret(1);
}

// LPVOID HeapAlloc(HANDLE, DWORD flags, SIZE_T)
HostCallOutcome K32_HeapAlloc(cpu::CpuState& c, cpu::IGuestMemory& m) {
    rt::GuestHeap* heap = HeapFromHandle(m, Arg(c, m, 0));
    if (!heap) heap = &rt::ProcessHeap();
    uint32_t p = heap->Alloc(Arg(c, m, 2), (Arg(c, m, 1) & HEAP_ZERO_MEMORY) != 0);
    if (!p) SetLastError(m, ERROR_NOT_ENOUGH_MEMORY);
    return Ret(p);
}
HostCallOutcome K32_HeapFree(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 2);
    if (!p) return Ret(1);
    rt::GuestHeap* heap = OwnerOf(m, Arg(c, m, 0), p);
    if (!heap) { SetLastError(m, ERROR_INVALID_PARAMETER); return Ret(0); }
    heap->Free(p);
    return Ret(1);
}
// LPVOID HeapReAlloc(HANDLE, DWORD flags, LPVOID, SIZE_T)
HostCallOutcome K32_HeapReAlloc(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t flags = Arg(c, m, 1), p = Arg(c, m, 2), size = Arg(c, m, 3);
    rt::GuestHeap* heap = OwnerOf(m, Arg(c, m, 0), p);
    if (!heap) { SetLastError(m, ERROR_INVALID_PARAMETER); return Ret(0); }
    uint32_t n = heap->Realloc(p, size, (flags & HEAP_ZERO_MEMORY) != 0, (flags & HEAP_REALLOC_IN_PLACE_ONLY) != 0);
    if (!n) SetLastError(m, ERROR_NOT_ENOUGH_MEMORY);
    return Ret(n);
}
HostCallOutcome K32_HeapSize(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 2);
    rt::GuestHeap* heap = OwnerOf(m, Arg(c, m, 0), p);
    return Ret(heap ? heap->Size(p) : 0xFFFFFFFFu);
}
HostCallOutcome K32_HeapValidate(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(1); }
HostCallOutcome K32_HeapCompact(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0x100000); }
HostCallOutcome K32_HeapSetInformation(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(1); }
HostCallOutcome K32_HeapLock(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(1); }

// --- Local*/Global*: на одной куче процесса, хендл == указатель
// (LMEM_MOVEABLE тоже отдаём указателем: Lock возвращает его же). ---
constexpr uint32_t LMEM_ZEROINIT = 0x40;

HostCallOutcome LocalAllocCommon(cpu::IGuestMemory& m, uint32_t flags, uint32_t size) {
    ProcessHeapHandle(m);
    uint32_t p = rt::ProcessHeap().Alloc(size, (flags & LMEM_ZEROINIT) != 0);
    if (!p) SetLastError(m, ERROR_NOT_ENOUGH_MEMORY);
    return Ret(p);
}
HostCallOutcome K32_LocalAlloc(cpu::CpuState& c, cpu::IGuestMemory& m) { return LocalAllocCommon(m, Arg(c, m, 0), Arg(c, m, 1)); }
HostCallOutcome K32_LocalFree(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0);
    ProcessHeapHandle(m);
    if (p && !rt::ProcessHeap().Free(p)) return Ret(p); // ошибка: возвращает хендл
    return Ret(0);
}
HostCallOutcome K32_LocalReAlloc(cpu::CpuState& c, cpu::IGuestMemory& m) {
    ProcessHeapHandle(m);
    uint32_t p = Arg(c, m, 0), size = Arg(c, m, 1), flags = Arg(c, m, 2);
    uint32_t n = rt::ProcessHeap().Realloc(p, size, (flags & LMEM_ZEROINIT) != 0, !(flags & 0x2) /* LMEM_MOVEABLE */);
    if (!n) n = rt::ProcessHeap().Realloc(p, size, (flags & LMEM_ZEROINIT) != 0, false);
    if (!n) SetLastError(m, ERROR_NOT_ENOUGH_MEMORY);
    return Ret(n);
}
HostCallOutcome K32_LocalSize(cpu::CpuState& c, cpu::IGuestMemory& m) {
    ProcessHeapHandle(m);
    uint32_t s = rt::ProcessHeap().Size(Arg(c, m, 0));
    return Ret(s == 0xFFFFFFFFu ? 0 : s);
}
HostCallOutcome K32_LocalLock(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(Arg(c, m, 0)); }
HostCallOutcome K32_LocalUnlock(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0); }
HostCallOutcome K32_LocalHandle(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(Arg(c, m, 0)); }
HostCallOutcome K32_LocalFlags(cpu::CpuState&, cpu::IGuestMemory&) { return Ret(0); }

// --- VirtualAlloc и компания ---
constexpr uint32_t MEM_COMMIT = 0x1000, MEM_RESERVE = 0x2000, MEM_RELEASE = 0x8000, MEM_DECOMMIT = 0x4000;
constexpr uint32_t MEM_TOP_DOWN = 0x100000;

uint8_t ProtFromWin(uint32_t p) {
    switch (p & 0xFF) {
        case 0x01: return cpu::kProtNone;                    // PAGE_NOACCESS
        case 0x02: return cpu::kProtRead;                    // PAGE_READONLY
        case 0x04: case 0x08: return cpu::kProtRW;           // READWRITE / WRITECOPY
        case 0x10: return cpu::kProtExec;                    // EXECUTE
        case 0x20: return cpu::kProtRead | cpu::kProtExec;   // EXECUTE_READ
        default: return cpu::kProtRWX;                       // EXECUTE_READWRITE и т.п.
    }
}
uint32_t WinFromProt(uint8_t p) {
    if (p == cpu::kProtNone) return 0x01;
    if (p & cpu::kProtExec) return (p & cpu::kProtWrite) ? 0x40 : 0x20;
    return (p & cpu::kProtWrite) ? 0x04 : 0x02;
}

// LPVOID VirtualAlloc(LPVOID addr, SIZE_T size, DWORD type, DWORD protect)
HostCallOutcome K32_VirtualAlloc(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t addr = Arg(c, m, 0), size = Arg(c, m, 1), type = Arg(c, m, 2), protect = Arg(c, m, 3);
    if (!size) { SetLastError(m, ERROR_INVALID_PARAMETER); return Ret(0); }
    uint8_t prot = ProtFromWin(protect);
    if (addr) {
        uint32_t base = addr & ~0xFFFu;
        uint32_t span = ((addr + size + 0xFFF) & ~0xFFFu) - base;
        if (m.IsRangeMapped(base, span)) {
            // MEM_COMMIT внутри уже зарезервированного региона: всё уже есть
            m.Protect(base, span, prot);
            return Ret(base);
        }
        if (!(type & MEM_RESERVE)) {
            // коммит части без резерва: недостающие страницы выделяем
            if (m.IsRangeFree(base, span) && m.Map(base, span, prot)) return Ret(base);
        }
        uint32_t rbase = addr & ~(cpu::PagedGuestMemory::kAllocationGranularity - 1);
        uint32_t rspan = ((addr + size + 0xFFF) & ~0xFFFu) - rbase;
        if (m.IsRangeFree(rbase, rspan) && m.Map(rbase, rspan, prot)) return Ret(rbase);
        SetLastError(m, 487); // ERROR_INVALID_ADDRESS
        return Ret(0);
    }
    uint32_t p = m.Allocate(size, prot, 0x10000, 0x7FFE0000u, (type & MEM_TOP_DOWN) != 0);
    if (!p) SetLastError(m, ERROR_NOT_ENOUGH_MEMORY);
    (void)MEM_COMMIT;
    return Ret(p);
}
HostCallOutcome K32_VirtualFree(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t addr = Arg(c, m, 0), size = Arg(c, m, 1), type = Arg(c, m, 2);
    if (type & MEM_RELEASE) {
        uint32_t base = m.RegionBase(addr);
        if (!base || base != addr) { SetLastError(m, ERROR_INVALID_PARAMETER); return Ret(0); }
        m.Unmap(base, m.RegionSize(base));
        return Ret(1);
    }
    if (type & MEM_DECOMMIT) {
        // страницы оставляем выделенными (как зарезервированные), только обнуляем
        uint32_t base = addr & ~0xFFFu;
        uint32_t span = size ? ((addr + size + 0xFFF) & ~0xFFFu) - base : m.RegionSize(m.RegionBase(addr));
        for (uint32_t i = 0; i < span; i += 4) if (m.IsMapped(base + i)) m.Write32(base + i, 0);
        return Ret(1);
    }
    SetLastError(m, ERROR_INVALID_PARAMETER);
    return Ret(0);
}
// BOOL VirtualProtect(LPVOID, SIZE_T, DWORD newProtect, PDWORD oldProtect)
HostCallOutcome K32_VirtualProtect(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t addr = Arg(c, m, 0), size = Arg(c, m, 1), prot = Arg(c, m, 2), oldp = Arg(c, m, 3);
    if (!m.IsRangeMapped(addr, size ? size : 1)) { SetLastError(m, 487); return Ret(0); }
    if (oldp) m.Write32(oldp, WinFromProt(m.GetProtection(addr)));
    m.Protect(addr & ~0xFFFu, ((addr + size + 0xFFF) & ~0xFFFu) - (addr & ~0xFFFu), ProtFromWin(prot));
    return Ret(1);
}
// SIZE_T VirtualQuery(LPCVOID, PMEMORY_BASIC_INFORMATION, SIZE_T)
HostCallOutcome K32_VirtualQuery(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t addr = Arg(c, m, 0), info = Arg(c, m, 1), len = Arg(c, m, 2);
    if (len < 28) { SetLastError(m, 24); return Ret(0); } // ERROR_BAD_LENGTH
    uint32_t page = addr & ~0xFFFu;
    if (m.IsMapped(page)) {
        uint32_t base = m.RegionBase(page);
        uint8_t prot = m.GetProtection(page);
        // длина: подряд идущие страницы с теми же правами
        uint32_t end = page;
        while (end < base + m.RegionSize(base) && m.GetProtection(end) == prot) end += 0x1000;
        m.Write32(info + 0, page);                // BaseAddress
        m.Write32(info + 4, base);                // AllocationBase
        m.Write32(info + 8, WinFromProt(prot));   // AllocationProtect
        m.Write32(info + 12, end - page);         // RegionSize
        m.Write32(info + 16, MEM_COMMIT);         // State
        m.Write32(info + 20, WinFromProt(prot));  // Protect
        m.Write32(info + 24, 0x20000);            // Type: MEM_PRIVATE
    } else {
        uint32_t end = page;
        while (end < 0x7FFF0000u && !m.IsMapped(end)) end += 0x1000;
        m.Write32(info + 0, page);
        m.Write32(info + 4, 0);
        m.Write32(info + 8, 0);
        m.Write32(info + 12, end - page);
        m.Write32(info + 16, 0x10000); // MEM_FREE
        m.Write32(info + 20, 0x01);
        m.Write32(info + 24, 0);
    }
    return Ret(28);
}
HostCallOutcome K32_IsBadReadPtr(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0), n = Arg(c, m, 1);
    return Ret(n && !m.IsRangeMapped(p, n) ? 1 : 0);
}
HostCallOutcome K32_IsBadWritePtr(cpu::CpuState& c, cpu::IGuestMemory& m) { return K32_IsBadReadPtr(c, m); }
HostCallOutcome K32_IsBadCodePtr(cpu::CpuState& c, cpu::IGuestMemory& m) { return Ret(m.IsMapped(Arg(c, m, 0)) ? 0 : 1); }
HostCallOutcome K32_IsBadStringPtrA(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0), n = Arg(c, m, 1);
    for (uint32_t i = 0; i < n; ++i) {
        if (!m.IsMapped(p + i)) return Ret(1);
        if (!m.Read8(p + i)) break;
    }
    return Ret(0);
}

// --- RtlMoveMemory/Fill/Zero (экспортируются KERNEL32 как функции) ---
HostCallOutcome K32_RtlMoveMemory(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t d = Arg(c, m, 0), s = Arg(c, m, 1), n = Arg(c, m, 2);
    std::vector<uint8_t> tmp(n);
    m.ReadBytes(s, tmp.data(), n);
    m.LoadBytes(d, tmp.data(), n);
    return Ret(0);
}
HostCallOutcome K32_RtlFillMemory(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t d = Arg(c, m, 0), n = Arg(c, m, 1), v = Arg(c, m, 2);
    for (uint32_t i = 0; i < n; ++i) m.Write8(d + i, static_cast<uint8_t>(v));
    return Ret(0);
}
HostCallOutcome K32_RtlZeroMemory(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t d = Arg(c, m, 0), n = Arg(c, m, 1);
    for (uint32_t i = 0; i < n; ++i) m.Write8(d + i, 0);
    return Ret(0);
}

HostCallOutcome K32_GlobalMemoryStatus(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0);
    m.Write32(p + 0, 32);
    m.Write32(p + 4, 20);              // dwMemoryLoad, %
    m.Write32(p + 8, 0x80000000u);     // TotalPhys: 2 ГБ
    m.Write32(p + 12, 0x60000000u);    // AvailPhys
    m.Write32(p + 16, 0x80000000u);    // TotalPageFile
    m.Write32(p + 20, 0x60000000u);
    m.Write32(p + 24, 0x7FFE0000u);    // TotalVirtual
    m.Write32(p + 28, 0x60000000u);    // AvailVirtual
    return Ret(0);
}
HostCallOutcome K32_GlobalMemoryStatusEx(cpu::CpuState& c, cpu::IGuestMemory& m) {
    uint32_t p = Arg(c, m, 0);
    m.Write32(p + 4, 20);
    auto w64 = [&](uint32_t off, uint64_t v) { m.Write32(p + off, static_cast<uint32_t>(v)); m.Write32(p + off + 4, static_cast<uint32_t>(v >> 32)); };
    w64(8, 0x80000000ull); w64(16, 0x60000000ull); w64(24, 0x80000000ull); w64(32, 0x60000000ull);
    w64(40, 0x7FFE0000ull); w64(48, 0x60000000ull); w64(56, 0);
    return Ret(1);
}

const HostFunctionEntry kEntries[] = {
    {"KERNEL32.dll", "GetProcessHeap", 0, CallConv::Stdcall, &K32_GetProcessHeap},
    {"KERNEL32.dll", "HeapCreate", 12, CallConv::Stdcall, &K32_HeapCreate},
    {"KERNEL32.dll", "HeapDestroy", 4, CallConv::Stdcall, &K32_HeapDestroy},
    {"KERNEL32.dll", "HeapAlloc", 12, CallConv::Stdcall, &K32_HeapAlloc},
    {"KERNEL32.dll", "HeapFree", 12, CallConv::Stdcall, &K32_HeapFree},
    {"KERNEL32.dll", "HeapReAlloc", 16, CallConv::Stdcall, &K32_HeapReAlloc},
    {"KERNEL32.dll", "HeapSize", 12, CallConv::Stdcall, &K32_HeapSize},
    {"KERNEL32.dll", "HeapValidate", 12, CallConv::Stdcall, &K32_HeapValidate},
    {"KERNEL32.dll", "HeapCompact", 8, CallConv::Stdcall, &K32_HeapCompact},
    {"KERNEL32.dll", "HeapSetInformation", 16, CallConv::Stdcall, &K32_HeapSetInformation},
    {"KERNEL32.dll", "HeapLock", 4, CallConv::Stdcall, &K32_HeapLock},
    {"KERNEL32.dll", "HeapUnlock", 4, CallConv::Stdcall, &K32_HeapLock},
    {"KERNEL32.dll", "LocalAlloc", 8, CallConv::Stdcall, &K32_LocalAlloc},
    {"KERNEL32.dll", "LocalFree", 4, CallConv::Stdcall, &K32_LocalFree},
    {"KERNEL32.dll", "LocalReAlloc", 12, CallConv::Stdcall, &K32_LocalReAlloc},
    {"KERNEL32.dll", "LocalSize", 4, CallConv::Stdcall, &K32_LocalSize},
    {"KERNEL32.dll", "LocalLock", 4, CallConv::Stdcall, &K32_LocalLock},
    {"KERNEL32.dll", "LocalUnlock", 4, CallConv::Stdcall, &K32_LocalUnlock},
    {"KERNEL32.dll", "LocalHandle", 4, CallConv::Stdcall, &K32_LocalHandle},
    {"KERNEL32.dll", "LocalFlags", 4, CallConv::Stdcall, &K32_LocalFlags},
    {"KERNEL32.dll", "GlobalAlloc", 8, CallConv::Stdcall, &K32_LocalAlloc},
    {"KERNEL32.dll", "GlobalFree", 4, CallConv::Stdcall, &K32_LocalFree},
    {"KERNEL32.dll", "GlobalReAlloc", 12, CallConv::Stdcall, &K32_LocalReAlloc},
    {"KERNEL32.dll", "GlobalSize", 4, CallConv::Stdcall, &K32_LocalSize},
    {"KERNEL32.dll", "GlobalLock", 4, CallConv::Stdcall, &K32_LocalLock},
    {"KERNEL32.dll", "GlobalUnlock", 4, CallConv::Stdcall, &K32_LocalUnlock},
    {"KERNEL32.dll", "GlobalHandle", 4, CallConv::Stdcall, &K32_LocalHandle},
    {"KERNEL32.dll", "GlobalFlags", 4, CallConv::Stdcall, &K32_LocalFlags},
    {"KERNEL32.dll", "VirtualAlloc", 16, CallConv::Stdcall, &K32_VirtualAlloc},
    {"KERNEL32.dll", "VirtualFree", 12, CallConv::Stdcall, &K32_VirtualFree},
    {"KERNEL32.dll", "VirtualProtect", 16, CallConv::Stdcall, &K32_VirtualProtect},
    {"KERNEL32.dll", "VirtualQuery", 12, CallConv::Stdcall, &K32_VirtualQuery},
    {"KERNEL32.dll", "IsBadReadPtr", 8, CallConv::Stdcall, &K32_IsBadReadPtr},
    {"KERNEL32.dll", "IsBadWritePtr", 8, CallConv::Stdcall, &K32_IsBadWritePtr},
    {"KERNEL32.dll", "IsBadCodePtr", 4, CallConv::Stdcall, &K32_IsBadCodePtr},
    {"KERNEL32.dll", "IsBadStringPtrA", 8, CallConv::Stdcall, &K32_IsBadStringPtrA},
    {"KERNEL32.dll", "RtlMoveMemory", 12, CallConv::Stdcall, &K32_RtlMoveMemory},
    {"KERNEL32.dll", "RtlFillMemory", 12, CallConv::Stdcall, &K32_RtlFillMemory},
    {"KERNEL32.dll", "RtlZeroMemory", 8, CallConv::Stdcall, &K32_RtlZeroMemory},
    {"KERNEL32.dll", "GlobalMemoryStatus", 4, CallConv::Stdcall, &K32_GlobalMemoryStatus},
    {"KERNEL32.dll", "GlobalMemoryStatusEx", 4, CallConv::Stdcall, &K32_GlobalMemoryStatusEx},
};
const bool kRegistered = RegisterHostFunctions(kEntries, sizeof(kEntries) / sizeof(kEntries[0]));

} // namespace

// Хендл кучи процесса для PEB и msvcrt (создаёт кучу при первом обращении).
uint32_t HleProcessHeapHandle(cpu::IGuestMemory& m) { return ProcessHeapHandle(m); }

} // namespace win32
