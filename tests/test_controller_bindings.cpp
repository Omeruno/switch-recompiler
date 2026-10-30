#include "source/ui/controller_bindings.h"
#include <cassert>
#include <cstdio>
namespace {
void Event(const ui::ControllerKeyboard::Batch& b,uint16_t key,bool down){assert(b.count==1 && b.events[0].key==key && b.events[0].down==down);}
}
int main(){
    ui::ControllerBindings profile;assert(ui::ControllerBindings::Valid(profile));
    for(auto key:ui::ControllerKeyChoices())assert(!ui::ControllerKeyName(key).empty());
    assert(ui::ControllerKeyName(0x71)=="F2" && ui::ControllerKeyName(0xbf)=="/" && ui::ControllerKeyName(0xa3)=="Right Ctrl");
    ui::ControllerKeyboard core;ui::ControllerKeyboard::Batch changes;
    profile.keys[0]='Z';profile.keys[1]='Z';profile.keys[2]=0;assert(core.Configure(profile,true,changes) && !changes.count);
    Event(core.Button(0,true),'Z',true);assert(!core.Button(1,true).count && !core.Button(0,true).count);
    assert(!core.Button(0,false).count);Event(core.Button(1,false),'Z',false);assert(!core.Button(1,false).count);
    assert(!core.Button(2,true).count && !core.Button(2,false).count);
    Event(core.Button(0,true),'Z',true);profile.keys[0]=0x71;assert(core.Configure(profile,true,changes));Event(changes,'Z',false);
    assert(!core.Button(0,true).count && !core.Button(0,false).count);Event(core.Button(0,true),0x71,true);
    auto invalid=profile;invalid.keys[0]=0x5b;assert(!core.Configure(invalid,false,changes) && !changes.count);
    Event(core.Button(0,false),0x71,false);
    Event(core.Button(0,true),0x71,true);assert(core.Configure(profile,false,changes));Event(changes,0x71,false);
    assert(!core.Button(0,false).count);
    auto native=core.Button(0,true);assert(native.native && !native.count);
    assert(core.Configure(profile,true,changes) && !changes.count);
    native=core.Button(0,false);assert(native.native && !native.count);Event(core.Button(0,true),0x71,true);Event(core.Button(0,false),0x71,false);
    // Generic and sided bindings refer to the same physical modifier owner.
    profile.keys[0]=0x11;profile.keys[1]=0xa2;profile.keys[2]=0xa3;assert(core.Configure(profile,true,changes));
    Event(core.Button(0,true),0xa2,true);assert(!core.Button(1,true).count);Event(core.Button(2,true),0xa3,true);
    assert(!core.Button(0,false).count);Event(core.Button(1,false),0xa2,false);Event(core.Button(2,false),0xa3,false);
    for(size_t n=0;n<ui::PadKeyCount;++n)profile.keys[n]=uint16_t('A'+n);
    assert(core.Configure(profile,true,changes));for(size_t n=0;n<ui::PadKeyCount;++n)Event(core.Button(n,true),uint16_t('A'+n),true);
    assert(core.Configure(profile,false,changes) && changes.count==ui::PadKeyCount);
    for(size_t n=0;n<ui::PadKeyCount;++n){assert(!changes.events[n].down && changes.events[n].key=='A'+n);assert(!core.Button(n,false).count);}
    assert(!core.Button(255,true).count);
    puts("Controller bindings core PASS (named fixed-US keys,duplicates/generic+sided modifier owners,unbound/no synthetic repeat,atomic profile/context change,held quarantine,invalid/reserved key rejection,12-key release bound)");
}
