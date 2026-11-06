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
        auto doc = litehtml::document::createFromString(
            "<!doctype html><html><head></head><body></body></html>", &c,
            litehtml::master_css + visual::control_styles());
        doc->render(1280);
        visual::Scripts js(doc, c, resources);
        auto now = visual::Scripts::clock_ms();
        auto run = [&](const std::string &code, unsigned budget = 500) {
            if (!js.eval(code, budget)) {
                fprintf(stderr, "JS: %s\n", js.error().c_str());
                std::exit(1);
            }
        };
        auto frame = [&]() {
            now += 20;
            js.poll(now);
            js.render_frame(now);
        };
        for (int i = 0; i < 10 && !js.ready(); i++)
            frame();
        CHECK(js.ready());
        std::ifstream input(std::string(argv[3]) + "/layout-observers.js");
        CHECK(input);
        std::string source((std::istreambuf_iterator<char>(input)), {});
        run(source);
        for (int i = 0; i < 160; i++)
            frame();
        run("if(globalThis.layoutFixtureError||!globalThis.layoutFixtureResult)throw "
            "Error(layoutFixtureError||'unfinished');");
        c.viewport_width = 640;
        c.viewport_height = 636;
        c.relayout(doc);
        frame();
        run("if(fixtureMedia.matches||mediaChanges.join(',')!=='false'||resizeEvents!==1)throw "
            "Error('narrow media/resize '+mediaChanges+'/'+resizeEvents);");
        frame();
        run("if(resizeEvents!==1)throw Error('duplicate resize');");
        c.viewport_width = 1280;
        c.relayout(doc);
        frame();
        run("if(mediaChanges.join(',')!=='false,true'||resizeEvents!==2)throw Error('wide "
            "media');fixtureMedia.onchange=null;scrollTo(0,20);scrollBy(0,20);");
        frame();
        run("if(scrollEvents!==1||scrollY!==40)throw Error('scroll coalescing');scrollTo(0,0);");
        frame();
        // Records can be consumed before the queued IO task, and are not delivered twice.
        run("globalThis.ioCalls=0;globalThis.takeObserver=new "
            "IntersectionObserver(()=>ioCalls++);takeObserver.observe(document.getElementById('far-"
            "target'));");
        now += 20;
        js.render_frame(now);
        run("const "
            "records=takeObserver.takeRecords();if(records.length!==1||records[0].isIntersecting)"
            "throw Error('takeRecords');takeObserver.disconnect();");
        frame();
        run("if(ioCalls)throw Error('records double delivery');");
        // The spec resolves all rootMargin percentages against root width.
        // Chromium 151 currently uses height for vertical percentages; keep this
        // normative check separate from the shared browser comparison fixture.
        run("document.getElementById('clipped').style.top='110px';globalThis.percentEntry=null;"
            "globalThis.percentObserver=new "
            "IntersectionObserver(e=>percentEntry=e[0],{root:document.getElementById('clip-root'),"
            "rootMargin:'10% 0px'});percentObserver.observe(document.getElementById('clipped'));");
        frame();
        frame();
        run("if(!percentEntry||percentEntry.rootBounds.height!==140||Math.abs(percentEntry."
            "intersectionRatio-.1)>1e-6)throw Error('percent root "
            "margin');percentObserver.disconnect();");
        // Repeated CSS refreshes must keep a current native renderer available.
        auto retained = doc->root()->select_one("#observed");
        for (int i = 0; i < 20; i++) {
            run("document.getElementById('observed').style.width='" + std::to_string(160 + i) +
                "px';");
            js.flush();
            CHECK(retained->get_render_item());
        }
        // Self-resizing callbacks yield to later frames with an observable loop error.
        run("globalThis.loops=0;globalThis.loopErrors=0;addEventListener('error',e=>{if(e.message&&"
            "e.message.includes('ResizeObserver loop')){loopErrors++;e.preventDefault();}});const "
            "looping=document.getElementById('observed');globalThis.loopObserver=new "
            "ResizeObserver(()=>{loops++;looping.style.width=(300+loops)+'px';});loopObserver."
            "observe(looping);");
        frame();
        run("if(loops!==1||!loopErrors)throw Error('resize loop "
            "guard');loopObserver.disconnect();");
        bool loop = false;
        for (auto &issue : js.issues().entries())
            if (issue.code == "resize-observer-loop")
                loop = true;
        CHECK(loop);
        // Unsupported modes reject rather than reporting invented device-pixel/visibility data.
        run("let rejected=0;try{new "
            "ResizeObserver(()=>{}).observe(document.body,{box:'device-pixel-content-box'});}catch("
            "e){rejected++;}try{new "
            "IntersectionObserver(()=>{},{threshold:2});}catch(e){rejected++;}try{new "
            "IntersectionObserver(()=>{},{rootMargin:'2em'});}catch(e){rejected++;}try{new "
            "IntersectionObserver(()=>{},{trackVisibility:true});}catch(e){rejected++;}if(rejected!"
            "==4)throw Error('validation');");
        // Exhaustion is bounded and disconnect releases target capacity.
        run("const many=new ResizeObserver(()=>{}),targets=[];let limited=false;for(let "
            "i=0;i<129;i++){const "
            "el=document.createElement('div');targets.push(el);try{many.observe(el);}catch(e){"
            "limited=e instanceof RangeError;break;}}if(!limited)throw Error('observer "
            "quota');many.disconnect();const reuse=new "
            "ResizeObserver(()=>{});reuse.observe(targets[0]);reuse.disconnect();");
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    browser_profile_close();
    curl_global_cleanup();
    TTF_Quit();
    SDL_Quit();
    puts("PASS native resize boxes/lifecycle/loop guard, intersection clipping/thresholds/queue, "
         "scroll and media events, quotas and cleanup");
}
