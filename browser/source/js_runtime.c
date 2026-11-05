#include "js_runtime.h"
#include "profile.h"
#include "js_web_apis.h"
#include "js_intl.h"
#include "dom_js.h"
#include "quickjs.h"
#include <curl/curl.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define JS_MEMORY_LIMIT (16u * 1024u * 1024u)
#define JS_STACK_LIMIT (512u * 1024u)
#define JS_TIME_LIMIT_MS 300

typedef struct { uint64_t deadline; unsigned checks; } ScriptBudget;
typedef union { max_align_t alignment; size_t size; } Allocation;
struct BrowserJS {
    JSRuntime *runtime;
    JSContext *context;
    JSValue global;
    BrowserPage *page;
    ScriptBudget budget;
    uint64_t clock;
};

static void *js_alloc(JSMallocState *state, size_t size) {
    if (size > SIZE_MAX - sizeof(Allocation) || state->malloc_size > state->malloc_limit ||
        size + sizeof(Allocation) > state->malloc_limit - state->malloc_size) return NULL;
    Allocation *block = malloc(sizeof(Allocation) + size);
    if (!block) return NULL;
    block->size = size;
    ++state->malloc_count;
    state->malloc_size += size + sizeof(Allocation);
    return block + 1;
}

static void js_dealloc(JSMallocState *state, void *pointer) {
    if (pointer) {
        --state->malloc_count;
        state->malloc_size -= ((Allocation *)pointer - 1)->size + sizeof(Allocation);
        free((Allocation *)pointer - 1);
    }
}

static void *js_resize(JSMallocState *state, void *pointer, size_t size) {
    if (!pointer) return js_alloc(state, size);
    if (!size) { js_dealloc(state, pointer); return NULL; }
    size_t old = ((Allocation *)pointer - 1)->size;
    if (size > SIZE_MAX - sizeof(Allocation) || (size > old &&
        (state->malloc_size > state->malloc_limit || size - old > state->malloc_limit - state->malloc_size))) return NULL;
    Allocation *block = realloc((Allocation *)pointer - 1, sizeof(Allocation) + size);
    if (!block) return NULL;
    block->size = size;
    state->malloc_size = state->malloc_size - old + size;
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

static JSValue storage_call(JSContext *context, JSValueConst self, int argc, JSValueConst *argv) {
    (void)self;
    BrowserPage *page = JS_GetContextOpaque(context);
    int32_t operation = -1;
    if (argc < 2 || JS_ToInt32(context,&operation,argv[0]) < 0) return JS_EXCEPTION;
    int session = JS_ToBool(context,argv[1]) > 0;
    if (operation == 4) return JS_NewInt32(context,(int)browser_storage_length(page->url,session));
    if (operation == 5) {
        uint32_t index = 0;
        if (argc > 2 && JS_ToUint32(context,&index,argv[2]) < 0) return JS_EXCEPTION;
        const char *key = browser_storage_key(page->url,session,index);
        return key ? JS_NewString(context,key) : JS_NULL;
    }
    size_t key_length = 0,value_length = 0;
    const char *key = argc > 2 ? JS_ToCStringLen(context,&key_length,argv[2]) : NULL;
    const char *value = argc > 3 ? JS_ToCStringLen(context,&value_length,argv[3]) : NULL;
    if ((argc > 2 && !key) || (argc > 3 && !value)) {
        if(key)JS_FreeCString(context,key);
        if(value)JS_FreeCString(context,value);
        return JS_EXCEPTION;
    }
    if ((key && memchr(key,0,key_length)) || (value && memchr(value,0,value_length))) {
        if(key)JS_FreeCString(context,key);
        if(value)JS_FreeCString(context,value);
        return JS_ThrowTypeError(context,"Storage strings cannot contain NUL");
    }
    JSValue result = JS_UNDEFINED;
    int status = 0;
    if (operation == 0) {
        const char *stored = key ? browser_storage_get(page->url,session,key) : NULL;
        result = stored ? JS_NewString(context,stored) : JS_NULL;
    } else if (operation == 1) status = browser_storage_set(page->url,session,key,value);
    else if (operation == 2) status = browser_storage_remove(page->url,session,key);
    else if (operation == 3) status = browser_storage_clear(page->url,session);
    else status = -1;
    if(key)JS_FreeCString(context,key);
        if(value)JS_FreeCString(context,value);
    if(status) return status == -2 ? JS_ThrowInternalError(context,"Storage could not be saved") : JS_ThrowRangeError(context,"Storage quota or origin limit");
    return result;
}

static JSValue cookie_call(JSContext *context, JSValueConst self, int argc, JSValueConst *argv) {
    (void)self;
    BrowserPage *page = JS_GetContextOpaque(context);
    if (argc) {
        size_t length = 0;
        const char *value = JS_ToCStringLen(context,&length,argv[0]);
        if(!value)return JS_EXCEPTION;
        if(!memchr(value,0,length))browser_cookie_set(page->url,value);
        JS_FreeCString(context,value);return JS_UNDEFINED;
    }
    char cookies[16384];browser_cookie_get(page->url,cookies,sizeof(cookies));return JS_NewString(context,cookies);
}

uint64_t browser_js_clock_ms(void) {
    struct timespec time;
    clock_gettime(CLOCK_MONOTONIC, &time);
    return (uint64_t)time.tv_sec * 1000u + (uint64_t)time.tv_nsec / 1000000u;
}
#define now_ms browser_js_clock_ms
static JSValue clock_read(JSContext *context, JSValueConst self, int argc, JSValueConst *argv) {
    (void)self; (void)argc; (void)argv;
    BrowserJS *session = JS_GetRuntimeOpaque(JS_GetRuntime(context));
    return JS_NewFloat64(context, (double)session->clock);
}
static void reset_budget(BrowserJS *session, unsigned milliseconds) {
    session->budget.deadline = now_ms() + milliseconds;
    session->budget.checks = 0;
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
    if (length >= capacity) {
        length = capacity - 1;
        while (length && ((unsigned char)text[length] & 0xc0) == 0x80) --length;
    }
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

static int sync_page(BrowserJS *session);
static void drain_jobs(BrowserJS *session) {
    for (unsigned i = 0; i < 64 && now_ms() < session->budget.deadline; ++i) {
        JSContext *context = NULL;
        int result = JS_ExecutePendingJob(session->runtime, &context);
        if (result < 0) { report_exception(context ? context : session->context, session->page); break; }
        if (!result) break;
    }
}
static JSValue call_global(BrowserJS *session, const char *name, int argc, JSValue *args) {
    JSValue fn = JS_GetPropertyStr(session->context, session->global, name);
    JSValue result = JS_Call(session->context, fn, session->global, argc, args);
    JS_FreeValue(session->context, fn);
    if (JS_IsException(result)) report_exception(session->context, session->page);
    return result;
}
BrowserJS *browser_js_start(BrowserPage *page) {
    if (!page) return NULL;
    page->js_ran = 0; page->js_notice[0] = 0;
    if (!page->inline_allowed && !page->external_allowed) {
        if (page->script_count) snprintf(page->js_notice,sizeof(page->js_notice),"Scripts blocked by site CSP");
        return NULL;
    }
    BrowserJS *session = calloc(1,sizeof(*session));
    if (!session) return NULL;
    session->page = page; session->clock = now_ms();
    JSRuntime *runtime = session->runtime = JS_NewRuntime2(&js_allocator,NULL);
    if (!runtime) { free(session); return NULL; }
    JS_SetRuntimeOpaque(runtime,session);
    JS_SetMemoryLimit(runtime,JS_MEMORY_LIMIT);
    JS_SetMaxStackSize(runtime,JS_STACK_LIMIT);
    reset_budget(session,JS_TIME_LIMIT_MS);
    JS_SetInterruptHandler(runtime,interrupted,&session->budget);
    JSContext *context = session->context = JS_NewContext(runtime);
    if (!context) { JS_FreeRuntime(runtime); free(session); return NULL; }
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
    session->global = JS_DupValue(context,global);
    JS_SetPropertyStr(context,global,"__clock",JS_NewCFunction(context,clock_read,"__clock",0));
    JS_SetPropertyStr(context,global,"__startedAt",JS_NewFloat64(context,(double)session->clock));
    JS_SetPropertyStr(context,global,"__sourceHTML",JS_NewString(context,page->html));
    JS_SetPropertyStr(context,global,"__storage",JS_NewCFunction(context,storage_call,"__storage",4));
    JS_SetPropertyStr(context,global,"__cookie",JS_NewCFunction(context,cookie_call,"__cookie",1));
    const char *sources[] = {browser_web_apis,browser_intl,browser_dom};
    int boot_ok = 1;
    for (unsigned i = 0; i < 3; ++i) {
        JSValue result = JS_Eval(context,sources[i],strlen(sources[i]),"<browser-bootstrap>",JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result)) { report_exception(context,page); boot_ok = 0; }
        JS_FreeValue(context,result);
        if (!boot_ok) break;
    }
    JS_FreeValue(context,document); JS_FreeValue(context,global);
    if (!boot_ok) { browser_js_destroy(session); return NULL; }
    for (size_t i = 0; i < page->script_count; ++i) {
        if (page->script_urls[i][0] ? !page->external_allowed : !page->inline_allowed) continue;
        if (!page->scripts[i][0]) continue;
        reset_budget(session,JS_TIME_LIMIT_MS);
        JSValue result = JS_Eval(context,page->scripts[i],strlen(page->scripts[i]),
                                page->script_urls[i][0] ? page->script_urls[i] : "<inline>",JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(result)) report_exception(context,page); else ++page->js_ran;
        JS_FreeValue(context,result);
        drain_jobs(session);
    }
    reset_budget(session,JS_TIME_LIMIT_MS);
    JSValue ready = call_global(session,"__ready",0,NULL); JS_FreeValue(context,ready);
    drain_jobs(session);
    reset_budget(session,30);
    sync_page(session);
    return session;
}
static int sync_page(BrowserJS *session) {
    JSContext *context = session->context;
    BrowserPage *page = session->page;
    JSValue snapshot = call_global(session,"__snapshot",0,NULL);
    if (JS_IsException(snapshot) || JS_IsNull(snapshot)) { JS_FreeValue(context,snapshot); return 0; }
    int changed = 0;
    const char *names[] = {"title","text"};
    char *targets[] = {page->title,page->text};
    size_t sizes[] = {sizeof(page->title),sizeof(page->text)};
    for (unsigned i = 0; i < 2; ++i) {
        JSValue value = JS_GetPropertyStr(context,snapshot,names[i]);
        const char *text = JS_ToCString(context,value);
        if (text && strncmp(targets[i],text,sizes[i]-1)) { copy_js_string(context,value,targets[i],sizes[i]); changed = 1; }
        if (text) JS_FreeCString(context,text);
        JS_FreeValue(context,value);
    }
    JSValue links = JS_GetPropertyStr(context,snapshot,"links");
    JSValue length = JS_GetPropertyStr(context,links,"length");
    uint32_t count = 0; JS_ToUint32(context,&count,length); JS_FreeValue(context,length);
    if (count > BROWSER_LINK_MAX) count = BROWSER_LINK_MAX;
    if (page->link_count != count) changed = 1;
    page->link_count = count;
    for (uint32_t i = 0; i < count; ++i) {
        JSValue link = JS_GetPropertyUint32(context,links,i);
        JSValue href = JS_GetPropertyStr(context,link,"href");
        char candidate[BROWSER_URL_MAX] = {0}; copy_js_string(context,href,candidate,sizeof(candidate));
        if (!browser_valid_url(candidate)) candidate[0] = 0;
        if (strcmp(candidate,page->links[i].url)) changed = 1;
        snprintf(page->links[i].url,sizeof(page->links[i].url),"%s",candidate);
        JSValue label = JS_GetPropertyStr(context,link,"label");
        char label_text[96] = {0}; copy_js_string(context,label,label_text,sizeof(label_text));
        if (strcmp(label_text,page->links[i].label)) changed = 1;
        snprintf(page->links[i].label,sizeof(page->links[i].label),"%s",label_text);
        JS_FreeValue(context,label); JS_FreeValue(context,href); JS_FreeValue(context,link);
    }
    JS_FreeValue(context,links); JS_FreeValue(context,snapshot);
    return changed;
}
int browser_js_tick(BrowserJS *session, uint64_t clock) {
    if (!session) return 0;
    if (clock > session->clock) session->clock = clock;
    reset_budget(session,8);
    JSValue arg = JS_NewFloat64(session->context,(double)session->clock);
    JSValue result = call_global(session,"__pump",1,&arg);
    if (JS_IsException(result)) {
        reset_budget(session,8);
        JSValue canceled = call_global(session,"__cancelAll",0,NULL);
        JS_FreeValue(session->context,canceled);
    }
    JS_FreeValue(session->context,result); JS_FreeValue(session->context,arg);
    drain_jobs(session);
    // Snapshot has its own small budget; a timed-out callback cannot freeze rendering.
    reset_budget(session,8);
    return sync_page(session);
}
int browser_js_click(BrowserJS *session, size_t index, char target[BROWSER_URL_MAX]) {
    if (!session) return 1;
    reset_budget(session,30);
    JSValue arg = JS_NewUint32(session->context,(uint32_t)index);
    JSValue result = call_global(session,"__click",1,&arg);
    int follow = 0;
    if (!JS_IsException(result)) {
        JSValue allowed = JS_GetPropertyStr(session->context,result,"follow");
        JSValue href = JS_GetPropertyStr(session->context,result,"href");
        if (target) { target[0] = 0; copy_js_string(session->context,href,target,BROWSER_URL_MAX); }
        follow = JS_ToBool(session->context,allowed) > 0 && target && browser_valid_url(target);
        JS_FreeValue(session->context,allowed); JS_FreeValue(session->context,href);
    }
    JS_FreeValue(session->context,result); JS_FreeValue(session->context,arg);
    drain_jobs(session); reset_budget(session,8); sync_page(session);
    return follow;
}
void browser_js_destroy(BrowserJS *session) {
    if (!session) return;
    JS_FreeValue(session->context,session->global);
    JS_FreeContext(session->context); JS_FreeRuntime(session->runtime); free(session);
}
void browser_js_apply(BrowserPage *page) {
    BrowserJS *session = browser_js_start(page);
    browser_js_destroy(session);
}
