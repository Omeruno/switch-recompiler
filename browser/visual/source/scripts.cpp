#include "scripts.h"
#include "websocket.h"
extern "C" {
#include "profile.h"
#include "quickjs.h"
}
#include "js_intl.h"
#include "native_dom_js.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <litehtml/el_text.h>
#include <litehtml/render_item.h>
#include <map>
#include <set>
#include <sstream>
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
std::string decode_utf8(const uint8_t *bytes, size_t size) {
    std::string out;
    out.reserve(size);
    for (size_t i = 0; i < size;) {
        unsigned lead = bytes[i], length = 0, low = 0x80, high = 0xbf;
        if (lead < 0x80) {
            out.push_back(char(lead));
            ++i;
            continue;
        }
        if (lead >= 0xc2 && lead <= 0xdf)
            length = 2;
        else if (lead >= 0xe0 && lead <= 0xef) {
            length = 3;
            if (lead == 0xe0)
                low = 0xa0;
            if (lead == 0xed)
                high = 0x9f;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            length = 4;
            if (lead == 0xf0)
                low = 0x90;
            if (lead == 0xf4)
                high = 0x8f;
        }
        if (!length) {
            out.append("\xef\xbf\xbd", 3);
            ++i;
            continue;
        }
        unsigned j = 1;
        for (; j < length && i + j < size; j++) {
            unsigned ch = bytes[i + j];
            if (ch < (j == 1 ? low : 0x80) || ch > (j == 1 ? high : 0xbf))
                break;
        }
        if (j != length) {
            out.append("\xef\xbf\xbd", 3);
            i += j;
        } else {
            out.append(reinterpret_cast<const char *>(bytes + i), length);
            i += length;
        }
    }
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
        std::string source, url, module, nonce;
        Resource *resource = nullptr;
        enum Mode { ordered, asynchronous, deferred, dynamic_ordered } mode = ordered;
        bool external = false, done = false, dynamic = false;
        litehtml::element::ptr element;
    };
    std::deque<Job> jobs;
    struct ScriptState {
        bool started = false, force_async = true;
    };
    std::unordered_map<const litehtml::element *, ScriptState> script_states;
    unsigned script_depth = 0;
    bool eval_blocked = false;
    litehtml::element::ptr current_script;
    struct Fetch {
        int id;
        Resource *resource;
        std::string url;
    };
    std::vector<Fetch> fetches;
    struct Rejection {
        JSValue promise, reason;
        bool reported = false, handled = false;
    };
    std::vector<Rejection> rejections;
    bool rejection_overflow = false;
    std::vector<std::string> policies;
    std::string last_error, next_url, source_origin;
    Issues issue_log;
    Submission next_form;
    bool next_reload = false;
    uint64_t now = 0, deadline = 0, origin_time = 0;
    unsigned checks = 0, created = 0;
    size_t mutated_bytes = 0;
    bool dirty = false, interactive = false, dom_ready = false, complete = false, loaded = false,
         usable_runtime = false;
    uint64_t previous_frame = 0;
    unsigned task_source = 0;
    WebSockets sockets;
#include "module_loader.inc"
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
        now = origin_time = Scripts::clock_ms();
        const JSMallocFunctions allocator = {alloc, dealloc, resize, usable};
        runtime = JS_NewRuntime2(&allocator, nullptr);
        if (!runtime)
            throw std::runtime_error("JavaScript runtime allocation failed");
        JS_SetMemoryLimit(runtime, 32u * 1024u * 1024u);
        JS_SetMaxStackSize(runtime, 256u * 1024u);
        JS_SetInterruptHandler(runtime, interrupt, this);
        JS_SetHostPromiseRejectionTracker(runtime, rejection_tracker, this);
        context = JS_NewContext(runtime);
        if (!context) {
            JS_FreeRuntime(runtime);
            runtime = nullptr;
            throw std::runtime_error("JavaScript context allocation failed");
        }
        JS_SetContextOpaque(context, this);
        JS_SetModuleLoaderFunc2(runtime, module_normalize, module_load, module_attributes, this);
        JS_SetHostImportModuleFunc(runtime, module_dynamic, this);
        auto global = JS_GetGlobalObject(context);
        JS_SetPropertyStr(context, global, "__native",
                          JS_NewCFunction(context, native, "__native", 4));
        JS_FreeValue(context, global);
        budget(500); // Trusted bundled facade, not page-supplied JavaScript.
        usable_runtime = evaluate(browser_dom, sizeof(browser_dom) - 1, "native-dom");
        global = JS_GetGlobalObject(context);
        hooks = JS_GetPropertyStr(context, global, "__visualHooks");
        JSAtom atom = JS_NewAtom(context, "__visualHooks");
        JS_DeleteProperty(context, global, atom, 0);
        JS_FreeAtom(context, atom);
        JS_FreeValue(context, global);
        if (!usable_runtime)
            throw std::runtime_error(last_error);
        budget(500);
        evaluate(browser_intl, sizeof(browser_intl) - 1, "intl-ru-en");
        absorb_meta();
        for (auto &el : doc->root()->select_all("script")) {
            script_states[el.get()] = {false, false};
            prepare_script(el, false);
            if (!script_states[el.get()].started && !el->get_attr("async"))
                script_states[el.get()].force_async = true;
        }
    }
    ~Impl() { release(); }
    void release() {
        sockets.clear();
        release_modules();
        for (auto &f : fetches)
            resources.cancel(*f.resource);
        fetches.clear();
        if (runtime)
            JS_SetHostPromiseRejectionTracker(runtime, nullptr, nullptr);
        if (context) {
            for (auto &r : rejections) {
                JS_FreeValue(context, r.promise);
                JS_FreeValue(context, r.reason);
            }
            rejections.clear();
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
    static void rejection_tracker(JSContext *ctx, JSValueConst promise, JSValueConst reason,
                                  JS_BOOL handled, void *opaque) noexcept {
        auto *self = static_cast<Impl *>(opaque);
        for (auto &r : self->rejections)
            if (JS_VALUE_GET_PTR(r.promise) == JS_VALUE_GET_PTR(promise)) {
                r.handled = handled;
                return;
            }
        if (handled)
            return;
        if (self->rejections.size() >= 32) {
            self->rejection_overflow = true;
            return;
        }
        auto saved_promise = JS_DupValue(ctx, promise), saved_reason = JS_DupValue(ctx, reason);
        try {
            self->rejections.push_back({saved_promise, saved_reason});
        } catch (...) {
            JS_FreeValue(ctx, saved_promise);
            JS_FreeValue(ctx, saved_reason);
            self->rejection_overflow = true;
        }
    }
    bool report_rejection() {
        if (rejection_overflow) {
            issue_log.add("limit", "promise-rejections", origin(container.page_url));
            rejection_overflow = false;
        }
        for (size_t i = 0; i < rejections.size();) {
            auto &r = rejections[i];
            if (r.handled && !r.reported) {
                JS_FreeValue(context, r.promise);
                JS_FreeValue(context, r.reason);
                rejections.erase(rejections.begin() + i);
                continue;
            }
            if (!r.reported || r.handled) {
                bool handled = r.handled;
                auto promise = JS_DupValue(context, r.promise),
                     reason = JS_DupValue(context, r.reason);
                r.reported = true;
                if (handled) {
                    JS_FreeValue(context, r.promise);
                    JS_FreeValue(context, r.reason);
                    rejections.erase(rejections.begin() + i);
                }
                // Listeners can attach catch handlers and mutate the tracker; retain no iterator.
                auto result = call("rejected", {promise, reason, JS_NewBool(context, handled)});
                if (!handled && !JS_IsException(result) && JS_ToBool(context, result) > 0)
                    issue_log.add("promise", "unhandled", origin(container.page_url));
                JS_FreeValue(context, result);
                return true;
            }
            ++i;
        }
        return false;
    }
    static uint64_t execution_ms() {
#if defined(CLOCK_THREAD_CPUTIME_ID) && !defined(__SWITCH__)
        timespec t{};
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) == 0)
            return uint64_t(t.tv_sec) * 1000 + t.tv_nsec / 1000000;
#endif
        return Scripts::clock_ms();
    }
    void budget(unsigned ms) {
        deadline = execution_ms() + ms;
        checks = 0;
    }
    static int interrupt(JSRuntime *, void *opaque) {
        auto *s = static_cast<Impl *>(opaque);
        return ++s->checks > 100000 || execution_ms() >= s->deadline;
    }
    void exception() {
        auto ex = JS_GetException(context);
        // Class only: messages/stacks may contain form values or access tokens.
        std::string kind = "Error";
        if (JS_IsObject(ex)) {
            auto value = JS_GetPropertyStr(context, ex, "name");
            const char *name = JS_ToCString(context, value);
            if (name) {
                for (auto known : {"TypeError", "ReferenceError", "SyntaxError", "RangeError",
                                   "InternalError", "EvalError"})
                    if (std::strcmp(name, known) == 0)
                        kind = known;
                JS_FreeCString(context, name);
            }
            JS_FreeValue(context, value);
        }
        issue_log.add("exception", kind,
                      source_origin.empty() ? origin(container.page_url) : source_origin);
        const char *s = JS_ToCString(context, ex);
        last_error = s ? s : "JavaScript exception";
        if (s)
            JS_FreeCString(context, s);
        JS_FreeValue(context, ex);
        if (last_error.size() > 192)
            last_error.resize(192);
    }
    bool evaluate(const char *source, size_t n, const std::string &name) {
        auto previous = source_origin;
        source_origin = name.rfind("https://", 0) == 0 ? origin(name) : origin(container.page_url);
        JSValue v = JS_Eval(context, source, n, name.c_str(), JS_EVAL_TYPE_GLOBAL);
        bool ok = !JS_IsException(v);
        if (!ok)
            exception();
        source_origin = std::move(previous);
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
    static JSValue microtask_job(JSContext *ctx, int, JSValueConst *args) {
        return JS_Call(ctx, args[0], JS_UNDEFINED, 0, nullptr);
    }
    bool drain() {
        for (unsigned i = 0; i < 32 && execution_ms() < deadline; i++) {
            JSContext *ctx = nullptr;
            int rc = JS_ExecutePendingJob(runtime, &ctx);
            if (rc < 0) {
                exception();
                break;
            }
            if (!rc)
                break;
        }
        return !JS_IsJobPending(runtime);
    }
    void flush_layout() {
        if (!dirty)
            return;
        container.restyle(doc);
        dirty = false;
    }
    struct Box {
        litehtml::position border, content, padding;
        unsigned depth = 1;
        bool rendered = false;
    };
    Box box(const litehtml::element::ptr &el) {
        Box b;
        bool connected = false, hidden = false, fixed = false;
        for (auto n = el; n; n = n->parent()) {
            ++b.depth;
            connected |= n == doc->root();
        }
        // Newly created detached nodes have not computed their CSS yet.
        if (!connected)
            return b;
        for (auto n = el; n; n = n->parent()) {
            hidden |= n->css().get_display() == litehtml::display_none;
            fixed |= n->css().get_position() == litehtml::element_position_fixed;
        }
        auto ri = el->get_render_item();
        if (!connected || hidden || !ri)
            return b;
        b.rendered = true;
        auto content = ri->get_placement();
        if (!fixed) {
            content.x -= container.scroll_x;
            content.y -= container.scroll_y;
        }
        b.content = {ri->padding_left(), ri->padding_top(), content.width, content.height};
        b.padding = {content.x - ri->padding_left(), content.y - ri->padding_top(),
                     content.width + ri->padding_left() + ri->padding_right(),
                     content.height + ri->padding_top() + ri->padding_bottom()};
        b.border = {b.padding.x - ri->border_left(), b.padding.y - ri->border_top(),
                    b.padding.width + ri->border_left() + ri->border_right(),
                    b.padding.height + ri->border_top() + ri->border_bottom()};
        return b;
    }
    JSValue rect(const litehtml::position &p) {
        auto out = JS_NewObject(context);
        for (auto pair : {std::pair<const char *, float>{"x", float(p.x)},
                          {"y", float(p.y)},
                          {"left", float(p.x)},
                          {"top", float(p.y)},
                          {"right", float(p.right())},
                          {"bottom", float(p.bottom())},
                          {"width", float(p.width)},
                          {"height", float(p.height)}})
            JS_SetPropertyStr(context, out, pair.first, JS_NewFloat64(context, pair.second));
        return out;
    }
    bool allowed(const std::string &directive, const std::string &url, bool inline_code,
                 const std::string &keyword = "'unsafe-inline'",
                 const std::string &nonce = "") const {
        if (policy_overflow)
            return false;
        const bool socket = directive == "connect-src" && url.rfind("wss://", 0) == 0;
        const auto checked_url = socket ? "https:" + url.substr(4) : url;
        if (!inline_code && (url.empty() || Resources::resolve("", checked_url).empty()) &&
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
            const bool script = directive == "script-src" || directive == "script-src-attr";
            const bool evaluation = keyword == "'unsafe-eval'";
            bool nonce_source = false, nonce_match = false, hashed = false, strict = false;
            for (const auto &token : it->second) {
                auto normalized = lower(token);
                if (normalized.rfind("'nonce-", 0) == 0 && token.size() > 8 &&
                    token.back() == '\'') {
                    auto value = token.substr(7, token.size() - 8);
                    bool valid = value.size() <= 256;
                    size_t padding = value.find('=');
                    if (padding == std::string::npos)
                        padding = value.size();
                    if (!padding || value.size() - padding > 2)
                        valid = false;
                    for (size_t i = 0; i < value.size(); ++i) {
                        auto c = static_cast<unsigned char>(value[i]);
                        if (i >= padding ? c != '='
                                         : !((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                             (c >= '0' && c <= '9') || c == '+' || c == '/' ||
                                             c == '-' || c == '_'))
                            valid = false;
                    }
                    nonce_source |= valid;
                    nonce_match |= valid && !nonce.empty() && nonce == value;
                }
                hashed |= normalized.rfind("'sha256-", 0) == 0 ||
                          normalized.rfind("'sha384-", 0) == 0 ||
                          normalized.rfind("'sha512-", 0) == 0;
                strict |= normalized == "'strict-dynamic'";
            }
            // Nonces do not authorize event attributes or eval. Hash execution and
            // strict-dynamic trust propagation remain unsupported and fail closed.
            bool yes = script && directive == "script-src" && !evaluation && nonce_match;
            for (auto &token : it->second) {
                if (script && !evaluation && strict)
                    continue;
                if (inline_code) {
                    if (token == keyword && (evaluation || !script || !(nonce_source || hashed)))
                        yes = true;
                } else if (token == "*" || token == "https:" || (socket && token == "wss:") ||
                           (token == "'self'" &&
                            origin(checked_url) == origin(container.page_url)) ||
                           (socket &&
                            (token == origin(checked_url) || token == origin(checked_url) + "/")) ||
                           token ==
                               (socket ? "wss:" + origin(checked_url).substr(6) : origin(url)) ||
                           token ==
                               (socket ? "wss:" + origin(checked_url).substr(6) : origin(url)) +
                                   "/")
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
    bool observing = false;
    using Element = litehtml::element::ptr;
    std::vector<Element> dom_children(const Element &el) const {
        std::vector<Element> out;
        for (auto &child : el->children())
            if (!generated(child))
                out.push_back(child);
        return out;
    }
    std::pair<Element, Element> siblings(const Element &el) const {
        auto parent = el->parent();
        if (!parent)
            return {};
        auto list = dom_children(parent);
        auto it = std::find(list.begin(), list.end(), el);
        return {it != list.begin() && it != list.end() ? *std::prev(it) : Element{},
                it != list.end() && std::next(it) != list.end() ? *std::next(it) : Element{}};
    }
    JSValue node_ids(const std::vector<Element> &list) {
        auto out = JS_NewArray(context);
        if (JS_IsException(out))
            throw std::runtime_error("Mutation allocation failed");
        try {
            for (uint32_t i = 0; i < list.size(); ++i)
                if (JS_SetPropertyUint32(context, out, i,
                                         JS_NewInt32(context, int(handle(list[i])))) < 0)
                    throw std::runtime_error("Mutation allocation failed");
        } catch (...) {
            JS_FreeValue(context, out);
            throw;
        }
        return out;
    }
    void mutation(const char *type, const Element &target, const std::vector<Element> &added = {},
                  const std::vector<Element> &removed = {}, const Element &previous = {},
                  const Element &next = {}, const char *attribute = nullptr,
                  const std::string *old = nullptr) {
        if (!observing)
            return;
        std::vector<Element> chain;
        bool document_ancestor = false;
        for (auto node = target; node; node = node->parent()) {
            if (chain.size() >= 256)
                throw std::runtime_error("DOM depth limit");
            chain.push_back(node);
            if (node == doc->root()) {
                document_ancestor = true;
                break;
            }
        }
        std::vector<JSValue> args;
        args.reserve(10);
        try {
            args.push_back(JS_NewString(context, type));
            args.push_back(JS_NewInt32(context, int(handle(target))));
            args.push_back(node_ids(added));
            args.push_back(node_ids(removed));
            args.push_back(JS_NewInt32(context, previous ? int(handle(previous)) : -1));
            args.push_back(JS_NewInt32(context, next ? int(handle(next)) : -1));
            args.push_back(attribute ? JS_NewString(context, attribute) : JS_NULL);
            args.push_back(old ? JS_NewStringLen(context, old->data(), old->size()) : JS_NULL);
            args.push_back(node_ids(chain));
            if (document_ancestor &&
                JS_SetPropertyUint32(context, args.back(), uint32_t(chain.size()),
                                     JS_NewInt32(context, 0)) < 0)
                throw std::runtime_error("Mutation allocation failed");
            auto result = call("mutated", std::move(args));
            JS_FreeValue(context, result);
        } catch (...) {
            for (auto arg : args)
                JS_FreeValue(context, arg);
            throw;
        }
    }
    void detach(const Element &child) {
        if (auto parent = child->parent()) {
            auto near = siblings(child);
            parent->removeChild(child);
            mutation("childList", parent, {}, {child}, near.first, near.second);
        }
    }
    bool is_fragment(const Element &el) const {
        return std::string(el->get_tagName()) == "__fragment";
    }
    void batch_children(const Element &parent, const std::vector<Element> &input,
                        Element before = {}, bool replace = false) {
        if (parent->is_text())
            throw std::runtime_error("Invalid parent");
        if (before && before->parent() != parent)
            throw std::runtime_error("Reference is not a child");
        std::vector<Element> added, fragments;
        auto add_unique = [&](const Element &node) {
            added.erase(std::remove(added.begin(), added.end(), node), added.end());
            added.push_back(node);
        };
        for (auto &node : input) {
            if (node == doc->root())
                throw std::runtime_error("Cannot move root");
            unsigned depth = 0;
            for (auto p = parent; p; p = p->parent()) {
                if (++depth > 256)
                    throw std::runtime_error("DOM depth limit");
                if (p == node)
                    throw std::runtime_error("DOM cycle");
            }
            if (is_fragment(node)) {
                fragments.push_back(node);
                for (auto &child : dom_children(node))
                    add_unique(child);
            } else
                add_unique(node);
        }
        for (auto &node : added) {
            if (node == parent)
                throw std::runtime_error("DOM cycle");
            for (auto p = parent; p; p = p->parent())
                if (p == node)
                    throw std::runtime_error("DOM cycle");
        }
        while (before && std::find(added.begin(), added.end(), before) != added.end())
            before = siblings(before).second;
        quota(0);
        for (auto &fragment : fragments) {
            auto removed = dom_children(fragment);
            for (auto &node : removed)
                fragment->removeChild(node);
            if (!removed.empty())
                mutation("childList", fragment, {}, removed);
        }
        for (auto &node : added)
            detach(node);
        auto removed = replace ? dom_children(parent) : std::vector<Element>{};
        if (replace) {
            for (auto &node : removed)
                parent->removeChild(node);
            before.reset();
        }
        auto remaining = dom_children(parent);
        Element previous = before              ? siblings(before).first
                           : remaining.empty() ? Element{}
                                               : remaining.back();
        auto tag = std::dynamic_pointer_cast<litehtml::html_tag>(parent);
        if (!tag)
            throw std::runtime_error("Invalid parent");
        for (auto &node : added) {
            parent->appendChild(node);
            if (before) {
                auto &list = tag->children();
                list.splice(std::find(list.begin(), list.end(), before), list,
                            std::find(list.begin(), list.end(), node));
            }
        }
        if (!added.empty() || !removed.empty())
            mutation("childList", parent, added, removed, previous, before);
        for (auto &node : added)
            activate_scripts(node);
        if (std::string(parent->get_tagName()) == "script")
            activate_scripts(parent);
    }
    std::set<std::string> seen_meta;
    bool policy_overflow = false;
    unsigned scripts_prepared = 0;
    void absorb_meta() {
        for (auto &el : doc->root()->select_all("meta")) {
            if (lower(el->get_attr("http-equiv", "")) != "content-security-policy")
                continue;
            std::string policy = el->get_attr("content", "");
            if (!seen_meta.insert(policy).second)
                continue;
            if (policies.size() >= 16) {
                policy_overflow = true;
                break;
            }
            policies.push_back(policy);
        }
        if (!eval_blocked && !allowed("script-src", "", true, "'unsafe-eval'")) {
            eval_blocked = true;
            auto result = call("blockEval", {});
            JS_FreeValue(context, result);
            eval_blocked = true;
        }
    }
    bool connected_script(const Element &el) const {
        for (auto n = el; n; n = n->parent()) {
            if (std::string(n->get_tagName()) == "template" ||
                std::string(n->get_tagName()) == "noscript")
                return false;
            if (n == doc->root())
                return true;
        }
        return false;
    }
    void script_event(const Element &el, const char *type) {
        if (pending_script_events.size() < 64)
            pending_script_events.push_back({el, type});
        else
            issue_log.add("limit", "script-events", origin(container.page_url));
    }
    void prepare_script(const Element &el, bool dynamic) {
        auto &state = script_states[el.get()];
        if (state.started || !connected_script(el))
            return;
        std::string type = lower(el->get_attr("type", ""));
        auto first = type.find_first_not_of(" \t\r\n\f"), last = type.find_last_not_of(" \t\r\n\f");
        type = first == std::string::npos ? "" : type.substr(first, last - first + 1);
        if (!type.empty() && type != "text/javascript" && type != "application/javascript" &&
            type != "module")
            return;
        const char *src_attr = el->get_attr("src");
        bool has_src = src_attr != nullptr;
        std::string src = src_attr ? src_attr : "";
        bool force_async = state.force_async;
        std::string source;
        el->get_text(source);
        if (!has_src && source.empty())
            return;
        state.started = true;
        if (scripts_prepared >= 64 || (!dynamic && jobs.size() >= 16)) {
            last_error = "Script count quota exceeded";
            issue_log.add("limit", "script-count", origin(container.page_url));
            return;
        }
        ++scripts_prepared;
        auto fail = [&](const char *category, const char *code) {
            issue_log.add(category, code, origin(container.page_url));
            script_event(el, "error");
        };
        absorb_meta();
        if (policy_overflow) {
            fail("policy", "script-policy-limit");
            return;
        }
        if (type != "module" && el->get_attr("nomodule"))
            return;
        if (type == "module") {
            try {
                Job job;
                job.element = el;
                job.dynamic = dynamic;
                job.external = has_src;
                job.mode = (el->get_attr("async") || (dynamic && force_async)) ? Job::asynchronous
                           : dynamic ? Job::dynamic_ordered
                                     : Job::deferred;
                const std::string credentials =
                    lower(el->get_attr("crossorigin", "")) == "use-credentials" ? "include"
                                                                                : "same-origin";
                if (*el->get_attr("integrity", "")) {
                    fail("unsupported", "module-integrity");
                    return;
                }
                if (has_src) {
                    if (src.empty())
                        throw std::runtime_error("Empty module src");
                    job.module = module_url(container.base, src, false);
                    module_fetch(job.module, credentials);
                } else {
                    if (!allowed("script-src", "", true) || modules.size() >= 64)
                        throw std::runtime_error("Inline module policy/quota");
                    job.module = "@inline:" + std::to_string(++inline_modules);
                    ModuleRecord record;
                    record.key = job.module;
                    record.base = record.meta_url = container.base;
                    record.source = std::move(source);
                    record.inline_module = true;
                    record.credentials = credentials;
                    modules.emplace(job.module, std::move(record));
                }
                jobs.push_back(std::move(job));
            } catch (const std::exception &) {
                fail("module", "prepare");
                last_error = "Module preparation failed";
            }
            return;
        }
        Job entry;
        entry.element = el;
        if (auto tag = std::dynamic_pointer_cast<litehtml::html_tag>(el)) {
            auto nonce = tag->native_attributes().find("nonce");
            if (nonce != tag->native_attributes().end() && nonce->second.size() <= 256)
                entry.nonce = nonce->second;
        }
        entry.dynamic = dynamic;
        if (has_src) {
            if (src.empty()) {
                fail("script-load", "empty-src");
                return;
            }
            entry.url = Resources::resolve(container.base, src);
            if (!allowed("script-src", entry.url, false, "'unsafe-inline'", entry.nonce)) {
                fail("policy", "external-script");
                return;
            }
            if (*el->get_attr("integrity", "")) {
                fail("unsupported", "script-integrity");
                return;
            }
            if (el->get_attr("crossorigin") && origin(entry.url) != origin(container.page_url)) {
                fail("unsupported", "script-cors");
                return;
            }
            entry.external = true;
            entry.mode = dynamic ? (force_async || el->get_attr("async") ? Job::asynchronous
                                                                         : Job::dynamic_ordered)
                         : el->get_attr("async") ? Job::asynchronous
                         : el->get_attr("defer") ? Job::deferred
                                                 : Job::ordered;
            entry.resource = resources.request(entry.url, 128u * 1024u);
            if (!entry.resource) {
                fail("limit", "script-resource");
                return;
            }
            entry.resource->critical = true;
        } else {
            if (source.size() > 128u * 1024u) {
                fail("limit", "script-bytes");
                return;
            }
            if (!allowed("script-src", "", true, "'unsafe-inline'", entry.nonce)) {
                fail("policy", "inline-script");
                return;
            }
            entry.url = container.base;
            entry.source = std::move(source);
        }
        jobs.push_back(std::move(entry));
        if (dynamic && !jobs.back().external)
            execute(jobs.back(), true);
    }
    void activate_scripts(const Element &root) {
        if (!root)
            return;
        auto snapshot = descendants(root);
        for (auto &el : snapshot)
            if (std::string(el->get_tagName()) == "meta" && connected_script(el)) {
                absorb_meta();
                break;
            }
        for (auto &el : snapshot)
            if (std::string(el->get_tagName()) == "script")
                prepare_script(el, true);
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
            out += ' ' + a.first + "=\"" + escape(a.first == "nonce" ? "" : a.second, true) + "\"";
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
        if (op == "microtask") {
            if (argc < 3 || !JS_IsFunction(context, a[2]))
                return JS_ThrowTypeError(context, "Microtask requires function");
            if (JS_EnqueueJob(context, microtask_job, 1, a + 2) < 0)
                return JS_EXCEPTION;
            return JS_UNDEFINED;
        }
        if (op == "observerActive") {
            observing = argc > 2 && JS_ToBool(context, a[2]) > 0;
            return JS_UNDEFINED;
        }
        if (op == "layoutObserverLimit") {
            auto code = str(2, 32);
            issue_log.add("limit",
                          code == "resize-loop"            ? "resize-observer-loop"
                          : code == "intersection-records" ? "intersection-records"
                                                           : "layout-observer-targets",
                          origin(container.page_url));
            return JS_UNDEFINED;
        }
        if (op == "observerLimit") {
            issue_log.add("limit", "mutation-observer", origin(container.page_url));
            return JS_UNDEFINED;
        }
        if (op == "currentScript")
            return current_script ? JS_NewInt32(context, int(handle(current_script))) : JS_NULL;
        if (op == "now")
            return JS_NewFloat64(context, double(now - origin_time));
        if (op == "scroll") {
            flush_layout();
            double x = 0, y = 0;
            if (argc > 2)
                JS_ToFloat64(context, &x, a[2]);
            if (argc > 3)
                JS_ToFloat64(context, &y, a[3]);
            if (!std::isfinite(x))
                x = 0;
            if (!std::isfinite(y))
                y = 0;
            container.scroll_x = float(std::clamp(
                x, 0.0, std::max(0.0, double(float(doc->width())) - container.viewport_width)));
            container.scroll_y = float(std::clamp(
                y, 0.0, std::max(0.0, double(float(doc->height())) - container.viewport_height)));
            container.scroll_requested = true;
            return JS_UNDEFINED;
        }
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
            issue_log.add("callback", "Error", origin(container.page_url));
            return JS_UNDEFINED;
        }
        if (op == "resolve")
            return js(Resources::resolve(argc > 3 && !str(3).empty() ? str(3) : container.base,
                                         str(2, 2048)));
        if (op == "reload") {
            next_url.clear();
            next_form = {};
            next_reload = true;
            return JS_UNDEFINED;
        }
        if (op == "navigate") {
            auto url = Resources::resolve(container.base, str(2, 2048));
            if (url.empty())
                throw std::runtime_error("Navigation requires HTTPS");
            next_form = {};
            next_reload = false;
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
        if (op == "wsURL") {
            return js(WebSockets::normalize(container.base, str(2, 2048)).url);
        }
        if (op == "wsCreate") {
            auto url = WebSockets::normalize(container.base, str(2, 2048));
            auto list = str(3, 4096);
            std::vector<std::string> protocols;
            size_t start = 0;
            while (start < list.size()) {
                auto end = list.find('\n', start);
                auto item = list.substr(start, end == std::string::npos ? end : end - start);
                if (item.empty() || item.size() > 128 || protocols.size() >= 16 ||
                    std::find(protocols.begin(), protocols.end(), item) != protocols.end())
                    throw std::runtime_error("Invalid WebSocket protocols");
                for (unsigned char c : item)
                    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9')) &&
                        std::string("!#$%&'*+-.^_`|~").find(char(c)) == std::string::npos)
                        throw std::runtime_error("Invalid WebSocket protocol");
                protocols.push_back(item);
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
            auto id = sockets.create(url, container.page_url, protocols,
                                     allowed("connect-src", url.url, false), now);
            if (!id)
                throw std::runtime_error("WebSocket quota exceeded");
            return JS_NewUint32(context, id);
        }
        if (op == "wsBuffered")
            return JS_NewFloat64(context, double(sockets.buffered(unsigned(integer(2)))));
        if (op == "wsSend" || op == "wsClose") {
            size_t size = 0;
            auto *bytes = argc > 4 ? JS_GetArrayBuffer(context, &size, a[4]) : nullptr;
            if (!bytes || size > (op == "wsClose" ? 123u : ws::message_limit))
                throw std::runtime_error("Invalid WebSocket buffer");
            auto id = unsigned(integer(2));
            if (op == "wsSend")
                return JS_NewBool(context, sockets.send(id, integer(3) != 0, bytes, size));
            auto code = unsigned(integer(3));
            if ((code && code != 1000 && (code < 3000 || code > 4999)) || (!code && size) ||
                !ws::valid_utf8(bytes, size))
                throw std::runtime_error("Invalid WebSocket close");
            sockets.close(id, code, bytes, size, argc > 5 && JS_ToBool(context, a[5]) > 0, now);
            return JS_UNDEFINED;
        }
        if (op == "encode") {
            auto text = str(2, 512u * 1024u);
            return JS_NewArrayBufferCopy(context, reinterpret_cast<const uint8_t *>(text.data()),
                                         text.size());
        }
        if (op == "decode") {
            size_t size = 0;
            auto *bytes = argc > 2 ? JS_GetArrayBuffer(context, &size, a[2]) : nullptr;
            if (!bytes || size > 512u * 1024u)
                throw std::runtime_error("Invalid byte buffer");
            auto text = decode_utf8(bytes, size);
            return JS_NewStringLen(context, text.data(), text.size());
        }
        if (op == "cancelFetch" || op == "fetchTimeout") {
            for (auto it = fetches.begin(); it != fetches.end(); ++it)
                if (it->id == integer(2)) {
                    if (op == "cancelFetch") {
                        resources.cancel(*it->resource);
                        fetches.erase(it);
                    } else
                        resources.timeout(*it->resource,
                                          unsigned(std::clamp(integer(3), 0, 60000)));
                    break;
                }
            return JS_UNDEFINED;
        }
        if (op == "fetch") {
            if (fetches.size() >= 32 || argc < 4)
                throw std::runtime_error("Fetch quota exceeded");
            auto field = [&](const char *key, size_t limit = 2048) {
                auto value = JS_GetPropertyStr(context, a[3], key);
                std::string result;
                try {
                    result = string(context, value, limit);
                } catch (...) {
                    JS_FreeValue(context, value);
                    throw;
                }
                JS_FreeValue(context, value);
                return result;
            };
            auto url = Resources::resolve(container.base, field("url"));
            if (!allowed("connect-src", url, false))
                throw std::runtime_error("Fetch CSP policy rejected request");
            WebRequest options;
            options.source = container.page_url;
            options.mode = field("mode", 32);
            options.credentials = field("credentials", 32);
            options.redirect = field("redirect", 32);
            auto timeout_value = JS_GetPropertyStr(context, a[3], "timeout");
            uint32_t timeout_ms = 0;
            JS_ToUint32(context, &timeout_ms, timeout_value);
            JS_FreeValue(context, timeout_value);
            options.timeout_ms = std::min(60000u, timeout_ms);
            auto headers = JS_GetPropertyStr(context, a[3], "headers"),
                 length = JS_GetPropertyStr(context, headers, "length");
            uint32_t count = 0;
            JS_ToUint32(context, &count, length);
            JS_FreeValue(context, length);
            if (count > 32) {
                JS_FreeValue(context, headers);
                throw std::runtime_error("Header quota exceeded");
            }
            for (uint32_t i = 0; i < count; i++) {
                auto pair = JS_GetPropertyUint32(context, headers, i),
                     key = JS_GetPropertyUint32(context, pair, 0),
                     value = JS_GetPropertyUint32(context, pair, 1);
                try {
                    options.headers.emplace_back(string(context, key, 128),
                                                 string(context, value, 8192));
                } catch (...) {
                    JS_FreeValue(context, key);
                    JS_FreeValue(context, value);
                    JS_FreeValue(context, pair);
                    JS_FreeValue(context, headers);
                    throw;
                }
                JS_FreeValue(context, key);
                JS_FreeValue(context, value);
                JS_FreeValue(context, pair);
            }
            JS_FreeValue(context, headers);
            auto value = JS_GetPropertyStr(context, a[3], "body");
            size_t size = 0;
            auto *bytes = JS_GetArrayBuffer(context, &size, value);
            if (!bytes || size > 65536) {
                JS_FreeValue(context, value);
                throw std::runtime_error("Request body quota exceeded");
            }
            std::string body(reinterpret_cast<const char *>(bytes), size);
            JS_FreeValue(context, value);
            auto *r =
                resources.network(url, field("method", 16), std::move(body), std::move(options));
            if (!r)
                throw std::runtime_error("Request policy or resource quota rejected request");
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
        if (op == "createFragment") {
            if (++created > 4096)
                throw std::runtime_error("Dynamic DOM node quota exceeded");
            return JS_NewInt32(context, int(handle(doc->create_element("__fragment", {}))));
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
            if (std::string(el->get_tagName()) == "script")
                script_states[el.get()] = {};
            return JS_NewInt32(context, int(handle(el)));
        }
        auto el = node(id);
        if (op == "scriptAsync")
            return JS_NewBool(context,
                              script_states[el.get()].force_async || el->get_attr("async"));
        if (op == "scriptForce") {
            script_states[el.get()].force_async = false;
            return JS_UNDEFINED;
        }
        if (op == "activeElement") {
            if (container.prune_focus())
                quota(0);
            auto focused = container.focused();
            return focused ? JS_NewInt32(context, int(handle(focused))) : JS_NULL;
        }
        if (op == "focus") {
            if (container.focus(id ? el : litehtml::element::ptr{}))
                quota(0);
            return JS_UNDEFINED;
        }
        if (op == "root")
            return JS_NewInt32(context, int(handle(doc->root())));
        if (op == "kind")
            return JS_NewInt32(context, el->is_text() ? 3 : is_fragment(el) ? 11 : 1);
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
            if (!el->is_text() && !text.empty() && created >= 4096)
                throw std::runtime_error("Dynamic DOM node quota exceeded");
            quota(text.size());
            if (auto leaf = std::dynamic_pointer_cast<litehtml::el_text>(el)) {
                std::string old;
                leaf->get_text(old);
                leaf->set_native_text(text);
                mutation("characterData", el, {}, {}, {}, {}, nullptr, &old);
            } else {
                auto children = dom_children(el);
                for (auto &child : children)
                    el->removeChild(child);
                std::vector<Element> added;
                if (!text.empty()) {
                    auto leaf = std::make_shared<litehtml::el_text>(text.c_str(), doc);
                    el->appendChild(leaf);
                    added.push_back(leaf);
                    ++created;
                }
                if (!children.empty() || !added.empty())
                    mutation("childList", el, added, children);
            }
            activate_scripts(el->is_text() ? el->parent() : el);
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
            if (std::string(el->get_tagName()) == "script") {
                auto old = dom_children(el);
                if (!source.empty() && created >= 4096)
                    throw std::runtime_error("Dynamic DOM node quota exceeded");
                for (auto &child : old)
                    el->removeChild(child);
                std::vector<Element> added;
                if (!source.empty()) {
                    auto leaf = std::make_shared<litehtml::el_text>(source.c_str(), doc);
                    el->appendChild(leaf);
                    added.push_back(leaf);
                    ++created;
                }
                if (!old.empty() || !added.empty())
                    mutation("childList", el, added, old);
                activate_scripts(el);
                return JS_UNDEFINED;
            }
            auto children = dom_children(el);
            for (auto &child : children)
                el->removeChild(child);
            doc->append_children_from_string(*el, source.c_str(), false, true);
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
            auto added = dom_children(el);
            for (auto &child : added)
                for (auto &node : descendants(child))
                    if (std::string(node->get_tagName()) == "script")
                        script_states[node.get()] = {true, false};
            if (!children.empty() || !added.empty())
                mutation("childList", el, added, children);
            activate_scripts(el); // Existing empty connected script receiving new text.
            return JS_UNDEFINED;
        }
        if (op == "nonce") {
            auto tag = std::dynamic_pointer_cast<litehtml::html_tag>(el);
            if (!tag)
                return js("");
            auto found = tag->native_attributes().find("nonce");
            return js(found == tag->native_attributes().end() ? "" : found->second);
        }
        if (op == "attr") {
            auto key = lower(str(2, 96));
            if (argc < 4) {
                auto *v = el->get_attr(key.c_str());
                return v ? js(v) : JS_NULL;
            }
            auto value = JS_IsNull(a[3]) ? "" : str(3, 2048);
            const char *old_ptr = el->get_attr(key.c_str());
            bool had_old = old_ptr != nullptr;
            std::string old = old_ptr ? old_ptr : "";
            if (!had_old && JS_IsNull(a[3]))
                return JS_UNDEFINED;
            quota(value.size());
            if (JS_IsNull(a[3]))
                el->remove_attr(key.c_str());
            else
                el->set_attr(key.c_str(), value.c_str());
            if (auto control = std::dynamic_pointer_cast<Control>(el))
                control->parse_attributes();
            else if (std::string(el->get_tagName()) == "option") {
                auto parent = el->parent();
                while (parent && std::string(parent->get_tagName()) != "select")
                    parent = parent->parent();
                if (auto control = std::dynamic_pointer_cast<Control>(parent))
                    control->parse_attributes();
            }
            mutation("attributes", el, {}, {}, {}, {}, key.c_str(), had_old ? &old : nullptr);
            if (std::string(el->get_tagName()) == "meta" && connected_script(el))
                absorb_meta();
            if (std::string(el->get_tagName()) == "script") {
                if (key == "async" && !JS_IsNull(a[3]))
                    script_states[el.get()].force_async = false;
                if (key == "src" && !JS_IsNull(a[3]))
                    activate_scripts(el);
            }
            return JS_UNDEFINED;
        }
        if (op == "formOwner") {
            auto form = form_owner(el);
            return form ? JS_NewInt32(context, int(handle(form))) : JS_NULL;
        }
        if (op == "type") {
            auto control = std::dynamic_pointer_cast<Control>(el);
            if (control) {
                auto t = control->type();
                return js(t == "select" ? (control->multiple() ? "select-multiple" : "select-one")
                                        : t);
            }
            return js(
                el->get_attr("type", std::string(el->get_tagName()) == "button" ? "submit" : ""));
        }
        if (op == "checked") {
            auto control = std::dynamic_pointer_cast<Control>(el);
            if (!control || (control->type() != "checkbox" && control->type() != "radio"))
                return JS_NewBool(context, false);
            if (argc > 2) {
                quota(0);
                control->checked(JS_ToBool(context, a[2]) > 0);
            }
            return JS_NewBool(context, control->checked());
        }
        if (op == "selectedIndex" || op == "options") {
            auto control = std::dynamic_pointer_cast<Control>(el);
            if (!control || control->type() != "select")
                throw std::runtime_error("Expected select");
            if (op == "selectedIndex") {
                if (argc > 2) {
                    quota(0);
                    control->selected_index(integer(2));
                }
                return JS_NewInt32(context, control->selected_index());
            }
            auto out = JS_NewArray(context);
            uint32_t i = 0;
            for (auto &option : control->options())
                JS_SetPropertyUint32(context, out, i++, JS_NewInt32(context, int(handle(option))));
            return out;
        }
        if (op == "selected") {
            auto parent = el->parent();
            while (parent && std::string(parent->get_tagName()) != "select")
                parent = parent->parent();
            auto control = std::dynamic_pointer_cast<Control>(parent);
            if (!control)
                return JS_NewBool(context, el->get_attr("selected") != nullptr);
            if (argc > 2) {
                quota(0);
                control->select_option(el, JS_ToBool(context, a[2]) > 0);
            }
            auto selected = control->selected_options();
            return JS_NewBool(context,
                              std::find(selected.begin(), selected.end(), el) != selected.end());
        }
        if (op == "validateForm" || op == "submitForm" || op == "resetForm") {
            auto form = form_owner(el);
            if (!form)
                throw std::runtime_error("No form owner");
            if (op == "resetForm") {
                quota(0);
                reset_form(form);
                return JS_UNDEFINED;
            }
            std::string error;
            auto request = prepare_form(el, container.base, error, op == "validateForm");
            if (op == "validateForm") {
                if (!request && argc > 2 && JS_ToBool(context, a[2]))
                    container.form_notice = error;
                return JS_NewBool(context, bool(request));
            }
            if (!request)
                throw std::runtime_error(error);
            if (!form_action_allowed(form_policies(doc, policies), container.page_url, request.url))
                throw std::runtime_error("Form blocked by CSP");
            next_url.clear();
            next_reload = false;
            next_form = std::move(request);
            return JS_UNDEFINED;
        }
        if (op == "value") {
            auto c = std::dynamic_pointer_cast<Control>(el);
            if (argc < 3)
                return js(c                                            ? c->value()
                          : std::string(el->get_tagName()) == "option" ? option_value(el)
                                                                       : el->get_attr("value", ""));
            auto value = str(2, 4096);
            quota(value.size());
            if (c)
                c->value(value);
            else
                el->set_attr("value", value.c_str());
            return JS_UNDEFINED;
        }
        if (op == "insert" || op == "append" || op == "remove" || op == "batch") {
            if (!id || el->is_text())
                throw std::runtime_error("Invalid parent");
            if (op == "remove") {
                auto child = node(integer(2));
                if (child->parent() != el)
                    throw std::runtime_error("Node is not a child");
                quota(0);
                detach(child);
                return JS_UNDEFINED;
            }
            if (op == "batch") {
                uint32_t count = 0;
                auto length = JS_GetPropertyStr(context, a[2], "length");
                if (JS_ToUint32(context, &count, length) < 0) {
                    JS_FreeValue(context, length);
                    return JS_EXCEPTION;
                }
                JS_FreeValue(context, length);
                if (count > 4096)
                    throw std::runtime_error("DOM batch quota exceeded");
                std::vector<Element> input;
                for (uint32_t i = 0; i < count; ++i) {
                    auto value = JS_GetPropertyUint32(context, a[2], i);
                    int32_t key = 0;
                    int rc = JS_ToInt32(context, &key, value);
                    JS_FreeValue(context, value);
                    if (rc < 0)
                        return JS_EXCEPTION;
                    input.push_back(node(key));
                }
                int mode = integer(3);
                auto list = dom_children(el);
                batch_children(el, input, mode == 1 && !list.empty() ? list.front() : Element{},
                               mode == 2);
            } else {
                auto child = node(integer(2));
                Element before = op == "insert" && integer(3) >= 0 ? node(integer(3)) : Element{};
                batch_children(el, {child}, before);
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
            flush_layout();
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
        if (op == "rect" || op == "geometry" || op == "intersection") {
            flush_layout();
            auto b = box(el);
            if (op == "rect")
                return rect(b.border);
            auto out = JS_NewObject(context);
            JS_SetPropertyStr(context, out, "border", rect(b.border));
            JS_SetPropertyStr(context, out, "rendered", JS_NewBool(context, b.rendered));
            if (op == "geometry") {
                // Non-replaced inline elements do not generate ResizeObserver sizes.
                if (b.rendered && el->css().get_display() == litehtml::display_inline &&
                    std::string(el->get_tagName()) != "img") {
                    b.border = litehtml::position{};
                    b.content = litehtml::position{};
                    JS_SetPropertyStr(context, out, "border", rect(b.border));
                }
                JS_SetPropertyStr(context, out, "content", rect(b.content));
                JS_SetPropertyStr(context, out, "depth", JS_NewInt32(context, int(b.depth)));
            } else {
                auto root_id = integer(2);
                Element root = root_id >= 0 ? node(root_id) : Element{};
                litehtml::position bounds{0, 0, container.viewport_width,
                                          container.viewport_height};
                if (root) {
                    auto rb = box(root);
                    bounds = rb.rendered && root->css().get_overflow() > litehtml::overflow_visible
                                 ? rb.padding
                                 : rb.border;
                }
                auto clips = JS_NewArray(context);
                unsigned index = 0;
                bool descendant = !root;
                for (auto n = b.rendered ? el->parent() : Element{}; n; n = n->parent()) {
                    if (n == root) {
                        descendant = true;
                        break;
                    }
                    if (n->css().get_overflow() > litehtml::overflow_visible)
                        JS_SetPropertyUint32(context, clips, index++, rect(box(n).padding));
                }
                JS_SetPropertyStr(context, out, "descendant", JS_NewBool(context, descendant));
                JS_SetPropertyStr(context, out, "root", rect(bounds));
                JS_SetPropertyStr(context, out, "clips", clips);
            }
            return out;
        }
        throw std::runtime_error("Unsupported DOM operation");
    }
    bool event(const litehtml::element::ptr &el, const std::string &type, bool cancelable,
               const litehtml::element::ptr &submitter = {}) {
        if (!usable_runtime)
            return true;
        budget(50);
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
                           JS_NewString(context, type.c_str()), JS_NewBool(context, cancelable),
                           JS_NewInt32(context, submitter ? int(handle(submitter)) : -1)});
        bool ok = JS_IsException(result) ? false : JS_ToBool(context, result) > 0;
        JS_FreeValue(context, result);
        drain();
        return ok;
    }
    bool runnable(const Job &j) const {
        if (!j.module.empty())
            return module_graph(j.module) != 0;
        return !j.resource ||
               (j.resource->state != Resource::queued && j.resource->state != Resource::loading);
    }
    void execute(Job &j, bool nested = false) {
        j.done = true;
        if (!j.module.empty()) {
            budget(50);
            auto &m = modules.at(j.module);
            if (module_graph(j.module) < 0 && m.state != ModuleRecord::failed)
                module_fail(m, module_graph_error(m.key), "dependency");
            bool ok = module_evaluate(m);
            m.static_root = true;
            m.error_reported = false;
            drain();
            module_refresh(m);
            if (!ok && m.fetch_error)
                script_event(j.element, "error");
            else {
                if (m.state == ModuleRecord::failed && !m.fetch_error)
                    module_report(m);
                if (j.external)
                    script_event(j.element, "load");
            }
            return;
        }
        if (script_depth >= 8) {
            issue_log.add("limit", "script-depth", origin(container.page_url));
            return;
        }
        if (j.external) {
            absorb_meta();
            if (policy_overflow || j.resource->state != Resource::ready ||
                !allowed("script-src", j.resource->effective, false, "'unsafe-inline'", j.nonce)) {
                last_error = "Script request failed or was blocked";
                issue_log.add("script-load", "failed", origin(j.url));
                script_event(j.element, "error");
                return;
            }
            if (j.resource->data.size() > 128u * 1024u) {
                issue_log.add("limit", "script-bytes", origin(j.url));
                script_event(j.element, "error");
                return;
            }
            j.source.assign(j.resource->data.begin(), j.resource->data.end());
        } else {
            absorb_meta();
            if (!allowed("script-src", "", true, "'unsafe-inline'", j.nonce)) {
                issue_log.add("policy", "inline-script", origin(container.page_url));
                return;
            }
        }
        if (!nested)
            budget(50);
        auto previous_script = current_script;
        current_script = j.element;
        ++script_depth;
        evaluate(j.source.c_str(), j.source.size(), j.url);
        --script_depth;
        current_script = previous_script;
        if (!nested)
            drain();
        if (j.external)
            script_event(j.element, "load");
    }
    std::vector<std::pair<Element, std::string>> pending_script_events;
    bool settle_fetch() {
        for (auto it = fetches.begin(); it != fetches.end(); ++it) {
            auto *r = it->resource;
            if (r->state == Resource::checking) {
                resources.follow(*r, allowed("connect-src", r->pending_url, false));
                return true;
            }
            if (r->state == Resource::queued || r->state == Resource::loading)
                continue;
            bool ok = r->transfer_ok && r->data.size() <= 512u * 1024u &&
                      allowed("connect-src", r->effective, false);
            auto headers = JS_NewArray(context);
            uint32_t index = 0;
            if (ok)
                for (auto &header : resources.exposed_headers(*r)) {
                    auto pair = JS_NewArray(context);
                    JS_SetPropertyUint32(context, pair, 0,
                                         JS_NewString(context, header.first.c_str()));
                    JS_SetPropertyUint32(context, pair, 1,
                                         JS_NewString(context, header.second.c_str()));
                    JS_SetPropertyUint32(context, headers, index++, pair);
                }
            auto bytes = JS_NewArrayBufferCopy(context, ok ? r->data.data() : nullptr,
                                               ok ? r->data.size() : 0);
            int id = it->id;
            fetches.erase(it);
            // Remove the native entry before invoking XHR event handlers: they may
            // synchronously start/cancel other requests and grow the vector.
            auto error = ok                     ? std::string{}
                         : r->web_error.empty() ? std::string("TypeError")
                                                : r->web_error;
            auto status = int(r->http_status);
            auto url = r->effective;
            auto text = r->status_text;
            bool redirected = r->redirects > 0, cors = r->cors_tainted;
            resources.release_body(*r);
            auto result = call(
                "fetched", {JS_NewInt32(context, id), JS_NewInt32(context, ok ? status : 0),
                            JS_NewString(context, ok ? url.c_str() : ""), bytes, headers,
                            JS_NewString(context, text.c_str()), JS_NewBool(context, redirected),
                            JS_NewBool(context, cors), JS_NewString(context, error.c_str())});
            JS_FreeValue(context, result);
            return true;
        }
        return false;
    }
    bool settle_socket() {
        WebSockets::Event event;
        if (!sockets.pop(event))
            return false;
        if (!event.diagnostic.empty())
            issue_log.add("websocket", event.diagnostic, event.origin);
        auto payload =
            event.binary
                ? JS_NewArrayBufferCopy(context, event.data.data(), event.data.size())
                : JS_NewStringLen(context, reinterpret_cast<const char *>(event.data.data()),
                                  event.data.size());
        auto result =
            call("socket", {JS_NewUint32(context, event.id), JS_NewInt32(context, int(event.kind)),
                            payload, JS_NewBool(context, event.binary),
                            JS_NewStringLen(context, event.origin.data(), event.origin.size()),
                            JS_NewStringLen(context, event.protocol.data(), event.protocol.size()),
                            JS_NewUint32(context, event.code),
                            JS_NewStringLen(context, event.reason.data(), event.reason.size()),
                            JS_NewBool(context, event.clean),
                            JS_NewFloat64(context, double(event.buffered))});
        JS_FreeValue(context, result);
        return true;
    }
    bool poll(uint64_t time) {
        if (!usable_runtime)
            return dirty;
        if (container.prune_focus())
            dirty = true;
        now = std::max(time, origin_time);
        sockets.pump(now);
        budget(50);
        if (!drain())
            return dirty;
        if (report_rejection()) {
            drain();
            return dirty;
        }
        budget(50);
        if (module_static_errors()) {
            drain();
            return dirty;
        }
        if (!pending_script_events.empty()) {
            auto target = pending_script_events.front();
            pending_script_events.erase(pending_script_events.begin());
            event(target.first, target.second, false);
            return dirty;
        }
        budget(50);
        Job *ordered = nullptr, *deferred = nullptr, *async = nullptr, *dynamic_order = nullptr;
        bool all_done = true;
        for (auto &j : jobs) {
            if (j.done)
                continue;
            all_done = false;
            if (j.mode == Job::ordered && !ordered)
                ordered = &j;
            if (j.mode == Job::dynamic_ordered && !dynamic_order)
                dynamic_order = &j;
            if (j.mode == Job::deferred && !deferred)
                deferred = &j;
            if (j.mode == Job::asynchronous && !async && runnable(j))
                async = &j;
        }
        if (async) {
            execute(*async);
            return dirty;
        }
        if (ordered && runnable(*ordered)) {
            execute(*ordered);
            return dirty;
        }
        if (dynamic_order && runnable(*dynamic_order)) {
            execute(*dynamic_order);
            return dirty;
        }
        const char *phase = nullptr;
        if (!ordered && !interactive) {
            interactive = true;
            phase = "interactive";
        } else if (!ordered && deferred && runnable(*deferred)) {
            execute(*deferred);
            return dirty;
        } else if (!ordered && !deferred && !dom_ready) {
            dom_ready = true;
            phase = "domReady";
        } else if (dom_ready && all_done && !resources.pending_load() && !complete) {
            complete = true;
            phase = "complete";
        } else if (complete && !loaded && !resources.pending_load()) {
            loaded = true;
            phase = "loaded";
        }
        if (phase) {
            budget(50);
            auto result = call(phase, {});
            JS_FreeValue(context, result);
            drain();
            return dirty;
        }
        // Rotate independent task sources; checkpoint after every callback.
        uint64_t turn_end = Scripts::clock_ms() + 8;
        for (unsigned task = 0; task < 8 && Scripts::clock_ms() < turn_end; ++task) {
            budget(50);
            bool ran = false;
            for (unsigned attempt = 0; attempt < 4 && !ran; ++attempt) {
                auto source = task_source++ % 4;
                if (source == 0)
                    ran = settle_fetch();
                else if (source == 1)
                    ran = settle_socket();
                else if (source == 2)
                    ran = pump_modules();
                else {
                    auto result = call("task", {JS_NewFloat64(context, double(now - origin_time))});
                    ran = !JS_IsException(result) && JS_ToBool(context, result) > 0;
                    JS_FreeValue(context, result);
                }
            }
            if (!ran || !drain())
                break;
        }
        return dirty;
    }
    void render_frame(uint64_t time) {
        if (!usable_runtime || (previous_frame && time < previous_frame + 16))
            return;
        now = std::max(time, origin_time);
        budget(50);
        if (!drain())
            return;
        previous_frame = time;
        uint64_t frame_end = Scripts::clock_ms() + 8;
        auto resize = call("viewport", {});
        JS_FreeValue(context, resize);
        if (!drain())
            return;
        auto begin = call("beginFrame", {JS_NewFloat64(context, double(now - origin_time))});
        JS_FreeValue(context, begin);
        for (unsigned frame = 0; frame < 64 && Scripts::clock_ms() < frame_end; ++frame) {
            budget(50);
            auto result = call("frame", {});
            bool ran = !JS_IsException(result) && JS_ToBool(context, result) > 0;
            JS_FreeValue(context, result);
            if (!ran)
                break;
            if (!drain())
                return;
        }
        budget(50);
        auto observers = call("beginObservers", {});
        JS_FreeValue(context, observers);
        uint64_t observer_end = Scripts::clock_ms() + 8;
        for (unsigned callback = 0; callback < 64; ++callback) {
            budget(50);
            auto result = call("resize", {});
            bool ran = !JS_IsException(result) && JS_ToBool(context, result) > 0;
            JS_FreeValue(context, result);
            if (!ran)
                break;
            if (!drain())
                return;
            if (Scripts::clock_ms() >= observer_end)
                break;
        }
        budget(50);
        auto end = call("endObservers", {});
        JS_FreeValue(context, end);
        if (!drain())
            return;
        budget(50);
        auto intersections =
            call("intersections", {JS_NewFloat64(context, double(now - origin_time))});
        JS_FreeValue(context, intersections);
    }
};
Scripts::Scripts(const litehtml::document::ptr &d, Container &c, Resources &r,
                 std::vector<std::string> p)
    : impl(std::make_unique<Impl>(d, c, r, std::move(p))) {}
Scripts::~Scripts() = default;
bool Scripts::poll(uint64_t n) { return impl->poll(n); }
void Scripts::focus(const litehtml::element::ptr &el) {
    if (!impl->usable_runtime) {
        impl->container.focus(el);
        return;
    }
    impl->budget(50);
    auto result = impl->call("focus", {JS_NewInt32(impl->context, el ? int(impl->handle(el)) : 0)});
    JS_FreeValue(impl->context, result);
    impl->drain();
}
bool Scripts::dispatch(const litehtml::element::ptr &el, const std::string &type, bool cancelable,
                       const litehtml::element::ptr &submitter) {
    return impl->event(el, type, cancelable, submitter);
}
bool Scripts::eval(const std::string &source, unsigned ms) {
    impl->budget(ms);
    bool result = impl->evaluate(source.c_str(), source.size(), "host-probe");
    impl->drain();
    return result;
}
bool Scripts::changed() const { return impl->dirty; }
void Scripts::flush() { impl->flush_layout(); }
void Scripts::render_frame(uint64_t now) { impl->render_frame(now); }

std::string Scripts::navigation() {
    auto url = std::move(impl->next_url);
    impl->next_url.clear();
    return url;
}
Submission Scripts::submission() {
    auto request = std::move(impl->next_form);
    impl->next_form = {};
    return request;
}
bool Scripts::reload_requested() {
    bool result = impl->next_reload;
    impl->next_reload = false;
    return result;
}
const Issues &Scripts::issues() const { return impl->issue_log; }
std::string Scripts::error() const { return impl->last_error; }
bool Scripts::ready() const { return impl->loaded; }
} // namespace visual
