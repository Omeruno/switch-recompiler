#include "scripts.h"
extern "C" {
#include "profile.h"
}
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)
int main(int argc, char **argv) {
    CHECK(argc == 7);
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    CHECK(TTF_Init() == 0);
    CHECK(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
    CHECK(browser_profile_open(nullptr, argv[5]));
    auto *surface = SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_RGBA32);
    CHECK(surface);
    auto *renderer = SDL_CreateSoftwareRenderer(surface);
    CHECK(renderer);
    {
        visual::Resources resources(TEST_ASSETS);
        auto *config = resources.request(std::string(argv[2]) + "/api-config", 4096);
        auto *cookies = resources.request(std::string(argv[2]) + "/set", 4096);
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (resources.pending() && std::chrono::steady_clock::now() < end) {
            resources.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(config->transfer_ok && cookies->transfer_ok);
        visual::Container c(renderer, resources, TEST_FONT);
        c.base = c.page_url = std::string(argv[2]) + "/api-page";
        auto doc = litehtml::document::createFromString(
            "<!doctype html><html><head></head><body></body></html>", &c);
        doc->render(1280);
        visual::Scripts js(doc, c, resources);
        auto run = [&](const std::string &code) {
            if (!js.eval(code, 500)) {
                fprintf(stderr, "JS: %s\n", js.error().c_str());
                std::exit(1);
            }
        };
        auto drive = [&]() {
            auto until = std::chrono::steady_clock::now() + std::chrono::seconds(6);
            unsigned turns = 0;
            do {
                resources.pump();
                js.poll(visual::Scripts::clock_ms());
                js.flush();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                ++turns;
            } while ((resources.pending() || !js.ready() || turns < 10) &&
                     std::chrono::steady_clock::now() < until);
            CHECK(!resources.pending() && js.ready());
        };
        drive();
        run("globalThis.config=" + std::string(config->data.begin(), config->data.end()) +
            ";globalThis.cross=config.cross;");
        CHECK(browser_cookie_network_set(c.page_url.c_str(), "hidden=1; Secure; HttpOnly; Path=/"));
        std::ifstream input(TEST_FIXTURES "/websocket.js");
        CHECK(input);
        run(std::string((std::istreambuf_iterator<char>(input)), {}));
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(12);
        while (!doc->root()->get_attr("data-network-done") &&
               !doc->root()->get_attr("data-network-error") &&
               std::chrono::steady_clock::now() < deadline) {
            resources.pump();
            js.poll(visual::Scripts::clock_ms());
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        run("if(globalThis.websocketError||!globalThis.websocketDone)throw "
            "Error(globalThis.websocketError||'websocket unfinished');");
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    browser_profile_close();
    curl_global_cleanup();
    TTF_Quit();
    SDL_Quit();
    puts("PASS native TLS WebSocket text/binary, framing, protocol, cookies, close, errors, "
         "cancellation, Blob and event loop");
}
