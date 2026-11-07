#include "scripts.h"
extern "C" {
#include "profile.h"
}
#include <cstdio>
#include <cstdlib>
#include <fstream>
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
    CHECK(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
    CHECK(browser_profile_open(nullptr, nullptr));
    auto *surface = SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_RGBA32);
    CHECK(surface);
    auto *renderer = SDL_CreateSoftwareRenderer(surface);
    CHECK(renderer);
    {
        visual::Resources resources(argv[1]);
        visual::Container c(renderer, resources, argv[2]);
        c.base = c.page_url = "https://example.test/";
        auto doc =
            litehtml::document::createFromString("<html><head></head><body></body></html>", &c);
        doc->render(1280);
        visual::Scripts js(doc, c, resources);
        auto now = visual::Scripts::clock_ms();
        for (int i = 0; i < 12; i++)
            js.poll(now + i * 20);
        for (const char *name : {"wasm-binaries.js", "wasm-bridge.js"}) {
            std::ifstream file(std::string(argv[3]) + "/" + name);
            CHECK(file);
            std::string text((std::istreambuf_iterator<char>(file)), {});
            if (!js.eval(text, 5000)) {
                fprintf(stderr, "%s: %s\n", name, js.error().c_str());
                return 1;
            }
        }
        CHECK(js.eval("if(!globalThis.wasmBridgeDone)throw Error('Wasm bridge unfinished');", 500));
    }
    for (const auto &policy :
         {std::string("script-src 'self'"), std::string("script-src 'self' 'wasm-unsafe-eval'"),
          std::string("script-src 'self' 'unsafe-eval'"),
          std::string("script-src 'self' 'nonce-eHl6' 'wasm-unsafe-eval'")}) {
        visual::Resources resources(argv[1]);
        visual::Container c(renderer, resources, argv[2]);
        c.base = c.page_url = "https://example.test/";
        auto doc = litehtml::document::createFromString("<html><body></body></html>", &c);
        doc->render(1280);
        visual::Scripts js(doc, c, resources, {policy});
        std::ifstream file(std::string(argv[3]) + "/wasm-binaries.js");
        CHECK(file);
        CHECK(js.eval(std::string((std::istreambuf_iterator<char>(file)), {}), 1000));
        bool allowed = policy.find("unsafe-eval") != std::string::npos;
        CHECK(js.eval(
            std::string("let compiled=false;try{new WebAssembly.Module(new "
                        "Uint8Array(wasmBinaries['game-core']));compiled=true;}catch(e){if(!(e "
                        "instanceof WebAssembly.CompileError))throw e;}if(compiled!==") +
                (allowed ? "true" : "false") + ")throw Error('Wasm CSP');",
            1000));
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    browser_profile_close();
    curl_global_cleanup();
    TTF_Quit();
    SDL_Quit();
    puts("PASS real JS/Wasm numeric imports/exports, start, bounded loops, traps, zero-copy "
         "memory/grow, globals, tables and custom sections");
}
