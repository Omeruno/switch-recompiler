// Exercise the real Browser, built-in Wasm game and physical input adapter boundary.
void browser_game_checks(Browser &app, const char *screenshot) {
    auto check = [](bool ok, const char *message) {
        if (!ok)
            throw std::runtime_error(message);
    };
    auto eval = [&](const std::string &source) {
        check(bool(app.scripts), "missing JS realm");
        if (!app.scripts->eval(source, 1000))
            throw std::runtime_error(app.scripts->error());
    };
    auto drive = [&](unsigned count) {
        for (unsigned i = 0; i < count; i++) {
            app.pump();
            SDL_Delay(18);
        }
    };
    drive(20);
    check(app.scripts && app.scripts->ready(), "demo did not load");
    eval("document.getElementById('game-start').click();");
    drive(18);
    eval("if(!document.getElementById('game-status').textContent.includes('Собрано'))throw "
         "Error(document.getElementById('game-status').textContent);");
    auto canvas = app.document->root()->select_one("#game-screen");
    check(bool(canvas), "missing canvas");
    check(app.enter_game_input(), "focused canvas did not capture game controls");
    eval("globalThis.inputTrace=[];addEventListener('gamepadconnected',e=>inputTrace.push(e."
         "gamepad instanceof "
         "Gamepad));addEventListener('gamepaddisconnected',e=>inputTrace.push(!e.gamepad.connected)"
         ");const "
         "field=document.getElementById('game-screen');field.addEventListener('keydown',e=>"
         "inputTrace.push(e instanceof "
         "KeyboardEvent&&e.code==='KeyD'));field.addEventListener('keyup',e=>inputTrace.push(e."
         "code==='KeyD'));");
    app.container.game_input.connected = true;
    app.container.game_input.axes[0] = .8f;
    app.container.game_input.buttons[0] = true;
    drive(2);
    eval("const pad=navigator.getGamepads()[0];if(!(pad instanceof "
         "Gamepad)||!pad.buttons[0].pressed||pad.axes[0]<.7)throw Error('gamepad snapshot');");
    SDL_KeyboardEvent key{};
    key.type = SDL_KEYDOWN;
    key.keysym.sym = SDLK_d;
    key.keysym.scancode = SDL_SCANCODE_D;
    app.game_key(key);
    unsigned styles = app.container.style_passes, layouts = app.container.layout_passes;
    drive(5);
    check(app.container.style_passes == styles && app.container.layout_passes == layouts,
          "pixel-only frames rebuilt CSS/layout");
    app.leave_game_input();
    drive(2);
    eval("if(inputTrace.length!==4||!inputTrace.every(Boolean)||navigator.getGamepads()[0]!==null)"
         "throw Error('input lifecycle '+inputTrace);");
    app.view.y = 0;
    app.pump();
    // Bring the game into view and perform native touch hit testing, capture and release.
    auto box = canvas->get_placement();
    app.view.y = std::max(0.f, float(box.y) - 12);
    app.pump();
    visual::Point client{
        (float(box.x) + 97.f * float(box.width) / 128.f - app.view.x) * app.view.scale(),
        (float(box.y) + 22.f * float(box.height) / 80.f - app.view.y) * app.view.scale()};
    app.touch_down(22, client, SDL_GetTicks());
    check(!app.canvas_touches.empty() && app.canvas_touches.begin()->second.exclusive,
          "canvas pointer capture did not suppress browser gestures");
    drive(2);
    app.touch_move(22, {client.x + 40, client.y + 20}, SDL_GetTicks() + 20);
    drive(2);
    app.touch_up(22, {client.x + 40, client.y + 20}, SDL_GetTicks() + 30);
    check(app.canvas_touches.empty(), "pointer capture leaked");
    eval("if(!document.getElementById('game-status').textContent.startsWith('Собрано огоньков: 1 "
         "'))throw Error('cross-module score callback: "
         "'+document.getElementById('game-status').textContent);");
    app.paint();
    check(app.container.painted_canvases > 0 && app.container.canvases->frames > 0,
          "Wasm frame did not reach SDL texture");
    auto *shot = SDL_CreateRGBSurfaceWithFormat(0, 1280, 720, 32, SDL_PIXELFORMAT_RGBA32);
    check(shot && SDL_RenderReadPixels(app.renderer, nullptr, shot->format->format, shot->pixels,
                                       shot->pitch) == 0,
          "SDL readback");
    check(SDL_SaveBMP(shot, screenshot) == 0, "game screenshot");
    SDL_FreeSurface(shot);
    eval("document.getElementById('game-pause').click();");
    drive(3);
    auto calls = app.scripts->game_stats().calls;
    drive(3);
    check(app.scripts->game_stats().calls == calls, "paused game still steps Wasm");
    eval("document.getElementById('game-pause').click();");
    drive(3);
    check(app.scripts->game_stats().calls > calls, "resume did not step Wasm");
    app.navigate("demo:/index.html", false);
    drive(12);
    check(!app.game_mode && app.canvas_touches.empty() && !app.container.game_input.connected,
          "navigation retained game controls");
    check(app.scripts->game_stats().instances == 0, "navigation retained old Wasm instance");
    puts("PASS owned Wasm game, canvas SDL frames, gamepad, keyboard, touch capture, pause/resume "
         "and navigation cleanup");
}
