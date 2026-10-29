#pragma once
// xp_icons.h — значки оболочки в духе Windows XP: жёлтые папки, диски,
// «Мой компьютер», корзина, файлы. Рисуются векторно со сглаживанием в
// нужном размере и кэшируются. Свои рисунки, не значки Microsoft.

#include <cstdint>

#include "surface.h"

namespace ui {
namespace xp {

enum class IconKind {
    Folder,        // обычная жёлтая папка
    FolderOpen,    // открытая папка
    Documents,     // «Мои документы»: папка с листом
    Pictures,      // «Мои рисунки»: папка с картинкой
    Programs,      // папка с программами: папка с окном
    Computer,      // «Мой компьютер»
    HardDrive,     // локальный диск
    SdCard,        // SD-карта
    CdDrive,       // компакт-диск (ROMFS — только чтение)
    RecycleBin,    // корзина
    Application,   // файл программы (.exe)
    TextFile,      // текстовый файл
    ImageFile,     // картинка
    File,          // прочий файл
    Shortcut,      // стрелка ярлыка (накладывается поверх)
    MusicFile,     // собственный музыкальный файл с нотами и кнопкой Play
    Count
};

// Рисует значок size x size (16..64) с левым верхним углом в (x, y).
// selected — синяя подсветка выделенного значка, как в проводнике XP.
void DrawIcon(Surface& s, IconKind kind, int x, int y, int size, bool selected = false);

// Пиксель значка в формате 0xAARRGGBB — для проверок
uint32_t IconPixel(IconKind kind, int size, int x, int y);

} // namespace xp
} // namespace ui
