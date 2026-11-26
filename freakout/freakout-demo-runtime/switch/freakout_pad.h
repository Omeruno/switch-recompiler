#ifndef FREAKOUT_PAD_H
#define FREAKOUT_PAD_H

#include <stdint.h>

enum {
    FO_XBOX_UP = 0x0001, FO_XBOX_DOWN = 0x0002,
    FO_XBOX_LEFT = 0x0004, FO_XBOX_RIGHT = 0x0008,
    FO_XBOX_START = 0x0010, FO_XBOX_BACK = 0x0020,
    FO_XBOX_LS = 0x0040, FO_XBOX_RS = 0x0080,
    FO_XBOX_LB = 0x0100, FO_XBOX_RB = 0x0200,
    FO_XBOX_A = 0x1000, FO_XBOX_B = 0x2000,
    FO_XBOX_X = 0x4000, FO_XBOX_Y = 0x8000,
};

enum {
    FO_DIK_ESCAPE = 0x01, FO_DIK_RETURN = 0x1C, FO_DIK_SPACE = 0x39,
    FO_DIK_UP = 0xC8, FO_DIK_LEFT = 0xCB, FO_DIK_RIGHT = 0xCD, FO_DIK_DOWN = 0xD0,
};

typedef struct {
    uint16_t buttons;
    uint8_t left_trigger, right_trigger;
    int16_t lx, ly, rx, ry;
} FoXboxPad;

void fo_pad_keyboard(const FoXboxPad *pad, uint8_t keys[256]);

#endif
