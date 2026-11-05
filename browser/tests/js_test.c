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

    BrowserPage csp_external = {0};
    strcpy(csp_external.url, "https://example.test/");
    strcpy(csp_external.csp, "default-src 'none'; script-src 'self'");
    const char *external_html = "<script src='/first.js'></script>"
        "<script>document.title = 'blocked'</script>";
    assert(browser_parse_html(&csp_external, external_html, strlen(external_html)));
    strcpy(csp_external.scripts[0], "document.title = 'external ran'");
    browser_js_apply(&csp_external);
    assert(csp_external.js_ran == 1);
    assert(strcmp(csp_external.title, "external ran") == 0);

    BrowserPage navigation = {0};
    strcpy(navigation.url, "https://example.test/ru/?utm_source=switch");
    const char *navigation_html = "<html lang='ru'><a href='/start?from=ru'>Play</a>"
        "<a href='/about'>About</a><script>"
        "const current = new URL(location.href);"
        "for (const link of document.querySelectorAll('a[href^=\"/start?\"]')) {"
        "const target = new URL(link.href);"
        "target.searchParams.set('utm_source', current.searchParams.get('utm_source'));"
        "link.href = target.pathname + target.search;}"
        "document.querySelectorAll('a')[1].href = 'javascript:alert(1)';"
        "document.title = document.documentElement.lang + ':' + location.hostname;"
        "if (navigator.language !== 'ru-RU') throw 'language';"
        "</script></html>";
    assert(browser_parse_html(&navigation, navigation_html, strlen(navigation_html)));
    browser_js_apply(&navigation);
    assert(navigation.js_ran == 1);
    assert(strcmp(navigation.title, "ru:example.test") == 0);
    assert(strcmp(navigation.links[0].url, "https://example.test/start?from=ru&utm_source=switch") == 0);
    assert(navigation.links[1].url[0] == 0);

    BrowserPage intl = {0};
    strcpy(intl.url, "https://example.test/");
    const char *intl_html = "<script>"
        "const r = new Intl.RelativeTimeFormat('ru-RU',{numeric:'auto'});"
        "const expected = ['через 1 минуту','через 2 минуты','через 5 минут','через 11 минут','через 21 минуту','через 22 минуты'];"
        "[1,2,5,11,21,22].forEach((n,i)=>{if(r.format(n,'minutes')!==expected[i])throw 'plural:'+n});"
        "if(r.format(-2,'hour')!=='2 часа назад'||r.format(0,'day')!=='сегодня'||r.format(1,'day')!=='завтра')throw 'relative';"
        "const d = new Intl.DateTimeFormat('ru',{weekday:'short',day:'numeric',month:'short',hour:'2-digit',minute:'2-digit',timeZone:'UTC'});"
        "if(d.format(Date.UTC(2026,10,5,9,7))!=='чт 5 нояб. 09:07')throw d.format(Date.UTC(2026,10,5,9,7));"
        "if(d.resolvedOptions().timeZone!=='UTC')throw 'timezone';"
        "let rejected=false;try{new Intl.DateTimeFormat('ru',{timeZone:'Europe/Moscow'})}catch(e){rejected=e instanceof RangeError}"
        "if(!rejected)throw 'unsupported timezone accepted';"
        "if(new Intl.RelativeTimeFormat('en').format(-1,'day')!=='1 day ago')throw 'english';"
        "document.title='Форматирование OK';</script>";
    assert(browser_parse_html(&intl, intl_html, strlen(intl_html)));
    browser_js_apply(&intl);
    assert(intl.js_ran == 1);
    assert(strcmp(intl.title, "Форматирование OK") == 0);

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
