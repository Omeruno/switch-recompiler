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
    eval("setTimeout(()=>localStorage.setItem('old_callback','timer'),16);fetch('/"
         "loop-image2.png?cancel=1').then(()=>localStorage.setItem('old_callback','fetch'));");
    app.resources.pump(); // Start the delayed transfer, then cancel the old realm/page.
    app.navigate(base + "/loop-clean", false);
    pump_until([&] { return app.scripts && app.scripts->ready(); });
    for (int i = 0; i < 25; ++i) {
        app.pump();
        SDL_Delay(10);
    }
    check(!browser_storage_get(base.c_str(), 0, "old_callback"),
          "old realm callback survived navigation");
    puts("PASS delayed async/defer/classic scripts, timer during load, DCL/load order, JSON "
         "without layout, image-only relayout, late CSS and coalesced DOM changes");
}
