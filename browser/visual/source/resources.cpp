#include "resources.h"
extern "C" {
#include "profile.h"
}
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <utility>
namespace visual {
Resources::Resources(std::string root) : local_root(std::move(root)) { multi = curl_multi_init(); }
Resources::~Resources() {
    clear();
    if (multi)
        curl_multi_cleanup(multi);
}
void Resources::clear() {
    for (auto &kv : items)
        if (kv.second.handle) {
            std::pair<Resources *, Resource *> *ctx = nullptr;
            curl_easy_getinfo(kv.second.handle, CURLINFO_PRIVATE, &ctx);
            curl_multi_remove_handle(multi, kv.second.handle);
            curl_easy_cleanup(kv.second.handle);
            delete ctx;
        }
    items.clear();
    active = 0;
    received = 0;
    ++revision;
}
std::string Resources::resolve(const std::string &base, const std::string &ref) {
    if (ref.empty())
        return base;
    if (ref.rfind("demo:/", 0) == 0)
        return ref;
    if (base.rfind("demo:/", 0) == 0 && ref.find(':') == std::string::npos) {
        if (ref.front() == '#')
            return base.substr(0, base.find('#')) + ref;
        return "demo:/" + ref;
    }
    CURLU *u = curl_url();
    char *text = nullptr, *scheme = nullptr, *user = nullptr, *pass = nullptr;
    bool ok = u &&
              (base.empty() || ref.rfind("https://", 0) == 0 ||
               curl_url_set(u, CURLUPART_URL, base.c_str(), 0) == CURLUE_OK) &&
              curl_url_set(u, CURLUPART_URL, ref.c_str(), 0) == CURLUE_OK &&
              curl_url_get(u, CURLUPART_SCHEME, &scheme, 0) == CURLUE_OK &&
              std::string(scheme) == "https" &&
              curl_url_get(u, CURLUPART_USER, &user, 0) != CURLUE_OK &&
              curl_url_get(u, CURLUPART_PASSWORD, &pass, 0) != CURLUE_OK &&
              curl_url_get(u, CURLUPART_URL, &text, 0) == CURLUE_OK;
    std::string out = ok ? text : "";
    curl_free(text);
    curl_free(scheme);
    curl_free(user);
    curl_free(pass);
    curl_url_cleanup(u);
    return out;
}
Resource *Resources::request(const std::string &url, size_t limit) {
    std::string key = url.substr(0, url.find('#'));
    auto found = items.find(key);
    if (found != items.end())
        return &found->second;
    if (key.empty() || items.size() >= 64 || (!multi && key.rfind("demo:/", 0) != 0))
        return nullptr;
    Resource r;
    r.url = key;
    r.effective = key;
    r.limit = limit;
    if (key.rfind("demo:/", 0) == 0) {
        std::string name = key.substr(6);
        // Local assets are an explicit allowlist, never a URL-to-path mapping.
        if (name != "index.html" && name != "style.css" && name != "landscape.png" &&
            name != "landscape.jpg" && name != "landscape.webp" && name != "interactions.js" &&
            name != "state.json") {
            r.state = Resource::failed;
            r.error = "Unknown demo asset";
        } else {
            std::ifstream f(local_root + "/" + name, std::ios::binary);
            if (f) {
                r.data.assign(std::istreambuf_iterator<char>(f), {});
            }
            if (!f || r.data.empty() || r.data.size() > limit ||
                r.data.size() > total_limit - received) {
                r.data.clear();
                r.state = Resource::failed;
                r.error = "Demo asset unavailable or over quota";
            } else {
                received += r.data.size();
                r.state = Resource::ready;
                r.http_status = 200;
                r.transfer_ok = true;
            }
        }
        ++revision;
    } else if (resolve("", key).empty()) {
        r.state = Resource::failed;
        r.error = "Only HTTPS URLs are supported";
    }
    return &items.emplace(key, std::move(r)).first->second;
}
size_t Resources::receive(char *data, size_t unit, size_t count, void *ctx) {
    auto *pair = static_cast<std::pair<Resources *, Resource *> *>(ctx);
    auto &r = *pair->second;
    auto &self = *pair->first;
    if (unit && count > std::numeric_limits<size_t>::max() / unit)
        return 0;
    size_t bytes = unit * count;
    if (bytes > r.limit - r.data.size() || bytes > total_limit - self.received) {
        r.error = "Resource byte quota exceeded";
        return 0;
    }
    r.data.insert(r.data.end(), data, data + bytes);
    self.received += bytes;
    return bytes;
}
size_t Resources::header(char *data, size_t unit, size_t count, void *opaque) {
    auto *ctx = static_cast<std::pair<Resources *, Resource *> *>(opaque);
    auto &r = *ctx->second;
    if (unit && count > std::numeric_limits<size_t>::max() / unit)
        return 0;
    size_t bytes = unit * count;
    std::string line(data, bytes);
    if (line.rfind("HTTP/", 0) == 0) {
        r.policies.clear();
        r.header_bytes = 0;
    }
    if (bytes > 16384 - r.header_bytes)
        return 0;
    r.header_bytes += bytes;
    auto colon = line.find(':');
    if (colon != std::string::npos) {
        auto key = line.substr(0, colon);
        for (char &ch : key)
            ch = char(std::tolower(static_cast<unsigned char>(ch)));
        if (key == "content-security-policy") {
            if (r.policies.size() >= 8)
                return 0;
            auto value = line.substr(colon + 1);
            while (!value.empty() && (value.back() == '\r' || value.back() == '\n'))
                value.pop_back();
            r.policies.push_back(value);
        }
    }
    return bytes;
}
void Resources::start(Resource &r) {
    r.handle = curl_easy_init();
    if (!r.handle) {
        r.state = Resource::failed;
        r.error = "curl initialization failed";
        ++revision;
        return;
    }
    auto *ctx = new std::pair<Resources *, Resource *>(this, &r);
    curl_easy_setopt(r.handle, CURLOPT_PRIVATE, ctx);
    curl_easy_setopt(r.handle, CURLOPT_URL, r.url.c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(r.handle, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(r.handle, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(r.handle, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(r.handle, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(r.handle, CURLOPT_DISALLOW_USERNAME_IN_URL, 1L);
    curl_easy_setopt(r.handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(r.handle, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(r.handle, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(r.handle, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(r.handle, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(r.handle, CURLOPT_HEADERFUNCTION, header);
    curl_easy_setopt(r.handle, CURLOPT_HEADERDATA, ctx);
    curl_easy_setopt(r.handle, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(r.handle, CURLOPT_WRITEDATA, ctx);
    curl_easy_setopt(r.handle, CURLOPT_USERAGENT, "SwitchVisualBrowser/0.3.0");
    CURLcode code = browser_profile_configure(r.handle);
    if (code != CURLE_OK || curl_multi_add_handle(multi, r.handle) != CURLM_OK) {
        r.error = browser_profile_status();
        r.state = Resource::failed;
        curl_easy_cleanup(r.handle);
        r.handle = nullptr;
        delete ctx;
        ++revision;
        return;
    }
    r.state = Resource::loading;
    ++active;
}
bool Resources::pump() {
    unsigned old = revision;
    if (!multi)
        return false;
    for (auto &kv : items)
        if (active < 4 && kv.second.state == Resource::queued && kv.second.critical)
            start(kv.second);
    // Keep a slot available for scripts/styles/user fetch while images are in flight.
    for (auto &kv : items)
        if (active < 3 && kv.second.state == Resource::queued && !kv.second.critical)
            start(kv.second);
    int running = 0;
    curl_multi_perform(multi, &running);
    int left = 0;
    while (CURLMsg *msg = curl_multi_info_read(multi, &left))
        if (msg->msg == CURLMSG_DONE) {
            std::pair<Resources *, Resource *> *ctx = nullptr;
            curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &ctx);
            Resource &r = *ctx->second;
            long http = 0;
            char *effective = nullptr;
            curl_easy_getinfo(r.handle, CURLINFO_RESPONSE_CODE, &http);
            curl_easy_getinfo(r.handle, CURLINFO_EFFECTIVE_URL, &effective);
            if (effective)
                r.effective = effective;
            r.http_status = http;
            r.transfer_ok = msg->data.result == CURLE_OK && http > 0;
            r.state = msg->data.result == CURLE_OK && http >= 200 && http < 300 ? Resource::ready
                                                                                : Resource::failed;
            if (r.state == Resource::failed && r.error.empty())
                r.error = msg->data.result == CURLE_OK ? "HTTP " + std::to_string(http)
                                                       : curl_easy_strerror(msg->data.result);
            if (!r.transfer_ok) {
                received -= r.data.size();
                r.data.clear();
            }
            browser_profile_log(r.url.c_str(), "visual", http, msg->data.result);
            curl_multi_remove_handle(multi, r.handle);
            curl_easy_cleanup(r.handle);
            r.handle = nullptr;
            delete ctx;
            --active;
            ++revision;
        }
    return old != revision;
}
bool Resources::pending_styles() const {
    for (const auto &kv : items)
        if (kv.second.stylesheet &&
            (kv.second.state == Resource::queued || kv.second.state == Resource::loading))
            return true;
    return false;
}
bool Resources::pending() const {
    for (const auto &kv : items)
        if (kv.second.state == Resource::queued || kv.second.state == Resource::loading)
            return true;
    return false;
}
std::string Resources::status() const {
    unsigned errors = 0;
    for (const auto &kv : items)
        if (kv.second.state == Resource::failed)
            ++errors;
    return std::to_string(items.size()) + " ресурсов · ошибок: " + std::to_string(errors) +
           (pending() ? " · загрузка" : "");
}
} // namespace visual
