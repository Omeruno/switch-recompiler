#ifndef SWITCH_BROWSER_PROFILE_H
#define SWITCH_BROWSER_PROFILE_H
#include <curl/curl.h>
#include <stddef.h>
// One single-threaded profile for the app. NULL directory means memory only.
int browser_profile_open(const char *directory, const char *ca_file);
void browser_profile_close(void);
int browser_profile_persistent(void);
int browser_profile_clear(void);
const char *browser_profile_status(void);
CURLcode browser_profile_configure(CURL *curl);
void browser_profile_save_cookies(void);
void browser_profile_log(const char *url, const char *phase, long status, CURLcode code);
int browser_origin(const char *url, char *out, size_t capacity);
// Storage: 0 success, -1 quota/invalid origin, -2 disk error.
const char *browser_storage_get(const char *url, int session, const char *key);
int browser_storage_set(const char *url, int session, const char *key, const char *value);
int browser_storage_remove(const char *url, int session, const char *key);
int browser_storage_clear(const char *url, int session);
size_t browser_storage_length(const char *url, int session);
const char *browser_storage_key(const char *url, int session, size_t index);
void browser_cookie_get(const char *url, char *output, size_t capacity);
int browser_cookie_set(const char *url, const char *assignment);
#endif
