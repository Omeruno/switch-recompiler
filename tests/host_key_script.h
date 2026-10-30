#pragma once
#include "source/win32/user32.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>
// Host diagnostic input only. Ticks are platform-wait calls, never game frames.
class HostKeyScript {
public:
    static constexpr size_t MaxEvents=256;
    bool Add(const std::string& text) {
        if(events_.size()==MaxEvents)return false;
        const auto a=text.find(':'),b=a==std::string::npos?a:text.find(':',a+1);
        if(a==std::string::npos || b==std::string::npos)return false;
        uint32_t tick=0,key=0;
        if(!Number(text.substr(0,a),tick) || !tick || tick>1000000 ||
           !Number(text.substr(a+1,b-a-1),key) || !key || key>254)return false;
        const auto action=text.substr(b+1);if(action!="down" && action!="up")return false;
        if(!events_.empty() && tick<events_.back().tick)return false;
        events_.push_back({tick,key,action=="down"});return true;
    }
    bool Valid(uint32_t limit) const {
        std::array<bool,256> held{};
        for(const auto& e:events_) {
            if(e.tick>limit || (!e.down && !held[e.key]))return false;
            held[e.key]=e.down;
        }
        for(bool down:held)if(down)return false;
        return true;
    }
    void Feed(uint32_t tick,std::vector<win32::user::InputEvent>& out) {
        while(next_<events_.size() && events_[next_].tick<=tick) {
            const auto& key=events_[next_++];win32::user::InputEvent e;
            e.type=key.down?win32::user::InputEvent::KeyDown:win32::user::InputEvent::KeyUp;e.vk=key.key;
            out.push_back(e);
        }
    }
    size_t Size() const {return events_.size();}
    bool Complete() const {return next_==events_.size();}
private:
    struct Event{uint32_t tick,key;bool down;};
    std::vector<Event> events_;size_t next_=0;
    static bool Number(const std::string& text,uint32_t& value) {
        if(text.empty())return false;
        size_t at=0;unsigned base=10;uint64_t n=0;
        if(text.size()>2 && text[0]=='0' && (text[1]=='x' || text[1]=='X')){base=16;at=2;}
        for(;at<text.size();++at) {
            const char c=text[at];const unsigned d=c>='0'&&c<='9'?unsigned(c-'0'):c>='a'&&c<='f'?unsigned(c-'a'+10):c>='A'&&c<='F'?unsigned(c-'A'+10):99;
            if(d>=base)return false;
            n=n*base+d;if(n>0xffffffffu)return false;
        }
        value=uint32_t(n);return true;
    }
};
