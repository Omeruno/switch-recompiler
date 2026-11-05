#ifndef SWITCH_BROWSER_TEXT_H
#define SWITCH_BROWSER_TEXT_H
#include <stddef.h>
// Convert SDL_ttf's character count into a byte prefix without splitting UTF-8.
size_t browser_utf8_prefix(const char *text, size_t characters, size_t byte_limit);
#endif
