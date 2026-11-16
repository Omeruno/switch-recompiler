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
        auto doc = litehtml::document::createFromString("<html><body></body></html>", &c);
        doc->render(1280);
        visual::Scripts js(doc, c, resources);
        auto run = [&](const std::string &source) {
            if (!js.eval(source, 5000)) {
                fprintf(stderr, "Codec JS: %s\n", js.error().c_str());
                std::exit(1);
            }
        };
        std::ifstream file(std::string(argv[3]) + "/text-codec.js");
        CHECK(file);
        run(std::string((std::istreambuf_iterator<char>(file)), {}));
        run("if(!globalThis.textCodecDone)throw Error('Codec fixture incomplete');");
        run(R"JS(
   // Native WHATWG queue recovery after fatal streaming errors. Chromium's ICU
   // decoder retains/drops different state here, so this is not a shared assertion.
   const fatal=new TextDecoder('utf-8',{fatal:true});fatal.decode(new Uint8Array([0xe2]),{stream:true});
   try{fatal.decode(new Uint8Array([65,66]),{stream:true});throw Error('Missing fatal error');}catch(e){if(e.name!=='TypeError')throw e;}
   if(fatal.decode(new Uint8Array([67]),{stream:true})!=='ABC')throw Error('UTF-8 fatal queue recovery');
   const u16fatal=new TextDecoder('utf-16le',{fatal:true});u16fatal.decode(new Uint8Array([0,0xd8]),{stream:true});
   try{u16fatal.decode(new Uint8Array([65,0,66,0]),{stream:true});throw Error('Missing UTF16 error');}catch(e){if(e.name!=='TypeError')throw e;}
   if(u16fatal.decode()!=='AB')throw Error('UTF-16 fatal queue recovery');
   const quota=new TextDecoder('utf-8',{fatal:true});quota.decode(new Uint8Array([0xe2]),{stream:true});let failed=false;
   try{quota.decode(new Uint8Array(524289),{stream:true});}catch(e){failed=e.name==='RangeError';}
   if(!failed||quota.decode(new Uint8Array([0x82,0xac]))!=='€')throw Error('Quota must preserve decoder state');
   const source='a'.repeat(524288);if(new TextEncoder().encode(source).length!==524288)throw Error('Boundary encoding');
   let limited=false;try{new TextEncoder().encode(source+'a');}catch(e){limited=e.name==='RangeError';}if(!limited)throw Error('Encoder quota');
   const largeDestination=new Uint8Array(1048576);if(new TextEncoder().encodeInto('Ё',largeDestination).written!==2)throw Error('Large destination');
   for(let i=0;i<300;i++)if(new TextDecoder('utf-8',{fatal:true}).decode(new Uint8Array([65]))!=='A')throw Error('Decoder lifetime');
  )JS");
    }
    {
        visual::Resources resources(argv[1]);
        visual::Container c(renderer, resources, argv[2]);
        auto doc = litehtml::document::createFromString(
            "<html><body><button id='text-codec-run'>Run</button><p "
            "id='text-codec-status'></p><script src='text-codec-demo.js'></script></body></html>",
            &c);
        doc->render(1280);
        visual::Scripts js(doc, c, resources);
        for (int i = 0; i < 20; ++i) {
            resources.pump();
            js.poll(visual::Scripts::clock_ms());
        }
        CHECK(js.eval("if(!document.getElementById('text-codec-status').textContent.includes('"
                      "Привет · Ё'))throw Error('Packaged encoding "
                      "demo');document.getElementById('text-codec-run').click();"));
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    browser_profile_close();
    curl_global_cleanup();
    TTF_Quit();
    SDL_Quit();
    puts("PASS strict/streaming UTF-8, UTF-16, Windows indexes, BOM/error state, encodeInto, "
         "BufferSource, quotas and cleanup");
}
