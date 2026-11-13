#include "message_hub.h"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)
int main() {
    using namespace visual;
    MessageHub hub;
    auto root = hub.create_window(), child = hub.create_window(root);
    hub.bind(root, nullptr, "https://root.test");
    hub.bind(child, nullptr, "https://child.test");
    MessagePacket out;
    CHECK(hub.post_window(child, root, "https://wrong.test", {1}, {}));
    CHECK(!hub.pop(root, out));
    auto ch = hub.channel(child);
    CHECK(hub.post_port(child, ch[0], {9}, {}));
    CHECK(!hub.pop(child, out));
    CHECK(hub.post_window(child, root, "https://root.test", {7}, {ch[1]}));
    CHECK(!hub.port_owned(child, ch[1]));
    hub.port_close(child, ch[1]);
    CHECK(hub.pop(root, out) && out.origin == "https://child.test" && out.source == child &&
          out.ports.size() == 1);
    const auto moved = out.ports[0];
    CHECK(hub.port_owned(root, moved));
    CHECK(!hub.pop(root, out));
    hub.port_start(root, moved);
    CHECK(hub.pop(root, out) && out.data == std::vector<uint8_t>{9} &&
          out.port_epoch == moved.epoch);
    // Native origin is rechecked on delivery, after navigation.
    CHECK(hub.post_window(child, root, "https://root.test", {1}, {}));
    hub.navigate(root);
    hub.bind(root, nullptr, "https://elsewhere.test");
    CHECK(!hub.pop(root, out));
    CHECK(!hub.window(child)->alive);
    auto other = hub.create_window(root);
    hub.bind(other, nullptr, "https://other.test");
    auto keep = hub.channel(other);
    bool failed = false;
    try {
        hub.post_window(other, root, "*", {1}, {keep[0], keep[0]});
    } catch (const std::invalid_argument &) {
        failed = true;
    }
    CHECK(failed && hub.port_owned(other, keep[0]));
    for (int i = 0; i < 64; ++i)
        CHECK(hub.post_window(other, root, "*", {1}, {}));
    failed = false;
    try {
        hub.post_window(other, root, "*", {1}, {keep[0]});
    } catch (const std::length_error &) {
        failed = true;
    }
    CHECK(failed && hub.port_owned(other, keep[0]));
    while (hub.pop(root, out)) {
    }
    // A dropped message closes its in-transit endpoints and queued transfers.
    auto nested = hub.channel(other);
    CHECK(hub.post_port(other, keep[0], {1}, {nested[0]}));
    CHECK(hub.post_window(other, root, "https://mismatch.test", {1}, {keep[1]}));
    CHECK(!hub.pop(root, out));
    CHECK(hub.post_port(other, nested[1], {1}, {}));
    CHECK(!hub.pop(root, out));
    // Global bytes and per-message bounds, followed by recovery.
    std::vector<uint8_t> large(MessageHub::packet_limit, 42);
    for (int i = 0; i < 8; ++i)
        CHECK(hub.post_window(other, root, "*", large, {}));
    failed = false;
    try {
        hub.post_window(other, root, "*", {1}, {});
    } catch (const std::length_error &) {
        failed = true;
    }
    CHECK(failed);
    while (hub.pop(root, out)) {
    }
    CHECK(hub.post_window(other, root, "*", {1}, {}));
    CHECK(hub.pop(root, out));
    puts("PASS broker ownership epochs, start, queued transfer, origin, lifecycle, quotas and "
         "recovery");
}
