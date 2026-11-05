#include "interaction.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)
int main() {
    visual::View v;
    v.content_height = 2400;
    visual::Gestures g(v);
    g.down(1, {100, 100}, 0);
    g.move(1, {104, 103}, 10);
    CHECK(g.up(1, {104, 103}, 50));
    CHECK(v.y == 0);
    g.down(1, {100, 400}, 100);
    g.move(1, {100, 200}, 150);
    CHECK(!g.up(1, {100, 200}, 160));
    CHECK(v.y == 200);
    g.tick(.02f);
    CHECK(v.y > 200);
    g.cancel();
    float old = v.y;
    g.tick(.02f);
    CHECK(v.y == old);
    g.down(1, {100, -40}, 200);
    g.move(1, {100, -80}, 210);
    CHECK(!g.up(1, {100, -80}, 220));
    CHECK(v.y == old);
    g.cancel();
    v.x = v.y = 0;
    g.down(1, {300, 300}, 300);
    g.down(2, {500, 300}, 300);
    g.move(2, {700, 300}, 320);
    CHECK(v.zoom == 2);
    CHECK(!g.up(2, {700, 300}, 330));
    CHECK(!g.up(1, {300, 300}, 340));
    CHECK(std::fabs(v.x - 150) < .01f && std::fabs(v.y - 150) < .01f);
    auto p = v.document({500, 300});
    CHECK(std::fabs(p.x - 400) < .01f && std::fabs(p.y - 300) < .01f);
    old = v.y;
    g.tick(.02f);
    CHECK(v.y == old);
    v.pinch({500, 300}, {500, 300}, 100);
    CHECK(v.zoom == 2.5f);
    v.pinch({500, 300}, {500, 300}, .001f);
    CHECK(v.zoom == 1 && v.x == 0);
    v.y = 99999;
    v.clamp();
    CHECK(v.y == 1764);
    v.drag(0, 99999);
    CHECK(v.y == 0);
    g.down(1, {100, 100}, 400);
    g.down(2, {300, 100}, 400);
    g.down(3, {500, 100}, 400);
    CHECK(!g.up(3, {500, 100}, 450));
    CHECK(!g.up(2, {300, 100}, 450));
    CHECK(!g.up(1, {100, 100}, 450));
    g.down(1, {100, 100}, 500);
    g.cancel();
    CHECK(!g.up(1, {100, 100}, 510));
    g.down(1, {100, 100}, 600);
    CHECK(!g.up(1, {100, 100}, 1400));
    puts("PASS taps, drag suppression, inertia, chrome isolation, pinch anchor, bounds, "
         "multi-touch and cancellation");
}
