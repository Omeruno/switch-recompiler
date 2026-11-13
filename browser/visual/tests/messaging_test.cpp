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
static void evaluate(visual::Scripts &js, const std::string &source) {
    if (!js.eval(source, 1000)) {
        fprintf(stderr, "JS: %s\n", js.error().c_str());
        std::exit(1);
    }
}
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
        c.base = c.page_url = "https://parent.test/";
        auto doc = litehtml::document::createFromString("<html><body></body></html>", &c);
        doc->render(1280);
        auto hub = std::make_shared<visual::MessageHub>();
        auto root = hub->create_window();
        visual::Scripts js(doc, c, resources, {}, hub, root);
        std::ifstream file(std::string(argv[3]) + "/messaging.js");
        CHECK(file);
        evaluate(js, std::string((std::istreambuf_iterator<char>(file)), {}));
        auto until = visual::Scripts::clock_ms() + 3000;
        while (visual::Scripts::clock_ms() < until) {
            js.poll(visual::Scripts::clock_ms());
            SDL_Delay(1);
        }
        evaluate(js, "if(!globalThis.messagingDone)throw "
                     "Error(globalThis.messagingError||'Messaging unfinished');");
        // Separate QuickJS runtimes, actual native origins and serialized wire data.
        visual::Resources child_resources(argv[1]);
        visual::Container child_c(renderer, child_resources, argv[2]);
        child_c.base = child_c.page_url = "https://child.test/";
        auto child_doc =
            litehtml::document::createFromString("<html><body></body></html>", &child_c);
        child_doc->render(300);
        auto child = hub->create_window(root);
        evaluate(js, R"JS(
            globalThis.roundTrip=false;globalThis.foreignSource=null;
            onmessage=e=>{
                if(e.data?.kind!=='hello')return;
                if(e.origin!=='https://child.test'||e.source===window||!e.isTrusted)
                    throw Error('Foreign origin/source');
                if(e.data.self!==e.data||e.data.map.get(e.data)!==99||e.data.bytes[0]!==71)
                    throw Error('Cross-runtime clone');
                foreignSource=e.source;
                try{e.source.document;throw Error('Foreign DOM exposed');}
                catch(err){if(err.name!=='SecurityError')throw err;}
                const returnChannel=new MessageChannel();
                returnChannel.port1.onmessage=event=>{
                    if(event.data!=='port-reply')throw Error('Port exchange');roundTrip=true;
                };
                e.source.postMessage({kind:'reply',value:e.data.value},e.origin,[returnChannel.port2]);
                e.ports[0].close();
            };
        )JS");
        {
            visual::Scripts child_js(child_doc, child_c, child_resources, {}, hub, child);
            evaluate(child_js, R"JS(
                if(parent===window||top!==parent||parent.parent!==parent)throw Error('Window topology');
                globalThis.childDone=false;globalThis.secret='child-only';const ch=new MessageChannel();
                onmessage=e=>{
                    if(e.data.kind!=='reply')return;
                    if(e.source!==parent||e.origin!=='https://parent.test'||e.data.value!==42)
                        throw Error('Reply origin/source');
                    e.ports[0].postMessage('port-reply');childDone=true;
                };
                const payload={kind:'hello',value:42,bytes:new Uint8Array([71,72])};
                payload.self=payload;payload.map=new Map([[payload,99]]);
                parent.postMessage(payload,'https://parent.test',[ch.port1,payload.bytes.buffer]);
                if(payload.bytes.byteLength!==0)throw Error('Cross-runtime buffer not detached');
            )JS");
            for (int i = 0; i < 80; ++i) {
                auto now = visual::Scripts::clock_ms();
                js.poll(now);
                child_js.poll(now);
            }
            CHECK(js.error().empty());
            evaluate(child_js, "if(!childDone)throw Error('Child reply missing');");
            evaluate(js, "if(!roundTrip||globalThis.secret!==undefined)throw Error('Separate "
                         "realms/port reply');");
        }
        evaluate(js, "if(!foreignSource.closed)throw Error('Destroyed realm not closed');");
        // Failed binding must never tear down an already running realm.
        bool rejected = false;
        try {
            visual::Scripts duplicate(doc, c, resources, {}, hub, root);
        } catch (const std::exception &) {
            rejected = true;
        }
        CHECK(rejected && hub->window(root)->alive && hub->window(root)->container == &c);
        // Backpressure must not consume transferable ownership on failure.
        evaluate(js,
                 "const blocked=new MessageChannel(),spare=new MessageChannel();for(let "
                 "i=0;i<64;i++)blocked.port1.postMessage(i);const preserved=new ArrayBuffer(8);let "
                 "limited=false;try{blocked.port1.postMessage('overflow',{transfer:[spare.port2,"
                 "preserved]});}catch(e){limited=e.name==='QuotaExceededError';}if(!limited||"
                 "preserved.byteLength!==8)throw Error('Quota "
                 "atomicity');globalThis.recovered=false;spare.port2.onmessage=e=>{recovered=e."
                 "data===99;};spare.port1.postMessage(99);");
        for (int i = 0; i < 10; ++i)
            js.poll(visual::Scripts::clock_ms());
        evaluate(js, "if(!recovered)throw Error('Port lost on rejected post');");
    }
    {
        visual::Resources resources(argv[1]);
        visual::Container c(renderer, resources, argv[2]);
        std::ifstream file(std::string(argv[1]) + "/index.html");
        CHECK(file);
        std::string html((std::istreambuf_iterator<char>(file)), {});
        auto doc = litehtml::document::createFromString(html.c_str(), &c);
        doc->render(1280);
        visual::Scripts js(doc, c, resources);
        for (int i = 0; i < 100; ++i) {
            SDL_Delay(1);
            resources.pump();
            js.poll(visual::Scripts::clock_ms());
            js.flush();
        }
        if (!js.error().empty())
            fprintf(stderr, "Offline demo JS: %s\n", js.error().c_str());
        evaluate(js, "if(!document.getElementById('messaging-status').textContent.includes('№1'))"
                     "throw Error(document.getElementById('messaging-status').textContent);");
        CHECK(js.click(doc->root()->select_one("#messaging-run"), 10, 10));
        for (int i = 0; i < 20; ++i)
            js.poll(visual::Scripts::clock_ms());
        evaluate(js, "if(!document.getElementById('messaging-status').textContent.includes('№2'))"
                     "throw Error('Offline demo repeat failed');");
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    browser_profile_close();
    curl_global_cleanup();
    TTF_Quit();
    SDL_Quit();
    puts("PASS async channels, structured clone/transfer, cross-runtime origins/source identity, "
         "isolation, teardown and quota atomicity");
}
