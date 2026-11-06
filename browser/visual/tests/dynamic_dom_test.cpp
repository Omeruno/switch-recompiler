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
        auto doc = litehtml::document::createFromString(
            "<!doctype html><html><head></head><body></body></html>", &c);
        doc->render(1280);
        visual::Scripts js(doc, c, resources);
        uint64_t now = visual::Scripts::clock_ms();
        for (int i = 0; i < 8 && !js.ready(); ++i)
            js.poll(now + i);
        std::ifstream input(std::string(argv[3]) + "/dynamic-dom.js");
        CHECK(input);
        std::string source((std::istreambuf_iterator<char>(input)), {});
        CHECK(js.eval(source));
        for (int i = 0; i < 50; ++i) {
            js.poll(now + 100 + i * 20);
            js.render_frame(now + 100 + i * 20);
        }
        if (!js.eval("if(!globalThis.dynamicFixtureDone||globalThis.dynamicFixtureError)throw "
                     "Error(globalThis.dynamicFixtureError||'fixture unfinished');")) {
            fprintf(stderr, "JS: %s\n", js.error().c_str());
            return 1;
        }
        js.flush();
        // A bounded observer loop must not create an unbounded native queue.
        CHECK(js.eval("globalThis.limitObserver=new "
                      "MutationObserver(()=>{});limitObserver.observe(document.body,{attributes:"
                      "true});for(let "
                      "i=0;i<400;i++)document.body.setAttribute('data-quota',i);globalThis."
                      "quotaRecords=limitObserver.takeRecords().length;limitObserver.disconnect();"
                      "if(quotaRecords!==256)throw Error('observer quota');",
                      500));
        bool limit = false;
        for (const auto &issue : js.issues().entries())
            if (issue.category == "limit" && issue.code == "mutation-observer")
                limit = true;
        CHECK(limit);
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    browser_profile_close();
    curl_global_cleanup();
    TTF_Quit();
    SDL_Quit();
    puts("PASS native mutation records, old values, filters/subtrees, transient observations, "
         "queue/disconnect, inline script activation and inert innerHTML");
}
