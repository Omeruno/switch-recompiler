#include "page.h"
#include "js_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    BrowserPage page = {0};
    strcpy(page.url, "https://example.test/");
    const char *html = "<title>Old</title><p>Initial</p>"
        "<script>document.title = 'Новый'; "
        "document.body.textContent = 'Привет, Switch'; "
        "console.log('script ran')</script>"
        "<script type='application/ld+json'>{\"name\":\"skip\"}</script>";
    assert(browser_parse_html(&page, html, strlen(html)));
    assert(page.script_count == 1);
    assert(page.inline_allowed);
    browser_js_apply(&page);
    assert(page.js_ran == 1);
    assert(strcmp(page.title, "Новый") == 0);
    assert(strcmp(page.text, "Привет, Switch") == 0);
    assert(strcmp(page.js_notice, "script ran") == 0);

    BrowserPage ordered = {0};
    strcpy(ordered.url, "https://example.test/");
    const char *ordered_html = "<script src='/first.js'></script>"
        "<script>document.title += ' two'</script>";
    assert(browser_parse_html(&ordered, ordered_html, strlen(ordered_html)));
    strcpy(ordered.scripts[0], "document.title = 'one'"); // stand-in for fetched HTTPS script
    browser_js_apply(&ordered);
    assert(ordered.js_ran == 2);
    assert(strcmp(ordered.title, "one two") == 0);

    BrowserPage blocked = {0};
    strcpy(blocked.url, "https://example.test/");
    strcpy(blocked.csp, "script-src 'self'");
    assert(browser_parse_html(&blocked, html, strlen(html)));
    browser_js_apply(&blocked);
    assert(blocked.js_ran == 0);
    assert(strcmp(blocked.title, "Old") == 0);

    BrowserPage looping = {0};
    strcpy(looping.url, "https://example.test/");
    const char *loop_html = "<script>while(true){}</script>";
    assert(browser_parse_html(&looping, loop_html, strlen(loop_html)));
    browser_js_apply(&looping);
    assert(looping.js_ran == 0);
    assert(looping.js_notice[0]);
    puts("browser JavaScript runtime: PASS");
    return 0;
}
