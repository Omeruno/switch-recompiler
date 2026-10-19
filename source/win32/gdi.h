#pragma once
// gdi.h
//
// Минимальная, но настоящая реализация GDI поверх собственного пиксельного
// буфера. Мы не эмулируем Windows GDI побитово — мы честно реализуем тот же
// контракт (SetPixel рисует пиксель, BitBlt копирует прямоугольник, LineTo
// рисует линию выбранным пером) поверх наших собственных структур данных.
//
// Формат хранения пикселя внутри нас: 0xAARRGGBB (alpha всегда 0xFF).
// ВАЖНО: Windows COLORREF — это 0x00BBGGRR (порядок байт ДРУГОЙ!), поэтому
// конвертация цвета на входе/выходе принципиальна — перепутать порядок R/B
// это классическая ошибка при портировании GDI-кода.

#include <cstdint>
#include <vector>
#include <unordered_map>

namespace gdi {

constexpr uint32_t GDI_HANDLE_BASE = 0xA0000000u;

// Windows COLORREF (0x00BBGGRR) -> наш внутренний формат (0xAARRGGBB)
inline uint32_t ColorRefToArgb(uint32_t colorref) {
    uint8_t r = static_cast<uint8_t>(colorref & 0xFF);
    uint8_t g = static_cast<uint8_t>((colorref >> 8) & 0xFF);
    uint8_t b = static_cast<uint8_t>((colorref >> 16) & 0xFF);
    return 0xFF000000u | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

// Обратно: наш формат -> Windows COLORREF (нужно для возврата "предыдущего
// цвета пикселя" из SetPixel — так по контракту делает настоящая функция)
inline uint32_t ArgbToColorRef(uint32_t argb) {
    uint8_t r = static_cast<uint8_t>((argb >> 16) & 0xFF);
    uint8_t g = static_cast<uint8_t>((argb >> 8) & 0xFF);
    uint8_t b = static_cast<uint8_t>(argb & 0xFF);
    return (static_cast<uint32_t>(b) << 16) | (static_cast<uint32_t>(g) << 8) | r;
}

struct Bitmap {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels; // ARGB, width*height элементов, row-major

    bool InBounds(int x, int y) const {
        return x >= 0 && y >= 0 && x < width && y < height;
    }
};

struct PenObject {
    int style = 0;
    int width = 1;
    uint32_t colorArgb = 0xFF000000u; // чёрный по умолчанию
};

struct BrushObject {
    uint32_t colorArgb = 0xFFFFFFFFu; // белый по умолчанию
};

enum class ObjectKind { Bitmap, Pen, Brush };

struct GdiObject {
    ObjectKind kind;
    Bitmap bitmap;
    PenObject pen;
    BrushObject brush;
};

struct DeviceContext {
    uint32_t selectedBitmap = 0; // 0 = нет привязанного битмапа (рисование некуда)
    uint32_t selectedPen = 0;
    uint32_t selectedBrush = 0;
    int currentX = 0;
    int currentY = 0;
    bool isScreenDC = false; // true для DC, полученного через GetDC (не CreateCompatibleDC)
    // Состояние текста/фона DC (SetTextColor/SetBkColor/SetBkMode). Paint
    // использует их и для монохромных шаблонов кисти, не только для текста.
    uint32_t textColorRef = 0x00000000u; // COLORREF, по умолчанию чёрный (как в Win32)
    uint32_t bkColorRef   = 0x00FFFFFFu; // COLORREF, по умолчанию белый
    int bkMode = 2;                      // OPAQUE (Win32 по умолчанию); TRANSPARENT = 1
    int rop2 = 13;                       // R2_COPYPEN (Win32 по умолчанию)
};

struct GdiPoint { int x; int y; };

class GdiSystem {
public:
    // --- DC ---
    uint32_t GetOrCreateScreenDC(int screenWidth, int screenHeight);
    uint32_t CreateCompatibleDC();
    void DeleteDC(uint32_t hdc);

    // --- Объекты ---
    uint32_t CreateCompatibleBitmapObj(int width, int height);
    uint32_t CreatePenObj(int style, int width, uint32_t colorref);
    uint32_t CreateBrushObj(uint32_t colorArgb); // сплошная кисть — для CreateSolidBrush/CreatePatternBrush
    uint32_t GetStockObject(int stockObjectId); // возвращает уже существующий "вечный" хендл
    void DeleteObject(uint32_t hObject);

    // Возвращает ПРЕДЫДУЩИЙ выбранный объект того же типа (контракт SelectObject)
    uint32_t SelectObject(uint32_t hdc, uint32_t hObject);

    // --- Рисование ---
    uint32_t SetPixel(uint32_t hdc, int x, int y, uint32_t colorref); // возвращает старый COLORREF
    uint32_t GetPixel(uint32_t hdc, int x, int y) const; // честное чтение, для Пипетки
    void MoveTo(uint32_t hdc, int x, int y);
    bool GetCurrentPos(uint32_t hdc, int& x, int& y) const;
    void LineTo(uint32_t hdc, int x, int y); // рисует текущим пером DC

    // Фигуры (эта сессия). Семантика Win32: заливка выбранной кистью
    // (пропускается для NULL_BRUSH), контур выбранным пером (пропускается
    // для NULL_PEN / PS_NULL). Прямоугольные границы как в Win32: правый и
    // нижний край НЕ включаются.
    void Polygon(uint32_t hdc, const GdiPoint* pts, int count);
    void Polyline(uint32_t hdc, const GdiPoint* pts, int count);
    void Rectangle(uint32_t hdc, int left, int top, int right, int bottom);
    void Ellipse(uint32_t hdc, int left, int top, int right, int bottom);
    // FillRect: заливка ЗАДАННОЙ кистью (не выбранной в DC), без контура
    void FillRectWithBrush(uint32_t hdc, int left, int top, int right, int bottom, uint32_t hBrush);

    // Состояние текста/фона. Возвращают прежнее значение (контракт Win32).
    uint32_t SetTextColor(uint32_t hdc, uint32_t colorref);
    uint32_t SetBkColor(uint32_t hdc, uint32_t colorref);
    int SetBkMode(uint32_t hdc, int mode);
    uint32_t GetTextColor(uint32_t hdc) const;
    uint32_t GetBkColor(uint32_t hdc) const;
    int GetBkMode(uint32_t hdc) const;

    // Режим смешивания пера/кисти с приёмником (SetROP2). Paint рисует
    // "резиновые" превью (рамки фигур, линия, курсор ластика, выделение) в
    // R2_NOT/R2_XORPEN: повторная отрисовка стирает предыдущую. Без этого
    // промежуточные превью оставались следами ("лесенка").
    int SetROP2(uint32_t hdc, int mode);
    int GetROP2(uint32_t hdc) const;
    void BitBlt(uint32_t hdcDst, int xDst, int yDst, int w, int h,
                uint32_t hdcSrc, int xSrc, int ySrc);

    // true, если с последнего вызова что-то нарисовали в "экранный" DC —
    // main.cpp опрашивает это, чтобы понять, когда обновлять реальный экран Switch
    bool ConsumeScreenDirtyFlag();

    // Доступ к пиксельным данным DC — нужен, чтобы вывести "экранный" DC
    // на настоящий framebuffer Switch
    // Хендл "экранного" DC (0, если ещё не создавался) — нужен, чтобы
    // найти его напрямую, не перебирая диапазон хендлов.
    uint32_t GetScreenDCHandle() const { return screenDcHandle_; }

    const Bitmap* GetBitmapForDC(uint32_t hdc) const;
    bool IsScreenDC(uint32_t hdc) const;

    // Заливает "экранный" DC заданным цветом ARGB — нужно при
    // создании/пересоздании окна (реальная Windows автоматически закрашивает
    // фон окна кистью hbrBackground ПЕРЕД тем, как приложение рисует поверх;
    // у нас такого автоматического шага не было — оставляли чёрным).
    void FillScreenDC(uint32_t argbColor);
    void ClearScreenDC() { FillScreenDC(0xFF000000u); } // чёрным — для обратной совместимости вызовов

    // Прямой мутабельный доступ к пикселям битмапа ПО ЕГО СОБСТВЕННОМУ
    // хендлу (не через DC) — нужен, например, CreateBitmap для распаковки
    // переданных вызывающим кодом исходных пиксельных данных сразу при
    // создании. nullptr, если хендл не битмап или не существует.
    Bitmap* GetBitmapObjectMutable(uint32_t hBitmap);

    // Объект по хендлу (nullptr, если нет). Публичный: нужен помощникам
    // заливки/пера в gdi.cpp и честному FillRect в host_functions.cpp.
    GdiObject* GetObject(uint32_t handle);

private:
    Bitmap* GetBitmapForDCMutable(uint32_t hdc);

    std::unordered_map<uint32_t, DeviceContext> dcs_;
    std::unordered_map<uint32_t, GdiObject> objects_;
    uint32_t nextHandle_ = GDI_HANDLE_BASE;
    uint32_t screenDcHandle_ = 0; // 0 = ещё не создан
    bool screenDirty_ = false;
};

extern GdiSystem g_gdi;

} // namespace gdi
