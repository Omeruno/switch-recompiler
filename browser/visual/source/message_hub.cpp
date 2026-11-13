#include "message_hub.h"
#include <algorithm>
#include <set>
#include <stdexcept>
namespace visual {
uint32_t MessageHub::create_window(uint32_t parent) {
    if (parent && (!window(parent) || !window(parent)->alive))
        throw std::invalid_argument("Closed parent window");
    if (next_window >= 64 || (parent && active_frames() >= 4))
        throw std::length_error("Window quota");
    Window entry;
    entry.parent = parent;
    const auto id = next_window + 1;
    windows.emplace(id, std::move(entry));
    next_window = id;
    return id;
}
MessageHub::Window *MessageHub::window(uint32_t id) {
    auto p = windows.find(id);
    return p == windows.end() ? nullptr : &p->second;
}
const MessageHub::Window *MessageHub::window(uint32_t id) const {
    auto p = windows.find(id);
    return p == windows.end() ? nullptr : &p->second;
}
void MessageHub::bind(uint32_t id, Container *c, const std::string &origin) {
    auto *w = window(id);
    if (!w || !w->alive || w->container)
        throw std::invalid_argument("Unavailable window");
    w->origin = origin.empty() ? "null" : origin;
    w->container = c;
}
void MessageHub::discard_transfers(const MessagePacket &m) {
    for (auto ref : m.ports) {
        auto p = ports.find(ref.id);
        if (p != ports.end() && !p->second.closed && p->second.owner == 0 &&
            p->second.epoch == ref.epoch) {
            p->second.closed = true; // Mark before following transfer cycles.
            discard(p->second.queue);
        }
    }
}
void MessageHub::discard(std::deque<MessagePacket> &q) {
    while (!q.empty()) {
        auto m = std::move(q.front());
        q.pop_front();
        queued_bytes -= m.data.size();
        --queued_messages;
        discard_transfers(m);
    }
}
void MessageHub::navigate(uint32_t id) {
    auto *w = window(id);
    if (!w || !w->alive)
        return;
    for (auto child : children(id))
        close_window(child);
    discard(w->queue);
    ++w->generation;
    w->container = nullptr;
    w->origin = "null";
    for (auto &p : ports)
        if (p.second.owner == id) {
            p.second.closed = true;
            p.second.started = false;
            discard(p.second.queue);
        }
}
void MessageHub::close_window(uint32_t id) {
    navigate(id);
    if (auto *w = window(id))
        w->alive = false;
}
uint32_t MessageHub::top(uint32_t id) const {
    for (unsigned i = 0; i < 64; ++i) {
        auto *w = window(id);
        if (!w || !w->parent)
            return id;
        id = w->parent;
    }
    return id;
}
std::vector<std::string> MessageHub::ancestors(uint32_t id) const {
    std::vector<std::string> out;
    for (unsigned i = 0; i < 64; ++i) {
        auto *w = window(id);
        if (!w || !w->parent)
            break;
        id = w->parent;
        auto *p = window(id);
        if (!p)
            break;
        out.push_back(p->origin);
    }
    return out;
}
std::vector<uint32_t> MessageHub::children(uint32_t id) const {
    std::vector<uint32_t> out;
    for (const auto &p : windows)
        if (p.second.alive && p.second.parent == id)
            out.push_back(p.first);
    return out;
}
size_t MessageHub::active_frames() const {
    size_t n = 0;
    for (const auto &p : windows)
        n += p.second.alive && p.second.parent != 0;
    return n;
}
std::vector<PortRef> MessageHub::channel(uint32_t owner) {
    auto *w = window(owner);
    if (!w || !w->alive || next_port >= 128)
        throw std::length_error("Channel quota");
    const auto a = next_port + 1, b = next_port + 2;
    Port first, second;
    first.peer = b;
    second.peer = a;
    first.owner = second.owner = owner;
    first.generation = second.generation = w->generation;
    std::vector<PortRef> result{{a, 1}, {b, 1}};
    ports.emplace(a, std::move(first));
    try {
        ports.emplace(b, std::move(second));
    } catch (...) {
        ports.erase(a);
        throw;
    }
    next_port = b;
    return result;
}
bool MessageHub::port_owned(uint32_t owner, PortRef r) const {
    auto p = ports.find(r.id);
    auto *w = window(owner);
    return w && w->alive && p != ports.end() && !p->second.closed && p->second.owner == owner &&
           p->second.generation == w->generation && p->second.epoch == r.epoch;
}
bool MessageHub::port_transferable(uint32_t owner, PortRef ref) const {
    auto *w = window(owner);
    auto p = ports.find(ref.id);
    return w && w->alive && p != ports.end() && p->second.owner == owner &&
           p->second.generation == w->generation && p->second.epoch == ref.epoch;
}
void MessageHub::port_start(uint32_t owner, PortRef r) {
    if (port_owned(owner, r))
        ports.at(r.id).started = true;
}
void MessageHub::port_close(uint32_t owner, PortRef r) {
    if (!port_owned(owner, r))
        return;
    auto &p = ports.at(r.id);
    p.closed = true;
    p.started = false;
    auto peer = ports.find(p.peer);
    if (peer != ports.end() && peer->second.peer == r.id)
        peer->second.peer = 0;
    p.peer = 0;
    discard(p.queue);
}
void MessageHub::validate_transfer(uint32_t source, const std::vector<PortRef> &list,
                                   uint32_t forbidden, uint32_t other) const {
    if (list.size() > 64)
        throw std::length_error("Transfer quota");
    std::set<uint32_t> ids;
    for (auto r : list)
        if (r.id == forbidden || r.id == other || !ids.insert(r.id).second ||
            !port_transferable(source, r))
            throw std::invalid_argument("Invalid port transfer");
}
void MessageHub::commit_transfer(const std::vector<PortRef> &list) noexcept {
    for (auto r : list) {
        auto &p = ports.find(r.id)->second;
        p.owner = 0;
        p.closed = false; // A transferred closed port becomes a fresh disentangled endpoint.
        p.started = false;
        ++p.epoch;
    }
}
std::vector<PortRef> MessageHub::clone_ports(uint32_t owner, const std::vector<PortRef> &list) {
    validate_transfer(owner, list, 0);
    std::vector<PortRef> result;
    for (auto r : list)
        result.push_back({r.id, r.epoch + 1});
    commit_transfer(list);
    for (auto r : result)
        ports.at(r.id).owner = owner;
    return result;
}
void MessageHub::quota(size_t bytes, size_t entries) const {
    if (bytes > packet_limit || bytes > byte_limit - queued_bytes || queued_messages >= 256 ||
        entries >= 64)
        throw std::length_error("Message quota");
}
bool MessageHub::post_window(uint32_t source, uint32_t target, std::string origin,
                             std::vector<uint8_t> data, const std::vector<PortRef> &list) {
    auto *s = window(source), *t = window(target);
    validate_transfer(source, list, 0);
    if (!s || !s->alive || !t || !t->alive)
        return false;
    quota(data.size(), t->queue.size());
    MessagePacket p{source,    s->generation,     t->generation,   0, 0,
                    s->origin, std::move(origin), std::move(data), {}};
    for (auto r : list)
        p.ports.push_back({r.id, r.epoch + 1});
    const auto bytes = p.data.size();
    t->queue.push_back(std::move(p)); // All allocation completes before ownership changes.
    commit_transfer(list);
    queued_bytes += bytes;
    ++queued_messages;
    return true;
}
bool MessageHub::post_port(uint32_t source, PortRef ref, std::vector<uint8_t> data,
                           const std::vector<PortRef> &list) {
    if (!port_owned(source, ref))
        return false;
    auto &sender = ports.at(ref.id);
    validate_transfer(source, list, ref.id, sender.peer);
    auto found = ports.find(sender.peer);
    if (found == ports.end() || found->second.closed) {
        // An open endpoint still serializes and transfers, even if its peer closed.
        if (data.size() > packet_limit)
            throw std::length_error("Message quota");
        commit_transfer(list);
        for (auto r : list) {
            auto &p = ports.at(r.id);
            p.closed = true;
            discard(p.queue);
        }
        return true;
    }
    auto &target = found->second;
    quota(data.size(), target.queue.size());
    MessagePacket p{
        source, window(source)->generation, 0, sender.peer, target.epoch, "", "", std::move(data),
        {}};
    for (auto r : list)
        p.ports.push_back({r.id, r.epoch + 1});
    const auto bytes = p.data.size();
    target.queue.push_back(std::move(p));
    commit_transfer(list);
    queued_bytes += bytes;
    ++queued_messages;
    return true;
}
bool MessageHub::valid_packet(const MessagePacket &p) const {
    auto *s = window(p.source);
    if (!s || !s->alive || s->generation != p.source_generation)
        return false;
    for (auto ref : p.ports) {
        auto it = ports.find(ref.id);
        if (it == ports.end() || it->second.closed || it->second.owner != 0 ||
            it->second.epoch != ref.epoch)
            return false;
    }
    return true;
}
bool MessageHub::pop(uint32_t owner, MessagePacket &out) {
    auto *w = window(owner);
    if (!w || !w->alive)
        return false;
    auto take = [&](std::deque<MessagePacket> &q, bool win) {
        while (!q.empty()) {
            auto p = std::move(q.front());
            q.pop_front();
            queued_bytes -= p.data.size();
            --queued_messages;
            const bool same_opaque = p.target_origin == "/" && p.source == owner;
            if (!valid_packet(p) ||
                (win && (p.target_generation != w->generation ||
                         !(p.target_origin == "*" || same_opaque ||
                           (w->origin != "null" && p.target_origin == w->origin))))) {
                discard_transfers(p);
                continue;
            }
            for (auto ref : p.ports) {
                auto &port = ports.at(ref.id);
                port.owner = owner;
                port.generation = w->generation;
                port.started = false;
            }
            out = std::move(p);
            return true;
        }
        return false;
    };
    auto take_port = [&]() {
        auto start = ports.upper_bound(port_cursor);
        for (size_t i = 0; i < ports.size(); ++i) {
            if (start == ports.end())
                start = ports.begin();
            auto it = start++;
            auto &p = it->second;
            if (p.owner == owner && p.generation == w->generation && !p.closed && p.started &&
                take(p.queue, false)) {
                port_cursor = it->first;
                out.port_epoch = p.epoch;
                return true;
            }
        }
        return false;
    };
    bool taken = prefer_ports ? (take_port() || take(w->queue, true))
                              : (take(w->queue, true) || take_port());
    if (taken)
        prefer_ports = !prefer_ports;
    return taken;
}
} // namespace visual
