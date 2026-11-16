#include "text_codec.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <vector>
namespace visual {
namespace {
constexpr size_t limit = 512u * 1024u;
constexpr uint32_t magic = 0x54434431, fatal_flag = 1, ignore_bom = 2, bom_seen = 4, streaming = 8;
struct State {
    uint32_t signature = magic, encoding = 0, flags = 0;
    uint32_t point = 0, needed = 0, seen = 0, lower = 0x80, upper = 0xbf;
    int32_t byte = -1, high = 0, reconsume = -1;
};
struct Value {
    JSContext *ctx;
    JSValue value;
    Value(JSContext *c, JSValue v) : ctx(c), value(v) {}
    ~Value() { JS_FreeValue(ctx, value); }
    JSValue take() {
        auto v = value;
        value = JS_UNDEFINED;
        return v;
    }
};
struct CString {
    JSContext *ctx;
    const char *data;
    ~CString() {
        if (data)
            JS_FreeCString(ctx, data);
    }
};
uint32_t integer(JSContext *ctx, JSValueConst v) {
    uint32_t n = 0;
    if (JS_ToUint32(ctx, &n, v) < 0)
        throw std::invalid_argument("Invalid codec number");
    return n;
}
struct Bytes {
    const uint8_t *data;
    size_t size;
};
Bytes buffer(JSContext *ctx, JSValueConst v) {
    size_t size = 0;
    auto *data = JS_GetArrayBuffer(ctx, &size, v);
    if (!data && JS_HasException(ctx))
        throw std::invalid_argument("Invalid codec buffer");
    static const uint8_t empty = 0;
    return {data ? data : &empty, size};
}
Bytes view(JSContext *ctx, JSValueConst v, JSValueConst offset, JSValueConst length,
           bool bounded = true) {
    auto b = buffer(ctx, v);
    size_t start = integer(ctx, offset), count = integer(ctx, length);
    if (start > b.size || count > b.size - start)
        throw std::invalid_argument("Codec view out of bounds");
    if (bounded && count > limit)
        throw std::length_error("Text codec byte quota");
    return {b.data + start, count};
}
void utf8(std::string &out, uint32_t cp) {
    if (cp < 0x80)
        out.push_back(char(cp));
    else if (cp < 0x800) {
        out.push_back(char(0xc0 | cp >> 6));
        out.push_back(char(0x80 | (cp & 63)));
    } else if (cp < 0x10000) {
        out.push_back(char(0xe0 | cp >> 12));
        out.push_back(char(0x80 | (cp >> 6 & 63)));
        out.push_back(char(0x80 | (cp & 63)));
    } else {
        out.push_back(char(0xf0 | cp >> 18));
        out.push_back(char(0x80 | (cp >> 12 & 63)));
        out.push_back(char(0x80 | (cp >> 6 & 63)));
        out.push_back(char(0x80 | (cp & 63)));
    }
}
void reset_utf8(State &s) {
    s.point = s.needed = s.seen = 0;
    s.lower = 0x80;
    s.upper = 0xbf;
}
void emit(State &s, std::string &out, uint32_t cp) {
    if (!(s.flags & bom_seen) && s.encoding <= 2) {
        s.flags |= bom_seen;
        if (cp == 0xfeff && !(s.flags & ignore_bom))
            return;
    }
    utf8(out, cp);
}
// WHATWG single-byte indexes; undefined Windows control slots retain C1 controls.
constexpr uint16_t cp1251[128] = {
    0x402, 0x403,  0x201a, 0x453,  0x201e, 0x2026, 0x2020, 0x2021, 0x20ac, 0x2030, 0x409,  0x2039,
    0x40a, 0x40c,  0x40b,  0x40f,  0x452,  0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
    0x98,  0x2122, 0x459,  0x203a, 0x45a,  0x45c,  0x45b,  0x45f,  0xa0,   0x40e,  0x45e,  0x408,
    0xa4,  0x490,  0xa6,   0xa7,   0x401,  0xa9,   0x404,  0xab,   0xac,   0xad,   0xae,   0x407,
    0xb0,  0xb1,   0x406,  0x456,  0x491,  0xb5,   0xb6,   0xb7,   0x451,  0x2116, 0x454,  0xbb,
    0x458, 0x405,  0x455,  0x457,  0x410,  0x411,  0x412,  0x413,  0x414,  0x415,  0x416,  0x417,
    0x418, 0x419,  0x41a,  0x41b,  0x41c,  0x41d,  0x41e,  0x41f,  0x420,  0x421,  0x422,  0x423,
    0x424, 0x425,  0x426,  0x427,  0x428,  0x429,  0x42a,  0x42b,  0x42c,  0x42d,  0x42e,  0x42f,
    0x430, 0x431,  0x432,  0x433,  0x434,  0x435,  0x436,  0x437,  0x438,  0x439,  0x43a,  0x43b,
    0x43c, 0x43d,  0x43e,  0x43f,  0x440,  0x441,  0x442,  0x443,  0x444,  0x445,  0x446,  0x447,
    0x448, 0x449,  0x44a,  0x44b,  0x44c,  0x44d,  0x44e,  0x44f,
};
constexpr uint16_t cp1252[128] = {
    0x20ac, 0x81,   0x201a, 0x192,  0x201e, 0x2026, 0x2020, 0x2021, 0x2c6,  0x2030, 0x160,  0x2039,
    0x152,  0x8d,   0x17d,  0x8f,   0x90,   0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
    0x2dc,  0x2122, 0x161,  0x203a, 0x153,  0x9d,   0x17e,  0x178,  0xa0,   0xa1,   0xa2,   0xa3,
    0xa4,   0xa5,   0xa6,   0xa7,   0xa8,   0xa9,   0xaa,   0xab,   0xac,   0xad,   0xae,   0xaf,
    0xb0,   0xb1,   0xb2,   0xb3,   0xb4,   0xb5,   0xb6,   0xb7,   0xb8,   0xb9,   0xba,   0xbb,
    0xbc,   0xbd,   0xbe,   0xbf,   0xc0,   0xc1,   0xc2,   0xc3,   0xc4,   0xc5,   0xc6,   0xc7,
    0xc8,   0xc9,   0xca,   0xcb,   0xcc,   0xcd,   0xce,   0xcf,   0xd0,   0xd1,   0xd2,   0xd3,
    0xd4,   0xd5,   0xd6,   0xd7,   0xd8,   0xd9,   0xda,   0xdb,   0xdc,   0xdd,   0xde,   0xdf,
    0xe0,   0xe1,   0xe2,   0xe3,   0xe4,   0xe5,   0xe6,   0xe7,   0xe8,   0xe9,   0xea,   0xeb,
    0xec,   0xed,   0xee,   0xef,   0xf0,   0xf1,   0xf2,   0xf3,   0xf4,   0xf5,   0xf6,   0xf7,
    0xf8,   0xf9,   0xfa,   0xfb,   0xfc,   0xfd,   0xfe,   0xff,
};
State state(JSContext *ctx, JSValueConst v) {
    auto bytes = buffer(ctx, v);
    State s;
    if (bytes.size != sizeof(State))
        throw std::invalid_argument("Invalid decoder state");
    std::memcpy(&s, bytes.data, sizeof(s));
    if (s.signature != magic || s.encoding > 4 || s.flags > 15 || s.needed > 3 ||
        s.seen > s.needed || s.point > 0x10ffff || s.lower < 0x80 || s.upper > 0xbf ||
        s.lower > s.upper || s.byte < -1 || s.byte > 255 ||
        (s.high != 0 && (s.high < 0xd800 || s.high > 0xdbff)) || s.reconsume < -1 ||
        s.reconsume > 65535)
        throw std::invalid_argument("Invalid decoder state");
    if ((s.encoding != 1 && s.encoding != 2) && s.reconsume != -1)
        throw std::invalid_argument("Invalid decoder reconsume state");
    return s;
}
void field(JSContext *ctx, JSValueConst object, const char *name, JSValue value) {
    // Create own data properties: inherited setters must never run while native
    // BufferSource pointers are live (especially encodeInto destinations).
    JS_DefinePropertyValueStr(ctx, object, name, value, JS_PROP_C_W_E);
}
JSValue decode(JSContext *ctx, int argc, JSValueConst *a) {
    // op,0,state,pending,input.buffer,input.offset,input.length,stream
    if (argc < 8)
        return JS_ThrowTypeError(ctx, "Missing decoder arguments");
    State s = state(ctx, a[2]);
    auto queued = buffer(ctx, a[3]), input = view(ctx, a[4], a[5], a[6]);
    if (!(s.flags & streaming)) {
        State fresh;
        fresh.encoding = s.encoding;
        fresh.flags = s.flags & (fatal_flag | ignore_bom);
        s = fresh;
        queued.size = 0;
    }
    if (queued.size > limit || input.size > limit - queued.size)
        throw std::length_error("Decoder queue quota");
    bool keep = JS_ToBool(ctx, a[7]) > 0;
    if (keep)
        s.flags |= streaming;
    else
        s.flags &= ~streaming;
    std::vector<uint8_t> combined;
    Bytes bytes = input;
    if (queued.size) {
        combined.reserve(queued.size + input.size);
        combined.insert(combined.end(), queued.data, queued.data + queued.size);
        combined.insert(combined.end(), input.data, input.data + input.size);
        bytes = {combined.data(), combined.size()};
    }
    size_t at = 0;
    bool failed = false;
    std::string output;
    output.reserve(bytes.size);
    auto error = [&]() {
        if (s.flags & fatal_flag) {
            failed = true;
            return;
        }
        emit(s, output, 0xfffd);
    };
    while (!failed && (at < bytes.size || s.reconsume >= 0)) {
        if (s.encoding == 0) {
            uint32_t b = bytes.data[at++];
            if (!s.needed) {
                if (b <= 0x7f) {
                    emit(s, output, b);
                    continue;
                }
                if (b >= 0xc2 && b <= 0xdf) {
                    s.needed = 1;
                    s.point = b & 31;
                } else if (b >= 0xe0 && b <= 0xef) {
                    s.needed = 2;
                    s.point = b & 15;
                    if (b == 0xe0)
                        s.lower = 0xa0;
                    if (b == 0xed)
                        s.upper = 0x9f;
                } else if (b >= 0xf0 && b <= 0xf4) {
                    s.needed = 3;
                    s.point = b & 7;
                    if (b == 0xf0)
                        s.lower = 0x90;
                    if (b == 0xf4)
                        s.upper = 0x8f;
                } else
                    error();
                continue;
            }
            if (b < s.lower || b > s.upper) {
                reset_utf8(s);
                --at;
                error();
                continue;
            }
            s.lower = 0x80;
            s.upper = 0xbf;
            s.point = (s.point << 6) | (b & 63);
            if (++s.seen == s.needed) {
                auto cp = s.point;
                reset_utf8(s);
                emit(s, output, cp);
            }
        } else if (s.encoding <= 2) {
            uint32_t unit;
            if (s.reconsume >= 0) {
                unit = uint32_t(s.reconsume);
                s.reconsume = -1;
            } else {
                if (s.byte < 0) {
                    s.byte = bytes.data[at++];
                    continue;
                }
                auto b = bytes.data[at++];
                unit = s.encoding == 1 ? uint32_t(s.byte) | uint32_t(b) << 8
                                       : uint32_t(s.byte) << 8 | b;
                s.byte = -1;
            }
            if (s.high) {
                auto high = uint32_t(s.high);
                s.high = 0;
                if (unit >= 0xdc00 && unit <= 0xdfff) {
                    emit(s, output, 0x10000 + ((high - 0xd800) << 10) + unit - 0xdc00);
                    continue;
                }
                s.reconsume = int32_t(unit);
                error();
                continue;
            }
            if (unit >= 0xd800 && unit <= 0xdbff)
                s.high = int32_t(unit);
            else if (unit >= 0xdc00 && unit <= 0xdfff)
                error();
            else
                emit(s, output, unit);
        } else {
            auto b = bytes.data[at++];
            emit(s, output, b < 128 ? b : (s.encoding == 3 ? cp1251 : cp1252)[b - 128]);
        }
    }
    if (!failed && !keep) {
        if (s.encoding == 0 && s.needed) {
            reset_utf8(s);
            error();
        } else if ((s.encoding == 1 || s.encoding == 2) && (s.byte >= 0 || s.high)) {
            s.byte = -1;
            s.high = 0;
            error();
        }
    }
    Value result(ctx, JS_NewObject(ctx));
    if (JS_IsException(result.value))
        return result.take();
    const size_t remaining = failed && keep ? bytes.size - at : 0;
    field(ctx, result.value, "state",
          JS_NewArrayBufferCopy(ctx, reinterpret_cast<const uint8_t *>(&s), sizeof(s)));
    field(ctx, result.value, "pending", JS_NewArrayBufferCopy(ctx, bytes.data + at, remaining));
    field(ctx, result.value, "error", JS_NewBool(ctx, failed));
    field(ctx, result.value, "text",
          JS_NewStringLen(ctx, failed ? "" : output.data(), failed ? 0 : output.size()));
    return JS_HasException(ctx) ? JS_EXCEPTION : result.take();
}
JSValue encode(JSContext *ctx, const std::string &op, int argc, JSValueConst *a) {
    if (argc < 3 || !JS_IsString(a[2]))
        return JS_ThrowTypeError(ctx, "Expected encoding string");
    size_t size = 0;
    CString source{ctx, JS_ToCStringLen(ctx, &size, a[2])};
    if (!source.data)
        return JS_EXCEPTION;
    if (size > limit)
        throw std::length_error("Encoder source quota");
    bool into = op == "codecEncodeInto";
    uint8_t *destination = nullptr;
    size_t capacity = limit;
    if (into) {
        if (argc < 6)
            return JS_ThrowTypeError(ctx, "Missing destination view");
        auto b = view(ctx, a[3], a[4], a[5], false);
        destination = const_cast<uint8_t *>(b.data);
        capacity = b.size;
    }
    std::string output;
    output.reserve(std::min(size, capacity));
    uint32_t read = 0;
    // QuickJS returns WTF-8: valid pairs are combined, isolated surrogates remain.
    for (size_t at = 0; at < size;) {
        uint32_t cp = uint8_t(source.data[at++]);
        unsigned rest = 0;
        if (cp >= 0xf0) {
            cp &= 7;
            rest = 3;
        } else if (cp >= 0xe0) {
            cp &= 15;
            rest = 2;
        } else if (cp >= 0xc0) {
            cp &= 31;
            rest = 1;
        }
        for (unsigned i = 0; i < rest; ++i)
            cp = cp << 6 | (uint8_t(source.data[at++]) & 63);
        uint32_t units = cp > 0xffff ? 2 : 1;
        if (cp >= 0xd800 && cp <= 0xdfff)
            cp = 0xfffd;
        size_t needed = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
        if (needed > capacity - output.size())
            break;
        utf8(output, cp);
        read += units;
    }
    if (!into)
        return JS_NewArrayBufferCopy(ctx, reinterpret_cast<const uint8_t *>(output.data()),
                                     output.size());
    Value result(ctx, JS_NewObject(ctx));
    if (JS_IsException(result.value))
        return result.take();
    field(ctx, result.value, "read", JS_NewUint32(ctx, read));
    field(ctx, result.value, "written", JS_NewUint32(ctx, uint32_t(output.size())));
    if (JS_HasException(ctx))
        return JS_EXCEPTION;
    if (!output.empty())
        std::memcpy(destination, output.data(), output.size());
    return result.take();
}
} // namespace
JSValue text_codec_operation(JSContext *ctx, const std::string &op, int argc, JSValueConst *a) {
    try {
        if (op == "codecNew") {
            if (argc < 5)
                return JS_ThrowTypeError(ctx, "Missing decoder options");
            State s;
            s.encoding = integer(ctx, a[2]);
            if (s.encoding > 4)
                return JS_ThrowRangeError(ctx, "Unsupported text encoding");
            if (JS_ToBool(ctx, a[3]) > 0)
                s.flags |= fatal_flag;
            if (JS_ToBool(ctx, a[4]) > 0)
                s.flags |= ignore_bom;
            return JS_NewArrayBufferCopy(ctx, reinterpret_cast<const uint8_t *>(&s), sizeof(s));
        }
        if (op == "codecDecode")
            return decode(ctx, argc, a);
        if (op == "codecEncode" || op == "codecEncodeInto")
            return encode(ctx, op, argc, a);
        return JS_ThrowTypeError(ctx, "Unknown text codec operation");
    } catch (const std::length_error &e) {
        return JS_ThrowRangeError(ctx, "%s", e.what());
    } catch (const std::bad_alloc &) {
        return JS_ThrowOutOfMemory(ctx);
    } catch (const std::exception &e) {
        if (JS_HasException(ctx))
            return JS_EXCEPTION;
        return JS_ThrowTypeError(ctx, "%s", e.what());
    }
}
} // namespace visual
