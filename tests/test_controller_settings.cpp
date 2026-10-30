#include "source/ui/desktop_shell.h"
#include "source/win32/shell_session.h"
#include "source/runtime/guest_session.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <algorithm>
namespace fs=std::filesystem;
namespace u=win32::user;
using Shell=ui::DesktopShell;using Store=ui::ShellPreferenceStore;
static std::vector<u::InputEvent> source;
static bool alive=true;
static bool Input(int,std::vector<u::InputEvent>& out){out.swap(source);return alive;}
static void Click(Shell& shell,int target){const auto r=shell.ControlBounds(target);shell.PointerDown(r.left+8,r.top+8);shell.PointerUp(r.left+8,r.top+8);}
static void OpenSettings(Shell& shell){shell.ToggleStart();Click(shell,Shell::Settings);assert(shell.ActiveDialog()==Shell::Dialog::Preferences);Click(shell,Shell::ControllerSettings);assert(shell.ActiveDialog()==Shell::Dialog::Bindings);}
static void ChooseKey(Shell& shell,uint16_t from,uint16_t to){const auto& keys=ui::ControllerKeyChoices();const auto a=std::find(keys.begin(),keys.end(),from),b=std::find(keys.begin(),keys.end(),to);assert(a!=keys.end()&&b!=keys.end());int count=int(b-a);for(int n=0;n<std::abs(count);++n)shell.Page(count>0?1:-1);}
static std::string Bytes(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
static void Write(const fs::path& p,const std::string& b){std::ofstream f(p,std::ios::binary|std::ios::trunc);f.write(b.data(),b.size());assert(f.good());}
struct Partial:rt::FileCopyIo{size_t written=0;ssize_t Write(int fd,const void* p,size_t n)override{if(written>=11){errno=ENOSPC;return -1;}const auto count=FileCopyIo::Write(fd,p,std::min(n,size_t(11)));if(count>0)written+=size_t(count);return count;}};
int main(int argc,char** argv){
    ui::FontCache::SetDirectory(fs::exists("romfs/fonts")?"romfs/fonts":"../../romfs/fonts");
    char pattern[]="/tmp/sr-pad-settings-XXXXXX";assert(mkdtemp(pattern));const fs::path base=pattern,root=base/"system",sd=base/"sd",rom=base/"rom";
    struct Cleanup{fs::path path;~Cleanup(){fs::remove_all(path);}}cleanup{base};fs::create_directory(sd);fs::create_directory(rom);
    rt::Vfs v;std::string error;assert(v.ConfigurePersistent(root.string(),sd.string(),rom.string(),error));
    const auto dir=root/"Documents and Settings/Player/Application Data/SwitchRecompiler";fs::create_directories(dir);
    // Independently encoded actual version1 fixture: generation42,sort1,txt association.
    const std::string hex="53525052454630312a0000000000000029000000bebbc42b01000000010000000300000074787416000000433a5c50726f6772616d735c7265616465722e657865";
    std::string old;for(size_t n=0;n<hex.size();n+=2)old.push_back(char(std::stoul(hex.substr(n,2),nullptr,16)));
    Write(dir/"prefs-0.bin",old);Store store;ui::ShellPreferences prefs;
    assert(store.Load(v,prefs)==Store::Status::Loaded && prefs.sort==1 && prefs.associations[0].extension=="txt" && prefs.controller==ui::ControllerBindings{});
    prefs.controller.keys[0]='Z';assert(store.Save(v,prefs));
    assert(Bytes(dir/"prefs-0.bin")==old && Bytes(dir/"prefs-1.bin").substr(0,8)=="SRPREF02");
    ui::ShellPreferences loaded;assert(store.Load(v,loaded)==Store::Status::Loaded && loaded.controller==prefs.controller && loaded.sort==1 && loaded.associations.size()==1);
    const auto good=Bytes(dir/"prefs-1.bin");auto changed=prefs;changed.controller.keys[0]=0x71;Partial fault;
    assert(!store.Save(v,changed,&fault) && Bytes(dir/"prefs-1.bin")==good);
    assert(store.Load(v,loaded)==Store::Status::Recovered && loaded.controller==prefs.controller);
    auto invalid=changed;invalid.controller.keys[0]=0x5b;assert(!store.Save(v,invalid) && errno==EINVAL);
    // Real settings UI: draft changes,explicit apply,save and fresh shell restore.
    Shell shell;assert(shell.ConfigureFilesystem(root.string(),sd.string(),rom.string(),error));shell.Enter();
    OpenSettings(shell);ChooseKey(shell,'Z',0x71);assert(shell.Browser().KeyBindings().keys[0]=='Z');
    Click(shell,Shell::BindingApply);assert(shell.Browser().KeyBindings().keys[0]==0x71 && shell.Browser().PreferenceStatus()==Store::Status::Applied);
    Shell beforeSave;assert(beforeSave.ConfigureFilesystem(root.string(),sd.string(),rom.string(),error) && beforeSave.Browser().KeyBindings().keys[0]=='Z');
    Click(shell,Shell::BindingSave);assert(shell.Browser().PreferenceStatus()==Store::Status::Saved);
    Shell restored;assert(restored.ConfigureFilesystem(root.string(),sd.string(),rom.string(),error) && restored.Browser().KeyBindings().keys[0]==0x71);
    assert(restored.Browser().Ordering()==ui::ShellBrowser::Sort::NameDescending && restored.Browser().Associations().size()==1);
    Click(shell,Shell::BindingReset);assert(shell.Browser().KeyBindings().keys[0]==0x71);Click(shell,Shell::BindingApply);assert(shell.Browser().KeyBindings()==ui::ControllerBindings{});
    Click(shell,Shell::BindingArcade);assert(shell.Browser().KeyBindings()==ui::ControllerBindings{});
    Click(shell,Shell::BindingSave);const auto arcade=shell.Browser().KeyBindings();
    assert(arcade.keys[0]==0x20 && arcade.keys[1]==0xbf && arcade.keys[2]==0x71 && arcade.keys[3]=='Z' && arcade.keys[5]==0x1b && arcade.keys[11]==0);
    Shell arcadeRestore;assert(arcadeRestore.ConfigureFilesystem(root.string(),sd.string(),rom.string(),error) && arcadeRestore.Browser().KeyBindings()==arcade);
    Click(shell,Shell::BindingReset);Click(shell,Shell::BindingApply);
    // Controller focus can reach every action,including Save and Back.
    for(int n=0;n<8 && shell.Focus()!=Shell::BindingSave;++n)shell.NextFocus();
    assert(shell.Focus()==Shell::BindingSave);shell.Accept();
    ChooseKey(shell,0x1b,'Z');Click(shell,Shell::BindingSave);
    const char* capture=argc>1?argv[1]:std::getenv("CONTROLLER_SETTINGS_CAPTURE");
    if(capture){ui::OwnedSurface image;image.Allocate(1280,720);assert(image.s.bits);shell.Render(image.s,u"12:00");FILE* f=std::fopen(capture,"wb");assert(f);std::fprintf(f,"P6\n1280 720\n255\n");for(int y=0;y<720;++y)for(int x=0;x<1280;++x){auto c=image.s.Get(x,y);uint8_t rgb[]={uint8_t(c>>16),uint8_t(c>>8),uint8_t(c)};assert(std::fwrite(rgb,1,3,f)==3);}assert(!std::fclose(f));}
    Click(shell,Shell::CancelDialog);assert(shell.ActiveDialog()==Shell::Dialog::Preferences);Click(shell,Shell::CancelDialog);
    // Actual production source/filter/mapper/ring,including native B while rebound.
    rt::GuestSession session;u::EnableUi(1280,720);const auto saved=u::g_platform;u::g_platform.wait=Input;
    {
        u::ShellSession bridge(shell);assert(bridge.Attach("Public bindings route"));
        auto send=[&](uint32_t vk,bool down,uint8_t pad=0){u::InputEvent event;event.type=down?u::InputEvent::KeyDown:u::InputEvent::KeyUp;event.vk=vk;event.padButton=pad;source.push_back(event);assert(bridge.ServiceHost()==u::ShellSession::HostStatus::Running);};
        auto drain=[&](){std::vector<u::InputEvent> out;u::g_platform.wait(0,out);return out;};
        send(0x1b,true,1);auto out=drain();assert(out.size()==1 && out[0].vk=='Z' && (u::S().asyncKeys['Z']&0x80));
        send(0x5b,true);out=drain();assert(shell.StartOpen() && out.size()==1 && out[0].vk=='Z' && out[0].type==u::InputEvent::KeyUp && !(u::S().asyncKeys['Z']&0x80));send(0x5b,false);
        send(0x1b,false,1);assert(drain().empty()); // release old quarantined guest press
        send(0x1b,true,1);assert(!shell.StartOpen() && drain().empty()); // native Escape,not rebound Z
        send(0x1b,false,1);assert(drain().empty()); // closing press cannot leak into guest
        send(0x1b,true,1);out=drain();assert(out.size()==1 && out[0].vk=='Z');send(0x1b,false,1);out=drain();assert(out.size()==1 && out[0].type==u::InputEvent::KeyUp);
        // Closing a settings modal through controller B must retain native ownership
        // in the parent preferences dialog,then return to guest after the next Back.
        send(0x5b,true);send(0x5b,false);Click(shell,Shell::Settings);Click(shell,Shell::ControllerSettings);
        send(0x1b,true,1);assert(shell.ActiveDialog()==Shell::Dialog::Preferences && drain().empty());send(0x1b,false,1);
        send(0x1b,true,1);assert(shell.ActiveDialog()==Shell::Dialog::None && drain().empty());send(0x1b,false,1);
        send(0x1b,true,1);assert(drain().size()==1);alive=false;assert(bridge.ServiceHost()==u::ShellSession::HostStatus::SourceClosed);
        assert(!(u::S().asyncKeys['Z']&0x80));
        out=drain();assert(out.size()==1 && out[0].vk=='Z' && out[0].type==u::InputEvent::KeyUp);
    }
    alive=true;source.clear();u::g_platform=saved;
    // Two edited physical bindings share one real guest key ownership count.
    OpenSettings(shell);shell.Move(1);ChooseKey(shell,0x0d,'Z');Click(shell,Shell::BindingApply);Click(shell,Shell::CancelDialog);Click(shell,Shell::CancelDialog);
    u::g_platform.wait=Input;
    {
        u::ShellSession bridge(shell);assert(bridge.Attach("Public duplicate/overflow route"));
        auto send=[&](uint8_t slot,bool down){u::InputEvent event;event.type=down?u::InputEvent::KeyDown:u::InputEvent::KeyUp;event.padButton=slot;event.vk=slot==1?0x1b:0x0d;source.push_back(event);assert(bridge.ServiceHost()==u::ShellSession::HostStatus::Running);};
        send(1,true);send(2,true);send(1,false);assert((u::S().asyncKeys['Z']&0x80) && bridge.PendingGuestInput()==1);
        send(2,false);std::vector<u::InputEvent> out;assert(u::g_platform.wait(0,out) && out.size()==2 && out[0].type==u::InputEvent::KeyDown && out[1].type==u::InputEvent::KeyUp);
        for(size_t n=0;n<u::ShellSession::MaxGuestInput;++n){u::InputEvent event;event.type=n%2?u::InputEvent::KeyUp:u::InputEvent::KeyDown;event.vk='Q';source.push_back(event);}
        assert(bridge.ServiceHost()==u::ShellSession::HostStatus::Running && bridge.PendingGuestInput()==u::ShellSession::MaxGuestInput);
        u::InputEvent event;event.type=u::InputEvent::KeyDown;event.padButton=1;event.vk=0x1b;source.push_back(event);
        assert(bridge.ServiceHost()==u::ShellSession::HostStatus::InputOverflow && !(u::S().asyncKeys['Z']&0x80));
    }
    source.clear();u::g_platform=saved;
    // Future versions are not erased,including binding-save UI errors.
    Write(dir/"prefs-1.bin","SRPREF03future");OpenSettings(shell);ChooseKey(shell,'Z',0x71);Click(shell,Shell::BindingSave);
    assert(shell.Browser().PreferenceStatus()==Store::Status::SaveFailed && shell.Browser().KeyBindings().keys[0]==0x71 && Bytes(dir/"prefs-1.bin")=="SRPREF03future");
    puts("Controller settings PASS (actual v1->v2 migration/preserved sort+associations,partial-write recovery,future protection,real editor draft/apply/save/reset/controller focus/fresh restore;production native-vs-guest B routing,held context quarantine,paired source-close releases,actual duplicate owners and mapped-input overflow cancellation)");
}
