#include "container.h"
#include <cstdio>
#include <cstdlib>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)
int main(int argc, char **argv) {
    CHECK(argc == 3);
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    CHECK(TTF_Init() == 0);
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_RGBA32);
    CHECK(surface);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
    CHECK(renderer);
    {
        visual::Resources resources(argv[1]);
        visual::Container c(renderer, resources, argv[2]);
        auto pixel = [&](int x, int y) {
            uint8_t *p = static_cast<uint8_t *>(surface->pixels) + y * surface->pitch + x * 4;
            return litehtml::web_color(p[0], p[1], p[2], p[3]);
        };
        auto clear = [&] {
            SDL_RenderSetClipRect(renderer, nullptr);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderClear(renderer);
            c.begin_paint();
        };
        litehtml::background_layer layer;
        layer.origin_box = layer.border_box = layer.clip_box = {0, 0, 1280, 636};
        layer.repeat = litehtml::background_repeat_no_repeat;
        litehtml::background_layer::linear_gradient linear;
        linear.start = {0, 0};
        linear.end = {1280, 0};
        linear.color_points = {{0, {255, 0, 0}}, {1, {0, 0, 255}}};
        clear();
        c.draw_linear_gradient(0, layer, linear);
        c.end_paint();
        SDL_RenderPresent(renderer);
        CHECK(pixel(100, 200).red > 220 && pixel(100, 200).blue < 35);
        CHECK(pixel(1180, 200).blue > 220 && pixel(1180, 200).red < 35);
        CHECK(pixel(640, 200).red > 115 && pixel(640, 200).blue > 115);
        CHECK(pixel(640, 20) == litehtml::web_color(255, 255, 255));
        linear.color_points = {{0, {255, 0, 0, 255}}, {1, {0, 0, 255, 0}}};
        clear();
        c.draw_linear_gradient(0, layer, linear);
        c.end_paint();
        SDL_RenderPresent(renderer);
        CHECK(pixel(640, 200).red > 245 && pixel(640, 200).green > 115 &&
              pixel(640, 200).green < 140);
        litehtml::background_layer::radial_gradient radial;
        radial.position = {640, 318};
        radial.radius = {200, 100};
        radial.color_points = {{0, {255, 0, 0}}, {1, {0, 0, 255}}};
        clear();
        c.draw_radial_gradient(0, layer, radial);
        c.end_paint();
        SDL_RenderPresent(renderer);
        CHECK(pixel(640, 402).red > 245);
        CHECK(pixel(900, 402).blue > 245);
        litehtml::background_layer::conic_gradient conic;
        conic.position = {640, 318};
        conic.angle = 0;
        conic.color_points = {{0, {255, 0, 0}},
                              {.25f, {0, 255, 0}},
                              {.5f, {0, 0, 255}},
                              {.75f, {255, 255, 255}},
                              {1, {255, 0, 0}}};
        clear();
        c.draw_conic_gradient(0, layer, conic);
        c.end_paint();
        SDL_RenderPresent(renderer);
        CHECK(pixel(640, 202).red > 245);
        CHECK(pixel(840, 402).green > 245);
        CHECK(pixel(640, 602).blue > 245);
        CHECK(pixel(440, 402).red > 245 && pixel(440, 402).blue > 245);
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    TTF_Quit();
    SDL_Quit();
    puts("PASS gradient directions, premultiplied transparency, radial aspect, conic angles and "
         "viewport clip");
}
