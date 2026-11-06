#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace visual {
namespace ws {
constexpr size_t message_limit = 512u * 1024u;
bool valid_utf8(const uint8_t *, size_t);
bool valid_close_code(unsigned);
std::string base64(const uint8_t *, size_t);
std::string accept_key(const std::string &);
std::vector<uint8_t> frame(unsigned opcode, const uint8_t *, size_t,
                           const std::array<uint8_t, 4> &mask);
struct Event {
    enum Kind { text, binary, ping, pong, close } kind = text;
    std::vector<uint8_t> data;
    unsigned code = 1005;
    std::string reason;
};
class Decoder {
    std::vector<uint8_t> wire, fragments;
    size_t cursor = 0;
    unsigned fragmented = 0;
    void consume(size_t);
    int fail(unsigned, const char *);

  public:
    enum Result { need_data, consumed, event, error };
    unsigned close_error = 0;
    std::string diagnostic;
    bool feed(const uint8_t *, size_t);
    Result next(Event &);
};
} // namespace ws
} // namespace visual
