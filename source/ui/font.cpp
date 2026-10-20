// font.cpp — шрифты GDI (см. font.h)

#include "font.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <tuple>

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../third_party/stb_truetype.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include "../win32/bitmap_font.h"

namespace ui {

struct FaceData {
    std::vector<uint8_t> data;
    stbtt_fontinfo info{};
    int unitsPerEm = 2048;
    int winAscent = 0, winDescent = 0, lineGap = 0;
    int avgWidth = 0, maxWidth = 0;
    bool fixedPitch = false;
    std::string family;
};

namespace {

std::string g_fontDir = "romfs:/fonts";
std::map<std::string, std::unique_ptr<FaceData>> g_faces; // по имени файла (nullptr — не загрузился)
std::map<FontDesc, std::unique_ptr<Font>> g_fonts;

int Be16(const uint8_t* p) { return static_cast<int16_t>((p[0] << 8) | p[1]); }
int BeU16(const uint8_t* p) { return (p[0] << 8) | p[1]; }

FaceData* LoadFace(const std::string& file, const std::string& family) {
    auto it = g_faces.find(file);
    if (it != g_faces.end()) return it->second.get();
    std::unique_ptr<FaceData> face(new FaceData);
    std::string path = g_fontDir + "/" + file;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        std::fseek(f, 0, SEEK_END);
        long size = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (size > 0) {
            face->data.resize(static_cast<size_t>(size));
            if (std::fread(face->data.data(), 1, face->data.size(), f) != face->data.size()) face->data.clear();
        }
        std::fclose(f);
    }
    if (face->data.empty() || !stbtt_InitFont(&face->info, face->data.data(), 0)) {
        g_faces[file] = nullptr;
        return nullptr;
    }
    const uint8_t* d = face->data.data();
    uint32_t head = stbtt__find_table(face->data.data(), 0, "head");
    uint32_t os2 = stbtt__find_table(face->data.data(), 0, "OS/2");
    uint32_t post = stbtt__find_table(face->data.data(), 0, "post");
    if (head) {
        face->unitsPerEm = BeU16(d + head + 18);
        face->maxWidth = Be16(d + head + 40) - Be16(d + head + 36);
    }
    int asc, desc, gap;
    stbtt_GetFontVMetrics(&face->info, &asc, &desc, &gap);
    face->winAscent = asc;
    face->winDescent = -desc;
    if (os2) {
        face->avgWidth = Be16(d + os2 + 2);
        face->winAscent = BeU16(d + os2 + 74);
        face->winDescent = BeU16(d + os2 + 76);
    }
    face->lineGap = std::max(0, asc - desc + gap - face->winAscent - face->winDescent);
    if (post) face->fixedPitch = (d[post + 12] | d[post + 13] | d[post + 14] | d[post + 15]) != 0;
    if (!face->avgWidth) face->avgWidth = face->unitsPerEm / 2;
    face->family = family;
    FaceData* raw = face.get();
    g_faces[file] = std::move(face);
    return raw;
}

std::string Lower(const std::string& s) {
    std::string r = s;
    for (char& c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

// Какой файл отвечает за шрифт Windows с таким именем
void ChooseFile(const FontDesc& d, std::string& file, std::string& family, bool& needBold) {
    std::string f = Lower(d.face);
    bool mono = f.find("courier") != std::string::npos || f.find("mono") != std::string::npos ||
                f.find("console") != std::string::npos || f.find("consolas") != std::string::npos ||
                f == "fixedsys" || f == "terminal" || (d.pitchAndFamily & 3) == 1 /* FIXED_PITCH */ ||
                ((d.pitchAndFamily & 0xF0) == 0x30 /* FF_MODERN */ && f.empty());
    bool serif = f.find("times") != std::string::npos || f.find("roman") != std::string::npos ||
                 (f.find("serif") != std::string::npos && f.find("sans") == std::string::npos) ||
                 f.find("georgia") != std::string::npos || f.find("garamond") != std::string::npos ||
                 ((d.pitchAndFamily & 0xF0) == 0x10 /* FF_ROMAN */ && f.empty());
    bool bold = d.weight >= 600;
    needBold = false;
    if (mono) { file = "LiberationMono-Regular.ttf"; family = "Courier New"; needBold = bold; }
    else if (serif) { file = "LiberationSerif-Regular.ttf"; family = "Times New Roman"; needBold = bold; }
    else if (bold) { file = "LiberationSans-Bold.ttf"; family = "Arial"; }
    else { file = "LiberationSans-Regular.ttf"; family = "Arial"; }
}

} // namespace

bool FontDesc::operator<(const FontDesc& o) const {
    return std::tie(height, width, weight, italic, underline, strikeout, charset, pitchAndFamily, face) <
           std::tie(o.height, o.width, o.weight, o.italic, o.underline, o.strikeout, o.charset, o.pitchAndFamily, o.face);
}

void FontCache::SetDirectory(const std::string& dir) {
    g_fontDir = dir;
    g_faces.clear();
    g_fonts.clear();
}

Font* FontCache::Get(const FontDesc& desc) {
    auto it = g_fonts.find(desc);
    if (it != g_fonts.end()) return it->second.get();
    std::unique_ptr<Font> font(new Font);
    font->desc_ = desc;
    std::string file, family;
    bool needBold = false;
    ChooseFile(desc, file, family, needBold);
    FaceData* face = LoadFace(file, family);
    if (!face && file == "LiberationSans-Bold.ttf") { face = LoadFace("LiberationSans-Regular.ttf", family); needBold = true; }
    TextMetrics& m = font->metrics_;
    m.weight = desc.weight ? desc.weight : 400;
    m.italic = desc.italic;
    m.underline = desc.underline;
    m.strikeout = desc.strikeout;
    m.charset = desc.charset;
    m.faceName = desc.face.empty() ? family : desc.face;

    if (face) {
        font->face_ = face;
        font->syntheticBold_ = needBold;
        int cell = face->winAscent + face->winDescent;
        float scale;
        if (desc.height < 0) scale = static_cast<float>(-desc.height) / face->unitsPerEm;
        else scale = static_cast<float>(desc.height ? desc.height : 16) / cell;
        font->scale_ = scale;
        m.ascent = static_cast<int>(std::lround(face->winAscent * scale));
        m.descent = static_cast<int>(std::lround(face->winDescent * scale));
        m.height = m.ascent + m.descent;
        m.internalLeading = std::max(0, m.height - static_cast<int>(std::lround(face->unitsPerEm * scale)));
        m.externalLeading = static_cast<int>(std::lround(face->lineGap * scale));
        m.aveCharWidth = std::max(1, static_cast<int>(std::lround(face->avgWidth * scale)));
        m.maxCharWidth = std::max(m.aveCharWidth, static_cast<int>(std::lround(face->maxWidth * scale)));
        m.fixedPitch = face->fixedPitch;
        if (needBold) { m.aveCharWidth++; m.maxCharWidth++; }
        m.pitchAndFamily = static_cast<uint8_t>((face->fixedPitch ? 0x30 : 0x20) | 0x06 /* TMPF_VECTOR|TMPF_TRUETYPE */);
    } else {
        // встроенный растровый шрифт 5x7 в ячейке 6x10, целый масштаб
        int cellH = desc.height < 0 ? -desc.height + 2 : (desc.height ? desc.height : 16);
        int k = std::max(1, (cellH + 5) / 10);
        font->bitmapScale_ = k;
        font->syntheticBold_ = desc.weight >= 600;
        m.ascent = 8 * k;
        m.descent = 2 * k;
        m.height = 10 * k;
        m.internalLeading = 2 * k;
        m.aveCharWidth = m.maxCharWidth = 6 * k + (font->syntheticBold_ ? 1 : 0);
        m.fixedPitch = true;
        m.pitchAndFamily = 0x30;
    }
    Font* raw = font.get();
    g_fonts[desc] = std::move(font);
    return raw;
}

const Font::Glyph& Font::GetGlyph(uint32_t ch) {
    auto it = glyphs_.find(ch);
    if (it != glyphs_.end()) return it->second;
    Glyph g;
    if (face_) {
        float sx = scale_;
        if (desc_.width > 0 && face_->avgWidth) sx = static_cast<float>(desc_.width) / face_->avgWidth;
        int adv, lsb;
        stbtt_GetCodepointHMetrics(&face_->info, static_cast<int>(ch), &adv, &lsb);
        g.advance = static_cast<int>(std::lround(adv * sx)) + (syntheticBold_ ? 1 : 0);
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox(&face_->info, static_cast<int>(ch), sx, scale_, &x0, &y0, &x1, &y1);
        int w = x1 - x0, h = y1 - y0;
        if (w > 0 && h > 0) {
            std::vector<uint8_t> alpha(static_cast<size_t>(w) * h);
            stbtt_MakeCodepointBitmap(&face_->info, alpha.data(), w, h, w, sx, scale_, static_cast<int>(ch));
            int shear = desc_.italic ? (h + 3) / 4 : 0;
            int bold = syntheticBold_ ? 1 : 0;
            g.width = w + shear + bold;
            g.height = h;
            g.xoff = x0;
            g.yoff = metrics_.ascent + y0;
            g.mask.assign(static_cast<size_t>(g.width) * h, 0);
            for (int y = 0; y < h; ++y) {
                int shift = desc_.italic ? (h - 1 - y) / 4 : 0;
                for (int x = 0; x < w; ++x) {
                    if (alpha[static_cast<size_t>(y) * w + x] < 80) continue;
                    g.mask[static_cast<size_t>(y) * g.width + x + shift] = 1;
                    if (bold) g.mask[static_cast<size_t>(y) * g.width + x + shift + 1] = 1;
                }
            }
        }
    } else {
        int k = bitmapScale_;
        g.advance = 6 * k + (syntheticBold_ ? 1 : 0);
        const win32::FontGlyph* fg = win32::FindGlyph(ch);
        if (fg) {
            g.width = 5 * k + (syntheticBold_ ? 1 : 0);
            g.height = win32::kFontRows * k;
            g.yoff = k;
            g.mask.assign(static_cast<size_t>(g.width) * g.height, 0);
            for (int row = 0; row < win32::kFontRows; ++row)
                for (int col = 0; col < 5; ++col) {
                    if (fg->rows[row][col] != '#') continue;
                    for (int sy = 0; sy < k; ++sy)
                        for (int sx = 0; sx <= k - 1 + (syntheticBold_ ? 1 : 0); ++sx)
                            g.mask[static_cast<size_t>(row * k + sy) * g.width + col * k + sx] = 1;
                }
        }
    }
    return glyphs_.emplace(ch, std::move(g)).first->second;
}

int Font::Advance(uint32_t ch) {
    if (ch == '\t') return GetGlyph(' ').advance * 8;
    return GetGlyph(ch).advance;
}

int Font::MeasureWidth(const std::u16string& text) {
    int w = 0;
    for (char16_t c : text) w += Advance(c);
    return w;
}

int Font::Draw(Surface& s, const Region& clip, int x, int y, const std::u16string& text, uint32_t rgb,
               int extraSpacing, const std::vector<int>* dx) {
    int pen = x;
    Rect bounds = clip.Bounds().Intersect(Rect(0, 0, s.width, s.height));
    bool simpleClip = clip.Rects().size() == 1;
    for (size_t i = 0; i < text.size(); ++i) {
        uint32_t ch = text[i];
        const Glyph& g = GetGlyph(ch);
        for (int gy = 0; gy < g.height; ++gy) {
            int py = y + g.yoff + gy;
            if (py < bounds.top || py >= bounds.bottom) continue;
            const uint8_t* m = g.mask.data() + static_cast<size_t>(gy) * g.width;
            for (int gx = 0; gx < g.width; ++gx) {
                if (!m[gx]) continue;
                int px = pen + g.xoff + gx;
                if (px < bounds.left || px >= bounds.right) continue;
                if (!simpleClip && !clip.Contains(px, py)) continue;
                s.Set(px, py, rgb);
            }
        }
        int step = (dx && i < dx->size()) ? (*dx)[i] : Advance(ch) + extraSpacing;
        pen += step;
    }
    int width = pen - x;
    // подчёркивание и зачёркивание — линиями толщиной ~1/14 высоты
    auto hline = [&](int ly) {
        int t = std::max(1, metrics_.height / 14);
        for (int yy = ly; yy < ly + t; ++yy) {
            if (yy < bounds.top || yy >= bounds.bottom) continue;
            for (int xx = std::max(x, bounds.left); xx < std::min(pen, bounds.right); ++xx)
                if (simpleClip || clip.Contains(xx, yy)) s.Set(xx, yy, rgb);
        }
    };
    if (desc_.underline) hline(y + metrics_.ascent + std::max(1, metrics_.descent / 3));
    if (desc_.strikeout) hline(y + metrics_.ascent - metrics_.ascent / 3);
    return width;
}

} // namespace ui
