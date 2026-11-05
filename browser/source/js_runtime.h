#ifndef SWITCH_BROWSER_JS_RUNTIME_H
#define SWITCH_BROWSER_JS_RUNTIME_H

#include "page.h"
#include <stdint.h>

typedef struct BrowserJS BrowserJS;
BrowserJS *browser_js_start(BrowserPage *page);
void browser_js_destroy(BrowserJS *session);
uint64_t browser_js_clock_ms(void);
// Returns whether visible page state changed. now_ms must be monotonic.
int browser_js_tick(BrowserJS *session, uint64_t now_ms);
// Dispatch an interactive element's click; return whether to follow its URL.
int browser_js_click(BrowserJS *session, size_t index, char target[BROWSER_URL_MAX]);
// Convenience for noninteractive callers.
void browser_js_apply(BrowserPage *page);

#endif
