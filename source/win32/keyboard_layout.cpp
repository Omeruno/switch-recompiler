#include "keyboard_layout.h"
#include <array>
namespace win32::keyboard {namespace {
// PC set-1 positions; navigation aliases on the keypad intentionally collide
// in virtual scans. Host physical navigation uses explicit E0 positions below.
constexpr std::array<uint8_t,89> base={
    0,0x1b,'1','2','3','4','5','6','7','8','9','0',0xbd,0xbb,8,9,
    'Q','W','E','R','T','Y','U','I','O','P',0xdb,0xdd,0x0d,0xa2,'A','S',
    'D','F','G','H','J','K','L',0xba,0xde,0xc0,0xa0,0xdc,'Z','X','C','V',
    'B','N','M',0xbc,0xbe,0xbf,0xa1,0x6a,0xa4,0x20,0x14,
    0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x90,0x91,
    0x24,0x26,0x21,0x6d,0x25,0x0c,0x27,0x6b,0x23,0x28,0x22,0x2d,0x2e,
    0x2c,0,0xe2,0x7a,0x7b
};
struct Extended {uint8_t scan,vk;const char* name;};
constexpr Extended extended[]={
    {0x1c,0x0d,"Num Enter"},{0x1d,0xa3,"Right Ctrl"},{0x35,0x6f,"Num /"},
    {0x37,0x2c,"Print Screen"},{0x38,0xa5,"Right Alt"},{0x46,3,"Break"},
    {0x47,0x24,"Home"},{0x48,0x26,"Up"},{0x49,0x21,"Page Up"},
    {0x4b,0x25,"Left"},{0x4d,0x27,"Right"},{0x4f,0x23,"End"},
    {0x50,0x28,"Down"},{0x51,0x22,"Page Down"},{0x52,0x2d,"Insert"},{0x53,0x2e,"Delete"},
    {0x5b,0x5b,"Left Windows"},{0x5c,0x5c,"Right Windows"},{0x5d,0x5d,"Application"}
};
constexpr uint8_t numpad[]={0x52,0x4f,0x50,0x51,0x4b,0x4c,0x4d,0x47,0x48,0x49};
uint32_t ScanKey(uint32_t scan) {
    if(scan>0xffff)return 0;
    if(scan<base.size())return base[scan];
    if(scan==0xe11d)return 0x13;
    if((scan&0xff00)==0xe000)for(const auto& key:extended)if(key.scan==(scan&255))return key.vk;
    return 0;
}
uint16_t VirtualScan(uint32_t vk) {
    if(vk>255)return 0;
    if(vk==0x10)vk=0xa0;
    if(vk==0x11)vk=0xa2;
    if(vk==0x12)vk=0xa4;
    if(vk>=0x60 && vk<=0x69)return numpad[vk-0x60];
    if(vk==0x6e)return 0x53;
    if(vk==0x13)return 0xe11d;
    for(size_t scan=1;scan<base.size();++scan)if(base[scan] && base[scan]==vk)return uint16_t(scan);
    for(const auto& key:extended)if(key.vk==vk)return uint16_t(0xe000|key.scan);
    return 0;
}
uint32_t Character(uint32_t vk) {
    if((vk>='A' && vk<='Z') || (vk>='0' && vk<='9'))return vk;
    if(vk>=0x60 && vk<=0x69)return '0'+vk-0x60;
    switch(vk) {
    case 8:case 9:case 0x0d:case 0x1b:case 0x20:return vk;
    case 0x6a:return '*';case 0x6b:return '+';case 0x6d:return '-';case 0x6e:return '.';case 0x6f:return '/';
    case 0xba:return ';';case 0xbb:return '=';case 0xbc:return ',';case 0xbd:return '-';case 0xbe:return '.';
    case 0xbf:return '/';case 0xc0:return '`';case 0xdb:return '[';case 0xdc:case 0xe2:return '\\';case 0xdd:return ']';case 0xde:return '\'';
    default:return 0;
    }
}
}
uint32_t Generic(uint32_t vk) {
    if(vk==0xa0 || vk==0xa1)return 0x10;
    if(vk==0xa2 || vk==0xa3)return 0x11;
    if(vk==0xa4 || vk==0xa5)return 0x12;
    return vk;
}
uint32_t Map(uint32_t code,uint32_t type) {
    if(type==0 || type==4){const auto scan=VirtualScan(code);return type==4?scan:scan>=0xe100?0:scan&255;}
    if(type==1 || type==3){const auto vk=ScanKey(code);return type==1?Generic(vk):vk;}
    return type==2?Character(code):0;
}
uint16_t PhysicalScan(uint32_t vk) {
    if(vk==0x90)return 0xe045;
    if(vk==0x2c)return 0xe037;
    for(const auto& key:extended)if(key.vk==vk && vk!=0x0d)return uint16_t(0xe000|key.scan);
    return VirtualScan(vk);
}
uint32_t PhysicalKey(uint32_t vk,uint16_t scan) {
    const auto generic=Generic(vk);
    if(generic>=0x10 && generic<=0x12) {
        const auto side=Map(scan,3);if(Generic(side)==generic && side!=generic)return side;
        if(vk==generic)return 0xa0+(generic-0x10)*2;
    }
    return vk;
}
uint32_t MessageScan(uint16_t scan) {
    if(scan==0xe11d)return 0x45u<<16; // Pause's E1 sequence has no WM extended flag
    return uint32_t(scan&255)<<16 | ((scan&0xff00)==0xe000?1u<<24:0);
}
std::string Name(uint32_t lparam) {
    auto scan=(lparam>>16)&255;const bool ext=(lparam&(1u<<24))!=0;
    if(ext) {
        if(scan==0x45)return "Num Lock";
        for(const auto& key:extended)if(key.scan==scan) {
            if((lparam&(1u<<25)) && key.vk==0xa3)return "Ctrl";
            if((lparam&(1u<<25)) && key.vk==0xa5)return "Alt";
            return key.name;
        }
        return {};
    }
    if(scan>=0x3b && scan<=0x44)return "F"+std::to_string(scan-0x3b+1);
    if(scan==0x57 || scan==0x58)return "F"+std::to_string(scan-0x57+11);
    switch(scan) {
    case 1:return "Esc";case 0x0e:return "Backspace";case 0x0f:return "Tab";case 0x1c:return "Enter";
    case 0x1d:return "Ctrl";case 0x2a:return "Shift";case 0x36:return lparam&(1u<<25)?"Shift":"Right Shift";
    case 0x37:return "Num *";case 0x38:return "Alt";case 0x39:return "Space";case 0x3a:return "Caps Lock";
    case 0x45:return "Pause";case 0x46:return "Scroll Lock";case 0x4a:return "Num -";case 0x4e:return "Num +";
    case 0x53:return "Num Del";case 0x54:return "Sys Req";
    default:break;
    }
    for(size_t digit=0;digit<10;++digit)if(numpad[digit]==scan)return "Num "+std::to_string(digit);
    const auto ch=Character(ScanKey(scan));return ch>=32 && ch<=126?std::string(1,char(ch)):std::string{};
}
}
