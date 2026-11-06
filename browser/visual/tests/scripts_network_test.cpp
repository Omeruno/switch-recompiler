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
    for (int mode = 0; mode < 4; ++mode) {
        visual::Resources resources(TEST_ASSETS);
        visual::Container c(renderer, resources, TEST_FONT);
        c.base = c.page_url = std::string(argv[2]) + "/nonce-page";
        const std::string policy =
            "script-src 'nonce-dGVzdA==' 'strict-dynamic' https: 'unsafe-inline' 'unsafe-eval'";
        std::vector<std::string> policies{mode == 3 ? "script-src 'Nonce-dGVzdA=='" : policy};
        if (mode == 1)
            policies.push_back("script-src 'nonce-b3RoZXI='");
        if (mode == 2)
            policies = {"script-src 'STRICT-DYNAMIC' https: 'unsafe-inline'"};
        const char *markup =
            R"HTML(<!doctype html><html><head><style>script[nonce="dGVzdA=="]{background-image:url('/nonce-should-not-leak')}</style></head><body>
          <script id="trusted" nonce="dGVzdA==">globalThis.trusted=1;try{globalThis.evalAllowed=eval('40+2')===42;}catch(e){globalThis.evalAllowed=false;}
          const denied=document.createElement('script');denied.text='globalThis.untrustedDynamic=true';document.body.appendChild(denied);
          const child=document.createElement('script');child.nonce=document.currentScript.nonce;child.text='globalThis.trustedDynamic=true';document.body.appendChild(child);
          </script>
          <script nonce="wrong">globalThis.wrongNonce=true;</script><script>globalThis.noNonce=true;</script>
          <script nonce="dGVzdA==" src="/nonce-source.js"></script>
          <button id="event" nonce="dGVzdA==" onclick="globalThis.inlineEvent=true">Test</button>
        </body></html>)HTML";
        auto doc = litehtml::document::createFromString(markup, &c);
        doc->render(1280);
        visual::Scripts js(doc, c, resources, policies);
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        do {
            resources.pump();
            js.poll(visual::Scripts::clock_ms());
            js.flush();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while ((!js.ready() || resources.pending()) && std::chrono::steady_clock::now() < until);
        CHECK(js.ready() && !resources.pending());
        js.dispatch(doc->root()->select_one("#event"), "click", true);
        run(js, "if(globalThis.wrongNonce||globalThis.noNonce||globalThis.untrustedDynamic||"
                "globalThis.inlineEvent)throw Error('CSP nonce "
                "bypass');if(document.getElementById('trusted').nonce!=='dGVzdA=='||document."
                "getElementById('trusted').getAttribute('nonce')!==''||document.querySelector('"
                "script[nonce=\"dGVzdA==\"]')||document.body.innerHTML.includes('dGVzdA=='))throw "
                "Error('nonce visibility');");
        if (mode == 0 || mode == 3)
            run(js, std::string("if(!globalThis.trusted||!globalThis.trustedDynamic||!globalThis."
                                "nonceExternal||globalThis.evalAllowed!==") +
                        (mode == 0 ? "true" : "false") +
                        ")throw Error('nonce authorization/eval');");
        else
            run(js, "if(globalThis.trusted||globalThis.trustedDynamic||globalThis.nonceExternal)"
                    "throw Error('multiple policy/strict-dynamic bypass');");
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
