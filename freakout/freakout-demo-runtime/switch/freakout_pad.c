#include "freakout_pad.h"
#include <string.h>

void fo_pad_keyboard(const FoXboxPad *pad, uint8_t keys[256])
{
    enum { DEAD_ZONE = 10000 };
    uint16_t directions = pad->buttons & (FO_XBOX_UP | FO_XBOX_DOWN | FO_XBOX_LEFT | FO_XBOX_RIGHT);
    if (!(directions & (FO_XBOX_LEFT | FO_XBOX_RIGHT))) {
        if (pad->lx < -DEAD_ZONE)
            directions |= FO_XBOX_LEFT;
        if (pad->lx > DEAD_ZONE)
            directions |= FO_XBOX_RIGHT;
    }
    if (!(directions & (FO_XBOX_UP | FO_XBOX_DOWN))) {
        if (pad->ly > DEAD_ZONE)
            directions |= FO_XBOX_UP;
        if (pad->ly < -DEAD_ZONE)
            directions |= FO_XBOX_DOWN;
    }
    memset(keys, 0, 256);
    keys[FO_DIK_UP] = directions & FO_XBOX_UP ? 0x80 : 0;
    keys[FO_DIK_DOWN] = directions & FO_XBOX_DOWN ? 0x80 : 0;
    keys[FO_DIK_LEFT] = directions & FO_XBOX_LEFT ? 0x80 : 0;
    keys[FO_DIK_RIGHT] = directions & FO_XBOX_RIGHT ? 0x80 : 0;
    keys[FO_DIK_RETURN] = pad->buttons & FO_XBOX_A ? 0x80 : 0;
    keys[FO_DIK_ESCAPE] = pad->buttons & (FO_XBOX_B | FO_XBOX_START) ? 0x80 : 0;
}
