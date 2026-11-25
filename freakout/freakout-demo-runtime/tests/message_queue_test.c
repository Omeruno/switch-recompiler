#include "freakout_messages.h"
#include <assert.h>

int main(void)
{
    FreakOutMessageQueue queue = {0};
    FreakOutMessage result = {0};
    const FreakOutMessage intro = { .hwnd = 0x200, .message = 0x400, .lparam = 41 };
    const FreakOutMessage unrelated = { .hwnd = 0x201, .message = 0x400 };
    const FreakOutMessage quit = { .message = FREAKOUT_WM_QUIT, .wparam = 7 };

    assert(!freakout_peek_message(&queue, 0, 0, 0, true, &result));
    assert(freakout_post_message(&queue, unrelated));
    assert(freakout_post_message(&queue, intro));
    assert(freakout_peek_message(&queue, 0x200, 0x400, 0x400, false, &result));
    assert(result.hwnd == 0x200 && result.lparam == 41 && queue.count == 2);
    assert(freakout_peek_message(&queue, 0x200, 0x400, 0x400, true, &result));
    assert(queue.count == 1);
    assert(!freakout_peek_message(&queue, UINT32_MAX, 0, 0, true, &result));
    assert(freakout_post_message(&queue, quit));
    assert(freakout_peek_message(&queue, 0x200, 0x400, 0x400, true, &result));
    assert(result.message == FREAKOUT_WM_QUIT && result.wparam == 7);
    assert(freakout_peek_message(&queue, 0, 0, 0, true, &result));
    assert(result.hwnd == 0x201 && queue.count == 0);

    for (size_t i = 0; i < FREAKOUT_MESSAGE_CAPACITY; ++i)
        assert(freakout_post_message(&queue, intro));
    assert(!freakout_post_message(&queue, intro));
}
