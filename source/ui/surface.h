#pragma once
// surface.h
//
// Поверхность — прямоугольный массив пикселей в формате DIB Windows:
// 1/4/8 бит с палитрой, 16 бит (5-5-5 или 5-6-5), 24 и 32 бита. Пиксели
// могут лежать в памяти хоста (экран, совместимые битмапы) или прямо в
// памяти гостя (DIB-секции: программа пишет в них сама, без копирования).
//
// Цвет внутри подсистемы — 0x00RRGGBB (как пиксель 32-битного DIB).
// COLORREF Windows — 0x00BBGGRR; переводы — ColorrefToRgb/RgbToColorref.

#include <cstdint>
#include <vector>

namespace ui {

inline uint32_t ColorrefToRgb(uint32_t c) {
    return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF);
}
inline uint32_t RgbToColorref(uint32_t c) { return ColorrefToRgb(c); }

// Optional write-through target for window rasterization. Destination reads
// remain on the authoritative CPU screen (including screen-DC ROP writes).
// Captured pixels come from individual raster writes, never screen extraction.
struct SurfaceWriteCapture {
    uint32_t* owners=nullptr;
    int screenWidth=0,screenHeight=0;
    uint32_t token=0;
    uint32_t* pixels=nullptr;
    int left=0,top=0,width=0,height=0;
    void Write(int x,int y,uint32_t rgb) const {
        if(!owners || x<0 || y<0 || x>=screenWidth || y>=screenHeight)return;
        const int64_t lx=int64_t(x)-left,ly=int64_t(y)-top;
        if(pixels && lx>=0 && ly>=0 && lx<width && ly<height) {
            pixels[size_t(ly)*width+size_t(lx)]=rgb&0xFFFFFFu;
            owners[size_t(y)*screenWidth+x]=token;
        } else owners[size_t(y)*screenWidth+x]=0; // direct/fallback screen write
    }
};

struct Surface {
    int width = 0, height = 0;
    int bpp = 32;
    int pitch = 0;            // байт на строку; отрицательный — DIB снизу вверх
    uint8_t* bits = nullptr;  // начало строки 0 (верхней)
    bool rgb565 = false;      // 16 бит: 5-6-5 вместо 5-5-5
    std::vector<uint32_t> palette; // 0x00RRGGBB для 1/4/8 бит
    const SurfaceWriteCapture* writeCapture=nullptr; // synchronous raster call only

    bool Valid() const { return bits && width > 0 && height > 0; }

    uint8_t* Row(int y) const { return bits + static_cast<intptr_t>(y) * pitch; }

    uint32_t Get(int x, int y) const {
        const uint8_t* row = Row(y);
        switch (bpp) {
        case 32: return reinterpret_cast<const uint32_t*>(row)[x] & 0xFFFFFFu;
        case 24: { const uint8_t* p = row + x * 3; return p[0] | (p[1] << 8) | (p[2] << 16); }
        case 16: {
            uint32_t v = row[x * 2] | (row[x * 2 + 1] << 8);
            uint32_t r, g, b;
            if (rgb565) { r = (v >> 11) & 31; g = (v >> 5) & 63; b = v & 31; g = (g << 2) | (g >> 4); }
            else { r = (v >> 10) & 31; g = (v >> 5) & 31; b = v & 31; g = (g << 3) | (g >> 2); }
            r = (r << 3) | (r >> 2);
            b = (b << 3) | (b >> 2);
            return (r << 16) | (g << 8) | b;
        }
        case 8: return PaletteColor(row[x]);
        case 4: return PaletteColor((row[x >> 1] >> ((x & 1) ? 0 : 4)) & 15);
        case 1: return PaletteColor((row[x >> 3] >> (7 - (x & 7))) & 1);
        }
        return 0;
    }

    void Set(int x, int y, uint32_t rgb) {
        uint8_t* row = Row(y);
        switch (bpp) {
        case 32:
            reinterpret_cast<uint32_t*>(row)[x]=rgb&0xFFFFFFu;
            if(writeCapture)writeCapture->Write(x,y,rgb);
            return;
        case 24: { uint8_t* p = row + x * 3; p[0] = rgb & 0xFF; p[1] = (rgb >> 8) & 0xFF; p[2] = (rgb >> 16) & 0xFF; return; }
        case 16: {
            uint32_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
            uint32_t v = rgb565 ? (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
                                : (((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
            row[x * 2] = v & 0xFF;
            row[x * 2 + 1] = (v >> 8) & 0xFF;
            return;
        }
        case 8: row[x] = static_cast<uint8_t>(NearestIndex(rgb)); return;
        case 4: {
            uint8_t idx = static_cast<uint8_t>(NearestIndex(rgb));
            uint8_t& b = row[x >> 1];
            b = (x & 1) ? static_cast<uint8_t>((b & 0xF0) | idx) : static_cast<uint8_t>((b & 0x0F) | (idx << 4));
            return;
        }
        case 1: {
            uint8_t& b = row[x >> 3];
            uint8_t bit = static_cast<uint8_t>(0x80 >> (x & 7));
            if (NearestIndex(rgb)) b |= bit; else b &= static_cast<uint8_t>(~bit);
            return;
        }
        }
    }

    // Сырое значение пикселя (индекс палитры для 1/4/8 бит) — для BitBlt
    // между поверхностями с одинаковой палитрой без потери индексов.
    uint32_t GetRaw(int x, int y) const {
        const uint8_t* row = Row(y);
        switch (bpp) {
        case 8: return row[x];
        case 4: return (row[x >> 1] >> ((x & 1) ? 0 : 4)) & 15;
        case 1: return (row[x >> 3] >> (7 - (x & 7))) & 1;
        default: return Get(x, y);
        }
    }

    uint32_t PaletteColor(uint32_t index) const {
        if (index < palette.size()) return palette[index];
        // палитры нет: оттенки серого (1 бит — чёрный/белый)
        if (bpp == 1) return index ? 0xFFFFFFu : 0;
        uint32_t max = (1u << bpp) - 1;
        uint32_t v = max ? index * 255 / max : 0;
        return (v << 16) | (v << 8) | v;
    }

    uint32_t NearestIndex(uint32_t rgb) const {
        size_t n = palette.empty() ? (1u << bpp) : palette.size();
        uint32_t best = 0, bestDist = 0xFFFFFFFFu;
        for (size_t i = 0; i < n; ++i) {
            uint32_t c = PaletteColor(static_cast<uint32_t>(i));
            int dr = static_cast<int>((c >> 16) & 0xFF) - static_cast<int>((rgb >> 16) & 0xFF);
            int dg = static_cast<int>((c >> 8) & 0xFF) - static_cast<int>((rgb >> 8) & 0xFF);
            int db = static_cast<int>(c & 0xFF) - static_cast<int>(rgb & 0xFF);
            uint32_t d = static_cast<uint32_t>(dr * dr + dg * dg + db * db);
            if (d < bestDist) { bestDist = d; best = static_cast<uint32_t>(i); if (!d) break; }
        }
        return best;
    }

    // Байт на строку для DIB Windows: выравнивание на 4 байта
    static int DibPitch(int width, int bpp) { return ((width * bpp + 31) / 32) * 4; }
};

// Поверхность со своим буфером в памяти хоста (32 бита)
struct OwnedSurface {
    Surface s;
    std::vector<uint8_t> storage;

    void Allocate(int w, int h, int bpp = 32) {
        s.width = w;
        s.height = h;
        s.bpp = bpp;
        s.pitch = Surface::DibPitch(w, bpp);
        storage.assign(static_cast<size_t>(s.pitch) * (h > 0 ? h : 0), 0);
        s.bits = storage.empty() ? nullptr : storage.data();
    }
};

} // namespace ui
