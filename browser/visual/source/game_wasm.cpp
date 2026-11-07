#include "game_wasm.h"
extern "C" {
#include "browser_port.h"
}
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>
namespace visual {
namespace {
struct Failure {
    unsigned kind;
    std::string message;
};
struct JSFailure {};
struct Val {
    JSContext *ctx;
    JSValue value;
    Val(JSContext *c, JSValue v) : ctx(c), value(v) {
        if (JS_IsException(value))
            throw JSFailure{};
    }
    ~Val() { JS_FreeValue(ctx, value); }
    Val(const Val &) = delete;
    Val &operator=(const Val &) = delete;
};
struct Engine {
    void *pool = nullptr;
    wasm_engine_t *engine = nullptr;
    Engine() {
        pool = std::malloc(64u * 1024u * 1024u);
        if (!pool)
            throw Failure{2, "Wasm runtime allocation failed"};
        MemAllocOption option{};
        option.pool.heap_buf = pool;
        option.pool.heap_size = 64u * 1024u * 1024u;
        engine = wasm_engine_new_with_args(Alloc_With_Pool, &option);
        if (!engine) {
            std::free(pool);
            pool = nullptr;
            throw Failure{2, "Wasm runtime initialization failed"};
        }
    }
    ~Engine() {
        if (engine)
            wasm_engine_delete(engine);
        std::free(pool);
    }
};
std::weak_ptr<Engine> shared_engine;
std::string name(const wasm_name_t *n) {
    return std::string(n->data, n->num_elems && n->data[n->num_elems - 1] == 0 ? n->num_elems - 1
                                                                               : n->num_elems);
}
const char *kind_name(wasm_externkind_t k) {
    switch (k) {
    case WASM_EXTERN_FUNC:
        return "function";
    case WASM_EXTERN_MEMORY:
        return "memory";
    case WASM_EXTERN_GLOBAL:
        return "global";
    case WASM_EXTERN_TABLE:
        return "table";
    default:
        return "unsupported";
    }
}
const char *value_name(wasm_valkind_t k) {
    switch (k) {
    case WASM_I32:
        return "i32";
    case WASM_I64:
        return "i64";
    case WASM_F32:
        return "f32";
    case WASM_F64:
        return "f64";
    case WASM_FUNCREF:
        return "funcref";
    default:
        return "externref";
    }
}
void leb(std::vector<uint8_t> &out, uint32_t v) {
    do {
        uint8_t b = v & 127;
        v >>= 7;
        out.push_back(b | (v ? 128 : 0));
    } while (v);
}
void section(std::vector<uint8_t> &out, unsigned id, const std::vector<uint8_t> &data) {
    out.push_back(id);
    leb(out, uint32_t(data.size()));
    out.insert(out.end(), data.begin(), data.end());
}
void string_bytes(std::vector<uint8_t> &out, const std::string &s) {
    leb(out, uint32_t(s.size()));
    out.insert(out.end(), s.begin(), s.end());
}
void signed_leb(std::vector<uint8_t> &out, int64_t v) {
    bool more;
    do {
        auto byte = uint8_t(v & 127);
        v >>= 7;
        more = !((v == 0 && !(byte & 64)) || (v == -1 && (byte & 64)));
        out.push_back(byte | (more ? 128 : 0));
    } while (more);
}
uint8_t wasm_type(unsigned kind) { return uint8_t(0x7f - kind); }
} // namespace
struct GameWasm::Impl {
    JSContext *ctx;
    std::shared_ptr<Engine> engine;
    wasm_store_t *store = nullptr;
    std::array<JSValue, 3> errors{JS_UNDEFINED, JS_UNDEFINED, JS_UNDEFINED};
    bool captured = false;
    JSValue pending = JS_UNDEFINED;
    bool has_pending = false;
    unsigned depth = 0, attempts = 0, compile_attempts = 0;
    size_t source_bytes = 0;
    GameStats counts;
    struct Module {
        wasm_module_t *module = nullptr;
        std::vector<uint8_t> bytes;
    };
    struct Host {
        Impl *owner;
        JSValue fn;
        std::vector<wasm_valkind_t> results;
    };
    struct Instance {
        wasm_instance_t *instance = nullptr;
        BrowserWasmState state{};
    };
    struct External {
        wasm_extern_t *value = nullptr;
        unsigned instance = 0;
        JSValue buffer = JS_UNDEFINED;
    };
    std::map<unsigned, Module> modules;
    std::map<unsigned, std::unique_ptr<Instance>> instances;
    std::map<unsigned, External> externals;
    std::vector<std::unique_ptr<Host>> hosts;
    std::vector<wasm_extern_t *> host_values;
    unsigned next_module = 0, next_instance = 0, next_external = 0;
    explicit Impl(JSContext *c) : ctx(c) {}
    ~Impl() {
        for (auto &p : externals) {
            if (!JS_IsUndefined(p.second.buffer)) {
                JS_DetachArrayBuffer(ctx, p.second.buffer);
                JS_FreeValue(ctx, p.second.buffer);
            }
            wasm_extern_delete(p.second.value);
        }
        for (auto *value : host_values)
            wasm_extern_delete(value);
        if (store)
            wasm_store_delete(store);
        for (auto &h : hosts)
            JS_FreeValue(ctx, h->fn);
        for (auto error : errors)
            JS_FreeValue(ctx, error);
        if (has_pending)
            JS_FreeValue(ctx, pending);
    }
    void ensure() {
        if (store)
            return;
        engine = shared_engine.lock();
        if (!engine) {
            engine = std::make_shared<Engine>();
            shared_engine = engine;
        }
        store = wasm_store_new(engine->engine);
        if (!store)
            throw Failure{2, "Wasm store allocation failed"};
    }
    JSValue text(const std::string &s) { return JS_NewStringLen(ctx, s.data(), s.size()); }
    std::string str(JSValueConst value, size_t max = 2048) {
        size_t length;
        const char *s = JS_ToCStringLen(ctx, &length, value);
        if (!s)
            throw JSFailure{};
        std::string out(s, std::min(length, max + 1));
        JS_FreeCString(ctx, s);
        if (length > max)
            throw Failure{3, "Wasm string quota"};
        return out;
    }
    uint32_t number(JSValueConst v) {
        uint32_t n;
        if (JS_ToUint32(ctx, &n, v) < 0)
            throw JSFailure{};
        return n;
    }
    JSValue property(JSValueConst object, const std::string &key) {
        JSAtom atom = JS_NewAtomLen(ctx, key.data(), key.size());
        if (atom == JS_ATOM_NULL)
            throw JSFailure{};
        auto v = JS_GetProperty(ctx, object, atom);
        JS_FreeAtom(ctx, atom);
        if (JS_IsException(v))
            throw JSFailure{};
        return v;
    }
    JSValue error(unsigned kind, const std::string &message) {
        if (kind == 3)
            return JS_ThrowRangeError(ctx, "%s", message.substr(0, 192).c_str());
        if (kind == 4)
            return JS_ThrowTypeError(ctx, "%s", message.substr(0, 192).c_str());
        auto e = JS_NewError(ctx);
        if (JS_IsException(e))
            return e;
        if (kind < 3 && captured)
            JS_SetPrototype(ctx, e, errors[kind]);
        else
            JS_SetPropertyStr(ctx, e, "name", text(kind == 3 ? "RangeError" : "TypeError"));
        JS_SetPropertyStr(ctx, e, "message", text(message.substr(0, 192)));
        return JS_Throw(ctx, e);
    }
    Module &module(unsigned id) {
        auto found = modules.find(id);
        if (found == modules.end())
            throw Failure{4, "Invalid WebAssembly module"};
        return found->second;
    }
    External &external(unsigned id, unsigned wanted = 255) {
        auto found = externals.find(id);
        if (found == externals.end() ||
            (wanted != 255 && wasm_extern_kind(found->second.value) != wanted))
            throw Failure{4, "Invalid WebAssembly object"};
        return found->second;
    }
    std::string trap_message(wasm_trap_t *trap) {
        wasm_message_t message = WASM_EMPTY_VEC;
        if (trap)
            wasm_trap_message(trap, &message);
        std::string text(message.data ? message.data : "",
                         std::min(message.num_elems, size_t(192)));
        while (!text.empty() && text.back() == 0)
            text.pop_back();
        wasm_byte_vec_delete(&message);
        if (trap)
            wasm_trap_delete(trap);
        return text.empty() ? "WebAssembly execution trapped" : text;
    }
    JSValue types(const wasm_valtype_vec_t *types) {
        auto array = JS_NewArray(ctx);
        for (unsigned i = 0; i < types->num_elems; i++)
            JS_SetPropertyUint32(ctx, array, i,
                                 text(value_name(wasm_valtype_kind(types->data[i]))));
        return array;
    }
    JSValue descriptor(const wasm_externtype_t *type) {
        auto out = JS_NewObject(ctx);
        auto kind = wasm_externtype_kind(type);
        JS_SetPropertyStr(ctx, out, "kind", text(kind_name(kind)));
        if (kind == WASM_EXTERN_FUNC) {
            auto *f = wasm_externtype_as_functype_const(type);
            JS_SetPropertyStr(ctx, out, "parameters", types(wasm_functype_params(f)));
            JS_SetPropertyStr(ctx, out, "results", types(wasm_functype_results(f)));
        } else if (kind == WASM_EXTERN_GLOBAL) {
            auto *g = wasm_externtype_as_globaltype_const(type);
            JS_SetPropertyStr(ctx, out, "valueType",
                              text(value_name(wasm_valtype_kind(wasm_globaltype_content(g)))));
            JS_SetPropertyStr(ctx, out, "mutable",
                              JS_NewBool(ctx, wasm_globaltype_mutability(g) == WASM_VAR));
        }
        return out;
    }
    JSValue metadata(Module &m, bool imports) {
        auto out = JS_NewArray(ctx);
        if (imports) {
            wasm_importtype_vec_t v = WASM_EMPTY_VEC;
            wasm_module_imports(m.module, &v);
            for (unsigned i = 0; i < v.num_elems; i++) {
                auto item = descriptor(wasm_importtype_type(v.data[i]));
                JS_SetPropertyStr(ctx, item, "module",
                                  text(name(wasm_importtype_module(v.data[i]))));
                JS_SetPropertyStr(ctx, item, "name", text(name(wasm_importtype_name(v.data[i]))));
                JS_SetPropertyUint32(ctx, out, i, item);
            }
            wasm_importtype_vec_delete(&v);
        } else {
            wasm_exporttype_vec_t v = WASM_EMPTY_VEC;
            wasm_module_exports(m.module, &v);
            for (unsigned i = 0; i < v.num_elems; i++) {
                auto item = descriptor(wasm_exporttype_type(v.data[i]));
                JS_SetPropertyStr(ctx, item, "name", text(name(wasm_exporttype_name(v.data[i]))));
                JS_SetPropertyUint32(ctx, out, i, item);
            }
            wasm_exporttype_vec_delete(&v);
        }
        return out;
    }
    unsigned compile(const uint8_t *bytes, size_t size) {
        if (++compile_attempts > 32)
            throw Failure{3, "WebAssembly compilation attempt quota"};
        if (size < 8 || std::memcmp(bytes, "\0asm\1\0\0\0", 8))
            throw Failure{0, "Invalid WebAssembly binary header"};
        if (size > 8u * 1024u * 1024u || size > 16u * 1024u * 1024u - source_bytes ||
            next_module >= 16)
            throw Failure{3, "WebAssembly module quota exceeded"};
        ensure();
        wasm_byte_vec_t binary = {size, const_cast<char *>(reinterpret_cast<const char *>(bytes)),
                                  size, 1, nullptr};
        auto *m = wasm_module_new(store, &binary);
        if (!m)
            throw Failure{0, "WebAssembly binary validation failed"};
        char reason[192] = {};
        if (!browser_wasm_module_supported(m, reason, sizeof(reason)))
            throw Failure{0, reason};
        Module item;
        item.module = m;
        item.bytes.assign(bytes, bytes + size);
        auto id = ++next_module;
        modules.emplace(id, std::move(item));
        source_bytes += size;
        counts.modules++;
        return id;
    }
    wasm_val_t from_js(JSValueConst value, wasm_valkind_t kind) {
        wasm_val_t out{};
        out.kind = kind;
        switch (kind) {
        case WASM_I32:
            if (JS_ToInt32(ctx, &out.of.i32, value) < 0)
                throw JSFailure{};
            break;
        case WASM_I64:
            if (JS_ToBigInt64(ctx, &out.of.i64, value) < 0)
                throw JSFailure{};
            break;
        case WASM_F32:
        case WASM_F64: {
            double n;
            if (JS_ToFloat64(ctx, &n, value) < 0)
                throw JSFailure{};
            if (kind == WASM_F32)
                out.of.f32 = float(n);
            else
                out.of.f64 = n;
            break;
        }
        default:
            throw Failure{4, "Reference and SIMD JS arguments are not supported"};
        }
        return out;
    }
    JSValue to_js(const wasm_val_t &v) {
        switch (v.kind) {
        case WASM_I32:
            return JS_NewInt32(ctx, v.of.i32);
        case WASM_I64:
            return JS_NewBigInt64(ctx, v.of.i64);
        case WASM_F32:
            return JS_NewFloat64(ctx, v.of.f32);
        case WASM_F64:
            return JS_NewFloat64(ctx, v.of.f64);
        default:
            throw Failure{4, "Reference and SIMD JS values are not supported"};
        }
    }
    static wasm_trap_t *callback(void *opaque, const wasm_val_vec_t *args,
                                 wasm_val_vec_t *results) noexcept {
        auto &h = *static_cast<Host *>(opaque);
        auto &self = *h.owner;
        try {
            std::vector<JSValue> values;
            values.reserve(args->num_elems);
            struct Cleanup {
                JSContext *ctx;
                std::vector<JSValue> &values;
                ~Cleanup() {
                    for (auto v : values)
                        JS_FreeValue(ctx, v);
                }
            } cleanup{self.ctx, values};
            for (size_t i = 0; i < args->num_elems; i++)
                values.push_back(self.to_js(args->data[i]));
            auto result = JS_Call(self.ctx, h.fn, JS_UNDEFINED, int(values.size()), values.data());
            Val answer(self.ctx, result);
            if (results->size < h.results.size())
                throw Failure{2, "Wasm import result storage unavailable"};
            results->num_elems = h.results.size();
            for (unsigned i = 0; i < h.results.size(); i++) {
                if (results->num_elems == 1)
                    results->data[i] = self.from_js(answer.value, h.results[i]);
                else {
                    Val item(self.ctx, JS_GetPropertyUint32(self.ctx, answer.value, i));
                    results->data[i] = self.from_js(item.value, h.results[i]);
                }
            }
            return nullptr;
        } catch (const Failure &e) {
            self.error(e.kind, e.message);
        } catch (const JSFailure &) {
        } catch (...) {
            JS_ThrowOutOfMemory(self.ctx);
        }
        if (!self.has_pending) {
            self.pending = JS_GetException(self.ctx);
            self.has_pending = true;
        } else {
            auto ignored = JS_GetException(self.ctx);
            JS_FreeValue(self.ctx, ignored);
        }
        wasm_message_t message = WASM_EMPTY_VEC;
        wasm_name_new_from_string_nt(&message, "JavaScript import threw");
        auto *trap = wasm_trap_new(self.store, &message);
        wasm_byte_vec_delete(&message);
        return trap;
    }
    static void grew(void *owner, const void *identity) noexcept {
        auto &self = *static_cast<Impl *>(owner);
        for (auto &item : self.externals) {
            if (wasm_extern_kind(item.second.value) == WASM_EXTERN_MEMORY &&
                browser_wasm_memory_identity(wasm_extern_as_memory(item.second.value)) ==
                    identity &&
                !JS_IsUndefined(item.second.buffer)) {
                JS_DetachArrayBuffer(self.ctx, item.second.buffer);
                JS_FreeValue(self.ctx, item.second.buffer);
                item.second.buffer = JS_UNDEFINED;
            }
        }
    }
    unsigned retain(wasm_extern_t *value, unsigned owner) {
        for (auto &e : externals)
            if (browser_wasm_same_external(value, e.second.value)) {
                wasm_extern_delete(value);
                return e.first;
            }
        if (next_external >= 512) {
            wasm_extern_delete(value);
            throw Failure{3, "WebAssembly export quota exceeded"};
        }
        auto id = ++next_external;
        externals.emplace(id, External{value, owner, JS_UNDEFINED});
        return id;
    }
    JSValue export_info(unsigned id) {
        auto &e = external(id);
        auto *type = wasm_extern_type(e.value);
        if (!type)
            throw Failure{2, "WebAssembly export type unavailable"};
        auto info = descriptor(type);
        wasm_externtype_delete(type);
        JS_SetPropertyStr(ctx, info, "id", JS_NewUint32(ctx, id));
        return info;
    }
    bool same_signature(wasm_func_t *function, const wasm_functype_t *wanted) {
        auto *actual = wasm_func_type(function);
        bool same = true;
        for (unsigned part = 0; part < 2; part++) {
            auto *a = part ? wasm_functype_results(actual) : wasm_functype_params(actual),
                 *b = part ? wasm_functype_results(wanted) : wasm_functype_params(wanted);
            if (a->num_elems != b->num_elems)
                same = false;
            else
                for (size_t i = 0; i < a->num_elems; i++)
                    if (wasm_valtype_kind(a->data[i]) != wasm_valtype_kind(b->data[i]))
                        same = false;
        }
        wasm_functype_delete(actual);
        return same;
    }
    JSValue instantiate(unsigned id, JSValueConst imports) {
        if (++attempts > 16)
            throw Failure{3, "WebAssembly instance quota exceeded"};
        auto &m = module(id);
        wasm_importtype_vec_t needed = WASM_EMPTY_VEC;
        wasm_module_imports(m.module, &needed);
        struct ImportCleanup {
            wasm_importtype_vec_t *p;
            ~ImportCleanup() { wasm_importtype_vec_delete(p); }
        } cleanup{&needed};
        if (needed.num_elems > 256)
            throw Failure{1, "WebAssembly import quota exceeded"};
        std::vector<wasm_extern_t *> bindings;
        bindings.reserve(needed.num_elems);
        for (unsigned i = 0; i < needed.num_elems; i++) {
            auto *type = wasm_importtype_type(needed.data[i]);
            auto kind = wasm_externtype_kind(type);
            Val item(ctx, JS_GetPropertyUint32(ctx, imports, i));
            Val value(ctx, JS_GetPropertyStr(ctx, item.value, "value"));
            wasm_extern_t *binding = nullptr;
            if (kind == WASM_EXTERN_FUNC) {
                if (!JS_IsFunction(ctx, value.value))
                    throw Failure{1, "WebAssembly import must be callable"};
                auto *signature = wasm_externtype_as_functype_const(type);
                if (wasm_functype_params(signature)->num_elems > 64 ||
                    wasm_functype_results(signature)->num_elems > 16)
                    throw Failure{1, "WebAssembly function arity quota"};
                Val original(ctx, JS_GetPropertyStr(ctx, item.value, "functionId"));
                auto original_id = number(original.value);
                if (original_id &&
                    !same_signature(
                        wasm_extern_as_func(external(original_id, WASM_EXTERN_FUNC).value),
                        signature))
                    throw Failure{1, "WebAssembly function import type mismatch"};
                auto host = std::make_unique<Host>();
                host->owner = this;
                for (unsigned part = 0; part < 2; part++) {
                    auto *types =
                        part ? wasm_functype_results(signature) : wasm_functype_params(signature);
                    for (size_t j = 0; j < types->num_elems; j++) {
                        auto k = wasm_valtype_kind(types->data[j]);
                        if (k > WASM_F64)
                            throw Failure{1, "Reference and SIMD host imports are not supported"};
                        if (part)
                            host->results.push_back(k);
                    }
                }
                host->fn = JS_DupValue(ctx, value.value);
                auto *function =
                    wasm_func_new_with_env(store, signature, callback, host.get(), nullptr);
                if (!function) {
                    JS_FreeValue(ctx, host->fn);
                    throw Failure{1, "WebAssembly host function allocation failed"};
                }
                hosts.push_back(std::move(host));
                binding = wasm_func_as_extern(function);
            } else if (kind == WASM_EXTERN_GLOBAL) {
                auto *global = wasm_externtype_as_globaltype_const(type);
                Val reference(ctx, JS_GetPropertyStr(ctx, item.value, "globalId"));
                auto global_id = number(reference.value);
                wasm_global_t *g = nullptr;
                if (global_id) {
                    g = wasm_global_copy(
                        wasm_extern_as_global(external(global_id, WASM_EXTERN_GLOBAL).value));
                } else {
                    if (wasm_globaltype_mutability(global) != WASM_CONST)
                        throw Failure{1, "Mutable global imports require a WebAssembly.Global"};
                    auto v =
                        from_js(value.value, wasm_valtype_kind(wasm_globaltype_content(global)));
                    g = wasm_global_new(store, global, &v);
                }
                if (g)
                    binding = wasm_global_as_extern(g);
            } else if (kind == WASM_EXTERN_MEMORY) {
                Val reference(ctx, JS_GetPropertyStr(ctx, item.value, "memoryId"));
                auto memory_id = number(reference.value);
                if (!memory_id)
                    throw Failure{1, "Memory imports require a WebAssembly.Memory"};
                auto *source = wasm_extern_as_memory(external(memory_id, WASM_EXTERN_MEMORY).value);
                if (!browser_wasm_memory_compatible(m.module, source))
                    throw Failure{1, "WebAssembly memory import limits mismatch"};
                auto *copy = wasm_memory_copy(source);
                if (copy)
                    binding = wasm_memory_as_extern(copy);
            } else
                throw Failure{1, "Cross-instance imported tables are not supported"};
            if (!binding)
                throw Failure{1, "WebAssembly import allocation failed"};
            host_values.push_back(binding);
            bindings.push_back(binding);
        }
        wasm_extern_vec_t vector = {bindings.size(), bindings.data(), bindings.size(),
                                    sizeof(wasm_extern_t *), nullptr};
        InstantiationArgs limits{64u * 1024u, 0, browser_wasm_memory_cap(m.module)};
        wasm_trap_t *trap = nullptr;
        // All JS coercions have finished. Snapshot immutable initializer values
        // now; mutable accesses keep the canonical storage pointer throughout.
        for (auto *binding : bindings)
            if (wasm_extern_kind(binding) == WASM_EXTERN_GLOBAL) {
                auto *g = wasm_extern_as_global(binding);
                if (browser_wasm_global_identity(g))
                    browser_wasm_global_refresh(g);
            }
        if (!browser_wasm_link_begin(m.module, &vector))
            throw Failure{1, "WebAssembly instantiation is already linking"};
        auto *native = wasm_instance_new_with_args_ex(store, m.module, &vector, &trap, &limits);
        browser_wasm_link_end();
        if (!native)
            throw Failure{2, trap_message(trap)};
        auto object = std::make_unique<Instance>();
        object->instance = native;
        object->state.owner = this;
        object->state.grown = grew;
        auto owner = ++next_instance;
        instances.emplace(owner, std::move(object));
        counts.instances++;
        char reason[192] = {};
        if (!browser_wasm_start(native, &instances.at(owner)->state, reason, sizeof(reason))) {
            counts.traps++;
            if (has_pending) {
                auto value = pending;
                pending = JS_UNDEFINED;
                has_pending = false;
                return JS_Throw(ctx, value);
            }
            throw Failure{2, reason};
        }
        wasm_extern_vec_t values = WASM_EMPTY_VEC;
        wasm_instance_exports(native, &values);
        wasm_exporttype_vec_t names = WASM_EMPTY_VEC;
        wasm_module_exports(m.module, &names);
        auto exports = JS_NewArray(ctx);
        for (unsigned i = 0; i < values.num_elems; i++) {
            auto handle = retain(wasm_extern_copy(values.data[i]), owner);
            auto item = export_info(handle);
            JS_SetPropertyStr(ctx, item, "name", text(name(wasm_exporttype_name(names.data[i]))));
            JS_SetPropertyUint32(ctx, exports, i, item);
        }
        wasm_extern_vec_delete(&values);
        wasm_exporttype_vec_delete(&names);
        return exports;
    }
    JSValue call(unsigned id, JSValueConst args) {
        if (depth >= 8)
            throw Failure{2, "WebAssembly/JavaScript recursion quota"};
        auto *f = wasm_extern_as_func(external(id, WASM_EXTERN_FUNC).value);
        auto *type = wasm_func_type(f);
        struct TypeCleanup {
            wasm_functype_t *p;
            ~TypeCleanup() { wasm_functype_delete(p); }
        } cleanup{type};
        auto *parameters = wasm_functype_params(type), *returns = wasm_functype_results(type);
        if (parameters->num_elems > 64 || returns->num_elems > 16)
            throw Failure{2, "WebAssembly function arity quota"};
        std::vector<wasm_val_t> argv, results;
        for (unsigned i = 0; i < parameters->num_elems; i++) {
            Val value(ctx, JS_GetPropertyUint32(ctx, args, i));
            argv.push_back(from_js(value.value, wasm_valtype_kind(parameters->data[i])));
        }
        for (size_t i = 0; i < returns->num_elems; i++) {
            wasm_val_t v{};
            v.kind = wasm_valtype_kind(returns->data[i]);
            if (v.kind > WASM_F64)
                throw Failure{4, "Reference and SIMD JS results are not supported"};
            results.push_back(v);
        }
        wasm_val_vec_t input = {argv.size(), argv.data(), argv.size(), sizeof(wasm_val_t), nullptr},
                       output = {results.size(), results.data(), results.size(), sizeof(wasm_val_t),
                                 nullptr};
        browser_wasm_budget(f);
        ++depth;
        counts.calls++;
        auto *trap = wasm_func_call(f, &input, &output);
        --depth;
        if (has_pending) {
            if (trap)
                wasm_trap_delete(trap);
            auto value = pending;
            pending = JS_UNDEFINED;
            has_pending = false;
            return JS_Throw(ctx, value);
        }
        if (trap) {
            counts.traps++;
            throw Failure{2, trap_message(trap)};
        }
        if (results.empty())
            return JS_UNDEFINED;
        if (results.size() == 1)
            return to_js(results[0]);
        auto array = JS_NewArray(ctx);
        for (unsigned i = 0; i < results.size(); i++)
            JS_SetPropertyUint32(ctx, array, i, to_js(results[i]));
        return array;
    }
    unsigned synthetic(unsigned kind, unsigned initial, unsigned maximum, unsigned valkind = 0,
                       bool mut = false, JSValueConst value = JS_UNDEFINED, bool has_max = true) {
        std::vector<uint8_t> wasm{0, 97, 115, 109, 1, 0, 0, 0}, data{1};
        if (kind == WASM_EXTERN_MEMORY) {
            data.push_back(has_max ? 1 : 0);
            leb(data, initial);
            if (has_max)
                leb(data, maximum);
            section(wasm, 5, data);
        } else if (kind == WASM_EXTERN_TABLE) {
            data.push_back(0x70);
            data.push_back(1);
            leb(data, initial);
            leb(data, maximum);
            section(wasm, 4, data);
        } else {
            data.push_back(wasm_type(valkind));
            data.push_back(mut ? 1 : 0);
            auto v = from_js(value, wasm_valkind_t(valkind));
            data.push_back(uint8_t(0x41 + valkind));
            if (valkind == 0)
                signed_leb(data, v.of.i32);
            else if (valkind == 1)
                signed_leb(data, v.of.i64);
            else {
                uint8_t bytes[8];
                auto size = valkind == 2 ? 4 : 8;
                std::memcpy(bytes,
                            valkind == 2 ? static_cast<void *>(&v.of.f32)
                                         : static_cast<void *>(&v.of.f64),
                            size);
                data.insert(data.end(), bytes, bytes + size);
            }
            data.push_back(0x0b);
            section(wasm, 6, data);
        }
        data = {1};
        string_bytes(data, "value");
        data.push_back(kind == WASM_EXTERN_MEMORY ? 2 : kind == WASM_EXTERN_TABLE ? 1 : 3);
        data.push_back(0);
        section(wasm, 7, data);
        auto id = compile(wasm.data(), wasm.size());
        Val empty(ctx, JS_NewArray(ctx));
        Val list(ctx, instantiate(id, empty.value));
        Val item(ctx, JS_GetPropertyUint32(ctx, list.value, 0));
        Val handle(ctx, JS_GetPropertyStr(ctx, item.value, "id"));
        return number(handle.value);
    }
    JSValue op(const std::string &op, int argc, JSValueConst *a) {
        auto arg = [&](int i) -> JSValueConst { return i < argc ? a[i] : JS_UNDEFINED; };
        auto id = (argc > 2 && op != "wasmCompile" && op != "wasmValidate" && op != "wasmErrors")
                      ? number(arg(2))
                      : 0;
        if (op == "wasmBlocked")
            throw Failure{0, "WebAssembly compilation blocked by CSP"};
        if (op == "wasmErrors") {
            if (captured)
                throw Failure{4, "Wasm error classes already initialized"};
            for (unsigned i = 0; i < 3; i++) {
                Val value(ctx, JS_GetPropertyUint32(ctx, arg(2), i));
                if (!JS_IsObject(value.value))
                    throw Failure{4, "Invalid error prototype"};
                errors[i] = JS_DupValue(ctx, value.value);
            }
            captured = true;
            return JS_UNDEFINED;
        }
        if (op == "wasmCompile" || op == "wasmValidate") {
            size_t size = 0;
            auto *bytes = JS_GetArrayBuffer(ctx, &size, arg(2));
            if (!bytes)
                throw Failure{4, "Expected WebAssembly bytes"};
            if (op == "wasmCompile")
                return JS_NewUint32(ctx, compile(bytes, size));
            if (size < 8 || size > 8u * 1024u * 1024u || std::memcmp(bytes, "\0asm\1\0\0\0", 8))
                return JS_FALSE;
            ensure();
            wasm_byte_vec_t binary = {size, reinterpret_cast<char *>(bytes), size, 1, nullptr};
            return JS_NewBool(ctx, wasm_module_validate(store, &binary));
        }
        if (op == "wasmImports" || op == "wasmExports")
            return metadata(module(id), op == "wasmImports");
        if (op == "wasmInstantiate")
            return instantiate(id, arg(3));
        if (op == "wasmCall")
            return call(id, arg(3));
        if (op == "wasmStandalone") {
            auto kind = number(arg(2));
            auto initial = number(arg(3)), maximum = number(arg(4));
            if (kind == WASM_EXTERN_MEMORY || kind == WASM_EXTERN_TABLE) {
                auto cap = kind == WASM_EXTERN_MEMORY ? 65536u : 65536u;
                if (initial > maximum || maximum > cap)
                    throw Failure{3, "WebAssembly limits out of range"};
                if (kind == WASM_EXTERN_MEMORY && initial > BROWSER_WASM_MAX_PAGES)
                    throw Failure{3, "WebAssembly memory quota"};
                return export_info(
                    synthetic(kind, initial, maximum, 0, false, JS_UNDEFINED,
                              kind != WASM_EXTERN_MEMORY || JS_ToBool(ctx, arg(5)) > 0));
            }
            return export_info(
                synthetic(kind, 0, 0, number(arg(3)), JS_ToBool(ctx, arg(4)) > 0, arg(5)));
        }
        if (op == "wasmMemoryBuffer") {
            auto &e = external(id, WASM_EXTERN_MEMORY);
            if (JS_IsUndefined(e.buffer)) {
                auto *memory = wasm_extern_as_memory(e.value);
                e.buffer = JS_NewArrayBuffer(
                    ctx, reinterpret_cast<uint8_t *>(wasm_memory_data(memory)),
                    wasm_memory_data_size(memory), [](JSRuntime *, void *, void *) {}, nullptr,
                    false);
                if (JS_IsException(e.buffer)) {
                    e.buffer = JS_UNDEFINED;
                    return JS_EXCEPTION;
                }
            }
            return JS_DupValue(ctx, e.buffer);
        }
        if (op == "wasmMemoryGrow") {
            auto *memory = wasm_extern_as_memory(external(id, WASM_EXTERN_MEMORY).value);
            auto old = wasm_memory_size(memory);
            if (!browser_wasm_grow(memory, number(arg(3))))
                throw Failure{3, "WebAssembly memory growth failed"};
            return JS_NewUint32(ctx, old);
        }
        if (op == "wasmGlobalGet" || op == "wasmGlobalSet") {
            auto *global = wasm_extern_as_global(external(id, WASM_EXTERN_GLOBAL).value);
            if (op == "wasmGlobalGet") {
                wasm_val_t value{};
                wasm_global_get(global, &value);
                return to_js(value);
            }
            auto *type = wasm_global_type(global);
            bool mut = wasm_globaltype_mutability(type) == WASM_VAR;
            auto k = wasm_valtype_kind(wasm_globaltype_content(type));
            wasm_globaltype_delete(type);
            if (!mut)
                throw Failure{4, "WebAssembly global is immutable"};
            auto value = from_js(arg(3), k);
            wasm_global_set(global, &value);
            return JS_UNDEFINED;
        }
        if (op == "wasmTableLength")
            return JS_NewUint32(
                ctx, wasm_table_size(wasm_extern_as_table(external(id, WASM_EXTERN_TABLE).value)));
        if (op == "wasmTableGet" || op == "wasmTableSet" || op == "wasmTableGrow") {
            auto &e = external(id, WASM_EXTERN_TABLE);
            auto *table = wasm_extern_as_table(e.value);
            auto index = number(arg(3));
            auto length = wasm_table_size(table);
            auto *type = wasm_table_type(table);
            auto kind = wasm_valtype_kind(wasm_tabletype_element(type));
            wasm_tabletype_delete(type);
            if (kind != WASM_FUNCREF)
                throw Failure{4, "Only funcref tables are supported by the JS bridge"};
            if (op == "wasmTableGet") {
                if (index >= length)
                    throw Failure{3, "WebAssembly table index out of bounds"};
                auto *ref = wasm_table_get(table, index);
                if (!ref)
                    return JS_NULL;
                auto *f = wasm_ref_as_func(ref);
                wasm_ref_delete(ref);
                if (!f)
                    throw Failure{2, "WebAssembly table function unavailable"};
                return export_info(retain(wasm_func_as_extern(f), e.instance));
            }
            auto function_id = number(arg(4));
            auto *f = function_id
                          ? wasm_extern_as_func(external(function_id, WASM_EXTERN_FUNC).value)
                          : nullptr;
            if (op == "wasmTableSet") {
                if (index >= length)
                    throw Failure{3, "WebAssembly table index out of bounds"};
                if (!browser_wasm_table_set(table, index, f))
                    throw Failure{4, "Cross-instance table references are unsupported"};
                return JS_UNDEFINED;
            }
            if (!browser_wasm_table_grow(table, index, f))
                throw Failure{3, "WebAssembly table growth failed"};
            return JS_NewUint32(ctx, length);
        }
        if (op == "wasmCustomSections") {
            auto &bytes = module(id).bytes;
            auto wanted = str(arg(3), 256);
            auto out = JS_NewArray(ctx);
            size_t cursor = 8;
            unsigned result = 0;
            auto read = [&](size_t end) {
                uint32_t v = 0;
                unsigned bits = 0;
                while (cursor < end && bits < 35) {
                    auto b = bytes[cursor++];
                    v |= uint32_t(b & 127) << bits;
                    if (!(b & 128))
                        return v;
                    bits += 7;
                }
                throw Failure{0, "Invalid WebAssembly custom section"};
            };
            while (cursor < bytes.size()) {
                auto section_id = bytes[cursor++];
                auto n = read(bytes.size());
                if (n > bytes.size() - cursor)
                    throw Failure{0, "Invalid WebAssembly section"};
                auto end = cursor + n;
                if (!section_id) {
                    auto len = read(end);
                    if (len > end - cursor)
                        throw Failure{0, "Invalid custom section name"};
                    std::string label(reinterpret_cast<const char *>(bytes.data() + cursor), len);
                    cursor += len;
                    if (label == wanted)
                        JS_SetPropertyUint32(
                            ctx, out, result++,
                            JS_NewArrayBufferCopy(ctx, bytes.data() + cursor, end - cursor));
                }
                cursor = end;
            }
            return out;
        }
        throw Failure{4, "Unknown WebAssembly operation"};
    }
};
GameWasm::GameWasm(JSContext *ctx) : impl(std::make_unique<Impl>(ctx)) {}
GameWasm::~GameWasm() = default;
JSValue GameWasm::operation(const std::string &op, int argc, JSValueConst *a) {
    try {
        return impl->op(op, argc, a);
    } catch (const Failure &e) {
        return impl->error(e.kind, e.message);
    } catch (const JSFailure &) {
        return JS_EXCEPTION;
    } catch (...) {
        return JS_ThrowOutOfMemory(impl->ctx);
    }
}
GameStats GameWasm::stats() const { return impl->counts; }
} // namespace visual
