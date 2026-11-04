#ifndef SWITCH_BROWSER_JS_RUNTIME_H
#define SWITCH_BROWSER_JS_RUNTIME_H

#include "page.h"

// Run bounded, inline classic scripts only. No network or system APIs are
// exposed to JavaScript. A CSP header blocks inline execution conservatively.
void browser_js_apply(BrowserPage *page);

#endif
