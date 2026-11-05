#include "profile.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv) {
    assert(argc == 2);
    curl_global_init(CURL_GLOBAL_DEFAULT);
    const char *a = "https://example.test/ru/page", *b = "https://other.test/ru/";
    assert(browser_profile_open(argv[1], NULL) && browser_profile_persistent());
    assert(!browser_storage_set(a, 0, "язык", "русский"));
    assert(!browser_storage_set(a, 1, "visit", "current"));
    assert(!strcmp(browser_storage_get("https://EXAMPLE.test:443/other", 0, "язык"), "русский"));
    assert(!browser_storage_get(b, 0, "язык"));
    assert(!browser_storage_get("https://example.test:444/", 0, "язык"));
    assert(!browser_storage_set(b, 0, "separate", "yes"));
    assert(browser_storage_length(a, 0) == 1);
    assert(!strcmp(browser_storage_key(a, 0, 0), "язык"));
    char *large = malloc(16385);
    memset(large, 'x', 16384);
    large[16384] = 0;
    assert(!browser_storage_set(a, 0, "large1", large));
    assert(!browser_storage_set(a, 0, "large2", large));
    assert(!browser_storage_set(a, 0, "large3", large));
    assert(browser_storage_set(a, 0, "large4", large) == -1);
    free(large);
    assert(!browser_storage_remove(a, 0, "large1"));
    char blocked[1024];
    snprintf(blocked, sizeof(blocked), "%s/storage.tmp", argv[1]);
    assert(!mkdir(blocked, 0700));
    assert(browser_storage_set(a, 0, "язык", "BAD") == -2);
    assert(!strcmp(browser_storage_get(a, 0, "язык"), "русский"));
    assert(browser_storage_clear(a, 0) == -2 && browser_storage_length(a, 0) == 3);
    assert(browser_storage_remove(a, 0, "язык") == -2);
    assert(browser_storage_remove(a, 0, "large3") == -2);
    assert(browser_storage_get(a, 0, "язык") && browser_storage_get(a, 0, "large3"));
    assert(!rmdir(blocked));
    assert(browser_cookie_set(a, "kd_lang=ru; Max-Age=3600; Path=/; Secure"));
    assert(browser_cookie_set(a, "pathonly=yes; Path=/ru"));
    assert(!browser_cookie_set(a, "foreign=bad; Domain=other.test"));
    assert(!browser_cookie_set(a, "wide=bad; Domain=test"));
    CURL *server = curl_easy_init();
    assert(server && browser_profile_configure(server) == CURLE_OK);
    assert(curl_easy_setopt(server, CURLOPT_COOKIELIST,
                            "#HttpOnly_example.test\tFALSE\t/\tTRUE\t4102444800\tauth\tSECRET") ==
           CURLE_OK);
    curl_easy_cleanup(server);
    assert(!browser_cookie_set(a, "auth=overwrite; Path=/"));
    char cookies[4096];
    browser_cookie_get(a, cookies, sizeof(cookies));
    assert(strstr(cookies, "kd_lang=ru") && strstr(cookies, "pathonly=yes") &&
           !strstr(cookies, "SECRET") && !strstr(cookies, "auth="));
    browser_cookie_get("https://example.test/russian", cookies, sizeof(cookies));
    assert(!strstr(cookies, "pathonly"));
    browser_cookie_get(b, cookies, sizeof(cookies));
    assert(!cookies[0]);
    assert(!browser_cookie_set(a, "__Host-bad=x; Secure"));
    assert(browser_cookie_set(a, "__Host-good=x; Secure; Path=/; Max-Age=60"));
    assert(browser_cookie_set(a, "delete=gone; Path=/; Max-Age=60"));
    assert(browser_cookie_set(a, "delete=gone; Path=/; Max-Age=0"));
    browser_cookie_get(a, cookies, sizeof(cookies));
    assert(!strstr(cookies, "delete="));
    browser_profile_close();
    assert(browser_profile_open(argv[1], NULL));
    assert(!strcmp(browser_storage_get(a, 0, "язык"), "русский"));
    assert(!browser_storage_get(a, 1, "visit"));
    browser_cookie_get(a, cookies, sizeof(cookies));
    assert(strstr(cookies, "kd_lang=ru") && !strstr(cookies, "pathonly="));
    assert(!browser_storage_set(a, 0, "recovery", "previous"));
    assert(!browser_storage_set(a, 0, "recovery", "latest"));
    browser_profile_close();
    char primary[1024];
    snprintf(primary, sizeof(primary), "%s/storage.bin", argv[1]);
    assert(!remove(primary)); // Interrupted rotation: only the backup survived.
    assert(browser_profile_open(argv[1], NULL));
    assert(strstr(browser_profile_status(), "Recovered"));
    assert(!strcmp(browser_storage_get(a, 0, "recovery"), "previous"));
    assert(browser_profile_clear());
    assert(!browser_storage_length(a, 0) && !browser_storage_length(b, 0));
    browser_cookie_get(a, cookies, sizeof(cookies));
    assert(!cookies[0]);
    char backup[1024];
    snprintf(backup, sizeof(backup), "%s/storage.bin.bak", argv[1]);
    assert(access(backup, F_OK) != 0);
    snprintf(backup, sizeof(backup), "%s/cookies.txt.bak", argv[1]);
    assert(access(backup, F_OK) != 0);
    browser_profile_close();
    char file[1024];
    snprintf(file, sizeof(file), "%s/storage.bin", argv[1]);
    FILE *f = fopen(file, "wb");
    assert(f);
    fwrite("SBST1\n\xff\xff\xff\xff", 1, 10, f);
    fclose(f);
    assert(browser_profile_open(argv[1], NULL));
    assert(strstr(browser_profile_status(), "Corrupt"));
    assert(!browser_storage_length(a, 0));
    browser_profile_close();
    curl_global_cleanup();
    puts("browser profile, cookie scope, persistence and quota: PASS");
    return 0;
}
