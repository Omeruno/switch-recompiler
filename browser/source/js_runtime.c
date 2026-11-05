#include "js_runtime.h"
#include "js_web_apis.h"
#include "js_intl.h"
#include "quickjs.h"
#include <curl/curl.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define JS_MEMORY_LIMIT (16u * 1024u * 1024u)
#define JS_STACK_LIMIT (512u * 1024u)
#define JS_TIME_LIMIT_MS 300

typedef struct { uint64_t deadline; unsigned checks; } ScriptBudget;
typedef union { max_align_t alignment; size_t size; } Allocation;

static void *js_alloc(JSMallocState *state, size_t size) {
    (void)state;
    if (size > SIZE_MAX - sizeof(Allocation)) return NULL;
    Allocation *block = malloc(sizeof(Allocation) + size);
    if (!block) return NULL;
    block->size = size;
    return block + 1;
}

static void js_dealloc(JSMallocState *state, void *pointer) {
    (void)state;
    if (pointer) free((Allocation *)pointer - 1);
}

static void *js_resize(JSMallocState *state, void *pointer, size_t size) {
    if (!pointer) return js_alloc(state, size);
    if (!size) { js_dealloc(state, pointer); return NULL; }
    if (size > SIZE_MAX - sizeof(Allocation)) return NULL;
    Allocation *block = realloc((Allocation *)pointer - 1, sizeof(Allocation) + size);
    if (!block) return NULL;
    block->size = size;
    return block + 1;
}

static size_t js_size(const void *pointer) {
    return pointer ? ((const Allocation *)pointer - 1)->size : 0;
}

static const JSMallocFunctions js_allocator = {
    js_alloc, js_dealloc, js_resize, js_size
};

static JSValue url_resolve(JSContext *context, JSValueConst this_value,
                           int argc, JSValueConst *argv) {
    (void)this_value;
    const char *input = argc ? JS_ToCString(context, argv[0]) : NULL;
    const char *base = argc > 1 && !JS_IsUndefined(argv[1]) ? JS_ToCString(context, argv[1]) : NULL;
    CURLU *url = curl_url();
    char *resolved = NULL;
    int valid = input && *input && url &&
                (!base || curl_url_set(url, CURLUPART_URL, base, 0) == CURLUE_OK) &&
                curl_url_set(url, CURLUPART_URL, input, 0) == CURLUE_OK &&
                curl_url_get(url, CURLUPART_URL, &resolved, 0) == CURLUE_OK;
    JSValue result = valid ? JS_NewString(context, resolved) : JS_ThrowTypeError(context, "Invalid URL");
    curl_free(resolved); curl_url_cleanup(url);
    if (input) JS_FreeCString(context, input);
    if (base) JS_FreeCString(context, base);
    return result;
}

static JSValue url_part(JSContext *context, JSValueConst this_value,
                        int argc, JSValueConst *argv) {
    (void)this_value;
    const char *input = argc ? JS_ToCString(context, argv[0]) : NULL;
    int32_t part = -1;
    if (argc > 1) JS_ToInt32(context, &part, argv[1]);
    CURLUPart kind = part == 0 ? CURLUPART_HOST : part == 1 ? CURLUPART_PATH : CURLUPART_QUERY;
    CURLU *url = curl_url();
    char *value = NULL;
    if (input && url && part >= 0 && part <= 2 &&
        curl_url_set(url, CURLUPART_URL, input, 0) == CURLUE_OK)
        curl_url_get(url, kind, &value, 0);
    JSValue result = JS_NewString(context, value ? value : "");
    curl_free(value); curl_url_cleanup(url);
    if (input) JS_FreeCString(context, input);
    return result;
}

static uint64_t now_ms(void) {
    struct timeval time;
    gettimeofday(&time, NULL);
    return (uint64_t)time.tv_sec * 1000u + (uint64_t)time.tv_usec / 1000u;
}

static int interrupted(JSRuntime *runtime, void *opaque) {
    (void)runtime;
    ScriptBudget *budget = (ScriptBudget *)opaque;
    return ++budget->checks > 100000u || now_ms() >= budget->deadline;
}

static JSValue console_log(JSContext *context, JSValueConst this_value,
                           int argc, JSValueConst *argv) {
    (void)this_value;
    BrowserPage *page = JS_GetContextOpaque(context);
    size_t used = strlen(page->js_notice);
    for (int i = 0; i < argc && used + 2 < sizeof(page->js_notice); ++i) {
        const char *value = JS_ToCString(context, argv[i]);
        if (!value) continue;
        if (used) page->js_notice[used++] = ' ';
        size_t length = strlen(value);
        if (length > sizeof(page->js_notice) - used - 1)
            length = sizeof(page->js_notice) - used - 1;
        memcpy(page->js_notice + used, value, length);
        used += length;
        page->js_notice[used] = 0;
        JS_FreeCString(context, value);
    }
    return JS_UNDEFINED;
}

static void copy_js_string(JSContext *context, JSValueConst value,
                           char *target, size_t capacity) {
    if (!JS_IsString(value)) return;
    size_t length = 0;
    const char *text = JS_ToCStringLen(context, &length, value);
    if (!text) return;
    if (length >= capacity) length = capacity - 1;
    memcpy(target, text, length);
    target[length] = 0;
    JS_FreeCString(context, text);
}

static void report_exception(JSContext *context, BrowserPage *page) {
    JSValue exception = JS_GetException(context);
    JSValue description = JS_ToString(context, exception);
    if (!JS_IsException(description))
        copy_js_string(context, description, page->js_notice, sizeof(page->js_notice));
    else {
        JSValue secondary = JS_GetException(context);
        JS_FreeValue(context, secondary);
    }
    if (!page->js_notice[0])
        snprintf(page->js_notice, sizeof(page->js_notice), "JavaScript interrupted or threw");
    JS_FreeValue(context, description);
    JS_FreeValue(context, exception);
}

void browser_js_apply(BrowserPage *page) {
    if (!page) return;
    page->js_ran = 0;
    page->js_notice[0] = 0;
    if (!page->script_count) return;
    if (!page->inline_allowed && !page->external_allowed) {
        snprintf(page->js_notice, sizeof(page->js_notice), "Scripts blocked by site CSP");
        return;
    }

    JSRuntime *runtime = JS_NewRuntime2(&js_allocator, NULL);
    if (!runtime) {
        snprintf(page->js_notice, sizeof(page->js_notice), "JavaScript runtime allocation failed");
        return;
    }
    JS_SetMemoryLimit(runtime, JS_MEMORY_LIMIT);
    JS_SetMaxStackSize(runtime, JS_STACK_LIMIT);
    ScriptBudget budget = {now_ms() + JS_TIME_LIMIT_MS, 0};
    JS_SetInterruptHandler(runtime, interrupted, &budget);
    JSContext *context = JS_NewContext(runtime);
    if (!context) {
        snprintf(page->js_notice, sizeof(page->js_notice), "JavaScript context allocation failed");
        JS_FreeRuntime(runtime);
        return;
    }
    JS_SetContextOpaque(context, page);
    JSValue global = JS_GetGlobalObject(context);
    JSValue document = JS_NewObject(context);
    JSValue body = JS_NewObject(context);
    JS_SetPropertyStr(context, body, "textContent", JS_NewString(context, page->text));
    JS_SetPropertyStr(context, document, "body", body);
    JS_SetPropertyStr(context, document, "title", JS_NewString(context, page->title));
    JS_SetPropertyStr(context, global, "document", JS_DupValue(context, document));
    JSValue console = JS_NewObject(context);
    JS_SetPropertyStr(context, console, "log", JS_NewCFunction(context, console_log, "log", 1));
    JS_SetPropertyStr(context, global, "console", console);
    JS_SetPropertyStr(context, global, "window", JS_DupValue(context, global));

    JS_SetPropertyStr(context, global, "__urlResolve", JS_NewCFunction(context, url_resolve, "__urlResolve", 2));
    JS_SetPropertyStr(context, global, "__urlPart", JS_NewCFunction(context, url_part, "__urlPart", 2));
    JSValue location = JS_NewObject(context);
    JS_SetPropertyStr(context, location, "href", JS_NewString(context, page->url));
    JS_SetPropertyStr(context, global, "location", location);
    JSValue root = JS_NewObject(context);
    JS_SetPropertyStr(context, root, "lang", JS_NewString(context, page->language));
    JS_SetPropertyStr(context, document, "documentElement", root);
    JS_SetPropertyStr(context, document, "referrer", JS_NewString(context, ""));
    JS_SetPropertyStr(context, document, "cookie", JS_NewString(context, ""));
    JSValue navigator = JS_NewObject(context);
    JS_SetPropertyStr(context, navigator, "language", JS_NewString(context, "ru-RU"));
    JSValue languages = JS_NewArray(context);
    JS_SetPropertyUint32(context, languages, 0, JS_NewString(context, "ru-RU"));
    JS_SetPropertyStr(context, navigator, "languages", languages);
    JS_SetPropertyStr(context, global, "navigator", navigator);
    JSValue links = JS_NewArray(context);
    for (size_t i = 0; i < page->link_count; ++i) {
        JSValue link = JS_NewObject(context);
        JS_SetPropertyStr(context, link, "href", JS_NewString(context, page->links[i].url));
        JS_SetPropertyStr(context, link, "rawHref", JS_NewString(context, page->links[i].href));
        JS_SetPropertyUint32(context, links, (uint32_t)i, link);
    }
    JS_SetPropertyStr(context, global, "__browserLinks", JS_DupValue(context, links));
    JSValue bootstrap = JS_Eval(context, browser_web_apis, strlen(browser_web_apis),
                                "<browser-apis>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(bootstrap)) {
        report_exception(context, page);
        JS_FreeValue(context, bootstrap);
        goto cleanup;
    }
    JS_FreeValue(context, bootstrap);
    bootstrap = JS_Eval(context, browser_intl, strlen(browser_intl), "<intl>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(bootstrap)) {
        report_exception(context, page);
        JS_FreeValue(context, bootstrap);
        goto cleanup;
    }
    JS_FreeValue(context, bootstrap);

    for (size_t i = 0; i < page->script_count; ++i) {
        if (page->script_urls[i][0] ? !page->external_allowed : !page->inline_allowed) continue;
        if (!page->scripts[i][0]) continue;
        budget.deadline = now_ms() + JS_TIME_LIMIT_MS;
        budget.checks = 0;
        JSValue result = JS_Eval(context, page->scripts[i], strlen(page->scripts[i]),
                                 page->script_urls[i][0] ? page->script_urls[i] : "<inline>",
                                 JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result)) {
            report_exception(context, page);
            JS_FreeValue(context, result);
            continue;
        }
        JS_FreeValue(context, result);
        ++page->js_ran;
    }
    for (int i = 0; i < 64 && now_ms() < budget.deadline; ++i) {
        JSContext *job_context = NULL;
        int result = JS_ExecutePendingJob(runtime, &job_context);
        if (result <= 0) break;
    }
    JSValue title = JS_GetPropertyStr(context, document, "title");
    copy_js_string(context, title, page->title, sizeof(page->title));
    JS_FreeValue(context, title);
    JSValue new_body = JS_GetPropertyStr(context, document, "body");
    if (JS_IsObject(new_body)) {
        JSValue body_text = JS_GetPropertyStr(context, new_body, "textContent");
        copy_js_string(context, body_text, page->text, sizeof(page->text));
        JS_FreeValue(context, body_text);
    }
    JS_FreeValue(context, new_body);
    for (size_t i = 0; i < page->link_count; ++i) {
        JSValue link = JS_GetPropertyUint32(context, links, (uint32_t)i);
        JSValue args[] = {JS_GetPropertyStr(context, link, "href"), JS_NewString(context, page->url)};
        JSValue resolved = url_resolve(context, JS_UNDEFINED, 2, args);
        if (!JS_IsException(resolved)) {
            char candidate[BROWSER_URL_MAX] = {0};
            copy_js_string(context, resolved, candidate, sizeof(candidate));
            if (browser_valid_url(candidate))
                snprintf(page->links[i].url, sizeof(page->links[i].url), "%s", candidate);
        } else {
            JSValue exception = JS_GetException(context);
            JS_FreeValue(context, exception);
        }
        JS_FreeValue(context, resolved);
        JS_FreeValue(context, args[0]); JS_FreeValue(context, args[1]);
        JS_FreeValue(context, link);
    }
cleanup:
    JS_FreeValue(context, links);
    JS_FreeValue(context, document);
    JS_FreeValue(context, global);
    JS_FreeContext(context);
    JS_FreeRuntime(runtime);
}
