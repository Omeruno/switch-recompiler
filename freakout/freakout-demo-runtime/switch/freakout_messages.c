#include "freakout_messages.h"
#include <string.h>

_Static_assert(sizeof(FreakOutMessage) == 28, "Unexpected 32-bit Win32 MSG layout");

bool freakout_post_message(FreakOutMessageQueue *queue, FreakOutMessage message)
{
    if (queue->count >= FREAKOUT_MESSAGE_CAPACITY)
        return false;
    queue->pending[queue->count++] = message;
    return true;
}

bool freakout_peek_message(FreakOutMessageQueue *queue, uint32_t window,
                           uint32_t minimum, uint32_t maximum,
                           bool remove, FreakOutMessage *result)
{
    for (size_t i = 0; i < queue->count; ++i) {
        const FreakOutMessage *message = &queue->pending[i];
        bool thread_filter = window == UINT32_MAX;
        bool window_matches = !window || (thread_filter ? !message->hwnd : message->hwnd == window);
        bool code_matches = (!minimum && !maximum) ||
                            (message->message >= minimum && message->message <= maximum);
        if (message->message != FREAKOUT_WM_QUIT && (!window_matches || !code_matches))
            continue;
        *result = *message;
        if (remove) {
            --queue->count;
            memmove(&queue->pending[i], &queue->pending[i + 1],
                    (queue->count - i) * sizeof *message);
        }
        return true;
    }
    return false;
}
