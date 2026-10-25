#pragma once
// xp_theme.h — отрисовка оболочки в духе Windows XP (Luna): обои, панель
// задач, кнопка «Пуск», область уведомлений. Только рисование, без логики.
//
// Картинки Microsoft (Bliss, флаг, значки) не используются: обои рисуются
// процедурно — небо с облаками и зелёный холм.

#include "surface.h"
#include "region.h"

namespace ui {
namespace xp {

// Обои на всю область r (кэшируются по размеру)
void DrawWallpaper(Surface& s, const Rect& r);
// Пиксель обоев размера w x h в точке (x, y) — для проверок
uint32_t WallpaperPixel(int w, int h, int x, int y);
// Панель задач Luna
void DrawTaskbar(Surface& s, const Rect& r);
// Зелёная кнопка «Пуск» со скруглённым правым краем; подпись рисует вызывающий
void DrawStartButton(Surface& s, const Rect& r, bool pressed, bool hot);
// Знак на кнопке «Пуск» (круг 20x20 с левым верхним углом в x, y)
void DrawStartGlyph(Surface& s, int x, int y);
// Кнопка программы на панели задач
void DrawTaskButton(Surface& s, const Rect& r, bool active, bool hot);
// Область уведомлений (часы) у правого края
void DrawTray(Surface& s, const Rect& r);

} // namespace xp
} // namespace ui
