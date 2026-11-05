#include "js_runtime.h"
#include "page.h"
#include "profile.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    assert(argc == 7);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    assert(browser_profile_open(argv[1], argv[5]));
    BrowserPage *page = calloc(1, sizeof(*page));
    assert(page);
    char url[2048];
    snprintf(url, sizeof(url), "%s/set?token=DO_NOT_LOG_TOKEN", argv[2]);
    int fetched = browser_fetch(page, url);
    if (!fetched)
        fprintf(stderr, "HTTPS fixture: %s\n", page->error);
    assert(fetched);
    assert(page->script_count == 1 && !page->script_fetch_failed);
    browser_js_apply(page);
    assert(strcmp(page->title, "Cookie reached script") == 0);
    char cookie[4096];
    browser_cookie_get(url, cookie, sizeof(cookie));
    assert(strstr(cookie, "ui=ru") && !strstr(cookie, "HTTP_ONLY_SECRET"));
    snprintf(url, sizeof(url), "%s/check", argv[2]);
    assert(browser_fetch(page, url));
    assert(strstr(page->text, "cookie ok"));
    browser_profile_close();
    assert(browser_profile_open(argv[1], argv[5]));
    assert(browser_fetch(page, url));
    assert(strstr(page->text, "cookie ok"));
    assert(!browser_fetch(page, argv[3]));
    assert(strstr(page->error, "certificate"));
    assert(!browser_fetch(page, argv[4]));
    assert(strstr(page->error, "certificate"));
    snprintf(url, sizeof(url), "%s/redirect-http", argv[2]);
    assert(!browser_fetch(page, url));
    browser_profile_close();
    assert(browser_profile_open(argv[1], argv[6]));
    snprintf(url, sizeof(url), "%s/check", argv[2]);
    assert(!browser_fetch(page, url));
    char path[1024];
    snprintf(path, sizeof(path), "%s/network.log", argv[1]);
    FILE *f = fopen(path, "r");
    assert(f);
    char logs[65537] = {0};
    fread(logs, 1, sizeof(logs) - 1, f);
    fclose(f);
    assert(!strstr(logs, "DO_NOT_LOG_TOKEN") && !strstr(logs, "HTTP_ONLY_SECRET") &&
           !strstr(logs, "/set") && !strstr(logs, "ui=ru"));
    free(page);
    browser_profile_close();
    curl_global_cleanup();
    puts("browser HTTPS trust, cookie resources and private diagnostics: PASS");
    return 0;
}
