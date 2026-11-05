#include "container.h"
#include "interaction.h"
extern "C" {
#include "profile.h"
}
#include <SDL_image.h>
#include <cstdio>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef __SWITCH__
#include <switch.h>
#include <sys/stat.h>
#endif
namespace {
void text(SDL_Renderer *renderer, TTF_Font *font, int x, int y, const std::string &s,
          SDL_Color color, int maxwidth = 1250) {
    if (s.empty())
        return;
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, s.c_str(), color);
    if (!surface)
        return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    if (texture) {
        SDL_Rect src = {0, 0, std::min(surface->w, maxwidth), surface->h},
                 dst = {x, y, src.w, src.h};
        SDL_RenderCopy(renderer, texture, &src, &dst);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}
struct Browser {
    SDL_Renderer *renderer;
    visual::Resources resources;
    visual::Container container;
    litehtml::document::ptr document;
    visual::View view;
    visual::Gestures gestures;
    std::string url = "demo:/index.html", notice;
    std::vector<std::string> history;
    unsigned built_revision = 0;
    bool rebuild = true;
    TTF_Font *ui = nullptr;
    int pointer_x = 640, pointer_y = 318;
    Browser(SDL_Renderer *r, const std::string &root, const std::string &font)
        : renderer(r), resources(root), container(r, resources, font), gestures(view) {
        ui = TTF_OpenFont(font.c_str(), 18);
        if (!ui)
            throw std::runtime_error(TTF_GetError());
        navigate(url, false);
    }
    ~Browser() {
        document.reset();
        container.reset();
        TTF_CloseFont(ui);
    }
    void navigate(const std::string &next, bool record = true) {
        if (next.empty())
            return;
        if (next.front() == '#') {
            jump(next.substr(1));
            return;
        }
        auto fragment = next.find('#');
        if (fragment != std::string::npos &&
            next.substr(0, fragment) == url.substr(0, url.find('#'))) {
            jump(next.substr(fragment + 1));
            return;
        }
        if (record) {
            history.push_back(url);
            if (history.size() > 16)
                history.erase(history.begin());
        }
        document.reset();
        container.reset();
        resources.clear();
        gestures.cancel();
        view = {};
        url = next;
        container.base = url;
        notice.clear();
        built_revision = 0;
        rebuild = true;
        resources.request(url, 1024u * 1024u);
    }
    void back() {
        if (!history.empty()) {
            auto previous = history.back();
            history.pop_back();
            navigate(previous, false);
        } else
            navigate("demo:/index.html", false);
    }
    void jump(const std::string &id) {
        if (!document)
            return;
        if (id.empty()) {
            view.y = 0;
            return;
        }
        // Attribute selector quoting is deliberately avoided for arbitrary IDs.
        auto nodes = document->root()->select_all("[id]");
        for (const auto &el : nodes)
            if (id == el->get_attr("id", "")) {
                view.y = float(el->get_placement().y);
                view.clamp();
                break;
            }
    }
    void build() {
        auto *r = resources.request(url, 1024u * 1024u);
        if (!r)
            return;
        if (r->state == visual::Resource::failed) {
            notice = r->error;
            built_revision = resources.revision;
            rebuild = false;
            return;
        }
        if (r->state != visual::Resource::ready)
            return;
        document.reset();
        container.reset();
        container.base = r->effective;
        container.zoom = view.zoom;
        std::string html(r->data.begin(), r->data.end());
        if (std::count(html.begin(), html.end(), '<') > 8192) {
            notice = "Превышен лимит сложности страницы";
            rebuild = false;
            return;
        }
        try {
            document = litehtml::document::createFromString(html, &container);
            if (!document)
                throw std::runtime_error("HTML parse failed");
            document->render(visual::Container::width);
            view.content_width = std::max(1280.f, float(document->width()));
            view.content_height = std::max(636.f, float(document->height()));
            view.clamp();
            notice.clear();
            built_revision = resources.revision;
            rebuild = false;
        } catch (const std::exception &e) {
            notice = e.what();
            document.reset();
            rebuild = false;
            built_revision = resources.revision;
        }
    }
    void pump() {
        resources.pump();
        if (rebuild || built_revision != resources.revision)
            build();
    }
    void click(visual::Point p) {
        if (p.y < 0) {
            if (p.x < 130)
                back();
            else if (p.x < 270)
                navigate("demo:/index.html");
            else if (p.x >= 1120)
                edit_address();
            else
                edit_address();
            return;
        }
        if (!document)
            return;
        auto q = view.document(p);
        auto redraw = [](const litehtml::position &) {};
        document->on_mouse_over(q.x, q.y, p.x / view.zoom, p.y / view.zoom, redraw);
        document->on_lbutton_down(q.x, q.y, p.x / view.zoom, p.y / view.zoom, redraw);
        document->on_lbutton_up(q.x, q.y, p.x / view.zoom, p.y / view.zoom, redraw);
        if (!container.anchor.empty()) {
            auto next = container.anchor;
            container.anchor.clear();
            navigate(next);
        }
    }
    void edit_address() {
#ifdef __SWITCH__
        gestures.cancel();
        SwkbdConfig keyboard;
        if (R_FAILED(swkbdCreate(&keyboard, 0)))
            return;
        swkbdConfigMakePresetDefault(&keyboard);
        swkbdConfigSetHeaderText(&keyboard, "HTTPS address");
        swkbdConfigSetStringLenMax(&keyboard, 2047);
        swkbdConfigSetInitialText(
            &keyboard, url.rfind("https://", 0) == 0 ? url.c_str() : "https://keldurn.com/ru/");
        char address[2048] = {};
        Result rc = swkbdShow(&keyboard, address, sizeof(address));
        swkbdClose(&keyboard);
        if (R_SUCCEEDED(rc)) {
            auto u = visual::Resources::resolve("", address);
            if (!u.empty())
                navigate(u);
            else
                notice = "Нужен адрес https://";
        }
#else
        notice = "Host: pass an HTTPS address as an argument";
#endif
    }
    void paint() {
        SDL_SetRenderDrawColor(renderer, 245, 247, 250, 255);
        SDL_RenderClear(renderer);
        container.zoom = view.zoom;
        container.begin_paint();
        if (document) {
            litehtml::position clip = {0, 0, 1280 / view.zoom, 636 / view.zoom};
            document->draw(0, -view.x, -view.y, &clip);
        }
        container.end_paint();
        SDL_SetRenderDrawColor(renderer, 20, 28, 41, 255);
        SDL_Rect chrome = {0, 0, 1280, 84};
        SDL_RenderFillRect(renderer, &chrome);
        text(renderer, ui, 16, 10, "‹ Назад", {230, 238, 250, 255}, 110);
        text(renderer, ui, 144, 10, "Демо", {112, 220, 235, 255}, 100);
        text(renderer, ui, 280, 10, url, {230, 238, 250, 255}, 840);
        text(renderer, ui, 1135, 10, "Адрес X", {112, 220, 235, 255}, 135);
        text(renderer, ui, 16, 46,
             notice.empty() ? "HTML/CSS · " + resources.status() + " · масштаб " +
                                  std::to_string(int(view.zoom * 100)) + "% · без JavaScript"
                            : notice,
             {174, 190, 211, 255}, 1250);
        if (!document && notice.empty())
            text(renderer, ui, 40, 140, "Загрузка страницы…", {25, 40, 60, 255});
        if (document && !gestures.active()) {
            SDL_SetRenderDrawColor(renderer, 15, 100, 160, 200);
            SDL_RenderDrawLine(renderer, pointer_x - 7, pointer_y + 84, pointer_x + 7,
                               pointer_y + 84);
            SDL_RenderDrawLine(renderer, pointer_x, pointer_y + 77, pointer_x, pointer_y + 91);
        }
    }
};
} // namespace
int main(int argc, char **argv) {
    std::string root, font, ca;
#ifdef __SWITCH__
    Result romfs = romfsInit();
    if (R_FAILED(romfs)) {
        consoleInit(nullptr);
        printf("RomFS failed: 0x%08x\n", romfs);
        consoleUpdate(nullptr);
        svcSleepThread(3000000000LL);
        consoleExit(nullptr);
        return 1;
    }
    root = "romfs:/visual";
    font = "romfs:/DejaVuSans.ttf";
    ca = "romfs:/cacert.pem";
    bool sockets = R_SUCCEEDED(socketInitializeDefault());
#else
    if (argc < 3) {
        fprintf(stderr, "Usage: visual-host ASSET_ROOT FONT [--test SCREENSHOT.bmp | HTTPS_URL]\n");
        return 1;
    }
    root = argv[1];
    font = argv[2];
    ca = "";
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) || TTF_Init()) {
        fprintf(stderr, "SDL: %s / %s\n", SDL_GetError(), TTF_GetError());
        return 1;
    }
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    curl_global_init(CURL_GLOBAL_DEFAULT);
#ifdef __SWITCH__
    mkdir("sdmc:/switch", 0700);
    mkdir("sdmc:/switch/switch-visual-browser", 0700);
    browser_profile_open("sdmc:/switch/switch-visual-browser", ca.c_str());
    hidInitializeTouchScreen();
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
#else
    browser_profile_open(nullptr, nullptr);
#endif
    SDL_Window *window = SDL_CreateWindow("Switch visual browser", SDL_WINDOWPOS_UNDEFINED,
                                          SDL_WINDOWPOS_UNDEFINED, 1280, 720, 0);
    SDL_Renderer *renderer =
        window
            ? SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC)
            : nullptr;
    if (!renderer && window)
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    int result = 0;
    if (!renderer) {
        fprintf(stderr, "Renderer: %s\n", SDL_GetError());
        result = 1;
    } else
        try {
            SDL_RenderSetLogicalSize(renderer, 1280, 720);
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            Browser app(renderer, root, font);
#ifndef __SWITCH__
            bool test = argc >= 5 && std::string(argv[3]) == "--test";
            if (argc >= 4 && !test)
                app.navigate(argv[3]);
#endif
            bool quit = false;
            uint32_t previous = SDL_GetTicks();
            while (!quit) {
#ifdef __SWITCH__
                if (!appletMainLoop())
                    break;
                padUpdate(&pad);
                u64 down = padGetButtonsDown(&pad), held = padGetButtons(&pad);
                if (down & HidNpadButton_Plus)
                    break;
                if (down & HidNpadButton_X)
                    app.edit_address();
                if (down & HidNpadButton_B)
                    app.back();
                if (down & HidNpadButton_Y)
                    app.navigate(app.url, false);
                if (down & HidNpadButton_A)
                    app.click({float(app.pointer_x), float(app.pointer_y)});
                if (held & HidNpadButton_Down)
                    app.view.drag(0, -10);
                if (held & HidNpadButton_Up)
                    app.view.drag(0, 10);
                HidAnalogStickState stick = padGetStickPos(&pad, 0);
                app.pointer_x = std::clamp(app.pointer_x + stick.x / 4096, 0, 1279);
                app.pointer_y = std::clamp(app.pointer_y - stick.y / 4096, 0, 635);
                if (down & HidNpadButton_ZR)
                    app.view.pinch({640, 318}, {640, 318}, 1.2f);
                if (down & HidNpadButton_ZL)
                    app.view.pinch({640, 318}, {640, 318}, 1 / 1.2f);
#endif
                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    if (event.type == SDL_QUIT)
                        quit = true;
                    else if (event.type == SDL_WINDOWEVENT &&
                             event.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                        app.gestures.cancel();
                    else if (event.type == SDL_FINGERDOWN)
                        app.gestures.down(event.tfinger.fingerId,
                                          {event.tfinger.x * 1280, event.tfinger.y * 720 - 84},
                                          event.tfinger.timestamp);
                    else if (event.type == SDL_FINGERMOTION)
                        app.gestures.move(event.tfinger.fingerId,
                                          {event.tfinger.x * 1280, event.tfinger.y * 720 - 84},
                                          event.tfinger.timestamp);
                    else if (event.type == SDL_FINGERUP) {
                        auto tap =
                            app.gestures.up(event.tfinger.fingerId,
                                            {event.tfinger.x * 1280, event.tfinger.y * 720 - 84},
                                            event.tfinger.timestamp);
                        if (tap)
                            app.click(*tap);
                    } else if (event.type == SDL_MOUSEBUTTONDOWN &&
                               event.button.which != SDL_TOUCH_MOUSEID &&
                               event.button.button == SDL_BUTTON_LEFT)
                        app.gestures.down(-1, {float(event.button.x), float(event.button.y - 84)},
                                          event.button.timestamp);
                    else if (event.type == SDL_MOUSEMOTION &&
                             event.motion.which != SDL_TOUCH_MOUSEID &&
                             (event.motion.state & SDL_BUTTON_LMASK))
                        app.gestures.move(-1, {float(event.motion.x), float(event.motion.y - 84)},
                                          event.motion.timestamp);
                    else if (event.type == SDL_MOUSEBUTTONUP &&
                             event.button.which != SDL_TOUCH_MOUSEID &&
                             event.button.button == SDL_BUTTON_LEFT) {
                        auto tap =
                            app.gestures.up(-1, {float(event.button.x), float(event.button.y - 84)},
                                            event.button.timestamp);
                        if (tap)
                            app.click(*tap);
                    } else if (event.type == SDL_MOUSEWHEEL)
                        app.view.drag(0, event.wheel.y * 72.f);
                    else if (event.type == SDL_KEYDOWN) {
                        if (event.key.keysym.sym == SDLK_ESCAPE)
                            quit = true;
                        else if (event.key.keysym.sym == SDLK_BACKSPACE)
                            app.back();
                        else if (event.key.keysym.sym == SDLK_HOME)
                            app.navigate("demo:/index.html");
                        else if (event.key.keysym.sym == SDLK_DOWN)
                            app.view.drag(0, -60);
                        else if (event.key.keysym.sym == SDLK_UP)
                            app.view.drag(0, 60);
                    }
                }
                uint32_t now = SDL_GetTicks();
                app.gestures.tick((now - previous) / 1000.f);
                previous = now;
                app.pump();
                app.paint();
#ifndef __SWITCH__
                if (test) {
                    if (!app.document)
                        throw std::runtime_error("demo document missing");
                    auto link = app.document->root()->select_one("#jump");
                    auto target = app.document->root()->select_one("#details");
                    auto a = app.document->root()->select_one("#card-a"),
                         b = app.document->root()->select_one("#card-b");
                    fprintf(stderr,
                            "layout: link=%d target=%d a=%d b=%d ax=%.1f bx=%.1f height=%.1f "
                            "images=%u decoded=%zu text=%u notice=%s\n",
                            bool(link), bool(target), bool(a), bool(b),
                            a ? float(a->get_placement().x) : -1,
                            b ? float(b->get_placement().x) : -1, app.view.content_height,
                            app.container.painted_images, app.container.decoded_images(),
                            app.container.painted_text, app.notice.c_str());
                    if (!link || !target || !a || !b ||
                        !(float(a->get_placement().x) < float(b->get_placement().x)) ||
                        !(app.view.content_height > 636) || app.container.painted_images < 1 ||
                        app.container.decoded_images() != 2 || app.container.painted_text < 10)
                        throw std::runtime_error("CSS/image/layout assertions failed");
                    SDL_Surface *shot =
                        SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_RGBA32);
                    if (!shot ||
                        SDL_RenderReadPixels(renderer, nullptr, shot->format->format, shot->pixels,
                                             shot->pitch) ||
                        SDL_SaveBMP(shot, argv[4]))
                        throw std::runtime_error(SDL_GetError());
                    SDL_FreeSurface(shot);
                    auto box = link->get_placement();
                    app.click({float(box.x) + 4, float(box.y) + 4});
                    if (app.view.y <= 0)
                        throw std::runtime_error(
                            "painted anchor hit-test/fragment navigation failed");
                    printf("PASS external CSS, flex layout, Russian text, PNG/JPEG paint, "
                           "scrolling and anchor hit testing\n");
                    quit = true;
                }
#endif
                SDL_RenderPresent(renderer);
                SDL_Delay(10);
            }
        } catch (const std::exception &e) {
            fprintf(stderr, "Visual browser: %s\n", e.what());
            result = 1;
        }
    if (renderer)
        SDL_DestroyRenderer(renderer);
    if (window)
        SDL_DestroyWindow(window);
    browser_profile_close();
    curl_global_cleanup();
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
#ifdef __SWITCH__
    if (sockets)
        socketExit();
    romfsExit();
#endif
    return result;
}
