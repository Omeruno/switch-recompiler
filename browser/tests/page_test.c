#include "page.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    assert(browser_valid_url("https://keldurn.com/ru/"));
    assert(!browser_valid_url("http://keldurn.com/ru/"));
    assert(!browser_valid_url("https://evil.test/\nheader"));
    assert(!browser_valid_url("https:///missing-host"));

    BrowserPage page = {0};
    strcpy(page.url, "https://keldurn.com/ru/");
    const char *html = "<html><head><title>Keldurn &amp; WoW</title>"
        "<style>.secret{display:none}</style></head><body>"
        "<h1>Welcome</h1><script>hidden()</script>"
        "<p>Play &amp; explore</p><a href='/play'>Enter game</a>"
        "<a href='http://insecure.test/'>blocked</a></body></html>";
    assert(browser_parse_html(&page, html, strlen(html)));
    assert(strcmp(page.title, "Keldurn & WoW") == 0);
    assert(strstr(page.text, "Welcome"));
    assert(strstr(page.text, "Play & explore"));
    assert(!strstr(page.text, "hidden()"));
    assert(page.script_count == 1);
    assert(page.link_count == 1);
    assert(strcmp(page.links[0].url, "https://keldurn.com/play") == 0);

    BrowserPage scripts = {0};
    strcpy(scripts.url, "https://keldurn.com/ru/");
    const char *ordered = "<script src='/acquisition.js'></script>"
        "<script>document.title='after external'</script>"
        "<script type='module' src='/module.js'></script>"
        "<script src='https://other.test/foreign.js'></script>";
    assert(browser_parse_html(&scripts, ordered, strlen(ordered)));
    assert(scripts.script_count == 3);
    assert(strcmp(scripts.script_urls[0], "/acquisition.js") == 0);
    assert(strstr(scripts.scripts[1], "after external"));
    assert(strcmp(scripts.script_urls[2], "https://other.test/foreign.js") == 0);
    puts("browser page parser: PASS");
    return 0;
}
