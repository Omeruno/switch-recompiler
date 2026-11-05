#include "text.h"
static size_t sequence(const unsigned char *text) {
    unsigned char first = text[0];
    size_t n = first < 0x80                     ? 1
               : first >= 0xc2 && first <= 0xdf ? 2
               : first >= 0xe0 && first <= 0xef ? 3
               : first >= 0xf0 && first <= 0xf4 ? 4
                                                : 1;
    for (size_t i = 1; i < n; i++)
        if (!text[i] || (text[i] & 0xc0) != 0x80)
            return 1;
    if (n == 3 && ((first == 0xe0 && text[1] < 0xa0) || (first == 0xed && text[1] >= 0xa0)))
        return 1;
    if (n == 4 && ((first == 0xf0 && text[1] < 0x90) || (first == 0xf4 && text[1] >= 0x90)))
        return 1;
    return n;
}
size_t browser_utf8_prefix(const char *text, size_t characters, size_t byte_limit) {
    size_t used = 0;
    if (!text)
        return 0;
    while (characters && text[used] && used < byte_limit) {
        size_t n = sequence((const unsigned char *)text + used);
        if (n > byte_limit - used)
            break;
        used += n;
        --characters;
    }
    return used;
}
