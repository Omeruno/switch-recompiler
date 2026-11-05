#include "profile.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#ifdef __SWITCH__
#include <switch.h>
#endif

#define RECORDS 256
#define ORIGIN_MAX 384
#define KEY_MAX 1024
#define VALUE_MAX 16384
#define ORIGIN_QUOTA 65536
#define TOTAL_QUOTA (1024u * 1024u)
typedef struct {
    char *origin, *key, *value;
    int session;
} Entry;
static Entry entries[RECORDS];
static size_t entry_count;
static CURLSH *shared;
static CURL *cookies;
static char directory[768], ca_path[768], status_text[160] = "Memory-only profile";
static int persistent;
static char *ca_data;

static void release(Entry *e) {
    free(e->origin);
    free(e->key);
    free(e->value);
    memset(e, 0, sizeof(*e));
}
int browser_origin(const char *url, char *out, size_t capacity) {
    CURLU *u = curl_url();
    char *scheme = NULL, *host = NULL, *port = NULL;
    int ok = 0;
    if (!url || !u || curl_url_set(u, CURLUPART_URL, url, 0) != CURLUE_OK ||
        curl_url_get(u, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK || strcmp(scheme, "https") ||
        curl_url_get(u, CURLUPART_HOST, &host, 0) != CURLUE_OK ||
        curl_url_get(u, CURLUPART_PORT, &port, CURLU_DEFAULT_PORT) != CURLUE_OK)
        goto done;
    // libcurl normalizes scheme but DNS hostnames are case insensitive.
    for (char *p = host; *p; p++)
        if (*p >= 'A' && *p <= 'Z')
            *p += (char)('a' - 'A');
    int n = snprintf(out, capacity, "https://%s:%s", host, port);
    ok = n > 0 && (size_t)n < capacity;
done:
    curl_free(scheme);
    curl_free(host);
    curl_free(port);
    curl_url_cleanup(u);
    return ok;
}
static int path(char *out, size_t capacity, const char *name) {
    int n = snprintf(out, capacity, "%s/%s", directory, name);
    return n > 0 && (size_t)n < capacity;
}
// Horizon rename does not replace an existing destination. Keep a previous
// complete snapshot so interruption between the two renames is recoverable.
static int replace_file(const char *source, const char *dest) {
    char backup[1100];
    snprintf(backup, sizeof(backup), "%s.bak", dest);
    struct stat st;
    if (!stat(dest, &st)) {
        if (remove(backup) && errno != ENOENT)
            return 0;
        if (rename(dest, backup))
            return 0;
        if (rename(source, dest)) {
            rename(backup, dest);
            return 0;
        }
        chmod(backup, 0600);
    } else if (rename(source, dest))
        return 0;
    chmod(dest, 0600);
    return 1;
}
static int write32(FILE *f, uint32_t value) {
    unsigned char b[4] = {(unsigned char)value, (unsigned char)(value >> 8),
                          (unsigned char)(value >> 16), (unsigned char)(value >> 24)};
    return fwrite(b, 1, 4, f) == 4;
}
static int read32(FILE *f, uint32_t *value) {
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4)
        return 0;
    *value =
        (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 1;
}
static int save_storage(void) {
    if (!persistent)
        return 1;
    char dest[1024], temp[1024];
    if (!path(dest, sizeof(dest), "storage.bin") || !path(temp, sizeof(temp), "storage.tmp"))
        return 0;
    FILE *f = fopen(temp, "wb");
    if (!f) {
        snprintf(status_text, sizeof(status_text), "Profile write failed; storage change rejected");
        return 0;
    }
    int ok = fwrite("SBST1\n", 1, 6, f) == 6;
    for (size_t i = 0; i < entry_count && ok; i++) {
        Entry *e = &entries[i];
        if (e->session)
            continue;
        uint32_t a = (uint32_t)strlen(e->origin), b = (uint32_t)strlen(e->key),
                 c = (uint32_t)strlen(e->value);
        ok = write32(f, a) && write32(f, b) && write32(f, c) && fwrite(e->origin, 1, a, f) == a &&
             fwrite(e->key, 1, b, f) == b && fwrite(e->value, 1, c, f) == c;
    }
    if (fflush(f))
        ok = 0;
    if (fclose(f))
        ok = 0;
    if (ok && replace_file(temp, dest)) {
        chmod(dest, 0600);
        return 1;
    }
    remove(temp);
    snprintf(status_text, sizeof(status_text), "Profile write failed; storage change rejected");
    return 0;
}
static size_t find(const char *origin, int session, const char *key) {
    for (size_t i = 0; i < entry_count; i++)
        if (entries[i].session == session && !strcmp(entries[i].origin, origin) &&
            !strcmp(entries[i].key, key))
            return i;
    return entry_count;
}
static int fits(const char *origin, int session, size_t replacing, size_t key, size_t value) {
    size_t total = key + value, own = key + value;
    for (size_t i = 0; i < entry_count; i++)
        if (i != replacing) {
            size_t n = strlen(entries[i].key) + strlen(entries[i].value);
            total += n + strlen(entries[i].origin);
            if (entries[i].session == session && !strcmp(entries[i].origin, origin))
                own += n;
        }
    return own <= ORIGIN_QUOTA && total + strlen(origin) <= TOTAL_QUOTA;
}
const char *browser_storage_get(const char *url, int session, const char *key) {
    char origin[ORIGIN_MAX];
    if (!key || !browser_origin(url, origin, sizeof(origin)))
        return NULL;
    size_t i = find(origin, !!session, key);
    return i < entry_count ? entries[i].value : NULL;
}
int browser_storage_set(const char *url, int session, const char *key, const char *value) {
    char origin[ORIGIN_MAX];
    session = !!session;
    if (!key || !value || strlen(key) > KEY_MAX || strlen(value) > VALUE_MAX ||
        !browser_origin(url, origin, sizeof(origin)))
        return -1;
    size_t i = find(origin, session, key);
    if ((i == entry_count && entry_count == RECORDS) ||
        !fits(origin, session, i, strlen(key), strlen(value)))
        return -1;
    Entry replacement = {strdup(origin), strdup(key), strdup(value), session};
    if (!replacement.origin || !replacement.key || !replacement.value) {
        release(&replacement);
        return -1;
    }
    int added = i == entry_count;
    Entry old = entries[i];
    entries[i] = replacement;
    if (added)
        ++entry_count;
    if (!session && !save_storage()) {
        entries[i] = old;
        if (added)
            --entry_count;
        release(&replacement);
        return -2;
    }
    release(&old);
    return 0;
}
int browser_storage_remove(const char *url, int session, const char *key) {
    char origin[ORIGIN_MAX];
    if (!key || !browser_origin(url, origin, sizeof(origin)))
        return -1;
    size_t i = find(origin, !!session, key);
    if (i == entry_count)
        return 0;
    Entry old = entries[i];
    entries[i] = entries[--entry_count];
    memset(&entries[entry_count], 0, sizeof(Entry));
    if (!session && !save_storage()) {
        entries[entry_count++] = entries[i];
        entries[i] = old;
        return -2;
    }
    release(&old);
    return 0;
}
int browser_storage_clear(const char *url, int session) {
    char origin[ORIGIN_MAX];
    if (!browser_origin(url, origin, sizeof(origin)))
        return -1;
    // Save once and roll back the full transaction on an I/O error.
    Entry old[RECORDS];
    size_t old_count = entry_count;
    memcpy(old, entries, sizeof(old));
    size_t used = 0;
    for (size_t i = 0; i < old_count; i++)
        if (old[i].session != !!session || strcmp(old[i].origin, origin))
            entries[used++] = old[i];
    entry_count = used;
    memset(entries + used, 0, (RECORDS - used) * sizeof(Entry));
    if (!session && !save_storage()) {
        memcpy(entries, old, sizeof(old));
        entry_count = old_count;
        return -2;
    }
    for (size_t i = 0; i < old_count; i++)
        if (old[i].session == !!session && !strcmp(old[i].origin, origin))
            release(&old[i]);
    return 0;
}
size_t browser_storage_length(const char *url, int session) {
    char origin[ORIGIN_MAX];
    size_t n = 0;
    if (browser_origin(url, origin, sizeof(origin)))
        for (size_t i = 0; i < entry_count; i++)
            if (entries[i].session == !!session && !strcmp(entries[i].origin, origin))
                ++n;
    return n;
}
const char *browser_storage_key(const char *url, int session, size_t index) {
    char origin[ORIGIN_MAX];
    if (!browser_origin(url, origin, sizeof(origin)))
        return NULL;
    for (size_t i = 0; i < entry_count; i++)
        if (entries[i].session == !!session && !strcmp(entries[i].origin, origin)) {
            if (!index--)
                return entries[i].key;
        }
    return NULL;
}
static char *read_string(FILE *f, uint32_t size) {
    char *s = malloc((size_t)size + 1);
    if (!s)
        return NULL;
    if (fread(s, 1, size, f) != size || memchr(s, 0, size)) {
        free(s);
        return NULL;
    }
    s[size] = 0;
    return s;
}
static int load_storage_file(const char *name) {
    FILE *f = fopen(name, "rb");
    if (!f)
        return errno == ENOENT ? -1 : 0;
    char magic[6];
    int ok = fread(magic, 1, 6, f) == 6 && !memcmp(magic, "SBST1\n", 6);
    size_t total = 0;
    while (ok) {
        int next = fgetc(f);
        if (next == EOF) {
            if (ferror(f))
                ok = 0;
            break;
        }
        ungetc(next, f);
        uint32_t a, b, c;
        if (entry_count == RECORDS || !read32(f, &a) || !read32(f, &b) || !read32(f, &c) || !a ||
            a >= ORIGIN_MAX || b > KEY_MAX || c > VALUE_MAX || (total += a + b + c) > TOTAL_QUOTA) {
            ok = 0;
            break;
        }
        Entry e = {read_string(f, a), NULL, NULL, 0};
        if (e.origin)
            e.key = read_string(f, b);
        if (e.key)
            e.value = read_string(f, c);
        char canonical[ORIGIN_MAX];
        if (!e.origin || !e.key || !e.value ||
            !browser_origin(e.origin, canonical, sizeof(canonical)) ||
            strcmp(e.origin, canonical) || find(e.origin, 0, e.key) != entry_count ||
            !fits(e.origin, 0, entry_count, b, c)) {
            release(&e);
            ok = 0;
            break;
        }
        entries[entry_count++] = e;
    }
    fclose(f);
    if (!ok) {
        for (size_t i = 0; i < entry_count; i++)
            release(&entries[i]);
        entry_count = 0;
    }
    return ok;
}

static int load_storage(void) {
    char primary[1024], backup[1100];
    path(primary, sizeof(primary), "storage.bin");
    snprintf(backup, sizeof(backup), "%s.bak", primary);
    int result = load_storage_file(primary);
    if (result == 1)
        return 1;
    int recovered = load_storage_file(backup);
    if (recovered == 1) {
        if ((remove(primary) && errno != ENOENT) || rename(backup, primary)) {
            persistent = 0;
            snprintf(status_text, sizeof(status_text),
                     "Backup recovered in memory; profile repair failed");
        } else
            snprintf(status_text, sizeof(status_text), "Recovered previous storage snapshot");
        return 1;
    }
    return result == -1 && recovered == -1;
}

#ifdef __SWITCH__
static CURLcode import_roots(CURL *curl, void *context, void *opaque) {
    (void)curl;
    (void)opaque;
    if (!ca_data)
        return CURLE_SSL_CACERT_BADFILE;
    const char *p = ca_data;
    size_t count = 0;
    while ((p = strstr(p, "-----BEGIN CERTIFICATE-----"))) {
        const char *end = strstr(p, "-----END CERTIFICATE-----");
        if (!end)
            return CURLE_SSL_CACERT_BADFILE;
        end += strlen("-----END CERTIFICATE-----");
        size_t size = (size_t)(end - p);
        char *pem = malloc(size + 1);
        if (!pem)
            return CURLE_OUT_OF_MEMORY;
        memcpy(pem, p, size);
        pem[size] = 0;
        Result result = sslContextImportServerPki((SslContext *)context, pem, size,
                                                  SslCertificateFormat_Pem, NULL);
        free(pem);
        if (R_FAILED(result)) {
            snprintf(status_text, sizeof(status_text), "CA import failed: libnx 0x%08X", result);
            return CURLE_SSL_CACERT_BADFILE;
        }
        ++count;
        p = end;
    }
    return count ? CURLE_OK : CURLE_SSL_CACERT_BADFILE;
}
#endif
static int ensure_cookies(void) {
    if (shared && cookies)
        return 1;
    shared = curl_share_init();
    cookies = curl_easy_init();
    if (!shared || !cookies ||
        curl_share_setopt(shared, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE) != CURLSHE_OK) {
        if (cookies)
            curl_easy_cleanup(cookies);
        if (shared)
            curl_share_cleanup(shared);
        cookies = NULL;
        shared = NULL;
        return 0;
    }
    // Reuse connections and TLS sessions where the linked backend supports it.
    curl_share_setopt(shared, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    curl_share_setopt(shared, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
    curl_share_setopt(shared, CURLSHOPT_SHARE, CURL_LOCK_DATA_CONNECT);
    curl_easy_setopt(cookies, CURLOPT_SHARE, shared);
    curl_easy_setopt(cookies, CURLOPT_COOKIEFILE, "");
    return 1;
}
int browser_profile_open(const char *dir, const char *ca_file) {
    browser_profile_close();
    if (ca_file && strlen(ca_file) >= sizeof(ca_path))
        return 0;
    if (ca_file)
        snprintf(ca_path, sizeof(ca_path), "%s", ca_file);
    if (ca_path[0]) {
        FILE *f = fopen(ca_path, "rb");
        if (!f) {
            snprintf(status_text, sizeof(status_text), "CA bundle missing");
            return 0;
        }
        if (fseek(f, 0, SEEK_END) || ftell(f) < 0 || ftell(f) > 512 * 1024) {
            fclose(f);
            return 0;
        }
        long size = ftell(f);
        rewind(f);
        ca_data = calloc(1, (size_t)size + 1);
        int ok = ca_data && fread(ca_data, 1, (size_t)size, f) == (size_t)size;
        fclose(f);
        if (!ok || !strstr(ca_data, "-----BEGIN CERTIFICATE-----")) {
            snprintf(status_text, sizeof(status_text), "CA bundle invalid");
            return 0;
        }
    }
    if (!ensure_cookies()) {
        snprintf(status_text, sizeof(status_text), "Cookie engine unavailable");
        return 0;
    }
    if (dir && strlen(dir) < sizeof(directory)) {
        snprintf(directory, sizeof(directory), "%s", dir);
        mkdir(directory, 0700);
        char name[1024];
        path(name, sizeof(name), "write-test.tmp");
        FILE *f = fopen(name, "wb");
        if (f) {
            fclose(f);
            remove(name);
            persistent = 1;
        }
    }
    snprintf(status_text, sizeof(status_text), "%s",
             persistent ? "Profile saved on SD" : "Memory-only profile; storage unavailable");
    if (persistent) {
        if (!load_storage())
            snprintf(status_text, sizeof(status_text),
                     "Corrupt storage ignored; new storage will replace it");
        char name[1024];
        path(name, sizeof(name), "cookies.txt");
        curl_easy_setopt(cookies, CURLOPT_COOKIESESSION, 1L);
        struct stat cookie_stat;
        if (stat(name, &cookie_stat)) {
            char backup[1100];
            snprintf(backup, sizeof(backup), "%s.bak", name);
            if (!stat(backup, &cookie_stat))
                rename(backup, name);
        }
        curl_easy_setopt(cookies, CURLOPT_COOKIEFILE, name);
        curl_easy_setopt(cookies, CURLOPT_COOKIELIST, "RELOAD");
    }
    return 1;
}
int browser_profile_persistent(void) { return persistent; }
const char *browser_profile_status(void) { return status_text; }
CURLcode browser_profile_configure(CURL *curl) {
    if (!ensure_cookies())
        return CURLE_OUT_OF_MEMORY;
    CURLcode result = curl_easy_setopt(curl, CURLOPT_SHARE, shared);
    if (result == CURLE_OK)
        result = curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
    if (result == CURLE_OK)
        result = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    if (result == CURLE_OK)
        result = curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    if (result == CURLE_OK && ca_path[0]) {
#ifdef __SWITCH__
        const curl_version_info_data *info = curl_version_info(CURLVERSION_NOW);
        if (info->ssl_version && strstr(info->ssl_version, "libnx"))
            result = curl_easy_setopt(curl, CURLOPT_SSL_CTX_FUNCTION, import_roots);
        else
#endif
            result = curl_easy_setopt(curl, CURLOPT_CAINFO, ca_path);
    }
    return result;
}
void browser_profile_save_cookies(void) {
    if (!persistent || !cookies)
        return;
    char source[1024], dest[1024];
    path(source, sizeof(source), "cookies.tmp");
    path(dest, sizeof(dest), "cookies.txt");
    struct curl_slist *list = NULL;
    if (curl_easy_getinfo(cookies, CURLINFO_COOKIELIST, &list) != CURLE_OK)
        return;
    FILE *f = fopen(source, "wb");
    int ok = f != NULL;
    if (f) {
        ok = fputs("# Netscape HTTP Cookie File\n", f) >= 0;
        for (struct curl_slist *p = list; p && ok; p = p->next) {
            // Only expiry-bearing cookies survive an application restart.
            const char *expires = p->data;
            for (unsigned i = 0; i < 4 && expires; i++) {
                expires = strchr(expires, '\t');
                if (expires)
                    expires++;
            }
            if (!expires || strtoll(expires, NULL, 10) <= (long long)time(NULL))
                continue;
            if (fprintf(f, "%s\n", p->data) < 0)
                ok = 0;
        }
        if (fflush(f))
            ok = 0;
        if (fclose(f))
            ok = 0;
    }
    curl_slist_free_all(list);
    if (!ok || !replace_file(source, dest)) {
        remove(source);
        snprintf(status_text, sizeof(status_text), "Could not save cookies");
    }
}
void browser_profile_close(void) {
    browser_profile_save_cookies();
    if (cookies) {
        curl_easy_setopt(cookies, CURLOPT_COOKIEJAR, NULL);
        curl_easy_cleanup(cookies);
    }
    cookies = NULL;
    if (shared)
        curl_share_cleanup(shared);
    shared = NULL;
    for (size_t i = 0; i < entry_count; i++)
        release(&entries[i]);
    entry_count = 0;
    free(ca_data);
    ca_data = NULL;
    directory[0] = ca_path[0] = 0;
    persistent = 0;
    snprintf(status_text, sizeof(status_text), "Memory-only profile");
}
void browser_profile_log(const char *url, const char *phase, long status, CURLcode code) {
    if (!persistent)
        return;
    char origin[ORIGIN_MAX], name[1024];
    if (!browser_origin(url, origin, sizeof(origin)) || !path(name, sizeof(name), "network.log"))
        return;
    struct stat st;
    if (!stat(name, &st) && st.st_size > 65536) {
        char previous[1024];
        path(previous, sizeof(previous), "network.previous.log");
        if (remove(previous) && errno != ENOENT)
            return;
        if (rename(name, previous))
            return;
    }
    FILE *f = fopen(name, "a");
    if (!f)
        return;
    // Origin only: never log query strings, cookies, tokens or response content.
    fprintf(f, "%lld %s %s HTTP=%ld CURL=%d\n", (long long)time(NULL), phase, origin, status,
            (int)code);
    fclose(f);
    chmod(name, 0600);
}

typedef struct {
    char *domain, *path, *name, *value;
    int tail, http_only, secure;
    long long expires;
} Cookie;
static int cookie_fields(char *line, Cookie *c) {
    c->http_only = !strncmp(line, "#HttpOnly_", 10);
    if (c->http_only)
        line += 10;
    else if (*line == '#')
        return 0;
    char *parts[7];
    parts[0] = line;
    for (unsigned i = 0; i < 6; i++) {
        char *tab = strchr(parts[i], '\t');
        if (!tab)
            return 0;
        *tab = 0;
        parts[i + 1] = tab + 1;
    }
    c->domain = parts[0];
    c->tail = !strcmp(parts[1], "TRUE");
    c->path = parts[2];
    c->secure = !strcmp(parts[3], "TRUE");
    c->expires = strtoll(parts[4], NULL, 10);
    c->name = parts[5];
    c->value = parts[6];
    return 1;
}
static int cookie_host(const Cookie *c, const char *host) {
    const char *domain = c->domain;
    if (*domain == '.')
        domain++;
    size_t a = strlen(host), b = strlen(domain);
    return !strcasecmp(host, domain) ||
           (c->tail && a > b && host[a - b - 1] == '.' && !strcasecmp(host + a - b, domain));
}
static int cookie_path(const char *cookie, const char *request) {
    size_t n = strlen(cookie);
    return n && !strncmp(cookie, request, n) &&
           (!request[n] || cookie[n - 1] == '/' || request[n] == '/');
}
static int url_cookie_parts(const char *url, char **host, char **request_path) {
    char origin[ORIGIN_MAX];
    if (!browser_origin(url, origin, sizeof(origin)))
        return 0;
    CURLU *u = curl_url();
    int ok = u && curl_url_set(u, CURLUPART_URL, url, 0) == CURLUE_OK &&
             curl_url_get(u, CURLUPART_HOST, host, 0) == CURLUE_OK &&
             curl_url_get(u, CURLUPART_PATH, request_path, 0) == CURLUE_OK;
    curl_url_cleanup(u);
    return ok;
}
void browser_cookie_get(const char *url, char *out, size_t capacity) {
    if (!capacity)
        return;
    out[0] = 0;
    char *host = NULL, *request = NULL;
    if (!ensure_cookies() || !url_cookie_parts(url, &host, &request)) {
        curl_free(host);
        curl_free(request);
        return;
    }
    struct curl_slist *list = NULL;
    curl_easy_getinfo(cookies, CURLINFO_COOKIELIST, &list);
    size_t used = 0;
    for (struct curl_slist *p = list; p; p = p->next) {
        char *line = strdup(p->data);
        Cookie c = {0};
        if (line && cookie_fields(line, &c) && !c.http_only && cookie_host(&c, host) &&
            cookie_path(c.path, request) && (!c.expires || c.expires > (long long)time(NULL))) {
            size_t n = strlen(c.name) + strlen(c.value) + 1;
            if (used + n + (used ? 2 : 0) < capacity) {
                if (used) {
                    memcpy(out + used, "; ", 2);
                    used += 2;
                }
                size_t k = strlen(c.name);
                memcpy(out + used, c.name, k);
                used += k;
                out[used++] = '=';
                k = strlen(c.value);
                memcpy(out + used, c.value, k);
                used += k;
                out[used] = 0;
            }
        }
        free(line);
    }
    curl_slist_free_all(list);
    curl_free(host);
    curl_free(request);
}
static char *trim(char *s) {
    while (*s == ' ' || *s == '\t')
        s++;
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t'))
        s[--n] = 0;
    return s;
}
int browser_cookie_set(const char *url, const char *assignment) {
    if (!assignment || strlen(assignment) > 4096 || strpbrk(assignment, "\r\n\t") ||
        !ensure_cookies())
        return 0;
    char *host = NULL, *request = NULL;
    if (!url_cookie_parts(url, &host, &request)) {
        curl_free(host);
        curl_free(request);
        return 0;
    }
    char *copy = strdup(assignment), *state = NULL;
    int result = 0;
    if (!copy)
        goto done;
    char *first = strtok_r(copy, ";", &state);
    if (!first)
        goto done;
    first = trim(first);
    char *equal = strchr(first, '=');
    if (!equal)
        goto done;
    *equal = 0;
    char *name = trim(first), *value = trim(equal + 1);
    if (!*name || strpbrk(name, " ()<>@,:\\\"/[]?={}") || strlen(name) > 256)
        goto done;
    for (const unsigned char *p = (unsigned char *)name; *p; p++)
        if (*p < 0x21 || *p > 0x7e)
            goto done;
    for (const unsigned char *p = (unsigned char *)value; *p; p++)
        if (*p < 0x20 || *p > 0x7e)
            goto done;
    char cookie_path_value[1024];
    snprintf(cookie_path_value, sizeof(cookie_path_value), "%s", request);
    char *last = strrchr(cookie_path_value, '/');
    if (last == cookie_path_value || !last)
        strcpy(cookie_path_value, "/");
    else
        *last = 0;
    int secure = 0, domain_attr = 0, path_attr = 0, has_max_age = 0;
    long long expires = 0;
    for (char *part = strtok_r(NULL, ";", &state); part; part = strtok_r(NULL, ";", &state)) {
        part = trim(part);
        char *eq = strchr(part, '=');
        char *attribute_value = "";
        if (eq) {
            *eq = 0;
            attribute_value = trim(eq + 1);
        }
        part = trim(part);
        if (!strcasecmp(part, "HttpOnly"))
            goto done;
        if (!strcasecmp(part, "Secure"))
            secure = 1;
        else if (!strcasecmp(part, "Domain")) {
            domain_attr = 1;
            if (*attribute_value == '.')
                attribute_value++;
            if (strcasecmp(attribute_value, host))
                goto done;
        } else if (!strcasecmp(part, "Path") && *attribute_value == '/') {
            if (strlen(attribute_value) >= sizeof(cookie_path_value))
                goto done;
            snprintf(cookie_path_value, sizeof(cookie_path_value), "%s", attribute_value);
            path_attr = 1;
        } else if (!strcasecmp(part, "Max-Age")) {
            char *end = NULL;
            long long age = strtoll(attribute_value, &end, 10);
            if (end != attribute_value && !*end) {
                has_max_age = 1;
                expires = age <= 0          ? 1
                          : age > 315360000 ? 4102444800LL
                                            : (long long)time(NULL) + age;
            }
        } else if (!strcasecmp(part, "Expires") && !has_max_age) {
            time_t date = curl_getdate(attribute_value, NULL);
            if (date != (time_t)-1)
                expires = (long long)date;
        }
    }
    if (!strncmp(name, "__Secure-", 9) && !secure)
        goto done;
    if (!strncmp(name, "__Host-", 7) &&
        (!secure || domain_attr || !path_attr || strcmp(cookie_path_value, "/")))
        goto done;
    // JavaScript cannot overwrite an HttpOnly cookie for the same host/path/name.
    struct curl_slist *list = NULL;
    curl_easy_getinfo(cookies, CURLINFO_COOKIELIST, &list);
    int protected = 0;
    for (struct curl_slist *p = list; p; p = p->next) {
        char *line = strdup(p->data);
        Cookie c = {0};
        if (line && cookie_fields(line, &c) && c.http_only && cookie_host(&c, host) &&
            !strcmp(c.path, cookie_path_value) && !strcmp(c.name, name))
            protected = 1;
        free(line);
    }
    curl_slist_free_all(list);
    if (protected)
        goto done;
    char record[6144];
    int n = snprintf(record, sizeof(record), "%s\tFALSE\t%s\t%s\t%lld\t%s\t%s", host,
                     cookie_path_value, secure ? "TRUE" : "FALSE", expires, name, value);
    if (n <= 0 || (size_t)n >= sizeof(record))
        goto done;
    result = curl_easy_setopt(cookies, CURLOPT_COOKIELIST, record) == CURLE_OK;
done:
    free(copy);
    curl_free(host);
    curl_free(request);
    return result;
}
int browser_profile_clear(void) {
    Entry old[RECORDS];
    size_t count = entry_count;
    memcpy(old, entries, sizeof(old));
    memset(entries, 0, sizeof(entries));
    entry_count = 0;
    if (!save_storage()) {
        memcpy(entries, old, sizeof(old));
        entry_count = count;
        return 0;
    }
    for (size_t i = 0; i < count; i++)
        release(&old[i]);
    if (cookies)
        curl_easy_setopt(cookies, CURLOPT_COOKIELIST, "ALL");
    int erased = 1;
    if (persistent) {
        const char *names[] = {"storage.bin.bak",     "storage.tmp", "cookies.txt",
                               "cookies.txt.bak",     "cookies.tmp", "network.log",
                               "network.previous.log"};
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
            char file[1024];
            path(file, sizeof(file), names[i]);
            if (remove(file) && errno != ENOENT)
                erased = 0;
        }
    }
    if (!erased)
        snprintf(status_text, sizeof(status_text), "Could not erase all profile files");
    return erased;
}
