#include "websocket.h"
extern "C" {
#include "profile.h"
}
#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <time.h>
#ifdef __SWITCH__
#include <switch.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif
namespace visual {
namespace {
uint64_t ws_clock() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return uint64_t(t.tv_sec) * 1000 + uint64_t(t.tv_nsec) / 1000000;
}
bool entropy(uint8_t *p, size_t n) {
#ifdef __SWITCH__
    randomGet(p, n);
    return true;
#else
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return false;
    size_t offset = 0;
    while (offset < n) {
        auto got = read(fd, p + offset, n - offset);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0) {
            ::close(fd);
            return false;
        }
        offset += size_t(got);
    }
    ::close(fd);
    return true;
#endif
}
std::string lower_ws(std::string s) {
    for (char &c : s)
        c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}
std::string trim_ws(std::string s) {
    auto a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}
bool token_ws(const std::string &s) {
    if (s.empty() || s.size() > 128)
        return false;
    for (unsigned char c : s)
        if (!std::isalnum(c) && std::string("!#$%&'*+-.^_`|~").find(char(c)) == std::string::npos)
            return false;
    return true;
}
bool has_token(const std::string &line, const std::string &wanted) {
    std::istringstream stream(line);
    std::string token;
    while (std::getline(stream, token, ','))
        if (lower_ws(trim_ws(token)) == wanted)
            return true;
    return false;
}
} // namespace
struct WebSockets::Connection {
    enum State { connecting, request, response, active, closing, closed } state = connecting;
    unsigned id = 0;
    SocketURL address;
    std::string source, nonce, protocol, request_text, response_text;
    std::vector<std::string> protocols;
    size_t request_cursor = 0, queued_bytes = 0, dropped_bytes = 0, event_bytes = 0;
    bool same_origin = false, received_close = false, sent_close = false, local_failure = false;
    unsigned peer_code = 1006;
    std::string peer_reason;
    uint64_t deadline = 0;
    CURL *easy = nullptr;
    CURLcode result = CURLE_OK;
    bool added = false;
    struct Out {
        std::vector<uint8_t> wire;
        size_t cursor = 0, payload = 0, header = 0;
        unsigned opcode = 0;
    };
    std::deque<Out> output;
    std::deque<Event> events;
    ws::Decoder decoder;
};
WebSockets::WebSockets() { multi = curl_multi_init(); }
WebSockets::~WebSockets() {
    clear();
    if (multi)
        curl_multi_cleanup(multi);
}
SocketURL WebSockets::normalize(const std::string &base, const std::string &input) {
    if (input.size() > 2048 || input.find('\0') != std::string::npos ||
        input.find('#') != std::string::npos)
        throw std::runtime_error("Invalid WebSocket URL");
    std::string candidate = input;
    auto prefix = lower_ws(candidate.substr(0, 8));
    if (prefix.rfind("wss://", 0) == 0)
        candidate = "https://" + candidate.substr(6);
    else if (prefix.rfind("ws://", 0) == 0 || prefix.rfind("http://", 0) == 0)
        throw std::runtime_error("Insecure WebSocket URL");
    CURLU *u = curl_url();
    char *value = nullptr;
    bool ok = u;
    auto get = [&](CURLUPart part, unsigned flags = 0) {
        char *out = nullptr;
        std::string text;
        if (curl_url_get(u, part, &out, flags) == CURLUE_OK)
            text = out;
        curl_free(out);
        return text;
    };
    if (ok && lower_ws(candidate).rfind("https://", 0) != 0)
        ok = curl_url_set(u, CURLUPART_URL, base.c_str(), 0) == CURLUE_OK;
    if (ok)
        ok = curl_url_set(u, CURLUPART_URL, candidate.c_str(), 0) == CURLUE_OK &&
             get(CURLUPART_SCHEME) == "https";
    if (ok && (curl_url_get(u, CURLUPART_USER, &value, 0) == CURLUE_OK)) {
        curl_free(value);
        value = nullptr;
        ok = false;
    }
    if (ok && (curl_url_get(u, CURLUPART_PASSWORD, &value, 0) == CURLUE_OK)) {
        curl_free(value);
        value = nullptr;
        ok = false;
    }
    if (ok && (curl_url_get(u, CURLUPART_FRAGMENT, &value, 0) == CURLUE_OK)) {
        curl_free(value);
        value = nullptr;
        ok = false;
    }
    SocketURL result;
    if (ok) {
        auto host = lower_ws(get(CURLUPART_HOST));
        curl_url_set(u, CURLUPART_HOST, host.c_str(), 0);
        result.https = get(CURLUPART_URL, CURLU_NO_DEFAULT_PORT);
        result.url = "wss:" + result.https.substr(6);
        char origin[384] = {};
        ok = browser_origin(result.https.c_str(), origin, sizeof(origin));
        result.http_origin = origin;
        if (result.http_origin.size() > 4 &&
            result.http_origin.compare(result.http_origin.size() - 4, 4, ":443") == 0)
            result.http_origin.resize(result.http_origin.size() - 4);
        if (ok)
            result.origin = "wss:" + result.http_origin.substr(6);
        result.host = host;
        auto port = get(CURLUPART_PORT);
        if (!port.empty() && port != "443")
            result.host += ":" + port;
        result.target = get(CURLUPART_PATH);
        if (result.target.empty())
            result.target = "/";
        auto query = get(CURLUPART_QUERY);
        if (!query.empty())
            result.target += "?" + query;
    }
    curl_url_cleanup(u);
    if (!ok || result.https.empty())
        throw std::runtime_error("Invalid WebSocket URL");
    for (unsigned char c : result.target)
        if (c < 0x21 || c == 0x7f)
            throw std::runtime_error("Invalid WebSocket target");
    return result;
}
void WebSockets::stop(Connection &c) {
    if (c.easy) {
        if (c.added)
            curl_multi_remove_handle(multi, c.easy);
        curl_easy_cleanup(c.easy);
        c.easy = nullptr;
        c.added = false;
    }
}
void WebSockets::clear() {
    for (auto &item : connections)
        stop(*item.second);
    connections.clear();
    receive_bytes = 0;
}
size_t WebSockets::buffered(unsigned id) const {
    auto it = connections.find(id);
    return it == connections.end() ? 0 : it->second->queued_bytes + it->second->dropped_bytes;
}
void WebSockets::finish(Connection &c, bool clean, const char *diagnostic) {
    if (c.state == Connection::closed)
        return;
    c.state = Connection::closed;
    stop(c);
    c.dropped_bytes += c.queued_bytes;
    c.queued_bytes = 0;
    c.output.clear();
    c.request_text.clear();
    c.response_text.clear();
    if (diagnostic) {
        Event event;
        event.id = c.id;
        event.kind = Event::error;
        event.origin = c.address.origin;
        event.diagnostic = diagnostic;
        event.buffered = c.dropped_bytes;
        c.events.push_back(std::move(event));
    }
    Event event;
    event.id = c.id;
    event.kind = Event::close;
    event.origin = c.address.origin;
    event.clean = clean;
    event.code = c.received_close ? c.peer_code : 1006;
    event.reason = c.received_close ? c.peer_reason : "";
    event.buffered = c.dropped_bytes;
    c.events.push_back(std::move(event));
    browser_profile_log(c.address.https.c_str(), "websocket-close", clean ? 101 : 0, c.result);
}
bool WebSockets::queue(Connection &c, unsigned opcode, const uint8_t *data, size_t size,
                       bool priority) {
    if (c.output.size() >= 64 || size > ws::message_limit ||
        ((opcode == 1 || opcode == 2) && size > 1024u * 1024u - c.queued_bytes))
        return false;
    size_t total = 0;
    for (auto &item : connections)
        total += item.second->queued_bytes;
    if ((opcode == 1 || opcode == 2) && size > 4u * 1024u * 1024u - total)
        return false;
    std::array<uint8_t, 4> mask{};
    if (!entropy(mask.data(), mask.size()))
        return false;
    Connection::Out out;
    out.opcode = opcode;
    out.payload = (opcode == 1 || opcode == 2) ? size : 0;
    out.wire = ws::frame(opcode, data, size, mask);
    out.header = out.wire.size() - size;
    c.queued_bytes += out.payload;
    if (priority) {
        auto position = c.output.begin();
        if (position != c.output.end() && position->cursor)
            ++position;
        c.output.insert(position, std::move(out));
    } else
        c.output.push_back(std::move(out));
    return true;
}
void WebSockets::fail(Connection &c, const char *tag, unsigned code) {
    if (c.state == Connection::closed)
        return;
    if (code && c.easy && (c.state == Connection::active || c.state == Connection::closing)) {
        // Best effort protocol close, never splice a control frame into a partial frame.
        if (c.output.empty() || !c.output.front().cursor) {
            std::array<uint8_t, 4> mask{};
            if (entropy(mask.data(), mask.size())) {
                uint8_t body[2] = {uint8_t(code >> 8), uint8_t(code)};
                auto wire = ws::frame(8, body, 2, mask);
                size_t sent = 0;
                curl_easy_send(c.easy, wire.data(), wire.size(), &sent);
            }
        }
    }
    c.local_failure = true;
    finish(c, false, tag);
}
unsigned WebSockets::create(SocketURL address, const std::string &source,
                            const std::vector<std::string> &protocols, bool permitted,
                            uint64_t now) {
    if (connections.size() >= 8 || next_id >= 32)
        return 0;
    auto object = std::make_unique<Connection>();
    auto &c = *object;
    c.id = ++next_id;
    c.address = std::move(address);
    c.source = source;
    c.protocols = protocols;
    c.deadline = now + 10000;
    char origin[384] = {};
    browser_origin(source.c_str(), origin, sizeof(origin));
    std::string source_origin = origin;
    if (source_origin.size() > 4 && source_origin.compare(source_origin.size() - 4, 4, ":443") == 0)
        source_origin.resize(source_origin.size() - 4);
    c.same_origin = !source_origin.empty() && source_origin == c.address.http_origin;
    unsigned id = c.id;
    connections.emplace(id, std::move(object));
    if (!permitted) {
        fail(c, "policy");
        return id;
    }
    if (!multi) {
        fail(c, "init");
        return id;
    }
    std::array<uint8_t, 16> nonce{};
    if (!entropy(nonce.data(), nonce.size())) {
        fail(c, "entropy");
        return id;
    }
    c.nonce = ws::base64(nonce.data(), nonce.size());
    c.request_text =
        "GET " + c.address.target + " HTTP/1.1\r\nHost: " + c.address.host +
        "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + c.nonce +
        "\r\nSec-WebSocket-Version: 13\r\nOrigin: " +
        (source_origin.empty() ? "null" : source_origin) + "\r\n";
    if (!protocols.empty()) {
        c.request_text += "Sec-WebSocket-Protocol: ";
        for (size_t i = 0; i < protocols.size(); i++) {
            if (i)
                c.request_text += ", ";
            c.request_text += protocols[i];
        }
        c.request_text += "\r\n";
    }
    if (c.same_origin) {
        char cookie[8192] = {};
        if (!browser_cookie_network_get(c.address.https.c_str(), cookie, sizeof(cookie))) {
            fail(c, "cookie-limit");
            return id;
        }
        if (cookie[0])
            c.request_text += std::string("Cookie: ") + cookie + "\r\n";
    }
    c.request_text += "\r\n";
    c.easy = curl_easy_init();
    if (!c.easy) {
        fail(c, "init");
        return id;
    }
    curl_easy_setopt(c.easy, CURLOPT_URL, c.address.https.c_str());
    curl_easy_setopt(c.easy, CURLOPT_PRIVATE, &c);
    curl_easy_setopt(c.easy, CURLOPT_CONNECT_ONLY, 1L);
    curl_easy_setopt(c.easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
    curl_easy_setopt(c.easy, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c.easy, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(c.easy, CURLOPT_TIMEOUT_MS, 10000L);
    curl_easy_setopt(c.easy, CURLOPT_NOSIGNAL, 1L);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(c.easy, CURLOPT_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(c.easy, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif
    c.result = c.same_origin ? browser_profile_configure(c.easy)
                             : browser_profile_configure_anonymous(c.easy);
    if (c.result != CURLE_OK || curl_multi_add_handle(multi, c.easy) != CURLM_OK) {
        fail(c, "tls");
        return id;
    }
    c.added = true;
    return id;
}
bool WebSockets::handshake(Connection &c) {
    auto end = c.response_text.find("\r\n\r\n");
    if (end == std::string::npos)
        return false;
    if (end + 4 > 16384) {
        fail(c, "handshake-limit");
        return false;
    }
    std::istringstream stream(c.response_text.substr(0, end));
    std::string line;
    if (!std::getline(stream, line) || line.rfind("HTTP/1.1 101", 0) != 0 ||
        (line.size() > 12 && line[12] != '\r' && line[12] != ' ')) {
        fail(c, "handshake");
        return false;
    }
    std::map<std::string, std::string> headers;
    std::vector<std::string> cookies;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        auto colon = line.find(':');
        if (colon == std::string::npos || !token_ws(line.substr(0, colon))) {
            fail(c, "handshake");
            return false;
        }
        auto name = lower_ws(line.substr(0, colon)), value = trim_ws(line.substr(colon + 1));
        for (unsigned char ch : value)
            if ((ch < 32 && ch != 9) || ch == 127) {
                fail(c, "handshake");
                return false;
            }
        if (name == "set-cookie") {
            if (cookies.size() >= 32) {
                fail(c, "handshake-limit");
                return false;
            }
            cookies.push_back(value);
        }
        auto found = headers.find(name);
        if (found == headers.end())
            headers[name] = value;
        else
            found->second += ", " + value;
    }
    if (!has_token(headers["connection"], "upgrade") ||
        !has_token(headers["upgrade"], "websocket") ||
        headers["sec-websocket-accept"] != ws::accept_key(c.nonce) ||
        !headers["sec-websocket-extensions"].empty()) {
        fail(c, "handshake");
        return false;
    }
    c.protocol = headers["sec-websocket-protocol"];
    if ((!c.protocol.empty() &&
         std::find(c.protocols.begin(), c.protocols.end(), c.protocol) == c.protocols.end()) ||
        (!c.protocols.empty() && c.protocol.empty())) {
        fail(c, "subprotocol");
        return false;
    }
    if (c.same_origin)
        for (auto &value : cookies)
            browser_cookie_network_set(c.address.https.c_str(), value.c_str());
    c.state = Connection::active;
    c.request_text.clear();
    c.request_text.shrink_to_fit();
    Event opened;
    opened.id = c.id;
    opened.kind = Event::open;
    opened.origin = c.address.origin;
    opened.protocol = c.protocol;
    c.events.push_back(std::move(opened));
    auto rest = end + 4;
    if (c.response_text.size() > rest &&
        !c.decoder.feed(reinterpret_cast<const uint8_t *>(c.response_text.data() + rest),
                        c.response_text.size() - rest)) {
        fail(c, "limit", 1009);
        return false;
    }
    c.response_text.clear();
    c.response_text.shrink_to_fit();
    browser_profile_log(c.address.https.c_str(), "websocket-open", 101, CURLE_OK);
    return true;
}
bool WebSockets::send(unsigned id, bool binary, const uint8_t *data, size_t size) {
    auto it = connections.find(id);
    if (it == connections.end() || it->second->state != Connection::active)
        return false;
    auto &c = *it->second;
    if (!queue(c, binary ? 2 : 1, data, size)) {
        fail(c, "send-limit", 1009);
        return false;
    }
    return true;
}
void WebSockets::close(unsigned id, unsigned code, const uint8_t *reason, size_t size,
                       bool connecting, uint64_t now) {
    auto it = connections.find(id);
    if (it == connections.end())
        return;
    auto &c = *it->second;
    if (c.state == Connection::closed || c.state == Connection::closing)
        return;
    if (connecting || c.state != Connection::active) {
        fail(c, "cancelled");
        return;
    }
    std::vector<uint8_t> body;
    if (code) {
        body.push_back(uint8_t(code >> 8));
        body.push_back(uint8_t(code));
    }
    if (size)
        body.insert(body.end(), reason, reason + size);
    if (!queue(c, 8, body.data(), body.size())) {
        fail(c, "send-limit");
        return;
    }
    c.state = Connection::closing;
    c.deadline = now + 5000;
}
void WebSockets::progress(Connection &c, uint64_t now) {
    if (c.state == Connection::closed || c.state == Connection::connecting)
        return;
    if (c.state == Connection::request) {
        size_t sent = 0;
        c.result = curl_easy_send(c.easy, c.request_text.data() + c.request_cursor,
                                  c.request_text.size() - c.request_cursor, &sent);
        if (c.result != CURLE_OK && c.result != CURLE_AGAIN) {
            fail(c, "transport");
            return;
        }
        c.request_cursor += sent;
        if (c.request_cursor < c.request_text.size())
            return;
        c.state = Connection::response;
    }
    if (c.state == Connection::response) {
        uint8_t buffer[4096];
        size_t got = 0;
        c.result = curl_easy_recv(c.easy, buffer, sizeof(buffer), &got);
        if (c.result == CURLE_AGAIN)
            return;
        if (c.result != CURLE_OK || !got) {
            fail(c, "handshake");
            return;
        }
        c.response_text.append(reinterpret_cast<const char *>(buffer), got);
        if (c.response_text.find("\r\n\r\n") == std::string::npos &&
            c.response_text.size() > 16384) {
            fail(c, "handshake-limit");
            return;
        }
        if (!handshake(c))
            return;
    }
    size_t write_budget = 16384;
    while (!c.output.empty() && write_budget) {
        auto &out = c.output.front();
        size_t sent = 0, remaining = std::min(write_budget, out.wire.size() - out.cursor);
        c.result = curl_easy_send(c.easy, out.wire.data() + out.cursor, remaining, &sent);
        if (c.result != CURLE_OK && c.result != CURLE_AGAIN) {
            fail(c, "transport");
            return;
        }
        auto old_payload =
            out.cursor > out.header ? std::min(out.payload, out.cursor - out.header) : 0;
        out.cursor += sent;
        auto new_payload =
            out.cursor > out.header ? std::min(out.payload, out.cursor - out.header) : 0;
        c.queued_bytes -= new_payload - old_payload;
        write_budget -= sent;
        if (out.cursor == out.wire.size()) {
            if (out.opcode == 8)
                c.sent_close = true;
            c.output.pop_front();
        }
        if (!sent || c.result == CURLE_AGAIN)
            break;
    }
    if (c.received_close && c.sent_close) {
        finish(c, !c.local_failure);
        return;
    }
    if (c.events.size() >= 32 || c.event_bytes > 512u * 1024u || receive_bytes > 1536u * 1024u)
        return;
    size_t read_budget = 65536;
    for (unsigned frames = 0; frames < 8;) {
        ws::Event input;
        auto result = c.decoder.next(input);
        if (result == ws::Decoder::error) {
            fail(c, c.decoder.diagnostic.c_str(), c.decoder.close_error);
            return;
        }
        if (result == ws::Decoder::need_data) {
            if (!read_budget)
                break;
            uint8_t buffer[16384];
            size_t got = 0;
            c.result = curl_easy_recv(c.easy, buffer, std::min(sizeof(buffer), read_budget), &got);
            if (c.result == CURLE_AGAIN)
                break;
            if (c.result != CURLE_OK || !got) {
                // An established transport disappearing reports an abnormal close;
                // protocol/handshake failures use the separate error task.
                finish(c, false);
                return;
            }
            read_budget -= got;
            if (!c.decoder.feed(buffer, got)) {
                fail(c, "receive-limit", 1009);
                return;
            }
            continue;
        }
        ++frames;
        if (result == ws::Decoder::consumed)
            continue;
        if (input.kind == ws::Event::ping) {
            if (!queue(c, 10, input.data.data(), input.data.size(), true)) {
                fail(c, "send-limit", 1009);
                return;
            }
            continue;
        }
        if (input.kind == ws::Event::pong)
            continue;
        if (input.kind == ws::Event::close) {
            c.received_close = true;
            c.peer_code = input.code;
            c.peer_reason = std::move(input.reason);
            if (c.state == Connection::active) {
                c.dropped_bytes += c.queued_bytes;
                c.queued_bytes = 0;
                if (!c.output.empty() && c.output.front().cursor) {
                    auto keep = std::move(c.output.front());
                    c.output.clear();
                    c.queued_bytes =
                        keep.payload - (keep.cursor > keep.header
                                            ? std::min(keep.payload, keep.cursor - keep.header)
                                            : 0);
                    c.dropped_bytes -= c.queued_bytes;
                    c.output.push_back(std::move(keep));
                } else
                    c.output.clear();
                if (!queue(c, 8, input.data.data(), input.data.size(), true)) {
                    fail(c, "send-limit");
                    return;
                }
                c.state = Connection::closing;
                c.deadline = now + 5000;
            }
            return;
        }
        if (c.state == Connection::active) {
            if (input.data.size() > 1024u * 1024u - c.event_bytes ||
                input.data.size() > 2u * 1024u * 1024u - receive_bytes) {
                fail(c, "receive-limit", 1009);
                return;
            }
            Event event;
            event.id = c.id;
            event.kind = Event::message;
            event.origin = c.address.origin;
            event.binary = input.kind == ws::Event::binary;
            event.data = std::move(input.data);
            c.event_bytes += event.data.size();
            receive_bytes += event.data.size();
            c.events.push_back(std::move(event));
            if (c.events.size() >= 32 || c.event_bytes > 512u * 1024u ||
                receive_bytes > 1536u * 1024u)
                break;
        }
    }
}
void WebSockets::pump(uint64_t now) {
    if (!multi)
        return;
    int running = 0;
    curl_multi_perform(multi, &running);
    int left = 0;
    while (auto *message = curl_multi_info_read(multi, &left))
        if (message->msg == CURLMSG_DONE) {
            Connection *c = nullptr;
            curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &c);
            if (!c || c->state != Connection::connecting)
                continue;
            c->result = message->data.result;
            if (c->result != CURLE_OK) {
                fail(*c, c->result == CURLE_PEER_FAILED_VERIFICATION ||
                                 c->result == CURLE_SSL_CONNECT_ERROR ||
                                 c->result == CURLE_SSL_CACERT_BADFILE
                             ? "tls"
                             : "transport");
            } else
                c->state = Connection::request;
        }
    auto start = connections.upper_bound(round_robin);
    if (start == connections.end())
        start = connections.begin();
    size_t count = connections.size();
    const auto until = ws_clock() + 8;
    while (count-- && start != connections.end() && ws_clock() < until) {
        auto current = start++;
        if (start == connections.end())
            start = connections.begin();
        auto &c = *current->second;
        round_robin = c.id;
        if (c.state != Connection::active && c.state != Connection::closed && now >= c.deadline) {
            fail(c, "timeout");
            continue;
        }
        progress(c, now);
    }
}
bool WebSockets::pop(Event &event) {
    auto it = connections.upper_bound(event_cursor);
    if (it == connections.end())
        it = connections.begin();
    size_t count = connections.size();
    while (count-- && it != connections.end()) {
        auto current = it++;
        if (it == connections.end())
            it = connections.begin();
        auto &c = *current->second;
        if (c.events.empty())
            continue;
        event_cursor = c.id;
        event = std::move(c.events.front());
        c.events.pop_front();
        if (event.kind == Event::message) {
            c.event_bytes -= event.data.size();
            receive_bytes -= event.data.size();
        }
        if (event.kind == Event::close) {
            receive_bytes -= c.event_bytes;
            connections.erase(current);
        }
        return true;
    }
    return false;
}
} // namespace visual
