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
static void wipe_payload(Resource &r) {
    std::fill(r.post_body.begin(), r.post_body.end(), '\0');
    r.post_body.clear();
}
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
    for (auto &kv : items)
        wipe_payload(kv.second);
    items.clear();
    active = 0;
    request_limit = false;
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
    if (items.size() >= 64)
        request_limit = true;
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
            name != "state.json" && name != "dynamic-demo.js") {
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
        completed(r);
    } else if (resolve("", key).empty()) {
        r.state = Resource::failed;
        r.error = "Only HTTPS URLs are supported";
    }
    return &items.emplace(key, std::move(r)).first->second;
}
Resource *Resources::navigate_form(const Submission &form, const std::string &source,
                                   const std::vector<std::string> &policies, size_t limit) {
    if (items.size() >= 64)
        request_limit = true;
    if (!multi || items.size() >= 64 || form.body.size() > 65536 ||
        (form.method != "GET" && form.method != "POST") ||
        !form_action_allowed(policies, source, form.url))
        return nullptr;
    Resource r;
    r.url = r.current_url = r.effective = form.url;
    r.method = form.method;
    r.post_body = form.body;
    r.limit = limit;
    r.critical = true;
    r.form_navigation = true;
    r.form_source = source;
    r.form_rules = policies;
    return &items.emplace("form:" + std::to_string(++next_navigation), std::move(r)).first->second;
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
        r.byte_limit = true;
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
        r.location.clear();
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
        if (key == "location") {
            auto value = line.substr(colon + 1);
            auto begin = value.find_first_not_of(" \t\r\n"),
                 end = value.find_last_not_of(" \t\r\n");
            r.location = begin == std::string::npos ? "" : value.substr(begin, end - begin + 1);
        }
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
        r.result = CURLE_FAILED_INIT;
        wipe_payload(r);
        completed(r);
        return;
    }
    auto *ctx = new std::pair<Resources *, Resource *>(this, &r);
    curl_easy_setopt(r.handle, CURLOPT_PRIVATE, ctx);
    curl_easy_setopt(r.handle, CURLOPT_URL,
                     (r.current_url.empty() ? r.url : r.current_url).c_str());
    if (r.method == "POST") {
        curl_easy_setopt(r.handle, CURLOPT_POST, 1L);
        curl_easy_setopt(r.handle, CURLOPT_POSTFIELDS, r.post_body.c_str());
        curl_easy_setopt(r.handle, CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(r.post_body.size()));
    }
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(r.handle, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(r.handle, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(r.handle, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(r.handle, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(r.handle, CURLOPT_DISALLOW_USERNAME_IN_URL, 1L);
    curl_easy_setopt(r.handle, CURLOPT_FOLLOWLOCATION, r.form_navigation ? 0L : 1L);
    curl_easy_setopt(r.handle, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(r.handle, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(r.handle, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(r.handle, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(r.handle, CURLOPT_HEADERFUNCTION, header);
    curl_easy_setopt(r.handle, CURLOPT_HEADERDATA, ctx);
    curl_easy_setopt(r.handle, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(r.handle, CURLOPT_WRITEDATA, ctx);
    curl_easy_setopt(r.handle, CURLOPT_USERAGENT, "SwitchVisualBrowser/0.7.1");
    CURLcode code = browser_profile_configure(r.handle);
    if (code != CURLE_OK || curl_multi_add_handle(multi, r.handle) != CURLM_OK) {
        r.error = browser_profile_status();
        r.result = code != CURLE_OK ? code : CURLE_FAILED_INIT;
        r.state = Resource::failed;
        curl_easy_cleanup(r.handle);
        r.handle = nullptr;
        wipe_payload(r);
        delete ctx;
        completed(r);
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
            if (r.form_navigation && msg->data.result == CURLE_OK &&
                (http == 301 || http == 302 || http == 303 || http == 307 || http == 308) &&
                !r.location.empty()) {
                auto next = resolve(r.effective, r.location);
                bool follow =
                    r.redirects < 5 && form_action_allowed(r.form_rules, r.form_source, next);
                browser_profile_log(r.url.c_str(), "form-redirect", http, msg->data.result);
                curl_multi_remove_handle(multi, r.handle);
                curl_easy_cleanup(r.handle);
                r.handle = nullptr;
                delete ctx;
                --active;
                received -= r.data.size();
                r.data.clear();
                if (!follow) {
                    r.state = Resource::failed;
                    r.transfer_ok = false;
                    r.error = "Переход после формы заблокирован или превышен лимит";
                    r.policy_blocked = true;
                    wipe_payload(r);
                    completed(r);
                    continue;
                }
                ++r.redirects;
                r.current_url = r.effective = next;
                r.state = Resource::queued;
                r.http_status = 0;
                r.transfer_ok = false;
                r.error.clear();
                if (http == 303 || ((http == 301 || http == 302) && r.method == "POST")) {
                    r.method = "GET";
                    wipe_payload(r);
                }
                ++revision;
                continue;
            }
            r.http_status = http;
            r.result = msg->data.result;
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
            wipe_payload(r);
            delete ctx;
            --active;
            completed(r);
        }
    return old != revision;
}
void Resources::completed(Resource &r) {
    ++revision;
    if (r.stylesheet)
        ++style_revision;
    if (r.image)
        ++image_revision;
}
bool Resources::pending_load() const {
    for (const auto &kv : items) {
        const auto &r = kv.second;
        if ((r.stylesheet || r.image) &&
            (r.state == Resource::queued || r.state == Resource::loading))
            return true;
    }
    return false;
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
Resources::Failures Resources::failures() const {
    Failures out;
    if (request_limit)
        ++out.limits;
    for (const auto &kv : items) {
        auto &r = kv.second;
        if (r.state != Resource::failed)
            continue;
        if (r.byte_limit)
            ++out.limits;
        else if (r.policy_blocked || r.result == CURLE_UNSUPPORTED_PROTOCOL)
            ++out.policy;
        else if (r.result == CURLE_PEER_FAILED_VERIFICATION ||
                 r.result == CURLE_SSL_CACERT_BADFILE || r.result == CURLE_SSL_CONNECT_ERROR ||
                 r.result == CURLE_SSL_CERTPROBLEM)
            ++out.tls;
        else if (r.result != CURLE_OK)
            ++out.transport;
        else if (r.http_status >= 300)
            ++out.http;
        else
            ++out.policy;
    }
    return out;
}
std::string Resources::status() const {
    unsigned errors = 0;
    for (const auto &kv : items)
        if (kv.second.state == Resource::failed)
            ++errors;
    return std::to_string(items.size()) + " ресурсов · сбоев загрузки: " + std::to_string(errors) +
           (pending() ? " · загрузка" : "");
}
} // namespace visual
