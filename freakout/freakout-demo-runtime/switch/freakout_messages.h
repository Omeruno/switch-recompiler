#ifndef FREAKOUT_MESSAGES_H
#define FREAKOUT_MESSAGES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { FREAKOUT_MESSAGE_CAPACITY = 32, FREAKOUT_WM_QUIT = 0x0012 };

typedef struct {
    uint32_t hwnd, message, wparam, lparam, time;
    int32_t x, y;
} FreakOutMessage;

typedef struct {
    FreakOutMessage pending[FREAKOUT_MESSAGE_CAPACITY];
    size_t count;
} FreakOutMessageQueue;

bool freakout_post_message(FreakOutMessageQueue *queue, FreakOutMessage message);
bool freakout_peek_message(FreakOutMessageQueue *queue, uint32_t window,
                           uint32_t minimum, uint32_t maximum,
                           bool remove, FreakOutMessage *result);

#endif
