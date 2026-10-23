// user32_image.cpp — иконки, курсоры и картинки USER32 (слой "ui2"):
// LoadIcon/LoadCursor/LoadBitmap/LoadImage из ресурсов модулей и из файлов
// (.bmp, .ico, .cur), системные иконки, CreateIconIndirect, DrawIcon(Ex),
// CopyImage, CreateIconFromResource(Ex), LookupIconIdFromDirectory(Ex).

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "../runtime/files.h"
#include "../runtime/process.h"
#include "../runtime/resources.h"
#include "user32_controls.h"

namespace win32 {
namespace user {

using namespace hle;

namespace {

struct Icon {
    int w = 0, h = 0;
    ui::OwnedSurface color;           // 32 бита
    std::vector<uint8_t> transparent; // 1 — прозрачный пиксель
    int hotX = 0, hotY = 0;
    bool cursor = false;
    bool shared = false;
};
struct IconState {
    std::map<uint32_t,std::unique_ptr<Icon>> icons;
    std::map<uint32_t,uint32_t> system;uint32_t next=0x00090010;
};
IconState& IconData(){return rt::LocalState<IconState>();}
std::map<uint32_t,std::unique_ptr<Icon>>& Icons(){return IconData().icons;}

uint32_t AddIcon(std::unique_ptr<Icon> ic) {
    uint32_t h = IconData().next;
    IconData().next += 4;
    Icons()[h] = std::move(ic);
    return h;
}
Icon* I(uint32_t h) {
    auto it = Icons().find(h);
    return it == Icons().end() ? nullptr : it->second.get();
}

uint16_t R16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t R32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<uint32_t>(p[3]) << 24); }

// Разбор DIB в памяти хоста (BITMAPINFOHEADER или BITMAPCOREHEADER + цвета + пиксели)
struct Dib {
    int w = 0, h = 0, bpp = 0;
    bool topDown = false;
    uint32_t compression = 0;
    std::vector<uint32_t> palette;
    const uint8_t* bits = nullptr;
    int pitch = 0;
    uint32_t masks[3] = {0, 0, 0};
    bool valid = false;
    std::vector<uint8_t> unpacked; // пиксели, распакованные из RLE

    uint32_t Pixel(int x, int y) const { // 0x00RRGGBB, y сверху
        int row = topDown ? y : h - 1 - y;
        const uint8_t* r = bits + static_cast<size_t>(row) * pitch;
        switch (bpp) {
        case 1: { uint32_t i = (r[x >> 3] >> (7 - (x & 7))) & 1; return i < palette.size() ? palette[i] : (i ? 0xFFFFFF : 0); }
        case 4: { uint32_t i = (r[x >> 1] >> ((x & 1) ? 0 : 4)) & 15; return i < palette.size() ? palette[i] : 0; }
        case 8: { uint32_t i = r[x]; return i < palette.size() ? palette[i] : 0; }
        case 16: {
            uint32_t v = R16(r + x * 2);
            if (compression == 3 && masks[1] == 0x7E0) return (((v >> 11) & 31) * 255 / 31) << 16 | (((v >> 5) & 63) * 255 / 63) << 8 | ((v & 31) * 255 / 31);
            return (((v >> 10) & 31) * 255 / 31) << 16 | (((v >> 5) & 31) * 255 / 31) << 8 | ((v & 31) * 255 / 31);
        }
        case 24: return r[x * 3] | (r[x * 3 + 1] << 8) | (r[x * 3 + 2] << 16);
        case 32: return R32(r + x * 4) & 0xFFFFFF;
        }
        return 0;
    }
    uint8_t Alpha(int x, int y) const {
        if (bpp != 32) return 255;
        int row = topDown ? y : h - 1 - y;
        return bits[static_cast<size_t>(row) * pitch + x * 4 + 3];
    }
};

// Распаковка BI_RLE8 (bpp 8) и BI_RLE4 (bpp 4) в обычные строки снизу вверх.
// Пропущенные (смещением) пиксели остаются с индексом 0.
void UnpackRle(const uint8_t* src, size_t n, int w, int h, int bpp, int pitch, std::vector<uint8_t>& out) {
    out.assign(static_cast<size_t>(pitch) * h, 0);
    int x = 0, y = 0;
    auto put = [&](int idx) {
        if (x < w && y < h) {
            uint8_t* row = out.data() + static_cast<size_t>(y) * pitch;
            if (bpp == 8) row[x] = static_cast<uint8_t>(idx);
            else row[x >> 1] |= static_cast<uint8_t>((idx & 15) << ((x & 1) ? 0 : 4));
        }
        ++x;
    };
    size_t i = 0;
    while (i + 1 < n && y < h) {
        uint8_t count = src[i], value = src[i + 1];
        i += 2;
        if (count) {
            for (int k = 0; k < count; ++k) put(bpp == 8 ? value : ((k & 1) ? value & 15 : value >> 4));
            continue;
        }
        if (value == 0) { x = 0; ++y; }          // конец строки
        else if (value == 1) break;              // конец картинки
        else if (value == 2) {                   // смещение
            if (i + 1 >= n) break;
            x += src[i];
            y += src[i + 1];
            i += 2;
        } else {                                 // value пикселей как есть
            size_t bytes = bpp == 8 ? value : (value + 1u) / 2;
            for (int k = 0; k < value && i + (bpp == 8 ? k : k / 2) < n; ++k)
                put(bpp == 8 ? src[i + k] : ((k & 1) ? src[i + k / 2] & 15 : src[i + k / 2] >> 4));
            i += (bytes + 1) & ~static_cast<size_t>(1); // выравнивание на слово
        }
    }
}

bool ParseDib(const uint8_t* p, size_t size, Dib& d, int heightOverride = 0) {
    if (size < 12) return false;
    uint32_t hs = R32(p);
    size_t colors;
    if (hs == 12) {
        d.w = R16(p + 4);
        d.h = static_cast<int16_t>(R16(p + 6));
        d.bpp = R16(p + 10);
    } else {
        if (size < 40) return false;
        d.w = static_cast<int32_t>(R32(p + 4));
        d.h = static_cast<int32_t>(R32(p + 8));
        d.bpp = R16(p + 14);
        d.compression = R32(p + 16);
    }
    if (d.h < 0) { d.topDown = true; d.h = -d.h; }
    if (heightOverride) d.h = heightOverride;
    bool rle = (d.compression == 1 && d.bpp == 8) || (d.compression == 2 && d.bpp == 4);
    if (d.compression != 0 && d.compression != 3 && !rle) return false;
    if (rle && d.topDown) return false; // RLE бывает только снизу вверх
    size_t off = hs;
    if (d.compression == 3) {
        if (off + 12 > size) return false;
        for (int i = 0; i < 3; ++i) d.masks[i] = R32(p + off + i * 4);
        if (hs == 40) off += 12;
    }
    if (d.bpp <= 8) {
        uint32_t used = hs >= 40 ? R32(p + 32) : 0;
        colors = used ? used : (1u << d.bpp);
        size_t entry = hs == 12 ? 3 : 4;
        for (size_t i = 0; i < colors && off + entry <= size; ++i, off += entry)
            d.palette.push_back(p[off] | (p[off + 1] << 8) | (p[off + 2] << 16));
    }
    d.pitch = ui::Surface::DibPitch(d.w, d.bpp);
    if (rle) {
        if (d.w <= 0 || d.h <= 0 || off >= size) return false;
        UnpackRle(p + off, size - off, d.w, d.h, d.bpp, d.pitch, d.unpacked);
        d.compression = 0;
        d.bits = d.unpacked.data();
        d.valid = true;
        return true;
    }
    if (off + static_cast<size_t>(d.pitch) * d.h > size) return false;
    d.bits = p + off;
    d.valid = d.w > 0 && d.h > 0;
    return d.valid;
}

// Иконка/курсор из данных RT_ICON/RT_CURSOR (или из файла .ico)
std::unique_ptr<Icon> IconFromData(const uint8_t* p, size_t size, bool cursor) {
    std::unique_ptr<Icon> ic(new Icon);
    ic->cursor = cursor;
    if (cursor && size > 4) { ic->hotX = R16(p); ic->hotY = R16(p + 2); p += 4; size -= 4; }
    if (size >= 8 && std::memcmp(p, "\x89PNG", 4) == 0) {
        // PNG-иконки Vista+ не поддерживаем: пустая 32x32
        ic->w = ic->h = 32;
        ic->color.Allocate(32, 32);
        ic->transparent.assign(32 * 32, 1);
        return ic;
    }
    Dib x;
    uint32_t fullH = size >= 12 ? R32(p + 8) : 0;
    int h = static_cast<int>(static_cast<int32_t>(fullH)) / 2;
    if (!ParseDib(p, size, x, h)) return nullptr;
    ic->w = x.w;
    ic->h = x.h;
    ic->color.Allocate(x.w, x.h);
    ic->transparent.assign(static_cast<size_t>(x.w) * x.h, 0);
    // маска AND (1 бит) — сразу после цветных пикселей
    const uint8_t* mask = x.bits + static_cast<size_t>(x.pitch) * x.h;
    int mpitch = ui::Surface::DibPitch(x.w, 1);
    bool haveMask = mask + static_cast<size_t>(mpitch) * x.h <= p + size;
    bool anyAlpha = false;
    if (x.bpp == 32) for (int y = 0; y < x.h && !anyAlpha; ++y) for (int xx = 0; xx < x.w; ++xx) if (x.Alpha(xx, y)) { anyAlpha = true; break; }
    for (int y = 0; y < x.h; ++y)
        for (int xx = 0; xx < x.w; ++xx) {
            ic->color.s.Set(xx, y, x.Pixel(xx, y));
            bool t = false;
            if (anyAlpha) t = x.Alpha(xx, y) < 128;
            else if (haveMask) t = (mask[static_cast<size_t>(x.h - 1 - y) * mpitch + (xx >> 3)] >> (7 - (xx & 7))) & 1;
            ic->transparent[static_cast<size_t>(y) * x.w + xx] = t ? 1 : 0;
        }
    return ic;
}

// Выбор картинки из каталога иконок (RT_GROUP_ICON или заголовок .ico)
int PickFromDirectory(const uint8_t* dir, size_t size, bool fromFile, int wantW, int wantH) {
    if (size < 6) return -1;
    uint16_t count = R16(dir + 4);
    size_t entry = fromFile ? 16 : 14;
    int best = -1, bestScore = -1;
    for (uint16_t i = 0; i < count && 6 + (i + 1) * entry <= size; ++i) {
        const uint8_t* e = dir + 6 + i * entry;
        int w = e[0] ? e[0] : 256, h = e[1] ? e[1] : 256;
        int bpp = R16(e + 6);
        int score = 1000000 - std::abs(w - wantW) * 1000 - std::abs(h - wantH) * 1000 + std::min(bpp, 32);
        if (score > bestScore) { bestScore = score; best = i; }
    }
    return best;
}

rt::Module* ModuleOf(uint32_t hInstance) {
    if (!rt::g_process) return nullptr;
    if (!hInstance) return rt::g_process->MainModule();
    return rt::g_process->FindByHandle(hInstance);
}

rt::ResId ResName(uint32_t p, bool unicode) {
    if (p < 0x10000) return rt::ResId::Num(static_cast<uint16_t>(p));
    std::u16string s = ReadText(p, unicode);
    if (!s.empty() && s[0] == '#') {
        uint32_t v = 0;
        for (size_t i = 1; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) v = v * 10 + (s[i] - '0');
        return rt::ResId::Num(static_cast<uint16_t>(v));
    }
    for (char16_t& ch : s) if (ch >= 'a' && ch <= 'z') ch = static_cast<char16_t>(ch - 32);
    return rt::ResId::Str(s);
}

uint32_t IconFromResource(rt::Module* mod, const rt::ResId& name, bool cursor, int cx, int cy) {
    rt::ResourceData gd;
    if (!rt::FindResource(mod, rt::ResId::Num(cursor ? rt::RT_GROUP_CURSOR : rt::RT_GROUP_ICON), name, gd)) return 0;
    const uint8_t* dir = rt::ResourcePtr(mod, gd);
    int idx = PickFromDirectory(dir, gd.size, false, cx ? cx : 32, cy ? cy : 32);
    if (idx < 0) return 0;
    uint16_t id = R16(dir + 6 + idx * 14 + 12);
    rt::ResourceData d;
    if (!rt::FindResource(mod, rt::ResId::Num(cursor ? rt::RT_CURSOR : rt::RT_ICON), rt::ResId::Num(id), d)) return 0;
    std::unique_ptr<Icon> ic = IconFromData(rt::ResourcePtr(mod, d), d.size, cursor);
    return ic ? AddIcon(std::move(ic)) : 0;
}

// Системные иконки: простые рисунки в духе Windows
uint32_t SystemIcon(uint32_t id) {
    auto& cache=IconData().system;
    auto it = cache.find(id);
    if (it != cache.end()) return it->second;
    std::unique_ptr<Icon> ic(new Icon);
    ic->w = ic->h = 32;
    ic->color.Allocate(32, 32);
    ic->transparent.assign(32 * 32, 1);
    auto put = [&](int x, int y, uint32_t c) {
        if (x < 0 || y < 0 || x >= 32 || y >= 32) return;
        ic->color.s.Set(x, y, c);
        ic->transparent[static_cast<size_t>(y) * 32 + x] = 0;
    };
    uint32_t fill = 0xFFFFFF, mark = 0x0000FF;
    switch (id) {
    case 32513: fill = 0xFF0000; mark = 0xFFFFFF; break;  // ошибка: красный круг
    case 32514: fill = 0xFFFFFF; mark = 0x0000FF; break;  // вопрос
    case 32515: fill = 0xFFFF00; mark = 0x000000; break;  // предупреждение
    case 32516: fill = 0xFFFFFF; mark = 0x0000FF; break;  // информация
    default: fill = 0xFFFFFF; mark = 0x000080; break;     // приложение
    }
    if (id == 32515) {
        for (int y = 2; y < 30; ++y) {
            int half = (y - 2) / 2 + 1;
            for (int x = 16 - half; x <= 15 + half; ++x) put(x, y, (x == 16 - half || x == 15 + half || y == 29) ? 0 : fill);
        }
        for (int y = 9; y < 22; ++y) { put(15, y, 0); put(16, y, 0); }
        for (int y = 24; y < 27; ++y) { put(15, y, 0); put(16, y, 0); }
    } else if (id >= 32513 && id <= 32516) {
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 32; ++x) {
                int dx = x * 2 - 31, dy = y * 2 - 31;
                int d2 = dx * dx + dy * dy;
                if (d2 <= 30 * 30) put(x, y, d2 >= 27 * 27 ? 0x000000 : fill);
            }
        if (id == 32513) {
            for (int k = -6; k <= 6; ++k) for (int t = -1; t <= 1; ++t) { put(16 + k + t, 16 + k, mark); put(16 + k + t, 16 - k, mark); }
        } else if (id == 32516) {
            for (int y = 7; y < 10; ++y) for (int x = 15; x < 18; ++x) put(x, y, mark);
            for (int y = 12; y < 25; ++y) for (int x = 15; x < 18; ++x) put(x, y, mark);
        } else {
            static const char* const q[] = {"..###..", ".#...#.", ".....#.", "....#..", "...#...", "...#...", ".......", "...#..."};
            for (int y = 0; y < 8; ++y) for (int x = 0; x < 7; ++x) if (q[y][x] == '#')
                for (int a = 0; a < 2; ++a) for (int b = 0; b < 2; ++b) put(9 + x * 2 + a, 8 + y * 2 + b, mark);
        }
    } else {
        // окно приложения
        for (int y = 4; y < 28; ++y) for (int x = 2; x < 30; ++x) put(x, y, (y < 9) ? 0x0A246A : (x == 2 || x == 29 || y == 27) ? 0 : 0xFFFFFF);
    }
    uint32_t h = AddIcon(std::move(ic));
    Icons()[h]->shared = true;
    cache[id] = h;
    return h;
}

// Картинка (DIB) -> битмап: DDB (32 или 1 бит) или DIB-секция
uint32_t BitmapFromDib(const Dib& d, bool dibSection, bool monochromeKeep) {
    if (!d.valid) return 0;
    if (dibSection) {
        cpu::IGuestMemory& m = *G().m;
        uint32_t size = static_cast<uint32_t>(d.pitch) * static_cast<uint32_t>(d.h);
        uint32_t bits = m.Allocate(size, cpu::kProtRW);
        if (!bits) return 0;
        // в памяти DIB снизу вверх, как у программы
        for (int y = 0; y < d.h; ++y) {
            int srcRow = d.topDown ? d.h - 1 - y : y;
            m.LoadBytes(bits + static_cast<uint32_t>(y * d.pitch), d.bits + static_cast<size_t>(srcRow) * d.pitch, static_cast<size_t>(d.pitch));
        }
        ui::Surface v;
        v.width = d.w;
        v.height = d.h;
        v.bpp = d.bpp;
        v.palette = d.palette;
        v.rgb565 = d.compression == 3 && d.masks[1] == 0x7E0;
        v.pitch = -d.pitch;
        v.bits = m.HostPointer(bits) + static_cast<size_t>(d.pitch) * (d.h - 1);
        std::vector<uint8_t> header(40, 0);
        uint32_t vals[] = {40, static_cast<uint32_t>(d.w), static_cast<uint32_t>(d.h)};
        std::memcpy(header.data(), vals, 12);
        header[12] = 1;
        header[14] = static_cast<uint8_t>(d.bpp);
        return ui::g_gdi.CreateDibSection(v, bits, header);
    }
    bool mono = monochromeKeep && d.bpp == 1;
    uint32_t hb = ui::g_gdi.CreateBitmap(d.w, d.h, mono ? 1 : 32);
    ui::BitmapObj* b = ui::g_gdi.Bitmap(hb);
    if (!b) return 0;
    for (int y = 0; y < d.h; ++y)
        for (int x = 0; x < d.w; ++x) {
            uint32_t px = d.Pixel(x, y);
            if (mono) b->view.Set(x, y, px == 0 ? 0 : 0xFFFFFF);
            else b->view.Set(x, y, px);
        }
    return hb;
}

uint32_t BitmapFromResource(rt::Module* mod, const rt::ResId& name, bool dibSection) {
    rt::ResourceData d;
    if (!rt::FindResource(mod, rt::ResId::Num(rt::RT_BITMAP), name, d)) return 0;
    Dib dib;
    if (!ParseDib(rt::ResourcePtr(mod, d), d.size, dib)) return 0;
    return BitmapFromDib(dib, dibSection, true);
}

bool ReadFileBytes(const std::u16string& guestPath, std::vector<uint8_t>& out) {
    std::string host = rt::FileSystem().ToHost(WideToUtf8(guestPath));
    FILE* f = std::fopen(host.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) { std::fclose(f); return false; }
    out.resize(static_cast<size_t>(n));
    bool ok = std::fread(out.data(), 1, out.size(), f) == out.size();
    std::fclose(f);
    return ok;
}

uint32_t LoadImageCommon(cpu::IGuestMemory& m, uint32_t hInst, uint32_t name, uint32_t type, int cx, int cy, uint32_t flags, bool unicode) {
    (void)m;
    constexpr uint32_t LR_LOADFROMFILE = 0x10, LR_CREATEDIBSECTION = 0x2000, LR_DEFAULTSIZE = 0x40;
    if ((flags & LR_DEFAULTSIZE) || type != 0) { if (!cx) cx = type == 0 ? 0 : 32; if (!cy) cy = type == 0 ? 0 : 32; }
    if (flags & LR_LOADFROMFILE) {
        std::vector<uint8_t> data;
        if (!ReadFileBytes(ReadText(name, unicode), data)) return 0;
        if (type == 0) { // .bmp
            if (data.size() < 14 || data[0] != 'B' || data[1] != 'M') return 0;
            Dib d;
            if (!ParseDib(data.data() + 14, data.size() - 14, d)) return 0;
            // смещение пикселей из BITMAPFILEHEADER
            uint32_t off = R32(data.data() + 10);
            if (off >= 14 && off < data.size()) d.bits = data.data() + off;
            return BitmapFromDib(d, (flags & LR_CREATEDIBSECTION) != 0, true);
        }
        int idx = PickFromDirectory(data.data(), data.size(), true, cx ? cx : 32, cy ? cy : 32);
        if (idx < 0) return 0;
        const uint8_t* e = data.data() + 6 + idx * 16;
        uint32_t size = R32(e + 8), off = R32(e + 12);
        if (static_cast<size_t>(off) + size > data.size()) return 0;
        std::vector<uint8_t> blob;
        if (type == 2) { blob.push_back(e[4]); blob.push_back(e[5]); blob.push_back(e[6]); blob.push_back(e[7]); }
        blob.insert(blob.end(), data.begin() + off, data.begin() + off + size);
        std::unique_ptr<Icon> ic = IconFromData(blob.data(), blob.size(), type == 2);
        return ic ? AddIcon(std::move(ic)) : 0;
    }
    if (!hInst && name < 0x10000) { // системные
        if (type == 1) return SystemIcon(name);
        if (type == 2) return 0x00017F00u | (name & 0xFF);
        return 0;
    }
    rt::Module* mod = ModuleOf(hInst);
    rt::ResId id = ResName(name, unicode);
    if (type == 0) return BitmapFromResource(mod, id, (flags & LR_CREATEDIBSECTION) != 0);
    uint32_t h = IconFromResource(mod, id, type == 2, cx, cy);
    if (!h && type == 1) h = SystemIcon(32512);
    return h;
}

void DrawIconAt(uint32_t hdc, Icon* ic, int x, int y, int cx, int cy, uint32_t flags) {
    if (!ic) return;
    if (cx <= 0) cx = ic->w;
    if (cy <= 0) cy = ic->h;
    for (int yy = 0; yy < cy; ++yy)
        for (int xx = 0; xx < cx; ++xx) {
            int sx = xx * ic->w / cx, sy = yy * ic->h / cy;
            bool t = ic->transparent[static_cast<size_t>(sy) * ic->w + sx] != 0;
            if ((flags & 3) == 1) { ui::g_gdi.SetPixel(hdc, x + xx, y + yy, t ? 0xFFFFFF : 0); continue; } // DI_MASK
            if (t && (flags & 1)) continue;
            ui::g_gdi.SetPixel(hdc, x + xx, y + yy, ui::RgbToColorref(ic->color.s.Get(sx, sy)));
        }
}

#define FN(name) HostCallOutcome name(cpu::CpuState& c, cpu::IGuestMemory& m)
#define ENTER (G().c = &c, G().m = &m)
#define AW(name)                                       \
    FN(name##A) { ENTER; return name##Impl(c, m, false); } \
    FN(name##W) { ENTER; return name##Impl(c, m, true); }
inline uint32_t A(cpu::CpuState& c, cpu::IGuestMemory& m, int i) { return Arg(c, m, i); }

HostCallOutcome LoadIconImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool u) { return Ret(LoadImageCommon(m, A(c, m, 0), A(c, m, 1), 1, 32, 32, 0, u)); }
AW(LoadIcon)
HostCallOutcome LoadCursorImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool u) {
    uint32_t h = LoadImageCommon(m, A(c, m, 0), A(c, m, 1), 2, 32, 32, 0, u);
    return Ret(h ? h : 0x00017F00u);
}
AW(LoadCursor)
HostCallOutcome LoadCursorFromFileImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool u) { return Ret(LoadImageCommon(m, 0, A(c, m, 0), 2, 32, 32, 0x10, u)); }
AW(LoadCursorFromFile)
HostCallOutcome LoadBitmapImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool u) { return Ret(LoadImageCommon(m, A(c, m, 0), A(c, m, 1), 0, 0, 0, 0, u)); }
AW(LoadBitmap)
HostCallOutcome LoadImageImpl(cpu::CpuState& c, cpu::IGuestMemory& m, bool u) {
    return Ret(LoadImageCommon(m, A(c, m, 0), A(c, m, 1), A(c, m, 2), static_cast<int32_t>(A(c, m, 3)), static_cast<int32_t>(A(c, m, 4)), A(c, m, 5), u));
}
AW(LoadImage)

FN(DestroyIcon) {
    ENTER;
    Icon* ic = I(A(c, m, 0));
    if (ic && !ic->shared) Icons().erase(A(c, m, 0));
    return Ret(1);
}
FN(DrawIcon) { ENTER; DrawIconAt(A(c, m, 0), I(A(c, m, 3)), static_cast<int32_t>(A(c, m, 1)), static_cast<int32_t>(A(c, m, 2)), 0, 0, 3); return Ret(1); }
FN(DrawIconEx) {
    ENTER;
    uint32_t flags = A(c, m, 8);
    if (!(flags & 3)) flags |= 3;
    DrawIconAt(A(c, m, 0), I(A(c, m, 3)), static_cast<int32_t>(A(c, m, 1)), static_cast<int32_t>(A(c, m, 2)),
               static_cast<int32_t>(A(c, m, 4)), static_cast<int32_t>(A(c, m, 5)), flags);
    return Ret(1);
}
FN(CreateIconIndirect) {
    ENTER;
    uint32_t p = A(c, m, 0);
    uint32_t hMask = m.Read32(p + 12), hColor = m.Read32(p + 16);
    ui::BitmapObj* mb = ui::g_gdi.Bitmap(hMask);
    ui::BitmapObj* cb = ui::g_gdi.Bitmap(hColor);
    if (!mb) return Ret(0);
    std::unique_ptr<Icon> ic(new Icon);
    ic->cursor = m.Read32(p) == 0;
    ic->hotX = static_cast<int>(m.Read32(p + 4));
    ic->hotY = static_cast<int>(m.Read32(p + 8));
    ic->w = mb->view.width;
    ic->h = cb ? cb->view.height : mb->view.height / 2;
    ic->color.Allocate(ic->w, ic->h);
    ic->transparent.assign(static_cast<size_t>(ic->w) * ic->h, 0);
    for (int y = 0; y < ic->h; ++y)
        for (int x = 0; x < ic->w; ++x) {
            bool t = (mb->view.Get(x, y) & 0xFFFFFF) != 0;
            uint32_t col = cb ? cb->view.Get(x, y) : ((mb->view.Get(x, y + ic->h) & 0xFFFFFF) ? 0xFFFFFF : 0);
            ic->transparent[static_cast<size_t>(y) * ic->w + x] = t ? 1 : 0;
            ic->color.s.Set(x, y, col);
        }
    return Ret(AddIcon(std::move(ic)));
}
FN(GetIconInfo) {
    ENTER;
    uint32_t h = A(c, m, 0), p = A(c, m, 1);
    Icon* ic = I(h);
    if (!ic) {
        if (!p) return Ret(0);
        // курсоры-заглушки: пустая информация
        for (uint32_t i = 0; i < 20; i += 4) m.Write32(p + i, 0);
        m.Write32(p, (h & 0xFFFFFF00u) == 0x00017F00u ? 0 : 1);
        return Ret(1);
    }
    uint32_t mask = ui::g_gdi.CreateBitmap(ic->w, ic->h, 1);
    uint32_t color = ui::g_gdi.CreateBitmap(ic->w, ic->h, 32);
    ui::BitmapObj* mb = ui::g_gdi.Bitmap(mask);
    ui::BitmapObj* cb = ui::g_gdi.Bitmap(color);
    for (int y = 0; y < ic->h; ++y)
        for (int x = 0; x < ic->w; ++x) {
            bool t = ic->transparent[static_cast<size_t>(y) * ic->w + x] != 0;
            mb->view.Set(x, y, t ? 0xFFFFFF : 0);
            cb->view.Set(x, y, t ? 0 : ic->color.s.Get(x, y));
        }
    m.Write32(p, ic->cursor ? 0 : 1);
    m.Write32(p + 4, static_cast<uint32_t>(ic->hotX));
    m.Write32(p + 8, static_cast<uint32_t>(ic->hotY));
    m.Write32(p + 12, mask);
    m.Write32(p + 16, color);
    return Ret(1);
}
FN(CopyIcon) {
    ENTER;
    Icon* ic = I(A(c, m, 0));
    if (!ic) return Ret(A(c, m, 0));
    std::unique_ptr<Icon> n(new Icon);
    n->w = ic->w;
    n->h = ic->h;
    n->color.Allocate(ic->w, ic->h);
    n->color.storage = ic->color.storage;
    n->color.s.bits = n->color.storage.data();
    n->transparent = ic->transparent;
    n->cursor = ic->cursor;
    return Ret(AddIcon(std::move(n)));
}
FN(CopyImage) {
    ENTER;
    uint32_t h = A(c, m, 0), type = A(c, m, 1);
    if (type != 0) return CopyIcon(c, m);
    ui::BitmapObj* b = ui::g_gdi.Bitmap(h);
    if (!b) return Ret(0);
    int cx = static_cast<int32_t>(A(c, m, 2)), cy = static_cast<int32_t>(A(c, m, 3));
    const ui::Surface& s = b->view;
    if (cx <= 0) cx = s.width;
    if (cy <= 0) cy = s.height;
    uint32_t nb = ui::g_gdi.CreateBitmap(cx, cy, s.bpp == 1 ? 1 : 32);
    ui::BitmapObj* d = ui::g_gdi.Bitmap(nb);
    for (int y = 0; y < cy; ++y)
        for (int x = 0; x < cx; ++x) d->view.Set(x, y, s.Get(x * s.width / cx, y * s.height / cy));
    if (A(c, m, 4) & 0x8) ui::g_gdi.DeleteObject(h); // LR_COPYDELETEORG
    return Ret(nb);
}
FN(CreateIconFromResourceEx) {
    ENTER;
    uint32_t p = A(c, m, 0), size = A(c, m, 1);
    std::vector<uint8_t> buf(size);
    m.ReadBytes(p, buf.data(), size);
    std::unique_ptr<Icon> ic = IconFromData(buf.data(), buf.size(), A(c, m, 2) == 0);
    return Ret(ic ? AddIcon(std::move(ic)) : 0);
}
FN(CreateIconFromResource) { return CreateIconFromResourceEx(c, m); }
FN(LookupIconIdFromDirectoryEx) {
    ENTER;
    uint32_t p = A(c, m, 0);
    uint16_t count = m.Read16(p + 4);
    std::vector<uint8_t> buf(6 + count * 14u);
    m.ReadBytes(p, buf.data(), buf.size());
    int idx = PickFromDirectory(buf.data(), buf.size(), false, static_cast<int32_t>(A(c, m, 2)) ? static_cast<int32_t>(A(c, m, 2)) : 32,
                                static_cast<int32_t>(A(c, m, 3)) ? static_cast<int32_t>(A(c, m, 3)) : 32);
    return Ret(idx < 0 ? 0 : R16(buf.data() + 6 + idx * 14 + 12));
}
FN(LookupIconIdFromDirectory) {
    ENTER;
    uint32_t p = A(c, m, 0);
    uint16_t count = m.Read16(p + 4);
    std::vector<uint8_t> buf(6 + count * 14u);
    m.ReadBytes(p, buf.data(), buf.size());
    int idx = PickFromDirectory(buf.data(), buf.size(), false, 32, 32);
    return Ret(idx < 0 ? 0 : R16(buf.data() + 6 + idx * 14 + 12));
}

#define E(name, bytes) {"USER32.dll", #name, bytes, CallConv::Stdcall, &name}
#define EAW(name, bytes) E(name##A, bytes), E(name##W, bytes)
const HostFunctionEntry kEntries[] = {
    EAW(LoadIcon, 8), EAW(LoadCursor, 8), EAW(LoadCursorFromFile, 4), EAW(LoadBitmap, 8), EAW(LoadImage, 24),
    E(DestroyIcon, 4), {"USER32.dll", "DestroyCursor", 4, CallConv::Stdcall, &DestroyIcon}, E(DrawIcon, 16), E(DrawIconEx, 36),
    E(CreateIconIndirect, 4), E(GetIconInfo, 8), E(CopyIcon, 4), E(CopyImage, 20), E(CreateIconFromResourceEx, 28),
    E(CreateIconFromResource, 16), E(LookupIconIdFromDirectoryEx, 20), E(LookupIconIdFromDirectory, 8),
};
const bool kRegistered = RegisterHostFunctionsLayer(kEntries, sizeof(kEntries) / sizeof(kEntries[0]), kUiLayer);

} // namespace

uint32_t LoadImageForStatic(uint32_t hInst, uint16_t id, const std::u16string& name, bool bitmap) {
    rt::Module* mod = ModuleOf(hInst);
    rt::ResId rid = id ? rt::ResId::Num(id) : rt::ResId::Str(name);
    if (!id) for (char16_t& ch : rid.name) if (ch >= 'a' && ch <= 'z') ch = static_cast<char16_t>(ch - 32);
    if (bitmap) return BitmapFromResource(mod, rid, false);
    uint32_t h = IconFromResource(mod, rid, false, 32, 32);
    if (!h && id >= 32512 && id <= 32517) h = SystemIcon(id);
    return h;
}

void DrawImageHandle(uint32_t hdc, uint32_t handle, int x, int y, bool bitmap) {
    if (bitmap || ui::g_gdi.Bitmap(handle)) {
        ui::BitmapObj* b = ui::g_gdi.Bitmap(handle);
        if (!b) return;
        uint32_t mem = ui::g_gdi.CreateDc(ui::DcKind::Memory);
        uint32_t old = ui::g_gdi.SelectObject(mem, handle);
        ui::g_gdi.BitBlt(hdc, x, y, b->view.width, b->view.height, mem, 0, 0, 0xCC0020);
        ui::g_gdi.SelectObject(mem, old);
        ui::g_gdi.DeleteDc(mem);
        return;
    }
    DrawIconAt(hdc, I(handle), x, y, 0, 0, 3);
}

// Иконка для MessageBox и т.п.
uint32_t SystemIconHandle(uint32_t id) { return SystemIcon(id); }

} // namespace user
} // namespace win32
