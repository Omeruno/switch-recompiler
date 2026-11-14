#include "frame_policy.h"
#include "scripts.h"
#include "unsupported_frame.h"
extern "C" {
#include "profile.h"
}
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <litehtml/render_item.h>
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
        visual::Resources r(TEST_ASSETS);
        auto *config = r.request(std::string(argv[2]) + "/api-config", 4096);
        auto *cookie = r.request(std::string(argv[2]) + "/set", 4096);
        while (r.pending()) {
            r.pump();
            SDL_Delay(2);
        }
        CHECK(config->transfer_ok && cookie->transfer_ok);
        visual::Container c(renderer, r, TEST_FONT);
        c.base = c.page_url = std::string(argv[2]) + "/parent";
        auto doc = litehtml::document::createFromString(
            "<html><body style='margin:0'><iframe id='frame' "
            "style='display:block;border:0;width:300px;height:150px'></iframe></body></html>",
            &c);
        doc->render(1280);
        visual::Scripts js(doc, c, r);
        auto run = [&](const std::string &s) {
            if (!js.eval(s, 1000)) {
                fprintf(stderr, "JS: %s\n", js.error().c_str());
                std::exit(1);
            }
        };
        run("const config=" + std::string(config->data.begin(), config->data.end()) +
            ";const "
            "frame=document.getElementById('frame');globalThis.events=[];globalThis.parentLoads=0;"
            "addEventListener('load',()=>parentLoads++);onmessage=e=>{if(e.source!"
            "==frame.contentWindow||e.origin!==new URL(frame.src).origin)throw Error('Frame "
            "source/origin');events.push(e.data);};frame.src=config.cross+'/frame-child';");
        auto drive = [&](int ms = 400) {
            auto until = visual::Scripts::clock_ms() + ms;
            do {
                r.pump();
                js.poll(visual::Scripts::clock_ms());
                js.flush();
                c.poll_frames();
                SDL_Delay(2);
            } while (visual::Scripts::clock_ms() < until);
        };
        drive(800);
        auto frame =
            std::dynamic_pointer_cast<visual::UnsupportedFrame>(doc->root()->select_one("#frame"));
        CHECK(frame);
        if (!frame->loaded())
            fprintf(stderr, "Frame: %s\n", frame->status().c_str());
        CHECK(frame->loaded());
        run("if(!events.some(e=>e.kind==='ready'&&e.width===300&&e.height===150&&e.blocked===1&&e."
            "cookieEmpty))"
            "throw Error('Frame init "
            "'+JSON.stringify(events));if(!events.some(e=>e.kind==='fetched'&&e.text==='anonymous')"
            ")throw Error('Anonymous "
            "fetch');globalThis.oldWindow=frame.contentWindow;oldWindow.postMessage('ping',new "
            "URL(frame.src).origin);");
        drive();
        run("if(!events.some(e=>e.kind==='pong'&&e.counter===1))throw Error('Round "
            "trip');try{frame.contentWindow.document;throw Error('Foreign DOM "
            "exposed');}catch(e){if(e.name!=='SecurityError')throw "
            "e;}if(frame.contentDocument!==null)throw Error('Foreign document');");
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);
        c.begin_paint();
        litehtml::position clip{0, 0, 1280, 636};
        doc->draw(0, 0, 0, &clip);
        c.end_paint();
        SDL_RenderPresent(renderer);
        Uint8 red, green, blue, alpha;
        auto *pixel = static_cast<Uint32 *>(surface->pixels);
        SDL_GetRGBA(pixel[(visual::Container::toolbar + 100) * surface->w + 200], surface->format,
                    &red, &green, &blue, &alpha);
        CHECK(red == 0x12 && green == 0x34 && blue == 0x56);
        SDL_GetRGBA(pixel[(visual::Container::toolbar + 100) * surface->w + 350], surface->format,
                    &red, &green, &blue, &alpha);
        CHECK(red == 255 && green == 255 && blue == 255);
        CHECK(c.frame_click(frame, 170, 20));
        drive();
        run("if(!events.some(e=>e.kind==='checked'&&e.checked))throw Error('Frame checkbox');");
        CHECK(c.frame_click(frame, 30, 30));
        drive();
        run("if(!events.some(e=>e.kind==='down'&&e.trusted&&e.x===30&&e.y===30&&e.buttons===1)||!"
            "events.some(e=>e."
            "kind==='up'&&e.trusted))throw Error('Mouse "
            "events');if(!events.some(e=>e.kind==='clicked'&&e.trusted&&e.x===30&&e.y===30))throw "
            "Error('Native frame click "
            "'+JSON.stringify(events));frame.style.width='450px';frame.style.height='220px';");
        drive();
        run("if(!events.some(e=>e.kind==='resized'&&e.width===450&&e.height===220))throw "
            "Error('Viewport resize');if(frame.contentWindow!==oldWindow)throw Error('Resize "
            "window identity');events=[];frame.src=config.cross+'/frame-allow';");
        drive();
        CHECK(frame->loaded());
        run("if(frame.contentWindow!==oldWindow||!events.some(e=>e.kind==='ready'))throw "
            "Error('Navigation proxy identity');");
        for (const auto *path :
             {"/frame-deny", "/frame-same", "/frame-none", "/frame-csp-sandbox"}) {
            run(std::string("events=[];frame.src=config.cross+'") + path + "';");
            drive();
            CHECK(!frame->loaded());
            run("if(events.length)throw Error('Rejected frame executed');");
        }
        run("frame.sandbox='allow-scripts';if(frame.getAttribute('sandbox')!=='allow-scripts')"
            "throw Error('Sandbox reflection');frame.src=config.cross+'/frame-child';");
        drive();
        CHECK(!frame->loaded());
        run("frame.setAttribute('sandbox','allow-scripts "
            "allow-same-origin');frame.src=config.cross+'/frame-child';");
        drive();
        CHECK(frame->loaded());
        // sandbox changes cancel the runtime; srcdoc is unsupported and must not fall back to src.
        run("frame.setAttribute('sandbox','allow-same-origin');events=[];");
        drive();
        CHECK(frame->loaded());
        run("if(events.length)throw Error('Sandbox script "
            "execution');frame.removeAttribute('sandbox');frame.setAttribute('srcdoc','<script>"
            "parent.postMessage(123,\"*\")</script>');");
        drive();
        CHECK(!frame->loaded());
        run("frame.removeAttribute('srcdoc');frame.src=config.cross+'/frame-child';");
        drive();
        CHECK(frame->loaded());
        run("frame.remove();");
        drive();
        run("if(!oldWindow.closed)throw Error('Detached window "
            "alive');document.body.appendChild(frame);");
        drive();
        CHECK(frame->loaded());
        run("if(frame.contentWindow===oldWindow)throw Error('Reinsert reuses closed proxy');");
        CHECK(
            js.eval("if(globalThis.frameCounter!==undefined)throw Error('Leaked child global');"));
        run("events=[];frame.src=frame.src;");
        drive();
        CHECK(frame->loaded());
        run("if(!events.some(e=>e.kind==='ready'&&e.counter===0))throw Error('Same source "
            "reload');");
        run("if(parentLoads!==1)throw Error('Parent load repeated');frame.sandbox='<absent>';");
        drive();
        CHECK(!frame->loaded());
        run("frame.sandbox='allow-scripts allow-same-origin';");
        drive();
        CHECK(frame->loaded());
        run("frame.srcdoc='<absent>';");
        drive();
        CHECK(!frame->loaded());
        run("frame.removeAttribute('srcdoc');");
        drive();
        CHECK(frame->loaded());
        run("frame.style.width='100000px';");
        drive();
        CHECK(!frame->loaded());
        CHECK(c.messages->frame_pixels == 0);
        // Parent CSP is enforced before the first request and before redirect hops.
    }
    {
        visual::Resources r(TEST_ASSETS);
        visual::Container c(renderer, r, TEST_FONT);
        c.base = c.page_url = std::string(argv[2]) + "/parent";
        auto doc = litehtml::document::createFromString(
            "<html><body><iframe id='x' style='width:300px;height:150px'></iframe></body></html>",
            &c);
        doc->render(1280);
        visual::Scripts js(doc, c, r, {"frame-src 'self'"});
        const std::string code =
            "document.getElementById('x').src='" + std::string(argv[2]) + "/frame-redirect';";
        CHECK(js.eval(code));
        auto end = visual::Scripts::clock_ms() + 400;
        while (visual::Scripts::clock_ms() < end) {
            js.poll(visual::Scripts::clock_ms());
            js.flush();
            c.poll_frames();
            SDL_Delay(2);
        }
        auto frame =
            std::dynamic_pointer_cast<visual::UnsupportedFrame>(doc->root()->select_one("#x"));
        CHECK(!frame->loaded());
        CHECK(c.messages->frame_pixels == 0);
    }
    {
        visual::Resources r(TEST_ASSETS);
        auto *state = r.request(std::string(argv[2]) + "/api-state", 8192);
        while (r.pending()) {
            r.pump();
            SDL_Delay(2);
        }
        CHECK(state->transfer_ok);
        std::string data(state->data.begin(), state->data.end());
        CHECK(data.find("frame-cookie-leak") == std::string::npos);
        CHECK(data.find("frame-forbidden") == std::string::npos);
    }
    {
        visual::Resource response;
        response.effective = "https://child.test/page";
        response.response_headers["content-type"] = "text/html";
        CHECK(visual::frame_response_allowed(response, {"https://parent.test"}));
        response.policies = {"frame-ancestors https://parent.test"};
        CHECK(visual::frame_response_allowed(response, {"https://parent.test"}));
        CHECK(!visual::frame_response_allowed(response,
                                              {"https://parent.test", "https://other.test"}));
        response.policies = {"frame-ancestors *; sandbox allow-scripts allow-same-origin"};
        CHECK(!visual::frame_response_allowed(response, {"https://parent.test"}));
        CHECK(
            visual::frame_source_allowed({"default-src 'none';child-src https://child.test/path/"},
                                         "https://parent.test", "https://child.test/path/ok"));
        CHECK(!visual::frame_source_allowed({"frame-src 'none';child-src *"}, "https://parent.test",
                                            "https://child.test/"));
        CHECK(!visual::frame_source_allowed({"frame-src *", "default-src 'none'"},
                                            "https://parent.test", "https://child.test/"));
        char cookies[4096]{};
        browser_cookie_get(argv[2], cookies, sizeof(cookies));
        CHECK(std::string(cookies).find("frame_set") == std::string::npos);
        CHECK(std::string(cookies).find("frame_js") == std::string::npos);
    }
    {
        visual::Resources r(TEST_ASSETS);
        visual::Container c(renderer, r, TEST_FONT);
        std::ifstream file(std::string(TEST_ASSETS) + "/index.html");
        CHECK(file);
        std::string html((std::istreambuf_iterator<char>(file)), {});
        auto doc = litehtml::document::createFromString(html.c_str(), &c);
        doc->render(1280);
        {
            visual::Scripts js(doc, c, r);
            auto drive = [&](const std::string &expected) {
                auto end = visual::Scripts::clock_ms() + 2500;
                while (visual::Scripts::clock_ms() < end) {
                    r.pump();
                    js.poll(visual::Scripts::clock_ms());
                    js.flush();
                    c.poll_frames();
                    std::string text;
                    doc->root()->select_one("#frame-status")->get_text(text);
                    if (text.find(expected) != std::string::npos)
                        return true;
                    SDL_Delay(2);
                }
                fprintf(stderr, "Offline parent JS: %s\n", js.error().c_str());
                for (auto &weak : c.frames)
                    if (auto frame = weak.lock())
                        fprintf(stderr, "Offline frame: %s\n", frame->status().c_str());
                return false;
            };
            CHECK(drive("Ответ из фрейма: 0"));
            auto frame = std::dynamic_pointer_cast<visual::UnsupportedFrame>(
                doc->root()->select_one("#native-frame"));
            CHECK(frame && frame->loaded());
            CHECK(js.eval("if(!document.getElementById('frame-status').textContent.includes('Ответ "
                          "из фрейма: 0'))throw Error('Offline frame init');"));
            auto box = frame->get_render_item()->get_placement();
            c.scroll_y = float(box.y);
            CHECK(c.frame_click(frame, float(box.x) + 60, 75));
            CHECK(drive("Ответ из фрейма: 1"));
            CHECK(js.eval("if(!document.getElementById('frame-status').textContent.includes('Ответ "
                          "из фрейма: 1'))throw Error('Offline frame "
                          "click');document.getElementById('frame-size').click();"));
            CHECK(drive("1 · размер 400 × 240"));
            CHECK(js.eval("if(!document.getElementById('frame-status').textContent.includes('1 · "
                          "размер 400 × 240'))throw Error('Offline frame resize/state');"));
        }
        doc.reset();
        CHECK(c.messages->frame_pixels == 0);
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    browser_profile_close();
    curl_global_cleanup();
    TTF_Quit();
    SDL_Quit();
    puts("PASS isolated HTTPS frames, CSS pixels, native click, resize, messaging, response "
         "policies, sandbox and teardown");
}
