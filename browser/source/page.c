#include "page.h"
#include "profile.h"
#include <curl/curl.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define HTML_LIMIT (2u * 1024u * 1024u)

typedef struct { char *data; size_t size; int overflow; } Body;

int browser_valid_url(const char *url) {
    if (!url || strncmp(url, "https://", 8) != 0 || !url[8]) return 0;
    size_t n = strlen(url);
    if (n >= BROWSER_URL_MAX || url[8] == '/') return 0;
    for (size_t i = 8; i < n; ++i) {
        unsigned char c = (unsigned char)url[i];
        if (c <= 0x20 || c == 0x7f || c == '\\' || c == '"' || c == '<' || c == '>') return 0;
    }
    CURLU *parsed = curl_url();
    char *host = NULL, *user = NULL, *password = NULL;
    int valid = parsed && curl_url_set(parsed, CURLUPART_URL, url, 0) == CURLUE_OK &&
        curl_url_get(parsed, CURLUPART_HOST, &host, 0) == CURLUE_OK && *host;
    if (valid && (curl_url_get(parsed,CURLUPART_USER,&user,0)==CURLUE_OK ||
                  curl_url_get(parsed,CURLUPART_PASSWORD,&password,0)==CURLUE_OK)) valid=0;
    curl_free(host);curl_free(user);curl_free(password);curl_url_cleanup(parsed);
    return valid;
}
static void network_error(BrowserPage *page,CURLcode code,int overflow) {
    const char *hint = "";
    if (code==CURLE_PEER_FAILED_VERIFICATION) hint=" Check console date and site certificate.";
    else if (code==CURLE_SSL_CACERT_BADFILE) hint=" CA bundle is missing or could not be imported.";
    else if (code==CURLE_COULDNT_RESOLVE_HOST) hint=" Check DNS and Wi-Fi.";
    else if (code==CURLE_OPERATION_TIMEDOUT) hint=" Request timed out; retry on a stable connection.";
    snprintf(page->error,sizeof(page->error),"%s%s",overflow?"Page exceeds 2 MiB limit":curl_easy_strerror(code),hint);
    if(code==CURLE_SSL_CACERT_BADFILE)snprintf(page->error,sizeof(page->error),"CA failure: %.140s",browser_profile_status());
}

static size_t receive_body(char *data, size_t unit, size_t count, void *context) {
    Body *body = (Body *)context;
    if (unit && count > SIZE_MAX / unit) return 0;
    size_t bytes = unit * count;
    if (bytes > HTML_LIMIT - body->size) { body->overflow = 1; return 0; }
    memcpy(body->data + body->size, data, bytes);
    body->size += bytes;
    body->data[body->size] = 0;
    return bytes;
}

typedef struct { char *data; size_t size; int overflow; } ScriptBody;

static size_t receive_script(char *data, size_t unit, size_t count, void *context) {
    ScriptBody *body = (ScriptBody *)context;
    if (unit && count > SIZE_MAX / unit) return 0;
    size_t bytes = unit * count;
    if (bytes >= BROWSER_SCRIPT_BYTES - body->size) { body->overflow = 1; return 0; }
    memcpy(body->data + body->size, data, bytes);
    body->size += bytes;
    body->data[body->size] = 0;
    return bytes;
}

static int same_origin(const char *base_url, const char *candidate_url) {
    CURLU *base = curl_url(), *candidate = curl_url();
    char *base_host = NULL, *host = NULL, *base_port = NULL, *port = NULL;
    char *user = NULL, *password = NULL;
    int result = 0;
    if (!base || !candidate || !browser_valid_url(candidate_url) ||
        curl_url_set(base, CURLUPART_URL, base_url, 0) != CURLUE_OK ||
        curl_url_set(candidate, CURLUPART_URL, candidate_url, 0) != CURLUE_OK ||
        curl_url_get(base, CURLUPART_HOST, &base_host, 0) != CURLUE_OK ||
        curl_url_get(candidate, CURLUPART_HOST, &host, 0) != CURLUE_OK ||
        curl_url_get(base, CURLUPART_PORT, &base_port, CURLU_DEFAULT_PORT) != CURLUE_OK ||
        curl_url_get(candidate, CURLUPART_PORT, &port, CURLU_DEFAULT_PORT) != CURLUE_OK) goto done;
    if (curl_url_get(candidate, CURLUPART_USER, &user, 0) == CURLUE_OK ||
        curl_url_get(candidate, CURLUPART_PASSWORD, &password, 0) == CURLUE_OK) goto done;
    result = strcasecmp(base_host, host) == 0 && strcmp(base_port, port) == 0;
done:
    curl_free(base_host); curl_free(host); curl_free(base_port); curl_free(port);
    curl_free(user); curl_free(password);
    curl_url_cleanup(base); curl_url_cleanup(candidate);
    return result;
}

static void fetch_scripts(BrowserPage *page) {
    if (!page->external_allowed) return;
    for (size_t i = 0; i < page->script_count; ++i) {
        if (!page->script_urls[i][0]) continue;
        CURLU *url = curl_url();
        char *resolved = NULL;
        int valid = url && curl_url_set(url, CURLUPART_URL, page->url, 0) == CURLUE_OK &&
                    curl_url_set(url, CURLUPART_URL, page->script_urls[i], 0) == CURLUE_OK &&
                    curl_url_get(url, CURLUPART_URL, &resolved, 0) == CURLUE_OK &&
                    same_origin(page->url, resolved);
        if (!valid) { ++page->script_fetch_failed; curl_free(resolved); curl_url_cleanup(url); continue; }
        CURL *curl = curl_easy_init();
        ScriptBody body = {page->scripts[i], 0, 0};
        long status = 0;
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_URL, resolved);
#if LIBCURL_VERSION_NUM >= 0x075500
            curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
#else
            curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
#endif
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
            curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
            CURLcode setup = browser_profile_configure(curl);
            curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_script);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
            CURLcode result = setup == CURLE_OK ? curl_easy_perform(curl) : setup;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
            browser_profile_log(resolved,"script",status,result);
            if (result != CURLE_OK || body.overflow || status != 200 || !body.size) {
                page->scripts[i][0] = 0;
                ++page->script_fetch_failed;
            }
            curl_easy_cleanup(curl);
        } else ++page->script_fetch_failed;
        curl_free(resolved);
        curl_url_cleanup(url);
    }
}

static int csp_sources(const char *csp, const char *directive) {
    const char *section = csp;
    while (*section) {
        const char *end = strchr(section, ';');
        if (!end) end = section + strlen(section);
        const char *p = section;
        while (p < end && isspace((unsigned char)*p)) ++p;
        const char *name = p;
        while (p < end && !isspace((unsigned char)*p)) ++p;
        if ((size_t)(p - name) == strlen(directive) &&
            strncasecmp(name, directive, (size_t)(p - name)) == 0) {
            int permissions = 0;
            while (p < end) {
                while (p < end && isspace((unsigned char)*p)) ++p;
                const char *token = p;
                while (p < end && !isspace((unsigned char)*p)) ++p;
                if ((size_t)(p - token) == 6 && strncasecmp(token, "'self'", 6) == 0)
                    permissions |= 2;
                if ((size_t)(p - token) == 15 && strncasecmp(token, "'unsafe-inline'", 15) == 0)
                    permissions |= 1;
                if (p - token == 1 && *token == '*') permissions |= 2;
            }
            return permissions;
        }
        section = *end ? end + 1 : end;
    }
    return -1;
}

static void set_script_permissions(BrowserPage *page) {
    int permissions = csp_sources(page->csp, "script-src-elem");
    if (permissions < 0) permissions = csp_sources(page->csp, "script-src");
    if (permissions < 0) permissions = csp_sources(page->csp, "default-src");
    if (permissions < 0) permissions = 3;
    page->inline_allowed = (permissions & 1) != 0;
    page->external_allowed = (permissions & 2) != 0;
}

static int same(const char *s, size_t n, const char *word) {
    size_t expected = strlen(word);
    if (n != expected) return 0;
    for (size_t i = 0; i < n; ++i)
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)word[i])) return 0;
    return 1;
}

static int prefix(const char *s, size_t available, const char *word) {
    size_t n = strlen(word);
    return available >= n && same(s, n, word);
}

static size_t receive_header(char *data, size_t unit, size_t count, void *context) {
    BrowserPage *page = (BrowserPage *)context;
    if (unit && count > SIZE_MAX / unit) return 0;
    size_t size = unit * count;
    if (prefix(data, size, "HTTP/")) page->csp[0] = 0; // final redirect wins
    const char *name = "Content-Security-Policy:";
    size_t name_size = strlen(name);
    if (prefix(data, size, name)) {
        size_t start = name_size;
        while (start < size && (data[start] == ' ' || data[start] == '\t')) ++start;
        size_t end = size;
        while (end > start && (data[end - 1] == '\r' || data[end - 1] == '\n' || data[end - 1] == ' ')) --end;
        size_t copy = end - start;
        if (copy >= sizeof(page->csp)) copy = sizeof(page->csp) - 1;
        memcpy(page->csp, data + start, copy);
        page->csp[copy] = 0;
    }
    return size;
}

static void append_char(char *buffer, size_t capacity, size_t *used, char c) {
    if (*used + 1 >= capacity) return;
    buffer[(*used)++] = c;
    buffer[*used] = 0;
}

static void append_space(char *buffer, size_t capacity, size_t *used, char c) {
    if (!*used) return;
    if (buffer[*used - 1] == ' ' || buffer[*used - 1] == '\n') return;
    append_char(buffer, capacity, used, c);
}

static size_t entity(const char *input, size_t remaining, char *out) {
    const struct { const char *name; char value; } known[] = {
        {"amp;", '&'}, {"lt;", '<'}, {"gt;", '>'}, {"quot;", '"'},
        {"apos;", '\''}, {"nbsp;", ' '}
    };
    for (size_t i = 0; i < sizeof(known)/sizeof(known[0]); ++i) {
        size_t len = strlen(known[i].name);
        if (remaining >= len && memcmp(input, known[i].name, len) == 0) {
            *out = known[i].value;
            return len;
        }
    }
    return 0;
}

static void add_link(BrowserPage *page, const char *href, size_t length) {
    if (!length || length >= BROWSER_URL_MAX || page->link_count >= BROWSER_LINK_MAX) return;
    char candidate[BROWSER_URL_MAX];
    memcpy(candidate, href, length);
    candidate[length] = 0;
    CURLU *url = curl_url();
    if (!url) return;
    if (curl_url_set(url, CURLUPART_URL, page->url, 0) == CURLUE_OK &&
        curl_url_set(url, CURLUPART_URL, candidate, 0) == CURLUE_OK) {
        char *resolved = NULL;
        if (curl_url_get(url, CURLUPART_URL, &resolved, 0) == CURLUE_OK) {
            if (browser_valid_url(resolved)) {
                BrowserLink *link = &page->links[page->link_count++];
                snprintf(link->url, sizeof(link->url), "%s", resolved);
                snprintf(link->href, sizeof(link->href), "%s", candidate);
                snprintf(link->label, sizeof(link->label), "%.95s", resolved);
            }
            curl_free(resolved);
        }
    }
    curl_url_cleanup(url);
}

static void parse_href(BrowserPage *page, const char *tag, size_t length) {
    size_t i = 0;
    while (i < length) {
        while (i < length && (isspace((unsigned char)tag[i]) || tag[i] == '/')) ++i;
        size_t start = i;
        while (i < length && (isalnum((unsigned char)tag[i]) || tag[i] == '-')) ++i;
        size_t name_end = i;
        if (start == name_end) { ++i; continue; }
        while (i < length && isspace((unsigned char)tag[i])) ++i;
        if (i >= length || tag[i] != '=') continue;
        ++i;
        while (i < length && isspace((unsigned char)tag[i])) ++i;
        char quote = 0;
        if (i < length && (tag[i] == '"' || tag[i] == '\'')) quote = tag[i++];
        size_t value_start = i;
        while (i < length && (quote ? tag[i] != quote : !isspace((unsigned char)tag[i]) && tag[i] != '>')) ++i;
        if (same(tag + start, name_end - start, "href")) add_link(page, tag + value_start, i - value_start);
        if (quote && i < length) ++i;
    }
}

static int attribute(const char *tag, size_t length, const char *wanted,
                     char *output, size_t capacity) {
    size_t i = 0;
    while (i < length) {
        while (i < length && (isspace((unsigned char)tag[i]) || tag[i] == '/')) ++i;
        size_t start = i;
        while (i < length && (isalnum((unsigned char)tag[i]) || tag[i] == '-')) ++i;
        size_t name_end = i;
        if (start == name_end) { ++i; continue; }
        while (i < length && isspace((unsigned char)tag[i])) ++i;
        if (i >= length || tag[i] != '=') {
            if (same(tag + start, name_end - start, wanted)) { output[0] = 0; return 1; }
            continue;
        }
        ++i;
        while (i < length && isspace((unsigned char)tag[i])) ++i;
        char quote = 0;
        if (i < length && (tag[i] == '"' || tag[i] == '\'')) quote = tag[i++];
        size_t value = i;
        while (i < length && (quote ? tag[i] != quote : !isspace((unsigned char)tag[i]))) ++i;
        if (same(tag + start, name_end - start, wanted)) {
            size_t copy = i - value;
            if (copy >= capacity) copy = capacity - 1;
            memcpy(output, tag + value, copy);
            output[copy] = 0;
            return 1;
        }
        if (quote && i < length) ++i;
    }
    return 0;
}

static void extract_scripts(BrowserPage *page, const char *html, size_t size) {
    for (size_t i = 0; i + 7 < size && page->script_count < BROWSER_SCRIPT_MAX;) {
        if (html[i] != '<' || !prefix(html + i + 1, size - i - 1, "script") ||
            !(isspace((unsigned char)html[i + 7]) || html[i + 7] == '>')) { ++i; continue; }
        size_t tag_end = i + 7;
        char quote = 0;
        while (tag_end < size) {
            char c = html[tag_end];
            if (quote) { if (c == quote) quote = 0; }
            else if (c == '"' || c == '\'') quote = c;
            else if (c == '>') break;
            ++tag_end;
        }
        if (tag_end == size) break;
        size_t close = tag_end + 1;
        while (close + 8 < size &&
               !(html[close] == '<' && prefix(html + close + 1, size - close - 1, "/script"))) ++close;
        if (close + 8 >= size) break;
        char type[80], src[BROWSER_URL_MAX];
        int has_type = attribute(html + i + 7, tag_end - i - 7, "type", type, sizeof(type));
        int has_src = attribute(html + i + 7, tag_end - i - 7, "src", src, sizeof(src));
        size_t length = close - tag_end - 1;
        if ((!has_type || same(type, strlen(type), "text/javascript") ||
                          same(type, strlen(type), "application/javascript")) &&
            ((has_src && src[0]) || (!has_src && length > 0 && length < BROWSER_SCRIPT_BYTES))) {
            size_t slot = page->script_count++;
            if (has_src) snprintf(page->script_urls[slot], sizeof(page->script_urls[slot]), "%s", src);
            else {
                memcpy(page->scripts[slot], html + tag_end + 1, length);
                page->scripts[slot][length] = 0;
            }
        }
        i = close + 8;
    }
}

int browser_parse_html(BrowserPage *page, const char *html, size_t size) {
    if (!page || !html) return 0;
    size_t copy = size < sizeof(page->html) - 1 ? size : sizeof(page->html) - 1;
    while (copy && copy < size && ((unsigned char)html[copy] & 0xc0) == 0x80) --copy;
    memcpy(page->html, html, copy);
    page->html[copy] = 0;
    page->dom_truncated = copy < size;
    page->text[0] = 0;
    page->title[0] = 0;
    snprintf(page->language, sizeof(page->language), "en");
    page->link_count = 0;
    page->script_count = 0;
    page->script_fetch_failed = 0;
    memset(page->script_urls, 0, sizeof(page->script_urls));
    memset(page->scripts, 0, sizeof(page->scripts));
    set_script_permissions(page);
    extract_scripts(page, html, size);
    size_t text_used = 0, title_used = 0;
    int in_title = 0, skip = 0;
    for (size_t i = 0; i < size;) {
        if (html[i] == '<') {
            size_t end = i + 1;
            char quote = 0;
            while (end < size) {
                char c = html[end];
                if (quote) { if (c == quote) quote = 0; }
                else if (c == '"' || c == '\'') quote = c;
                else if (c == '>') break;
                ++end;
            }
            if (end == size) break;
            size_t p = i + 1;
            int closing = p < end && html[p] == '/';
            if (closing) ++p;
            size_t name = p;
            while (p < end && isalpha((unsigned char)html[p])) ++p;
            size_t len = p - name;
            if (same(html + name, len, "script")) skip = closing ? 0 : 1;
            else if (same(html + name, len, "style")) skip = closing ? 0 : 2;
            else if (same(html + name, len, "title")) in_title = !closing;
            else if (!closing && same(html + name, len, "html"))
                attribute(html + p, end - p, "lang", page->language, sizeof(page->language));
            else if (!skip && !closing && same(html + name, len, "a")) parse_href(page, html + p, end - p);
            if (!skip && (same(html + name, len, "p") || same(html + name, len, "br") ||
                          same(html + name, len, "div") || same(html + name, len, "li") ||
                          same(html + name, len, "h1") || same(html + name, len, "h2")))
                append_space(page->text, sizeof(page->text), &text_used, '\n');
            i = end + 1;
            continue;
        }
        if (skip) { ++i; continue; }
        char c = html[i];
        if (c == '&') {
            char decoded;
            size_t consumed = entity(html + i + 1, size - i - 1, &decoded);
            if (consumed) { c = decoded; i += consumed; }
        }
        if (in_title) {
            if (isspace((unsigned char)c)) append_space(page->title, sizeof(page->title), &title_used, ' ');
            else append_char(page->title, sizeof(page->title), &title_used, c);
        } else if (isspace((unsigned char)c)) {
            append_space(page->text, sizeof(page->text), &text_used, ' ');
        } else append_char(page->text, sizeof(page->text), &text_used, c);
        ++i;
    }
    return 1;
}

int browser_fetch(BrowserPage *page, const char *url) {
    if (!page) return 0;
    if (!browser_valid_url(url)) { memset(page,0,sizeof(*page)); snprintf(page->error,sizeof(page->error),"Invalid HTTPS URL (credentials are not accepted)"); return 0; }
    memset(page, 0, sizeof(*page));
    Body body = {0};
    body.data = malloc(HTML_LIMIT + 1);
    CURL *curl = curl_easy_init();
    if (!body.data || !curl) {
        snprintf(page->error, sizeof(page->error), "Out of memory or curl initialization failed");
        free(body.data);
        if (curl) curl_easy_cleanup(curl);
        return 0;
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 25L);
    CURLcode setup = browser_profile_configure(curl);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "SwitchBrowser/0.3 (libnx; independent prototype)");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, receive_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, page);
    CURLcode result = setup == CURLE_OK ? curl_easy_perform(curl) : setup;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &page->http_status);
    char *effective = NULL;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective);
    if (effective && browser_valid_url(effective)) snprintf(page->url, sizeof(page->url), "%s", effective);
    else snprintf(page->url, sizeof(page->url), "%s", url);
    if (result != CURLE_OK || body.overflow) {
        network_error(page,result,body.overflow);
    } else if (page->http_status >= 400) {
        snprintf(page->error, sizeof(page->error), "HTTP %ld", page->http_status);
    } else {
        browser_parse_html(page, body.data, body.size);
        fetch_scripts(page);
    }
    browser_profile_log(page->url,"page",page->http_status,result);
    curl_easy_cleanup(curl);
    browser_profile_save_cookies();
    free(body.data);
    return page->error[0] == 0;
}
