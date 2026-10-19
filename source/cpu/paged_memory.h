#pragma once
// paged_memory.h
//
// Адресное пространство гостя как в настоящей Windows: 4 ГБ виртуальных
// адресов, разбитых на страницы по 4 КиБ. Память выделяется регионами
// (как VirtualAlloc) в любом месте пространства, поэтому EXE по 0x400000 и
// DLL по 0x5F400000 живут рядом, а физически занято ровно столько, сколько
// выделено.
//
// Обращение к невыделенной странице не валит процесс: запоминается
// «ошибка доступа» (Fault), чтение возвращает 0, запись игнорируется.
// Интерпретатор проверяет её после каждой инструкции и превращает в
// исключение гостя (EXCEPTION_ACCESS_VIOLATION) или в понятную диагностику.

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include <cstdio>
#include <cstdlib>

namespace cpu {

// Права страницы. Пока проверяется только факт выделения: права хранятся
// для VirtualQuery/VirtualProtect, но запись в R-страницу не ловится.
enum : uint8_t {
    kProtNone  = 0,
    kProtRead  = 1,
    kProtWrite = 2,
    kProtExec  = 4,
    kProtRW    = kProtRead | kProtWrite,
    kProtRWX   = kProtRead | kProtWrite | kProtExec,
};

struct MemoryFault {
    bool pending = false;
    bool write = false;
    uint32_t address = 0;
};

class PagedGuestMemory final {
public:
    static constexpr uint32_t kPageShift = 12;
    static constexpr uint32_t kPageSize = 1u << kPageShift;
    static constexpr uint32_t kPageMask = kPageSize - 1;
    static constexpr uint32_t kPageCount = 1u << (32 - kPageShift);
    // Гранулярность выделения Windows: регионы начинаются на границе 64 КиБ.
    static constexpr uint32_t kAllocationGranularity = 0x10000;

    PagedGuestMemory() : pages_(kPageCount, nullptr), info_(kPageCount) {}

    // Совместимость со старой плоской памятью (тесты, ранний код): сразу
    // выделяет один регион [base, base+size).
    PagedGuestMemory(uint32_t base, size_t size) : PagedGuestMemory() {
        if (!Map(base, static_cast<uint32_t>(size), kProtRWX)) {
            std::fprintf(stderr, "[FATAL] PagedGuestMemory: cannot map 0x%x+0x%zx\n", base, size);
            std::exit(1);
        }
        flatBase_ = base;
        flatEnd_ = base + static_cast<uint32_t>(size);
    }

    PagedGuestMemory(const PagedGuestMemory&) = delete;
    PagedGuestMemory& operator=(const PagedGuestMemory&) = delete;

    // Для кода, написанного под плоскую память.
    uint32_t BaseAddress() const { return flatBase_; }
    uint32_t EndAddress() const { return flatEnd_; }

    // --- Управление регионами -------------------------------------------

    // Выделяет [addr, addr+size) (округляется до страниц), заполняя нулями.
    // false, если хоть одна страница уже занята.
    bool Map(uint32_t addr, uint32_t size, uint8_t prot = kProtRW) {
        uint32_t first = addr >> kPageShift;
        uint64_t end = (static_cast<uint64_t>(addr) + size + kPageMask) >> kPageShift;
        if (size == 0 || end > kPageCount) return false;
        for (uint64_t p = first; p < end; ++p)
            if (pages_[p]) return false;

        uint32_t count = static_cast<uint32_t>(end - first);
        Block block;
        block.data.reset(new (std::nothrow) uint8_t[static_cast<size_t>(count) * kPageSize]);
        if (!block.data) return false;
        std::memset(block.data.get(), 0, static_cast<size_t>(count) * kPageSize);
        block.livePages = count;
        uint32_t blockIndex = AllocBlockSlot(std::move(block));

        for (uint32_t i = 0; i < count; ++i) {
            pages_[first + i] = blocks_[blockIndex].data.get() + static_cast<size_t>(i) * kPageSize;
            info_[first + i] = PageInfo{prot, blockIndex, first};
        }
        return true;
    }

    // Освобождает страницы. Блок хост-памяти отдаётся, когда в нём не
    // осталось живых страниц.
    void Unmap(uint32_t addr, uint32_t size) {
        uint32_t first = addr >> kPageShift;
        uint64_t end = (static_cast<uint64_t>(addr) + size + kPageMask) >> kPageShift;
        if (end > kPageCount) end = kPageCount;
        for (uint64_t p = first; p < end; ++p) {
            if (!pages_[p]) continue;
            uint32_t b = info_[p].block;
            pages_[p] = nullptr;
            info_[p] = PageInfo{};
            if (--blocks_[b].livePages == 0) {
                blocks_[b].data.reset();
                freeBlocks_.push_back(b);
            }
        }
    }

    bool Protect(uint32_t addr, uint32_t size, uint8_t prot) {
        uint32_t first = addr >> kPageShift;
        uint64_t end = (static_cast<uint64_t>(addr) + size + kPageMask) >> kPageShift;
        for (uint64_t p = first; p < end; ++p)
            if (!pages_[p]) return false;
        for (uint64_t p = first; p < end; ++p) info_[p].prot = prot;
        return true;
    }

    bool IsMapped(uint32_t addr) const { return pages_[addr >> kPageShift] != nullptr; }

    bool IsRangeMapped(uint32_t addr, uint32_t size) const {
        if (size == 0) return true;
        uint64_t end = (static_cast<uint64_t>(addr) + size + kPageMask) >> kPageShift;
        if (end > kPageCount) return false;
        for (uint64_t p = addr >> kPageShift; p < end; ++p)
            if (!pages_[p]) return false;
        return true;
    }

    bool IsRangeFree(uint32_t addr, uint32_t size) const {
        uint64_t end = (static_cast<uint64_t>(addr) + size + kPageMask) >> kPageShift;
        if (end > kPageCount) return false;
        for (uint64_t p = addr >> kPageShift; p < end; ++p)
            if (pages_[p]) return false;
        return true;
    }

    uint8_t GetProtection(uint32_t addr) const { return info_[addr >> kPageShift].prot; }

    // Начало региона, к которому относится адрес (для VirtualQuery/VirtualFree).
    // 0, если адрес не выделен.
    uint32_t RegionBase(uint32_t addr) const {
        const uint8_t* page = pages_[addr >> kPageShift];
        if (!page) return 0;
        return info_[addr >> kPageShift].regionFirstPage << kPageShift;
    }

    // Размер региона, начиная с его базы (смежные страницы того же блока).
    uint32_t RegionSize(uint32_t base) const {
        uint32_t p = base >> kPageShift;
        if (!pages_[p]) return 0;
        uint32_t block = info_[p].block;
        uint32_t first = info_[p].regionFirstPage;
        uint32_t q = p;
        while (q < kPageCount && pages_[q] && info_[q].block == block && info_[q].regionFirstPage == first) ++q;
        return (q - p) << kPageShift;
    }

    // Ищет свободный диапазон размера size, начиная с адреса на границе
    // align, в пределах [low, high). 0 — не нашлось.
    uint32_t FindFree(uint32_t size, uint32_t low = 0x10000, uint32_t high = 0x7FFF0000,
                      uint32_t align = kAllocationGranularity, bool topDown = false) const {
        if (size == 0 || align == 0) return 0;
        uint64_t span = (static_cast<uint64_t>(size) + kPageMask) & ~static_cast<uint64_t>(kPageMask);
        if (!topDown) {
            for (uint64_t a = AlignUp(low, align); a + span <= high; a += align)
                if (IsRangeFree(static_cast<uint32_t>(a), static_cast<uint32_t>(span)))
                    return static_cast<uint32_t>(a);
        } else {
            if (static_cast<uint64_t>(high) < span) return 0;
            for (int64_t a = static_cast<int64_t>((high - span) & ~static_cast<uint64_t>(align - 1));
                 a >= static_cast<int64_t>(low); a -= align)
                if (IsRangeFree(static_cast<uint32_t>(a), static_cast<uint32_t>(span)))
                    return static_cast<uint32_t>(a);
        }
        return 0;
    }

    // Выделить где угодно (как VirtualAlloc(NULL, ...)). 0 — нет места.
    uint32_t Allocate(uint32_t size, uint8_t prot = kProtRW, uint32_t low = 0x10000,
                      uint32_t high = 0x7FFF0000, bool topDown = false) {
        uint32_t addr = FindFree(size, low, high, kAllocationGranularity, topDown);
        if (!addr || !Map(addr, size, prot)) return 0;
        return addr;
    }

    // --- Ошибки доступа ----------------------------------------------------

    MemoryFault& Fault() { return fault_; }
    const MemoryFault& Fault() const { return fault_; }
    void ClearFault() { fault_ = MemoryFault{}; }

    // --- IGuestMemory ------------------------------------------------------

    uint8_t Read8(uint32_t addr) const {
        const uint8_t* page = pages_[addr >> kPageShift];
        if (!page) return ReadFault(addr);
        return page[addr & kPageMask];
    }

    uint16_t Read16(uint32_t addr) const {
        uint32_t off = addr & kPageMask;
        const uint8_t* page = pages_[addr >> kPageShift];
        if (page && off <= kPageSize - 2) {
            uint16_t v;
            std::memcpy(&v, page + off, 2);
            return v;
        }
        return static_cast<uint16_t>(Read8(addr) | (Read8(addr + 1) << 8));
    }

    uint32_t Read32(uint32_t addr) const {
        uint32_t off = addr & kPageMask;
        const uint8_t* page = pages_[addr >> kPageShift];
        if (page && off <= kPageSize - 4) {
            uint32_t v;
            std::memcpy(&v, page + off, 4);
            return v;
        }
        return static_cast<uint32_t>(Read16(addr)) | (static_cast<uint32_t>(Read16(addr + 2)) << 16);
    }

    void Write8(uint32_t addr, uint8_t value) {
        uint8_t* page = pages_[addr >> kPageShift];
        if (!page) { WriteFault(addr); return; }
        page[addr & kPageMask] = value;
    }

    void Write16(uint32_t addr, uint16_t value) {
        uint32_t off = addr & kPageMask;
        uint8_t* page = pages_[addr >> kPageShift];
        if (page && off <= kPageSize - 2) {
            std::memcpy(page + off, &value, 2);
            return;
        }
        // Пересечение страниц: сначала проверяем обе, чтобы не записать половину.
        if (!IsRangeMapped(addr, 2)) { WriteFault(pages_[addr >> kPageShift] ? addr + 1 : addr); return; }
        Write8(addr, static_cast<uint8_t>(value));
        Write8(addr + 1, static_cast<uint8_t>(value >> 8));
    }

    void Write32(uint32_t addr, uint32_t value) {
        uint32_t off = addr & kPageMask;
        uint8_t* page = pages_[addr >> kPageShift];
        if (page && off <= kPageSize - 4) {
            std::memcpy(page + off, &value, 4);
            return;
        }
        if (!IsRangeMapped(addr, 4)) { WriteFault(pages_[addr >> kPageShift] ? (addr | kPageMask) + 1 : addr); return; }
        for (int i = 0; i < 4; ++i) Write8(addr + i, static_cast<uint8_t>(value >> (8 * i)));
    }

    // Указатель на байты инструкции для декодера. Если до конца страницы
    // меньше 16 байт, байты копируются во временный буфер (следующая
    // страница может отсутствовать — тогда хвост нулевой, а ошибка будет
    // только если декодер реально до него дочитается: это проверяет сам
    // декодер через FetchLimit).
    const uint8_t* RawPointer(uint32_t addr) const {
        const uint8_t* page = pages_[addr >> kPageShift];
        uint32_t off = addr & kPageMask;
        if (!page) {
            ReadFault(addr);
            std::memset(fetchScratch_, 0, sizeof(fetchScratch_));
            return fetchScratch_;
        }
        if (off <= kPageSize - sizeof(fetchScratch_)) return page + off;
        uint32_t inPage = kPageSize - off;
        std::memcpy(fetchScratch_, page + off, inPage);
        const uint8_t* next = pages_[((addr >> kPageShift) + 1) & (kPageCount - 1)];
        if (next) std::memcpy(fetchScratch_ + inPage, next, sizeof(fetchScratch_) - inPage);
        else std::memset(fetchScratch_ + inPage, 0, sizeof(fetchScratch_) - inPage);
        return fetchScratch_;
    }

    // Сколько байт подряд, начиная с addr, можно прочитать (до 16).
    uint32_t FetchLimit(uint32_t addr) const {
        if (!pages_[addr >> kPageShift]) return 0;
        uint32_t inPage = kPageSize - (addr & kPageMask);
        if (inPage >= 16) return 16;
        return pages_[((addr >> kPageShift) + 1) & (kPageCount - 1)] ? 16 : inPage;
    }

    // Прямой указатель на байт, если страница выделена (для быстрых
    // копирований в host-функциях). Действителен до конца страницы.
    uint8_t* HostPointer(uint32_t addr) {
        uint8_t* page = pages_[addr >> kPageShift];
        return page ? page + (addr & kPageMask) : nullptr;
    }

    // --- Удобные блочные операции -------------------------------------------

    void LoadBytes(uint32_t addr, const uint8_t* data, size_t len) {
        while (len) {
            uint8_t* dst = HostPointer(addr);
            if (!dst) { WriteFault(addr); return; }
            size_t chunk = kPageSize - (addr & kPageMask);
            if (chunk > len) chunk = len;
            std::memcpy(dst, data, chunk);
            addr += static_cast<uint32_t>(chunk);
            data += chunk;
            len -= chunk;
        }
    }

    void ReadBytes(uint32_t addr, uint8_t* out, size_t len) const {
        while (len) {
            const uint8_t* page = pages_[addr >> kPageShift];
            size_t chunk = kPageSize - (addr & kPageMask);
            if (chunk > len) chunk = len;
            if (!page) { ReadFault(addr); std::memset(out, 0, chunk); }
            else std::memcpy(out, page + (addr & kPageMask), chunk);
            addr += static_cast<uint32_t>(chunk);
            out += chunk;
            len -= chunk;
        }
    }

private:
    struct PageInfo {
        uint8_t prot = kProtNone;
        uint32_t block = 0;
        uint32_t regionFirstPage = 0;
    };
    struct Block {
        std::unique_ptr<uint8_t[]> data;
        uint32_t livePages = 0;
    };

    static uint64_t AlignUp(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }

    uint32_t AllocBlockSlot(Block&& block) {
        if (!freeBlocks_.empty()) {
            uint32_t i = freeBlocks_.back();
            freeBlocks_.pop_back();
            blocks_[i] = std::move(block);
            return i;
        }
        blocks_.push_back(std::move(block));
        return static_cast<uint32_t>(blocks_.size() - 1);
    }

    uint8_t ReadFault(uint32_t addr) const {
        if (!fault_.pending) fault_ = MemoryFault{true, false, addr};
        return 0;
    }
    void WriteFault(uint32_t addr) {
        if (!fault_.pending) fault_ = MemoryFault{true, true, addr};
    }

    std::vector<uint8_t*> pages_;
    std::vector<PageInfo> info_;
    std::vector<Block> blocks_;
    std::vector<uint32_t> freeBlocks_;
    mutable MemoryFault fault_;
    uint32_t flatBase_ = 0, flatEnd_ = 0;
    mutable uint8_t fetchScratch_[16] = {};
};

} // namespace cpu
