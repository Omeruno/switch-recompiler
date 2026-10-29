#include "source/ui/desktop_shell.h"
#include "source/runtime/audio_output.h"
#include "tests/audio_capture_device.h"
#include <filesystem>
#include <fstream>
#include <cassert>
#include <cstdio>
namespace fs=std::filesystem;using Shell=ui::DesktopShell;
static uint64_t timeUs=1000000;static uint64_t Now(){return timeUs;}
static void Click(Shell& s,int target,int shift=0){const auto r=s.ControlBounds(target);assert(!r.Empty() && s.Enabled(target));const int x=r.left+r.Width()/2+shift,y=r.top+r.Height()/2;s.PointerDown(x,y);s.PointerUp(x,y);}
static int Row(Shell& s,const char* name){for(size_t n=0;n<s.Browser().Entries().size();++n)if(s.Browser().Entries()[n].name==name)return Shell::BrowserRowBase+int(n);assert(false);return 0;}
int main(int argc,char** argv){
    const fs::path source=argc>1?argv[1]:"audio/stereo-vbr.mp3";char pattern[]="/tmp/sr-dbl-XXXXXX";assert(mkdtemp(pattern));const fs::path root=pattern;
    struct Cleanup{fs::path p;~Cleanup(){fs::remove_all(p);}}cleanup{root};Shell shell(&Now);std::string error;assert(shell.ConfigureFilesystem(root.string(),"","",error));
    fs::create_directory(root/"A");fs::create_directory(root/"B");fs::copy_file(source,root/"A/music.mp3");
    AudioCaptureDevice device;assert(rt::Audio().Bind(&device));{
        rt::AudioScope audio;Click(shell,Shell::Computer);Click(shell,Shell::QuickC);auto row=Row(shell,"A");
        Click(shell,row);assert(shell.Browser().Path()=="C:\\");timeUs+=200000;Click(shell,row);assert(shell.Browser().Path()=="C:\\A");
        Click(shell,Shell::NavUp);row=Row(shell,"A");timeUs+=1000000;Click(shell,row);timeUs+=500001;Click(shell,row);assert(shell.Browser().Path()=="C:\\");
        timeUs+=1000000;Click(shell,row);timeUs+=100000;Click(shell,row,20);assert(shell.Browser().Path()=="C:\\");
        timeUs+=1000000;Click(shell,Row(shell,"A"));timeUs+=100000;Click(shell,Row(shell,"B"));assert(shell.Browser().Path()=="C:\\");timeUs+=100000;Click(shell,Row(shell,"B"));assert(shell.Browser().Path()=="C:\\B");
        Click(shell,Shell::NavUp);timeUs+=1000000;row=Row(shell,"A");Click(shell,row);shell.Enter();timeUs+=100000;Click(shell,row);assert(shell.Browser().Path()=="C:\\");
        timeUs+=1000000;Click(shell,row);shell.OpenContext(false);assert(shell.ContextOpen());shell.Back();timeUs+=100000;Click(shell,row);assert(shell.Browser().Path()=="C:\\");
        // Holding or moving a press cannot act as the second click.
        timeUs+=1000000;Click(shell,row);auto r=shell.ControlBounds(row);shell.PointerDown(r.left+20,r.top+20);timeUs+=600000;shell.PointerUp(r.left+20,r.top+20);assert(shell.Browser().Path()=="C:\\");
        timeUs+=1000000;Click(shell,row);shell.PointerDown(r.left+20,r.top+20);timeUs+=100000;shell.PointerUp(r.left+40,r.top+20);assert(shell.Browser().Path()=="C:\\");
        timeUs+=1000000;Click(shell,row);timeUs+=100000;Click(shell,row);assert(shell.Browser().Path()=="C:\\A");
        auto music=Row(shell,"music.mp3");timeUs+=1000000;Click(shell,music);assert(shell.ActiveDialog()==Shell::Dialog::None);timeUs+=100000;Click(shell,music);
        assert(shell.ActiveDialog()==Shell::Dialog::MediaPlayer && shell.Media().Status()==rt::MediaPlayer::State::Playing && device.active.size()==4);
        Click(shell,Shell::DialogClose);assert(device.active.empty() && !rt::Audio().Client());
    }assert(rt::Audio().Bind(nullptr));
    puts("Shell double-click PASS (single selection,timed same-path activation,slow/distant/different-item/held/drag/session rejection,real folder navigation and MP3 playback;controller Open preserved)");
}
