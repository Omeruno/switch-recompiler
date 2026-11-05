#include "js_runtime.h"
#include "page.h"
#include "profile.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void run(BrowserPage *page, const char *url, const char *script) {
    char html[8192];
    snprintf(html, sizeof(html), "<script>%s</script>", script);
    memset(page, 0, sizeof(*page));
    snprintf(page->url, sizeof(page->url), "%s", url);
    assert(browser_parse_html(page, html, strlen(html)));
    browser_js_apply(page);
    if (page->js_notice[0])
        fprintf(stderr, "%s\n", page->js_notice);
    assert(page->js_ran == 1);
}
int main(int argc, char **argv) {
    assert(argc == 2);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    assert(browser_profile_open(argv[1], NULL));
    BrowserPage *page = calloc(1, sizeof(*page));
    assert(page);
    run(page, "https://example.test/ru/",
        "localStorage.setItem('language','русский');sessionStorage.setItem('session','yes');"
        "document.cookie='kd_lang=ru; Path=/; Secure; "
        "Max-Age=3600';if(!document.cookie.includes('kd_lang=ru'))throw "
        "'cookie';if(localStorage.length!==1||localStorage.key(0)!=='language')throw 'length';let "
        "rejected=false;try{localStorage.setItem('big','x'.repeat(17000))}catch(e){rejected=e "
        "instanceof RangeError}if(!rejected)throw 'quota';");
    run(page, "https://example.test:443/other",
        "if(localStorage.getItem('language')!=='русский'||sessionStorage.getItem('session')!=='yes'"
        ")throw 'navigation';");
    run(page, "https://other.test/",
        "if(localStorage.getItem('language')!==null||sessionStorage.getItem('session')!==null||"
        "document.cookie)throw 'origin leak';localStorage.setItem('other','value');");
    run(page, "https://example.test:444/",
        "if(localStorage.getItem('language')!==null)throw 'port leak';");
    browser_profile_close();
    assert(browser_profile_open(argv[1], NULL));
    run(page, "https://example.test/ru/",
        "if(localStorage.getItem('language')!=='русский'||sessionStorage.getItem('session')!==null|"
        "|!document.cookie.includes('kd_lang=ru'))throw "
        "'restart';localStorage.removeItem('language');if(localStorage.length!==0)throw 'remove';");
    run(page, "https://other.test/",
        "if(localStorage.getItem('other')!=='value')throw 'cross origin "
        "remove';localStorage.clear();if(localStorage.length)throw 'clear';");
    free(page);
    browser_profile_close();
    curl_global_cleanup();
    puts("browser JS storage and document.cookie: PASS");
    return 0;
}
