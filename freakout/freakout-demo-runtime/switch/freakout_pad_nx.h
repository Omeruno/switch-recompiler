#ifndef FREAKOUT_PAD_NX_H
#define FREAKOUT_PAD_NX_H

#include <switch.h>
#include "freakout_pad.h"

static inline FoXboxPad fo_nx_pad(const PadState *pad)
{
    static const struct { u64 hid; uint16_t xbox; } map[] = {
        {HidNpadButton_Up, FO_XBOX_UP}, {HidNpadButton_Down, FO_XBOX_DOWN},
        {HidNpadButton_Left, FO_XBOX_LEFT}, {HidNpadButton_Right, FO_XBOX_RIGHT},
        {HidNpadButton_A, FO_XBOX_A}, {HidNpadButton_B, FO_XBOX_B},
        {HidNpadButton_X, FO_XBOX_X}, {HidNpadButton_Y, FO_XBOX_Y},
        {HidNpadButton_L, FO_XBOX_LB}, {HidNpadButton_R, FO_XBOX_RB},
        {HidNpadButton_StickL, FO_XBOX_LS}, {HidNpadButton_StickR, FO_XBOX_RS},
        {HidNpadButton_Plus, FO_XBOX_START}, {HidNpadButton_Minus, FO_XBOX_BACK},
    };
    u64 held = padGetButtons(pad);
    HidAnalogStickState left = padGetStickPos(pad, 0);
    HidAnalogStickState right = padGetStickPos(pad, 1);
    FoXboxPad result = {0};
    for (unsigned i = 0; i < sizeof map / sizeof map[0]; i++)
        if (map[i].hid & held)
            result.buttons |= map[i].xbox;
    result.left_trigger = held & HidNpadButton_ZL ? 255 : 0;
    result.right_trigger = held & HidNpadButton_ZR ? 255 : 0;
    result.lx = left.x;
    result.ly = left.y;
    result.rx = right.x;
    result.ry = right.y;
    return result;
}

#endif
