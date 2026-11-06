#include "scripts.h"
extern "C" {
#include "profile.h"
}
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
    CHECK(argc == 4);
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    CHECK(TTF_Init() == 0);
    CHECK(curl_global_init(CURL_GLOBAL_DEFAULT) == 0);
    CHECK(browser_profile_open(nullptr, nullptr));
    auto *surface = SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_RGBA32);
    CHECK(surface);
    auto *renderer = SDL_CreateSoftwareRenderer(surface);
    CHECK(renderer);
    {
        visual::Resources resources(argv[1]);
        visual::Container c(renderer, resources, argv[2]);
        c.base = c.page_url = "https://example.test/";
        auto doc = litehtml::document::createFromString(
            R"HTML(<!doctype html><style>
body {margin:20px;background:white} input,textarea {display:block;margin:10px 0;}
#wide {width:600px;} #narrow {width:80px;} #box {width:300px;height:44px;box-sizing:border-box;padding:8px;border:2px solid black;}
#limits {width:100px;min-width:260px;max-width:350px;height:20px;min-height:32px;}
#percent {width:90%;} #bare {border:0;background:transparent;width:500px;height:24px;}
#multi {width:220px;height:70px;} #long {width:250px;height:24px;}
#bare:focus {color:#b00000;} #owner:focus-within {background:#eef4ff;}
#disabled {display:none} #hidden {display:none}
</style><input id=wide placeholder="Широкое поле — высота не растягивается"><input id=narrow value=Short>
<input id=box value="Размер 300 × 44"><input id=limits value="Минимальный размер">
<div style="width:400px"><input id=percent placeholder="90% ширины"></div>
<div id=owner><input id=bare aria-label="Поле без границ" placeholder="Прозрачное поле без CSS-границ"></div>
<textarea id=multi>Русский текст переносится на несколько строк внутри поля без потери символов.</textarea>
<input id=long><input id=disabled disabled><input id=hidden type=hidden>)HTML",
            &c, litehtml::master_css + visual::control_styles());
        doc->render(1280);
        auto field = [&](const char *id) {
            auto el = std::dynamic_pointer_cast<visual::Control>(
                doc->root()->select_one(std::string("#") + id));
            CHECK(el);
            return el;
        };
        auto wide = field("wide")->get_placement(), narrow = field("narrow")->get_placement();
        CHECK(float(wide.height) == float(narrow.height));
        CHECK(float(wide.width) > 590 && float(narrow.width) < 120);
        auto box = field("box")->get_placement();
        CHECK(float(box.width) == 280 && float(box.height) == 24);
        auto limits = field("limits")->get_placement();
        CHECK(float(limits.width) >= 260 && float(limits.height) >= 32);
        CHECK(float(field("percent")->get_placement().width) >= 360);
        std::string long_value;
        for (int i = 0; i < 400; ++i)
            long_value += "Ж";
        field("long")->value(long_value);
        auto paint = [&] {
            SDL_RenderSetClipRect(renderer, nullptr);
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
            SDL_RenderClear(renderer);
            c.begin_paint();
            doc->draw(0, 0, 0, nullptr);
            c.end_paint();
            SDL_RenderPresent(renderer);
        };
        auto pixel = [&](int x, int y) {
            auto *p = static_cast<uint8_t *>(surface->pixels) + y * surface->pitch + x * 4;
            return litehtml::web_color(p[0], p[1], p[2], p[3]);
        };
        auto bare = field("bare")->get_placement();
        paint();
        CHECK(pixel(int(float(bare.x)) - 10, int(float(bare.y)) - 8 + 84) ==
              litehtml::web_color(122, 134, 153));
        unsigned before = c.painted_text;
        CHECK(before >= 8); // Includes a visible prefix of the 800-byte Cyrillic value.
        visual::Scripts js(doc, c, resources);
        auto eval = [&](const char *source) {
            if (!js.eval(source)) {
                fprintf(stderr, "JS: %s\n", js.error().c_str());
                std::exit(1);
            }
        };
        eval("globalThis.focusLog=[];const "
             "a=document.getElementById('bare'),b=document.getElementById('long');"
             "a.addEventListener('focus',e=>focusLog.push('focus'));a.addEventListener('blur',e=>"
             "focusLog.push('blur'));"
             "document.getElementById('owner').addEventListener('focusin',e=>focusLog.push('in'));"
             "a.focus();a.focus();if(document.activeElement!==a||focusLog.join(',')!=='focus,in')"
             "throw Error('focus');");
        js.flush();
        CHECK(c.focused() == field("bare"));
        CHECK(doc->root()->select_one("#bare:focus"));
        CHECK(doc->root()->select_one("#owner:focus-within"));
        bare = field("bare")->get_placement();
        paint();
        CHECK(pixel(int(float(bare.x)) - 10, int(float(bare.y)) - 8 + 84) ==
              litehtml::web_color(24, 100, 200));
        CHECK(SDL_SaveBMP(surface, argv[3]) == 0);
        eval(
            "document.getElementById('disabled').focus();document.getElementById('hidden').focus();"
            "if(document.activeElement!==a)throw Error('hidden focus');b.focus();"
            "if(document.activeElement!==b||focusLog.join(',')!=='focus,in,blur')throw "
            "Error('blur');");
        js.flush();
        paint();
        CHECK(c.painted_text >= before);
        js.focus(field("bare"));
        js.flush();
        CHECK(c.focused() == field("bare"));
        CHECK(doc->root()->select_one("#bare:focus"));
        eval("a.blur();if(document.activeElement!==document.body)throw Error('body fallback');"
             "a.focus();a.remove();if(document.activeElement!==document.body)throw Error('detached "
             "focus');document.body.appendChild(a);if(document.activeElement===a)throw "
             "Error('reattach focus');");
        js.flush();
        CHECK(!c.focused());
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    browser_profile_close();
    curl_global_cleanup();
    TTF_Quit();
    SDL_Quit();
    puts("PASS independent control dimensions, min sizes, border-box, visible native frames, UTF-8 "
         "fitting, textarea wrapping, native/JS focus and hidden/detached controls");
}
