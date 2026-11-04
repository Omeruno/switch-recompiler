#include <switch.h>
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "page.h"

#define HISTORY_MAX 8
#define VIEW_LINES 20
#define VIEW_WIDTH 74

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

static void show_text(const char *text, size_t skip_lines) {
    size_t line = 0, column = 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p == '\n' || column == VIEW_WIDTH) {
            if (line >= skip_lines && line < skip_lines + VIEW_LINES) putchar('\n');
            ++line;
            column = 0;
            if (line >= skip_lines + VIEW_LINES) break;
            if (*p == '\n') continue;
        }
        if (line >= skip_lines) putchar(*p);
        ++column;
    }
}

static void render(const BrowserPage *page, bool loaded, int selection,
                   size_t scroll, size_t link, const char *status) {
    consoleClear();
    printf("Switch Browser | native HTTPS prototype\n");
    if (!loaded) {
        printf("\n%c Open Keldurn (RU)\n", selection == 0 ? '>' : ' ');
        printf("%c Open Keldurn game\n", selection == 1 ? '>' : ' ');
        printf("%c Enter HTTPS address\n\n", selection == 2 ? '>' : ' ');
        printf("D-pad: select  A: open  +: quit\n");
    } else {
        printf("%.73s\n", page->url);
        printf("HTTP %ld | %.60s\n", page->http_status, page->title);
        printf("Up/Down: scroll  ZL/ZR: link  A: follow  B: back\n");
        printf("X: address  Y: reload  +: quit\n");
        if (page->error[0]) printf("Page error: %.69s\n", page->error);
        printf("\n");
        show_text(page->text, scroll);
        printf("\n\n");
        if (page->link_count)
            printf("Link %u/%u: %.66s\n", (unsigned)(link + 1),
                   (unsigned)page->link_count, page->links[link].url);
    }
    printf("\n%.74s\n", status);
    consoleUpdate(NULL);
}

static void load_page(BrowserPage *page, const char *url, char *status,
                      size_t status_size, size_t *scroll, size_t *link) {
    snprintf(status, status_size, "Loading %.100s", url);
    consoleClear();
    printf("Switch Browser | native HTTPS prototype\n\n%s\n", status);
    consoleUpdate(NULL);
    int ok = browser_fetch(page, url);
    snprintf(status, status_size, "%s", ok ? "Page received over HTTPS." : page->error);
    *scroll = 0;
    *link = 0;
}

int main(void) {
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);

    BrowserPage *page = calloc(1, sizeof(*page));
    if (!page) { consoleExit(NULL); return 1; }
    Result socket_result = socketInitializeDefault();
    int curl_result = R_SUCCEEDED(socket_result) ? curl_global_init(CURL_GLOBAL_DEFAULT) : -1;
    bool network_ready = R_SUCCEEDED(socket_result) && curl_result == 0;
    char status[192];
    snprintf(status, sizeof(status), "%s", network_ready ?
             "Direct HTTPS ready. No Nintendo WebApplet." : "Network initialization failed.");
    char address[BROWSER_URL_MAX] = "https://";
    char history[HISTORY_MAX][BROWSER_URL_MAX] = {{0}};
    size_t history_count = 0, scroll = 0, link = 0;
    bool loaded = false;
    int selection = 0;

    while (appletMainLoop()) {
        padUpdate(&pad);
        u64 pressed = padGetButtonsDown(&pad);
        if (pressed & HidNpadButton_Plus) break;
        if (!loaded) {
            if (pressed & HidNpadButton_Up) selection = (selection + 2) % 3;
            if (pressed & HidNpadButton_Down) selection = (selection + 1) % 3;
            if (pressed & HidNpadButton_A) {
                const char *next = selection == 0 ? "https://keldurn.com/ru/" :
                                   selection == 1 ? "https://play.keldurn.com/" : address;
                bool accepted = selection != 2 || enter_url(address);
                if (accepted && network_ready) {
                    next = selection == 2 ? address : next;
                    load_page(page, next, status, sizeof(status), &scroll, &link);
                    loaded = true;
                } else if (!accepted) snprintf(status, sizeof(status), "Address cancelled or invalid.");
                padUpdate(&pad);
            }
        } else {
            if (pressed & HidNpadButton_Up && scroll) --scroll;
            if (pressed & HidNpadButton_Down) ++scroll;
            if (page->link_count) {
                if (pressed & HidNpadButton_ZL) link = (link + page->link_count - 1) % page->link_count;
                if (pressed & HidNpadButton_ZR) link = (link + 1) % page->link_count;
            }
            if (pressed & HidNpadButton_B) {
                if (history_count) {
                    char previous[BROWSER_URL_MAX];
                    strcpy(previous, history[--history_count]);
                    load_page(page, previous, status, sizeof(status), &scroll, &link);
                } else loaded = false;
            } else if (pressed & HidNpadButton_Y) {
                char current[BROWSER_URL_MAX];
                strcpy(current, page->url);
                load_page(page, current, status, sizeof(status), &scroll, &link);
            } else if (pressed & HidNpadButton_X) {
                strcpy(address, page->url);
                if (enter_url(address)) {
                    if (history_count < HISTORY_MAX) strcpy(history[history_count++], page->url);
                    load_page(page, address, status, sizeof(status), &scroll, &link);
                }
                padUpdate(&pad);
            } else if (pressed & HidNpadButton_A && page->link_count) {
                char next[BROWSER_URL_MAX];
                strcpy(next, page->links[link].url);
                if (history_count < HISTORY_MAX) strcpy(history[history_count++], page->url);
                load_page(page, next, status, sizeof(status), &scroll, &link);
            }
        }
        render(page, loaded, selection, scroll, link, status);
    }
    if (network_ready) curl_global_cleanup();
    if (R_SUCCEEDED(socket_result)) socketExit();
    free(page);
    consoleExit(NULL);
    return 0;
}
