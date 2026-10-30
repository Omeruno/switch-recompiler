#include "host_key_script.h"
#include <cassert>
#include <cstdio>
int main(){
    HostKeyScript script;
    for(const auto* bad:{"","0:65:down","-1:65:down","1:0:down","1:255:down","1:0x100000041:down","4294967296:65:down","1:65:DOWN","1:65:up:extra","1::down","0x:65:down","1:65:down "})assert(!script.Add(bad));
    assert(script.Add("2:0x41:down") && script.Add("2:65:down")); // explicit repeat, stable same-tick order
    assert(!script.Add("1:65:up") && !script.Valid(3));
    assert(script.Add("3:65:up") && script.Valid(3) && !script.Valid(2));
    std::vector<win32::user::InputEvent> out;script.Feed(1,out);assert(out.empty());script.Feed(2,out);
    assert(out.size()==2 && out[0].vk=='A' && out[1].type==win32::user::InputEvent::KeyDown);
    script.Feed(2,out);assert(out.size()==2 && !script.Complete());script.Feed(4,out);
    assert(out.size()==3 && out.back().type==win32::user::InputEvent::KeyUp && script.Complete());
    HostKeyScript orphan;assert(orphan.Add("1:65:up") && !orphan.Valid(1));
    HostKeyScript maximum;for(unsigned i=0;i<256;++i)assert(maximum.Add(i%2?"1:65:up":"1:65:down"));
    assert(maximum.Valid(1) && !maximum.Add("1:65:down"));
    puts("Host key script PASS (bounded strict decimal/hex parser,ordered same-tick repeats,balanced releases,no wrap/partial parse,exact one-shot delivery;wait ticks are not game frames)");
}
