#include "text.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    assert(browser_utf8_prefix("Привет", 1, 100) == 2);
    assert(browser_utf8_prefix("Привет", 3, 100) == 6);
    assert(browser_utf8_prefix("Привет", 100, 5) == 4);
    assert(browser_utf8_prefix("A😀Б", 1, 100) == 1);
    assert(browser_utf8_prefix("A😀Б", 2, 100) == 5);
    assert(browser_utf8_prefix("A😀Б", 3, 100) == 7);
    assert(browser_utf8_prefix("A😀Б", 2, 4) == 1);
    const char *text = "Привет, Switch! 😀 Русский текст.";
    for (size_t count = 0; count < 100; count++)
        for (size_t cap = 0; cap < strlen(text) + 2; cap++) {
            size_t bytes = browser_utf8_prefix(text, count, cap);
            assert(bytes <= cap && bytes <= strlen(text));
            assert(!text[bytes] || ((unsigned char)text[bytes] & 0xc0) != 0x80);
        }
    assert(browser_utf8_prefix("", 1, 100) == 0);
    assert(browser_utf8_prefix(NULL, 1, 100) == 0);
    assert(browser_utf8_prefix("\xd0", 1, 100) == 1);
    puts("SDL character counts to UTF-8 byte boundaries: PASS");
    return 0;
}
