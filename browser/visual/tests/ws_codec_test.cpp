#include "ws_codec.h"
#include <cstdio>
#include <cstdlib>
using namespace visual::ws;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)
static std::vector<uint8_t> server(unsigned op, const std::vector<uint8_t> &data, bool fin = true) {
    auto out = frame(op, data.data(), data.size(), {0, 0, 0, 0});
    out[0] = uint8_t(op | (fin ? 128 : 0));
    size_t header = out.size() - data.size();
    out.erase(out.begin() + header - 4, out.begin() + header);
    out[1] &= 127;
    return out;
}
int main() {
    CHECK(accept_key("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    auto masked = frame(1, reinterpret_cast<const uint8_t *>("Hello"), 5, {0x37, 0xfa, 0x21, 0x3d});
    CHECK((masked ==
           std::vector<uint8_t>{0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58}));
    for (size_t n :
         {size_t(0), size_t(125), size_t(126), size_t(65535), size_t(65536), message_limit}) {
        std::vector<uint8_t> body(n, 0x41);
        auto wire = server(2, body);
        Decoder decoder;
        Event event;
        for (size_t i = 0; i < wire.size();) {
            size_t step = std::min(size_t(997), wire.size() - i);
            CHECK(decoder.feed(wire.data() + i, step));
            i += step;
            auto result = decoder.next(event);
            CHECK(result == (i == wire.size() ? Decoder::event : Decoder::need_data));
        }
        CHECK(event.kind == Event::binary && event.data == body);
    }
    Decoder fragments;
    Event e;
    auto first = server(1, {0xd0}, false), ping = server(9, {1}), last = server(0, {0x96});
    CHECK(fragments.feed(first.data(), first.size()));
    CHECK(fragments.next(e) == Decoder::consumed);
    CHECK(fragments.feed(ping.data(), ping.size()));
    CHECK(fragments.next(e) == Decoder::event && e.kind == Event::ping);
    CHECK(fragments.feed(last.data(), last.size()));
    CHECK(fragments.next(e) == Decoder::event && e.data == std::vector<uint8_t>({0xd0, 0x96}));
    for (auto wire : std::vector<std::vector<uint8_t>>{{0xc1, 0},
                                                       {0x81, 0x80},
                                                       {0x80, 0},
                                                       {0x83, 0},
                                                       {0x09, 0},
                                                       {0x89, 126, 0, 126},
                                                       {0x81, 126, 0, 1},
                                                       {0x82, 127, 0x80, 0, 0, 0, 0, 0, 0, 0},
                                                       {0x88, 1, 0},
                                                       {0x88, 2, 3, 0xed},
                                                       {0x81, 1, 0xff},
                                                       {0x82, 127, 0, 0, 0, 0, 0, 8, 0, 1}}) {
        Decoder d;
        CHECK(d.feed(wire.data(), wire.size()));
        CHECK(d.next(e) == Decoder::error);
    }
    for (auto body : std::vector<std::vector<uint8_t>>{
             {0xc0, 0x80}, {0xed, 0xa0, 0x80}, {0xf4, 0x90, 0x80, 0x80}, {0xc2}})
        CHECK(!valid_utf8(body.data(), body.size()));
    CHECK(valid_close_code(1000) && valid_close_code(1014) && valid_close_code(3000) &&
          !valid_close_code(1015) && !valid_close_code(2000) && !valid_close_code(5000));
    puts("WebSocket RFC6455 codec PASS");
}
