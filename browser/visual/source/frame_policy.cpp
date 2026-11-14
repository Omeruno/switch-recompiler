#include "frame_policy.h"
extern "C" {
#include "profile.h"
}
#include <algorithm>
#include <sstream>
namespace visual {
namespace {
std::string lower(std::string s) {
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z')
            c += 32;
    return s;
}
std::string origin(const std::string &s) {
    char out[384]{};
    if (!browser_origin(s.c_str(), out, sizeof(out)))
        return {};
    std::string r = out;
    if (r.size() > 4 && r.substr(r.size() - 4) == ":443")
        r.resize(r.size() - 4);
    return r;
}
} // namespace
bool frame_source_allowed(const std::vector<std::string> &policies, const std::string &source,
                          const std::string &target) {
    if (Resources::resolve("", target).rfind("https://", 0) != 0)
        return false;
    for (const auto &policy : policies) {
        std::map<std::string, std::string> entries;
        std::istringstream parts(policy);
        std::string part;
        while (std::getline(parts, part, ';')) {
            std::istringstream fields(part);
            std::string key;
            fields >> key;
            key = lower(key);
            if (!entries.count(key))
                entries.emplace(key, std::string((std::istreambuf_iterator<char>(fields)), {}));
        }
        if (entries.count("sandbox"))
            return false;
        auto it = entries.find("frame-src");
        if (it == entries.end())
            it = entries.find("child-src");
        if (it == entries.end())
            it = entries.find("default-src");
        if (it != entries.end() &&
            !form_action_allowed({"form-action " + it->second}, source, target))
            return false;
    }
    return true;
}
bool frame_response_allowed(const Resource &r, const std::vector<std::string> &ancestors) {
    auto mime = r.response_headers.find("content-type");
    if (mime == r.response_headers.end())
        return false;
    auto type = lower(mime->second.substr(0, mime->second.find(';')));
    while (!type.empty() && type.back() == ' ')
        type.pop_back();
    if (type != "text/html")
        return false;
    bool has_ancestors = false;
    for (const auto &policy : r.policies) {
        std::istringstream parts(policy);
        std::string part;
        bool seen = false;
        while (std::getline(parts, part, ';')) {
            std::istringstream fields(part);
            std::string name;
            fields >> name;
            if (lower(name) == "sandbox")
                return false;
            if (lower(name) != "frame-ancestors")
                continue;
            if (seen)
                continue;
            seen = true;
            has_ancestors = true;
            std::string rules((std::istreambuf_iterator<char>(fields)), {});
            // Reuse the bounded HTTPS source-list matcher. Unsupported patterns deny.
            for (const auto &ancestor : ancestors)
                if (!form_action_allowed({"form-action " + rules}, r.effective, ancestor))
                    return false;
        }
    }
    if (has_ancestors)
        return true; // Enforced frame-ancestors supersedes XFO.
    auto xfo = r.response_headers.find("x-frame-options");
    if (xfo == r.response_headers.end())
        return true;
    auto value = lower(xfo->second);
    auto first = value.find_first_not_of(" \t");
    auto last = value.find_last_not_of(" \t");
    value = first == std::string::npos ? "" : value.substr(first, last - first + 1);
    if (value == "deny")
        return false;
    if (value != "sameorigin")
        return false; // Conflicting/unknown XFO fails closed.
    const auto own = origin(r.effective);
    return !own.empty() && std::all_of(ancestors.begin(), ancestors.end(),
                                       [&](const auto &s) { return origin(s) == own; });
}
} // namespace visual
