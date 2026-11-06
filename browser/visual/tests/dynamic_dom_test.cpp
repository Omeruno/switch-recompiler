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
            "<!doctype html><html><head></head><body><noscript id=warning><p>Enable "
            "JavaScript</p><script>globalThis.badNoscript=true;</script></noscript></body></html>",
            &c, litehtml::master_css + visual::control_styles());
        doc->render(1280);
        visual::Scripts js(doc, c, resources);
        uint64_t now = visual::Scripts::clock_ms();
        for (int i = 0; i < 8 && !js.ready(); ++i)
            js.poll(now + i);
        CHECK(js.eval("if(globalThis.badNoscript||getComputedStyle(document.getElementById('"
                      "warning')).display!=='none')throw Error('active noscript');"));
        std::ifstream type_input(std::string(argv[3]) + "/dom-interfaces.js");
        CHECK(type_input);
        std::string type_source((std::istreambuf_iterator<char>(type_input)), {});
        if (!js.eval(type_source)) {
            fprintf(stderr, "Interfaces: %s\n", js.error().c_str());
            return 1;
        }
        CHECK(js.eval("if(!HTMLScriptElement.supports('module')||HTMLScriptElement.supports('"
                      "importmap'))throw Error('unsupported script capability advertised');"));
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
        // This stresses queue capacity, not callback timing; allow sanitizer overhead.
        if (!js.eval("globalThis.limitObserver=new "
                      "MutationObserver(()=>{});limitObserver.observe(document.body,{attributes:"
                      "true});for(let "
                      "i=0;i<400;i++)document.body.setAttribute('data-quota',i);globalThis."
                      "quotaRecords=limitObserver.takeRecords().length;limitObserver.disconnect();"
                      "if(quotaRecords!==256)throw Error('observer quota');",
                      2000)) {
            fprintf(stderr, "Quota JS: %s\n", js.error().c_str());
            return 1;
        }
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
