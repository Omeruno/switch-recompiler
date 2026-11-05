#include <switch.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <curl/curl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "page.h"
#include "js_runtime.h"
#include "profile.h"
#include "text.h"
#include <sys/stat.h>

#define SCREEN_W 1280
#define SCREEN_H 720
#define HISTORY_MAX 8
#define MAX_LINES 2048
#define VISIBLE_LINES 15
#define LINE_BYTES 1023

typedef struct { size_t offset, length; } VisualLine;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    TTF_Font *font;
    TTF_Font *heading;
    BrowserPage *page;
    BrowserJS *js;
    VisualLine lines[MAX_LINES];
    size_t line_count, scroll, link, history_count;
    char history[HISTORY_MAX][BROWSER_URL_MAX];
    char address[BROWSER_URL_MAX];
    char status[192];
    char last_js_notice[192];
    char last_profile_status[160];
    char startup_error[256];
    bool romfs_ready, sdl_ready, ttf_ready;
    bool loaded, network_ready, local_demo, clear_pending;
    int selection;
} App;

static const SDL_Color white = {240, 244, 251, 255};
static const SDL_Color muted = {154, 170, 191, 255};
static const SDL_Color accent = {105, 214, 232, 255};

static void rect(App *app, int x, int y, int w, int h, SDL_Color color) {
    SDL_SetRenderDrawColor(app->renderer, color.r, color.g, color.b, color.a);
    SDL_Rect bounds = {x, y, w, h};
    SDL_RenderFillRect(app->renderer, &bounds);
}

static void label(App *app, TTF_Font *font, int x, int y, const char *text,
                  SDL_Color color, int max_width) {
    if (!font || !text || !*text) return;
    int measured = 0, characters = 0;
    if (TTF_MeasureUTF8(font, text, max_width, &measured, &characters) != 0 || characters <= 0) return;
    char clipped[2048];
    size_t bytes=browser_utf8_prefix(text,(size_t)characters,sizeof(clipped)-1);
    if(!bytes)return;
    memcpy(clipped,text,bytes);
    clipped[bytes]=0;
    SDL_Surface *surface = TTF_RenderUTF8_Blended(font, clipped, color);
    if (!surface) return;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(app->renderer, surface);
    if (texture) {
        int width = surface->w < max_width ? surface->w : max_width;
        SDL_Rect source = {0, 0, width, surface->h};
        SDL_Rect dest = {x, y, width, surface->h};
        SDL_RenderCopy(app->renderer, texture, &source, &dest);
        SDL_DestroyTexture(texture);
    }
    SDL_FreeSurface(surface);
}

static bool enter_url(char *url) {
    SwkbdConfig keyboard;
    if (R_FAILED(swkbdCreate(&keyboard, 0))) return false;
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetHeaderText(&keyboard, "HTTPS address");
    swkbdConfigSetGuideText(&keyboard, "Enter a complete https:// URL");
    swkbdConfigSetStringLenMax(&keyboard, BROWSER_URL_MAX - 1);
    swkbdConfigSetInitialText(&keyboard, url);
    char typed[BROWSER_URL_MAX] = {0};
    Result result = swkbdShow(&keyboard, typed, sizeof(typed));
    swkbdClose(&keyboard);
    if (R_FAILED(result) || !browser_valid_url(typed)) return false;
    strcpy(url, typed);
    return true;
}

static void add_line(App *app, size_t start, size_t length) {
    if (app->line_count < MAX_LINES)
        app->lines[app->line_count++] = (VisualLine){start, length};
}

static void layout_text(App *app) {
    app->line_count = 0;
    const char *text = app->page->text;
    size_t pos = 0;
    while (text[pos] && app->line_count < MAX_LINES) {
        if (text[pos] == '\n') { add_line(app, pos, 0); ++pos; continue; }
        size_t end = pos;
        while (text[end] && text[end] != '\n' && end - pos < LINE_BYTES) ++end;
        // Do not hand a partial UTF-8 sequence to SDL_ttf at the 1023-byte cap.
        if (text[end] && text[end] != '\n')
            while (end > pos && ((unsigned char)text[end] & 0xc0) == 0x80) --end;
        if (end == pos) end = pos + 1;
        char sample[LINE_BYTES + 1];
        memcpy(sample, text + pos, end - pos);
        sample[end - pos] = 0;
        int extent = 0, count = 0;
        if (TTF_MeasureUTF8(app->font, sample, 1152, &extent, &count) != 0 || count <= 0)
            count = 1;
        size_t length = browser_utf8_prefix(sample,(size_t)count,end-pos);
        if(!length)length=browser_utf8_prefix(sample,1,end-pos);
        if (length < end - pos) {
            size_t space = length;
            while (space > 0 && text[pos + space - 1] != ' ') --space;
            if (space > length / 2) length = space;
        }
        while (length && text[pos + length - 1] == ' ') --length;
        if (!length) length = browser_utf8_prefix(sample,(size_t)count,end-pos);
        add_line(app, pos, length);
        pos += length;
        while (text[pos] == ' ') ++pos;
    }
    app->scroll = 0;
}

static void render(App *app) {
    SDL_SetRenderDrawColor(app->renderer, 8, 14, 25, 255);
    SDL_RenderClear(app->renderer);
    rect(app, 0, 0, SCREEN_W, 76, (SDL_Color){18, 29, 47, 255});
    label(app, app->heading, 30, 18, "Switch Browser", white, 400);
    label(app, app->font, 950, 27, "Режим чтения", accent, 290);

    if (!app->loaded) {
        label(app, app->heading, 46, 110, "Open a page on this Switch", white, 1100);
        const char *items[] = {"Keldurn — русский сайт", "Keldurn — игра",
                               "Ввести HTTPS-адрес", "JavaScript: локальная проверка"};
        for (int i = 0; i < 4; ++i) {
            int y = 172 + 82 * i;
            rect(app, 44, y, 1192, 70, i == app->selection ?
                 (SDL_Color){29, 75, 91, 255} : (SDL_Color){25, 39, 60, 255});
            label(app, app->heading, 72, y + 17, items[i], i == app->selection ? accent : white, 1100);
        }
        label(app, app->font, 46, 550, app->clear_pending ? "Удалить данные всех сайтов? A: удалить    B: отмена" : "D-pad: выбор    A: открыть    −: очистить данные    +: выход", muted, 1160);
    } else {
        label(app, app->font, 26, 86, app->page->url, accent, 1210);
        label(app, app->font, 26, 122, app->page->title, white, 1210);
        rect(app, 24, 163, 1232, 412, (SDL_Color){17, 27, 43, 255});
        if (app->page->error[0])
            label(app, app->heading, 40, 184, app->page->error, (SDL_Color){255, 153, 136, 255}, 1160);
        for (size_t i = 0; i < VISIBLE_LINES && i + app->scroll < app->line_count; ++i) {
            VisualLine line = app->lines[i + app->scroll];
            if (!line.length) continue;
            char text[LINE_BYTES + 1];
            memcpy(text, app->page->text + line.offset, line.length);
            text[line.length] = 0;
            label(app, app->font, 42, 173 + (int)i * 26, text, white, 1170);
        }
        rect(app, 24, 586, 1232, 69, (SDL_Color){25, 39, 60, 255});
        if (app->page->link_count) {
            char counter[80];
            BrowserLink *selected=&app->page->links[app->link];
            snprintf(counter,sizeof(counter),"%s %u/%u",selected->url[0]?"Ссылка":"Кнопка",
                     (unsigned)(app->link+1),(unsigned)app->page->link_count);
            label(app,app->font,40,592,counter,accent,250);
            label(app,app->font,300,592,selected->label,white,930);
            label(app,app->font,40,621,selected->url[0]?selected->url:"A: нажать кнопку",muted,1160);
        } else label(app, app->font, 40, 605, "Нет ссылок или кнопок", muted, 1160);
        label(app, app->font, 27, 666,
              "Up/Down: прокрутка   ZL/ZR: элемент   A: выбрать   B: назад   X: адрес   Y: обновить   +: выход",
              muted, 1220);
    }
    label(app, app->font, 28, 694, app->status, muted, 1200);
    SDL_RenderPresent(app->renderer);
}

static void load_page(App *app, const char *url) {
    snprintf(app->status, sizeof(app->status), "Загрузка: %.130s", url);
    render(app);
    browser_js_destroy(app->js); app->js = NULL;
    int ok = browser_fetch(app->page, url);
    if (ok) app->js = browser_js_start(app->page);
    if (ok && app->page->js_notice[0])
        snprintf(app->status, sizeof(app->status), "JS %u/%u | download errors %u: %.100s",
                 (unsigned)app->page->js_ran, (unsigned)app->page->script_count,
                 (unsigned)app->page->script_fetch_failed,
                 app->page->js_notice);
    else if (ok)
        snprintf(app->status, sizeof(app->status), "HTTPS OK | JS %u/%u | download errors %u",
                 (unsigned)app->page->js_ran, (unsigned)app->page->script_count,
                 (unsigned)app->page->script_fetch_failed);
    else snprintf(app->status, sizeof(app->status), "%s", app->page->error);
    app->link = 0;
    layout_text(app);
    app->loaded = true;
    app->local_demo = false;
}

static void load_demo(App *app) {
    browser_js_destroy(app->js); app->js = NULL;
    static const char html[] = "<html lang='ru'><head><title>Живая страница</title></head><body>"
        "<h1>Автономная проверка браузера</h1><p id='clock'>Таймер запускается…</p>"
        "<button id='counter'>Нажатий: 0</button><p id='ready'>Ожидание DOM</p>"
        "<a href='https://keldurn.com/ru/'>Открыть Keldurn</a>"
        "<script>let seconds=0,clicks=0;"
        "setInterval(()=>document.getElementById('clock').textContent='Прошло секунд: '+(++seconds),1000);"
        "document.getElementById('counter').addEventListener('click',function(){this.textContent='Нажатий: '+(++clicks)});"
        "document.addEventListener('DOMContentLoaded',()=>document.getElementById('ready').textContent='DOM готов. Выберите кнопку ZL/ZR и нажмите A.');"
        "setTimeout(()=>{const p=document.createElement('p');p.textContent='Отложенное обновление работает';document.body.append(p)},2000);"
        "</script></body></html>";
    memset(app->page, 0, sizeof(*app->page));
    strcpy(app->page->url, "https://browser.invalid/offline-demo");
    browser_parse_html(app->page, html, sizeof(html) - 1);
    app->js = browser_js_start(app->page);
    snprintf(app->status, sizeof(app->status), "JS %u/%u: %.125s",
             (unsigned)app->page->js_ran, (unsigned)app->page->script_count,
             app->page->js_notice);
    app->link = 0;
    layout_text(app);
    app->loaded = true;
    app->local_demo = true;
}

static bool setup_graphics(App *app) {
    Result romfs_result = romfsInit();
    if (R_FAILED(romfs_result)) {
        snprintf(app->startup_error, sizeof(app->startup_error), "romfsInit: 0x%08X", romfs_result);
        return false;
    }
    app->romfs_ready = true;
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        snprintf(app->startup_error, sizeof(app->startup_error), "SDL_Init: %.220s", SDL_GetError());
        return false;
    }
    app->sdl_ready = true;
    if (TTF_Init() != 0) {
        snprintf(app->startup_error, sizeof(app->startup_error), "TTF_Init: %.220s", TTF_GetError());
        return false;
    }
    app->ttf_ready = true;
    app->window = SDL_CreateWindow("Switch Browser", SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED, SCREEN_W, SCREEN_H,
                                   SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!app->window) {
        snprintf(app->startup_error, sizeof(app->startup_error), "SDL_CreateWindow: %.200s", SDL_GetError());
        return false;
    }
    app->renderer = SDL_CreateRenderer(app->window, -1,
                                       SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!app->renderer) app->renderer = SDL_CreateRenderer(app->window, -1, SDL_RENDERER_SOFTWARE);
    if (!app->renderer) {
        snprintf(app->startup_error, sizeof(app->startup_error), "SDL_CreateRenderer: %.190s", SDL_GetError());
        return false;
    }
    SDL_RenderSetLogicalSize(app->renderer, SCREEN_W, SCREEN_H);
    app->font = TTF_OpenFont("romfs:/DejaVuSans.ttf", 21);
    app->heading = TTF_OpenFont("romfs:/DejaVuSans.ttf", 29);
    if (!app->font || !app->heading) {
        snprintf(app->startup_error, sizeof(app->startup_error), "TTF_OpenFont: %.210s", TTF_GetError());
        return false;
    }
    return true;
}

static void run_console_fallback(BrowserPage *page, const char *reason) {
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    Result socket_result = socketInitializeDefault();
    bool curl_ready = R_SUCCEEDED(socket_result) && curl_global_init(CURL_GLOBAL_DEFAULT) == 0;
    Result ca_romfs = romfsInit();
    mkdir("sdmc:/switch",0700);
    bool network_ready = curl_ready && browser_profile_open("sdmc:/switch/switch-browser",R_SUCCEEDED(ca_romfs) ? "romfs:/cacert.pem" : NULL);
    char status[192] = "Press A to load Keldurn, X for HTTPS address.";
    char address[BROWSER_URL_MAX] = "https://";
    bool have_page = false;
    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 pressed = padGetButtonsDown(&pad);
        if (pressed & HidNpadButton_Plus) break;
        if ((pressed & HidNpadButton_A) && network_ready) {
            have_page = true;
            bool ok = browser_fetch(page, "https://keldurn.com/ru/");
            if (ok) browser_js_apply(page);
            snprintf(status, sizeof(status), "%s", ok ? "HTTPS OK" : page->error);
        }
        if ((pressed & HidNpadButton_X) && network_ready) {
            if (enter_url(address)) {
                have_page = true;
                bool ok = browser_fetch(page, address);
                if (ok) browser_js_apply(page);
                snprintf(status, sizeof(status), "%s", ok ? "HTTPS OK" : page->error);
            }
            padUpdate(&pad);
        }
        consoleClear();
        printf("Switch Browser: graphics unavailable\n\nCause: %.220s\n\n", reason);
        printf("A: Keldurn  X: address  +: quit\nNetwork: %s\n%s\n\n",
               network_ready ? "ready" : "failed", status);
        printf("CA: %s\nProfile: %.150s\n",R_SUCCEEDED(ca_romfs)?"bundled":"system trust (diagnostic fallback)",browser_profile_status());
        if (have_page) {
            printf("HTTP %ld\n%.72s\n\n", page->http_status, page->url);
            printf("%.900s\n", page->text);
        }
        consoleUpdate(NULL);
    }
    browser_profile_close();
    if (curl_ready) curl_global_cleanup();
    if (R_SUCCEEDED(ca_romfs)) romfsExit();
    if (R_SUCCEEDED(socket_result)) socketExit();
    consoleExit(NULL);
}

int main(void) {
    App *app = calloc(1, sizeof(*app));
    if (!app) return 1;
    app->page = calloc(1, sizeof(*app->page));
    if (!app->page) { free(app); return 1; }
    if (!setup_graphics(app)) {
        char reason[sizeof(app->startup_error)];
        snprintf(reason, sizeof(reason), "%s", app->startup_error[0] ? app->startup_error : "Unknown graphics error");
        if (app->heading) TTF_CloseFont(app->heading);
        if (app->font) TTF_CloseFont(app->font);
        if (app->renderer) SDL_DestroyRenderer(app->renderer);
        if (app->window) SDL_DestroyWindow(app->window);
        if (app->ttf_ready) TTF_Quit();
        if (app->sdl_ready) SDL_Quit();
        if (app->romfs_ready) romfsExit();
        run_console_fallback(app->page, reason);
        free(app->page); free(app); return 2;
    }
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    Result socket_result = socketInitializeDefault();
    int curl_result = R_SUCCEEDED(socket_result) ? curl_global_init(CURL_GLOBAL_DEFAULT) : -1;
    mkdir("sdmc:/switch",0700);
    app->network_ready = R_SUCCEEDED(socket_result) && curl_result == 0 &&
        browser_profile_open("sdmc:/switch/switch-browser","romfs:/cacert.pem");
    strcpy(app->address, "https://");
    snprintf(app->status, sizeof(app->status), "%s", app->network_ready ?
             browser_profile_status() : "Ошибка сети или CA-набора. Проверьте NRO и Wi-Fi.");
    bool dirty = true;

    while (appletMainLoop()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) if (event.type == SDL_QUIT) goto done;
        padUpdate(&pad);
        u64 pressed = padGetButtonsDown(&pad);
        if (pressed & HidNpadButton_Plus) break;
        if (!app->loaded) {
            if (pressed & HidNpadButton_Up) { app->selection = (app->selection + 3) % 4; dirty = true; }
            if (pressed & HidNpadButton_Down) { app->selection = (app->selection + 1) % 4; dirty = true; }
            if (pressed & HidNpadButton_Minus) { app->clear_pending = !app->clear_pending; dirty = true; }
            if (app->clear_pending) {
                if (pressed & HidNpadButton_B) { app->clear_pending = false; dirty = true; }
                if (pressed & HidNpadButton_A) {
                    int cleared = browser_profile_clear();
                    snprintf(app->status,sizeof(app->status),"%s",cleared ? "Данные сайтов удалены." : "Не удалось удалить данные профиля.");
                    app->clear_pending = false; dirty = true;
                }
            } else if (pressed & HidNpadButton_A) {
                const char *next = app->selection == 0 ? "https://keldurn.com/ru/" :
                                   app->selection == 1 ? "https://keldurn.com/play" : app->address;
                bool accepted = app->selection != 2 || enter_url(app->address);
                if (app->selection == 3) load_demo(app);
                else if (accepted && app->network_ready) load_page(app, next);
                else if (!accepted) strcpy(app->status, "Адрес отменён или неверен.");
                padUpdate(&pad);
                dirty = true;
            }
        } else {
            if (pressed & HidNpadButton_Up && app->scroll) { --app->scroll; dirty = true; }
            if (pressed & HidNpadButton_Down && app->scroll + VISIBLE_LINES < app->line_count) {
                ++app->scroll; dirty = true;
            }
            if (app->page->link_count) {
                if (pressed & HidNpadButton_ZL) {
                    app->link = (app->link + app->page->link_count - 1) % app->page->link_count; dirty = true;
                }
                if (pressed & HidNpadButton_ZR) {
                    app->link = (app->link + 1) % app->page->link_count; dirty = true;
                }
            }
            if (pressed & HidNpadButton_B) {
                if (app->history_count) {
                    char previous[BROWSER_URL_MAX];
                    strcpy(previous, app->history[--app->history_count]);
                    load_page(app, previous);
                } else { app->loaded = false; browser_js_destroy(app->js); app->js = NULL; }
                dirty = true;
            } else if (pressed & HidNpadButton_Y) {
                if (app->local_demo) load_demo(app);
                else {
                    char current[BROWSER_URL_MAX];
                    strcpy(current, app->page->url);
                    load_page(app, current);
                }
                dirty = true;
            } else if (pressed & HidNpadButton_X) {
                strcpy(app->address, app->local_demo ? "https://" : app->page->url);
                if (enter_url(app->address)) {
                    if (!app->local_demo && app->history_count < HISTORY_MAX)
                        strcpy(app->history[app->history_count++], app->page->url);
                    load_page(app, app->address);
                }
                padUpdate(&pad); dirty = true;
            } else if (pressed & HidNpadButton_A && app->page->link_count) {
                char next[BROWSER_URL_MAX];
                strcpy(next, app->page->links[app->link].url);
                bool follow = browser_js_click(app->js, app->link, next);
                if (follow && browser_valid_url(next)) {
                    if (!app->local_demo && app->history_count < HISTORY_MAX)
                        strcpy(app->history[app->history_count++], app->page->url);
                    load_page(app, next);
                } else {
                    layout_text(app);
                    if (app->link >= app->page->link_count) app->link = 0;
                }
                dirty = true;
            }
        }
        if (app->loaded && browser_js_tick(app->js, browser_js_clock_ms())) {
            size_t scroll = app->scroll;
            layout_text(app);
            app->scroll = scroll < app->line_count ? scroll : 0;
            if (app->link >= app->page->link_count) app->link = 0;
            dirty = true;
        }
        if (app->loaded && strcmp(app->last_js_notice, app->page->js_notice)) {
            snprintf(app->last_js_notice, sizeof(app->last_js_notice), "%s", app->page->js_notice);
            if (app->page->js_notice[0]) snprintf(app->status, sizeof(app->status), "JS: %.180s", app->page->js_notice);
            dirty = true;
        }
        if (app->network_ready && strcmp(app->last_profile_status, browser_profile_status())) {
            snprintf(app->last_profile_status, sizeof(app->last_profile_status), "%s", browser_profile_status());
            snprintf(app->status, sizeof(app->status), "%s", browser_profile_status());
            dirty = true;
        }
        if (dirty) { render(app); dirty = false; }
        SDL_Delay(16);
    }
done:
    browser_js_destroy(app->js);
    browser_profile_close();
    if (curl_result == 0) curl_global_cleanup();
    if (R_SUCCEEDED(socket_result)) socketExit();
    TTF_CloseFont(app->heading);
    TTF_CloseFont(app->font);
    SDL_DestroyRenderer(app->renderer);
    SDL_DestroyWindow(app->window);
    TTF_Quit(); SDL_Quit(); romfsExit();
    free(app->page);
    free(app);
    return 0;
}
