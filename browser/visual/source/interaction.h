#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
namespace visual {
// Dead zone and quadratic response: precision near center, fast at full throw.
inline float stick_scroll_speed(float value) {
    float a = std::min(1.f, std::fabs(value));
    if (a <= .18f)
        return 0;
    float t = (a - .18f) / .82f;
    return std::copysign(2200.f * t * t, value);
}
struct Point {
    float x = 0, y = 0;
};
struct View {
    float x = 0, y = 0, zoom = 1, base_scale = 1, content_width = 1280, content_height = 636;
    float scale() const { return zoom * base_scale; }
    void clamp() {
        x = std::clamp(x, 0.f, std::max(0.f, content_width - 1280 / scale()));
        y = std::clamp(y, 0.f, std::max(0.f, content_height - 636 / scale()));
    }
    Point document(Point p) const { return {x + p.x / scale(), y + p.y / scale()}; }
    void drag(float dx, float dy) {
        x -= dx / scale();
        y -= dy / scale();
        clamp();
    }
    void pinch(Point old_center, Point new_center, float ratio) {
        auto anchor = document(old_center);
        zoom = std::clamp(zoom * ratio, 1.f, 2.5f);
        x = anchor.x - new_center.x / scale();
        y = anchor.y - new_center.y / scale();
        clamp();
    }
};
class Gestures {
    struct Contact {
        Point start, last;
        uint32_t began, time;
    };
    std::map<int64_t, Contact> contacts;
    bool suppressed = false, dragging = false;
    float vx = 0, vy = 0;
    static float distance(Point a, Point b) { return std::hypot(a.x - b.x, a.y - b.y); }

  public:
    View &view;
    explicit Gestures(View &v) : view(v) {}
    bool active() const { return !contacts.empty(); }
    void stop_inertia() { vx = vy = 0; }
    void cancel() {
        contacts.clear();
        suppressed = dragging = false;
        vx = vy = 0;
    }
    void down(int64_t id, Point p, uint32_t now) {
        vx = vy = 0;
        if (contacts.empty()) {
            suppressed = false;
            dragging = false;
        }
        if (contacts.size() < 10)
            contacts.emplace(id, Contact{p, p, now, now});
        if (contacts.size() > 1)
            suppressed = true;
    }
    void move(int64_t id, Point p, uint32_t now) {
        auto it = contacts.find(id);
        if (it == contacts.end())
            return;
        Contact &c = it->second;
        Point previous = c.last;
        if (contacts.size() == 2) {
            auto a = contacts.begin(), b = std::next(a);
            Point olda = a->second.last, oldb = b->second.last;
            c.last = p;
            Point newa = a->second.last, newb = b->second.last;
            float d = distance(olda, oldb), nd = distance(newa, newb);
            if (a->second.start.y >= 0 && b->second.start.y >= 0 && d > 8 && nd > 8)
                view.pinch({(olda.x + oldb.x) / 2, (olda.y + oldb.y) / 2},
                           {(newa.x + newb.x) / 2, (newa.y + newb.y) / 2}, nd / d);
        } else if (contacts.size() == 1) {
            if (distance(c.start, p) > 12)
                dragging = true;
            if (dragging) {
                // Finger gestures on browser chrome must never pan page content.
                if (c.start.y >= 0) {
                    view.drag(p.x - previous.x, p.y - previous.y);
                    float dt = std::max(4u, now - c.time) / 1000.f;
                    vx = std::clamp((p.x - previous.x) / dt, -2500.f, 2500.f);
                    vy = std::clamp((p.y - previous.y) / dt, -2500.f, 2500.f);
                }
            }
            c.last = p;
        }
        c.time = now;
    }
    std::optional<Point> up(int64_t id, Point p, uint32_t now) {
        auto it = contacts.find(id);
        if (it == contacts.end())
            return {};
        bool tap = contacts.size() == 1 && !suppressed && !dragging &&
                   distance(it->second.start, p) <= 12 && now - it->second.began < 700;
        if (now - it->second.time > 100)
            vx = vy = 0;
        contacts.erase(it);
        if (!contacts.empty() || suppressed)
            vx = vy = 0;
        return tap ? std::optional<Point>(p) : std::nullopt;
    }
    void tick(float seconds) {
        if (active())
            return;
        seconds = std::clamp(seconds, 0.f, .05f);
        view.drag(vx * seconds, vy * seconds);
        float decay = std::exp(-7 * seconds);
        vx *= decay;
        vy *= decay;
        if (std::fabs(vx) < 2)
            vx = 0;
        if (std::fabs(vy) < 2)
            vy = 0;
    }
};
} // namespace visual
