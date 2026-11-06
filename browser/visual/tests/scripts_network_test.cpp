#include "scripts.h"
extern "C" {
#include "profile.h"
}
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            fprintf(stderr, "%d: %s\n", __LINE__, #x);                                             \
            std::exit(1);                                                                          \
        }                                                                                          \
    } while (0)
static void run(visual::Scripts &s, const std::string &source) {
    if (!s.eval(source)) {
        fprintf(stderr, "JS: %s\n", s.error().c_str());
        std::exit(1);
    }
}
int main(int argc, char **argv) {
    CHECK(argc == 7);
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    CHECK(TTF_Init() == 0);
    CHECK(curl_global_init(CURL_GLOBAL_DEFAULT) == 0);
    CHECK(browser_profile_open(nullptr, argv[5]));
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_RGBA32);
    CHECK(surface);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
    CHECK(renderer);
    {
        visual::Resources resources(TEST_ASSETS);
        auto *root = resources.request(std::string(argv[2]) + "/set", 4096);
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (resources.pending() && std::chrono::steady_clock::now() < until) {
            resources.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(root->state == visual::Resource::ready && root->policies.size() == 1);
        visual::Container c(renderer, resources, TEST_FONT);
        c.base = c.page_url = root->effective;
        std::string html(root->data.begin(), root->data.end());
        auto doc = litehtml::document::createFromString(html, &c);
        doc->render(1280);
        visual::Scripts s(doc, c, resources, root->policies);
        auto drive = [&] {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            unsigned turns = 0;
            do {
                resources.pump();
                s.poll(visual::Scripts::clock_ms());
                s.flush();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                ++turns;
            } while ((resources.pending() || !s.ready() || turns < 5) &&
                     std::chrono::steady_clock::now() < deadline);
            CHECK(!resources.pending() && s.ready());
        };
        drive();
        run(s, "if(document.title!=='Cookie reached script')throw Error('external script/cookie "
               "contract');if(document.cookie.includes('HTTP_ONLY_SECRET'))throw Error('HttpOnly "
               "exposed');");
        run(s, "globalThis.data=null;fetch('/visual-json').then(r=>{if(!r.ok||r.status!==200)throw "
               "Error('status');return r.json()}).then(x=>{data=x;const "
               "p=document.createElement('p');p.id='network-result';p.textContent=x.message;"
               "document.body.appendChild(p);});");
        drive();
        run(s, "if(data?.value!==42||document.getElementById('network-result')?.textContent!=='"
               "Загрузка выполнена')throw Error('fetch JSON/DOM failed');");
        run(s, "globalThis.missing=null;fetch('/visual-404').then(async "
               "r=>missing={status:r.status,ok:r.ok,text:await r.text()});");
        drive();
        run(s, "if(missing?.status!==404||missing.ok||missing.text!=='missing')throw Error('HTTP "
               "error response contract');");
        run(s, "globalThis.cross=false;fetch('https://elsewhere.invalid/"
               "').catch(()=>cross=true);globalThis.downgrade=false;fetch('/"
               "redirect-http').catch(()=>downgrade=true);");
        drive();
        run(s, "if(!cross||!downgrade)throw Error('fetch policy failed');");
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    browser_profile_close();
    curl_global_cleanup();
    TTF_Quit();
    SDL_Quit();
    puts("PASS external scripts, final CSP headers, cookie isolation, async fetch JSON/text, HTTP "
         "errors and origin/redirect policy");
}
