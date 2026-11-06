#pragma once
#include "ws_codec.h"
#include <curl/curl.h>
#include <deque>
#include <map>
#include <memory>
#include <string>
#include <vector>
namespace visual {
struct SocketURL {
    std::string url, https, origin, http_origin, host, target;
};
class WebSockets {
    struct Connection;
    CURLM *multi = nullptr;
    std::map<unsigned, std::unique_ptr<Connection>> connections;
    unsigned next_id = 0, round_robin = 0, event_cursor = 0;
    size_t receive_bytes = 0;
    void stop(Connection &);
    void finish(Connection &, bool, const char *diagnostic = nullptr);
    bool queue(Connection &, unsigned, const uint8_t *, size_t, bool priority = false);
    void fail(Connection &, const char *, unsigned protocol_code = 0);
    bool handshake(Connection &);
    void progress(Connection &, uint64_t);

  public:
    struct Event {
        unsigned id = 0;
        enum Kind { open, message, error, close } kind = open;
        bool binary = false, clean = false;
        unsigned code = 1006;
        size_t buffered = 0;
        std::string protocol, origin, reason, diagnostic;
        std::vector<uint8_t> data;
    };
    WebSockets();
    ~WebSockets();
    WebSockets(const WebSockets &) = delete;
    WebSockets &operator=(const WebSockets &) = delete;
    static SocketURL normalize(const std::string &base, const std::string &input);
    unsigned create(SocketURL, const std::string &source, const std::vector<std::string> &,
                    bool permitted, uint64_t now);
    bool send(unsigned, bool, const uint8_t *, size_t);
    void close(unsigned, unsigned, const uint8_t *, size_t, bool connecting, uint64_t now);
    size_t buffered(unsigned) const;
    void pump(uint64_t now);
    bool pop(Event &);
    void clear();
};
} // namespace visual
