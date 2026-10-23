#include "desktop_shell.h"
#include "font.h"
#include "shell_icons.h"
#include <algorithm>
#include <cstdio>

namespace ui {
namespace {
std::u16string Wide(const std::string& text) {
    std::u16string out;
    for(size_t i=0;i<text.size();) {
        const auto a=static_cast<unsigned char>(text[i++]);uint32_t cp=a;int count=0;
        if(a>=0xC2 && a<=0xDF){cp=a&31;count=1;}
        else if(a>=0xE0 && a<=0xEF){cp=a&15;count=2;}
        else if(a>=0xF0 && a<=0xF4){cp=a&7;count=3;}
        else if(a>=128)cp=0xFFFD;
        bool valid=i+count<=text.size();
        for(int n=0;n<count && valid;++n) {
            auto b=static_cast<unsigned char>(text[i+n]);
            if((b&0xC0)!=0x80)valid=false;else cp=(cp<<6)|(b&63);
        }
        if(valid && count) {
            i+=count;
            if(cp<(count==1?0x80u:count==2?0x800u:0x10000u) || cp>0x10FFFF || (cp>=0xD800 && cp<=0xDFFF))cp=0xFFFD;
        } else if(!valid)cp=0xFFFD;
        if(cp<32 || cp==127)cp=' ';
        if(cp>0xFFFF){cp-=0x10000;out.push_back(char16_t(0xD800+(cp>>10)));cp=0xDC00+(cp&1023);}
        out.push_back(char16_t(cp));
    }
    return out;
}
void Fill(Surface& s,Rect r,uint32_t c) {
    r=r.Intersect({0,0,s.width,s.height});
    for(int y=r.top;y<r.bottom;++y)for(int x=r.left;x<r.right;++x)s.Set(x,y,c);
}
void Frame(Surface& s,Rect r,uint32_t c) {
    Fill(s,{r.left,r.top,r.right,r.top+1},c);Fill(s,{r.left,r.bottom-1,r.right,r.bottom},c);
    Fill(s,{r.left,r.top,r.left+1,r.bottom},c);Fill(s,{r.right-1,r.top,r.right,r.bottom},c);
}
uint32_t Mix(uint32_t a,uint32_t b,int n,int d) {
    uint32_t out=0;
    for(int shift=0;shift<=16;shift+=8)out|=uint32_t((int((a>>shift)&255)*(d-n)+int((b>>shift)&255)*n)/d)<<shift;
    return out;
}
void Gradient(Surface& s,Rect r,uint32_t a,uint32_t b) {
    const int height=std::max(1,r.Height()-1);
    for(int y=r.top;y<r.bottom;++y)Fill(s,{r.left,y,r.right,y+1},Mix(a,b,y-r.top,height));
}
void Text(Surface& s,Rect r,std::u16string text,uint32_t color=0x182943,int size=16,bool bold=false) {
    FontDesc desc;desc.face="Liberation Sans";desc.height=-size;desc.weight=bold?700:400;
    auto* font=FontCache::Get(desc);
    // Keep clipping bounded, including UTF-16 surrogate pairs at the cut.
    if(font->MeasureWidth(text)>r.Width()) {
        while(!text.empty() && font->MeasureWidth(text+u"…")>r.Width()) {
            const auto c=text.back();text.pop_back();
            if(c>=0xDC00 && c<=0xDFFF && !text.empty())text.pop_back();
        }
        text+=u"…";
    }
    font->Draw(s,Region(r),r.left,r.top,text,color);
}
void Icon(Surface& s,int x,int y,const uint32_t* pixels) {
    for(int iy=0;iy<32;++iy)for(int ix=0;ix<32;++ix) {
        const int px=x+ix,py=y+iy;
        if(px<0 || py<0 || px>=s.width || py>=s.height)continue;
        const auto c=pixels[iy*32+ix];const unsigned alpha=c>>24;
        if(alpha)s.Set(px,py,Mix(s.Get(px,py),c,alpha,255));
    }
}
} // namespace

Rect DesktopShell::Bounds(int t) {
    switch(t) {
    case Start:return {0,680,142,720};
    case Programs:return {20,26,132,116};
    case Computer:return {20,132,132,222};
    case Documents:return {20,238,132,328};
    case MenuPrograms:return {16,370,234,422};
    case MenuComputer:return {250,370,466,414};
    case MenuDocuments:return {250,422,466,466};
    case MenuRun:return {250,474,466,518};
    case About:return {16,430,234,482};
    case Exit:return {266,632,466,668};
    case Close:return {1118,108,1150,138};
    case Launch:return {954,578,1138,614};
    case Previous:return {240,578,364,614};
    case Next:return {374,578,498,614};
    default:
        if(t>=RowBase && t<RowBase+Rows)return {240,184+(t-RowBase)*26,1138,210+(t-RowBase)*26};
        return {};
    }
}
void DesktopShell::SetCatalog(const ExecutableCatalog& catalog) {
    const auto old=selected_<files_.size()?files_[selected_]:std::string();
    files_.clear();
    for(const auto& path:catalog.Files()) {
        if(files_.size()>=ExecutableCatalog::MaxFiles)break;
        files_.push_back(path.substr(0,ExecutableCatalog::MaxPathBytes));
    }
    selected_=0;
    auto it=std::find(files_.begin(),files_.end(),old);
    if(it!=files_.end())selected_=size_t(it-files_.begin());
    errors_=catalog.Errors();truncated_=catalog.Truncated();
}
void DesktopShell::SetStatus(const std::string& status) {status_=status.substr(0,240);}
void DesktopShell::Enter() {
    launch_.clear();exit_=false;startOpen_=false;hover_=pressed_=None;
    focus_=panel_==Panel::Programs && !files_.empty()?RowBase+int(selected_%Rows):panel_==Panel::Desktop?Programs:Close;
}
bool DesktopShell::Enabled(int t) const {
    if(t==Computer || t==Documents || t==MenuComputer || t==MenuDocuments || t==MenuRun || t==None)return false;
    if(t==Launch)return !files_.empty();
    if(t==Previous)return First()>0;
    if(t==Next)return First()+Rows<files_.size();
    if(t>=RowBase && t<RowBase+Rows)return First()+size_t(t-RowBase)<files_.size();
    return t>=Start && t<=Next;
}
std::vector<int> DesktopShell::FocusOrder() const {
    if(startOpen_)return {MenuPrograms,About,Exit,Start};
    if(panel_==Panel::About)return {Close,Start};
    if(panel_==Panel::Programs) {
        std::vector<int> out;
        for(size_t i=First();i<files_.size() && i<First()+Rows;++i)out.push_back(RowBase+int(i-First()));
        for(int t:{Previous,Next,Launch,Close,Start})if(Enabled(t))out.push_back(t);
        return out;
    }
    return {Programs,Start};
}
void DesktopShell::ToggleStart() {pressed_=None;startOpen_=!startOpen_;focus_=startOpen_?MenuPrograms:Programs;}
void DesktopShell::Move(int delta) {
    if(panel_==Panel::Programs && !startOpen_ && !files_.empty() && focus_>=RowBase) {
        const int count=int(files_.size());selected_=size_t((int(selected_)+delta%count+count)%count);
        focus_=RowBase+int(selected_%Rows);return;
    }
    const auto order=FocusOrder();
    const auto it=std::find(order.begin(),order.end(),focus_);
    const int current=it==order.end()?0:int(it-order.begin()),count=int(order.size());
    focus_=order[size_t((current+delta%count+count)%count)];
}
void DesktopShell::Page(int delta) {
    if(panel_!=Panel::Programs || startOpen_ || files_.empty())return;
    const int page=std::max(0,std::min(int(First()/Rows)+delta,int((files_.size()-1)/Rows)));
    selected_=std::min(size_t(page*Rows)+selected_%Rows,files_.size()-1);focus_=RowBase+int(selected_%Rows);
}
void DesktopShell::Accept() {Activate(focus_);}
void DesktopShell::Back() {
    pressed_=None;
    if(startOpen_){startOpen_=false;focus_=Programs;}
    else if(panel_!=Panel::Desktop){panel_=Panel::Desktop;focus_=Programs;}
    else exit_=true;
}
int DesktopShell::HitTest(int x,int y) const {
    if(Bounds(Start).Contains(x,y))return Start;
    if(startOpen_) {
        for(int t:{MenuPrograms,MenuComputer,MenuDocuments,MenuRun,About,Exit})if(Bounds(t).Contains(x,y))return t;
        return None; // Menu is modal to clicks; no click-through to desktop/window.
    }
    if(panel_!=Panel::Desktop) {
        if(Bounds(Close).Contains(x,y))return Close;
        if(panel_==Panel::Programs) {
            for(int t:{Launch,Previous,Next})if(Bounds(t).Contains(x,y))return t;
            for(int i=0;i<Rows;++i)if(Bounds(RowBase+i).Contains(x,y))return RowBase+i;
        }
        return None;
    }
    for(int t:{Programs,Computer,Documents})if(Bounds(t).Contains(x,y))return t;
    return None;
}
void DesktopShell::PointerMove(int x,int y) {hover_=HitTest(x,y);}
void DesktopShell::PointerDown(int x,int y) {PointerMove(x,y);pressed_=hover_;}
void DesktopShell::PointerUp(int x,int y) {
    PointerMove(x,y);const int target=pressed_;pressed_=None;
    if(target!=hover_)return;
    if(target==None){if(startOpen_){startOpen_=false;focus_=Programs;}return;}
    if(!Enabled(target))return;
    focus_=target;
    if(target>=RowBase) {
        const auto next=First()+size_t(target-RowBase);
        // First click selects; Open or controller Accept explicitly launches.
        selected_=next;return;
    }
    Activate(target);
}
void DesktopShell::Activate(int target) {
    if(!Enabled(target))return;
    if(target>=RowBase) {selected_=First()+size_t(target-RowBase);launch_=files_[selected_];return;}
    switch(target) {
    case Start:ToggleStart();return;
    case Programs:case MenuPrograms:panel_=Panel::Programs;focus_=files_.empty()?Close:RowBase+int(selected_%Rows);break;
    case About:panel_=Panel::About;focus_=Close;break;
    case Close:panel_=Panel::Desktop;focus_=Programs;break;
    case Previous:Page(-1);break;
    case Next:Page(1);break;
    case Launch:launch_=files_[selected_];break;
    case Exit:exit_=true;break;
    default:break;
    }
    startOpen_=false;
}
void DesktopShell::Render(Surface& s,const std::u16string& clock) const {
    if(s.width!=Width || s.height!=Height || !s.Valid() || s.bpp!=32)return;
    Gradient(s,{0,0,Width,680},0x21689C,0x092F61);
    Text(s,{880,42,1240,88},u"Switch Recompiler",0xD0EAFF,30,true);
    Text(s,{925,90,1240,120},u"Рабочая среда • один гостевой сеанс",0xA5CEE9,14);
    Text(s,{22,640,1250,663},u"Стик: курсор  •  A: выбрать  •  X: открыть  •  R: Пуск  •  B: назад  •  +: выход",0xD3E6FA,14);
    Text(s,{22,663,1250,680},Wide(status_),0xC1DCF4,12);
    auto control=[&](int target,std::u16string label,Rect r) {
        const bool enabled=Enabled(target),hot=hover_==target && enabled,down=pressed_==target && hot;
        Gradient(s,r,down?0x255BB3:hot?0xFCFCFF:0xF6F7F3,down?0x3E79CF:0xDAE3EF);
        Frame(s,r,enabled?0x3465A4:0xA0ABBA);
        if(focus_==target && enabled)Frame(s,{r.left+3,r.top+3,r.right-3,r.bottom-3},0x6684AE);
        Text(s,{r.left+10,r.top+7,r.right-7,r.bottom-2},label,!enabled?0x8793A0:down?0xFFFFFF:0x162B47,15,true);
    };
    for(int t:{Programs,Computer,Documents}) {
        const auto r=Bounds(t);const bool selected=focus_==t,enabled=Enabled(t);
        if(selected || (hover_==t && enabled)){Fill(s,r,pressed_==t?0x15457E:0x285F97);Frame(s,r,0x81B4ED);}
        Icon(s,r.left+40,r.top+6,t==Programs?shell_art::programs:t==Computer?shell_art::computer:shell_art::folder);
        Text(s,{r.left+4,r.top+44,r.right-2,r.top+66},t==Programs?u"Программы":t==Computer?u"Мой":u"Мои",enabled?0xFFFFFF:0xA7BDD3,14,true);
        if(t!=Programs)Text(s,{r.left+4,r.top+64,r.right-2,r.bottom},t==Computer?u"компьютер":u"документы",0xA7BDD3,14,true);
    }
    if(panel_!=Panel::Desktop) {
        Fill(s,{224,106,1166,630},0x13385B);
        Fill(s,{218,100,1160,624},0xECE9D8);Frame(s,{218,100,1160,624},0x0646A4);
        Gradient(s,{220,102,1158,146},0x4E93F8,0x0950C6);
        Text(s,{234,110,1100,142},panel_==Panel::Programs?u"Программы — запуск EXE":u"О Switch Recompiler",0xFFFFFF,20,true);
        control(Close,u"×",Bounds(Close));
        if(panel_==Panel::Programs) {
            Fill(s,{240,184,1138,554},0xFFFFFF);Frame(s,{239,183,1139,555},0x9EACB9);
            Text(s,{240,151,1138,179},u"ROMFS / SD / drive_c/Programs  •  Список доступных программ",0x42576B,14);
            for(size_t i=First();i<files_.size() && i<First()+Rows;++i) {
                const int target=RowBase+int(i-First());const auto r=Bounds(target);
                const bool chosen=i==selected_;
                if(chosen)Fill(s,r,0x316AC5);else if(hover_==target)Fill(s,r,0xE6F0FF);
                if(focus_==target)Frame(s,r,0x92B8EF);
                Text(s,{r.left+8,r.top+3,r.right-8,r.bottom-1},Wide(files_[i]),chosen?0xFFFFFF:0x203044,14);
            }
            if(files_.empty())Text(s,{260,204,1118,250},u"EXE не найдены. Добавьте программу на SD и откройте оболочку снова.",0x536376,16);
            control(Previous,u"← Назад",Bounds(Previous));control(Next,u"Далее →",Bounds(Next));control(Launch,u"Открыть",Bounds(Launch));
            char count[80];std::snprintf(count,sizeof(count),"%zu / %zu",files_.empty()?0:selected_+1,files_.size());
            Text(s,{522,584,682,610},Wide(count),0x42576B,16);
            Text(s,{242,556,1136,577},truncated_?u"Достигнут лимит поиска: список неполный.":errors_?u"Часть каталогов недоступна: список может быть неполным.":u"При запуске: L меняет UI, ZL выбирает программный вывод.",0x5A6574,13);
        } else {
            Icon(s,256,184,shell_art::computer);
            Text(s,{312,184,1120,220},u"Switch Recompiler",0x123D75,26,true);
            Text(s,{256,254,1120,288},u"Native-оболочка для поддерживаемых Win32-приложений.",0x24354A,19);
            Text(s,{256,306,1120,336},u"Постоянный C: — SD / switch-recompiler / drive_c",0x24354A,17);
            Text(s,{256,346,1120,376},u"S: — реальная SD-карта    •    R: — ROMFS, только чтение",0x24354A,17);
            Text(s,{256,410,1120,442},u"Иконки Tango: Public Domain. Шрифты Liberation: OFL.",0x526477,16);
            Text(s,{256,454,1120,484},u"Проводник пока недоступен. После выхода из приложения вы вернётесь сюда.",0x526477,16);
        }
    }
    Gradient(s,{0,680,Width,720},0x397FE5,0x1955BA);Fill(s,{0,680,Width,682},0x70ACFA);
    Gradient(s,Bounds(Start),(startOpen_ || pressed_==Start)?0x1B7435:0x5BB464,(startOpen_ || pressed_==Start)?0x195A2F:0x248337);
    Frame(s,Bounds(Start),0x1C6332);
    if(focus_==Start || hover_==Start)Frame(s,{3,683,139,717},0xADDEAB);
    Fill(s,{14,691,32,709},0xE8FAEB);Text(s,{16,689,34,712},u"S",0x21713D,14,true);
    Text(s,{45,685,137,717},u"Пуск",0xFFFFFF,22,true);
    Text(s,{166,690,500,715},u"Switch Recompiler",0xDCEBFF,16);
    Gradient(s,{1158,682,1280,720},0x2598DE,0x127BBD);
    Text(s,{1180,689,1272,716},clock,0xFFFFFF,17);
    if(startOpen_) {
        Fill(s,{4,288,484,680},0x163B64);Fill(s,{0,284,480,680},0xFFFFFF);
        Fill(s,{242,352,478,630},0xD8E8F7);Frame(s,{0,284,480,680},0x1555AD);
        Gradient(s,{2,286,478,352},0x4D98EB,0x1C63BD);
        Icon(s,18,302,shell_art::computer);Text(s,{66,306,465,342},u"Player",0xFFFFFF,23,true);
        for(int t:{MenuPrograms,MenuComputer,MenuDocuments,MenuRun,About}) {
            const auto r=Bounds(t);const bool hot=Enabled(t) && (focus_==t || hover_==t);
            if(hot){Fill(s,r,pressed_==t?0x1D4C99:0x316AC5);Frame(s,r,0x204DA0);}
            Text(s,{r.left+10,r.top+12,r.right-4,r.bottom-2},t==MenuPrograms?u"Программы →":t==MenuComputer?u"Мой компьютер":t==MenuDocuments?u"Мои документы":t==MenuRun?u"Выполнить…":u"О программе",!Enabled(t)?0x8195A9:hot?0xFFFFFF:0x173250,17,t==MenuPrograms);
        }
        Text(s,{258,538,466,602},u"Пока недоступно",0x8397AB,13);
        Gradient(s,{2,622,478,678},0x3282D6,0x1C61B5);control(Exit,u"Выйти в Homebrew",Bounds(Exit));
    }
}
} // namespace ui
