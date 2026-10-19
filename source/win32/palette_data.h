// Автосгенерировано из настоящих данных paint.exe (адрес 0x103d260,
// найден дизассемблированием: конструктор объекта-палитры на 0x100ca69,
// заполняющая функция 0x100c9a4). Это ТОЧНО классическая 28-цветная
// палитра MS Paint, не воссоздана визуально — прочитана из самого
// бинарника. Формат каждой записи в исходнике — 3 байта RGB (R,G,B),
// упакованные в 4-байтный шаг (4-й байт не используется).
#pragma once
#include <cstdint>
namespace win32 {
constexpr int PALETTE_COLOR_COUNT = 28;
// ARGB8888 (альфа всегда 0xFF)
constexpr uint32_t g_paintPalette[28] = {
    0xFF000000u,
    0xFF808080u,
    0xFF800000u,
    0xFF808000u,
    0xFF008000u,
    0xFF008080u,
    0xFF000080u,
    0xFF800080u,
    0xFFFFFFFFu,
    0xFFC0C0C0u,
    0xFFFF0000u,
    0xFFFFFF00u,
    0xFF00FF00u,
    0xFF00FFFFu,
    0xFF4000FFu,
    0xFFFF00FFu,
    0xFF000000u,
    0xFF808080u,
    0xFF800000u,
    0xFF808000u,
    0xFF008000u,
    0xFF008080u,
    0xFF000080u,
    0xFF800080u,
    0xFF808040u,
    0xFF004040u,
    0xFF0080FFu,
    0xFF004080u,
};
}
