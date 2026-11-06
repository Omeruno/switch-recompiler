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
    CHECK(argc == 3);
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
        auto doc = litehtml::document::createFromString("<div id=box>before</div>", &c);
        doc->render(1280);
        visual::Scripts js(doc, c, resources);
        auto start = visual::Scripts::clock_ms();
        for (int i = 0; i < 10 && !js.ready(); ++i)
            js.poll(start + i);
        CHECK(js.ready());
        auto run = [&](const char *code) {
            if (!js.eval(code)) {
                fprintf(stderr, "JS %s\n", js.error().c_str());
                std::exit(1);
            }
        };
        run("globalThis.trace=[];setTimeout(()=>{trace.push('t1');Promise.resolve().then(()=>{"
            "trace.push('p1');queueMicrotask(()=>trace.push('nested'));});queueMicrotask(()=>trace."
            "push('q1'));},0);setTimeout(()=>trace.push('t2'),0);");
        js.poll(start + 100);
        run("if(trace.join(',')!=='t1,p1,q1,nested,t2')throw Error('timer checkpoint '+trace);");
        run("trace=[];requestAnimationFrame(t=>{trace.push('a');globalThis.stamp=t;Promise.resolve("
            ").then(()=>trace.push('pa'));requestAnimationFrame(()=>trace.push('next'));});"
            "requestAnimationFrame(t=>{if(t!==stamp)throw Error('frame time');trace.push('b');});");
        js.poll(start + 200);
        run("if(trace.length)throw Error('frame used timer queue');");
        js.render_frame(start + 200);
        run("if(trace.join(',')!=='a,pa,b'||!Number.isFinite(stamp)||stamp<0)throw Error('frame "
            "checkpoint '+trace);");
        js.render_frame(start + 205);
        run("if(trace.join(',')!=='a,pa,b')throw Error('early next frame');");
        js.render_frame(start + 220);
        run("if(trace.join(',')!=='a,pa,b,next')throw Error('nested frame');");
        run("trace=[];requestAnimationFrame(()=>{trace.push('cancel');cancelAnimationFrame("
            "cancelled);});const "
            "cancelled=requestAnimationFrame(()=>trace.push('BAD'));setTimeout(()=>trace.push('"
            "timer'),0);");
        js.render_frame(start + 240);
        run("if(trace.join(',')!=='cancel')throw Error('cancel frame');");
        js.poll(start + 250);
        run("if(trace.join(',')!=='cancel,timer')throw Error('separate timer handles');");
        run("globalThis.microCount=0;globalThis.timerRan=false;function "
            "chain(){if(++microCount<200)Promise.resolve().then(chain);}Promise.resolve().then("
            "chain);setTimeout(()=>timerRan=true,0);");
        js.poll(start + 300);
        run("if(timerRan)throw Error('task jumped unfinished checkpoint');");
        for (int i = 0; i < 10; ++i)
            js.poll(start + 310 + i);
        run("if(microCount!==200||!timerRan)throw Error('microtask continuation');");
        run("globalThis.rejections=[];globalThis.handled=[];addEventListener('unhandledrejection',"
            "e=>{rejections.push(e.reason);e.preventDefault();});addEventListener('"
            "rejectionhandled',e=>handled.push(e.promise));Promise.reject('sync').catch(()=>{});"
            "const "
            "micro=Promise.reject('micro');queueMicrotask(()=>micro.catch(()=>{}));globalThis.late="
            "Promise.reject('late');");
        js.poll(start + 400);
        run("if(rejections.join(',')!=='late')throw Error('rejection checkpoint "
            "'+rejections);late.catch(()=>{});");
        js.poll(start + 401);
        run("if(handled.length!==1||handled[0]!==late)throw Error('late catch');");
        run("globalThis.geometry=[];requestAnimationFrame(()=>{const "
            "box=document.getElementById('box');box.style.width='123px';geometry.push(box."
            "getBoundingClientRect().width);Promise.resolve().then(()=>box.style.width='231px');})"
            ";");
        js.render_frame(start + 500);
        // A native font/layout pass can exhaust the frame quantum. Complete its
        // checkpoint on the next turn before observing the final layout.
        js.poll(start + 501);
        js.flush();
        run("if(geometry[0]!==123||document.getElementById('box').getBoundingClientRect().width!=="
            "231)throw Error('frame geometry');");
        run("globalThis.timerThis=false;globalThis.frameThis=false;setTimeout(function(a,b){"
            "timerThis=this===globalThis&&a==='a'&&b==='b';},0,'a','b');requestAnimationFrame("
            "function(){frameThis=this===globalThis;});");
        js.poll(start + 550);
        js.render_frame(start + 550);
        run("if(!timerThis||!frameThis)throw Error('callback receiver/arguments');");
        run("globalThis.microSurvived=false;queueMicrotask(()=>{throw Error('microtask "
            "failure');});queueMicrotask(()=>microSurvived=true);");
        js.poll(start + 570);
        js.poll(start + 571);
        run("if(!microSurvived)throw Error('microtask failure stopped queue');");
        run("trace=[];setTimeout(()=>{throw Error('callback "
            "failure');},0);setTimeout(()=>trace.push('survived'),0);");
        js.poll(start + 600);
        run("if(trace[0]!=='survived')throw Error('timer failure stopped queue');");
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    browser_profile_close();
    curl_global_cleanup();
    TTF_Quit();
    SDL_Quit();
    puts("PASS checkpoints between tasks/frames, rAF cancellation and timestamps, long microtask "
         "continuation, rejection events, geometry reads and callback failure isolation");
}
