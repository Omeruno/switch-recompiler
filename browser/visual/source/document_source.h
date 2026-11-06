#pragma once
#include "resources.h"
#include <litehtml/encodings.h>
namespace visual {
// HTTP encoding is authoritative over meta; litehtml still gives a BOM precedence.
inline litehtml::estring document_source(const Resource &resource) {
    std::string body(resource.data.begin(), resource.data.end());
    auto found = resource.response_headers.find("content-type");
    if (found == resource.response_headers.end())
        return litehtml::estring(body);
    const auto &header = found->second;
    size_t cursor = header.find(';');
    while (cursor != std::string::npos && cursor < header.size()) {
        ++cursor;
        auto end = header.find(';', cursor);
        auto field = header.substr(cursor, end == std::string::npos ? end : end - cursor);
        auto equal = field.find('=');
        if (equal != std::string::npos) {
            auto trim = [](std::string text) {
                auto a = text.find_first_not_of(" \t"), b = text.find_last_not_of(" \t");
                return a == std::string::npos ? std::string{} : text.substr(a, b - a + 1);
            };
            auto key = trim(field.substr(0, equal));
            for (char &c : key)
                if (c >= 'A' && c <= 'Z')
                    c += 32;
            if (key == "charset") {
                auto label = trim(field.substr(equal + 1));
                if (label.size() >= 2 && (label.front() == '\'' || label.front() == '"') &&
                    label.back() == label.front())
                    label = label.substr(1, label.size() - 2);
                auto encoding = litehtml::get_encoding(label);
                if (encoding != litehtml::encoding::null)
                    return litehtml::estring(body, encoding, litehtml::confidence::certain);
                break;
            }
        }
        cursor = end;
    }
    return litehtml::estring(body);
}
} // namespace visual
