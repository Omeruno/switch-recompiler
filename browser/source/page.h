#ifndef SWITCH_BROWSER_PAGE_H
#define SWITCH_BROWSER_PAGE_H

#include <stddef.h>

#define BROWSER_URL_MAX 1024
#define BROWSER_TEXT_MAX 32768
#define BROWSER_LINK_MAX 64

typedef struct {
    char url[BROWSER_URL_MAX];
    char label[96];
} BrowserLink;

typedef struct {
    char url[BROWSER_URL_MAX];
    char title[256];
    char text[BROWSER_TEXT_MAX];
    BrowserLink links[BROWSER_LINK_MAX];
    size_t link_count;
    long http_status;
    char error[192];
} BrowserPage;

int browser_valid_url(const char *url);
int browser_parse_html(BrowserPage *page, const char *html, size_t size);
int browser_fetch(BrowserPage *page, const char *url);

#endif
