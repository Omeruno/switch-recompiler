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
static void run(visual::Scripts &s, const std::string &code) {
    if (!s.eval(code)) {
        fprintf(stderr, "JS error: %s\n", s.error().c_str());
        std::exit(1);
    }
}
int main(int argc, char **argv) {
    CHECK(argc == 3);
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    CHECK(TTF_Init() == 0);
    CHECK(curl_global_init(CURL_GLOBAL_DEFAULT) == 0);
    CHECK(browser_profile_open(nullptr, nullptr));
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_RGBA32);
    CHECK(surface);
    SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
    CHECK(renderer);
    {
        visual::Resources resources(argv[1]);
        visual::Container c(renderer, resources, argv[2]);
        c.base = c.page_url = "https://example.test/page";
        auto doc = litehtml::document::createFromString(
            R"HTML(<!doctype html><html><head><title>Before</title><style id="sheet">.new{color:#ff0000;width:111px} .hide{display:none}</style></head><body><main id="root"><p id="label">Before</p><a id="link" href="/target"><span id="inside">Click</span></a><form id="form"><input id="input" name="q"><button id="submit">Send</button></form><p id="timer">waiting</p></main><script>globalThis.order=['script'];document.addEventListener('DOMContentLoaded',()=>order.push('ready'));globalThis.original=document.getElementById('label');original.textContent='Изменено';setTimeout(()=>document.getElementById('timer').textContent='timer fired',20);Promise.resolve().then(()=>order.push('promise'));</script></body></html>)HTML",
            &c, litehtml::master_css + visual::control_styles());
        doc->render(1280);
        auto original = doc->root()->select_one("#label");
        visual::Scripts s(doc, c, resources);
        uint64_t now = visual::Scripts::clock_ms();
        s.poll(now);
        s.flush();
        s.poll(now + 1);
        s.flush();
        CHECK(s.ready());
        CHECK(original == doc->root()->select_one("#label"));
        std::string text;
        original->get_text(text);
        CHECK(text == "Изменено");
        run(s, "if(order.join(',')!=='script,promise,ready')throw Error('lifecycle "
               "order');if(original!==document.getElementById('label'))throw Error('identity');");
        run(s,
            R"JS(globalThis.trace=[];document.addEventListener('click',()=>trace.push('capture'),true);document.getElementById('link').addEventListener('click',e=>{trace.push('link');e.preventDefault();});document.addEventListener('click',()=>trace.push('bubble'));)JS");
        CHECK(!s.dispatch(doc->root()->select_one("#inside"), "click"));
        run(s, "if(trace.join(',')!=='capture,link,bubble')throw Error('event path');");
        run(s, "const "
               "created=document.createElement('div');created.id='created';created.classList.add('"
               "new');created.textContent='Привет';document.getElementById('root').appendChild("
               "created);globalThis.saved=created;");
        s.flush();
        auto made = doc->root()->select_one("#created");
        CHECK(made && int(float(made->get_placement().width)) == 111);
        CHECK(made->css().get_color().red == 255);
        run(s, "saved.classList.add('hide');");
        s.flush();
        CHECK(!made->get_render_item());
        run(s, "saved.classList.remove('hide');saved.style.width='222px';saved.dataset.state='yes';"
               "document.getElementById('input').value='Русский ввод';");
        s.flush();
        CHECK(int(float(made->get_placement().width)) == 222);
        run(s,
            "if(saved.dataset.state!=='yes'||document.querySelectorAll('#created,#label').length!=="
            "2||document.getElementById('input').value!=='Русский ввод')throw Error('DOM props');");
        run(s, "document.getElementById('sheet').textContent='.new{color:#00ff00}';saved.style."
               "width='123px';");
        s.flush();
        CHECK(made->css().get_color().green == 255);
        run(s,
            "document.getElementById('label').innerHTML='<b id=bold>Native "
            "HTML</"
            "b>';globalThis.bold=document.getElementById('bold');bold.remove();if(bold.isConnected)"
            "throw Error('detach');document.getElementById('label').appendChild(bold);");
        s.flush();
        CHECK(doc->root()->select_one("#bold"));
        c.viewport_width = 640;
        c.viewport_height = 318;
        c.invalidate_text();
        doc->refresh_native_dom();
        doc->render(640);
        CHECK(original == doc->root()->select_one("#label"));
        run(s, "if(saved!==document.getElementById('created')||original!==document.getElementById('"
               "label'))throw Error('resize reset');");
        s.poll(now + 50);
        s.flush();
        text.clear();
        doc->root()->select_one("#timer")->get_text(text);
        CHECK(text == "timer fired");
        run(s, "document.getElementById('form').addEventListener('submit',e=>{e.preventDefault();"
               "document.title='Handled form';});");
        CHECK(!s.dispatch(doc->root()->select_one("#form"), "submit"));
        s.flush();
        run(s, "if(document.title!=='Handled form')throw Error('submit');location.href='/next';");
        CHECK(s.navigation() == "https://example.test/next");
        run(s, "localStorage.setItem('visual','ok');if(localStorage.getItem('visual')!=='ok')throw "
               "Error('storage');");
        run(s, "const u=new URL('/path?x=1#h',location.href);u.searchParams.set('q','Русский + "
               "&');if(u.origin!=='https://example.test'||u.searchParams.get('q')!=='Русский + "
               "&'||!u.href.endsWith('#h'))throw Error('URL params '+u.origin+' '+u.href+' "
               "'+u.searchParams.get('q'));");
        run(s,
            "document.documentElement.lang='ru';if(document.documentElement.lang!=='ru')throw "
            "Error('lang reflection');const "
            "p=document.createElement('p');p.id='prepended';document.body.prepend(p,'start');if("
            "document.body.firstChild!==p||p.nextSibling.textContent!=='start')throw "
            "Error('prepend order');document.body.prepend(p);if(document.body.firstChild!==p)throw "
            "Error('existing-node prepend');");
        s.flush();
        CHECK(!s.eval("while(true){}", 5));
        run(s, "if(2+2!==4)throw Error('runtime did not recover');");
    }
    {
        visual::Resources resources(argv[1]);
        visual::Container c(renderer, resources, argv[2]);
        c.base = c.page_url = "https://example.test/";
        auto doc = litehtml::document::createFromString(
            "<p>blocked</p><script>globalThis.ran=true</script>", &c);
        doc->render(1280);
        visual::Scripts s(doc, c, resources, {"default-src 'none'; script-src 'self'"});
        s.poll(visual::Scripts::clock_ms());
        run(s, "if(globalThis.ran!==undefined)throw Error('CSP inline bypass');let "
               "blocked=false;try{eval('1')}catch(e){blocked=true}if(!blocked)throw Error('CSP "
               "eval bypass');let constructorBlocked=false;try{(()=>{}).constructor('return "
               "1')()}catch(e){constructorBlocked=true}if(!constructorBlocked)throw "
               "Error('constructor bypass');");
    }
    {
        visual::Resources resources(argv[1]);
        visual::Container c(renderer, resources, argv[2]);
        c.base = c.page_url = "https://example.test/";
        auto doc = litehtml::document::createFromString(
            "<button id='x' "
            "onclick='globalThis.handlerRan=true'>Tap</button><script>globalThis.scriptRan=true;</"
            "script>",
            &c);
        doc->render(1280);
        visual::Scripts s(doc, c, resources,
                          {"script-src-elem 'unsafe-inline'; script-src-attr 'none'"});
        s.poll(visual::Scripts::clock_ms());
        s.dispatch(doc->root()->select_one("#x"), "click");
        run(s, "if(!globalThis.scriptRan||globalThis.handlerRan!==undefined)throw Error('CSP "
               "handler scope bypass');");
    }
    SDL_DestroyRenderer(renderer);
    SDL_FreeSurface(surface);
    browser_profile_close();
    curl_global_cleanup();
    TTF_Quit();
    SDL_Quit();
    puts("PASS shared native DOM, identity, CSS rematch, HTML mutation, events, timers, forms, "
         "navigation, CSP and interruption");
}
