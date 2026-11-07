#include "scripts.h"
extern "C" {
#include "profile.h"
}
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
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
        visual::Resources resources(TEST_ASSETS);
        auto *config = resources.request(std::string(argv[2]) + "/api-config", 4096);
        auto *cookies = resources.request(std::string(argv[2]) + "/set", 4096);
        auto end = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (resources.pending() && std::chrono::steady_clock::now() < end) {
            resources.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(config->transfer_ok && cookies->transfer_ok);
        visual::Container c(renderer, resources, TEST_FONT);
        c.base = c.page_url = std::string(argv[2]) + "/api-page";
        auto doc = litehtml::document::createFromString(
            "<!doctype html><html><head></head><body></body></html>", &c);
        doc->render(1280);
        visual::Scripts js(doc, c, resources);
        auto run = [&](const std::string &code) {
            if (!js.eval(code, 500)) {
                fprintf(stderr, "JS: %s\n", js.error().c_str());
                std::exit(1);
            }
        };
        auto drive = [&]() {
            auto until = std::chrono::steady_clock::now() + std::chrono::seconds(6);
            unsigned turns = 0;
            do {
                resources.pump();
                js.poll(visual::Scripts::clock_ms());
                js.flush();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                ++turns;
            } while ((resources.pending() || !js.ready() || turns < 10) &&
                     std::chrono::steady_clock::now() < until);
            CHECK(!resources.pending() && js.ready());
        };
        drive();
        run("globalThis.config=" + std::string(config->data.begin(), config->data.end()) +
            ";globalThis.cross=config.cross;");
        std::ifstream type_input(TEST_FIXTURES "/network-types.js");
        CHECK(type_input);
        run(std::string((std::istreambuf_iterator<char>(type_input)), {}));
        drive();
        run("if(globalThis.networkTypesError||!globalThis.networkTypesResult)throw "
            "Error(globalThis.networkTypesError||'types "
            "unfinished');");
        run("globalThis.gameNetworkDone=false;globalThis.gameNetworkError='';(async()=>{"
            "const good=await "
            "WebAssembly.instantiateStreaming(fetch('/api-wasm?large=1'),{env:{memory:new "
            "WebAssembly.Memory({initial:1,maximum:1}),score:new "
            "WebAssembly.Global({value:'i32',mutable:true},0),observe:()=>0}});"
            "good.instance.exports.step(0,0,0,95,20,1);if(good.instance.exports.score.value!==1)"
            "throw Error('real streamed game');"
            "const remote=await WebAssembly.compileStreaming(fetch(cross+'/api-wasm'));if(!(remote "
            "instanceof WebAssembly.Module))throw Error('CORS wasm');"
            "for(const url of "
            "['/api-wasm?mime=bad','/api-wasm?status=404',cross+'/api-wasm?cors=deny','/"
            "api-wasm?over=1']){let rejected=false;try{await "
            "WebAssembly.compileStreaming(fetch(url));}catch(e){rejected=e instanceof "
            "TypeError;}if(!rejected)throw Error('stream rejection '+url);}"
            "const abort=new AbortController();const "
            "promise=WebAssembly.compileStreaming(fetch('/"
            "api-slow',{signal:abort.signal}));abort.abort();try{await promise;throw "
            "Error('missing abort');}catch(e){if(e.name!=='AbortError')throw e;}"
            "globalThis.gameNetworkDone=true;})().catch(e=>{globalThis.gameNetworkError=e.name+': "
            "'+e.message;});");
        auto game_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while (std::chrono::steady_clock::now() < game_deadline) {
            resources.pump();
            js.poll(visual::Scripts::clock_ms());
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        run("if(gameNetworkError||!gameNetworkDone)throw Error(gameNetworkError||'Wasm network "
            "unfinished');");
        std::ifstream input(TEST_FIXTURES "/web-network.js");
        CHECK(input);
        run(std::string((std::istreambuf_iterator<char>(input)), {}));
        // The fixture chains requests; native pending() can be empty between tasks.
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while (!doc->root()->get_attr("data-network-done") &&
               !doc->root()->get_attr("data-network-error") &&
               std::chrono::steady_clock::now() < deadline) {
            resources.pump();
            js.poll(visual::Scripts::clock_ms());
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        drive();
        run("if(globalThis.webNetworkError||!globalThis.webNetworkDone)throw "
            "Error(webNetworkError||'network unfinished');");
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    browser_profile_close();
    curl_global_cleanup();
    TTF_Quit();
    SDL_Quit();
    puts("PASS native Fetch/XHR POST/binary/headers, CORS preflight/exposure, cookies, redirects, "
         "TLS, cancellation, timeout and reentrancy");
}
