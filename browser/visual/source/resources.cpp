#include "resources.h"
extern "C" {
#include "profile.h"
}
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>
#include <time.h>
#include <utility>
namespace visual {
static uint64_t network_clock() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return uint64_t(t.tv_sec) * 1000 + t.tv_nsec / 1000000;
}
static std::string net_origin(const std::string &url) {
    char out[384] = {};
    browser_origin(url.c_str(), out, sizeof(out));
    return out;
}
static std::string net_lower(std::string value) {
    for (char &ch : value)
        ch = char(std::tolower(static_cast<unsigned char>(ch)));
    return value;
}
static std::set<std::string> header_tokens(const std::string &value, bool lowercase = true) {
    std::set<std::string> out;
    std::istringstream stream(value);
    std::string token;
    while (std::getline(stream, token, ',')) {
        auto a = token.find_first_not_of(" \t"), b = token.find_last_not_of(" \t");
        if (a != std::string::npos)
            out.insert(lowercase ? net_lower(token.substr(a, b - a + 1))
                                 : token.substr(a, b - a + 1));
    }
    return out;
}
static bool cors_safe(const std::string &name, const std::string &value) {
    if (value.size() > 128)
        return false;
    if (name == "accept-language" || name == "content-language")
        return std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return std::isalnum(c) || std::string(" *,-.;=").find(char(c)) != std::string::npos;
        });
    if (name != "accept" && name != "content-type")
        return false;
    for (unsigned char c : value)
        if ((c < 32 && c != 9) || c == 127 ||
            std::string("\"():<>?@[\\]{}").find(char(c)) != std::string::npos)
            return false;
    if (name == "accept")
        return true;
    auto mime = net_lower(value.substr(0, value.find(';')));
    while (!mime.empty() && mime.back() == ' ')
        mime.pop_back();
    return mime == "text/plain" || mime == "application/x-www-form-urlencoded" ||
           mime == "multipart/form-data";
}
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
    for (auto &kv : items) {
        stop(kv.second);
        wipe_payload(kv.second);
    }
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
            name != "state.json" && name != "dynamic-demo.js" && name != "modules-demo.js" &&
            name != "module-state.js" && name != "module-step.js") {
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
                r.body_bytes = r.data.size();
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
Resource *Resources::navigate_document(const std::string &url, unsigned timeout_ms) {
    auto *resource = request(url, 1024u * 1024u);
    if (resource) {
        resource->document_navigation = true;
        resource->critical = true;
        resource->navigation_timeout_ms = std::clamp(timeout_ms, 100u, 60000u);
    }
    return resource;
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
bool Resources::valid_method(const std::string &s) {
    return s == "GET" || s == "HEAD" || s == "POST" || s == "PUT" || s == "PATCH" ||
           s == "DELETE" || s == "OPTIONS";
}
bool Resources::valid_header(const std::string &key, const std::string &value) {
    if (key.empty() || key.size() > 128 || value.size() > 8192)
        return false;
    for (unsigned char c : key)
        if (!std::isalnum(c) && std::string("!#$%&'*+-.^_`|~").find(char(c)) == std::string::npos)
            return false;
    return value.find('\r') == std::string::npos && value.find('\n') == std::string::npos &&
           value.find('\0') == std::string::npos;
}
bool Resources::forbidden_header(const std::string &key) {
    static const std::set<std::string> forbidden = {"accept-charset",
                                                    "accept-encoding",
                                                    "access-control-request-headers",
                                                    "access-control-request-method",
                                                    "connection",
                                                    "content-length",
                                                    "cookie",
                                                    "cookie2",
                                                    "date",
                                                    "dnt",
                                                    "expect",
                                                    "host",
                                                    "keep-alive",
                                                    "origin",
                                                    "permissions-policy",
                                                    "referer",
                                                    "te",
                                                    "trailer",
                                                    "transfer-encoding",
                                                    "upgrade",
                                                    "user-agent",
                                                    "via",
                                                    "x-http-method",
                                                    "x-http-method-override",
                                                    "x-method-override"};
    return forbidden.count(key) || key.rfind("proxy-", 0) == 0 || key.rfind("sec-", 0) == 0;
}
Resource *Resources::network(const std::string &url, const std::string &method, std::string body,
                             WebRequest options) {
    if (items.size() >= 64)
        request_limit = true;
    if (items.size() >= 64 || !valid_method(method) || body.size() > 65536 ||
        options.headers.size() > 32 || (options.mode != "cors" && options.mode != "same-origin") ||
        (options.credentials != "omit" && options.credentials != "same-origin" &&
         options.credentials != "include") ||
        (options.redirect != "follow" && options.redirect != "error"))
        return nullptr;
    size_t header_size = 0;
    for (auto &h : options.headers) {
        h.first = net_lower(h.first);
        header_size += h.first.size() + h.second.size();
        if (!valid_header(h.first, h.second) || forbidden_header(h.first))
            return nullptr;
    }
    if (header_size > 8192 || ((method == "GET" || method == "HEAD") && !body.empty()))
        return nullptr;
    if (url.rfind("demo:/", 0) == 0) {
        if (method != "GET" || !body.empty() || !options.headers.empty())
            return nullptr;
        // Offline demo assets cannot accept writes or map arbitrary filesystem paths.
        return request(url, 512u * 1024u);
    }
    if (resolve("", url).empty() || net_origin(options.source).empty())
        return nullptr;
    Resource r;
    r.url = r.current_url = r.effective = url.substr(0, url.find('#'));
    r.method = method;
    r.post_body = std::move(body);
    r.web_request = true;
    r.critical = true;
    r.limit = 512u * 1024u;
    r.web = std::move(options);
    r.cors_origin = net_origin(r.web.source);
    r.started_ms = network_clock();
    timeout(r, r.web.timeout_ms);
    if (!prepare_web(r)) {
        wipe_payload(r);
        return nullptr;
    }
    return &items.emplace("web:" + std::to_string(++next_navigation), std::move(r)).first->second;
}
void Resources::timeout(Resource &r, unsigned ms) {
    r.web.timeout_ms = ms ? std::min(60000u, ms) : 15000;
    r.deadline_ms = r.started_ms + r.web.timeout_ms;
    if (r.handle) {
        auto now = network_clock();
        curl_easy_setopt(r.handle, CURLOPT_TIMEOUT_MS,
                         long(r.deadline_ms > now ? r.deadline_ms - now : 1));
    }
}
void Resources::stop(Resource &r) {
    if (r.handle) {
        std::pair<Resources *, Resource *> *ctx = nullptr;
        curl_easy_getinfo(r.handle, CURLINFO_PRIVATE, &ctx);
        curl_multi_remove_handle(multi, r.handle);
        curl_easy_cleanup(r.handle);
        r.handle = nullptr;
        delete ctx;
        if (active)
            --active;
    }
    curl_slist_free_all(r.request_headers);
    r.request_headers = nullptr;
}
void Resources::discard(Resource &r) {
    received -= r.data.size();
    std::fill(r.data.begin(), r.data.end(), 0);
    std::vector<unsigned char>().swap(r.data);
}
void Resources::release_body(Resource &r) {
    if (r.web_request)
        discard(r);
}
void Resources::cancel(Resource &r, const std::string &error) {
    if (!r.web_request || r.state == Resource::ready || r.state == Resource::failed)
        return;
    stop(r);
    discard(r);
    wipe_payload(r);
    r.web_error = error;
    r.result = CURLE_ABORTED_BY_CALLBACK;
    r.transfer_ok = false;
    r.state = Resource::failed;
    completed(r);
}
bool Resources::prepare_web(Resource &r) {
    const bool cross = net_origin(r.current_url) != net_origin(r.web.source);
    r.cors_tainted |= cross;
    if ((r.web.mode == "same-origin" && r.cors_tainted) ||
        (r.web.credentials == "include" && r.cors_tainted))
        return false;
    r.unsafe_headers.clear();
    size_t safe_bytes = 0;
    for (auto &h : r.web.headers) {
        if (!cors_safe(h.first, h.second))
            r.unsafe_headers.push_back(h.first);
        else
            safe_bytes += h.second.size();
    }
    if (safe_bytes > 1024)
        for (auto &h : r.web.headers)
            if (std::find(r.unsafe_headers.begin(), r.unsafe_headers.end(), h.first) ==
                r.unsafe_headers.end())
                r.unsafe_headers.push_back(h.first);
    std::sort(r.unsafe_headers.begin(), r.unsafe_headers.end());
    r.preflight =
        r.cors_tainted && ((r.method != "GET" && r.method != "HEAD" && r.method != "POST") ||
                           !r.unsafe_headers.empty());
    return true;
}
bool Resources::web_response(Resource &r, long status) {
    if (r.cors_tainted) {
        auto allow = r.response_headers.find("access-control-allow-origin");
        if (allow == r.response_headers.end() ||
            (allow->second != "*" && allow->second != r.cors_origin))
            return false;
    }
    if (r.preflight) {
        if (status < 200 || status >= 300)
            return false;
        auto methods = header_tokens(r.response_headers["access-control-allow-methods"], false);
        if (r.method != "GET" && r.method != "HEAD" && r.method != "POST" &&
            !methods.count(r.method) && !methods.count("*"))
            return false;
        auto headers = header_tokens(r.response_headers["access-control-allow-headers"]);
        for (auto &name : r.unsafe_headers)
            if (!headers.count(name) && (!headers.count("*") || name == "authorization"))
                return false;
        discard(r);
        r.response_headers.clear();
        r.preflight = false;
        r.state = Resource::queued;
        r.http_status = 0;
        r.transfer_ok = false;
        ++revision;
        return true;
    }
    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
        if (!r.location.empty()) {
            auto next = resolve(r.effective, r.location);
            if (r.web.redirect == "error" || r.redirects >= 5 || next.empty())
                return false;
            if (net_origin(next) != net_origin(r.current_url)) {
                if (net_origin(r.current_url) != net_origin(r.web.source))
                    r.cors_origin = "null";
                r.web.headers.erase(
                    std::remove_if(r.web.headers.begin(), r.web.headers.end(),
                                   [](const auto &h) { return h.first == "authorization"; }),
                    r.web.headers.end());
            }
            if ((status == 303 && r.method != "HEAD") ||
                ((status == 301 || status == 302) && r.method == "POST")) {
                r.method = "GET";
                wipe_payload(r);
                r.web.headers.erase(std::remove_if(r.web.headers.begin(), r.web.headers.end(),
                                                   [](const auto &h) {
                                                       return h.first == "content-type" ||
                                                              h.first == "content-language" ||
                                                              h.first == "content-encoding" ||
                                                              h.first == "content-location";
                                                   }),
                                    r.web.headers.end());
            }
            ++r.redirects;
            r.pending_url = next;
            discard(r);
            r.state = Resource::checking;
            r.transfer_ok = false;
            ++revision;
            return true;
        }
    }
    r.state = status >= 200 && status < 300 ? Resource::ready : Resource::failed;
    wipe_payload(r);
    completed(r);
    return true;
}
void Resources::follow(Resource &r, bool permitted) {
    if (r.state != Resource::checking)
        return;
    r.current_url = r.effective = r.pending_url;
    r.pending_url.clear();
    if (!permitted || !prepare_web(r)) {
        r.policy_blocked = true;
        r.web_error = "TypeError";
        r.transfer_ok = false;
        r.state = Resource::failed;
        wipe_payload(r);
        completed(r);
        return;
    }
    r.response_headers.clear();
    r.state = Resource::queued;
    r.http_status = 0;
    ++revision;
}
std::vector<std::pair<std::string, std::string>>
Resources::exposed_headers(const Resource &r) const {
    const std::set<std::string> safe = {"cache-control", "content-language", "content-length",
                                        "content-type",  "expires",          "last-modified",
                                        "pragma"};
    auto found = r.response_headers.find("access-control-expose-headers");
    auto expose = header_tokens(found == r.response_headers.end() ? "" : found->second);
    std::vector<std::pair<std::string, std::string>> out;
    for (auto &h : r.response_headers)
        if (h.first != "set-cookie" && h.first != "set-cookie2" &&
            (!r.cors_tainted || safe.count(h.first) || expose.count(h.first) || expose.count("*")))
            out.push_back(h);
    return out;
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
    r.body_bytes += bytes;
    return bytes;
}
size_t Resources::header(char *data, size_t unit, size_t count, void *opaque) {
    auto *ctx = static_cast<std::pair<Resources *, Resource *> *>(opaque);
    auto &r = *ctx->second;
    if (unit && count > std::numeric_limits<size_t>::max() / unit)
        return 0;
    size_t bytes = unit * count;
    if (r.web_request) {
        if (bytes > 16384 - r.web_header_bytes) {
            r.byte_limit = true;
            return 0;
        }
        r.web_header_bytes += bytes;
    }
    std::string line(data, bytes);
    bool status_line = line.rfind("HTTP/", 0) == 0;
    if (status_line) {
        r.policies.clear();
        r.response_headers.clear();
        r.status_text.clear();
        auto space = line.find(' ');
        if (space != std::string::npos) {
            auto reason = line.find(' ', space + 1);
            if (reason != std::string::npos)
                r.status_text = line.substr(reason + 1);
            while (!r.status_text.empty() &&
                   (r.status_text.back() == '\r' || r.status_text.back() == '\n'))
                r.status_text.pop_back();
        }
        r.location.clear();
        r.header_bytes = 0;
    }
    if (bytes > 16384 - r.header_bytes)
        return 0;
    r.header_bytes += bytes;
    if (status_line)
        return bytes;
    auto colon = line.find(':');
    if (colon != std::string::npos) {
        auto key = line.substr(0, colon);
        for (char &ch : key)
            ch = char(std::tolower(static_cast<unsigned char>(ch)));
        auto begin_value = line.find_first_not_of(" \t", colon + 1);
        auto end_value = line.find_last_not_of(" \t\r\n");
        auto header_value = begin_value == std::string::npos || end_value < begin_value
                                ? ""
                                : line.substr(begin_value, end_value - begin_value + 1);
        auto found = r.response_headers.find(key);
        if (r.web_request &&
            (header_value.size() > 8192 ||
             (found == r.response_headers.end() && r.response_headers.size() >= 64) ||
             (found != r.response_headers.end() &&
              found->second.size() + 2 + header_value.size() > 8192))) {
            r.byte_limit = true;
            return 0;
        }
        if (found == r.response_headers.end())
            r.response_headers.emplace(key, header_value);
        else
            found->second += ", " + header_value;
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
    if (!r.started_ms)
        r.started_ms = network_clock();
    if ((r.document_navigation || r.form_navigation) && !r.deadline_ms)
        r.deadline_ms = r.started_ms + r.navigation_timeout_ms;
    r.web_header_bytes = 0;
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
    bool headers_ok = true;
    if (r.web_request) {
        const auto method = r.preflight ? std::string("OPTIONS") : r.method;
        if (method == "HEAD")
            curl_easy_setopt(r.handle, CURLOPT_NOBODY, 1L);
        else if (method != "GET") {
            curl_easy_setopt(r.handle, CURLOPT_CUSTOMREQUEST, method.c_str());
            if (!r.preflight) {
                curl_easy_setopt(r.handle, CURLOPT_POSTFIELDS, r.post_body.data());
                curl_easy_setopt(r.handle, CURLOPT_POSTFIELDSIZE_LARGE,
                                 curl_off_t(r.post_body.size()));
            }
        }
        auto add = [&](const std::string &key, const std::string &value) {
            auto line = key + (value.empty() ? ";" : ": " + value);
            auto *list = curl_slist_append(r.request_headers, line.c_str());
            if (list)
                r.request_headers = list;
            else
                headers_ok = false;
        };
        add("Expect", "");
        if (r.cors_tainted || (method != "GET" && method != "HEAD"))
            add("Origin", r.cors_origin);
        if (r.preflight) {
            add("Access-Control-Request-Method", r.method);
            std::string names;
            for (auto &name : r.unsafe_headers) {
                if (!names.empty())
                    names += ", ";
                names += name;
            }
            if (!names.empty())
                add("Access-Control-Request-Headers", names);
        } else {
            bool content_type = false;
            for (auto &h : r.web.headers) {
                add(h.first, h.second);
                content_type |= h.first == "content-type";
            }
            if (!content_type) {
                // CURLOPT_POSTFIELDS otherwise invents a form content type for raw buffers.
                auto *list = curl_slist_append(r.request_headers, "Content-Type:");
                if (list)
                    r.request_headers = list;
                else
                    headers_ok = false;
            }
        }
        curl_easy_setopt(r.handle, CURLOPT_HTTPHEADER, r.request_headers);
    } else if (r.method == "POST") {
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
    curl_easy_setopt(r.handle, CURLOPT_FOLLOWLOCATION,
                     (r.form_navigation || r.web_request) ? 0L : 1L);
    curl_easy_setopt(r.handle, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(r.handle, CURLOPT_CONNECTTIMEOUT, 5L);
    curl_easy_setopt(r.handle, CURLOPT_TIMEOUT, 15L);
    if (r.document_navigation || r.form_navigation) {
        auto now = network_clock();
        curl_easy_setopt(r.handle, CURLOPT_TIMEOUT_MS,
                         long(r.deadline_ms > now ? r.deadline_ms - now : 1));
        curl_easy_setopt(r.handle, CURLOPT_LOW_SPEED_LIMIT, 64L);
        curl_easy_setopt(r.handle, CURLOPT_LOW_SPEED_TIME, 20L);
    }
    curl_easy_setopt(r.handle, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(r.handle, CURLOPT_HEADERFUNCTION, header);
    curl_easy_setopt(r.handle, CURLOPT_HEADERDATA, ctx);
    curl_easy_setopt(r.handle, CURLOPT_WRITEFUNCTION, receive);
    curl_easy_setopt(r.handle, CURLOPT_WRITEDATA, ctx);
    curl_easy_setopt(r.handle, CURLOPT_USERAGENT, "SwitchVisualBrowser/0.11.1");
    if (r.web_request) {
        auto now = network_clock();
        auto remaining = r.deadline_ms > now ? r.deadline_ms - now : 1;
        curl_easy_setopt(r.handle, CURLOPT_TIMEOUT_MS, long(remaining));
    }
    bool credentials =
        !r.web_request || (!r.preflight && !r.cors_tainted && r.web.credentials != "omit");
    CURLcode code = credentials ? browser_profile_configure(r.handle)
                                : browser_profile_configure_anonymous(r.handle);
    if (!headers_ok)
        code = CURLE_OUT_OF_MEMORY;
    if (code != CURLE_OK || curl_multi_add_handle(multi, r.handle) != CURLM_OK) {
        r.error = browser_profile_status();
        r.result = code != CURLE_OK ? code : CURLE_FAILED_INIT;
        r.state = Resource::failed;
        curl_easy_cleanup(r.handle);
        r.handle = nullptr;
        curl_slist_free_all(r.request_headers);
        r.request_headers = nullptr;
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
    for (auto &kv : items) {
        auto &r = kv.second;
        if (r.web_request &&
            (r.state == Resource::queued || r.state == Resource::loading ||
             r.state == Resource::checking) &&
            network_clock() >= r.deadline_ms)
            cancel(r, "TimeoutError");
    }
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
            r.elapsed_ms = network_clock() - r.started_ms;
            long http = 0;
            char *effective = nullptr;
            curl_easy_getinfo(r.handle, CURLINFO_RESPONSE_CODE, &http);
            curl_easy_getinfo(r.handle, CURLINFO_EFFECTIVE_URL, &effective);
            if (effective)
                r.effective = effective;
            if (!r.web_request && !r.form_navigation) {
                long redirects = 0;
                curl_easy_getinfo(r.handle, CURLINFO_REDIRECT_COUNT, &redirects);
                r.redirects = unsigned(std::max(0L, redirects));
            }
            if (r.web_request) {
                browser_profile_log(r.current_url.c_str(), "web", http, msg->data.result);
                r.http_status = http;
                r.result = msg->data.result;
                r.transfer_ok = msg->data.result == CURLE_OK && http > 0;
                stop(r);
                if (!r.transfer_ok) {
                    r.web_error =
                        r.result == CURLE_OPERATION_TIMEDOUT ? "TimeoutError" : "TypeError";
                    discard(r);
                    r.state = Resource::failed;
                    wipe_payload(r);
                    completed(r);
                } else if (!web_response(r, http)) {
                    r.web_error = "TypeError";
                    r.policy_blocked = true;
                    r.transfer_ok = false;
                    discard(r);
                    r.state = Resource::failed;
                    wipe_payload(r);
                    completed(r);
                }
                continue;
            }
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
                if (r.document_navigation || r.form_navigation) {
                    if (r.byte_limit)
                        r.error = "Превышен лимит размера страницы";
                    else if (r.result == CURLE_OPERATION_TIMEDOUT)
                        r.error = "Сервер не завершил загрузку за отведённое время";
                    else if (r.result == CURLE_COULDNT_RESOLVE_HOST)
                        r.error = "Не удалось найти сервер (DNS)";
                    else if (r.result == CURLE_COULDNT_CONNECT)
                        r.error = "Не удалось соединиться с сервером";
                    else if (r.result == CURLE_PEER_FAILED_VERIFICATION ||
                             r.result == CURLE_SSL_CONNECT_ERROR ||
                             r.result == CURLE_SSL_CACERT_BADFILE)
                        r.error = "Не удалось проверить защищённое соединение (TLS)";
                }
                discard(r);
            }
            browser_profile_log(r.url.c_str(), "visual", http, msg->data.result);
            if (r.redirects)
                browser_profile_log(r.effective.c_str(), "visual-final", http, msg->data.result);
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
            (kv.second.state == Resource::queued || kv.second.state == Resource::loading ||
             kv.second.state == Resource::checking))
            return true;
    return false;
}
bool Resources::pending() const {
    for (const auto &kv : items)
        if (kv.second.state == Resource::queued || kv.second.state == Resource::loading ||
            kv.second.state == Resource::checking)
            return true;
    return false;
}
Resources::Failures Resources::failures() const {
    Failures out;
    if (request_limit)
        ++out.limits;
    for (const auto &kv : items) {
        auto &r = kv.second;
        if (r.state != Resource::failed || r.web_error == "AbortError")
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
        if (kv.second.state == Resource::failed && kv.second.web_error != "AbortError")
            ++errors;
    return std::to_string(items.size()) + " ресурсов · сбоев загрузки: " + std::to_string(errors) +
           (pending() ? " · загрузка" : "");
}
} // namespace visual
