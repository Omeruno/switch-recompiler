#pragma once
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>
namespace visual {
class Container;
struct PortRef {
    uint32_t id = 0, epoch = 0;
};
struct MessagePacket {
    uint32_t source = 0, source_generation = 0, target_generation = 0;
    uint32_t port = 0, port_epoch = 0;
    std::string origin, target_origin;
    std::vector<uint8_t> data;
    std::vector<PortRef> ports;
};
// One bounded broker per browsing-context group. No JS values cross runtimes.
class MessageHub {
  public:
    struct Window {
        uint32_t parent = 0, generation = 1;
        Container *container = nullptr;
        std::string origin = "null";
        bool alive = true;
        std::deque<MessagePacket> queue;
    };

  private:
    struct Port {
        uint32_t peer = 0, owner = 0, generation = 0, epoch = 1;
        bool closed = false, started = false;
        std::deque<MessagePacket> queue;
    };
    std::map<uint32_t, Window> windows;
    std::map<uint32_t, Port> ports;
    uint32_t next_window = 0, next_port = 0, port_cursor = 0;
    size_t queued_bytes = 0, queued_messages = 0;
    bool prefer_ports = false;
    void discard(std::deque<MessagePacket> &);
    void discard_transfers(const MessagePacket &);
    void validate_transfer(uint32_t, const std::vector<PortRef> &, uint32_t, uint32_t = 0) const;
    void commit_transfer(const std::vector<PortRef> &) noexcept;
    bool valid_packet(const MessagePacket &) const;
    void quota(size_t, size_t) const;

  public:
    size_t frame_pixels = 0; // Shared render-target budget, native code only.
    static constexpr size_t packet_limit = 256u * 1024u, byte_limit = 2u * 1024u * 1024u;
    uint32_t create_window(uint32_t parent = 0);
    Window *window(uint32_t);
    const Window *window(uint32_t) const;
    void bind(uint32_t, Container *, const std::string &);
    void navigate(uint32_t);
    void close_window(uint32_t);
    uint32_t top(uint32_t) const;
    std::vector<std::string> ancestors(uint32_t) const;
    std::vector<uint32_t> children(uint32_t) const;
    std::vector<PortRef> channel(uint32_t);
    std::vector<PortRef> clone_ports(uint32_t, const std::vector<PortRef> &);
    bool port_owned(uint32_t, PortRef) const;
    bool port_transferable(uint32_t, PortRef) const;
    void port_start(uint32_t, PortRef);
    void port_close(uint32_t, PortRef);
    bool post_window(uint32_t, uint32_t, std::string, std::vector<uint8_t>,
                     const std::vector<PortRef> &);
    bool post_port(uint32_t, PortRef, std::vector<uint8_t>, const std::vector<PortRef> &);
    bool pop(uint32_t, MessagePacket &);
    size_t active_frames() const;
};
} // namespace visual
