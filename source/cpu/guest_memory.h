#pragma once
// guest_memory.h
//
// Абстракция адресного пространства эмулируемой программы.
// На Switch это будет обёртка над буфером, выделенным через svcMapMemory.
// Для хостовых тестов — плоский std::vector.
//
// ВАЖНО: реальная сборка под Switch (devkitA64) идёт с флагом -fno-exceptions —
// поэтому здесь принципиально нет ни одного throw. Вместо исключений при
// обращении за пределы буфера — печатаем диагностику и вызываем abort().
// Это не "плохое исправление", а осознанный выбор: в embedded/homebrew-среде
// исключения обычно отключают ради размера бинарника и предсказуемости,
// и это стандартная практика (тот же подход в большинстве игровых движков).

#include <cstdint>
#include <vector>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace cpu {

// main.cpp выставляет false сразу после consoleExit(). Без этой проверки
// FatalError/FatalMemoryError попытались бы напечатать диагностику через
// printf ПОСЛЕ закрытия консоли — и именно это оказалось настоящей причиной
// крашей на реальном железе (падало внутри ConsoleSwRenderer_drawChar из
// libnx, найдено через addr2line) — не сама ошибка, а ПОПЫТКА её напечатать
// в уже закрытую консоль маскировала истинное место сбоя.
inline bool g_consoleAvailable = true;

// Текущий EIP гостевого кода — интерпретатор обновляет перед каждым шагом.
// Полезно для ЛЮБОЙ будущей диагностики краха (не только этой сессии) —
// без этого FatalError/FatalMemoryError не говорят, ГДЕ в гостевом коде
// произошёл сбой, только что именно (адрес памяти/сообщение).
inline uint32_t g_lastKnownEip = 0;

[[noreturn]] inline void FatalError(const char* msg) {
    if (g_consoleAvailable) {
        std::printf("[FATAL] %s (EIP=0x%08x)\n", msg, g_lastKnownEip);
        std::fflush(stdout);
    }
    // ВАЖНО: std::abort() на Switch/Atmosphere вызывает полный крах системы
    // (синий экран с перезагрузкой), а не просто завершение НАШЕГО процесса,
    // как на обычном ПК. std::exit() должен корректно вернуть в hbmenu.
    std::exit(1);
}

[[noreturn]] inline void FatalMemoryError(const char* msg, uint32_t addr) {
    if (g_consoleAvailable) {
        std::printf("[FATAL] Guest memory error: %s (addr=0x%08x, EIP=0x%08x)\n", msg, addr, g_lastKnownEip);
        std::fflush(stdout);
    }
    std::exit(1);
}

class IGuestMemory {
public:
    virtual ~IGuestMemory() = default;

    virtual uint8_t  Read8(uint32_t addr) const = 0;
    virtual uint16_t Read16(uint32_t addr) const = 0;
    virtual uint32_t Read32(uint32_t addr) const = 0;
    virtual void     Write8(uint32_t addr, uint8_t value) = 0;
    virtual void     Write16(uint32_t addr, uint16_t value) = 0;
    virtual void     Write32(uint32_t addr, uint32_t value) = 0;

    // Удобный доступ для decoder'а — читает N сырых байт (для fetch инструкций)
    virtual const uint8_t* RawPointer(uint32_t addr) const = 0;
};

// Простая реализация: один сплошной регион [baseAddress, baseAddress+size)
class FlatGuestMemory : public IGuestMemory {
public:
    FlatGuestMemory(uint32_t baseAddress, size_t size)
        : baseAddress_(baseAddress), buffer_(size, 0) {}

    uint8_t Read8(uint32_t addr) const override {
        size_t off = Offset(addr);
        return buffer_[off];
    }

    uint16_t Read16(uint32_t addr) const override {
        size_t off = Offset(addr);
        if (off + 2 > buffer_.size()) FatalMemoryError("Read16 out of bounds", addr);
        uint16_t value;
        std::memcpy(&value, &buffer_[off], 2);
        return value;
    }

    uint32_t Read32(uint32_t addr) const override {
        size_t off = Offset(addr);
        if (off + 4 > buffer_.size()) FatalMemoryError("Read32 out of bounds", addr);
        uint32_t value;
        std::memcpy(&value, &buffer_[off], 4);
        return value;
    }

    void Write8(uint32_t addr, uint8_t value) override {
        size_t off = Offset(addr);
        buffer_[off] = value;
    }

    void Write16(uint32_t addr, uint16_t value) override {
        size_t off = Offset(addr);
        if (off + 2 > buffer_.size()) FatalMemoryError("Write16 out of bounds", addr);
        std::memcpy(&buffer_[off], &value, 2);
    }

    void Write32(uint32_t addr, uint32_t value) override {
        size_t off = Offset(addr);
        if (off + 4 > buffer_.size()) FatalMemoryError("Write32 out of bounds", addr);
        std::memcpy(&buffer_[off], &value, 4);
    }

    // Максимальная длина x86-инструкции — 15 байт. RawPointer должен
    // гарантировать, что decoder'у будет ГДЕ читать эти байты, а не только
    // что начальный адрес валиден — иначе на некоторых платформах (не на
    // x86, где часто "прокатывает" из-за особенностей аллокатора) чтение
    // за пределы буфера может честно упасть на невыделенной странице памяти.
    static constexpr size_t kMaxInstructionLength = 15;

    const uint8_t* RawPointer(uint32_t addr) const override {
        size_t off = Offset(addr);
        if (off + kMaxInstructionLength > buffer_.size()) {
            FatalMemoryError("RawPointer: недостаточно места до конца буфера для декодирования инструкции", addr);
        }
        return &buffer_[off];
    }

    // Для теста: загрузить сырые байты кода по конкретному адресу
    void LoadBytes(uint32_t addr, const uint8_t* data, size_t len) {
        size_t off = Offset(addr);
        if (off + len > buffer_.size()) FatalMemoryError("LoadBytes out of bounds", addr);
        std::memcpy(&buffer_[off], data, len);
    }

    uint32_t BaseAddress() const { return baseAddress_; }
    uint32_t EndAddress() const { return baseAddress_ + static_cast<uint32_t>(buffer_.size()); }

private:
    size_t Offset(uint32_t addr) const {
        if (addr < baseAddress_) FatalMemoryError("address below base", addr);
        size_t off = addr - baseAddress_;
        if (off >= buffer_.size()) FatalMemoryError("address above region", addr);
        return off;
    }

    uint32_t baseAddress_;
    std::vector<uint8_t> buffer_;
};

} // namespace cpu
