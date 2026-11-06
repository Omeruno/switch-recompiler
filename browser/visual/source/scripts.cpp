#include "scripts.h"
extern "C" {
#include "profile.h"
#include "quickjs.h"
}
#include "js_intl.h"
#include "native_dom_js.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <litehtml/el_text.h>
#include <map>
#include <set>
#include <stdexcept>
#include <time.h>
#include <unordered_map>
namespace visual {
uint64_t Scripts::clock_ms() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return uint64_t(t.tv_sec) * 1000 + t.tv_nsec / 1000000;
}
namespace {
union Allocation {
    max_align_t align;
    size_t size;
};
void *alloc(JSMallocState *s, size_t n) {
    if (n > SIZE_MAX - sizeof(Allocation) || s->malloc_size > s->malloc_limit ||
        n + sizeof(Allocation) > s->malloc_limit - s->malloc_size)
        return nullptr;
    auto *p = static_cast<Allocation *>(std::malloc(n + sizeof(Allocation)));
    if (!p)
        return nullptr;
    p->size = n;
    s->malloc_size += n + sizeof(Allocation);
    s->malloc_count++;
    return p + 1;
}
void dealloc(JSMallocState *s, void *p) {
    if (!p)
        return;
    auto *a = static_cast<Allocation *>(p) - 1;
    s->malloc_size -= a->size + sizeof(Allocation);
    s->malloc_count--;
    std::free(a);
}
void *resize(JSMallocState *s, void *p, size_t n) {
    if (!p)
        return alloc(s, n);
    if (!n) {
        dealloc(s, p);
        return nullptr;
    }
    auto *a = static_cast<Allocation *>(p) - 1;
    size_t old = a->size;
    if (n > SIZE_MAX - sizeof(Allocation) ||
        (n > old &&
         (s->malloc_size > s->malloc_limit || n - old > s->malloc_limit - s->malloc_size)))
        return nullptr;
    a = static_cast<Allocation *>(std::realloc(a, n + sizeof(Allocation)));
    if (!a)
        return nullptr;
    a->size = n;
    s->malloc_size = s->malloc_size - old + n;
    return a + 1;
}
size_t usable(const void *p) { return p ? (static_cast<const Allocation *>(p) - 1)->size : 0; }
std::string string(JSContext *ctx, JSValueConst v, size_t limit = 65536) {
    size_t n = 0;
    const char *s = JS_ToCStringLen(ctx, &n, v);
    if (!s)
        throw std::runtime_error("Invalid string");
    if (n > limit) {
        JS_FreeCString(ctx, s);
        throw std::runtime_error("String quota exceeded");
    }
    std::string out(s, n);
    JS_FreeCString(ctx, s);
    return out;
}
std::string lower(std::string s) {
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::vector<std::string> words(const std::string &s) {
    std::vector<std::string> out;
    size_t p = 0;
    while (p < s.size()) {
        while (p < s.size() && std::isspace(static_cast<unsigned char>(s[p])))
            p++;
        size_t b = p;
        while (p < s.size() && !std::isspace(static_cast<unsigned char>(s[p])))
            p++;
        if (p > b)
            out.push_back(s.substr(b, p - b));
    }
    return out;
}
std::string origin(const std::string &url) {
    char out[384];
    if (url.rfind("demo:/", 0) == 0)
        return "demo:";
    if (!browser_origin(url.c_str(), out, sizeof(out)))
        return "";
    std::string result = out;
    if (result.size() >= 4 && result.compare(result.size() - 4, 4, ":443") == 0)
        result.resize(result.size() - 4);
    return result;
}
std::string escape(const std::string &s, bool attr = false) {
    std::string out;
    for (char c : s) {
        if (c == '&')
            out += "&amp;";
        else if (c == '<')
            out += "&lt;";
        else if (c == '>')
            out += "&gt;";
        else if (c == '"' && attr)
            out += "&quot;";
        else
            out += c;
    }
    return out;
}
bool generated(const litehtml::element::ptr &n) {
    return n->tag() == litehtml::__tag_before_ || n->tag() == litehtml::__tag_after_;
}
} // namespace
struct Scripts::Impl {
    litehtml::document::ptr doc;
    Container &container;
    Resources &resources;
    JSRuntime *runtime = nullptr;
    JSContext *context = nullptr;
    JSValue hooks = JS_UNDEFINED;
    std::vector<litehtml::element::ptr> nodes{nullptr};
    std::unordered_map<const litehtml::element *, unsigned> ids;
    struct Job {
        std::string source, url;
        Resource *resource = nullptr;
        bool external = false;
    };
    std::vector<Job> jobs;
    size_t job = 0;
    struct Fetch {
        int id;
        Resource *resource;
        std::string url;
    };
    std::vector<Fetch> fetches;
    std::vector<std::string> policies;
    std::string last_error, next_url;
    uint64_t now = 0, deadline = 0;
    unsigned checks = 0, created = 0;
    size_t mutated_bytes = 0;
    bool dirty = false, loaded = false, usable_runtime = false;
    Impl(const litehtml::document::ptr &d, Container &c, Resources &r,
         std::vector<std::string> policy)
        : doc(d), container(c), resources(r), policies(std::move(policy)) {
        try {
            initialize();
        } catch (...) {
            release();
            throw;
        }
    }
    void initialize() {
        descendants(doc->root());
        now = Scripts::clock_ms();
        const JSMallocFunctions allocator = {alloc, dealloc, resize, usable};
        runtime = JS_NewRuntime2(&allocator, nullptr);
        if (!runtime)
            throw std::runtime_error("JavaScript runtime allocation failed");
        JS_SetMemoryLimit(runtime, 32u * 1024u * 1024u);
        JS_SetMaxStackSize(runtime, 256u * 1024u);
        JS_SetInterruptHandler(runtime, interrupt, this);
        context = JS_NewContext(runtime);
        if (!context) {
            JS_FreeRuntime(runtime);
            runtime = nullptr;
            throw std::runtime_error("JavaScript context allocation failed");
        }
        JS_SetContextOpaque(context, this);
        auto global = JS_GetGlobalObject(context);
        JS_SetPropertyStr(context, global, "__native",
                          JS_NewCFunction(context, native, "__native", 4));
        JS_FreeValue(context, global);
        budget(100);
        usable_runtime = evaluate(browser_dom, sizeof(browser_dom) - 1, "native-dom");
        global = JS_GetGlobalObject(context);
        hooks = JS_GetPropertyStr(context, global, "__visualHooks");
        JSAtom atom = JS_NewAtom(context, "__visualHooks");
        JS_DeleteProperty(context, global, atom, 0);
        JS_FreeAtom(context, atom);
        JS_FreeValue(context, global);
        if (!usable_runtime)
            throw std::runtime_error(last_error);
        budget(50);
        evaluate(browser_intl, sizeof(browser_intl) - 1, "intl-ru-en");
        for (auto &el : doc->root()->select_all("meta"))
            if (lower(el->get_attr("http-equiv", "")) == "content-security-policy")
                policies.push_back(el->get_attr("content", ""));
        if (!allowed("script-src", "", true, "'unsafe-eval'")) {
            auto result = call("blockEval", {});
            JS_FreeValue(context, result);
        }
        for (auto &el : doc->root()->select_all("script")) {
            std::string type = lower(el->get_attr("type", ""));
            if (type == "module") {
                last_error = "JavaScript modules are not implemented";
                continue;
            }
            if (!type.empty() && type != "text/javascript" && type != "application/javascript")
                continue;
            if (jobs.size() >= 16) {
                last_error = "Script count quota exceeded";
                break;
            }
            std::string src = el->get_attr("src", "");
            Job entry;
            if (src.empty()) {
                el->get_text(entry.source);
                if (entry.source.size() > 128u * 1024u) {
                    last_error = "Script byte quota exceeded";
                    continue;
                }
                if (!allowed("script-src", "", true))
                    continue;
                entry.url = "inline";
            } else {
                entry.url = Resources::resolve(container.base, src);
                if (!allowed("script-src", entry.url, false))
                    continue;
                entry.external = true;
                entry.resource = resources.request(entry.url, 128u * 1024u);
                if (entry.resource)
                    entry.resource->critical = true;
                if (!entry.resource)
                    continue;
            }
            jobs.push_back(std::move(entry));
        }
    }
    ~Impl() { release(); }
    void release() {
        if (context) {
            JS_FreeValue(context, hooks);
            JS_FreeContext(context);
            context = nullptr;
            hooks = JS_UNDEFINED;
        }
        if (runtime) {
            JS_FreeRuntime(runtime);
            runtime = nullptr;
        }
    }
    void budget(unsigned ms) {
        deadline = Scripts::clock_ms() + ms;
        checks = 0;
    }
    static int interrupt(JSRuntime *, void *opaque) {
        auto *s = static_cast<Impl *>(opaque);
        return ++s->checks > 100000 || Scripts::clock_ms() >= s->deadline;
    }
    void exception() {
        auto ex = JS_GetException(context);
        const char *s = JS_ToCString(context, ex);
        last_error = s ? s : "JavaScript exception";
        if (s)
            JS_FreeCString(context, s);
        JS_FreeValue(context, ex);
        if (last_error.size() > 192)
            last_error.resize(192);
    }
    bool evaluate(const char *source, size_t n, const std::string &name) {
        JSValue v = JS_Eval(context, source, n, name.c_str(), JS_EVAL_TYPE_GLOBAL);
        bool ok = !JS_IsException(v);
        if (!ok)
            exception();
        JS_FreeValue(context, v);
        return ok;
    }
    JSValue call(const char *name, std::vector<JSValue> args) {
        auto f = JS_GetPropertyStr(context, hooks, name);
        auto v = JS_Call(context, f, hooks, int(args.size()), args.data());
        JS_FreeValue(context, f);
        for (auto a : args)
            JS_FreeValue(context, a);
        if (JS_IsException(v))
            exception();
        return v;
    }
    void drain() {
        for (unsigned i = 0; i < 32 && Scripts::clock_ms() < deadline; i++) {
            JSContext *ctx = nullptr;
            int rc = JS_ExecutePendingJob(runtime, &ctx);
            if (rc < 0) {
                exception();
                break;
            }
            if (!rc)
                break;
        }
    }
    bool allowed(const std::string &directive, const std::string &url, bool inline_code,
                 const std::string &keyword = "'unsafe-inline'") const {
        if (!inline_code && (url.empty() || Resources::resolve("", url).empty()) &&
            url.rfind("demo:/", 0) != 0)
            return false;
        for (const auto &policy : policies) {
            std::map<std::string, std::vector<std::string>> entries;
            size_t start = 0;
            while (start < policy.size()) {
                auto end = policy.find(';', start);
                auto list =
                    words(policy.substr(start, end == std::string::npos ? end : end - start));
                if (!list.empty() && !entries.count(list[0])) {
                    auto name = lower(list[0]);
                    list.erase(list.begin());
                    entries.emplace(name, std::move(list));
                }
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
            auto it = entries.find(directive == "script-src" && keyword == "'unsafe-inline'"
                                       ? "script-src-elem"
                                       : directive);
            if (it == entries.end())
                it = entries.find(directive);
            if (it == entries.end() && directive == "script-src-attr")
                it = entries.find("script-src");
            if (it == entries.end())
                it = entries.find("default-src");
            if (it == entries.end())
                continue;
            bool yes = false;
            for (auto &token : it->second) {
                if (token == "'strict-dynamic'")
                    return false;
                if (inline_code) {
                    if (token == keyword)
                        yes = true;
                } else if (token == "*" || token == "https:" ||
                           (token == "'self'" && origin(url) == origin(container.page_url)) ||
                           token == origin(url) || token == origin(url) + "/")
                    yes = true;
            }
            if (!yes)
                return false;
        }
        return true;
    }
    unsigned handle(const litehtml::element::ptr &el) {
        if (!el)
            return 0;
        auto i = ids.find(el.get());
        if (i != ids.end())
            return i->second;
        if (nodes.size() >= 8192)
            throw std::runtime_error("DOM handle quota exceeded");
        unsigned id = unsigned(nodes.size());
        nodes.push_back(el);
        ids.emplace(el.get(), id);
        return id;
    }
    litehtml::element::ptr node(int id) {
        if (id == 0)
            return doc->root();
        if (id < 0 || size_t(id) >= nodes.size())
            throw std::runtime_error("Invalid node handle");
        return nodes[size_t(id)];
    }
    std::vector<litehtml::element::ptr> descendants(const litehtml::element::ptr &root,
                                                    bool include = true) {
        std::vector<litehtml::element::ptr> out, stack{root};
        while (!stack.empty()) {
            auto n = stack.back();
            stack.pop_back();
            if (generated(n))
                continue;
            if (include || n != root)
                out.push_back(n);
            if (out.size() > 8192)
                throw std::runtime_error("DOM traversal quota exceeded");
            auto children = n->children();
            for (auto it = children.rbegin(); it != children.rend(); ++it)
                stack.push_back(*it);
        }
        return out;
    }
    void quota(size_t bytes) {
        if (bytes > 65536 || mutated_bytes > 8u * 1024u * 1024u - bytes)
            throw std::runtime_error("DOM mutation quota exceeded");
        mutated_bytes += bytes;
        dirty = true;
    }
    std::string html(const litehtml::element::ptr &el, unsigned depth = 0) {
        if (depth > 128)
            throw std::runtime_error("DOM depth exceeded");
        if (el->is_text()) {
            std::string t;
            el->get_text(t);
            return escape(t);
        }
        auto tag = std::dynamic_pointer_cast<litehtml::html_tag>(el);
        if (!tag)
            return "";
        std::string out = "<" + std::string(el->get_tagName());
        for (auto &a : tag->native_attributes())
            out += ' ' + a.first + "=\"" + escape(a.second, true) + "\"";
        out += '>';
        for (auto &child : el->children())
            if (!generated(child)) {
                out += html(child, depth + 1);
                if (out.size() > 65536)
                    throw std::runtime_error("HTML quota exceeded");
            }
        out += "</" + std::string(el->get_tagName()) + ">";
        return out;
    }
    static JSValue native(JSContext *ctx, JSValueConst, int argc, JSValueConst *argv) {
        auto *s = static_cast<Impl *>(JS_GetContextOpaque(ctx));
        try {
            return s->operation(argc, argv);
        } catch (const std::exception &e) {
            return JS_ThrowTypeError(ctx, "%s", e.what());
        } catch (...) {
            return JS_ThrowInternalError(ctx, "Native DOM failure");
        }
    }
    JSValue operation(int argc, JSValueConst *a) {
        if (argc < 2)
            return JS_ThrowTypeError(context, "Invalid native call");
        auto str = [&](int i, size_t limit = 65536) {
            if (i >= argc)
                throw std::runtime_error("Missing argument");
            return string(context, a[i], limit);
        };
        auto integer = [&](int i) {
            int32_t value = 0;
            if (i >= argc || JS_ToInt32(context, &value, a[i]) < 0)
                throw std::runtime_error("Expected integer");
            return value;
        };
        const std::string op = str(0, 32);
        int id = integer(1);
        auto js = [&](const std::string &text) {
            return JS_NewStringLen(context, text.data(), text.size());
        };
        if (op == "now")
            return JS_NewFloat64(context, double(now));
        if (op == "viewport") {
            auto out = JS_NewObject(context);
            for (auto pair :
                 {std::pair<const char *, double>{"width", double(container.viewport_width)},
                  {"height", double(container.viewport_height)},
                  {"ratio", 1280.0 / container.viewport_width},
                  {"scrollX", double(container.scroll_x)},
                  {"scrollY", double(container.scroll_y)}})
                JS_SetPropertyStr(context, out, pair.first, JS_NewFloat64(context, pair.second));
            return out;
        }
        if (op == "media") {
            litehtml::media_features features;
            container.get_media_features(features);
            return JS_NewBool(context,
                              litehtml::parse_media_query_list(str(2, 512), doc).check(features));
        }
        if (op == "url")
            return js(container.page_url);
        if (op == "report") {
            last_error = str(2, 192);
            return JS_UNDEFINED;
        }
        if (op == "resolve")
            return js(Resources::resolve(argc > 3 && !str(3).empty() ? str(3) : container.base,
                                         str(2, 2048)));
        if (op == "navigate") {
            auto url = Resources::resolve(container.base, str(2, 2048));
            if (url.empty())
                throw std::runtime_error("Navigation requires HTTPS");
            next_url = url;
            return JS_UNDEFINED;
        }
        if (op == "origin")
            return js(origin(str(2, 2048)));
        if (op == "urlQuery") {
            CURLU *u = curl_url();
            char *out = nullptr;
            bool ok = u && curl_url_set(u, CURLUPART_URL, str(2, 2048).c_str(), 0) == CURLUE_OK &&
                      curl_url_set(u, CURLUPART_QUERY, str(3, 4096).c_str(), 0) == CURLUE_OK &&
                      curl_url_get(u, CURLUPART_URL, &out, 0) == CURLUE_OK;
            auto result = ok ? js(out) : JS_ThrowTypeError(context, "Invalid URL");
            curl_free(out);
            curl_url_cleanup(u);
            return result;
        }
        if (op == "urlPart") {
            CURLU *u = curl_url();
            char *part = nullptr;
            int key = integer(3);
            if (key < 0 || key > 2 || !u)
                throw std::runtime_error("Invalid URL part");
            curl_url_set(u, CURLUPART_URL, str(2, 2048).c_str(), 0);
            curl_url_get(u,
                         key == 0   ? CURLUPART_HOST
                         : key == 1 ? CURLUPART_PATH
                                    : CURLUPART_QUERY,
                         &part, 0);
            JSValue result = js(part ? part : "");
            curl_free(part);
            curl_url_cleanup(u);
            return result;
        }
        if (op == "fetch") {
            if (fetches.size() >= 32)
                throw std::runtime_error("Fetch quota exceeded");
            auto url = Resources::resolve(container.base, str(3, 2048));
            if (origin(url) != origin(container.page_url) || !allowed("connect-src", url, false))
                throw std::runtime_error("Fetch origin/CSP policy rejected request");
            auto *r = resources.request(url, 512u * 1024u);
            if (!r)
                throw std::runtime_error("Resource quota exceeded");
            r->critical = true;
            fetches.push_back({integer(2), r, url});
            return JS_UNDEFINED;
        }
        if (op == "cookie") {
            if (argc > 2) {
                browser_cookie_set(container.page_url.c_str(), str(2, 4096).c_str());
                return JS_UNDEFINED;
            }
            char value[8192];
            browser_cookie_get(container.page_url.c_str(), value, sizeof(value));
            return js(value);
        }
        if (op.rfind("storage", 0) == 0) {
            int session = argc > 2 && JS_ToBool(context, a[2]);
            if (op == "storageLength")
                return JS_NewInt32(
                    context, int(browser_storage_length(container.page_url.c_str(), session)));
            if (op == "storageKey") {
                auto *p =
                    browser_storage_key(container.page_url.c_str(), session, size_t(integer(3)));
                return p ? js(p) : JS_NULL;
            }
            if (op == "storageGet") {
                auto *p =
                    browser_storage_get(container.page_url.c_str(), session, str(3, 1024).c_str());
                return p ? js(p) : JS_NULL;
            }
            int rc = 0;
            if (op == "storageSet")
                rc = browser_storage_set(container.page_url.c_str(), session, str(3, 1024).c_str(),
                                         str(4, 16384).c_str());
            else if (op == "storageRemove")
                rc = browser_storage_remove(container.page_url.c_str(), session,
                                            str(3, 1024).c_str());
            else if (op == "storageClear")
                rc = browser_storage_clear(container.page_url.c_str(), session);
            else
                throw std::runtime_error("Unknown storage operation");
            if (rc)
                throw std::runtime_error("Storage quota/write failure");
            return JS_UNDEFINED;
        }
        if (op == "create" || op == "createText") {
            if (++created > 4096)
                throw std::runtime_error("Dynamic DOM node quota exceeded");
            auto name = str(2, op == "create" ? 64 : 65536);
            quota(name.size());
            litehtml::element::ptr el;
            if (op == "createText")
                el = std::make_shared<litehtml::el_text>(name.c_str(), doc);
            else {
                if (name.empty() || !std::isalpha(static_cast<unsigned char>(name[0])) ||
                    std::any_of(name.begin(), name.end(), [](unsigned char ch) {
                        return !std::isalnum(ch) && ch != '-' && ch != ':';
                    }))
                    throw std::runtime_error("Invalid element name");
                el = doc->create_element(lower(name).c_str(), {});
            }
            return JS_NewInt32(context, int(handle(el)));
        }
        auto el = node(id);
        if (op == "root")
            return JS_NewInt32(context, int(handle(doc->root())));
        if (op == "kind")
            return JS_NewInt32(context, el->is_text() ? 3 : 1);
        if (op == "tag")
            return js(el->get_tagName());
        if (op == "parent") {
            if (!id)
                return JS_NewInt32(context, -1);
            return JS_NewInt32(context, el == doc->root() ? 0
                                        : el->parent()    ? int(handle(el->parent()))
                                                          : -1);
        }
        if (op == "children") {
            auto result = JS_NewArray(context);
            uint32_t index = 0;
            if (!id)
                JS_SetPropertyUint32(context, result, index++,
                                     JS_NewInt32(context, int(handle(doc->root()))));
            else
                for (auto &child : el->children())
                    if (!generated(child))
                        JS_SetPropertyUint32(context, result, index++,
                                             JS_NewInt32(context, int(handle(child))));
            return result;
        }
        if (op == "text") {
            if (argc < 3) {
                std::string text;
                el->get_text(text);
                return js(text);
            }
            if (!id)
                throw std::runtime_error("Cannot replace document text");
            auto text = str(2);
            quota(text.size());
            if (auto leaf = std::dynamic_pointer_cast<litehtml::el_text>(el))
                leaf->set_native_text(text);
            else {
                auto children = el->children();
                for (auto &child : children)
                    el->removeChild(child);
                if (!text.empty())
                    el->appendChild(std::make_shared<litehtml::el_text>(text.c_str(), doc));
            }
            return JS_UNDEFINED;
        }
        if (op == "html") {
            if (argc < 3) {
                std::string text;
                for (auto &child : el->children()) {
                    text += html(child);
                    if (text.size() > 65536)
                        throw std::runtime_error("HTML quota exceeded");
                }
                return js(text);
            }
            if (!id || el->is_text())
                throw std::runtime_error("Invalid innerHTML target");
            auto source = str(2, 16384);
            if (std::count(source.begin(), source.end(), '<') > 128)
                throw std::runtime_error("Fragment node quota exceeded");
            quota(source.size());
            auto children = el->children();
            for (auto &child : children)
                el->removeChild(child);
            doc->append_children_from_string(*el, source.c_str(), false);
            try {
                size_t count = 0;
                for (auto &child : el->children())
                    count += descendants(child).size();
                if (count > 4096 - created)
                    throw std::runtime_error("Fragment node quota exceeded");
                created += unsigned(count);
            } catch (...) {
                auto added = el->children();
                for (auto &child : added)
                    el->removeChild(child);
                for (auto &child : children)
                    el->appendChild(child);
                throw;
            }
            return JS_UNDEFINED;
        }
        if (op == "attr") {
            auto key = lower(str(2, 96));
            if (argc < 4) {
                auto *v = el->get_attr(key.c_str());
                return v ? js(v) : JS_NULL;
            }
            auto value = JS_IsNull(a[3]) ? "" : str(3, 2048);
            quota(value.size());
            if (JS_IsNull(a[3]))
                el->remove_attr(key.c_str());
            else
                el->set_attr(key.c_str(), value.c_str());
            return JS_UNDEFINED;
        }
        if (op == "value") {
            auto c = std::dynamic_pointer_cast<Control>(el);
            if (argc < 3)
                return js(c ? c->value() : el->get_attr("value", ""));
            auto value = str(2, 4096);
            quota(value.size());
            if (c)
                c->value(value);
            else
                el->set_attr("value", value.c_str());
            return JS_UNDEFINED;
        }
        if (op == "insert") {
            if (!id || el->is_text())
                throw std::runtime_error("Invalid parent");
            auto child = node(integer(2));
            int before_id = integer(3);
            auto before = before_id < 0 ? litehtml::element::ptr{} : node(before_id);
            if (before && before->parent() != el)
                throw std::runtime_error("Reference is not a child");
            if (before == child)
                return JS_UNDEFINED;
            unsigned depth = 0;
            for (auto parent = el; parent; parent = parent->parent()) {
                if (++depth > 256)
                    throw std::runtime_error("DOM depth limit");
                if (parent == child)
                    throw std::runtime_error("DOM cycle");
            }
            if (child == doc->root())
                throw std::runtime_error("Cannot move root");
            auto tag = std::dynamic_pointer_cast<litehtml::html_tag>(el);
            if (!tag)
                throw std::runtime_error("Invalid parent");
            quota(0);
            if (auto old = child->parent())
                old->removeChild(child);
            el->appendChild(child);
            if (before) {
                auto &list = tag->children();
                list.splice(std::find(list.begin(), list.end(), before), list,
                            std::find(list.begin(), list.end(), child));
            }
            return JS_UNDEFINED;
        }
        if (op == "append" || op == "remove") {
            if (!id || el->is_text())
                throw std::runtime_error("Invalid parent");
            auto child = node(integer(2));
            if (child == doc->root())
                throw std::runtime_error("Cannot move root");
            quota(0);
            if (op == "remove") {
                if (child->parent() != el)
                    throw std::runtime_error("Node is not a child");
                el->removeChild(child);
            } else {
                unsigned depth = 0;
                for (auto p = el; p; p = p->parent()) {
                    if (++depth > 256)
                        throw std::runtime_error("DOM depth limit");
                    if (p == child)
                        throw std::runtime_error("DOM cycle");
                }
                if (auto p = child->parent())
                    p->removeChild(child);
                el->appendChild(child);
            }
            return JS_UNDEFINED;
        }
        if (op == "query" || op == "matches") {
            auto selector = str(2, 512);
            litehtml::css_selector parsed;
            if (!parsed.parse(selector, doc->mode()))
                throw std::runtime_error("Invalid selector");
            if (op == "matches") {
                auto tag = std::dynamic_pointer_cast<litehtml::html_tag>(el);
                return JS_NewBool(context, tag && tag->select(parsed));
            }
            auto result = JS_NewArray(context);
            uint32_t index = 0;
            for (auto &item : el->select_all(parsed))
                if ((!id || item != el) && !generated(item) && !item->is_text())
                    JS_SetPropertyUint32(context, result, index++,
                                         JS_NewInt32(context, int(handle(item))));
            return result;
        }
        if (op == "order") {
            if (argc < 3 || !JS_IsArray(context, a[2]))
                throw std::runtime_error("Expected node list");
            auto length = JS_GetPropertyStr(context, a[2], "length");
            int32_t count = 0;
            JS_ToInt32(context, &count, length);
            JS_FreeValue(context, length);
            if (count < 0 || count > 8192)
                throw std::runtime_error("Node list quota");
            std::set<int> keys;
            for (int i = 0; i < count; i++) {
                auto v = JS_GetPropertyUint32(context, a[2], i);
                int32_t n = 0;
                JS_ToInt32(context, &n, v);
                JS_FreeValue(context, v);
                keys.insert(n);
            }
            auto out = JS_NewArray(context);
            uint32_t i = 0;
            for (auto &n : descendants(el, !id))
                if (ids.count(n.get()) && keys.count(int(ids[n.get()])))
                    JS_SetPropertyUint32(context, out, i++,
                                         JS_NewInt32(context, int(ids[n.get()])));
            return out;
        }
        if (op == "id") {
            auto key = str(2, 2048);
            if (key.empty())
                return JS_NewInt32(context, 0);
            for (auto &n : descendants(doc->root()))
                if (key == n->get_attr("id", ""))
                    return JS_NewInt32(context, int(handle(n)));
            return JS_NewInt32(context, 0);
        }
        if (op == "computed") {
            if (dirty) {
                container.invalidate_text();
                doc->refresh_native_dom();
                doc->render(container.viewport_width);
                dirty = false;
            }
            auto key = str(2, 96);
            auto &css = el->css();
            if (key == "color")
                return js(css.get_color().to_string());
            if (key == "font-size")
                return js(std::to_string(float(css.get_font_size())) + "px");
            if (key == "display") {
                auto d = css.get_display();
                return js(d == litehtml::display_none           ? "none"
                          : d == litehtml::display_block        ? "block"
                          : d == litehtml::display_flex         ? "flex"
                          : d == litehtml::display_inline_block ? "inline-block"
                                                                : "inline");
            }
            auto p = el->get_placement();
            if (key == "width")
                return js(std::to_string(float(p.width)) + "px");
            if (key == "height")
                return js(std::to_string(float(p.height)) + "px");
            return js("");
        }
        if (op == "rect") {
            if (dirty) {
                container.invalidate_text();
                doc->refresh_native_dom();
                doc->render(container.viewport_width);
                dirty = false;
            }
            auto p = el->get_placement();
            auto out = JS_NewObject(context);
            for (auto pair : {std::pair<const char *, float>{"x", float(p.x) - container.scroll_x},
                              {"y", float(p.y) - container.scroll_y},
                              {"left", float(p.x) - container.scroll_x},
                              {"top", float(p.y) - container.scroll_y},
                              {"right", float(p.right()) - container.scroll_x},
                              {"bottom", float(p.bottom()) - container.scroll_y},
                              {"width", float(p.width)},
                              {"height", float(p.height)}})
                JS_SetPropertyStr(context, out, pair.first, JS_NewFloat64(context, pair.second));
            return out;
        }
        throw std::runtime_error("Unsupported DOM operation");
    }
    bool event(const litehtml::element::ptr &el, const std::string &type, bool cancelable) {
        if (!usable_runtime)
            return true;
        budget(30);
        if (el && allowed("script-src-attr", "", true)) {
            for (auto n = el; n; n = n->parent()) {
                const char *source = n->get_attr(("on" + type).c_str());
                if (!source || std::strlen(source) > 2048)
                    continue;
                std::string body = "(function(event){" + std::string(source) + "\n})";
                auto fn = JS_Eval(context, body.c_str(), body.size(), "inline-handler",
                                  JS_EVAL_TYPE_GLOBAL);
                if (JS_IsException(fn)) {
                    exception();
                    JS_FreeValue(context, fn);
                    continue;
                }
                auto result = call("inline", {JS_NewInt32(context, int(handle(n))),
                                              JS_NewString(context, type.c_str()), fn});
                JS_FreeValue(context, result);
            }
        }
        auto result =
            call("event", {JS_NewInt32(context, el ? int(handle(el)) : 0),
                           JS_NewString(context, type.c_str()), JS_NewBool(context, cancelable)});
        bool ok = JS_IsException(result) ? false : JS_ToBool(context, result) > 0;
        JS_FreeValue(context, result);
        drain();
        return ok;
    }
    bool poll(uint64_t time) {
        if (!usable_runtime)
            return dirty;
        now = time;
        budget(8);
        if (!loaded) {
            while (job < jobs.size()) {
                auto &j = jobs[job];
                if (j.resource && (j.resource->state == Resource::queued ||
                                   j.resource->state == Resource::loading))
                    return dirty;
                if (j.external) {
                    if (j.resource->state != Resource::ready ||
                        !allowed("script-src", j.resource->effective, false)) {
                        last_error = "Script request failed or was blocked";
                        ++job;
                        continue;
                    }
                    j.source.assign(j.resource->data.begin(), j.resource->data.end());
                }
                budget(50);
                evaluate(j.source.c_str(), j.source.size(), j.url.substr(0, j.url.find('?')));
                ++job;
                drain();
                return dirty;
            }
            loaded = true;
            budget(30);
            auto result = call("ready", {});
            JS_FreeValue(context, result);
            drain();
        }
        budget(8);
        auto tick = call("tick", {JS_NewFloat64(context, double(now))});
        JS_FreeValue(context, tick);
        drain();
        for (auto it = fetches.begin(); it != fetches.end();) {
            auto *r = it->resource;
            if (r->state == Resource::queued || r->state == Resource::loading) {
                ++it;
                continue;
            }
            bool ok = r->transfer_ok && r->data.size() <= 512u * 1024u &&
                      origin(r->effective) == origin(container.page_url) &&
                      allowed("connect-src", r->effective, false);
            std::string body = ok ? std::string(r->data.begin(), r->data.end()) : "";
            budget(8);
            auto result =
                call("fetched",
                     {JS_NewInt32(context, it->id),
                      JS_NewBool(context, ok && r->http_status >= 200 && r->http_status < 300),
                      JS_NewInt32(context, int(r->http_status)),
                      JS_NewString(context, r->effective.c_str()),
                      JS_NewStringLen(context, body.data(), body.size()),
                      JS_NewString(context, ok ? "" : "Fetch failed or redirected across origin")});
            JS_FreeValue(context, result);
            it = fetches.erase(it);
            drain();
        }
        return dirty;
    }
};
Scripts::Scripts(const litehtml::document::ptr &d, Container &c, Resources &r,
                 std::vector<std::string> p)
    : impl(std::make_unique<Impl>(d, c, r, std::move(p))) {}
Scripts::~Scripts() = default;
bool Scripts::poll(uint64_t n) { return impl->poll(n); }
bool Scripts::dispatch(const litehtml::element::ptr &el, const std::string &type, bool cancelable) {
    return impl->event(el, type, cancelable);
}
bool Scripts::eval(const std::string &source, unsigned ms) {
    impl->budget(ms);
    bool result = impl->evaluate(source.c_str(), source.size(), "host-probe");
    impl->drain();
    return result;
}
bool Scripts::changed() const { return impl->dirty; }
void Scripts::flush() {
    if (!impl->dirty)
        return;
    impl->container.invalidate_text();
    impl->doc->refresh_native_dom();
    impl->doc->render(impl->container.viewport_width);
    impl->dirty = false;
}
std::string Scripts::navigation() {
    auto url = std::move(impl->next_url);
    impl->next_url.clear();
    return url;
}
std::string Scripts::error() const { return impl->last_error; }
bool Scripts::ready() const { return impl->loaded; }
} // namespace visual
