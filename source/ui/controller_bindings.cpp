#include "controller_bindings.h"
#include "../win32/keyboard_layout.h"
#include <algorithm>
namespace ui {
const std::vector<uint16_t>& ControllerKeyChoices() {
    static const auto choices=[] {
        std::vector<uint16_t> out{0,0x20,0x0d,0x1b,0x09,0x08,0x25,0x26,0x27,0x28,
            0xa0,0xa1,0xa2,0xa3,0xa4,0xa5,0x10,0x11,0x12,0x2d,0x2e,0x24,0x23,0x21,0x22};
        for(uint16_t key='A';key<='Z';++key)out.push_back(key);
        for(uint16_t key='0';key<='9';++key)out.push_back(key);
        for(uint16_t key=0x70;key<=0x7b;++key)out.push_back(key);
        for(uint16_t key:{0xbau,0xbbu,0xbcu,0xbdu,0xbeu,0xbfu,0xc0u,0xdbu,0xdcu,0xddu,0xdeu})out.push_back(key);
        return out;
    }();return choices;
}
bool ControllerBindings::Valid(const ControllerBindings& p) {
    const auto& choices=ControllerKeyChoices();
    for(auto key:p.keys)if(std::find(choices.begin(),choices.end(),key)==choices.end())return false;
    return true;
}
std::string ControllerKeyName(uint16_t key) {
    if(!key)return "Unbound";
    const auto scan=win32::keyboard::PhysicalScan(key);
    return win32::keyboard::Name(win32::keyboard::MessageScan(scan));
}
const char* ControllerButtonName(size_t button) {
    static constexpr const char* names[]={"B","X","Y","L","ZL","Minus (-)","D-pad Left","D-pad Up","D-pad Right","D-pad Down","Left stick click","Right stick click"};
    return button<PadKeyCount?names[button]:"Invalid button";
}
bool ControllerKeyboard::Configure(const ControllerBindings& profile,bool guest,Batch& out) {
    out={};if(!ControllerBindings::Valid(profile))return false;
    if(profile==profile_ && guest==guest_)return true;
    for(auto& press:pressed_) {
        if(press.owner==Guest) {
            if(press.key && --counts_[press.key]==0)out.events[out.count++]={press.key,false};
            press.owner=Blocked;press.key=0;
        }
    }
    profile_=profile;guest_=guest;return true;
}
ControllerKeyboard::Batch ControllerKeyboard::Button(size_t button,bool down) {
    Batch out;if(button>=PadKeyCount)return out;
    auto& press=pressed_[button];
    if(down) {
        if(press.owner!=None)return out; // physical edge, no synthetic repeat
        press.owner=guest_?Guest:Native;press.key=guest_?profile_.keys[button]:0;
        if(press.key)press.key=uint16_t(win32::keyboard::PhysicalKey(press.key,win32::keyboard::PhysicalScan(press.key)));
        out.native=!guest_;
        if(press.key && counts_[press.key]++==0)out.events[out.count++]={press.key,true};
    } else {
        out.native=press.owner==Native;
        if(press.owner==Guest && press.key && --counts_[press.key]==0)out.events[out.count++]={press.key,false};
        press={};
    }
    return out;
}
}
