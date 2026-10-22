// Standard 32-bit CHOOSECOLOR ABI, shared by ANSI/Unicode callers. All colors
// crossing the API are COLORREF; CPU GDI retains painting/ROP semantics.
#include "user32_controls.h"
#include "../runtime/guest_execution.h"
#include <array>
#include <algorithm>
#include <map>
namespace win32 {
void SetCommonDialogError(uint32_t error);
namespace {
using namespace user;
using namespace hle;
constexpr uint32_t CC_RGBINIT=1,CC_FULLOPEN=2,CC_PREVENTFULLOPEN=4,CC_SHOWHELP=8,CC_ENABLEHOOK=16;
constexpr uint32_t CC_ENABLETEMPLATE=32,CC_ENABLETEMPLATEHANDLE=64;
constexpr uint32_t RED_ID=706,GREEN_ID=707,BLUE_ID=708,DEFINE_ID=719,ADD_ID=712;
// RGB literals are converted only at the API boundary.
constexpr uint32_t basic[]={
    0xFF8080,0xFFFF80,0x80FF80,0x00FF80,0x80FFFF,0x0080FF,0xFF80C0,0xFF80FF,
    0xFF0000,0xFFFF00,0x80FF00,0x00FF40,0x00FFFF,0x0080C0,0x8080C0,0xFF00FF,
    0x804040,0xFF8040,0x00FF00,0x008080,0x004080,0x8080FF,0x800040,0xFF0080,
    0x800000,0xFF8000,0x008000,0x008040,0x0000FF,0x0000A0,0x800080,0x8000FF,
    0x400000,0x804000,0x004000,0x004040,0x000080,0x000040,0x400040,0x400080,
    0x000000,0x808000,0x808040,0x808080,0x408080,0xC0C0C0,0x400040,0xFFFFFF};
struct ColorRequest {
    cpu::IGuestMemory* memory=nullptr;
    uint32_t pointer=0,owner=0,customPointer=0,flags=0,hook=0,original=0,color=0;
    uint32_t hwnd=0,edits[3]={},advanced[8]={};
    std::array<uint32_t,16> custom{};
    int selected=-1,nextCustom=0;
    bool wide=false,done=false,accepted=false,expanded=false,inHook=false;
};
std::map<uint32_t,ColorRequest*>& Colors() {static std::map<uint32_t,ColorRequest*> requests;return requests;}
bool Alive(const ColorRequest& r) {return Win(r.hwnd) && !g_guestExitRequested && rt::GuestExecution::Checkpoint();}
uint32_t Hook(ColorRequest& r,uint32_t message,uint32_t wp,uint32_t lp) {
    if(!r.hook || r.inHook || !Alive(r))return 0;
    struct HookScope {bool& value;~HookScope(){value=false;}} scope{r.inHook};
    r.inHook=true;
    return CallProc(r.hook,r.wide,r.hwnd,message,wp,lp,r.wide);
}
void Select(ColorRequest& r,uint32_t color) {
    r.color=color&0xFFFFFF;
    for(int n=0;n<3;++n)if(auto* w=Win(r.edits[n])) {
        const auto value=std::to_string((r.color>>(8*n))&255);
        w->text=AnsiToWide(value);Redraw(w->hwnd);
    }
    if(auto* w=Win(r.hwnd))InvalidateClient(w,ui::Region(ui::Rect(0,0,w->client.Width(),w->client.Height())),false,false);
}
uint32_t Child(ColorRequest& r,const char16_t* cls,const char16_t* title,uint32_t id,
               int x,int y,int w,int h,uint32_t style=0) {
    return CreateWindowInternal(0,cls,0,title,WS_CHILD|WS_VISIBLE|style,x,y,w,h,r.hwnd,id,0,0,true,0,0);
}
void Layout(ColorRequest& r) {
    auto* top=Win(r.hwnd);if(!top)return;
    const int width=r.expanded?580:284,height=(r.flags&CC_SHOWHELP)?454:420;
    SetWindowPosInternal(r.hwnd,0,std::max(0,std::min(top->rect.left,S().screenW-width)),
        std::max(0,std::min(top->rect.top,S().screenH-height)),width,height,0x14);
    for(auto h:r.advanced)if(h)ShowWindowInternal(h,r.expanded?5:0);
    for(auto h:r.edits)if(h)ShowWindowInternal(h,r.expanded?5:0);
    if(!Win(r.hwnd))return;
    const auto children=Win(r.hwnd)->children;
    for(uint32_t h:children)if(auto* w=Win(h)) {
        if(w->id==1 || w->id==2)SetWindowPosInternal(h,0,
            (r.expanded?316:12)+(w->id==2?126:0),350,110,28,0x14);
    }
    if(auto* w=Win(r.hwnd))InvalidateClient(w,ui::Region(ui::Rect(0,0,w->client.Width(),w->client.Height())),true,true);
}
void Fill(uint32_t dc,ui::Rect rect,uint32_t colorref) {
    const auto brush=ui::g_gdi.CreateSolidBrush(colorref);
    ui::g_gdi.FillRect(dc,rect,brush);ui::g_gdi.DeleteObject(brush);
}
void Paint(ColorRequest& r) {
    auto* w=Win(r.hwnd);if(!w)return;
    if(w->ncDirty)DrawNonClient(w);
    const auto dc=ui::g_gdi.CreateDc(ui::DcKind::Window,r.hwnd,false);
    ui::g_gdi.FillRect(dc,ui::Rect(0,0,w->client.Width(),w->client.Height()),SysColorBrush(15));
    for(int i=0;i<64;++i) {
        int x=12+(i%8)*30,y=i<48?40+(i/8)*28:252+((i-48)/8)*28;
        ui::Rect box(x,y,x+24,y+21);
        Fill(dc,box,i<48?ui::RgbToColorref(basic[i]):r.custom[i-48]);
        Edge3D(dc,box,16,20,16,20);
        if(r.selected==i)FocusRect(dc,ui::Rect(x-3,y-3,x+27,y+24));
    }
    if(r.expanded) {Fill(dc,ui::Rect(316,40,552,108),r.color);Edge3D(dc,{316,40,552,108},16,20,16,20);}
    ui::g_gdi.DeleteDc(dc);
    if((w=Win(r.hwnd))) {w->update.Clear();w->erase=false;w->paintAnnounced=false;}
}
void Accept(ColorRequest& r) {
    // COLOROKSTRING sees the proposed rgbResult and may veto it.
    const auto previous=r.memory->Read32(r.pointer+12);
    r.memory->Write32(r.pointer+12,r.color);
    const auto veto=Hook(r,RegisterMessage(u"commdlg_ColorOK"),0,r.pointer);
    if(!Alive(r))return;
    if(veto) {r.memory->Write32(r.pointer+12,previous);return;}
    r.color=r.memory->Read32(r.pointer+12)&0xFFFFFF;r.accepted=true;r.done=true;
}
uint32_t ColorProc(uint32_t hwnd,uint32_t msg,uint32_t wp,uint32_t lp,bool unicode) {
    auto found=Colors().find(hwnd);
    if(found==Colors().end())return DefWindowProc(hwnd,msg,wp,lp,unicode);
    auto& r=*found->second;
    // user::WM_INITDIALOG is delivered once, after constructing all standard controls.
    if(msg!=user::WM_INITDIALOG) {
        const auto handled=Hook(r,msg,wp,lp);
        if(!Alive(r) || handled)return handled;
    }
    if(msg==RegisterMessage(u"commdlg_SetRGBColor")) {Select(r,lp);return 1;}
    switch(msg) {
    case user::WM_CLOSE:r.done=true;return 0;
    case user::WM_NCDESTROY:r.done=true;return 0;
    case user::WM_ERASEBKGND:return 1;
    case user::WM_PAINT:Paint(r);return 0;
    case user::WM_LBUTTONDOWN: {
        const int x=static_cast<int16_t>(lp),y=static_cast<int16_t>(lp>>16);
        for(int i=0;i<64;++i) {
            const int bx=12+(i%8)*30,by=i<48?40+(i/8)*28:252+((i-48)/8)*28;
            if(x>=bx && x<bx+24 && y>=by && y<by+21) {
                r.selected=i;if(i>=48)r.nextCustom=i-48;
                Select(r,i<48?ui::RgbToColorref(basic[i]):r.custom[i-48]);
                SetFocusInternal(hwnd);break;
            }
        }
        return 0;
    }
    case user::WM_KEYDOWN: {
        int delta=wp==37?-1:wp==39?1:wp==38?-8:wp==40?8:0;
        if(delta) {r.selected=std::max(0,std::min(63,(r.selected<0?0:r.selected)+delta));
            Select(r,r.selected<48?ui::RgbToColorref(basic[r.selected]):r.custom[r.selected-48]);return 0;}
        break;
    }
    case user::WM_COMMAND: {
        const auto id=wp&0xFFFF,code=wp>>16;
        if(id==1) {Accept(r);return 0;}
        if(id==2 || id==3) {r.done=true;return 0;} // IDCANCEL / IDABORT
        if(id==DEFINE_ID && !(r.flags&CC_PREVENTFULLOPEN)) {r.expanded=true;Layout(r);return 0;}
        if(id==ADD_ID && r.expanded) {
            const int slot=r.nextCustom;r.custom[slot]=r.color;r.selected=48+slot;
            if(r.customPointer)r.memory->Write32(r.customPointer+slot*4,r.color);
            r.nextCustom=(slot+1)%16;Select(r,r.color);return 0;
        }
        if(id>=RED_ID && id<=BLUE_ID && code==0x300) {
            uint32_t color=0;
            for(int i=0;i<3;++i) {
                auto* edit=Win(r.edits[i]);if(!edit || edit->text.empty())return 0;
                unsigned value=0;for(char16_t ch:edit->text) {if(ch<u'0' || ch>u'9')return 0;value=value*10+ch-u'0';if(value>255)return 0;}
                color|=value<<(8*i);
            }
            r.color=color;if(auto* w=Win(hwnd))InvalidateClient(w,ui::Region({316,40,552,108}),false,false);
            return 0;
        }
        if(id==0x40E) {if(Win(r.owner))SendMsg(r.owner,RegisterMessage(u"commdlg_help"),hwnd,r.pointer);return 0;}
        break;
    }
    default:break;
    }
    return DefWindowProc(hwnd,msg,wp,lp,unicode);
}
HostCallOutcome HostColorProc(cpu::CpuState& c,cpu::IGuestMemory& m) {
    return Ret(ColorProc(Arg(c,m,0),Arg(c,m,1),Arg(c,m,2),Arg(c,m,3),true));
}
HostCallOutcome ChooseColor(cpu::CpuState& c,cpu::IGuestMemory& m,bool wide) {
    SetCommonDialogError(0);
    const uint32_t p=Arg(c,m,0);
    if(!p) {SetCommonDialogError(2);return Ret(0);}
    if(m.Read32(p)!=36) {SetCommonDialogError(1);return Ret(0);}
    ColorRequest r;r.memory=&m;r.pointer=p;r.owner=m.Read32(p+4);r.original=m.Read32(p+12);
    r.customPointer=m.Read32(p+16);r.flags=m.Read32(p+20);r.wide=wide;
    r.hook=(r.flags&CC_ENABLEHOOK)?m.Read32(p+28):0;
    if((r.flags&CC_ENABLEHOOK) && !r.hook) {SetCommonDialogError(0xB);return Ret(0);}
    // Custom dialog templates are not silently substituted with the standard UI.
    if(r.flags&(CC_ENABLETEMPLATE|CC_ENABLETEMPLATEHANDLE)) {SetCommonDialogError(3);return Ret(0);}
    if(!IsHostLayerEnabled(kUiLayer)) {SetCommonDialogError(2);return Ret(0);}
    const auto oldContext=G();G()={&c,&m};
    struct ContextScope {GuestCtx old;~ContextScope(){G()=old;}} context{oldContext};
    r.color=(r.flags&CC_RGBINIT)?r.original&0xFFFFFF:0;
    r.expanded=(r.flags&CC_FULLOPEN) && !(r.flags&CC_PREVENTFULLOPEN);
    for(int i=0;i<16;++i)r.custom[i]=r.customPointer?m.Read32(r.customPointer+4*i)&0xFFFFFF:0xFFFFFF;
    const int index=FindHostFunction("COMDLG32.dll","__ColorDialogWndProc");
    if(index<0) {SetCommonDialogError(2);return Ret(0);}
    const auto address=IndexToHostAddress(index);RegisterHostWndProc(address,&ColorProc,true);
    WndClass cls;cls.name=u"SwitchCommonColorDialog";cls.wndProc=address;cls.unicode=true;
    cls.hbrBackground=SysColorBrush(15);cls.cbWndExtra=32;RegisterClassInternal(cls);
    const int width=r.expanded?580:284,height=(r.flags&CC_SHOWHELP)?454:420;
    r.hwnd=CreateWindowInternal(WS_EX_DLGMODALFRAME,cls.name,0,u"Color",WS_POPUP|WS_CAPTION|WS_SYSMENU,
        std::max(0,(S().screenW-width)/2),std::max(0,(S().screenH-height)/2),width,height,r.owner,0,0,0,true,0,0);
    if(!r.hwnd) {SetCommonDialogError(2);return Ret(0);}
    Colors()[r.hwnd]=&r;
    const uint32_t previousFocus=Q().focus;
    const bool ownerEnabled=Win(r.owner) && !(Win(r.owner)->style&WS_DISABLED);
    struct ModalScope {
        ColorRequest& r;bool ownerEnabled;uint32_t focus;
        ~ModalScope() {
            if(rt::GuestExecution::Checkpoint())DestroyWindowInternal(r.hwnd);
            Colors().erase(r.hwnd);
            if(ownerEnabled)if(auto* owner=Win(r.owner))owner->style&=~WS_DISABLED;
            if(rt::GuestExecution::Checkpoint()) {
                if(Win(r.owner))SetActive(r.owner);
                if(Win(focus))SetFocusInternal(focus);
            }
        }
    } modal{r,ownerEnabled,previousFocus};
    if(ownerEnabled)Win(r.owner)->style|=WS_DISABLED;
    Child(r,u"Static",u"Basic colors:",720,12,12,244,22);
    Child(r,u"Static",u"Custom colors:",721,12,224,244,22);
    const auto define=Child(r,u"Button",u"Define custom colors",DEFINE_ID,12,310,236,28,0x10000);
    if(r.flags&CC_PREVENTFULLOPEN)if(auto* w=Win(define))w->style|=WS_DISABLED;
    const char16_t* labels[]={u"Red:",u"Green:",u"Blue:"};
    for(int i=0;i<3;++i) {
        r.advanced[i]=Child(r,u"Static",labels[i],730+i,316,130+i*42,100,22);
        r.edits[i]=Child(r,u"Edit",u"",RED_ID+i,430,126+i*42,90,26,WS_BORDER|0x2000|0x10000);
        SendMsg(r.edits[i],0xC5,3,0); // EM_SETLIMITTEXT: bounded RGB input
    }
    r.advanced[3]=Child(r,u"Button",u"Add to custom colors",ADD_ID,316,270,236,28,0x10000);
    if(r.flags&CC_SHOWHELP)Child(r,u"Button",u"Help",0x40E,12,384,90,28,0x10000);
    Child(r,u"Button",u"OK",1,0,0,110,28,0x10001);
    Child(r,u"Button",u"Cancel",2,0,0,110,28,0x10000);
    Layout(r);Select(r,r.color);ShowWindowInternal(r.hwnd,5);SetFocusInternal(r.hwnd);
    Hook(r,user::WM_INITDIALOG,r.edits[0],p);
    while(Alive(r) && !r.done) {
        Msg msg;if(!FetchMessage(msg,0,0,0,true,true))break;
        if(msg.message==user::WM_QUIT) {PostQuit(msg.wParam);break;}
        if(!Alive(r))break;
        if(DialogMessage(r.hwnd,msg))continue;
        if(msg.message==user::WM_TIMER && msg.lParam) {
            CallStdcall(c,m,msg.lParam,{msg.hwnd,user::WM_TIMER,msg.wParam,NowMs()});continue;
        }
        TranslateKey(msg);if(msg.hwnd)SendMsg(msg.hwnd,msg.message,msg.wParam,msg.lParam);
    }
    const bool accepted=r.accepted && Alive(r);
    if(!g_guestExitRequested && rt::GuestExecution::Checkpoint())m.Write32(p+12,accepted?r.color:r.original);
    return Ret(accepted?1:0);
}
HostCallOutcome ChooseColorA(cpu::CpuState& c,cpu::IGuestMemory& m) {return ChooseColor(c,m,false);}
HostCallOutcome ChooseColorW(cpu::CpuState& c,cpu::IGuestMemory& m) {return ChooseColor(c,m,true);}
const HostFunctionEntry exports[]={
    {"COMDLG32.dll","ChooseColorA",4,CallConv::Stdcall,&ChooseColorA},
    {"COMDLG32.dll","ChooseColorW",4,CallConv::Stdcall,&ChooseColorW},
    {"COMDLG32.dll","__ColorDialogWndProc",16,CallConv::Stdcall,&HostColorProc},
};
const bool registered=RegisterHostFunctions(exports,sizeof(exports)/sizeof(exports[0]));
} // namespace
} // namespace win32
