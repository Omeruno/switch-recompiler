#include "desktop_shell.h"
#include "font.h"
#include "shell_icons.h"
#include "xp_theme.h"
#include "xp_icons.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>

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
// Подпись значка рабочего стола как в XP: по центру, до двух строк, белая с
// тенью; у выделенного — синяя подложка под текстом
void DesktopLabel(Surface& s,Rect cell,int top,const std::u16string& text,bool selected,bool enabled) {
    FontDesc desc;desc.face="Liberation Sans";desc.height=-14;desc.weight=400;
    auto* font=FontCache::Get(desc);
    const int maxWidth=cell.Width()-6;
    std::vector<std::u16string> lines;std::u16string line;
    size_t i=0;
    while(i<text.size() && lines.size()<2) {
        size_t space=text.find(u' ',i);std::u16string word=text.substr(i,space==std::u16string::npos?std::u16string::npos:space-i);
        i=space==std::u16string::npos?text.size():space+1;
        std::u16string next=line.empty()?word:line+u" "+word;
        if(!line.empty() && font->MeasureWidth(next)>maxWidth){lines.push_back(line);line=word;}
        else line=next;
    }
    if(!line.empty() && lines.size()<2)lines.push_back(line);
    if(i<text.size() && !lines.empty())lines.back()+=u"…";
    for(size_t n=0;n<lines.size();++n) {
        auto& l=lines[n];
        while(l.size()>1 && font->MeasureWidth(l)>maxWidth){l.pop_back();if(l.back()!=u'…'){l.pop_back();l+=u"…";}}
        const int w=font->MeasureWidth(l),x=cell.left+(cell.Width()-w)/2,y=top+int(n)*16;
        if(selected)Fill(s,{x-2,y,x+w+2,y+16},0x316AC5);
        else font->Draw(s,Region(Rect(x+1,y+1,x+w+2,y+18)),x+1,y+1,l,0x0A1F3A);
        font->Draw(s,Region(Rect(x,y,x+w+1,y+17)),x,y,l,enabled?0xFFFFFF:0xC8D2DC);
    }
}
int TextWidth(const std::u16string& text,int size,bool bold) {
    FontDesc desc;desc.face="Liberation Sans";desc.height=-size;desc.weight=bold?700:400;
    return FontCache::Get(desc)->MeasureWidth(text);
}
// Текст с переносом по словам в пределах r
void WrapText(Surface& s,Rect r,const std::u16string& text,uint32_t color,int size) {
    std::u16string line;int y=r.top;const int step=size+4;
    size_t i=0;
    auto flush=[&]() {if(!line.empty() && y+step<=r.bottom+2)Text(s,{r.left,y,r.right,y+step},line,color,size);y+=step;line.clear();};
    while(i<text.size()) {
        size_t space=text.find(u' ',i);
        std::u16string word=text.substr(i,space==std::u16string::npos?std::u16string::npos:space-i);
        i=space==std::u16string::npos?text.size():space+1;
        std::u16string next=line.empty()?word:line+u" "+word;
        if(!line.empty() && TextWidth(next,size,false)>r.Width()){flush();line=word;}
        else line=next;
    }
    flush();
}
// Геометрия окна оболочки (логические координаты 1280x720)
Rect WindowRect() {return {156,8,1258,676};}
Rect TaskPaneRect() {return {160,126,390,646};}
Rect ContentRect() {return DesktopShell::ContentArea();}
Rect StatusRect() {return {160,646,1254,672};}
// Значок, тип и размер объекта проводника
xp::IconKind EntryIcon(const ShellBrowser::Entry& e) {
    auto lower=[](std::string v){for(char& c:v)if(c>='A' && c<='Z')c=char(c+32);return v;};
    if(e.kind==ShellBrowser::Kind::Drive) {
        const char d=e.name.empty()?'C':e.name[0];
        return d=='S'?xp::IconKind::SdCard:d=='R' || e.readOnly?xp::IconKind::CdDrive:xp::IconKind::HardDrive;
    }
    const auto name=lower(e.name);
    if(e.kind==ShellBrowser::Kind::Directory) {
        if(name=="my documents" || name=="мои документы")return xp::IconKind::Documents;
        if(name=="my pictures" || name=="мои рисунки")return xp::IconKind::Pictures;
        if(name=="programs" || name=="program files")return xp::IconKind::Programs;
        return xp::IconKind::Folder;
    }
    auto ends=[&](const char* ext){const size_t n=std::strlen(ext);return name.size()>n && name.compare(name.size()-n,n,ext)==0;};
    if(ends(".exe"))return xp::IconKind::Application;
    if(ends(".txt") || ends(".ini") || ends(".log"))return xp::IconKind::TextFile;
    if(ends(".bmp") || ends(".png") || ends(".jpg") || ends(".jpeg") || ends(".gif"))return xp::IconKind::ImageFile;
    if(ends(".lnk"))return xp::IconKind::Application;
    return xp::IconKind::File;
}
std::u16string EntryType(const ShellBrowser::Entry& e) {
    switch(EntryIcon(e)) {
    case xp::IconKind::HardDrive:return u"Локальный диск";
    case xp::IconKind::SdCard:return u"SD-карта";
    case xp::IconKind::CdDrive:return u"Только чтение (ROMFS)";
    case xp::IconKind::Folder:case xp::IconKind::Documents:case xp::IconKind::Pictures:case xp::IconKind::Programs:return u"Папка с файлами";
    case xp::IconKind::Application:return u"Приложение";
    case xp::IconKind::TextFile:return u"Текстовый документ";
    case xp::IconKind::ImageFile:return u"Рисунок";
    default:return u"Файл";
    }
}
std::u16string FormatBytes(uint64_t v) {
    char out[48];
    if(v>=1024ull*1024*1024)std::snprintf(out,sizeof(out),"%.1f ГБ",double(v)/(1024.0*1024*1024));
    else if(v>=1024*1024)std::snprintf(out,sizeof(out),"%.1f МБ",double(v)/(1024.0*1024));
    else if(v>=1024)std::snprintf(out,sizeof(out),"%.1f КБ",double(v)/1024);
    else std::snprintf(out,sizeof(out),"%llu байт",static_cast<unsigned long long>(v));
    return Wide(out);
}
std::u16string EntrySize(const ShellBrowser::Entry& e) {
    if(e.kind==ShellBrowser::Kind::Directory)return u"";
    if(e.kind==ShellBrowser::Kind::Drive)return e.readOnly && !e.size?u"":u"Свободно: "+FormatBytes(e.size);
    return FormatBytes(e.size);
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
    case FolderTreeButton:return {450,63,540,91};
    case TreeExpand:return {416,504,554,540};
    case TreeCollapse:return {560,504,698,540};
    case TreeOpen:return {704,504,834,540};
    case ImageFit:return {416,504,592,540};
    case ImageActual:return {600,504,778,540};
    case TextOpen:case TextNew:return {}; // reached through context menu
    case TextEdit:return {416,450,554,486};
    case TextInsert:return {560,450,698,486};
    case TextErase:return {704,450,842,486};
    case TextSave:return {416,504,554,540};
    case TextSaveAs:return {560,504,698,540};
    case TextDiscard:return {704,504,834,540};
    case Settings:return {6,358,238,410};
    case PreferenceSort:return {416,276,630,310};
    case AssociationPrevious:return {416,392,500,428};
    case AssociationNext:return {508,392,592,428};
    case AssociationChange:return {600,392,790,428};
    case AssociationForget:return {798,392,1006,428};
    case PreferenceSave:return {416,504,630,540};
    case FileActions:return {162,40,212,58};
    case FolderActions:return {214,40,276,58};
    case Copy:return {178,258,378,278};
    case Paste:return {178,280,378,300};
    case NewFolder:return {178,214,378,234};
    case Rename:return {178,236,378,256};
    case Delete:return {178,302,378,322};
    case Properties:return {178,324,378,344};
    case ConfirmDelete:return {664,504,828,540};
    case CancelDialog:return {840,504,1006,540};
    case GuestTask:return {158,684,468,716};
    case Start:return {0,680,142,720};
    case Programs:return {20,26,132,116};
    case Computer:return {20,132,132,222};
    case Documents:return {20,238,132,328};
    case MenuPrograms:return {6,586,238,622};
    case MenuComputer:return {246,398,474,432};
    case MenuDocuments:return {246,360,474,394};
    case MenuRun:return {246,450,474,484};
    case About:return {6,414,238,466};
    case Exit:return {300,638,474,672};
    case Close:return {1226,12,1250,34};
    case Launch:return {178,170,378,190};
    case Previous:return {1170,649,1208,669};
    case Next:return {1212,649,1250,669};
    case OpenWith:return {178,192,378,212};
    case NavBack:return {162,63,262,91};
    case NavUp:return {306,63,344,91};
    case Refresh:return {1162,99,1252,123};
    case Sort:return {552,63,680,91};
    case Address:return {216,99,1158,123};
    case QuickComputer:return {178,392,378,412};
    case QuickDocuments:return {178,414,378,434};
    case QuickC:return {178,436,378,456};
    case QuickS:return {178,458,378,478};
    case QuickR:return {178,480,378,500};
    default:
        if(t>=DesktopLinkBase && t<DesktopLinkBase+24){const int n=t-DesktopLinkBase;return {160+(n/5)*132,26+(n%5)*112,272+(n/5)*132,116+(n%5)*112};}
        if(t>=TreeRowBase && t<TreeRowBase+7)return {416,270+(t-TreeRowBase)*30,1006,300+(t-TreeRowBase)*30};
        if(t>=TextLineBase && t<TextLineBase+5)return {416,300+(t-TextLineBase)*28,1006,328+(t-TextLineBase)*28};
        if(t>=BrowserRowBase && t<BrowserRowBase+Rows){const int n=t-BrowserRowBase,col=n%2,row=n/2;return {400+col*352,134+row*72,400+col*352+344,134+row*72+68};}
        if(t>=RowBase && t<RowBase+Rows)return {400,134+(t-RowBase)*36,1246,168+(t-RowBase)*36};
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
    catalogFiles_=files_;RefreshLinks();
    const auto retained=std::find(files_.begin(),files_.end(),old);if(retained!=files_.end())selected_=size_t(retained-files_.begin());
    errors_=catalog.Errors();truncated_=catalog.Truncated();
}
bool DesktopShell::ConfigureFilesystem(const std::string& system,const std::string& sd,const std::string& rom,std::string& error) {
    if(TextDialog() || dialog_==Dialog::ImageViewer){error="Close the document before changing filesystem roots.";return false;}
    CloseContext();browser_.CancelCopy();clipboardPath_.clear();clipboardName_.clear();
    const bool ok=browser_.Configure(system,sd,rom,error);if(ok)RefreshLinks();return ok;
}
void DesktopShell::SetStatus(const std::string& status) {status_=status.substr(0,240);}
void DesktopShell::SetGuestActive(const std::string& label) {
    returnPanel_=panel_;returnFocus_=focus_;guestActive_=true;guestMinimized_=false;guestTaskPending_=false;
    guestLabel_=label.substr(0,240);panel_=Panel::Desktop;Enter();
}
void DesktopShell::EndGuest() {
    CloseContext();secondaryTarget_=-2;
    browser_.CancelCopy();
    guestActive_=false;guestLabel_.clear();
    if(!TextDialog() && dialog_!=Dialog::ImageViewer){panel_=returnPanel_;focus_=returnFocus_;}
    request_.Clear();startOpen_=false;pressed_=None;
    if(!TextDialog() && dialog_!=Dialog::ImageViewer){dialog_=Dialog::None;textInput_=TextInput::None;operationPath_.clear();}
}
void DesktopShell::Enter() {
    CloseContext();secondaryTarget_=-2;
    browser_.CancelCopy();
    if(TextDialog() || dialog_==Dialog::ImageViewer){request_.Clear();startOpen_=false;hover_=pressed_=None;return;}
    RefreshLinks();
    dialog_=Dialog::None;operationPath_.clear();operationNotice_.clear();
    request_.Clear();pendingDocument_.clear();textInput_=TextInput::None;exit_=false;startOpen_=false;hover_=pressed_=None;
    if(panel_==Panel::Explorer){browser_.Refresh();FocusBrowserSelection();return;}
    focus_=panel_==Panel::Programs && !files_.empty()?RowBase+int(selected_%Rows):panel_==Panel::Desktop?Programs:Close;
}
bool DesktopShell::Enabled(int t) const {
    if(contextOpen_)return t>=ContextBase && size_t(t-ContextBase)<contextCount_ && ControlEnabled(contextActions_[size_t(t-ContextBase)]);
    return ControlEnabled(t);
}
bool DesktopShell::ControlEnabled(int t) const {
    if(t==None)return false;
    if(textInput_!=TextInput::None)return false;
    if(dialog_==Dialog::FolderTree) {
        if(t==CancelDialog)return true;
        if(t==TreeOpen || t==TreeExpand || t==TreeCollapse)return !tree_.Nodes().empty();
        return t>=TreeRowBase && t<TreeRowBase+7 && treeSelection_/7*7+size_t(t-TreeRowBase)<tree_.Nodes().size();
    }
    if(dialog_==Dialog::ImageViewer)return t==ImageFit || t==ImageActual || t==CancelDialog;
    if(TextDialog()) {
        if(t==CancelDialog || t==TextSaveAs)return true;
        if(t==TextSave)return !document_.Path().empty();
        if(dialog_==Dialog::TextClose)return t==TextDiscard;
        if(t==TextEdit)return document_.Lines()[textSelection_].text.size()<=TextDocument::MaxEditUnits;
        if(t==TextInsert || t==TextErase)return true;
        return t>=TextLineBase && t<TextLineBase+5 && textSelection_/5*5+size_t(t-TextLineBase)<document_.Lines().size();
    }
    if(dialog_==Dialog::Preferences) {
        if(t==PreferenceSort || t==PreferenceSave || t==CancelDialog)return true;
        if(t==AssociationPrevious || t==AssociationNext)return browser_.Associations().size()>1;
        return (t==AssociationChange || t==AssociationForget) && preferenceSelection_<browser_.Associations().size();
    }
    if(dialog_!=Dialog::None)return t==CancelDialog || (dialog_==Dialog::DeleteConfirm && t==ConfirmDelete);
    if(t==ConfirmDelete || t==CancelDialog)return false;
    if(t>=TextEdit && t<=TextDiscard)return false;
    if(t>=TreeOpen && t<=TreeCollapse)return false;
    if(t==FolderTreeButton)return panel_==Panel::Explorer;
    if(t==DesktopShortcut || t==StartShortcut)return panel_==Panel::Explorer && !browser_.Entries().empty() && browser_.Entries()[browser_.Selected()].kind==ShellBrowser::Kind::File && ShellBrowser::IsExecutable(browser_.Entries()[browser_.Selected()].path);
    if(t>=DesktopLinkBase && t<DesktopLinkBase+24)return !guestActive_ && size_t(t-DesktopLinkBase)<desktopLinks_.size();
    if(t==ImageFit || t==ImageActual)return false;
    if(t==ImageOpen) {
        if(panel_!=Panel::Explorer || browser_.Entries().empty())return false;
        const auto& entry=browser_.Entries()[browser_.Selected()];auto name=entry.name;
        std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return c>='A' && c<='Z'?char(c+32):char(c);});
        return entry.kind==ShellBrowser::Kind::File && name.size()>=4 && name.substr(name.size()-4)==".bmp";
    }
    if(t==TextNew)return panel_==Panel::Explorer && !browser_.Path().empty() && !browser_.ReadOnly();
    if(t==TextOpen) {
        if(panel_!=Panel::Explorer || browser_.Entries().empty())return false;
        const auto& entry=browser_.Entries()[browser_.Selected()];auto name=entry.name;
        std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return c>='A' && c<='Z'?char(c+32):char(c);});
        return entry.kind==ShellBrowser::Kind::File && name.size()>=4 && name.substr(name.size()-4)==".txt";
    }
    if(t==Settings)return browser_.Ready();
    if(t==FileActions)return panel_==Panel::Explorer && !browser_.Entries().empty();
    if(t==FolderActions)return panel_==Panel::Explorer;
    if(t==Copy)return panel_==Panel::Explorer && !browser_.Entries().empty() && browser_.Entries()[browser_.Selected()].kind==ShellBrowser::Kind::File;
    if(t==Paste)return panel_==Panel::Explorer && !clipboardPath_.empty() && !browser_.Path().empty() && !browser_.ReadOnly();
    if(t==NewFolder)return panel_==Panel::Explorer && !browser_.Path().empty() && !browser_.ReadOnly();
    if(t==Properties)return panel_==Panel::Explorer && !browser_.Entries().empty();
    if(t==Rename || t==Delete)return panel_==Panel::Explorer && !browser_.ReadOnly() &&
        !browser_.Entries().empty() && browser_.Entries()[browser_.Selected()].kind!=ShellBrowser::Kind::Drive;
    if(guestActive_ && (t==MenuRun || t==OpenWith || t==Exit))return false;
    if(guestActive_ && t==Launch) {
        if(panel_!=Panel::Explorer || browser_.Entries().empty())return false;
        return browser_.Entries()[browser_.Selected()].kind!=ShellBrowser::Kind::File;
    }
    if(t==Computer || t==Documents || t==MenuComputer || t==MenuDocuments || t==MenuRun ||
       t==QuickComputer || t==QuickDocuments || t==Address || t==Refresh || t==Sort)return browser_.Ready();
    if(t>=QuickC && t<=QuickR)return browser_.Mounted(t==QuickC?'C':t==QuickS?'S':'R');
    if(t==NavBack)return browser_.CanBack();
    if(t==NavUp)return browser_.CanUp();
    if(t>=BrowserRowBase && t<BrowserRowBase+Rows)return browser_.Selected()/Rows*Rows+size_t(t-BrowserRowBase)<browser_.Entries().size();
    if(t==OpenWith)return panel_==Panel::Explorer && browser_.CanChooseApplication();
    if(t==Launch)return panel_==Panel::Explorer?browser_.CanOpenSelected():!files_.empty() && browser_.Ready();
    if(t==Previous)return panel_==Panel::Explorer?browser_.Selected()/Rows>0:First()>0;
    if(t==Next)return panel_==Panel::Explorer?(browser_.Selected()/Rows+1)*Rows<browser_.Entries().size():First()+Rows<files_.size();
    if(t>=RowBase && t<RowBase+Rows)return First()+size_t(t-RowBase)<files_.size();
    return t>=Start && t<=Next;
}
std::vector<int> DesktopShell::FocusOrder() const {
    if(contextOpen_) {
        std::vector<int> out;for(size_t i=0;i<contextCount_;++i)if(Enabled(ContextBase+int(i)))out.push_back(ContextBase+int(i));
        return out;
    }
    if(textInput_!=TextInput::None)return {None};
    if(dialog_==Dialog::FolderTree)return {TreeExpand,TreeCollapse,TreeOpen,CancelDialog};
    if(dialog_==Dialog::ImageViewer)return {ImageFit,ImageActual,CancelDialog};
    if(TextDialog()) {
        std::vector<int> out;
        if(dialog_==Dialog::TextEditor)for(int t:{TextEdit,TextInsert,TextErase})if(Enabled(t))out.push_back(t);
        for(int t:{TextSave,TextSaveAs,TextDiscard,CancelDialog})if(Enabled(t))out.push_back(t);
        return out;
    }
    if(dialog_==Dialog::Preferences) {
        std::vector<int> out;for(int t:{PreferenceSort,AssociationPrevious,AssociationNext,AssociationChange,AssociationForget,PreferenceSave,CancelDialog})if(Enabled(t))out.push_back(t);
        return out;
    }
    if(dialog_==Dialog::DeleteConfirm)return {CancelDialog,ConfirmDelete};
    if(dialog_==Dialog::Properties || dialog_==Dialog::CopyProgress)return {CancelDialog};
    if(startOpen_) {
        std::vector<int> out;
        for(int t:{MenuPrograms,MenuComputer,MenuDocuments,MenuRun,About,Settings,Exit,Start})if(Enabled(t))out.push_back(t);
        return out;
    }
    if(panel_==Panel::About)return {Close,Start};
    if(panel_==Panel::Explorer) {
        std::vector<int> out;
        if(!browser_.Entries().empty())out.push_back(BrowserRowBase+int(browser_.Selected()%Rows));
        for(int t:{NavBack,NavUp,Address,Refresh,Sort,FolderTreeButton,QuickComputer,QuickDocuments,QuickC,QuickS,QuickR,NewFolder,Rename,Delete,Properties,Copy,Paste,FileActions,FolderActions,Previous,Next,OpenWith,Launch,Close,Start})
            if(Enabled(t))out.push_back(t);
        return out;
    }
    if(panel_==Panel::Programs) {
        std::vector<int> out;
        for(size_t i=First();i<files_.size() && i<First()+Rows;++i)out.push_back(RowBase+int(i-First()));
        for(int t:{Previous,Next,Launch,Close,Start})if(Enabled(t))out.push_back(t);
        return out;
    }
    std::vector<int> out;
    for(size_t i=0;i<desktopLinks_.size();++i)if(Enabled(DesktopLinkBase+int(i)))out.push_back(DesktopLinkBase+int(i));
    for(int t:{Programs,Computer,Documents,Start})if(Enabled(t))out.push_back(t);
    return out;
}
void DesktopShell::NextFocus() {
    if(textInput_!=TextInput::None)return;
    const auto order=FocusOrder();const auto it=std::find(order.begin(),order.end(),focus_);
    focus_=it==order.end() || it+1==order.end()?order.front():*(it+1);
}
void DesktopShell::FocusBrowserSelection() {
    focus_=browser_.Entries().empty()?Address:BrowserRowBase+int(browser_.Selected()%Rows);
}
void DesktopShell::OpenBrowser(const std::string& path) {
    if(browser_.Navigate(path)){panel_=Panel::Explorer;FocusBrowserSelection();}
    else if(startOpen_)focus_=menuReturnFocus_;
}
void DesktopShell::LaunchProgram() {
    request_.Clear();if(guestActive_) {SetStatus("Close the current application before starting another.");return;}
    if(selected_<files_.size()) {
        if(ShellLink::IsLink(files_[selected_]))browser_.OpenPath(browser_.Filesystem().ToGuest(files_[selected_]),request_);
        else browser_.ResolveCatalogExecutable(files_[selected_],request_);
    }
}
void DesktopShell::RefreshLinks() {
    if(!browser_.Ready())return;
    const auto previous=selected_<files_.size()?files_[selected_]:std::string();
    desktopLinks_.clear();files_=catalogFiles_;linkTruncated_=false;linkErrors_=0;
    const auto profile=browser_.Filesystem().ProfileDirectory();
    for(const bool start:{false,true}) {
        bool truncated=false;size_t errors=0;auto entries=browser_.ListAt(profile+(start?"\\Start Menu\\Programs":"\\Desktop"),&truncated,&errors);
        linkTruncated_=linkTruncated_ || truncated;linkErrors_+=errors;
        std::sort(entries.begin(),entries.end(),[](const ShellBrowser::Entry& a,const ShellBrowser::Entry& b){return a.path<b.path;});
        for(const auto& entry:entries)if(entry.kind==ShellBrowser::Kind::File && ShellLink::IsLink(entry.path)) {
            if(!start){if(desktopLinks_.size()<24)desktopLinks_.push_back(entry.path);else linkTruncated_=true;}
            if(start){if(files_.size()<ExecutableCatalog::MaxFiles)files_.push_back(browser_.Filesystem().ToHost(entry.path));else linkTruncated_=true;}
        }
    }
    selected_=0;for(size_t i=0;i<files_.size();++i)if(files_[i]==previous){selected_=i;break;}
}
void DesktopShell::ChooseApplication(const std::string& document) {
    if(guestActive_)return;
    pendingDocument_=document;textInput_=TextInput::OpenWith;initialText_=browser_.ApplicationFor(document);
}
void DesktopShell::OpenCurrent() {
    if(guestActive_ && (browser_.Entries().empty() || browser_.Entries()[browser_.Selected()].kind==ShellBrowser::Kind::File))return;
    if(!browser_.OpenSelected(request_) && browser_.LastError()==ShellBrowser::Error::UnsupportedFile && !browser_.UnassociatedDocument().empty())
        ChooseApplication(browser_.UnassociatedDocument());
    FocusBrowserSelection();
}
void DesktopShell::CloseText() {
    if(dialog_==Dialog::TextClose){dialog_=Dialog::TextEditor;focus_=TextEdit;return;}
    if(document_.Dirty()){dialog_=Dialog::TextClose;focus_=CancelDialog;return;}
    CloseDialog();
}
void DesktopShell::SaveText(const std::string& path) {
    const bool closing=dialog_==Dialog::TextClose;
    const bool saved=document_.Save(browser_.Filesystem(),path);
    if(!saved) {
        textNotice_=document_.Error()==EBUSY?u"Файл изменился: сохраните под новым именем.":document_.Error()==EEXIST?u"Имя уже занято. Укажите новое имя.":document_.Error()==EROFS || document_.Error()==EACCES?u"Нет доступа к записи. Сохраните в другой папке.":u"Ошибка записи. Текст остаётся в памяти.";
    } else {
        textNotice_=u"Сохранено.";
        if(!browser_.Refresh())textNotice_+=u" Список не обновлён.";
        else for(size_t i=0;i<browser_.Entries().size();++i)if(browser_.Entries()[i].path==document_.Path()){browser_.Select(i);break;}
    }
    if(!document_.BackupPath().empty())textNotice_+=u" Копия: "+Wide(document_.BackupPath().substr(document_.BackupPath().find_last_of('/')+1));
    if(!document_.RecoveryPath().empty())textNotice_+=u" Остаток: "+Wide(document_.RecoveryPath().substr(document_.RecoveryPath().find_last_of('/')+1));
    if(closing && saved){operationNotice_=textNotice_;CloseDialog();}
}
void DesktopShell::CloseDialog() {
    if(dialog_==Dialog::CopyProgress)browser_.CancelCopy();
    if(dialog_==Dialog::ImageViewer)image_.Clear();
    if(TextDialog())document_.New();
    dialog_=Dialog::None;operationPath_.clear();pressed_=hover_=None;focus_=dialogReturnFocus_;
}
bool DesktopShell::CaptureSelection() {
    if(browser_.Entries().empty())return false;
    operationPath_=browser_.Entries()[browser_.Selected()].path;operationParent_=browser_.Path();
    return browser_.SelectedProperties(properties_);
}
void DesktopShell::MutationFeedback(ShellBrowser::MutationResult result) {
    operationNotice_=result==ShellBrowser::MutationResult::Applied?u"Операция выполнена.":
        result==ShellBrowser::MutationResult::AppliedViewStale?u"Операция выполнена, но список не обновлён. Нажмите Обновить.":u"";
    if(result!=ShellBrowser::MutationResult::Failed)FocusBrowserSelection();
}
bool DesktopShell::ServiceCopy() {
    if(dialog_!=Dialog::CopyProgress || !browser_.ServiceCopy())return false;
    if(!browser_.CopyRunning()) {
        const auto result=browser_.CopyResult();CloseDialog();MutationFeedback(result);
    }
    return true;
}
void DesktopShell::CompleteTextInput(bool accepted,const std::string& input) {
    const auto purpose=textInput_;textInput_=TextInput::None;
    if(purpose==TextInput::ShortcutCommand) {
        if(accepted && browser_.CreateShortcut(input,shortcutStart_)){RefreshLinks();operationNotice_=u"Ярлык сохранён в профиле.";}
        return;
    }
    if(purpose==TextInput::TextLine || purpose==TextInput::TextSavePath) {
        if(!accepted)return;
        if(purpose==TextInput::TextSavePath) {
            if(input.size()<3 || !((input[0]>='A' && input[0]<='Z') || (input[0]>='a' && input[0]<='z')) || input[1]!=':' || (input[2]!='\\' && input[2]!='/'))textNotice_=u"Укажите полный путь C:\\, S:\\ или R:\\.";
            else SaveText(input);
        }
        else if(document_.Edit(textSelection_,input)){textNotice_=u"Строка изменена. Сохраните документ.";textColumn_=0;}
        else textNotice_=u"Строка не принята: предел 500 UTF-16, без переводов строк и двоичных символов.";
        return;
    }
    if(!accepted || purpose==TextInput::None){pendingDocument_.clear();pendingAssociation_.clear();operationPath_.clear();return;}
    if(purpose==TextInput::AssociationApplication) {
        browser_.ReassignAssociation(pendingAssociation_,input);pendingAssociation_.clear();return;
    }
    if(purpose==TextInput::PasteName) {
        if(browser_.Path()!=operationParent_) {operationNotice_=u"Папка изменилась. Повторите вставку.";return;}
        if(browser_.BeginCopyHere(clipboardPath_,input)) {
            operationPath_=browser_.Path()+(browser_.Path().back()=='\\'?"":"\\")+input;
            dialog_=Dialog::CopyProgress;dialogReturnFocus_=Paste;focus_=CancelDialog;pressed_=hover_=None;
        }
        return;
    }
    if(purpose==TextInput::NewFolder || purpose==TextInput::Rename) {
        // Selection/navigation is frozen while input is pending. Keep a captured
        // path check as well; do not redirect a command to a different selection.
        const auto selected=browser_.Entries().empty()?std::string():browser_.Entries()[browser_.Selected()].path;
        if(browser_.Path()!=operationParent_ || (purpose==TextInput::Rename && selected!=operationPath_)) {
            operationNotice_=u"Папка или выделение изменились. Повторите команду.";operationPath_.clear();return;
        }
        MutationFeedback(purpose==TextInput::NewFolder?browser_.CreateFolder(input):browser_.RenameSelected(input));
        operationPath_.clear();return;
    }
    request_.Clear();
    if(purpose==TextInput::Run) {
        if(browser_.RunCommand(input,request_) && request_.nativePath.empty()){panel_=Panel::Explorer;FocusBrowserSelection();}
        else if(browser_.LastError()==ShellBrowser::Error::UnsupportedFile && !browser_.UnassociatedDocument().empty())
            ChooseApplication(browser_.UnassociatedDocument());
        return;
    }
    std::string path=input;
    if(path.size()>=2 && path.front()=='"' && path.back()=='"')path=path.substr(1,path.size()-2);
    if(purpose==TextInput::OpenWith) {
        const auto document=pendingDocument_;pendingDocument_.clear();browser_.OpenWith(document,path,request_);
    } else OpenBrowser(path);
}
Rect DesktopShell::ContextBounds(int target) const {
    const int row=target-ContextBase;
    if(!contextOpen_ || row<0 || size_t(row)>=contextCount_)return {};
    return {contextRect_.left+4,contextRect_.top+4+row*30,contextRect_.right-4,contextRect_.top+34+row*30};
}
void DesktopShell::CloseContext() {
    if(!contextOpen_)return;
    contextOpen_=false;contextCount_=0;contextPath_.clear();pressed_=hover_=None;focus_=contextReturnFocus_;
}
void DesktopShell::ShowContext(bool folder,int x,int y) {
    if(ModalActive() || startOpen_ || panel_!=Panel::Explorer)return;
    if(!folder && browser_.Entries().empty())folder=true;
    contextCount_=0;contextPath_.clear();contextParent_=browser_.Path();contextReturnFocus_=focus_;
    auto add=[&](int action){contextActions_[contextCount_++]=action;};
    if(folder){add(Paste);add(NewFolder);add(TextNew);add(Refresh);}
    else {
        const auto& entry=browser_.Entries()[browser_.Selected()];contextPath_=entry.path;contextFile_=entry.kind==ShellBrowser::Kind::File;
        add(Launch);
        if(ControlEnabled(TextOpen))add(TextOpen);
        if(ControlEnabled(ImageOpen))add(ImageOpen);
        if(browser_.CanChooseApplication())add(OpenWith);
        if(entry.kind==ShellBrowser::Kind::File)add(Copy);
        if(ControlEnabled(DesktopShortcut)){add(DesktopShortcut);add(StartShortcut);}
        if(entry.kind!=ShellBrowser::Kind::Drive){add(Rename);add(Delete);}
        add(Properties);
    }
    const int height=8+int(contextCount_)*30;
    x=std::max(ContentRect().left,std::min(x,ContentRect().right-252));y=std::max(ContentRect().top,std::min(y,ContentRect().bottom-height));
    contextRect_={x,y,x+252,y+height};contextOpen_=true;pressed_=hover_=None;
    const auto order=FocusOrder();focus_=order.front();
}
void DesktopShell::OpenContext(bool folder) {
    if(contextOpen_){CloseContext();return;}
    const auto row=Bounds(BrowserRowBase+int(browser_.Selected()%Rows));
    ShowContext(folder || focus_==FolderActions,row.left+40,row.top);
}
int DesktopShell::SecondaryTarget(int x,int y) const {
    if(!ContentRect().Contains(x,y))return -2;
    const int target=HitTest(x,y);
    return target>=BrowserRowBase && target<BrowserRowBase+Rows && Enabled(target)?target:-1;
}
void DesktopShell::SecondaryDown(int x,int y) {
    secondaryTarget_=-2;
    if(contextOpen_){CloseContext();return;}
    if(ModalActive() || startOpen_ || panel_!=Panel::Explorer || pressed_!=None)return;
    secondaryTarget_=SecondaryTarget(x,y);secondaryParent_=browser_.Path();secondaryPath_.clear();
    if(secondaryTarget_>=BrowserRowBase)secondaryPath_=browser_.Entries()[browser_.Selected()/Rows*Rows+size_t(secondaryTarget_-BrowserRowBase)].path;
}
void DesktopShell::SecondaryUp(int x,int y) {
    const int target=secondaryTarget_;secondaryTarget_=-2;
    if(target==-2 || ModalActive() || startOpen_ || secondaryParent_!=browser_.Path() || target!=SecondaryTarget(x,y))return;
    if(target>=BrowserRowBase) {
        const auto index=browser_.Selected()/Rows*Rows+size_t(target-BrowserRowBase);
        if(index>=browser_.Entries().size() || browser_.Entries()[index].path!=secondaryPath_)return;
        browser_.Select(index);FocusBrowserSelection();
    }
    ShowContext(target==-1,x,y);
}
void DesktopShell::ToggleStart() {
    if(ModalActive())return;
    pressed_=None;
    if(!startOpen_) {menuReturnFocus_=focus_;startOpen_=true;focus_=MenuPrograms;}
    else {startOpen_=false;focus_=menuReturnFocus_;}
}
void DesktopShell::Move(int delta) {
    if(textInput_!=TextInput::None)return;
    if(dialog_==Dialog::FolderTree) {
        if(!tree_.Nodes().empty())treeSelection_=size_t(std::max<int64_t>(0,std::min<int64_t>(int64_t(tree_.Nodes().size())-1,int64_t(treeSelection_)+delta)));
        return;
    }
    if(dialog_==Dialog::ImageViewer) {
        if(!imageFit_)imagePanY_=int(std::max<int64_t>(0,std::min<int64_t>(std::max(0,image_.Height()-ImageViewport().Height()),int64_t(imagePanY_)+int64_t(delta)*32)));
        return;
    }
    if(TextDialog()) {
        if(dialog_==Dialog::TextEditor) {
            textSelection_=size_t(std::max<int64_t>(0,std::min<int64_t>(int64_t(document_.Lines().size())-1,int64_t(textSelection_)+delta)));
            textColumn_=0;
        }
        return;
    }
    if(dialog_==Dialog::Preferences) {
        const auto count=browser_.Associations().size();
        if(count)preferenceSelection_=size_t((int64_t(preferenceSelection_)+delta%int64_t(count)+int64_t(count))%int64_t(count));
        return;
    }
    if(dialog_!=Dialog::None) {if(dialog_==Dialog::DeleteConfirm && delta%2)focus_=focus_==CancelDialog?ConfirmDelete:CancelDialog;return;}
    if(panel_==Panel::Explorer && !startOpen_ && focus_>=BrowserRowBase) {
        browser_.Move(delta);FocusBrowserSelection();return;
    }
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
    if(dialog_==Dialog::FolderTree){if(delta>0)tree_.Expand(treeSelection_);else tree_.Collapse(treeSelection_);return;}
    if(dialog_==Dialog::ImageViewer) {
        if(!imageFit_)imagePanX_=int(std::max<int64_t>(0,std::min<int64_t>(std::max(0,image_.Width()-ImageViewport().Width()),int64_t(imagePanX_)+int64_t(delta)*32)));
        return;
    }
    if(dialog_==Dialog::TextEditor) {
        const auto length=document_.Lines()[textSelection_].text.size();
        textColumn_=size_t(std::max<int64_t>(0,std::min<int64_t>(length,int64_t(textColumn_)+int64_t(delta)*40)));return;
    }
    if(ModalActive())return;
    if(panel_==Panel::Explorer && !startOpen_){browser_.Page(delta,Rows);FocusBrowserSelection();return;}
    if(panel_!=Panel::Programs || startOpen_ || files_.empty())return;
    const int page=std::max(0,std::min(int(First()/Rows)+delta,int((files_.size()-1)/Rows)));
    selected_=std::min(size_t(page*Rows)+selected_%Rows,files_.size()-1);focus_=RowBase+int(selected_%Rows);
}
void DesktopShell::Accept() {Activate(focus_);}
void DesktopShell::Back() {
    pressed_=None;secondaryTarget_=-2;
    if(contextOpen_){CloseContext();return;}
    if(textInput_!=TextInput::None){CompleteTextInput(false,"");return;}
    if(TextDialog()){CloseText();return;}
    if(dialog_!=Dialog::None){const bool copying=dialog_==Dialog::CopyProgress;CloseDialog();if(copying)operationNotice_=u"Копирование отменено. Файл назначения не создан.";return;}
    if(startOpen_){ToggleStart();}
    else if(panel_==Panel::Explorer && browser_.CanBack()){browser_.Back();FocusBrowserSelection();}
    else if(panel_!=Panel::Desktop){panel_=Panel::Desktop;focus_=Programs;}
    else if(!guestActive_)exit_=true;
}
int DesktopShell::HitTest(int x,int y) const {
    if(contextOpen_) {
        for(size_t i=0;i<contextCount_;++i)if(ContextBounds(ContextBase+int(i)).Contains(x,y))return ContextBase+int(i);
        return None;
    }
    if(textInput_!=TextInput::None)return None;
    if(dialog_==Dialog::FolderTree) {
        for(int t:{TreeExpand,TreeCollapse,TreeOpen,CancelDialog})if(Bounds(t).Contains(x,y))return t;
        for(int i=0;i<7;++i)if(Bounds(TreeRowBase+i).Contains(x,y))return TreeRowBase+i;
        return None;
    }
    if(dialog_==Dialog::ImageViewer) {
        for(int t:{ImageFit,ImageActual,CancelDialog})if(Bounds(t).Contains(x,y))return t;
        return None;
    }
    if(TextDialog()) {
        for(int t:FocusOrder())if(Bounds(t).Contains(x,y))return t;
        if(dialog_==Dialog::TextEditor)for(int i=0;i<5;++i)if(Bounds(TextLineBase+i).Contains(x,y))return TextLineBase+i;
        return None;
    }
    if(dialog_==Dialog::Preferences) {
        for(int t:{PreferenceSort,AssociationPrevious,AssociationNext,AssociationChange,AssociationForget,PreferenceSave,CancelDialog})if(Bounds(t).Contains(x,y))return t;
        return None;
    }
    if(dialog_!=Dialog::None) {
        if(Bounds(CancelDialog).Contains(x,y))return CancelDialog;
        if(dialog_==Dialog::DeleteConfirm && Bounds(ConfirmDelete).Contains(x,y))return ConfirmDelete;
        return None;
    }
    if(Bounds(Start).Contains(x,y))return Start;
    if(startOpen_) {
        for(int t:{MenuPrograms,MenuComputer,MenuDocuments,MenuRun,About,Settings,Exit})if(Bounds(t).Contains(x,y))return t;
        return None; // Menu is modal to clicks; no click-through to desktop/window.
    }
    if(panel_!=Panel::Desktop) {
        if(Bounds(Close).Contains(x,y))return Close;
        if(panel_==Panel::Explorer) {
            for(int t:{NavBack,NavUp,Refresh,Sort,FolderTreeButton,Address,QuickComputer,QuickDocuments,QuickC,QuickS,QuickR,OpenWith,Launch,Previous,Next,NewFolder,Rename,Delete,Properties,Copy,Paste,FileActions,FolderActions})if(Bounds(t).Contains(x,y))return t;
            for(int i=0;i<Rows;++i)if(Bounds(BrowserRowBase+i).Contains(x,y))return BrowserRowBase+i;
        }
        if(panel_==Panel::Programs) {
            for(int t:{Launch,Previous,Next})if(Bounds(t).Contains(x,y))return t;
            for(int i=0;i<Rows;++i)if(Bounds(RowBase+i).Contains(x,y))return RowBase+i;
        }
        return None;
    }
    for(size_t i=0;i<desktopLinks_.size();++i)if(Bounds(DesktopLinkBase+int(i)).Contains(x,y))return DesktopLinkBase+int(i);
    for(int t:{Programs,Computer,Documents})if(Bounds(t).Contains(x,y))return t;
    return None;
}
void DesktopShell::PointerMove(int x,int y) {hover_=HitTest(x,y);if(contextOpen_ && Enabled(hover_))focus_=hover_;}
void DesktopShell::PointerDown(int x,int y) {secondaryTarget_=-2;PointerMove(x,y);pressed_=hover_;}
void DesktopShell::PointerUp(int x,int y) {
    PointerMove(x,y);const int target=pressed_;pressed_=None;
    if(target!=hover_)return;
    if(target==None){if(contextOpen_)CloseContext();else if(startOpen_){ToggleStart();}return;}
    if(!Enabled(target))return;
    focus_=target;
    if(target>=DesktopLinkBase && target<DesktopLinkBase+24){browser_.OpenPath(desktopLinks_[size_t(target-DesktopLinkBase)],request_);return;}
    if(target>=TreeRowBase && dialog_==Dialog::FolderTree){treeSelection_=treeSelection_/7*7+size_t(target-TreeRowBase);focus_=TreeOpen;return;}
    if(target>=TextLineBase && TextDialog()){textSelection_=textSelection_/5*5+size_t(target-TextLineBase);textColumn_=0;focus_=TextEdit;return;}
    if(target>=BrowserRowBase) {browser_.Select(browser_.Selected()/Rows*Rows+size_t(target-BrowserRowBase));return;}
    if(target>=RowBase) {
        const auto next=First()+size_t(target-RowBase);
        // First click selects; Open or controller Accept explicitly launches.
        selected_=next;return;
    }
    Activate(target);
}
void DesktopShell::Activate(int target) {
    if(!Enabled(target))return;
    if(contextOpen_) {
        const int action=contextActions_[size_t(target-ContextBase)];
        const auto path=contextPath_;const auto parent=contextParent_;const bool file=contextFile_;
        CloseContext();
        if(browser_.Path()!=parent || (!path.empty() && (browser_.Entries().empty() || browser_.Entries()[browser_.Selected()].path!=path))) {
            operationNotice_=u"Выделение изменилось. Откройте меню снова.";return;
        }
        if(!path.empty()) {
            rt::FileProperties current;if(!browser_.SelectedProperties(current))return;
            if((current.kind==rt::FileProperties::Kind::File)!=file){operationNotice_=u"Тип объекта изменился. Обновите список.";return;}
        }
        Activate(action);return;
    }
    if(target>=DesktopLinkBase && target<DesktopLinkBase+24){browser_.OpenPath(desktopLinks_[size_t(target-DesktopLinkBase)],request_);return;}
    if(target>=TreeRowBase && dialog_==Dialog::FolderTree){treeSelection_=treeSelection_/7*7+size_t(target-TreeRowBase);focus_=TreeOpen;return;}
    if(target>=TextLineBase && TextDialog()){textSelection_=textSelection_/5*5+size_t(target-TextLineBase);textColumn_=0;focus_=TextEdit;return;}
    if(target>=BrowserRowBase) {browser_.Select(browser_.Selected()/Rows*Rows+size_t(target-BrowserRowBase));OpenCurrent();return;}
    if(target>=RowBase) {selected_=First()+size_t(target-RowBase);LaunchProgram();return;}
    operationNotice_.clear();
    switch(target) {
    case DesktopShortcut:case StartShortcut:
        shortcutStart_=target==StartShortcut;initialText_=rt::QuoteCommandArgument(browser_.Entries()[browser_.Selected()].path);
        {std::u16string units;TextDocument::DecodeUtf8(initialText_,units);if(units.size()>500){operationNotice_=u"Путь превышает предел клавиатуры ярлыка (500 UTF-16).";break;}}
        textInput_=TextInput::ShortcutCommand;break;
    case FolderTreeButton:
        tree_.Reset(browser_.Filesystem());treeSelection_=0;dialog_=Dialog::FolderTree;dialogReturnFocus_=FolderTreeButton;focus_=TreeOpen;break;
    case TreeExpand:tree_.Expand(treeSelection_);break;
    case TreeCollapse:tree_.Collapse(treeSelection_);break;
    case TreeOpen: {
        const auto path=tree_.Nodes()[treeSelection_].path;CloseDialog();OpenBrowser(path);break;
    }
    case ImageOpen: {
        if(!image_.Open(browser_.Filesystem(),browser_.Entries()[browser_.Selected()].path)) {
            operationNotice_=BitmapDocument::ErrorText(image_.LastError());break;
        }
        imageFit_=true;imagePanX_=imagePanY_=0;dialogReturnFocus_=FileActions;
        dialog_=Dialog::ImageViewer;focus_=ImageFit;break;
    }
    case ImageFit:case ImageActual:imageFit_=target==ImageFit;imagePanX_=imagePanY_=0;break;
    case TextNew:case TextOpen: {
        if(target==TextNew)document_.New();
        else if(!document_.Open(browser_.Filesystem(),browser_.Entries()[browser_.Selected()].path)) {
            operationNotice_=u"TXT не открыт: допустим UTF-8 / UTF-16 с BOM, до 64 КиБ, без двоичных символов.";break;
        }
        textSelection_=textColumn_=0;textNotice_.clear();dialogReturnFocus_=FileActions;
        dialog_=Dialog::TextEditor;focus_=TextEdit;break;
    }
    case TextEdit:textInput_=TextInput::TextLine;initialText_=TextDocument::Utf8(document_.Lines()[textSelection_].text);break;
    case TextInsert:
        if(document_.InsertAfter(textSelection_)){++textSelection_;textColumn_=0;textNotice_=u"Строка добавлена.";}
        else textNotice_=u"Документ превышает 64 КиБ.";
        break;
    case TextErase:
        document_.Erase(textSelection_);textSelection_=std::min(textSelection_,document_.Lines().size()-1);textColumn_=0;break;
    case TextSave:SaveText();break;
    case TextSaveAs: {
        textInput_=TextInput::TextSavePath;initialText_=document_.Path().empty()?browser_.Path()+(browser_.Path().back()=='\\'?"":"\\")+"Новый.txt":document_.Path();
        std::u16string pathUnits;TextDocument::DecodeUtf8(initialText_,pathUnits);if(pathUnits.size()>500)initialText_.clear();
        break;
    }
    case TextDiscard:CloseDialog();break;
    case Settings:dialog_=Dialog::Preferences;dialogReturnFocus_=Start;focus_=CancelDialog;preferenceSelection_=0;break;
    case PreferenceSort:browser_.CycleSort();break;
    case PreferenceSave:browser_.SavePreferences();break;
    case AssociationPrevious:case AssociationNext: {
        const auto count=browser_.Associations().size();
        preferenceSelection_=target==AssociationPrevious?(preferenceSelection_+count-1)%count:(preferenceSelection_+1)%count;break;
    }
    case AssociationChange:
        pendingAssociation_=browser_.Associations()[preferenceSelection_].extension;
        initialText_=browser_.Associations()[preferenceSelection_].application;textInput_=TextInput::AssociationApplication;break;
    case AssociationForget:
        browser_.ForgetAssociation(browser_.Associations()[preferenceSelection_].extension);
        if(preferenceSelection_>=browser_.Associations().size())preferenceSelection_=0;
        break;
    case FileActions:OpenContext(false);break;
    case FolderActions:OpenContext(true);break;
    case Copy:
        if(CaptureSelection()) {
            clipboardPath_=operationPath_;clipboardName_=browser_.Entries()[browser_.Selected()].name;operationPath_.clear();
            operationNotice_=u"Файл выбран. Откройте папку назначения и нажмите Вставить.";
        }break;
    case Paste:
        operationParent_=browser_.Path();textInput_=TextInput::PasteName;initialText_=clipboardName_;break;
    case NewFolder:
        operationParent_=browser_.Path();operationPath_.clear();textInput_=TextInput::NewFolder;initialText_="Новая папка";break;
    case Rename:
        if(CaptureSelection()){textInput_=TextInput::Rename;initialText_=browser_.Entries()[browser_.Selected()].name;}break;
    case Delete:case Properties:
        if(CaptureSelection()) {
            dialogReturnFocus_=target;dialog_=target==Delete?Dialog::DeleteConfirm:Dialog::Properties;
            focus_=CancelDialog;pressed_=hover_=None;
        }break;
    case CancelDialog: {
        if(TextDialog()){CloseText();break;}
        const bool copying=dialog_==Dialog::CopyProgress;CloseDialog();
        if(copying)operationNotice_=u"Копирование отменено. Файл назначения не создан.";
        break;
    }
    case ConfirmDelete: {
        const auto path=operationPath_;const auto kind=properties_.kind;rt::FileProperties current;
        const bool same=browser_.Path()==operationParent_ && !browser_.Entries().empty() &&
            browser_.Entries()[browser_.Selected()].path==path;
        CloseDialog();
        if(!same){operationNotice_=u"Выделение изменилось. Повторите команду.";break;}
        if(!browser_.SelectedProperties(current))break;
        if(current.kind!=kind){operationNotice_=u"Тип объекта изменился. Повторите команду.";break;}
        MutationFeedback(browser_.DeleteSelected());break;
    }
    case Start:ToggleStart();return;
    case Programs:case MenuPrograms:panel_=Panel::Programs;focus_=files_.empty()?Close:RowBase+int(selected_%Rows);break;
    case Computer:case MenuComputer:case QuickComputer:OpenBrowser("");break;
    case Documents:case MenuDocuments:case QuickDocuments:OpenBrowser(browser_.DocumentsPath());break;
    case QuickC:OpenBrowser("C:\\");break;
    case QuickS:OpenBrowser("S:\\");break;
    case QuickR:OpenBrowser("R:\\");break;
    case NavBack:browser_.Back();FocusBrowserSelection();break;
    case NavUp:browser_.Up();FocusBrowserSelection();break;
    case Refresh:browser_.Refresh();FocusBrowserSelection();break;
    case Sort:browser_.CycleSort();FocusBrowserSelection();break;
    case Address:textInput_=TextInput::Address;initialText_=browser_.Path();break;
    case MenuRun:textInput_=TextInput::Run;initialText_=browser_.Path().empty()?"C:\\Programs\\":browser_.Path();focus_=menuReturnFocus_;break;
    case About:panel_=Panel::About;focus_=Close;break;
    case Close:panel_=Panel::Desktop;focus_=Programs;break;
    case Previous:Page(-1);break;
    case Next:Page(1);break;
    case OpenWith:ChooseApplication(browser_.SelectedDocument());break;
    case Launch:if(panel_==Panel::Explorer)OpenCurrent();else LaunchProgram();break;
    case Exit:exit_=true;break;
    default:break;
    }
    startOpen_=false;
}
void DesktopShell::Render(Surface& s,const std::u16string& clock) const {
    if(s.width!=Width || s.height!=Height || !s.Valid() || s.bpp!=32)return;
    xp::DrawWallpaper(s,{0,0,Width,680});

    // подсказки управления и строка состояния — с тенью, чтобы читались на обоях
    if(!guestActive_ && panel_==Panel::Desktop && !startOpen_) {
        const std::u16string hints=u"Стик: курсор  •  A/X: открыть  •  Y: кнопки  •  R: Пуск  •  B: назад  •  +: выход";
        Text(s,{23,641,1251,664},hints,0x0A1F3A,14);Text(s,{22,640,1250,663},hints,0xF2F7FC,14);
    }
    const std::u16string status=browser_.LastError()!=ShellBrowser::Error::None?ShellBrowser::ErrorText(browser_.LastError()):browser_.PreferenceWarning()?browser_.PreferenceText():linkTruncated_?u"Список ярлыков неполный: достигнут предел.":linkErrors_?u"Некоторые ярлыки недоступны.":Wide(status_);
    Text(s,{23,664,1251,681},status,0x0A1F3A,12);Text(s,{22,663,1250,680},status,0xE4EEF8,12);
    auto control=[&](int target,std::u16string label,Rect r) {
        const bool enabled=Enabled(target),hot=hover_==target && enabled,down=pressed_==target && hot;
        xp::DrawPushButton(s,r,enabled,hot,down,focus_==target && enabled);
        const int w=TextWidth(label,14,false);
        Text(s,{std::max(r.left+6,r.left+(r.Width()-w)/2)+(down?1:0),r.top+(r.Height()-17)/2+(down?1:0),r.right-4,r.bottom-2},label,enabled?0x000000:0xA19F92,14);
    };
    // Значки рабочего стола: 48 px по центру ячейки, подпись под ним
    auto desktopIcon=[&](int target,xp::IconKind kind,const std::u16string& label,bool link) {
        const auto r=Bounds(target);const bool enabled=Enabled(target);
        const bool selected=focus_==target || (hover_==target && enabled);
        xp::DrawIcon(s,kind,r.left+(r.Width()-48)/2,r.top+2,48,selected);
        if(link)xp::DrawIcon(s,xp::IconKind::Shortcut,r.left+(r.Width()-48)/2,r.top+2,48);
        DesktopLabel(s,r,r.top+54,label,selected,enabled);
    };
    desktopIcon(Programs,xp::IconKind::Programs,u"Программы",false);
    desktopIcon(Computer,xp::IconKind::Computer,u"Мой компьютер",false);
    desktopIcon(Documents,xp::IconKind::Documents,u"Мои документы",false);
    for(size_t i=0;i<desktopLinks_.size();++i) {
        const auto name=desktopLinks_[i].substr(desktopLinks_[i].find_last_of('\\')+1);
        desktopIcon(DesktopLinkBase+int(i),xp::IconKind::Application,Wide(name.substr(0,name.size()-4)),true);
    }
    if(panel_!=Panel::Desktop) {
        // Окно в стиле Windows XP: заголовок, строка меню, панель кнопок,
        // адресная строка, синяя панель задач слева, содержимое справа
        const Rect win=WindowRect();
        xp::DrawWindowFrame(s,win,30);
        const bool explorer=panel_==Panel::Explorer,programs=panel_==Panel::Programs;
        const std::u16string location=browser_.Path().empty()?u"Мой компьютер":Wide(browser_.Path());
        std::u16string title=programs?u"Все программы":explorer?location:u"О программе Switch Recompiler";
        if(explorer && !browser_.Path().empty()) {
            auto p=browser_.Path();while(p.size()>3 && (p.back()=='\\' || p.back()=='/'))p.pop_back();
            const auto cut=p.find_last_of("\\/");if(cut!=std::string::npos && cut+1<p.size())title=Wide(p.substr(cut+1));
        }
        xp::DrawIcon(s,explorer?(browser_.Path().empty()?xp::IconKind::Computer:xp::IconKind::FolderOpen):programs?xp::IconKind::Programs:xp::IconKind::Computer,win.left+8,win.top+7,16);
        Text(s,{win.left+31,win.top+7,win.right-90,win.top+28},title,0x0A246A,15,true);
        Text(s,{win.left+30,win.top+6,win.right-90,win.top+27},title,0xFFFFFF,15,true);
        const auto close=Bounds(Close);
        xp::DrawCaptionButton(s,{close.left-52,close.top,close.left-28,close.bottom},xp::CaptionButton::Minimize,false,false);
        xp::DrawCaptionButton(s,{close.left-26,close.top,close.left-2,close.bottom},xp::CaptionButton::Maximize,false,false);
        xp::DrawCaptionButton(s,close,xp::CaptionButton::Close,hover_==Close,pressed_==Close && hover_==Close);
        if(focus_==Close)xp::DrawFocusRect(s,{close.left-2,close.top-2,close.right+2,close.bottom+2});

        // строка меню: «Файл» и «Правка» открывают меню объекта и папки
        Fill(s,{win.left+4,win.top+30,win.right-4,win.top+52},0xECE9D8);
        Fill(s,{win.left+4,win.top+52,win.right-4,win.top+53},0xD6D2C2);
        auto menuItem=[&](int target,const std::u16string& label,Rect r) {
            const bool on=target!=None && Enabled(target);
            if(on && (hover_==target || pressed_==target))Fill(s,r,0x316AC5);
            Text(s,{r.left+7,r.top+2,r.right,r.bottom},label,on && (hover_==target || pressed_==target)?0xFFFFFF:on || target==None?0x000000:0xA0A0A0,14);
            if(on && focus_==target)xp::DrawFocusRect(s,r);
        };
        menuItem(explorer?FileActions:None,u"Файл",Bounds(FileActions));
        menuItem(explorer?FolderActions:None,u"Правка",Bounds(FolderActions));
        int mx=Bounds(FolderActions).right+4;
        for(const char16_t* m:{u"Вид",u"Избранное",u"Сервис",u"Справка"}) {
            const int w=TextWidth(m,14,false)+14;menuItem(None,m,{mx,win.top+32,mx+w,win.top+50});mx+=w+2;
        }

        // панель кнопок
        Fill(s,{win.left+4,win.top+53,win.right-4,win.top+92},0xECE9D8);
        Fill(s,{win.left+4,win.top+92,win.right-4,win.top+93},0xD6D2C2);
        auto toolButton=[&](int target,xp::ToolGlyph glyph,const std::u16string& label,Rect r) {
            const bool on=target!=None && Enabled(target);
            xp::DrawToolButton(s,r,on && hover_==target,on && pressed_==target && hover_==target);
            xp::DrawToolGlyph(s,glyph,r.left+5,r.top+(r.Height()-24)/2,24,on);
            if(!label.empty())Text(s,{r.left+33,r.top+9,r.right-2,r.bottom},label,on?0x000000:0xA0A0A0,14);
            if(on && focus_==target)xp::DrawFocusRect(s,{r.left+2,r.top+2,r.right-2,r.bottom-2});
        };
        const auto back=Bounds(NavBack);
        toolButton(explorer?NavBack:None,xp::ToolGlyph::Back,u"Назад",back);
        toolButton(None,xp::ToolGlyph::Forward,u"",{back.right+2,back.top,back.right+40,back.bottom});
        toolButton(explorer?NavUp:None,xp::ToolGlyph::Up,u"",Bounds(NavUp));
        xp::DrawToolSeparator(s,Bounds(NavUp).right+5,back.top+3,back.bottom-3);
        toolButton(None,xp::ToolGlyph::Search,u"Поиск",{Bounds(NavUp).right+11,back.top,Bounds(FolderTreeButton).left-2,back.bottom});
        toolButton(explorer?FolderTreeButton:None,xp::ToolGlyph::Folders,u"Папки",Bounds(FolderTreeButton));
        xp::DrawToolSeparator(s,Bounds(FolderTreeButton).right+5,back.top+3,back.bottom-3);
        toolButton(explorer?Sort:None,xp::ToolGlyph::Views,!explorer?u"":browser_.Ordering()==ShellBrowser::Sort::Name?u"Имя ↑":browser_.Ordering()==ShellBrowser::Sort::NameDescending?u"Имя ↓":u"Размер ↓",Bounds(Sort));

        // адресная строка с кнопкой «Переход» (обновляет текущую папку)
        Fill(s,{win.left+4,win.top+93,win.right-4,win.top+122},0xECE9D8);
        Text(s,{win.left+10,win.top+99,win.left+60,win.top+120},u"Адрес",0x7A7A7A,14);
        const auto address=Bounds(Address);
        xp::DrawEditField(s,address);
        xp::DrawIcon(s,explorer?(browser_.Path().empty()?xp::IconKind::Computer:xp::IconKind::Folder):xp::IconKind::Programs,address.left+3,address.top+4,16);
        Text(s,{address.left+24,address.top+4,address.right-6,address.bottom},explorer?location:programs?u"Все программы (ROMFS, SD, C:\\Programs)":u"Switch Recompiler",0x000000,14);
        if(explorer && hover_==Address)Frame(s,address,0x316AC5);
        if(explorer && focus_==Address)xp::DrawFocusRect(s,{address.left+2,address.top+2,address.right-2,address.bottom-2});
        const auto go=Bounds(Refresh);
        xp::DrawToolButton(s,go,explorer && hover_==Refresh,explorer && pressed_==Refresh && hover_==Refresh);
        xp::DrawToolGlyph(s,xp::ToolGlyph::Go,go.left+4,go.top+3,18,explorer);
        Text(s,{go.left+26,go.top+4,go.right,go.bottom},u"Переход",explorer?0x000000:0xA0A0A0,14);
        if(explorer && focus_==Refresh)xp::DrawFocusRect(s,go);

        // тело окна: синяя панель задач слева, белая область справа
        const Rect pane=TaskPaneRect(),content=ContentRect(),status=StatusRect();
        xp::DrawTaskPane(s,pane);
        Fill(s,content,0xFFFFFF);
        Fill(s,{content.left-1,content.top,content.left,content.bottom},0x7F9DB9);
        auto group=[&](int top,int bottom,const std::u16string& caption,bool special) {
            xp::DrawTaskGroup(s,{pane.left+10,top,pane.right-10,bottom},25,special);
            Text(s,{pane.left+20,top+6,pane.right-32,top+24},caption,special?0xFFFFFF:0x215DC6,12,true);
        };
        auto link=[&](int target,xp::IconKind icon,const std::u16string& label) {
            const auto r=Bounds(target);const bool on=Enabled(target),hot=on && (hover_==target || pressed_==target);
            xp::DrawIcon(s,icon,r.left,r.top+1,16);
            const uint32_t color=!on?0x8D9AC0:hot?0x428EFF:0x215DC6;
            Text(s,{r.left+22,r.top+2,r.right,r.bottom},label,color,13);
            if(hot)Fill(s,{r.left+22,r.bottom-3,std::min(r.right,r.left+22+TextWidth(label,13,false)),r.bottom-2},color);
            if(on && focus_==target)xp::DrawFocusRect(s,{r.left+20,r.top,std::min(r.right,r.left+26+TextWidth(label,13,false)),r.bottom});
        };
        auto details=[&](int top,const std::u16string& heading,const std::u16string& text) {
            group(top,pane.bottom-10,u"Подробно",false);
            Text(s,{pane.left+20,top+32,pane.right-20,top+50},heading,0x000000,13,true);
            WrapText(s,{pane.left+20,top+52,pane.right-20,pane.bottom-14},text,0x000000,12);
        };
        if(explorer) {
            group(Bounds(Launch).top-30,Bounds(Properties).bottom+6,u"Задачи для файлов и папок",true);
            link(Launch,xp::IconKind::Application,u"Открыть");
            link(OpenWith,xp::IconKind::Application,u"Открыть с помощью…");
            link(NewFolder,xp::IconKind::Folder,u"Создать новую папку");
            link(Rename,xp::IconKind::File,u"Переименовать");
            link(Copy,xp::IconKind::File,u"Копировать");
            link(Paste,xp::IconKind::FolderOpen,u"Вставить");
            link(Delete,xp::IconKind::RecycleBin,u"Удалить");
            link(Properties,xp::IconKind::File,u"Свойства");
            group(Bounds(QuickComputer).top-30,Bounds(QuickR).bottom+6,u"Другие места",false);
            link(QuickComputer,xp::IconKind::Computer,u"Мой компьютер");
            link(QuickDocuments,xp::IconKind::Documents,u"Мои документы");
            link(QuickC,xp::IconKind::HardDrive,u"Локальный диск (C:)");
            link(QuickS,xp::IconKind::SdCard,u"SD-карта (S:)");
            link(QuickR,xp::IconKind::CdDrive,u"ROMFS (R:)");

            // содержимое: плитки — крупный значок, имя, тип и размер
            const auto& entries=browser_.Entries();const size_t first=browser_.Selected()/Rows*Rows;
            for(size_t i=first;i<entries.size() && i<first+Rows;++i) {
                const auto& e=entries[i];const int target=BrowserRowBase+int(i-first);const auto r=Bounds(target);
                const bool chosen=i==browser_.Selected();
                xp::DrawIcon(s,EntryIcon(e),r.left+6,r.top+(r.Height()-48)/2,48,chosen);
                const int tx=r.left+62;
                const auto name=Wide(e.name);
                const int nw=std::min(r.right-tx,TextWidth(name,14,false)+4);
                if(chosen)Fill(s,{tx-2,r.top+10,tx+nw,r.top+28},0x316AC5);
                Text(s,{tx,r.top+11,r.right-4,r.top+28},name,chosen?0xFFFFFF:0x000000,14);
                Text(s,{tx,r.top+30,r.right-4,r.top+46},EntryType(e),0x7F7F7F,12);
                const auto extra=EntrySize(e);
                if(!extra.empty())Text(s,{tx,r.top+46,r.right-4,r.top+62},extra,0x7F7F7F,12);
                if(focus_==target)xp::DrawFocusRect(s,{tx-2,r.top+10,tx+nw,r.top+28});
            }
            if(entries.empty())Text(s,{content.left+20,content.top+20,content.right-20,content.top+44},browser_.Errors() || browser_.Truncated()?u"Нет объектов для отображения.":u"Эта папка пуста.",0x7F7F7F,14);

            std::u16string note;
            if(browser_.LastError()!=ShellBrowser::Error::None)note=ShellBrowser::ErrorText(browser_.LastError());
            else if(browser_.PreferenceWarning())note=browser_.PreferenceText();
            else if(!operationNotice_.empty())note=operationNotice_;
            else {
                if(browser_.Truncated())note+=u"Список неполный: достигнут предел просмотра. ";
                if(browser_.Errors())note+=u"Некоторые объекты недоступны. ";
                if(note.empty() && browser_.CanChooseApplication()) {
                    const auto app=browser_.ApplicationFor(browser_.SelectedDocument());
                    note=app.empty()?u"Для этого файла не выбрана программа: «Открыть с помощью…».":u"Открывается программой "+Wide(app.substr(app.find_last_of('\\')+1))+u".";
                }
                if(note.empty() && !entries.empty() && browser_.Selected()<entries.size())note=EntryType(entries[browser_.Selected()])+u". "+EntrySize(entries[browser_.Selected()]);
            }
            const std::u16string heading=entries.empty() || browser_.Selected()>=entries.size()?title:Wide(entries[browser_.Selected()].name);
            details(Bounds(QuickR).bottom+18,heading,note);
            xp::DrawStatusBar(s,status);
            char count[96];std::snprintf(count,sizeof(count),"Объектов: %zu",entries.size());
            Text(s,{status.left+8,status.top+5,status.left+300,status.bottom},Wide(count),browser_.LastError()!=ShellBrowser::Error::None?0xA33C22:0x000000,13);
        } else if(programs) {
            group(Bounds(Launch).top-30,Bounds(Launch).bottom+6,u"Задачи для программ",true);
            link(Launch,xp::IconKind::Application,u"Запустить программу");
            const std::u16string note=(truncated_ || linkTruncated_)?u"Достигнут предел поиска: список неполный.":(errors_ || linkErrors_)?u"Часть папок недоступна: список может быть неполным.":u"Программы из ROMFS, с SD-карты и из C:\\Programs. L при запуске — прежний режим, ZL — программный вывод.";
            details(Bounds(Launch).bottom+18,files_.empty()?u"Программы не найдены":Wide(files_[selected_].substr(files_[selected_].find_last_of("\\/")+1)),note);
            for(size_t i=First();i<files_.size() && i<First()+Rows;++i) {
                const int target=RowBase+int(i-First());const auto r=Bounds(target);
                const bool chosen=i==selected_;
                xp::DrawIcon(s,xp::IconKind::Application,r.left+4,r.top+(r.Height()-24)/2,24,chosen);
                // имя файла, а за ним серым — папка, где он лежит
                const auto cut=files_[i].find_last_of("\\/");
                const auto name=Wide(cut==std::string::npos?files_[i]:files_[i].substr(cut+1));
                const int nw=std::min(r.right-r.left-36,TextWidth(name,14,false)+4);
                if(chosen)Fill(s,{r.left+32,r.top+7,r.left+34+nw,r.top+25},0x316AC5);
                Text(s,{r.left+34,r.top+8,r.right-4,r.top+26},name,chosen?0xFFFFFF:0x000000,14);
                if(cut!=std::string::npos)Text(s,{r.left+44+nw,r.top+9,r.right-4,r.top+26},Wide(files_[i].substr(0,cut)),0x7F7F7F,12);
                if(focus_==target)xp::DrawFocusRect(s,{r.left+32,r.top+7,r.left+34+nw,r.top+25});
            }
            if(files_.empty())Text(s,{content.left+20,content.top+20,content.right-20,content.top+44},u"Программы не найдены. Добавьте EXE на SD-карту и откройте оболочку снова.",0x7F7F7F,14);
            xp::DrawStatusBar(s,status);
            char count[96];std::snprintf(count,sizeof(count),"Программа %zu из %zu",files_.empty()?0:selected_+1,files_.size());
            Text(s,{status.left+8,status.top+5,status.left+300,status.bottom},Wide(count),0x000000,13);
        } else {
            group(pane.top+12,pane.top+140,u"Switch Recompiler",true);
            Text(s,{pane.left+20,pane.top+46,pane.right-20,pane.top+64},u"Версия для Nintendo Switch",0x000000,13);
            details(pane.top+156,u"Сведения",u"Значки и обои нарисованы в духе Windows XP; картинки Microsoft не используются. Шрифты Liberation: OFL.");
            xp::DrawIcon(s,xp::IconKind::Computer,content.left+30,content.top+30,48);
            Text(s,{content.left+96,content.top+36,content.right-20,content.top+70},u"Switch Recompiler",0x0A246A,26,true);
            Text(s,{content.left+96,content.top+100,content.right-20,content.top+126},u"Оболочка для запуска поддерживаемых программ Win32.",0x000000,16);
            Text(s,{content.left+96,content.top+140,content.right-20,content.top+164},u"C: — постоянный диск: SD / switch-recompiler / drive_c",0x000000,15);
            Text(s,{content.left+96,content.top+170,content.right-20,content.top+194},u"S: — SD-карта целиком    •    R: — ROMFS, только чтение",0x000000,15);
            Text(s,{content.left+96,content.top+220,content.right-20,content.top+244},u"После выхода из программы вы вернётесь в прежнюю папку.",0x4D4D4D,15);
            xp::DrawStatusBar(s,status);
        }
        if(explorer || programs) {
            // листание страниц в строке состояния
            auto pageButton=[&](int target,const std::u16string& label) {
                const auto r=Bounds(target);const bool on=Enabled(target);
                xp::DrawPushButton(s,r,on,on && hover_==target,on && pressed_==target && hover_==target,on && focus_==target);
                Text(s,{r.left+(r.Width()-TextWidth(label,12,true))/2,r.top+3,r.right,r.bottom},label,on?0x000000:0xA0A0A0,12,true);
            };
            const size_t total=explorer?browser_.Entries().size():files_.size(),first=explorer?browser_.Selected()/Rows*Rows:First();
            char page[64];std::snprintf(page,sizeof(page),"%zu–%zu",total?first+1:0,std::min(total,first+size_t(Rows)));
            const auto prev=Bounds(Previous);
            Text(s,{prev.left-120,status.top+5,prev.left-8,status.bottom},Wide(page),0x000000,13);
            pageButton(Previous,u"◄");pageButton(Next,u"►");
        }
    }
    // Панель задач Luna: зелёная «пуск», кнопка программы, трей с часами
    xp::DrawTaskbar(s,{0,680,Width,720});
    const bool startDown=startOpen_ || pressed_==Start;
    xp::DrawStartButton(s,Bounds(Start),startDown,focus_==Start || hover_==Start);
    xp::DrawStartGlyph(s,12+(startDown?1:0),690+(startDown?1:0));
    Text(s,{41+(startDown?1:0),688+(startDown?1:0),137,717},u"пуск",0x1D4E1D,23,true); // тень
    Text(s,{40+(startDown?1:0),687+(startDown?1:0),136,716},u"пуск",0xFFFFFF,23,true);
    if(guestActive_) {
        const auto task=Bounds(GuestTask);
        xp::DrawTaskButton(s,task,!guestMinimized_,hover_==GuestTask);
        Icon(s,task.left+3,task.top,shell_art::programs);
        Text(s,{task.left+40,task.top+7,task.right-8,task.bottom},(guestTaskPending_?u"Ожидание · ":u"")+Wide(guestLabel_),0xFFFFFF,14,true);
    }
    xp::DrawTray(s,{1158,680,1280,720});
    Text(s,{1186,690,1272,716},clock,0xFFFFFF,16);
    if(startOpen_) {
        // Меню «Пуск» Windows XP: шапка с пользователем, два столбца, низ с выключением
        const Rect menu{0,284,480,680};
        xp::DrawStartMenuFrame(s,menu,62,44,240);
        xp::DrawUserPicture(s,10,292,48);
        Text(s,{68,305,470,340},u"Player",0x0A2B7A,22,true);Text(s,{67,304,469,339},u"Player",0xFFFFFF,22,true);
        auto item=[&](int target,xp::IconKind icon,int iconSize,const std::u16string& title,const std::u16string& subtitle,bool bold) {
            const auto r=Bounds(target);const bool on=Enabled(target),hot=on && (focus_==target || hover_==target);
            if(hot)Fill(s,r,pressed_==target?0x1D4C99:0x316AC5);
            const int iy=r.top+(r.Height()-iconSize)/2;
            xp::DrawIcon(s,icon,r.left+6,iy,iconSize);
            const int tx=r.left+iconSize+14;
            const uint32_t color=!on?0x9AA6B8:hot?0xFFFFFF:0x000000;
            if(subtitle.empty())Text(s,{tx,r.top+(r.Height()-18)/2,r.right-4,r.bottom},title,color,14,bold);
            else {
                Text(s,{tx,r.top+(r.Height()-34)/2,r.right-4,r.bottom},title,color,14,true);
                Text(s,{tx,r.top+(r.Height()-34)/2+18,r.right-4,r.bottom},subtitle,!on?0x9AA6B8:hot?0xDCE8FA:0x6D6D6D,12);
            }
        };
        item(Settings,xp::IconKind::Computer,32,u"Настройки оболочки",u"Сортировка и ассоциации",true);
        item(About,xp::IconKind::Application,32,u"О программе",u"Switch Recompiler",true);
        xp::DrawMenuSeparator(s,14,232,574);
        {
            const auto r=Bounds(MenuPrograms);const bool hot=Enabled(MenuPrograms) && (focus_==MenuPrograms || hover_==MenuPrograms);
            if(hot)Fill(s,r,pressed_==MenuPrograms?0x1D4C99:0x316AC5);
            const std::u16string label=u"Все программы";
            const int w=TextWidth(label,14,true),x=r.left+(r.Width()-w-30)/2;
            Text(s,{x,r.top+9,r.right,r.bottom},label,hot?0xFFFFFF:0x000000,14,true);
            xp::DrawAllProgramsArrow(s,x+w+10,r.top+8,20);
        }
        item(MenuDocuments,xp::IconKind::Documents,24,u"Мои документы",u"",true);
        item(MenuComputer,xp::IconKind::Computer,24,u"Мой компьютер",u"",true);
        xp::DrawMenuSeparator(s,252,468,442);
        item(MenuRun,xp::IconKind::Application,24,u"Выполнить…",u"",false);
        WrapText(s,{256,496,470,580},guestActive_?u"Запущена программа: одна за раз.":browser_.Ready()?u"Выполнить: путь к программе и аргументы.":u"Файлы недоступны.",0x6D7F99,12);
        // «Выключение» — выход из оболочки в меню Homebrew
        {
            const auto r=Bounds(Exit);const bool hot=focus_==Exit || hover_==Exit;
            if(hot)Fill(s,r,0x2A6ADB);
            xp::DrawPowerGlyph(s,r.left+4,r.top+3,28,hot);
            Text(s,{r.left+39,r.top+9,r.right,r.bottom},u"Выйти в Homebrew",0xFFFFFF,14);
        }
    }
    if(contextOpen_) {
        Fill(s,{contextRect_.left+3,contextRect_.top+3,contextRect_.right+3,contextRect_.bottom+3},0x526477);
        Fill(s,contextRect_,0xFFFFFF);Frame(s,contextRect_,0x52779D);
        for(size_t i=0;i<contextCount_;++i) {
            const int target=ContextBase+int(i),action=contextActions_[i];const auto r=ContextBounds(target);
            const bool enabled=Enabled(target),hot=enabled && (focus_==target || hover_==target);
            if(hot)Fill(s,r,pressed_==target?0x1D4C99:0x316AC5);
            const auto label=action==Launch?u"Открыть":action==OpenWith?u"Выбрать приложение…":action==Copy?u"Копировать":
                action==Rename?u"Переименовать…":action==Delete?u"Удалить…":action==Properties?u"Свойства":
                action==DesktopShortcut?u"Ярлык на рабочий стол…":action==StartShortcut?u"Ярлык в Пуск…":action==ImageOpen?u"Просмотреть BMP…":action==TextOpen?u"Текст: читать / изменить…":action==TextNew?u"Новый TXT…":action==Paste?u"Вставить сюда…":action==NewFolder?u"Новая папка…":u"Обновить";
            Text(s,{r.left+12,r.top+6,r.right-8,r.bottom-2},label,!enabled?0x8793A0:hot?0xFFFFFF:0x162B47,16);
        }
    }
    if(dialog_!=Dialog::None) {
        // диалог — окно XP поверх проводника
        xp::DrawWindowFrame(s,{396,196,1028,546},34);
        const std::u16string dialogTitle=dialog_==Dialog::DeleteConfirm?u"Подтверждение удаления":dialog_==Dialog::CopyProgress?u"Копирование":dialog_==Dialog::Preferences?u"Настройки оболочки":TextDialog()?u"Текстовый документ":dialog_==Dialog::ImageViewer?u"Просмотр BMP":dialog_==Dialog::FolderTree?u"Дерево папок":u"Свойства";
        Text(s,{411,204,1010,230},dialogTitle,0x0A246A,16,true);Text(s,{410,203,1009,229},dialogTitle,0xFFFFFF,16,true);
        const auto slash=operationPath_.find_last_of('\\');
        if(dialog_!=Dialog::Preferences && !TextDialog() && dialog_!=Dialog::ImageViewer && dialog_!=Dialog::FolderTree)Text(s,{416,250,1006,278},Wide(operationPath_.substr(slash==std::string::npos?0:slash+1)),0x123D75,18,true);
        if(dialog_!=Dialog::Preferences && !TextDialog() && dialog_!=Dialog::ImageViewer && dialog_!=Dialog::FolderTree)Text(s,{416,282,1006,304},Wide(operationPath_),0x42576B,13);
        if(dialog_==Dialog::FolderTree) {
            Text(s,{416,240,1006,264},tree_.Error()?u"Папка недоступна или достигнут предел.":tree_.Truncated()?u"Показана часть папок: предел дерева/поиска.":u"↑/↓ выбор, → развернуть, ← свернуть, Открыть: перейти",0x42576B,13);
            for(size_t n=0;n<7;++n) {
                const auto i=treeSelection_/7*7+n;if(i>=tree_.Nodes().size())break;const auto& node=tree_.Nodes()[i];const auto r=Bounds(TreeRowBase+int(n));
                Fill(s,r,i==treeSelection_?0xD7E8FC:0xFFFFFF);Text(s,{r.left+6+int(std::min(node.depth,20u))*12,r.top+5,r.right-6,r.bottom},(node.expanded?u"− ":u"+ ")+Wide(node.name),0x182943,14);
            }
            control(TreeExpand,u"Развернуть",Bounds(TreeExpand));control(TreeCollapse,u"Свернуть",Bounds(TreeCollapse));control(TreeOpen,u"Открыть",Bounds(TreeOpen));control(CancelDialog,u"Закрыть",Bounds(CancelDialog));
        } else if(dialog_==Dialog::ImageViewer) {
            char info[100];std::snprintf(info,sizeof(info),"%d × %d   %s   ←/→/↑/↓",image_.Width(),image_.Height(),imageFit_?"Вписать":"100%");
            Text(s,{416,240,1006,260},Wide(image_.Path()),0x123D75,14,true);
            Text(s,{416,260,1006,280},Wide(info),0x42576B,13);
            image_.Draw(s,ImageViewport(),imageFit_,imagePanX_,imagePanY_);
            control(ImageFit,u"Вписать",Bounds(ImageFit));control(ImageActual,u"100%",Bounds(ImageActual));control(CancelDialog,u"Закрыть",Bounds(CancelDialog));
        } else if(TextDialog()) {
            Text(s,{416,240,1006,266},Wide(document_.Path().empty()?"Новый документ":document_.Path())+(document_.Dirty()?u" *":u""),0x123D75,15,true);
            if(dialog_==Dialog::TextClose) {
                Text(s,{416,300,1006,332},u"Есть несохранённый текст.",0x24354A,20,true);
                Text(s,{416,344,1006,372},u"Сохраните, выберите новое имя или отмените закрытие.",0x42576B,15);
                control(TextDiscard,u"Не сохранять",Bounds(TextDiscard));
            } else {
                char info[100];std::snprintf(info,sizeof(info),"Строка %zu/%zu, с позиции %zu",textSelection_+1,document_.Lines().size(),textColumn_+1);
                Text(s,{416,270,1006,296},Wide(info)+u"  ↑/↓ строки, ←/→ прокрутка",0x42576B,13);
                for(size_t row=0;row<5;++row) {
                    const auto i=textSelection_/5*5+row;if(i>=document_.Lines().size())break;
                    const auto r=Bounds(TextLineBase+int(row));Fill(s,r,i==textSelection_?0xD7E8FC:0xFFFFFF);
                    const auto& line=document_.Lines()[i].text;size_t column=i==textSelection_?std::min(textColumn_,line.size()):0;
                    if(column<line.size() && line[column]>=0xdc00 && line[column]<=0xdfff && column)--column;
                    auto visible=line.substr(column,120);if(!visible.empty() && visible.back()>=0xd800 && visible.back()<=0xdbff)visible.pop_back();std::replace(visible.begin(),visible.end(),u'\t',u' ');
                    Text(s,{r.left+5,r.top+4,r.right-5,r.bottom},visible,0x182943,15);
                }
                control(TextEdit,u"Изменить…",Bounds(TextEdit));control(TextInsert,u"+ строка",Bounds(TextInsert));control(TextErase,u"− строка",Bounds(TextErase));
            }
            Text(s,{416,488,1006,504},textNotice_.empty()?u"До 64 КиБ; строка ввода до 500 UTF-16. Y/Tab: кнопки.":textNotice_,0x8B3F24,12);
            control(TextSave,u"Сохранить",Bounds(TextSave));control(TextSaveAs,u"Как…",Bounds(TextSaveAs));
            control(CancelDialog,dialog_==Dialog::TextClose?u"Отмена":u"Закрыть",Bounds(CancelDialog));
        } else if(dialog_==Dialog::Preferences) {
            Text(s,{416,246,1006,272},u"Сортировка проводника",0x24354A,17,true);
            control(PreferenceSort,browser_.Ordering()==ShellBrowser::Sort::Name?u"Имя ↑":browser_.Ordering()==ShellBrowser::Sort::NameDescending?u"Имя ↓":u"Размер ↓",Bounds(PreferenceSort));
            const auto& associations=browser_.Associations();char count[64];
            std::snprintf(count,sizeof(count),"%zu / %zu",associations.empty()?0:preferenceSelection_+1,associations.size());
            Text(s,{416,322,1006,346},u"Типы файлов: "+Wide(count)+u"  (↑/↓ — выбор)",0x24354A,16,true);
            Text(s,{416,354,1006,382},preferenceSelection_<associations.size()?Wide("."+associations[preferenceSelection_].extension+" → "+associations[preferenceSelection_].application):u"Ассоциаций пока нет. Откройте документ и выберите приложение.",0x42576B,14);
            control(AssociationPrevious,u"←",Bounds(AssociationPrevious));control(AssociationNext,u"→",Bounds(AssociationNext));
            control(AssociationChange,u"Сменить EXE…",Bounds(AssociationChange));control(AssociationForget,u"Забыть тип",Bounds(AssociationForget));
            Text(s,{416,444,1006,470},browser_.LastError()!=ShellBrowser::Error::None?ShellBrowser::ErrorText(browser_.LastError()):browser_.PreferenceText(),browser_.PreferenceWarning() || browser_.LastError()!=ShellBrowser::Error::None?0xA33C22:0x42576B,14);
            Text(s,{416,472,1006,496},browser_.PreferenceWarning()?u"Текущие изменения доступны сейчас. Сохранение можно повторить.":u"Приложения проверяются при открытии документа.",0x526477,13);
            control(PreferenceSave,u"Сохранить",Bounds(PreferenceSave));control(CancelDialog,u"Закрыть",Bounds(CancelDialog));
        } else if(dialog_==Dialog::CopyProgress) {
            char bytes[64];std::snprintf(bytes,sizeof(bytes),"%llu",static_cast<unsigned long long>(browser_.CopyBytes()));
            Text(s,{416,324,1006,350},u"Записано: "+Wide(bytes)+u" байт",0x24354A,18,true);
            Text(s,{416,368,1006,396},u"Источник: "+Wide(clipboardPath_),0x42576B,14);
            Text(s,{416,410,1006,440},u"Существующие файлы не заменяются.",0x42576B,16);
            Text(s,{416,454,1006,482},u"B: отмена копирования",0x526477,14);
            control(CancelDialog,u"Отмена",Bounds(CancelDialog));
        } else if(dialog_==Dialog::DeleteConfirm) {
            Text(s,{416,324,1006,350},u"Удалить выбранный объект безвозвратно?",0x24354A,18,true);
            Text(s,{416,368,1006,396},u"Корзины нет. Восстановление не предусмотрено.",0x9D3826,16);
            Text(s,{416,410,1006,440},u"Папку можно удалить только если она пуста.",0x42576B,16);
            control(ConfirmDelete,u"Удалить",Bounds(ConfirmDelete));control(CancelDialog,u"Отмена",Bounds(CancelDialog));
        } else {
            Text(s,{416,314,1006,340},properties_.kind==rt::FileProperties::Kind::Directory?u"Тип: папка":u"Тип: файл",0x24354A,16);
            char bytes[80];std::snprintf(bytes,sizeof(bytes),"%llu",static_cast<unsigned long long>(properties_.size));
            Text(s,{416,346,1006,372},properties_.kind==rt::FileProperties::Kind::Directory?u"Размер содержимого: не подсчитан":u"Размер: "+Wide(bytes)+u" байт",0x24354A,16);
            const auto timestamp=[](int64_t seconds) {
                const time_t time=static_cast<time_t>(seconds);struct tm utc{};char text[64];
                if(!gmtime_r(&time,&utc) || !std::strftime(text,sizeof(text),"%Y-%m-%d %H:%M:%S UTC",&utc))return std::u16string(u"недоступно");
                return Wide(text);
            };
            Text(s,{416,378,1006,404},u"Изменён: "+timestamp(properties_.modifiedSeconds),0x24354A,16);
            Text(s,{416,410,1006,436},u"Доступ: "+timestamp(properties_.accessedSeconds),0x24354A,16);
            Text(s,{416,448,1006,474},properties_.mountReadOnly?u"Том: только чтение":u"Режим тома: запись разрешена",0x42576B,16);
            Text(s,{416,476,1006,500},u"Время создания и размер папки не определяются.",0x526477,13);
            control(CancelDialog,u"Закрыть",Bounds(CancelDialog));
        }
    }

}
} // namespace ui
