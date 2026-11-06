#include "container.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)
int main(int argc, char **argv) {
    CHECK(argc == 4);
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    CHECK(TTF_Init() == 0);
    auto *s = SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_RGBA32);
    CHECK(s);
    auto *r = SDL_CreateSoftwareRenderer(s);
    CHECK(r);
    {
        visual::Resources resources(argv[1]);
        visual::Container c(r, resources, argv[2]);
        std::ifstream html(std::string(argv[3]) + "/flex-gaps.html");
        CHECK(html);
        std::string source((std::istreambuf_iterator<char>(html)), {});
        std::ifstream reference(std::string(argv[3]) + "/flex-gaps.tsv");
        CHECK(reference);
        std::string row;
        int previous = 0, count = 0, failures = 0;
        litehtml::document::ptr doc;
        while (std::getline(reference, row)) {
            if (row.empty() || row[0] == '#')
                continue;
            std::istringstream line(row);
            int width;
            std::string id;
            float x, y, w, h;
            CHECK(line >> width >> id >> x >> y >> w >> h);
            if (width != previous) {
                doc.reset();
                c.reset();
                c.viewport_width = width;
                doc = litehtml::document::createFromString(source, &c);
                doc->render(width);
                previous = width;
            }
            auto el = doc->root()->select_one("#" + id);
            CHECK(el);
            auto a = el->get_placement(), b = el->parent()->get_placement();
            if (std::abs(float(a.x - b.x) - x) > .1f || std::abs(float(a.y - b.y) - y) > .1f ||
                std::abs(float(a.width) - w) > .1f || std::abs(float(a.height) - h) > .1f) {
                fprintf(stderr, "%d %s expected %.2f %.2f %.2f %.2f actual %.2f %.2f %.2f %.2f\n",
                        width, id.c_str(), x, y, w, h, float(a.x - b.x), float(a.y - b.y),
                        float(a.width), float(a.height));
                failures++;
            }
            count++;
        }
        CHECK(count == 120);
        CHECK(failures == 0);
    }
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(s);
    TTF_Quit();
    SDL_Quit();
    puts("PASS 120 Chromium reference rectangles: row/column, reverse/wrap, grow/shrink, auto "
         "margins, percentages, units, cascade and responsive gaps");
}
