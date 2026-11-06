// Included inside Scripts::Impl. QuickJS owns parsing/linking/evaluation; the
// browser owns asynchronous source loading and the per-navigation module map.
struct ModuleRecord {
    enum State { fetching, compiled, evaluating, evaluated, failed } state = fetching;
    std::string key, base, meta_url, source, failure_kind, display_error,
        credentials = "same-origin";
    bool fetch_error = true;
    Resource *resource = nullptr;
    std::vector<std::string> dependencies;
    JSValue value = JS_UNDEFINED, promise = JS_UNDEFINED, error = JS_UNDEFINED;
    bool inline_module = false, static_root = false, error_reported = false, runtime_error = false;
};
struct ModuleImport {
    std::string key;
    JSValue resolve, reject;
};
std::map<std::string, ModuleRecord> modules;
std::deque<ModuleImport> module_imports;
size_t module_bytes = 0, module_edges = 0;
unsigned inline_modules = 0;

static JSModuleDef *module_def(const ModuleRecord &m) {
    return static_cast<JSModuleDef *>(JS_VALUE_GET_PTR(m.value));
}
JSValue module_type_error(const char *message) {
    JS_ThrowTypeError(context, "%s", message);
    return JS_GetException(context);
}
const ModuleRecord *module_first_failure(const std::string &key,
                                         std::set<std::string> &seen) const {
    if (!seen.insert(key).second)
        return nullptr;
    auto &m = modules.at(key);
    if (m.state == ModuleRecord::failed)
        return &m;
    for (auto &dep : m.dependencies)
        if (auto *error = module_first_failure(dep, seen))
            return error;
    return nullptr;
}
void module_fail(ModuleRecord &m, JSValue error, const char *code) {
    if (m.state == ModuleRecord::failed) {
        JS_FreeValue(context, error);
        return;
    }
    m.failure_kind = code;
    m.fetch_error = m.failure_kind != "parse" && m.failure_kind != "link" &&
                    m.failure_kind != "specifier" && m.failure_kind != "evaluation";
    if (m.failure_kind == "dependency") {
        std::set<std::string> seen;
        if (auto *cause = module_first_failure(m.key, seen)) {
            m.failure_kind = cause->failure_kind;
            m.fetch_error = cause->fetch_error;
        }
    }
    m.state = ModuleRecord::failed;
    m.error = error;
    std::string{}.swap(m.source);
    issue_log.add("module", code, origin(m.inline_module ? container.page_url : m.key));
    m.display_error = std::string("Module ") + code;
    const char *text = JS_ToCString(context, error);
    if (text) {
        m.display_error.assign(text, std::min(size_t(192), std::strlen(text)));
        JS_FreeCString(context, text);
    } else if (JS_HasException(context)) {
        auto ignored = JS_GetException(context);
        JS_FreeValue(context, ignored);
    }
    last_error = m.display_error;
    if (JS_IsObject(error)) {
        auto name = JS_GetPropertyStr(context, error, "name");
        const char *kind = JS_ToCString(context, name);
        if (kind) {
            for (auto known : {"TypeError", "ReferenceError", "SyntaxError", "RangeError",
                               "InternalError", "EvalError"})
                if (std::strcmp(kind, known) == 0)
                    issue_log.add("exception", known,
                                  origin(m.inline_module ? container.page_url : m.key));
            JS_FreeCString(context, kind);
        }
        JS_FreeValue(context, name);
        if (JS_HasException(context)) {
            auto ignored = JS_GetException(context);
            JS_FreeValue(context, ignored);
        }
    }
    if (m.resource)
        resources.release_body(*m.resource);
}
std::string module_url(const std::string &base, const std::string &specifier,
                       bool import_specifier = true) {
    if (specifier.empty() || specifier.size() > 2048 || specifier.find('\0') != std::string::npos)
        throw std::runtime_error("Invalid module URL");
    const auto low = lower(specifier);
    if (import_specifier && specifier.rfind("./", 0) != 0 && specifier.rfind("../", 0) != 0 &&
        specifier.front() != '/' && low.rfind("https://", 0) != 0 && low.rfind("demo:/", 0) != 0)
        throw std::runtime_error("Bare module specifiers require an import map");
    auto url = Resources::resolve(base, specifier);
    if (url.rfind("demo:/", 0) == 0) {
        std::string path = url.substr(6), suffix;
        auto end = path.find_first_of("?#");
        if (end != std::string::npos) {
            suffix = path.substr(end);
            path.resize(end);
        }
        std::vector<std::string> parts;
        std::istringstream stream(path);
        std::string part;
        while (std::getline(stream, part, '/')) {
            if (part == "..") {
                if (!parts.empty())
                    parts.pop_back();
            } else if (!part.empty() && part != ".")
                parts.push_back(part);
        }
        url = "demo:/";
        for (size_t i = 0; i < parts.size(); ++i) {
            if (i)
                url += '/';
            url += parts[i];
        }
        return url + suffix;
    }
    if (url.empty())
        throw std::runtime_error("Modules require HTTPS");
    CURLU *u = curl_url();
    char *host = nullptr, *text = nullptr;
    bool ok = u && curl_url_set(u, CURLUPART_URL, url.c_str(), 0) == CURLUE_OK;
    if (ok && curl_url_get(u, CURLUPART_HOST, &host, 0) == CURLUE_OK)
        curl_url_set(u, CURLUPART_HOST, lower(host).c_str(), 0);
    if (ok)
        ok = curl_url_get(u, CURLUPART_URL, &text, CURLU_NO_DEFAULT_PORT) == CURLUE_OK;
    std::string result = ok ? text : "";
    curl_free(host);
    curl_free(text);
    curl_url_cleanup(u);
    if (result.empty())
        throw std::runtime_error("Invalid module URL");
    return result;
}
std::string module_reference(const std::string &base, const std::string &name) {
    auto found = modules.find(base);
    return module_url(found == modules.end()
                          ? (base.rfind("https://", 0) == 0 || base.rfind("demo:/", 0) == 0
                                 ? base
                                 : container.base)
                          : found->second.base,
                      name);
}
ModuleRecord &module_fetch(const std::string &key, const std::string &credentials = "same-origin") {
    auto found = modules.find(key);
    if (found != modules.end())
        return found->second;
    if (modules.size() >= 64)
        throw std::runtime_error("Module map quota exceeded");
    ModuleRecord entry;
    entry.key = entry.base = entry.meta_url = key;
    entry.credentials = credentials;
    auto &m = modules.emplace(key, std::move(entry)).first->second;
    if (!allowed("script-src", key, false)) {
        module_fail(m, module_type_error("Module blocked by script policy"), "policy");
        return m;
    }
    WebRequest options;
    options.source = container.page_url;
    options.credentials = credentials;
    m.resource = resources.network(key, "GET", {}, std::move(options));
    if (!m.resource)
        module_fail(m, module_type_error("Module request rejected"), "request");
    else
        m.resource->limit = 128u * 1024u;
    return m;
}
static int module_attributes(JSContext *ctx, void *, JSValueConst attributes) {
    if (JS_IsUndefined(attributes))
        return 0;
    JSPropertyEnum *names = nullptr;
    uint32_t count = 0;
    if (JS_GetOwnPropertyNames(ctx, &names, &count, attributes,
                               JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) < 0)
        return -1;
    JS_FreePropertyEnum(ctx, names, count);
    if (count) {
        JS_ThrowTypeError(ctx, "Import attributes are not supported");
        return -1;
    }
    return 0;
}
static char *module_normalize(JSContext *ctx, const char *base, const char *name, void *opaque) {
    auto *self = static_cast<Impl *>(opaque);
    try {
        auto key = self->module_reference(base, name);
        if (!self->allowed("script-src", key, false)) {
            JS_ThrowTypeError(ctx, "Module policy rejected import");
            return nullptr;
        }
        auto found = self->modules.find(key);
        if (found != self->modules.end() && found->second.state == ModuleRecord::failed) {
            JS_Throw(ctx, JS_DupValue(ctx, found->second.error));
            return nullptr;
        }
        return js_strdup(ctx, key.c_str());
    } catch (const std::exception &) {
        JS_ThrowTypeError(ctx, "Unresolvable module specifier");
        return nullptr;
    }
}
static JSModuleDef *module_load(JSContext *ctx, const char *name, void *opaque, JSValueConst) {
    auto *self = static_cast<Impl *>(opaque);
    auto it = self->modules.find(name);
    if (it == self->modules.end() || it->second.state == ModuleRecord::fetching ||
        it->second.state == ModuleRecord::failed) {
        JS_ThrowTypeError(ctx, "Module graph is not ready");
        return nullptr;
    }
    return module_def(it->second);
}
static void module_dynamic(JSContext *ctx, const char *base, const char *name, JSValueConst resolve,
                           JSValueConst reject, JSValueConst attributes, void *opaque) {
    auto *self = static_cast<Impl *>(opaque);
    try {
        if (module_attributes(ctx, opaque, attributes) < 0)
            throw std::runtime_error("Import attributes rejected");
        if (self->module_imports.size() >= 32)
            throw std::runtime_error("Dynamic import quota exceeded");
        auto key = self->module_reference(base, name);
        if (!self->allowed("script-src", key, false))
            throw std::runtime_error("Module policy rejected import");
        auto parent = self->modules.find(base);
        self->module_fetch(key, parent == self->modules.end() ? "same-origin"
                                                              : parent->second.credentials);
        ModuleImport entry{key, JS_DupValue(ctx, resolve), JS_DupValue(ctx, reject)};
        try {
            self->module_imports.push_back(std::move(entry));
        } catch (...) {
            JS_FreeValue(ctx, entry.resolve);
            JS_FreeValue(ctx, entry.reject);
            throw;
        }
        return;
    } catch (const std::exception &) {
        JSValue error = JS_HasException(ctx)
                            ? JS_GetException(ctx)
                            : self->module_type_error("Dynamic import rejected by policy or quota");
        auto result = JS_Call(ctx, reject, JS_UNDEFINED, 1, &error);
        JS_FreeValue(ctx, result);
        JS_FreeValue(ctx, error);
    }
}
bool module_mime(const ModuleRecord &m) {
    if (m.key.rfind("demo:/", 0) == 0)
        return m.resource->effective.size() >= 3 &&
               m.resource->effective.compare(m.resource->effective.size() - 3, 3, ".js") == 0;
    auto it = m.resource->response_headers.find("content-type");
    if (it == m.resource->response_headers.end())
        return false;
    auto mime = lower(it->second.substr(0, it->second.find(';')));
    auto a = mime.find_first_not_of(" \t"), b = mime.find_last_not_of(" \t");
    if (a == std::string::npos)
        return false;
    mime = mime.substr(a, b - a + 1);
    return mime == "text/javascript" || mime == "application/javascript" ||
           mime == "text/ecmascript" || mime == "application/ecmascript" ||
           mime == "application/x-javascript" || mime == "application/x-ecmascript" ||
           mime == "text/jscript" || mime == "text/livescript" || mime == "text/x-javascript" ||
           mime == "text/x-ecmascript" ||
           (mime.size() == 18 && mime.rfind("text/javascript1.", 0) == 0 && mime.back() >= '0' &&
            mime.back() <= '5');
}
void module_compile(ModuleRecord &m) {
    if (m.source.size() > 128u * 1024u || m.source.size() > 4u * 1024u * 1024u - module_bytes) {
        module_fail(m, module_type_error("Module source quota exceeded"), "bytes");
        return;
    }
    module_bytes += m.source.size();
    auto source = decode_utf8(reinterpret_cast<const uint8_t *>(m.source.data()), m.source.size());
    std::string{}.swap(m.source);
    m.value = JS_Eval(context, source.c_str(), source.size(), m.key.c_str(),
                      JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY |
                          JS_EVAL_FLAG_DEFER_MODULE_RESOLUTION);
    if (JS_IsException(m.value)) {
        m.value = JS_UNDEFINED;
        module_fail(m, JS_GetException(context), "parse");
        return;
    }
    auto *def = module_def(m);
    auto meta = JS_GetImportMeta(context, def);
    if (JS_IsException(meta)) {
        module_fail(m, JS_GetException(context), "metadata");
        return;
    }
    int meta_result =
        JS_SetPropertyStr(context, meta, "url", JS_NewString(context, m.meta_url.c_str()));
    JS_FreeValue(context, meta);
    if (meta_result < 0) {
        module_fail(m, JS_GetException(context), "metadata");
        return;
    }
    m.state = ModuleRecord::compiled;
    int count = JS_GetModuleRequestCount(def);
    if (count < 0 || size_t(count) > 512 - module_edges) {
        module_fail(m, module_type_error("Module dependency quota exceeded"), "edges");
        return;
    }
    module_edges += count;
    try {
        for (int i = 0; i < count; i++) {
            auto atom = JS_GetModuleRequestName(context, def, i);
            const char *name = JS_AtomToCString(context, atom);
            JS_FreeAtom(context, atom);
            if (!name)
                throw std::runtime_error("Invalid module name");
            std::string specifier(name);
            JS_FreeCString(context, name);
            auto key = module_url(m.base, specifier);
            m.dependencies.push_back(key);
            module_fetch(key, m.credentials);
        }
    } catch (const std::exception &) {
        module_fail(m, module_type_error("Module dependency could not be resolved"), "specifier");
    }
}
// -1 failed, 0 waiting, 1 fetched/compiled. Cycles are linked by QuickJS.
int module_graph(const std::string &key, std::set<std::string> &seen, unsigned depth = 0) const {
    auto it = modules.find(key);
    if (it == modules.end() || depth > 64)
        return -1;
    auto &m = it->second;
    if (m.state == ModuleRecord::failed)
        return -1;
    if (m.state == ModuleRecord::fetching)
        return 0;
    if (!seen.insert(key).second)
        return 1;
    int state = 1;
    for (auto &dep : m.dependencies) {
        int next = module_graph(dep, seen, depth + 1);
        if (next < 0)
            return -1;
        if (!next)
            state = 0;
    }
    return state;
}
int module_graph(const std::string &key) const {
    std::set<std::string> seen;
    return module_graph(key, seen);
}
JSValue module_graph_error(const std::string &key) {
    std::set<std::string> seen;
    auto *failure = module_first_failure(key, seen);
    return failure ? JS_DupValue(context, failure->error)
                   : module_type_error("Module dependency failed");
}
bool module_policy(const std::string &key, std::set<std::string> &seen) {
    if (!seen.insert(key).second)
        return true;
    auto &m = modules.at(key);
    if (!allowed("script-src", m.inline_module ? "" : m.meta_url, m.inline_module))
        return false;
    for (auto &dep : m.dependencies)
        if (!module_policy(dep, seen))
            return false;
    return true;
}
bool module_evaluate(ModuleRecord &m) {
    if (m.state == ModuleRecord::evaluating || m.state == ModuleRecord::evaluated)
        return true;
    if (m.state == ModuleRecord::failed)
        return false;
    std::set<std::string> seen;
    if (!module_policy(m.key, seen)) {
        module_fail(m, module_type_error("Module execution blocked by script policy"), "policy");
        return false;
    }
    if (JS_ResolveModule(context, m.value) < 0) {
        module_fail(m, JS_GetException(context), "link");
        return false;
    }
    auto previous = current_script;
    current_script.reset();
    m.promise = JS_EvalFunction(context, JS_DupValue(context, m.value));
    current_script = previous;
    if (JS_IsException(m.promise)) {
        m.promise = JS_UNDEFINED;
        module_fail(m, JS_GetException(context), "link");
        return false;
    }
    m.state = ModuleRecord::evaluating;
    if (JS_MarkPromiseHandled(context, m.promise) < 0) {
        module_fail(m, module_type_error("Invalid module evaluation promise"), "evaluation");
        return false;
    }
    return true;
}
void module_refresh(ModuleRecord &m) {
    if (m.state != ModuleRecord::evaluating)
        return;
    auto state = JS_PromiseState(context, m.promise);
    if (state == JS_PROMISE_FULFILLED)
        m.state = ModuleRecord::evaluated;
    else if (state == JS_PROMISE_REJECTED) {
        m.runtime_error = true;
        module_fail(m, JS_PromiseResult(context, m.promise), "evaluation");
    }
}
void module_report(ModuleRecord &m) {
    m.error_reported = true;
    auto result = call("moduleError", {JS_DupValue(context, m.error),
                                       JS_NewString(context, m.display_error.c_str())});
    JS_FreeValue(context, result);
}
bool module_static_errors() {
    for (auto &item : modules) {
        auto &m = item.second;
        if (!m.static_root)
            continue;
        module_refresh(m);
        if (m.state == ModuleRecord::failed && !m.fetch_error && !m.error_reported) {
            module_report(m);
            return true;
        }
    }
    return false;
}
JSValue module_rejection(const ModuleRecord &m) {
    if (m.failure_kind != "link")
        return JS_DupValue(context, m.error);
    // Linking is retried conceptually for each import; browser SyntaxError
    // objects differ, while parse/evaluation failure objects are cached.
    auto value = JS_GetPropertyStr(context, m.error, "message");
    const char *message = JS_ToCString(context, value);
    JS_ThrowSyntaxError(context, "%s", message ? message : "Module linking failed");
    JS_FreeCString(context, message);
    JS_FreeValue(context, value);
    return JS_GetException(context);
}
bool pump_modules() {
    // One fetch/compile/completion per turn. No synchronous network inside QuickJS.
    for (auto &item : modules) {
        auto &m = item.second;
        if (m.state == ModuleRecord::fetching) {
            if (!m.inline_module && !m.resource) {
                module_fail(m, module_type_error("Module request was not created"), "request");
                return true;
            }
            if (m.resource) {
                auto &r = *m.resource;
                if (r.state == Resource::checking) {
                    resources.follow(r, allowed("script-src", r.pending_url, false));
                    return true;
                }
                if (r.state == Resource::queued || r.state == Resource::loading)
                    continue;
                if (r.state != Resource::ready || !r.transfer_ok ||
                    !allowed("script-src", r.effective, false) || !module_mime(m)) {
                    module_fail(m, module_type_error("Module HTTP/CORS/MIME/policy failure"),
                                "load");
                    return true;
                }
                m.source.assign(r.data.begin(), r.data.end());
                m.base = m.meta_url = r.effective;
                auto hash = m.key.find('#');
                if (!r.redirects && hash != std::string::npos)
                    m.meta_url += m.key.substr(hash);
                resources.release_body(r);
            }
            module_compile(m);
            return true;
        }
        if (m.state == ModuleRecord::evaluating) {
            auto state = JS_PromiseState(context, m.promise);
            if (state == JS_PROMISE_FULFILLED) {
                m.state = ModuleRecord::evaluated;
                return true;
            }
            if (state == JS_PROMISE_REJECTED) {
                m.runtime_error = true;
                module_fail(m, JS_PromiseResult(context, m.promise), "evaluation");
                return true;
            }
        }
        if (m.state == ModuleRecord::failed && m.runtime_error && m.static_root &&
            !m.error_reported) {
            m.error_reported = true;
            auto result = call("moduleError", {JS_DupValue(context, m.error),
                                               JS_NewString(context, m.display_error.c_str())});
            JS_FreeValue(context, result);
            return true;
        }
    }
    for (auto it = module_imports.begin(); it != module_imports.end(); ++it) {
        auto &m = modules.at(it->key);
        int graph = module_graph(it->key);
        if (!graph)
            continue;
        if (graph < 0 && m.state != ModuleRecord::failed)
            module_fail(m, module_graph_error(m.key), "dependency");
        if (m.state == ModuleRecord::compiled) {
            module_evaluate(m);
            return true;
        }
        if (m.state == ModuleRecord::evaluating)
            continue;
        auto entry = *it;
        module_imports.erase(it);
        JSValue value = m.state == ModuleRecord::failed
                            ? module_rejection(m)
                            : JS_GetModuleNamespace(context, module_def(m));
        bool failed = m.state == ModuleRecord::failed || JS_IsException(value);
        if (JS_IsException(value))
            value = JS_GetException(context);
        auto result =
            JS_Call(context, failed ? entry.reject : entry.resolve, JS_UNDEFINED, 1, &value);
        JS_FreeValue(context, result);
        JS_FreeValue(context, value);
        JS_FreeValue(context, entry.resolve);
        JS_FreeValue(context, entry.reject);
        return true;
    }
    return false;
}
void release_modules() {
    if (!context)
        return;
    for (auto &entry : module_imports) {
        JS_FreeValue(context, entry.resolve);
        JS_FreeValue(context, entry.reject);
    }
    module_imports.clear();
    for (auto &item : modules) {
        auto &m = item.second;
        if (m.resource)
            resources.cancel(*m.resource);
        JS_FreeValue(context, m.value);
        JS_FreeValue(context, m.promise);
        JS_FreeValue(context, m.error);
    }
    modules.clear();
}
