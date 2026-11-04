#include <switch.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include <curl/curl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "page.h"

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
    VisualLine lines[MAX_LINES];
    size_t line_count, scroll, link, history_count;
    char history[HISTORY_MAX][BROWSER_URL_MAX];
    char address[BROWSER_URL_MAX];
    char status[192];
    bool loaded, network_ready;
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
    int measured = 0, bytes = 0;
    if (TTF_MeasureUTF8(font, text, max_width, &measured, &bytes) != 0 || bytes <= 0) return;
    if (bytes > 2047) bytes = 2047;
    char clipped[2048];
    memcpy(clipped, text, (size_t)bytes);
    clipped[bytes] = 0;
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
            count = (unsigned char)sample[0] < 0x80 ? 1 :
                    ((unsigned char)sample[0] & 0xe0) == 0xc0 ? 2 :
                    ((unsigned char)sample[0] & 0xf0) == 0xe0 ? 3 : 4;
        size_t length = (size_t)count;
        if (length > end - pos) length = end - pos;
        if (length < end - pos) {
            size_t space = length;
            while (space > 0 && text[pos + space - 1] != ' ') --space;
            if (space > length / 2) length = space;
        }
        while (length && text[pos + length - 1] == ' ') --length;
        if (!length) length = (size_t)count;
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
    label(app, app->font, 950, 27, "Native HTTPS", accent, 290);

    if (!app->loaded) {
        label(app, app->heading, 46, 110, "Open a page on this Switch", white, 1100);
        const char *items[] = {"Keldurn — русский сайт", "Keldurn — игра", "Ввести HTTPS-адрес"};
        for (int i = 0; i < 3; ++i) {
            int y = 180 + 104 * i;
            rect(app, 44, y, 1192, 82, i == app->selection ?
                 (SDL_Color){29, 75, 91, 255} : (SDL_Color){25, 39, 60, 255});
            label(app, app->heading, 72, y + 23, items[i], i == app->selection ? accent : white, 1100);
        }
        label(app, app->font, 46, 570, "D-pad: выбор    A: открыть    +: выход", muted, 1160);
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
            snprintf(counter, sizeof(counter), "Ссылка %u/%u",
                     (unsigned)(app->link + 1), (unsigned)app->page->link_count);
            label(app, app->font, 40, 592, counter, accent, 1160);
            label(app, app->font, 40, 621, app->page->links[app->link].url, white, 1160);
        } else label(app, app->font, 40, 605, "Ссылок нет", muted, 1160);
        label(app, app->font, 27, 666,
              "Up/Down: прокрутка   ZL/ZR: ссылка   A: открыть   B: назад   X: адрес   Y: обновить   +: выход",
              muted, 1220);
    }
    label(app, app->font, 28, 694, app->status, muted, 1200);
    SDL_RenderPresent(app->renderer);
}

static void load_page(App *app, const char *url) {
    snprintf(app->status, sizeof(app->status), "Загрузка: %.130s", url);
    render(app);
    int ok = browser_fetch(app->page, url);
    snprintf(app->status, sizeof(app->status), "%s", ok ?
             "Страница получена по HTTPS." : app->page->error);
    app->link = 0;
    layout_text(app);
    app->loaded = true;
}

static bool setup_graphics(App *app) {
    if (R_FAILED(romfsInit())) return false;
    if (SDL_Init(SDL_INIT_VIDEO) != 0 || TTF_Init() != 0) return false;
    app->window = SDL_CreateWindow("Switch Browser", SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED, SCREEN_W, SCREEN_H,
                                   SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!app->window) return false;
    app->renderer = SDL_CreateRenderer(app->window, -1,
                                       SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!app->renderer) app->renderer = SDL_CreateRenderer(app->window, -1, SDL_RENDERER_SOFTWARE);
    if (!app->renderer) return false;
    SDL_RenderSetLogicalSize(app->renderer, SCREEN_W, SCREEN_H);
    app->font = TTF_OpenFont("romfs:/DejaVuSans.ttf", 21);
    app->heading = TTF_OpenFont("romfs:/DejaVuSans.ttf", 29);
    return app->font && app->heading;
}

int main(void) {
    App *app = calloc(1, sizeof(*app));
    if (!app) return 1;
    app->page = calloc(1, sizeof(*app->page));
    if (!app->page) { free(app); return 1; }
    if (!setup_graphics(app)) {
        if (app->heading) TTF_CloseFont(app->heading);
        if (app->font) TTF_CloseFont(app->font);
        if (app->renderer) SDL_DestroyRenderer(app->renderer);
        if (app->window) SDL_DestroyWindow(app->window);
        TTF_Quit(); SDL_Quit(); romfsExit();
        free(app->page); free(app); return 2;
    }
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    Result socket_result = socketInitializeDefault();
    int curl_result = R_SUCCEEDED(socket_result) ? curl_global_init(CURL_GLOBAL_DEFAULT) : -1;
    app->network_ready = R_SUCCEEDED(socket_result) && curl_result == 0;
    strcpy(app->address, "https://");
    snprintf(app->status, sizeof(app->status), "%s", app->network_ready ?
             "Прямой HTTPS готов. Системный WebApplet не используется." : "Ошибка инициализации сети.");
    bool dirty = true;

    while (appletMainLoop()) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) if (event.type == SDL_QUIT) goto done;
        padUpdate(&pad);
        u64 pressed = padGetButtonsDown(&pad);
        if (pressed & HidNpadButton_Plus) break;
        if (!app->loaded) {
            if (pressed & HidNpadButton_Up) { app->selection = (app->selection + 2) % 3; dirty = true; }
            if (pressed & HidNpadButton_Down) { app->selection = (app->selection + 1) % 3; dirty = true; }
            if (pressed & HidNpadButton_A) {
                const char *next = app->selection == 0 ? "https://keldurn.com/ru/" :
                                   app->selection == 1 ? "https://play.keldurn.com/" : app->address;
                bool accepted = app->selection != 2 || enter_url(app->address);
                if (accepted && app->network_ready) load_page(app, next);
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
                } else app->loaded = false;
                dirty = true;
            } else if (pressed & HidNpadButton_Y) {
                char current[BROWSER_URL_MAX];
                strcpy(current, app->page->url);
                load_page(app, current); dirty = true;
            } else if (pressed & HidNpadButton_X) {
                strcpy(app->address, app->page->url);
                if (enter_url(app->address)) {
                    if (app->history_count < HISTORY_MAX)
                        strcpy(app->history[app->history_count++], app->page->url);
                    load_page(app, app->address);
                }
                padUpdate(&pad); dirty = true;
            } else if (pressed & HidNpadButton_A && app->page->link_count) {
                char next[BROWSER_URL_MAX];
                strcpy(next, app->page->links[app->link].url);
                if (app->history_count < HISTORY_MAX)
                    strcpy(app->history[app->history_count++], app->page->url);
                load_page(app, next); dirty = true;
            }
        }
        if (dirty) { render(app); dirty = false; }
        SDL_Delay(16);
    }
done:
    if (app->network_ready) curl_global_cleanup();
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
