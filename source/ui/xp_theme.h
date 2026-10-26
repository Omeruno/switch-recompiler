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

// --- Окна ---

// Окно Luna: синяя рамка, заголовок высоты titleH со скруглёнными верхними
// углами, серо-бежевая клиентская область. Текст заголовка рисует вызывающий.
void DrawWindowFrame(Surface& s, const Rect& r, int titleH, bool active = true);
enum class CaptionButton { Minimize, Maximize, Close, Restore };
void DrawCaptionButton(Surface& s, const Rect& r, CaptionButton kind, bool hot, bool pressed);
// Кнопка XP (OK, Отмена и т.п.); focused — пунктир внутри
void DrawPushButton(Surface& s, const Rect& r, bool enabled, bool hot, bool pressed, bool focused);
// Плоская кнопка панели инструментов: рамка видна только при наведении
void DrawToolButton(Surface& s, const Rect& r, bool hot, bool pressed);
// Разделитель панели инструментов и строки меню
void DrawToolSeparator(Surface& s, int x, int top, int bottom);
// Значки панели инструментов (размер size x size)
enum class ToolGlyph { Back, Forward, Up, Search, Folders, Views, Go };
void DrawToolGlyph(Surface& s, ToolGlyph kind, int x, int y, int size, bool enabled);
// Синяя панель задач проводника слева
void DrawTaskPane(Surface& s, const Rect& r);
// Группа панели задач: заголовок headerH (особая группа — тёмно-синяя) и тело;
// подпись заголовка рисует вызывающий
void DrawTaskGroup(Surface& s, const Rect& r, int headerH, bool special);
// Белое поле ввода с утопленной рамкой (адресная строка)
void DrawEditField(Surface& s, const Rect& r);
// Строка состояния окна
void DrawStatusBar(Surface& s, const Rect& r);
// Пунктирная рамка фокуса
void DrawFocusRect(Surface& s, const Rect& r);

// --- Меню «Пуск» ---

// Рамка меню: синяя шапка headerH со скруглёнными углами и оранжевой
// полоской, белый левый столбец до splitX, голубой правый, синий низ footerH
void DrawStartMenuFrame(Surface& s, const Rect& r, int headerH, int footerH, int splitX);
// Картинка пользователя в белой рамке (size x size)
void DrawUserPicture(Surface& s, int x, int y, int size);
// Кнопка «Выключение»: красный квадрат со знаком питания
void DrawPowerGlyph(Surface& s, int x, int y, int size, bool hot);
// Зелёная стрелка «Все программы»
void DrawAllProgramsArrow(Surface& s, int x, int y, int size);
// Разделитель в меню: тонкая линия, гаснущая к краям
void DrawMenuSeparator(Surface& s, int left, int right, int y);

} // namespace xp
} // namespace ui
