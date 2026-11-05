#include "page.h"
#include "js_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    BrowserPage *page = calloc(1,sizeof(*page));
    assert(page);
    strcpy(page->url,"https://example.test/demo");
    const char *html = "<html lang='ru'><head><title>Original</title></head><body>"
        "<p id='ready'></p><p id='ticks'>0</p><time data-kd-when datetime='2026-11-05T09:07:00Z'></time>"
        "<button id='add'>Добавить</button><a href='/original'>Ссылка</a>"
        "<script>let count=0;"
        "document.addEventListener('DOMContentLoaded',()=>{document.getElementById('ready').textContent='Готово'});"
        "const timer=setInterval(()=>{document.getElementById('ticks').textContent=String(++count);if(count===2)clearInterval(timer)},10);"
        "const canceled=setTimeout(()=>{document.title='BAD'},1);clearTimeout(canceled);"
        "requestAnimationFrame(t=>{if(t<0)throw 'timestamp';document.title='Frame'});"
        "document.querySelector('time[data-kd-when]').textContent='Дата: '+new Intl.DateTimeFormat('ru',{hour:'2-digit',minute:'2-digit'}).format(Date.parse(document.querySelector('time').dateTime));"
        "document.getElementById('add').addEventListener('click',async function(){const p=document.createElement('p');p.classList.add('message');p.dataset.result='ok';p.append('Кнопка работает');document.body.prepend(p);await Promise.resolve();this.textContent='Нажато'},{once:true});"
        "document.addEventListener('click',event=>{const link=event.target.closest('a');if(link){event.preventDefault();link.href='/changed';document.title='Prevented'}});"
        "</script></body></html>";
    assert(browser_parse_html(page,html,strlen(html)));
    BrowserJS *session=browser_js_start(page);
    assert(session && page->js_ran==1 && !page->js_notice[0]);
    assert(strstr(page->text,"Готово") && strstr(page->text,"Дата: 09:07"));
    uint64_t base=browser_js_clock_ms();
    assert(browser_js_tick(session,base+100));
    assert(strcmp(page->title,"Frame")==0);
    assert(browser_js_tick(session,base+200));
    assert(!browser_js_tick(session,base+300));
    assert(strstr(page->text,"\n2\n"));
    char target[BROWSER_URL_MAX]={0};
    assert(!browser_js_click(session,0,target));
    assert(strstr(page->text,"Кнопка работает") && strcmp(page->links[0].label,"Нажато")==0);
    assert(!browser_js_click(session,1,target));
    assert(strcmp(page->title,"Prevented")==0);
    assert(strcmp(page->links[1].url,"https://example.test/changed")==0);
    browser_js_destroy(session);

    // A fresh navigation owns a new heap; previous timers cannot change it.
    const char *fresh="<title>Fresh</title><script>setTimeout(()=>{document.body.textContent='Новая страница'},1)</script>";
    assert(browser_parse_html(page,fresh,strlen(fresh)));
    session=browser_js_start(page);
    assert(session);
    browser_js_tick(session,browser_js_clock_ms()+1000);
    assert(strcmp(page->title,"Fresh")==0);
    assert(strcmp(page->text,"Новая страница")==0);
    browser_js_destroy(session);

    // A runaway callback is interrupted and the session remains destructible.
    const char *runaway="<title>Responsive</title><script>setTimeout(()=>{while(true){}},1)</script>";
    assert(browser_parse_html(page,runaway,strlen(runaway)));
    session=browser_js_start(page);
    assert(session);
    browser_js_tick(session,browser_js_clock_ms()+1000);
    assert(strstr(page->js_notice,"interrupted"));
    browser_js_destroy(session);

    // The custom allocator enforces the heap cap; OOM does not kill the host.
    const char *memory="<script>globalThis.huge = new Array(5000000).fill('x')</script>";
    assert(browser_parse_html(page,memory,strlen(memory)));
    session=browser_js_start(page);
    assert(session && page->js_ran==0 && page->js_notice[0]);
    browser_js_destroy(session);

    // Bound registrations as well as execution time.
    const char *limit="<script>for(let i=0;i<65;i++)setTimeout(()=>{},100000)</script>";
    assert(browser_parse_html(page,limit,strlen(limit)));
    session=browser_js_start(page);
    assert(session && strstr(page->js_notice,"Timer limit"));
    browser_js_destroy(session);
    free(page);
    puts("browser persistent DOM, timers and input: PASS");
    return 0;
}
