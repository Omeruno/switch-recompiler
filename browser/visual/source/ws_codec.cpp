#include "ws_codec.h"
#include <algorithm>
#include <cstring>
namespace visual {
namespace ws {
bool valid_utf8(const uint8_t *p, size_t size) {
    for (size_t i = 0; i < size;) {
        unsigned c = p[i], n = 0, lo = 0x80, hi = 0xbf;
        if (c < 0x80) {
            ++i;
            continue;
        }
        if (c >= 0xc2 && c <= 0xdf)
            n = 2;
        else if (c >= 0xe0 && c <= 0xef) {
            n = 3;
            if (c == 0xe0)
                lo = 0xa0;
            if (c == 0xed)
                hi = 0x9f;
        } else if (c >= 0xf0 && c <= 0xf4) {
            n = 4;
            if (c == 0xf0)
                lo = 0x90;
            if (c == 0xf4)
                hi = 0x8f;
        } else
            return false;
        if (n > size - i)
            return false;
        for (unsigned j = 1; j < n; j++)
            if (p[i + j] < (j == 1 ? lo : 0x80) || p[i + j] > (j == 1 ? hi : 0xbf))
                return false;
        i += n;
    }
    return true;
}
bool valid_close_code(unsigned code) {
    return code >= 1000 && code < 5000 && code != 1004 && code != 1005 && code != 1006 &&
           code != 1015 && (code < 1016 || code >= 3000);
}
std::string base64(const uint8_t *p, size_t size) {
    const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    for (size_t i = 0; i < size; i += 3) {
        uint32_t v = uint32_t(p[i]) << 16;
        if (i + 1 < size)
            v |= uint32_t(p[i + 1]) << 8;
        if (i + 2 < size)
            v |= p[i + 2];
        out += alphabet[v >> 18];
        out += alphabet[(v >> 12) & 63];
        out += i + 1 < size ? alphabet[(v >> 6) & 63] : '=';
        out += i + 2 < size ? alphabet[v & 63] : '=';
    }
    return out;
}
static uint32_t rol(uint32_t x, unsigned bits) { return (x << bits) | (x >> (32 - bits)); }
std::string accept_key(const std::string &key) {
    const std::string input = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::vector<uint8_t> bytes(input.begin(), input.end());
    uint64_t bits = uint64_t(bytes.size()) * 8;
    bytes.push_back(0x80);
    while (bytes.size() % 64 != 56)
        bytes.push_back(0);
    for (int i = 7; i >= 0; i--)
        bytes.push_back(uint8_t(bits >> (i * 8)));
    uint32_t state[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    for (size_t offset = 0; offset < bytes.size(); offset += 64) {
        uint32_t words[80];
        for (unsigned i = 0; i < 16; i++) {
            words[i] = 0;
            for (unsigned j = 0; j < 4; j++)
                words[i] = (words[i] << 8) | bytes[offset + i * 4 + j];
        }
        for (unsigned i = 16; i < 80; i++)
            words[i] = rol(words[i - 3] ^ words[i - 8] ^ words[i - 14] ^ words[i - 16], 1);
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
        for (unsigned i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d);
                k = 0x5a827999;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ed9eba1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8f1bbcdc;
            } else {
                f = b ^ c ^ d;
                k = 0xca62c1d6;
            }
            uint32_t temp = rol(a, 5) + f + e + k + words[i];
            e = d;
            d = c;
            c = rol(b, 30);
            b = a;
            a = temp;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
    }
    uint8_t digest[20];
    for (unsigned i = 0; i < 5; i++)
        for (unsigned j = 0; j < 4; j++)
            digest[i * 4 + j] = uint8_t(state[i] >> (24 - j * 8));
    return base64(digest, sizeof(digest));
}
std::vector<uint8_t> frame(unsigned opcode, const uint8_t *data, size_t size,
                           const std::array<uint8_t, 4> &mask) {
    std::vector<uint8_t> out;
    out.reserve(size + 14);
    out.push_back(uint8_t(0x80 | opcode));
    if (size < 126)
        out.push_back(uint8_t(0x80 | size));
    else if (size <= 65535) {
        out.push_back(0xfe);
        out.push_back(uint8_t(size >> 8));
        out.push_back(uint8_t(size));
    } else {
        out.push_back(0xff);
        for (int i = 7; i >= 0; i--)
            out.push_back(uint8_t(uint64_t(size) >> (i * 8)));
    }
    out.insert(out.end(), mask.begin(), mask.end());
    for (size_t i = 0; i < size; i++)
        out.push_back(data[i] ^ mask[i & 3]);
    return out;
}
int Decoder::fail(unsigned code, const char *tag) {
    close_error = code;
    diagnostic = tag;
    return error;
}
bool Decoder::feed(const uint8_t *p, size_t size) {
    if (close_error)
        return false;
    if (!size)
        return true;
    if (size > message_limit + 14 + 16384 - (wire.size() - cursor)) {
        fail(1009, "limit");
        return false;
    }
    if (cursor) {
        wire.erase(wire.begin(), wire.begin() + cursor);
        cursor = 0;
    }
    wire.insert(wire.end(), p, p + size);
    return true;
}
void Decoder::consume(size_t size) {
    cursor += size;
    if (cursor == wire.size()) {
        wire.clear();
        cursor = 0;
    } else if (cursor >= 16384) {
        wire.erase(wire.begin(), wire.begin() + cursor);
        cursor = 0;
    }
}
Decoder::Result Decoder::next(Event &out) {
    if (close_error)
        return error;
    size_t available = wire.size() - cursor;
    if (available < 2)
        return need_data;
    const uint8_t *p = wire.data() + cursor;
    bool fin = p[0] & 0x80;
    unsigned opcode = p[0] & 15;
    uint64_t size = p[1] & 127;
    size_t header = 2;
    if ((p[0] & 0x70) || (p[1] & 0x80) ||
        (opcode != 0 && opcode != 1 && opcode != 2 && opcode != 8 && opcode != 9 && opcode != 10)) {
        fail(1002, "protocol");
        return error;
    }
    if (size == 126) {
        if (available < 4)
            return need_data;
        size = (uint64_t(p[2]) << 8) | p[3];
        header = 4;
        if (size < 126) {
            fail(1002, "protocol");
            return error;
        }
    } else if (size == 127) {
        if (available < 10)
            return need_data;
        if (p[2] & 0x80) {
            fail(1002, "protocol");
            return error;
        }
        size = 0;
        for (unsigned i = 2; i < 10; i++)
            size = (size << 8) | p[i];
        header = 10;
        if (size < 65536) {
            fail(1002, "protocol");
            return error;
        }
    }
    if (opcode >= 8 && (!fin || size > 125)) {
        fail(1002, "protocol");
        return error;
    }
    if (size > message_limit || ((opcode == 0) && size > message_limit - fragments.size())) {
        fail(1009, "limit");
        return error;
    }
    if (opcode < 8 && ((opcode == 0 && !fragmented) || (opcode != 0 && fragmented))) {
        fail(1002, "protocol");
        return error;
    }
    if (size > available - header)
        return need_data;
    auto *payload = p + header;
    out = Event{};
    if (opcode == 8) {
        if (size == 1) {
            fail(1002, "protocol");
            return error;
        }
        out.kind = Event::close;
        if (size) {
            out.code = (unsigned(payload[0]) << 8) | payload[1];
            if (!valid_close_code(out.code)) {
                fail(1002, "protocol");
                return error;
            }
            if (!valid_utf8(payload + 2, size - 2)) {
                fail(1007, "utf8");
                return error;
            }
            out.reason.assign(reinterpret_cast<const char *>(payload + 2), size - 2);
        }
        out.data.assign(payload, payload + size);
        consume(header + size);
        return event;
    }
    if (opcode == 9 || opcode == 10) {
        out.kind = opcode == 9 ? Event::ping : Event::pong;
        out.data.assign(payload, payload + size);
        consume(header + size);
        return event;
    }
    unsigned kind = opcode ? opcode : fragmented;
    if (fragmented || !fin) {
        if (!fragmented)
            fragmented = opcode;
        fragments.insert(fragments.end(), payload, payload + size);
        consume(header + size);
        if (!fin)
            return consumed;
        out.data = std::move(fragments);
        fragments.clear();
        fragmented = 0;
    } else {
        out.data.assign(payload, payload + size);
        consume(header + size);
    }
    if (kind == 1 && !valid_utf8(out.data.data(), out.data.size())) {
        fail(1007, "utf8");
        return error;
    }
    out.kind = kind == 1 ? Event::text : Event::binary;
    return event;
}
} // namespace ws
} // namespace visual
