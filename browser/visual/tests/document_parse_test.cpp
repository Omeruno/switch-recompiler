#include "document_source.h"
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
        std::ifstream file(std::string(argv[3]) + "/noscript.html");
        CHECK(file);
        std::string html((std::istreambuf_iterator<char>(file)), {});
        auto doc = litehtml::document::createFromString(
            html, &c, litehtml::master_css + visual::control_styles());
        doc->render(1280);
        visual::Scripts js(doc, c, resources);
        auto run = [&](const char *code) {
            if (!js.eval(code, 500)) {
                fprintf(stderr, "JS %s\n", js.error().c_str());
                std::exit(1);
            }
        };
        auto now = visual::Scripts::clock_ms();
        for (int i = 0; i < 20; i++) {
            now += 20;
            js.poll(now);
            js.flush();
        }
        run(R"JS(
   if(document.querySelector('noscript style')||document.querySelector('noscript img')||globalThis.noscriptExecuted)throw Error('noscript parsed as elements');
   for(const node of document.querySelectorAll('noscript'))if(!node.childNodes.length||[...node.childNodes].some(child=>child.nodeType!==3)||!node.textContent.includes('<style>'))throw Error('noscript not raw text');
   if(getComputedStyle(document.getElementById('visible')).display==='none'||document.getElementById('visible').getBoundingClientRect().height<=0)throw Error('noscript stylesheet hid page');
   if(getComputedStyle(document.getElementById('delayed')).display!=='block')throw Error('delayed recovery still hidden');
   document.getElementById('fragment').innerHTML='<noscript><style>p{display:none}</style><img src="/fragment-leak"></noscript><p id="fragment-text">Visible</p>';
  )JS");
        js.flush();
        run("if(document.querySelector('#fragment noscript "
            "style')||getComputedStyle(document.getElementById('fragment-text')).display==='none')"
            "throw Error('fragment noscript');");
        CHECK(resources.count() == 0);
        CHECK(js.navigation().empty());
    }
    {
        visual::Resources resources(argv[1]);
        visual::Container c(renderer, resources, argv[2]);
        auto parse = [&](std::string body, std::string mime) {
            visual::Resource r;
            r.data.assign(body.begin(), body.end());
            r.response_headers["content-type"] = mime;
            auto doc = litehtml::document::createFromString(visual::document_source(r), &c);
            auto el = doc->root()->select_one("#text");
            CHECK(el);
            std::string text;
            el->get_text(text);
            return text;
        };
        CHECK(parse("<meta charset=utf-8><p id=text>\xcf\xf0\xe8\xe2\xe5\xf2</p>",
                    "text/html; CHARSET = \"windows-1251\"") == "Привет");
        CHECK(parse("\xef\xbb\xbf<p id=text>Привет</p>", "text/html; charset=windows-1251") ==
              "Привет");
        CHECK(parse("<meta charset=utf-8><p id=text>Привет</p>", "text/html; charset=unknown") ==
              "Привет");
        CHECK(parse("<meta charset=utf-8><p id=text>Привет</p>", "text/html") == "Привет");
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    browser_profile_close();
    curl_global_cleanup();
    TTF_Quit();
    SDL_Quit();
    puts("PASS scripting-aware noscript, inert parser fallback, delayed visible content, fragments "
         "and HTTP charset/BOM/meta precedence");
}
