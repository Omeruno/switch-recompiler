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
        for (const char *name : {"browser-compat.js"}) {
            std::ifstream file(std::string(argv[3]) + "/" + name);
            CHECK(file);
            std::string text((std::istreambuf_iterator<char>(file)), {});
            if (!js.eval(text, 5000)) {
                fprintf(stderr, "%s: %s\n", name, js.error().c_str());
                return 1;
            }
        }
        CHECK(js.eval("if(!globalThis.browserCompatDone)throw Error('Browser APIs unfinished');",
                      500));
        auto button = doc->root()->select_one("#trusted-input");
        CHECK(button);
        js.focus(button);
        js.click(button, 120, 45);
        js.key("keydown", "a", "KeyA");
        js.pointer(button, "pointerdown", 10, 10, 1, 1, "touch");
        CHECK(js.eval(
            "if(clickCoordinates.join()!=='120,45')throw Error('click "
            "coordinates');if(inputTrust.join()!=='true,true,true')throw Error('native trust "
            "'+inputTrust);document.getElementById('trusted-input').dispatchEvent(savedInput);if("
            "inputTrust[inputTrust.length-1]!==false)throw Error('redispatch trust');"));
        CHECK(js.eval("const "
                      "frame=document.createElement('iframe');frame.id='unsupported-frame';frame."
                      "width='300';frame.height='150';document.body.appendChild(frame);const "
                      "r=frame.getBoundingClientRect();if(r.width<250||r.height<100)throw "
                      "Error('frame intrinsic layout');"));
        c.begin_paint();
        litehtml::position clip{0, 0, 1280, 636};
        doc->draw(0, 0, 0, &clip);
        c.end_paint();
        CHECK(c.painted_frames == 1);
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    browser_profile_close();
    curl_global_cleanup();
    TTF_Quit();
    SDL_Quit();
    puts("PASS base64, secure integer random, trusted native input versus synthetic dispatch and "
         "visible unsupported iframe");
}
