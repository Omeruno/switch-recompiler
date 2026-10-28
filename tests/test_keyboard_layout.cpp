#include "source/win32/keyboard_layout.h"
#include <cassert>
#include <cstdio>
namespace k=win32::keyboard;
int main() {
    struct Sample{uint32_t vk,scan;};
    const Sample ordinary[]={{'A',0x1e},{'Q',0x10},{'Z',0x2c},{'0',0x0b},{'1',2},{0x1b,1},{8,0x0e},{9,0x0f},{0x0d,0x1c},{0x20,0x39},{0x70,0x3b},{0x79,0x44},{0x7b,0x58},{0xba,0x27},{0xe2,0x56}};
    for(auto s:ordinary){assert(k::Map(s.vk,0)==s.scan && k::Map(s.scan,1)==s.vk && k::Map(s.scan,3)==s.vk);}
    assert(k::Map(0xa3,0)==0x1d && k::Map(0xa3,4)==0xe01d && k::Map(0xe01d,3)==0xa3 && k::Map(0xe01d,1)==0x11);
    assert(k::Map(0xa5,4)==0xe038 && k::Map(0xe038,3)==0xa5 && k::Map(0xe038,1)==0x12);
    assert(k::Map(0x36,1)==0x10 && k::Map(0x36,3)==0xa1 && k::Map(0x2a,3)==0xa0);
    assert(k::Map(0x10,0)==0x2a && k::Map(0x11,0)==0x1d && k::Map(0x12,0)==0x38);
    assert(k::Map(0x64,0)==0x4b && k::Map(0x25,0)==0x4b && k::Map(0x4b,1)==0x25);
    assert(k::Map('A',2)=='A' && k::Map(0xbb,2)=='=' && k::Map(0xde,2)=='\'' && k::Map(0x65,2)=='5');
    assert(k::Map(0x13,4)==0xe11d && !k::Map(0x13,0) && k::Map(0xe11d,3)==0x13);
    for(auto bad:{0u,0xffu,0x136u,0xe036u,0xe145u,0xffe01du,0xffffffffu})assert(!k::Map(bad,3));
    for(uint32_t vk=256;vk<65536;++vk)assert(!k::Map(vk,0) && !k::Map(vk,2));
    assert(!k::Map('A',99));
    assert(k::Name(0x001e0001)=="A" && k::Name(0xc01effff)=="A");
    assert(k::Name(0x00360000)=="Right Shift" && k::Name(0x02360000)=="Shift");
    assert(k::Name(0x011d0000)=="Right Ctrl" && k::Name(0x031d0000)=="Ctrl");
    assert(k::Name(0x014b0000)=="Left" && k::Name(0x004b0000)=="Num 4");
    assert(k::Name(0x00450000)=="Pause" && k::Name(0x01450000)=="Num Lock");
    assert(k::Name(0x011c0000)=="Num Enter" && k::Name(0x001c0000)=="Enter");
    assert(k::Name(0x00ff0000).empty() && k::Name(0x01ff0000).empty());
    assert(k::PhysicalScan(0x25)==0xe04b && k::PhysicalScan(0x64)==0x4b);
    assert(k::MessageScan(k::PhysicalScan(0xa3))==0x011d0000 && k::MessageScan(0xe11d)==0x00450000);
    assert(k::PhysicalKey(0x10,0x36)==0xa1 && k::PhysicalKey(0xa0,0x36)==0xa1);
    puts("US keyboard layout PASS (independent mappings/numpad collisions/modifiers/E0-E1/characters/names/invalid ranges)");
}
