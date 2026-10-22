#pragma once
#include <cstdint>
namespace ui {
constexpr int kCursorWidth=11,kCursorHeight=19;
inline uint32_t CursorRgba(int x,int y) {
    static const char* const arrow[]={"X","XX","X.X","X..X","X...X","X....X","X.....X","X......X","X.......X","X........X",
        "X.....XXXXX","X..X..X","X.X X..X","XX  X..X","X    X..X","     X..X","      X..X","      X..X","       XX"};
    if(y<0 || y>=kCursorHeight || x<0)return 0;
    for(int n=0;n<=x;++n)if(!arrow[y][n])return 0;
    return arrow[y][x]=='X'?0xFF000000u:arrow[y][x]=='.'?0xFFFFFFFFu:0;
}
} // namespace ui
