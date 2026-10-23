#include "source/ui/launcher_loop.h"
#include "source/ui/pointer_input.h"
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
struct Frontend {
    std::string trace;
    int picks=0,runs=0,ends=0;
    int failOutput=-1,shutdownRun=-1,shutdownPick=-1;
    bool alive=true,output=false,guest=false;
    std::vector<bool> initial;
    bool Alive(){return alive;}
    bool Pick(bool first){assert(!output&&!guest);initial.push_back(first);trace+='P';
        if(picks++==shutdownPick)alive=false;
        return picks<=3;}
    bool BeginOutput(){assert(!output&&!guest);output=true;trace+='B';return picks!=failOutput;}
    void RunGuest(){assert(output&&!guest);guest=true;trace+='G';++runs;
        if(runs==shutdownRun)alive=false;
        // Includes the synchronous session cleanup contract, even on load failure.
        guest=false;}
    void EndOutput(){assert(output&&!guest);output=false;++ends;trace+='E';}
    void RestorePicker(){assert(!output&&!guest&&alive);trace+='C';}
};
int main(){
    static_assert(ui::UseNewGuestUi(false),"Every EXE including Paint defaults to new UI");
    static_assert(!ui::UseNewGuestUi(true),"L requests the legacy UI");
    Frontend normal;ui::RunLauncher(normal);
    assert(normal.trace=="PBGECPBGECPBGECP");
    assert((normal.initial==std::vector<bool>{true,false,false,false}));
    assert(normal.runs==3&&normal.ends==3);
    Frontend failed;failed.failOutput=1;ui::RunLauncher(failed);
    assert(failed.trace=="PBECPBGECPBGECP"&&failed.runs==2&&failed.ends==3);
    Frontend shutdown;shutdown.shutdownRun=1;ui::RunLauncher(shutdown);
    assert(shutdown.trace=="PBGE"&&!shutdown.output);
    Frontend pickerShutdown;pickerShutdown.shutdownPick=0;ui::RunLauncher(pickerShutdown);
    assert(pickerShutdown.trace=="P"&&pickerShutdown.ends==0);
    // Guest exit PLUS and launch A/ZR/touch must be released before reuse.
    ui::PointerInputGate gate;gate.Reset(7,true);
    auto e=gate.Update(7,true);assert(!e.down&&!e.up&&!e.held&&!e.touching);
    e=gate.Update(0,false);assert(!e.down&&!e.up&&!e.touching);
    e=gate.Update(7,true);assert(e.down==7&&e.held==7&&e.touching);
    gate.Reset(7,true);e=gate.Update(7,true);assert(!e.down&&!e.up&&!e.touching);
    std::puts("launcher loop: restart, partial output failure, shutdown ordering and held input OK");
}
