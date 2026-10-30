// resources.cpp — поиск ресурсов в образе модуля (см. resources.h)

#include "resources.h"

namespace rt {

namespace {

bool Span(const std::vector<uint8_t>& img,uint64_t at,uint64_t count){return at<=img.size() && count<=img.size()-at;}
uint32_t R32(const std::vector<uint8_t>& img, uint32_t off) {
    if (static_cast<size_t>(off) + 4 > img.size()) return 0;
    return img[off] | (img[off + 1] << 8) | (img[off + 2] << 16) | (static_cast<uint32_t>(img[off + 3]) << 24);
}
uint16_t R16(const std::vector<uint8_t>& img, uint32_t off) {
    if (static_cast<size_t>(off) + 2 > img.size()) return 0;
    return static_cast<uint16_t>(img[off] | (img[off + 1] << 8));
}

bool NameEquals(const std::vector<uint8_t>& img, uint64_t strOff, const std::u16string& name) {
    if(!Span(img,strOff,2))return false;
    uint16_t len = R16(img, uint32_t(strOff));
    if(!Span(img,strOff+2,uint64_t(len)*2))return false;
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
    const uint64_t dir = uint64_t(root) + dirOff;
    if(!Span(img,dir,16))return 0xFFFFFFFFu;
    uint16_t named = R16(img, dir + 12), ids = R16(img, dir + 14);
    if(!Span(img,dir+16,uint64_t(uint32_t(named)+ids)*8))return 0xFFFFFFFFu;
    for (uint32_t i = 0; i < static_cast<uint32_t>(named) + ids; ++i) {
        uint32_t e = dir + 16 + i * 8;
        uint32_t nameField = R32(img, e), data = R32(img, e + 4);
        if (nameField & 0x80000000u) {
            if (!id.name.empty() && NameEquals(img, uint64_t(root) + (nameField & 0x7FFFFFFFu), id.name)) return data;
        } else if (id.name.empty() && (nameField & 0xFFFF) == id.id) {
            return data;
        }
    }
    return 0xFFFFFFFFu;
}

} // namespace

bool FindResource(const Module* m, const ResId& type, const ResId& name, ResourceData& out, uint16_t lang) {
    out={};
    if (!m || m->unloaded || !m->resourceRva) return false;
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
        const uint64_t dir=uint64_t(root)+langDir;
        if(!Span(img,dir,16) || !Span(img,dir+16,uint64_t(uint32_t(R16(img,dir+12))+R16(img,dir+14))*8))return false;
        if (R16(img, dir + 12) + R16(img, dir + 14) == 0) return false;
        l = R32(img, dir + 16 + 4);
    }
    if (l & 0x80000000u) return false;
    const uint64_t entry=uint64_t(root)+l;
    if(!Span(img,entry,16))return false;
    out.entryRva = uint32_t(entry);
    out.rva = R32(img, entry);
    out.size = R32(img, entry + 4);
    return static_cast<size_t>(out.rva) + out.size <= img.size();
}

const uint8_t* ResourcePtr(const Module* m, const ResourceData& d) {
    if (!m || m->unloaded || static_cast<size_t>(d.rva) + d.size > m->image.size()) return nullptr;
    return m->image.data() + d.rva;
}

bool FindStringResourceSpan(const Module* m,uint32_t id,ResourceData& data,uint32_t& offset,uint16_t& chars) {
    id&=0xffff;offset=0;chars=0;
    if(!FindResource(m,ResId::Num(RT_STRING),ResId::Num(uint16_t((id>>4)+1)),data))return false;
    const auto* p=ResourcePtr(m,data);if(!p)return false;
    uint64_t off=0;
    for(unsigned n=0;n<=unsigned(id&15);++n){
        if(off+2>data.size)return false;
        const auto length=uint16_t(p[off]|uint16_t(p[off+1])<<8);off+=2;
        if(off+uint64_t(length)*2>data.size)return false;
        if(n==(id&15)){offset=uint32_t(off);chars=length;return true;}off+=uint64_t(length)*2;
    }return false;
}
bool LoadStringResource(const Module* m,uint32_t id,std::u16string& out) {
    out.clear();ResourceData d;uint32_t off=0;uint16_t chars=0;
    if(!FindStringResourceSpan(m,id,d,off,chars) || !chars)return false;
    const auto* p=ResourcePtr(m,d)+off;out.reserve(chars);
    for(unsigned n=0;n<chars;++n)out.push_back(char16_t(p[n*2]|uint16_t(p[n*2+1])<<8));
    return true;
}

} // namespace rt
