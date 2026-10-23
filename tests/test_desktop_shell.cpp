#include "source/ui/desktop_shell.h"
#include "source/ui/font.h"
#include "source/ui/presentation.h"
#include "source/ui/pointer_input.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <unistd.h>

using Shell=ui::DesktopShell;
static void Click(Shell& shell,int target) {
    auto r=Shell::Bounds(target);const int x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;
    shell.PointerDown(x,y);shell.PointerUp(x,y);
}
static void Snapshot(ui::Surface& surface,const char* prefix,const char* name) {
    if(!prefix)return;
    const auto path=std::string(prefix)+"-"+name+".ppm";
    FILE* file=std::fopen(path.c_str(),"wb");assert(file);
    std::fprintf(file,"P6\n%d %d\n255\n",surface.width,surface.height);
    for(int y=0;y<surface.height;++y)for(int x=0;x<surface.width;++x) {
        const auto p=surface.Get(x,y);const unsigned char bytes[]={uint8_t(p>>16),uint8_t(p>>8),uint8_t(p)};
        assert(std::fwrite(bytes,1,3,file)==3);
    }
    std::fclose(file);
}
int main(int argc,char** argv) {
    ui::FontCache::SetDirectory(argc>1?argv[1]:"../../romfs/fonts");
    ui::FontDesc font;font.face="Liberation Sans";font.height=-16;
    // Fail if screenshot accidentally uses the emergency bitmap fallback.
    assert(ui::FontCache::Get(font)->Metrics().faceName=="Liberation Sans");
    assert(!ui::FontCache::Get(font)->Metrics().fixedPitch);
    const char* prefix=argc>2?argv[2]:nullptr;
    Shell shell;shell.Enter();
    assert(shell.ActivePanel()==Shell::Panel::Desktop && shell.Focus()==Shell::Programs);
    assert(!shell.Enabled(Shell::Computer) && !shell.Enabled(Shell::Documents));
    assert(!shell.Enabled(Shell::MenuRun) && !shell.Enabled(Shell::None));
    ui::OwnedSurface image;image.Allocate(Shell::Width,Shell::Height);
    shell.Render(image.s,u"13:42");Snapshot(image.s,prefix,"desktop");
    assert(image.s.Get(800,690)!=image.s.Get(800,620));
    const auto normalStart=image.s.Get(80,710);
    shell.PointerMove(80,700);shell.PointerDown(80,700);shell.Render(image.s,u"13:42");
    assert(normalStart!=image.s.Get(80,710));
    // Release elsewhere cancels, returning to the shell cancels any pending press.
    shell.PointerUp(500,500);assert(!shell.StartOpen());
    shell.PointerDown(80,700);shell.Enter();shell.PointerUp(80,700);assert(!shell.StartOpen());
    Click(shell,Shell::Start);assert(shell.StartOpen());
    shell.Render(image.s,u"13:42");Snapshot(image.s,prefix,"start");
    Click(shell,Shell::MenuComputer);assert(shell.StartOpen()); // Disabled stays inert.
    Click(shell,Shell::About);assert(!shell.StartOpen() && shell.ActivePanel()==Shell::Panel::About);
    shell.Render(image.s,u"13:42");Snapshot(image.s,prefix,"about");
    shell.ToggleStart();shell.Back();
    assert(shell.ActivePanel()==Shell::Panel::About && shell.Focus()==Shell::Close);
    shell.ToggleStart();shell.ToggleStart();assert(shell.Focus()==Shell::Close);
    // Dismissing Start outside the menu also restores the underlying focus.
    shell.ToggleStart();shell.PointerDown(900,200);shell.PointerUp(900,200);
    assert(!shell.StartOpen() && shell.Focus()==Shell::Close);
    shell.Back();shell.Accept();assert(shell.ActivePanel()==Shell::Panel::Programs);
    assert(!shell.Enabled(Shell::Launch));Click(shell,Shell::Launch);assert(shell.LaunchPath().empty());
    shell.Back();assert(shell.ActivePanel()==Shell::Panel::Desktop);
    // B closes a submenu/window before requesting a homebrew exit.
    shell.ToggleStart();shell.Back();assert(!shell.StartOpen() && !shell.ExitRequested());

    char pattern[]="/tmp/sr-shell-XXXXXX";const char* dir=mkdtemp(pattern);assert(dir);
    std::vector<std::string> paths;
    for(int i=0;i<35;++i) {
        char name[32];std::snprintf(name,sizeof(name),"/app-%02d.exe",i);
        paths.push_back(std::string(dir)+name);
        FILE* f=std::fopen(paths.back().c_str(),"wb");assert(f);std::fclose(f);
    }
    paths.push_back(std::string(dir)+"/тест-文件.exe");
    FILE* unicode=std::fopen(paths.back().c_str(),"wb");assert(unicode);std::fclose(unicode);
    ui::ExecutableCatalog catalog;catalog.ScanRoot(std::string(dir)+"/",0);
    shell.SetCatalog(catalog);assert(shell.Count()==36);
    shell.Enter();Click(shell,Shell::Programs);assert(shell.Selection()==0);
    shell.Move(-1);assert(shell.Selection()==35);shell.Move(1);assert(shell.Selection()==0);
    shell.Page(1);assert(shell.Selection()==14);assert(shell.Enabled(Shell::Previous));
    shell.Page(100);assert(shell.Selection()==28 && !shell.Enabled(Shell::Next));
    Click(shell,Shell::RowBase+7);assert(shell.Selection()==35 && shell.LaunchPath().empty());
    shell.Render(image.s,u"13:42");Snapshot(image.s,prefix,"programs");
    shell.Accept();assert(shell.LaunchPath()==catalog.Files()[35]);
    const auto launch=shell.LaunchPath();
    shell.SetCatalog(catalog);shell.Enter();assert(shell.Selection()==35 && shell.LaunchPath().empty());
    assert(shell.ActivePanel()==Shell::Panel::Programs); // Same host model on guest return.
    Click(shell,Shell::Launch);assert(shell.LaunchPath()==launch);
    shell.Enter();shell.Page(-100);shell.Page(1);Click(shell,Shell::RowBase+3);
    assert(shell.Selection()==17);
    const int rowFocus=shell.Focus();shell.ToggleStart();shell.ToggleStart();assert(shell.Focus()==rowFocus);
    shell.Accept();assert(shell.LaunchPath()==catalog.Files()[17]);
    shell.Enter();shell.Back();shell.ToggleStart();shell.Move(1);shell.Accept();
    assert(shell.ActivePanel()==Shell::Panel::About); // Keyboard Start -> About.
    shell.Back();shell.ToggleStart();shell.Move(2);shell.Accept();assert(shell.ExitRequested());
    shell.Enter();assert(!shell.ExitRequested());

    // Physical/scaled output and touch share the actual presentation viewport.
    // Guards assert pitch padding is untouched; letterbox cannot hit Start.
    shell.Render(image.s,u"13:42");
    ui::SoftwarePresentation backend;ui::Presentation presentation(&backend);
    assert(presentation.Submit(image.s));
    for(auto size:{std::pair<int,int>{1280,720},{800,600},{640,360}}) {
        const int w=size.first,h=size.second,stride=w+7;
        std::vector<uint32_t> output(size_t(stride)*h+8,0xABCD1234u);
        assert(presentation.Render({output.data(),w,h,size_t(stride),size_t(stride)*h}));
        const auto vp=ui::FitViewport(Shell::Width,Shell::Height,w,h);
        const auto start=Shell::Bounds(Shell::Start);
        const int px=vp.x+((start.left+start.right)/2)*vp.width/Shell::Width;
        const int py=vp.y+((start.top+start.bottom)/2)*vp.height/Shell::Height;
        float sx=0,sy=0;assert(vp.Contains(px,py));assert(vp.ToSource(px,py,Shell::Width,Shell::Height,sx,sy));
        assert(shell.HitTest(int(sx),int(sy))==Shell::Start);
        if(h==600){assert(!vp.Contains(0,0));assert(output[0]==0xFF000000u);}
        for(int y=0;y<h;++y)for(int x=w;x<stride;++x)assert(output[size_t(y)*stride+x]==0xABCD1234u);
        for(size_t i=size_t(stride)*h;i<output.size();++i)assert(output[i]==0xABCD1234u);
    }
    // Launch/return quarantine: no pressed A or touch is replayed to a fresh screen.
    ui::PointerInputGate gate;gate.Reset(1,true);
    auto edges=gate.Update(1,true);assert(!edges.down && !edges.touching);
    gate.Update(0,false);edges=gate.Update(1,true);assert(edges.down==1 && edges.touching);
    for(const auto& path:paths)assert(unlink(path.c_str())==0);
    assert(rmdir(dir)==0);
    std::puts("Desktop shell: native render, licensed font/art, menu/selection/return, paging and scaled input PASS");
}
