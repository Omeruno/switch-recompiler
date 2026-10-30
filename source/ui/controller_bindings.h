#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
namespace ui {
enum class PadKeyButton : uint8_t {B,X,Y,L,ZL,Minus,Left,Up,Right,Down,StickL,StickR,Count};
constexpr size_t PadKeyCount=size_t(PadKeyButton::Count);
struct ControllerBindings {
    std::array<uint16_t,PadKeyCount> keys{{0x1b,0x0d,0x09,0x12,0,0,0x25,0x26,0x27,0x28,0,0}};
    bool operator==(const ControllerBindings& b) const {return keys==b.keys;}
    bool operator!=(const ControllerBindings& b) const {return !(*this==b);}
    static bool Valid(const ControllerBindings&);
};
ControllerBindings ArcadeControllerBindings();
const std::vector<uint16_t>& ControllerKeyChoices();
std::string ControllerKeyName(uint16_t key);
const char* ControllerButtonName(size_t button);
// Bounded physical-owner/refcount state. No guest memory, native input API or
// callbacks. Profile/context changes release old keys and quarantine held owners.
class ControllerKeyboard {
public:
    struct Transition {uint16_t key=0;bool down=false;};
    struct Batch {std::array<Transition,PadKeyCount> events{};size_t count=0;bool native=false;};
    bool Configure(const ControllerBindings&,bool guest,Batch&);
    Batch Button(size_t button,bool down);
private:
    enum Owner:uint8_t {None,Native,Guest,Blocked};
    struct Press{Owner owner=None;uint16_t key=0;};
    std::array<Press,PadKeyCount> pressed_{};
    std::array<uint8_t,256> counts_{};
    ControllerBindings profile_;
    bool guest_=false;
};
}
