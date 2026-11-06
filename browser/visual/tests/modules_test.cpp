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
        visual::Container c(renderer, resources, TEST_FONT);
        c.base = c.page_url = std::string(argv[2]) + "/esm-page";
        auto *config = resources.request(std::string(argv[2]) + "/esm-config", 4096);
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (resources.pending() && std::chrono::steady_clock::now() < until) {
            resources.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(config->transfer_ok);
        auto doc = litehtml::document::createFromString(
            R"HTML(<!doctype html><html><head><script>globalThis.moduleTrace=[];globalThis.lifecycle=[];globalThis.moduleUnhandled=[];addEventListener('unhandledrejection',e=>{moduleUnhandled.push(String(e.reason));e.preventDefault();});addEventListener('DOMContentLoaded',()=>lifecycle.push('dom'));addEventListener('load',()=>lifecycle.push('load'));</script><script nomodule>globalThis.badNoModule=true;</script><script type=module src='/esm-fixtures/main.js'></script></head><body></body></html>)HTML",
            &c);
        doc->render(1280);
        visual::Scripts js(doc, c, resources,
                           {"script-src 'self' 'unsafe-inline'; connect-src 'none'"});
        auto run = [&](const std::string &code) {
            if (!js.eval(code, 500)) {
                fprintf(stderr, "JS %s\n", js.error().c_str());
                std::exit(1);
            }
        };
        auto drive = [&](const std::function<bool()> &done) {
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
            do {
                resources.pump();
                js.poll(visual::Scripts::clock_ms());
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } while (!done() && std::chrono::steady_clock::now() < deadline);
            if (!done()) {
                js.eval("if(globalThis.fixtureError)throw Error(fixtureError+' / "
                        "'+JSON.stringify(globalThis.moduleUnhandled));");
                fprintf(stderr, "Module timeout: %s\n", js.error().c_str());
                for (auto &e : js.issues().entries())
                    fprintf(stderr, "%s %s\n", e.category.c_str(), e.code.c_str());
                std::exit(1);
            }
        };
        drive([&] { return doc->root()->get_attr("data-module-done") && js.ready(); });
        run("if(globalThis.badNoModule||moduleTrace.join(',')!=='leaf,default,cycle-b,cycle-a,main,"
            "lazy,leaf,done')throw Error('trace "
            "'+moduleTrace);if(lifecycle.join(',')!=='dom,load')throw Error('lifecycle "
            "'+lifecycle);");
        run("globalThis.classicImported=false;import('./esm-fixtures/"
            "extra.js').then(m=>{if(m.extra!=='extra')throw "
            "Error('namespace');classicImported=true;document.documentElement.setAttribute('data-"
            "classic-import','1');});");
        drive([&] { return doc->root()->get_attr("data-classic-import") != nullptr; });
        run("globalThis.moduleErrors=[];(async()=>{for(const name of "
            "['parse-error','link-error','runtime-error','parent-error']){let a,b;try{await "
            "import('./esm-fixtures/'+name+'.js');}catch(e){a=e;}try{await "
            "import('./esm-fixtures/"
            "'+name+'.js');}catch(e){b=e;}if(!a||!b||(name==='link-error'?a===b:a!==b))throw "
            "Error('failure "
            "cache "
            "'+name);moduleErrors.push(a.name);}document.documentElement.setAttribute('data-errors'"
            ",'1');})().catch(e=>{globalThis.fixtureError=String(e);document.documentElement."
            "setAttribute('data-errors','1');});");
        drive([&] { return doc->root()->get_attr("data-errors") != nullptr; });
        run("if(globalThis.fixtureError||moduleErrors.join(',')!=='SyntaxError,SyntaxError,Error,"
            "SyntaxError')"
            "throw Error(fixtureError||moduleErrors);");
        run(R"JS(globalThis.negativeDone=false;(async()=>{
          let denied=0;for(const spec of ['/esm-wrong.js','/esm-large.js','bare-package']){try{await import(spec);}catch(e){if(e instanceof TypeError)denied++;}}
          try{await import('./esm-fixtures/extra.js',{with:{type:'json'}});}catch(e){if(e instanceof TypeError)denied++;}
          if(denied!==4||globalThis.badMimeExecuted||globalThis.largeModuleExecuted)throw Error('module rejection');
          const inline=document.createElement('script');inline.type='module';inline.text="import {extra} from './esm-fixtures/extra.js';if(document.currentScript!==null||import.meta.url!==location.href)throw Error('inline meta');globalThis.inlineRuns=(globalThis.inlineRuns||0)+1;document.documentElement.setAttribute('data-inline','1');";document.body.appendChild(inline);
          if(globalThis.inlineRuns)throw Error('inline module ran synchronously');
          const inert=document.createElement('div');inert.innerHTML='<script type=module>globalThis.badInertModule=true;</script>';document.body.appendChild(inert);
          document.documentElement.setAttribute('data-negative','1');
        })().catch(e=>{globalThis.fixtureError=String(e);document.documentElement.setAttribute('data-negative','1');document.documentElement.setAttribute('data-inline','error');});)JS");
        drive([&] {
            return doc->root()->get_attr("data-negative") && doc->root()->get_attr("data-inline");
        });
        run("if(globalThis.fixtureError||inlineRuns!==1||globalThis.badInertModule||"
            "moduleUnhandled.length)throw "
            "Error(globalThis.fixtureError||'dynamic inline "
            "'+globalThis.inlineRuns+'/"
            "'+JSON.stringify(moduleUnhandled));globalThis.moduleOrder=[];function "
            "orderedModule(src){const "
            "s=document.createElement('script');s.type='module';s.async=false;s.src=src;document."
            "body.appendChild(s);}orderedModule('/esm-fixtures/slow.js');orderedModule('/"
            "esm-fixtures/fast.js');setTimeout(()=>moduleOrder.push('timer'),0);");
        drive([&] { return !resources.pending(); });
        for (int i = 0; i < 15; i++) {
            js.poll(visual::Scripts::clock_ms());
        }
        run("if(moduleOrder.indexOf('timer')>moduleOrder.indexOf('slow')||moduleOrder.indexOf('"
            "slow')>moduleOrder.indexOf('fast')||moduleOrder.length!==3)throw Error('ordered "
            "modules '+moduleOrder);");
    }
    {
        visual::Resources resources(TEST_ASSETS);
        visual::Container c(renderer, resources, TEST_FONT);
        c.base = c.page_url = std::string(argv[2]) + "/esm-lifecycle";
        auto doc = litehtml::document::createFromString(
            R"HTML(<script>globalThis.moduleLifecycle=[];globalThis.unhandledModules=0;addEventListener('DOMContentLoaded',()=>moduleLifecycle.push('dom'));addEventListener('load',()=>moduleLifecycle.push('window-load'));addEventListener('error',()=>{moduleLifecycle.push('error');document.documentElement.setAttribute('data-runtime-error','1');});addEventListener('unhandledrejection',()=>unhandledModules++);</script><script type=module src='/esm-fixtures/tla-error.js' onload="moduleLifecycle.push('module-load')" onerror="moduleLifecycle.push('module-error')"></script>)HTML",
            &c);
        doc->render(1280);
        visual::Scripts js(doc, c, resources);
        auto until = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        do {
            resources.pump();
            js.poll(visual::Scripts::clock_ms());
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (!doc->root()->get_attr("data-runtime-error") &&
                 std::chrono::steady_clock::now() < until);
        if (!js.eval("if(moduleLifecycle.join(',')!=='start,module-load,dom,window-load,end,error'|"
                     "|unhandledModules)throw Error('TLA lifecycle '+moduleLifecycle);")) {
            fprintf(stderr, "Lifecycle: %s\n", js.error().c_str());
            std::exit(1);
        }
    }

    {
        visual::Resources resources(TEST_ASSETS);
        visual::Container c(renderer, resources, TEST_FONT);
        c.base = c.page_url = std::string(argv[2]) + "/esm-cross";
        auto *config = resources.request(std::string(argv[2]) + "/esm-config", 4096);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (resources.pending() && std::chrono::steady_clock::now() < deadline) {
            resources.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(config->transfer_ok);
        std::string config_json(config->data.begin(), config->data.end());
        auto doc = litehtml::document::createFromString(
            "<!doctype html><html><head></head><body></body></html>", &c);
        doc->render(1280);
        visual::Scripts js(doc, c, resources);
        auto run = [&](const std::string &code) {
            if (!js.eval(code, 500)) {
                fprintf(stderr, "Cross module: %s\n", js.error().c_str());
                std::exit(1);
            }
        };
        auto drive = [&](const char *attribute) {
            auto until = std::chrono::steady_clock::now() + std::chrono::seconds(6);
            do {
                resources.pump();
                js.poll(visual::Scripts::clock_ms());
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            } while (!doc->root()->get_attr(attribute) && std::chrono::steady_clock::now() < until);
            if (!doc->root()->get_attr(attribute)) {
                fprintf(stderr, "Cross timeout: %s\n", js.error().c_str());
                std::exit(1);
            }
        };
        run("globalThis.moduleConfig=" + config_json +
            ";globalThis.crossModule=moduleConfig.cross;");
        run(R"JS((async()=>{
            const check=(ok,label)=>{if(!ok)throw Error(label);};
            const remote=await import(crossModule+'/esm-fixtures/extra.js');check(remote.extra==='extra','anonymous CORS');
            const redirected=await import('/esm-redirect.js?target=/esm-fixtures/meta.js');check(redirected.value==='extra'&&redirected.url.endsWith('/esm-fixtures/meta.js'),'redirect base/meta');
            let errors=0;try{await import(crossModule+'/esm-deny.js');}catch(e){if(e instanceof TypeError)errors++;}check(errors===1&&!globalThis.badCorsExecuted,'module CORS denied');
            await new Promise(resolve=>{const s=document.createElement('script');s.type='module';s.crossOrigin='use-credentials';s.src=crossModule+'/esm-fixtures/extra.js?credentials';s.onerror=resolve;document.body.appendChild(s);});
            const meta=document.createElement('meta');meta.httpEquiv='Content-Security-Policy';meta.content="script-src 'self' 'unsafe-inline'";document.head.appendChild(meta);
            try{await import(crossModule+'/esm-fixtures/extra.js');}catch(e){errors++;}
            try{await import('/esm-redirect.js?target='+encodeURIComponent(crossModule+'/esm-deny.js?blocked'));}catch(e){errors++;}
            check(errors===3,'module CSP redirect/cache');document.documentElement.setAttribute('data-cross-done','1');
        })().catch(e=>{globalThis.crossFixtureError=String(e);document.documentElement.setAttribute('data-cross-done','1');});)JS");
        drive("data-cross-done");
        run("if(globalThis.crossFixtureError)throw Error(crossFixtureError);");
    }

    for (auto name : {"parse-error", "link-error", "runtime-error"}) {
        visual::Resources resources(TEST_ASSETS);
        visual::Container c(renderer, resources, TEST_FONT);
        c.base = c.page_url = std::string(argv[2]) + "/esm-errors";
        std::string html =
            "<script>globalThis.errorsTrace=[];addEventListener('error',()=>errorsTrace.push('"
            "window-error'));addEventListener('load',()=>errorsTrace.push('window-load'));</"
            "script><script type=module src='/esm-fixtures/" +
            std::string(name) +
            ".js' onload=\"errorsTrace.push('script-load')\" "
            "onerror=\"errorsTrace.push('script-error')\"></script>";
        auto doc = litehtml::document::createFromString(html, &c);
        doc->render(1280);
        visual::Scripts js(doc, c, resources);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        do {
            resources.pump();
            js.poll(visual::Scripts::clock_ms());
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (!js.ready() && std::chrono::steady_clock::now() < deadline);
        if (!js.eval("if(errorsTrace.join(',')!=='window-error,script-load,window-load')throw "
                     "Error('module error events '+errorsTrace);")) {
            fprintf(stderr, "%s events: %s\n", name, js.error().c_str());
            std::exit(1);
        }
    }

    {
        visual::Resources resources(TEST_ASSETS);
        visual::Container c(renderer, resources, TEST_FONT);
        c.base = c.page_url = std::string(argv[2]) + "/esm-quota";
        auto doc =
            litehtml::document::createFromString("<html><head></head><body></body></html>", &c);
        doc->render(1280);
        visual::Scripts js(doc, c, resources);
        CHECK(js.eval("globalThis.quotaFailure='';Promise.allSettled(Array.from({length:40},(_,i)=>"
                      "import('./esm-fixtures/extra.js?quota='+i))).then(results=>{const "
                      "denied=results.filter(x=>x.status==='rejected');if(denied.length<8||denied."
                      "some(x=>!(x.reason instanceof "
                      "TypeError))||results.some(x=>x.status==='fulfilled'&&x.value.extra!=='extra'"
                      "))throw Error('module import "
                      "quota');document.documentElement.setAttribute('data-quota-done','1');})."
                      "catch(e=>{quotaFailure=String(e);document.documentElement.setAttribute('"
                      "data-quota-done','1');});",
                      500));
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        do {
            resources.pump();
            js.poll(visual::Scripts::clock_ms());
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        } while (!doc->root()->get_attr("data-quota-done") &&
                 std::chrono::steady_clock::now() < deadline);
        CHECK(doc->root()->get_attr("data-quota-done"));
        CHECK(js.eval("if(quotaFailure)throw Error(quotaFailure);"));
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    browser_profile_close();
    curl_global_cleanup();
    TTF_Quit();
    SDL_Quit();
    puts("PASS module graph, scope, imports/exports/live bindings/cycles, TLA/dynamic import, "
         "identity and failure cache");
}
