// Included only by the host executable; tests the actual Browser pump and counters.
void browser_network_checks(Browser &app, const std::string &base) {
    auto check = [](bool ok, const char *message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    auto pump_until = [&](const std::function<bool()> &done) {
        auto deadline = SDL_GetTicks() + 6000;
        do {
            app.pump();
            SDL_Delay(2);
        } while (!done() && SDL_GetTicks() < deadline);
        check(done(), "network browser test timed out");
    };
    auto eval = [&](const char *code) {
        if (!app.scripts)
            throw std::runtime_error("missing JS");
        if (!app.scripts->eval(code))
            throw std::runtime_error(app.scripts->error());
    };
    app.navigate(base + "/loop-page", false);
    pump_until([&] { return app.scripts != nullptr; });
    auto doc = app.document;
    auto field = std::dynamic_pointer_cast<visual::Control>(doc->root()->select_one("#field"));
    check(bool(field), "missing field");
    field->value("Сохранённый ввод");
    unsigned styles = app.container.style_passes;
    pump_until([&] { return app.scripts->ready() && !app.resources.pending(); });
    check(app.document == doc && field->value() == "Сохранённый ввод",
          "resource replaced native DOM/input");
    check(app.container.style_passes == styles, "script/image completion rebuilt CSS");
    eval("if(!trace.includes('async')||trace.indexOf('async')>trace.indexOf('ordered')||trace."
         "indexOf('during-wait')>trace.indexOf('ordered')||trace.indexOf('defer1')>trace.indexOf('"
         "defer2')||trace.indexOf('defer2')>trace.indexOf('dom')||trace.indexOf('dom')>trace."
         "indexOf('load'))throw Error('script schedule "
         "'+trace);if(states.join(',')!=='interactive,interactive')throw Error('defer states "
         "'+states);if(document.readyState!=='complete'||document.currentScript!==null)throw "
         "Error('lifecycle');");
    eval("if(trace.filter(x=>x==='window-capture').length!==1)throw Error('resource load reached "
         "window capture');");
    unsigned layouts = app.container.layout_passes;
    eval("fetch('/loop-data').then(r=>r.json()).then(v=>globalThis.jsonDone=v.ok);");
    pump_until([&] { return !app.resources.pending(); });
    for (int i = 0; i < 10; ++i)
        app.pump();
    eval("if(!globalThis.jsonDone)throw Error('fetch not delivered');");
    check(app.container.layout_passes == layouts && app.container.style_passes == styles,
          "JSON rebuilt document");
    eval("const "
         "im=document.createElement('img');im.id='dynamic-image';im.src='/"
         "loop-image2.png';document.body.appendChild(im);const "
         "below=document.createElement('div');below.id='below';below.textContent='Below "
         "image';document.body.appendChild(below);");
    app.pump();
    styles = app.container.style_passes;
    layouts = app.container.layout_passes;
    auto below = doc->root()->select_one("#below");
    auto before = below->get_placement();
    pump_until([&] { return !app.resources.pending(); });
    check(app.container.style_passes == styles && app.container.layout_passes > layouts,
          "image did not use layout-only path");
    check(float(below->get_placement().y) > float(before.y),
          "intrinsic image size did not move sibling");
    eval("const "
         "sheet=document.createElement('link');sheet.rel='stylesheet';sheet.href='/"
         "loop-late.css';document.head.appendChild(sheet);");
    app.pump();
    styles = app.container.style_passes;
    pump_until([&] { return !app.resources.pending(); });
    check(app.container.style_passes > styles, "late CSS did not restyle");
    eval("if(document.getElementById('marker').getBoundingClientRect().width!==231)throw "
         "Error('late CSS "
         "geometry');setTimeout(()=>document.getElementById('marker').textContent='one',0);"
         "setTimeout(()=>Promise.resolve().then(()=>document.getElementById('marker').textContent='"
         "two'),0);");
    styles = app.container.style_passes;
    layouts = app.container.layout_passes;
    app.pump();
    check(app.container.style_passes == styles + 1 && app.container.layout_passes == layouts + 1,
          "task mutations not coalesced");
    check(app.document == doc && field->value() == "Сохранённый ввод",
          "late updates replaced DOM/input");
    eval("const "
         "tall=document.createElement('div');tall.style.height='1800px';tall.style.width='100px';"
         "document.body.appendChild(tall);globalThis.browserResize=0;new "
         "ResizeObserver(()=>browserResize++).observe(tall);");
    for (int i = 0; i < 12; i++) {
        app.pump();
        SDL_Delay(2);
    }
    eval("if(!browserResize)throw Error('browser resize integration');scrollTo(0,80);");
    app.pump();
    check(app.view.y == 80 && app.container.scroll_y == 80,
          "script scroll did not reach native view");
    eval("scrollBy(0,100);");
    app.pump();
    check(app.view.y == 180, "relative script scroll did not reach view");
    eval("scrollTo(0,0);new "
         "ResizeObserver(()=>localStorage.setItem('layout_leak','resize')).observe(tall);new "
         "IntersectionObserver(()=>localStorage.setItem('layout_leak','intersection')).observe("
         "tall);");
    eval("setTimeout(()=>localStorage.setItem('old_callback','timer'),16);fetch('/"
         "loop-image2.png?cancel=1').then(()=>localStorage.setItem('old_callback','fetch'));");
    eval("const oldXHR=new "
         "XMLHttpRequest();oldXHR.open('GET','/"
         "api-slow');oldXHR.onload=()=>localStorage.setItem('xhr_leak','1');oldXHR.send();");
    app.resources.pump(); // Start the delayed transfer, then cancel the old realm/page.
    app.navigate(base + "/loop-clean", false);
    pump_until([&] { return app.scripts && app.scripts->ready(); });
    for (int i = 0; i < 25; ++i) {
        app.pump();
        SDL_Delay(10);
    }
    check(!browser_storage_get(base.c_str(), 0, "old_callback"),
          "old realm callback survived navigation");
    check(!browser_storage_get(base.c_str(), 0, "layout_leak"),
          "layout observer survived navigation");
    check(!browser_storage_get(base.c_str(), 0, "xhr_leak"), "XHR callback survived navigation");
    puts("PASS delayed async/defer/classic scripts, timer during load, DCL/load order, JSON "
         "without layout, image-only relayout, late CSS and coalesced DOM changes");
}

void browser_dynamic_checks(Browser &app, const std::string &base) {
    auto check = [](bool ok, const char *message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    auto until = [&](const std::function<bool()> &done) {
        auto end = SDL_GetTicks() + 6500;
        do {
            app.pump();
            SDL_Delay(2);
        } while (!done() && SDL_GetTicks() < end);
        check(done(), "dynamic network timeout");
    };
    auto eval = [&](const char *code) {
        check(bool(app.scripts), "missing dynamic JS");
        if (!app.scripts->eval(code))
            throw std::runtime_error(app.scripts->error());
    };
    app.navigate(base + "/dyn-page", false);
    until([&] { return app.scripts && app.scripts->ready() && !app.resources.pending(); });
    auto doc = app.document;
    eval(R"JS(globalThis.dynTrace=[];globalThis.dynEvents=[];globalThis.observerChanges=0;
      const mo=new MutationObserver(records=>observerChanges+=records.length);mo.observe(document.getElementById('net-result'),{childList:true});
      function script(id,src,ordered){const s=document.createElement('script');s.id=id;if(ordered)s.async=false;s.src=src;s.onload=()=>{if(document.currentScript!==null)throw Error('load currentScript');dynEvents.push('load:'+id);};s.onerror=()=>dynEvents.push('error:'+id);document.body.appendChild(s);return s;}
      globalThis.slow=script('slow','/dyn-slow.js',true);script('fast','/dyn-fast.js',true);script('async','/dyn-async.js',false);slow.remove();setTimeout(()=>dynTrace.push('timer'),0);
    )JS");
    until([&] { return !app.resources.pending(); });
    for (int i = 0; i < 20; ++i)
        app.pump();
    eval("if(dynTrace.indexOf('async')>dynTrace.indexOf('slow')||dynTrace.indexOf('slow')>dynTrace."
         "indexOf('fast')||dynTrace.indexOf('timer')>dynTrace.indexOf('slow')||observerChanges!==2)"
         "throw Error('dynamic ordering '+dynTrace+' / "
         "'+observerChanges);if(dynEvents.length!==3||windowLoads!==1||domReady!==1)throw "
         "Error('dynamic lifecycle');document.body.appendChild(slow);slow.src='/dyn-fast.js';");
    for (int i = 0; i < 5; ++i)
        app.pump();
    eval("if(dynTrace.filter(x=>x==='slow').length!==1||dynTrace.filter(x=>x==='fast').length!==1)"
         "throw Error('dynamic "
         "restarted');script('repeat','/dyn-fast.js',true);script('missing','/"
         "dyn-404.js',false);const "
         "late=document.createElement('script');late.id='late';late.onload=()=>dynEvents.push('"
         "load:late');document.body.appendChild(late);late.src='/dyn-async.js?late=1';");
    until([&] { return !app.resources.pending(); });
    for (int i = 0; i < 20; ++i)
        app.pump();
    eval("globalThis.largeExecuted=false;fetch('/"
         "dyn-large.js').then(r=>r.text()).then(()=>globalThis.largeFetched=true);");
    until([&] { return !app.resources.pending(); });
    for (int i = 0; i < 10; ++i)
        app.pump();
    eval("if(!largeFetched)throw Error('large fetch fixture');");
    // Fetch is independent of the script/image resource cache. Prime that cache
    // explicitly to keep exercising the script byte-limit check on cached data.
    check(app.resources.request(base + "/dyn-large.js", 512u * 1024u) != nullptr,
          "large script cache fixture");
    until([&] { return !app.resources.pending(); });
    eval("script('large','/dyn-large.js',false);");
    for (int i = 0; i < 10; ++i)
        app.pump();
    eval("if(largeExecuted||!dynEvents.includes('error:large'))throw Error('cached script byte "
         "limit bypass');");
    eval("if(dynTrace.filter(x=>x==='fast').length!==2||!dynEvents.includes('error:missing')||!"
         "dynEvents.includes('load:late'))throw Error('cache/error/late-src');const "
         "sri=document.createElement('script');sri.integrity='sha256-not-supported';sri.src='/"
         "dyn-fast.js?integrity=1';sri.onerror=()=>dynEvents.push('error:sri');document.body."
         "appendChild(sri);");
    for (int i = 0; i < 5; ++i)
        app.pump();
    eval(
        R"JS(if(!dynEvents.includes('error:sri'))throw Error('SRI ignored');globalThis.badInline=false;
       const meta=document.createElement('meta');meta.setAttribute('http-equiv','Content-Security-Policy');meta.setAttribute('content',"script-src 'none'");document.head.appendChild(meta);
       const denied=document.createElement('script');denied.text='badInline=true';document.body.appendChild(denied);meta.remove();script('denied','/dyn-fast.js?blocked=1',false);
    )JS");
    for (int i = 0; i < 10; ++i)
        app.pump();
    eval("if(badInline||!dynEvents.includes('error:denied')||windowLoads!==1||domReady!==1)throw "
         "Error('CSP bypass/lifecycle');");
    check(app.document == doc, "dynamic script replaced native tree");
    app.navigate(base + "/dyn-page", false);
    until([&] { return app.scripts && app.scripts->ready() && !app.resources.pending(); });
    eval("const "
         "delayed=document.createElement('script');delayed.src='/"
         "dyn-leak.js';document.body.appendChild(delayed);const obs=new "
         "MutationObserver(()=>localStorage.setItem('observer_leak','1'));obs.observe(document."
         "body,{attributes:true});setTimeout(()=>document.body.setAttribute('data-leak','1'),16);");
    app.resources.pump();
    app.navigate(base + "/loop-clean", false);
    until([&] { return app.scripts && app.scripts->ready() && !app.resources.pending(); });
    for (int i = 0; i < 30; ++i) {
        app.pump();
        SDL_Delay(10);
    }
    check(!browser_storage_get(base.c_str(), 0, "dynamic_leak") &&
              !browser_storage_get(base.c_str(), 0, "observer_leak"),
          "old dynamic realm survived navigation");
    puts("PASS dynamic external ordering, default async, detached execution, cache/once/late src, "
         "events, observer integration, CSP/SRI rejection and navigation cancellation");
}
