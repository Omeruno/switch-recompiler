// resources.cpp — поиск ресурсов в образе модуля (см. resources.h)

#include "resources.h"

namespace rt {

namespace {

uint32_t R32(const std::vector<uint8_t>& img, uint32_t off) {
    if (static_cast<size_t>(off) + 4 > img.size()) return 0;
    return img[off] | (img[off + 1] << 8) | (img[off + 2] << 16) | (static_cast<uint32_t>(img[off + 3]) << 24);
}
uint16_t R16(const std::vector<uint8_t>& img, uint32_t off) {
    if (static_cast<size_t>(off) + 2 > img.size()) return 0;
    return static_cast<uint16_t>(img[off] | (img[off + 1] << 8));
}

bool NameEquals(const std::vector<uint8_t>& img, uint32_t strOff, const std::u16string& name) {
    uint16_t len = R16(img, strOff);
    if (len != name.size()) return false;
    for (uint16_t i = 0; i < len; ++i) {
        char16_t a = static_cast<char16_t>(R16(img, strOff + 2 + i * 2)), b = name[i];
        if (a >= 'a' && a <= 'z') a = static_cast<char16_t>(a - 32);
        if (b >= 'a' && b <= 'z') b = static_cast<char16_t>(b - 32);
        if (a != b) return false;
    }
    return true;
}

// Ищет запись в каталоге dirOff (смещение от начала .rsrc = root).
// Возвращает OffsetToData записи или 0xFFFFFFFF.
uint32_t FindEntry(const std::vector<uint8_t>& img, uint32_t root, uint32_t dirOff, const ResId& id) {
    uint32_t dir = root + dirOff;
    uint16_t named = R16(img, dir + 12), ids = R16(img, dir + 14);
    for (uint32_t i = 0; i < static_cast<uint32_t>(named) + ids; ++i) {
        uint32_t e = dir + 16 + i * 8;
        uint32_t nameField = R32(img, e), data = R32(img, e + 4);
        if (nameField & 0x80000000u) {
            if (!id.name.empty() && NameEquals(img, root + (nameField & 0x7FFFFFFFu), id.name)) return data;
        } else if (id.name.empty() && (nameField & 0xFFFF) == id.id) {
            return data;
        }
    }
    return 0xFFFFFFFFu;
}

} // namespace

bool FindResource(const Module* m, const ResId& type, const ResId& name, ResourceData& out, uint16_t lang) {
    if (!m || !m->resourceRva) return false;
    const std::vector<uint8_t>& img = m->image;
    uint32_t root = m->resourceRva;
    uint32_t t = FindEntry(img, root, 0, type);
    if (t == 0xFFFFFFFFu || !(t & 0x80000000u)) return false;
    uint32_t n = FindEntry(img, root, t & 0x7FFFFFFFu, name);
    if (n == 0xFFFFFFFFu || !(n & 0x80000000u)) return false;
    uint32_t langDir = n & 0x7FFFFFFFu;
    uint32_t l = 0xFFFFFFFFu;
    if (lang) l = FindEntry(img, root, langDir, ResId::Num(lang));
    if (l == 0xFFFFFFFFu) l = FindEntry(img, root, langDir, ResId::Num(0));
    if (l == 0xFFFFFFFFu) {
        uint32_t dir = root + langDir;
        if (R16(img, dir + 12) + R16(img, dir + 14) == 0) return false;
        l = R32(img, dir + 16 + 4);
    }
    if (l & 0x80000000u) return false;
    uint32_t entry = root + l;
    out.entryRva = entry;
    out.rva = R32(img, entry);
    out.size = R32(img, entry + 4);
    return static_cast<size_t>(out.rva) + out.size <= img.size();
}

const uint8_t* ResourcePtr(const Module* m, const ResourceData& d) {
    if (!m || static_cast<size_t>(d.rva) + d.size > m->image.size()) return nullptr;
    return m->image.data() + d.rva;
}

bool LoadStringResource(const Module* m, uint32_t id, std::u16string& out) {
    ResourceData d;
    if (!FindResource(m, ResId::Num(RT_STRING), ResId::Num(static_cast<uint16_t>((id >> 4) + 1)), d)) return false;
    const uint8_t* p = ResourcePtr(m, d);
    if (!p) return false;
    uint32_t off = 0;
    for (uint32_t i = 0; i < 16; ++i) {
        if (off + 2 > d.size) return false;
        uint16_t len = static_cast<uint16_t>(p[off] | (p[off + 1] << 8));
        off += 2;
        if (i == (id & 15)) {
            if (off + len * 2u > d.size || !len) return false;
            out.clear();
            for (uint16_t k = 0; k < len; ++k) out.push_back(static_cast<char16_t>(p[off + k * 2] | (p[off + k * 2 + 1] << 8)));
            return true;
        }
        off += len * 2u;
    }
    return false;
}

} // namespace rt
