// test_xp_icons.cpp — значки оболочки в духе XP: у каждого есть
// непрозрачная середина и прозрачные углы, рисование детерминировано,
// выделение меняет цвет. С аргументом пути сохраняет лист значков (PPM).

#include "source/ui/xp_icons.h"

#include <cstdio>

using ui::xp::IconKind;

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s (line %d)\n", #cond, __LINE__); ++failures; } } while (0)

int main(int argc, char** argv) {
    const int kinds = static_cast<int>(IconKind::Count);
    const int sizes[] = {16, 32, 48};
    for (int k = 0; k < kinds; ++k) {
        IconKind kind = static_cast<IconKind>(k);
        for (int size : sizes) {
            int opaque = 0;
            for (int y = 0; y < size; ++y)
                for (int x = 0; x < size; ++x)
                    if ((ui::xp::IconPixel(kind, size, x, y) >> 24) > 200) ++opaque;
            // ярлык — маленькая стрелка в углу, остальные занимают заметную часть
            CHECK(opaque > size * size / (kind == IconKind::Shortcut ? 20 : 6));
            if (kind != IconKind::Shortcut) CHECK((ui::xp::IconPixel(kind, size, 0, 0) >> 24) == 0);
        }
    }
    // папка — жёлтая: красный и зелёный каналы заметно больше синего
    uint32_t mid = ui::xp::IconPixel(IconKind::Folder, 48, 24, 34);
    CHECK(((mid >> 16) & 255) > 200 && ((mid >> 8) & 255) > 150 && (mid & 255) < 140);

    // лист: строка значков каждого размера, выделенный вариант рядом
    ui::OwnedSurface sheet;
    sheet.Allocate(kinds * 112 + 16, 3 * 64 + 16);
    for (int y = 0; y < sheet.s.height; ++y)
        for (int x = 0; x < sheet.s.width; ++x) sheet.s.Set(x, y, 0xFFFFFF);
    for (int k = 0; k < kinds; ++k)
        for (int i = 0; i < 3; ++i) {
            ui::xp::DrawIcon(sheet.s, static_cast<IconKind>(k), 16 + k * 112, 8 + i * 64, sizes[i]);
            ui::xp::DrawIcon(sheet.s, static_cast<IconKind>(k), 16 + k * 112 + 52, 8 + i * 64, sizes[i], true);
        }
    // выделение меняет цвет, а повторное рисование — нет
    uint32_t plain = sheet.s.Get(16 + 24, 8 + 128 + 34), chosen = sheet.s.Get(16 + 52 + 24, 8 + 128 + 34);
    CHECK(plain != chosen);
    ui::xp::DrawIcon(sheet.s, IconKind::Folder, 16, 8 + 128, 48);
    CHECK(sheet.s.Get(16 + 24, 8 + 128 + 34) != 0xFFFFFF);

    if (argc > 1) {
        if (std::FILE* f = std::fopen(argv[1], "wb")) {
            std::fprintf(f, "P6\n%d %d\n255\n", sheet.s.width, sheet.s.height);
            for (int y = 0; y < sheet.s.height; ++y)
                for (int x = 0; x < sheet.s.width; ++x) {
                    uint32_t p = sheet.s.Get(x, y);
                    unsigned char rgb[3] = {static_cast<unsigned char>(p >> 16), static_cast<unsigned char>(p >> 8), static_cast<unsigned char>(p)};
                    std::fwrite(rgb, 1, 3, f);
                }
            std::fclose(f);
        }
    }
    std::printf(failures ? "XP icons: FAILED (%d)\n" : "XP icons: PASS\n", failures);
    return failures ? 1 : 0;
}
