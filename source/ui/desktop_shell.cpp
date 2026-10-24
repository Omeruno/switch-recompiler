#include "desktop_shell.h"
#include "font.h"
#include "shell_icons.h"
#include <algorithm>
#include <cstdio>
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
    case Copy:return {522,578,628,614};
    case Paste:return {636,578,744,614};
    case NewFolder:return {240,430,406,458};
    case Rename:return {240,462,406,490};
    case Delete:return {240,494,406,522};
    case Properties:return {240,526,406,554};
    case ConfirmDelete:return {664,504,828,540};
    case CancelDialog:return {840,504,1006,540};
    case GuestTask:return {158,684,468,716};
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
    case OpenWith:return {752,578,944,614};
    case NavBack:return {240,150,326,178};
    case NavUp:return {332,150,406,178};
    case Refresh:return {412,150,516,178};
    case Sort:return {522,150,644,178};
    case Address:return {652,150,1138,178};
    case QuickComputer:return {240,194,406,230};
    case QuickDocuments:return {240,238,406,274};
    case QuickC:return {240,300,406,336};
    case QuickS:return {240,344,406,380};
    case QuickR:return {240,388,406,424};
    default:
        if(t>=BrowserRowBase && t<BrowserRowBase+Rows)return {422,184+(t-BrowserRowBase)*26,1138,210+(t-BrowserRowBase)*26};
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
bool DesktopShell::ConfigureFilesystem(const std::string& system,const std::string& sd,const std::string& rom,std::string& error) {
    browser_.CancelCopy();clipboardPath_.clear();clipboardName_.clear();
    return browser_.Configure(system,sd,rom,error);
}
void DesktopShell::SetStatus(const std::string& status) {status_=status.substr(0,240);}
void DesktopShell::SetGuestActive(const std::string& label) {
    returnPanel_=panel_;returnFocus_=focus_;guestActive_=true;guestMinimized_=false;guestTaskPending_=false;
    guestLabel_=label.substr(0,240);panel_=Panel::Desktop;Enter();
}
void DesktopShell::EndGuest() {
    browser_.CancelCopy();
    guestActive_=false;guestLabel_.clear();panel_=returnPanel_;focus_=returnFocus_;
    request_.Clear();startOpen_=false;pressed_=None;dialog_=Dialog::None;textInput_=TextInput::None;operationPath_.clear();
}
void DesktopShell::Enter() {
    browser_.CancelCopy();
    dialog_=Dialog::None;operationPath_.clear();operationNotice_.clear();
    request_.Clear();pendingDocument_.clear();textInput_=TextInput::None;exit_=false;startOpen_=false;hover_=pressed_=None;
    if(panel_==Panel::Explorer){browser_.Refresh();FocusBrowserSelection();return;}
    focus_=panel_==Panel::Programs && !files_.empty()?RowBase+int(selected_%Rows):panel_==Panel::Desktop?Programs:Close;
}
bool DesktopShell::Enabled(int t) const {
    if(t==None)return false;
    if(textInput_!=TextInput::None)return false;
    if(dialog_!=Dialog::None)return t==CancelDialog || (dialog_==Dialog::DeleteConfirm && t==ConfirmDelete);
    if(t==ConfirmDelete || t==CancelDialog)return false;
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
    if(textInput_!=TextInput::None)return {None};
    if(dialog_==Dialog::DeleteConfirm)return {CancelDialog,ConfirmDelete};
    if(dialog_==Dialog::Properties || dialog_==Dialog::CopyProgress)return {CancelDialog};
    if(startOpen_) {
        std::vector<int> out;
        for(int t:{MenuPrograms,MenuComputer,MenuDocuments,MenuRun,About,Exit,Start})if(Enabled(t))out.push_back(t);
        return out;
    }
    if(panel_==Panel::About)return {Close,Start};
    if(panel_==Panel::Explorer) {
        std::vector<int> out;
        if(!browser_.Entries().empty())out.push_back(BrowserRowBase+int(browser_.Selected()%Rows));
        for(int t:{NavBack,NavUp,Address,Refresh,Sort,QuickComputer,QuickDocuments,QuickC,QuickS,QuickR,NewFolder,Rename,Delete,Properties,Copy,Paste,Previous,Next,OpenWith,Launch,Close,Start})
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
    if(selected_<files_.size())browser_.ResolveCatalogExecutable(files_[selected_],request_);
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
void DesktopShell::CloseDialog() {
    if(dialog_==Dialog::CopyProgress)browser_.CancelCopy();
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
    if(!accepted || purpose==TextInput::None){pendingDocument_.clear();operationPath_.clear();return;}
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
void DesktopShell::ToggleStart() {
    if(ModalActive())return;
    pressed_=None;
    if(!startOpen_) {menuReturnFocus_=focus_;startOpen_=true;focus_=MenuPrograms;}
    else {startOpen_=false;focus_=menuReturnFocus_;}
}
void DesktopShell::Move(int delta) {
    if(textInput_!=TextInput::None)return;
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
    if(ModalActive())return;
    if(panel_==Panel::Explorer && !startOpen_){browser_.Page(delta,Rows);FocusBrowserSelection();return;}
    if(panel_!=Panel::Programs || startOpen_ || files_.empty())return;
    const int page=std::max(0,std::min(int(First()/Rows)+delta,int((files_.size()-1)/Rows)));
    selected_=std::min(size_t(page*Rows)+selected_%Rows,files_.size()-1);focus_=RowBase+int(selected_%Rows);
}
void DesktopShell::Accept() {Activate(focus_);}
void DesktopShell::Back() {
    pressed_=None;
    if(textInput_!=TextInput::None){CompleteTextInput(false,"");return;}
    if(dialog_!=Dialog::None){const bool copying=dialog_==Dialog::CopyProgress;CloseDialog();if(copying)operationNotice_=u"Копирование отменено. Файл назначения не создан.";return;}
    if(startOpen_){ToggleStart();}
    else if(panel_==Panel::Explorer && browser_.CanBack()){browser_.Back();FocusBrowserSelection();}
    else if(panel_!=Panel::Desktop){panel_=Panel::Desktop;focus_=Programs;}
    else if(!guestActive_)exit_=true;
}
int DesktopShell::HitTest(int x,int y) const {
    if(textInput_!=TextInput::None)return None;
    if(dialog_!=Dialog::None) {
        if(Bounds(CancelDialog).Contains(x,y))return CancelDialog;
        if(dialog_==Dialog::DeleteConfirm && Bounds(ConfirmDelete).Contains(x,y))return ConfirmDelete;
        return None;
    }
    if(Bounds(Start).Contains(x,y))return Start;
    if(startOpen_) {
        for(int t:{MenuPrograms,MenuComputer,MenuDocuments,MenuRun,About,Exit})if(Bounds(t).Contains(x,y))return t;
        return None; // Menu is modal to clicks; no click-through to desktop/window.
    }
    if(panel_!=Panel::Desktop) {
        if(Bounds(Close).Contains(x,y))return Close;
        if(panel_==Panel::Explorer) {
            for(int t:{NavBack,NavUp,Refresh,Sort,Address,QuickComputer,QuickDocuments,QuickC,QuickS,QuickR,OpenWith,Launch,Previous,Next,NewFolder,Rename,Delete,Properties,Copy,Paste})if(Bounds(t).Contains(x,y))return t;
            for(int i=0;i<Rows;++i)if(Bounds(BrowserRowBase+i).Contains(x,y))return BrowserRowBase+i;
        }
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
    if(target==None){if(startOpen_){ToggleStart();}return;}
    if(!Enabled(target))return;
    focus_=target;
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
    if(target>=BrowserRowBase) {browser_.Select(browser_.Selected()/Rows*Rows+size_t(target-BrowserRowBase));OpenCurrent();return;}
    if(target>=RowBase) {selected_=First()+size_t(target-RowBase);LaunchProgram();return;}
    operationNotice_.clear();
    switch(target) {
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
    Fill(s,{0,0,Width,680},0x3A6EA5);

    if(!guestActive_)Text(s,{22,640,1250,663},u"Стик: курсор  •  A/X: открыть  •  Y: кнопки  •  R: Пуск  •  B: назад  •  +: выход",0xD3E6FA,14);
    Text(s,{22,663,1250,680},browser_.LastError()!=ShellBrowser::Error::None?ShellBrowser::ErrorText(browser_.LastError()):Wide(status_),0xC1DCF4,12);
    auto control=[&](int target,std::u16string label,Rect r) {
        const bool enabled=Enabled(target),hot=hover_==target && enabled,down=pressed_==target && hot;
        Gradient(s,r,target==Close?(down?0xA22917:0xEE9A84):down?0x255BB3:hot?0xFCFCFF:0xFFFFFF,
                 target==Close?0xC43A22:down?0x3E79CF:0xE4E1D1);
        Frame(s,r,enabled?0x3465A4:0xA0ABBA);
        if(focus_==target && enabled)Frame(s,{r.left+3,r.top+3,r.right-3,r.bottom-3},0x6684AE);
        Text(s,{r.left+10,r.top+7,r.right-7,r.bottom-2},label,target==Close?0xFFFFFF:!enabled?0x8793A0:down?0xFFFFFF:0x162B47,15,true);
    };
    for(int t:{Programs,Computer,Documents}) {
        const auto r=Bounds(t);const bool selected=focus_==t,enabled=Enabled(t);
        if(selected || (hover_==t && enabled)){Fill(s,r,pressed_==t?0x15457E:0x285F97);Frame(s,r,0x81B4ED);}
        Icon(s,r.left+40,r.top+6,t==Programs?shell_art::programs:t==Computer?shell_art::computer:shell_art::folder);
        Text(s,{r.left+4,r.top+44,r.right-2,r.top+66},t==Programs?u"Программы":t==Computer?u"Мой":u"Мои",enabled?0xFFFFFF:0xA7BDD3,14,true);
        if(t!=Programs)Text(s,{r.left+4,r.top+64,r.right-2,r.bottom},t==Computer?u"компьютер":u"документы",enabled?0xFFFFFF:0xA7BDD3,14,true);
    }
    if(panel_!=Panel::Desktop) {
        Fill(s,{224,106,1166,630},0x13385B);
        Fill(s,{218,100,1160,624},0xECE9D8);Frame(s,{218,100,1160,624},0x0646A4);
        Gradient(s,{220,102,1158,112},0x5C9CFF,0x0A5AE7);
        Gradient(s,{220,112,1158,146},0x0868F4,0x0347BD);
        Text(s,{234,110,1100,142},panel_==Panel::Programs?u"Все программы — запуск EXE":panel_==Panel::Explorer?u"Проводник":u"О Switch Recompiler",0xFFFFFF,20,true);
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
            Text(s,{242,556,1136,577},truncated_?u"Достигнут лимит поиска: список неполный.":errors_?u"Часть каталогов недоступна: список может быть неполным.":u"Новый UI по умолчанию. L: прежний режим; ZL: программный вывод.",0x5A6574,13);
        } else if(panel_==Panel::Explorer) {
            control(NavBack,u"← Назад",Bounds(NavBack));control(NavUp,u"Вверх",Bounds(NavUp));
            control(Refresh,u"Обновить",Bounds(Refresh));
            control(Sort,browser_.Ordering()==ShellBrowser::Sort::Name?u"Имя ↑":browser_.Ordering()==ShellBrowser::Sort::NameDescending?u"Имя ↓":u"Размер ↓",Bounds(Sort));
            control(Address,browser_.Path().empty()?u"Мой компьютер":Wide(browser_.Path()),Bounds(Address));
            Fill(s,{240,184,406,554},0xD8E8F7);
            control(QuickComputer,u"Мой компьютер",Bounds(QuickComputer));
            control(QuickDocuments,u"Мои документы",Bounds(QuickDocuments));
            control(QuickC,u"C:  Система",Bounds(QuickC));control(QuickS,u"S:  SD-карта",Bounds(QuickS));
            control(QuickR,u"R:  ROMFS",Bounds(QuickR));
            control(NewFolder,u"Новая папка…",Bounds(NewFolder));control(Rename,u"Переименовать…",Bounds(Rename));
            control(Delete,u"Удалить…",Bounds(Delete));control(Properties,u"Свойства",Bounds(Properties));
            Fill(s,{422,184,1138,554},0xFFFFFF);Frame(s,{421,183,1139,555},0x9EACB9);
            const auto& entries=browser_.Entries();const size_t first=browser_.Selected()/Rows*Rows;
            for(size_t i=first;i<entries.size() && i<first+Rows;++i) {
                const auto& e=entries[i];const int target=BrowserRowBase+int(i-first);const auto r=Bounds(target);
                const bool chosen=i==browser_.Selected();
                if(chosen)Fill(s,r,0x316AC5);else if(hover_==target)Fill(s,r,0xE6F0FF);
                if(focus_==target)Frame(s,r,0x92B8EF);
                const uint32_t color=chosen?0xFFFFFF:0x203044;
                Text(s,{r.left+8,r.top+3,860,r.bottom-1},Wide(e.name),color,14);
                const bool exe=ShellBrowser::IsExecutable(e.path);
                Text(s,{866,r.top+3,994,r.bottom-1},e.kind==ShellBrowser::Kind::Drive?u"Том":e.kind==ShellBrowser::Kind::Directory?u"Папка":exe?u"Приложение":u"Документ",color,13);
                if(e.kind!=ShellBrowser::Kind::Directory) {
                    char size[48];
                    if(e.size>=1024*1024*1024ull)std::snprintf(size,sizeof(size),"%.1f GiB",double(e.size)/(1024*1024*1024ull));
                    else if(e.size>=1024*1024)std::snprintf(size,sizeof(size),"%.1f MiB",double(e.size)/(1024*1024));
                    else if(e.size>=1024)std::snprintf(size,sizeof(size),"%.1f KiB",double(e.size)/1024);
                    else std::snprintf(size,sizeof(size),"%llu B",static_cast<unsigned long long>(e.size));
                    Text(s,{1000,r.top+3,r.right-5,r.bottom-1},e.kind==ShellBrowser::Kind::Drive && e.readOnly?u"только чтение":Wide(size),color,12);
                }
            }
            if(entries.empty())Text(s,{440,204,1118,252},browser_.Errors() || browser_.Truncated()?u"Нет записей для отображения. См. сообщение ниже.":u"Папка пуста.",0x536376,16);
            std::u16string note;
            if(browser_.LastError()!=ShellBrowser::Error::None)note=ShellBrowser::ErrorText(browser_.LastError());
            else if(!operationNotice_.empty())note=operationNotice_;
            else {
                if(browser_.Truncated())note+=u"Лимит просмотра: список неполный. ";
                if(browser_.Errors())note+=u"Некоторые записи недоступны. ";
                if(note.empty() && browser_.CanChooseApplication()) {
                    const auto app=browser_.ApplicationFor(browser_.SelectedDocument());
                    note=app.empty()?u"Открыть: выбрать приложение. Тип запоминается до выхода.":u"Приложение: "+Wide(app.substr(app.find_last_of('\\')+1))+u" (до выхода из оболочки)";
                }
                if(note.empty())note=browser_.Path().empty()?u"Для томов показано доступное место. R: только чтение.":browser_.ReadOnly()?u"Только чтение. Адресная строка открывает ввод пути.":u"Документы: Открыть или Выбрать приложение. Тип запоминается до выхода.";
            }
            Text(s,{240,556,1138,577},note,browser_.LastError()!=ShellBrowser::Error::None?0xA33C22:0x5A6574,13);
            control(OpenWith,u"Выбрать приложение",Bounds(OpenWith));
            control(Previous,u"← Страница",Bounds(Previous));control(Next,u"Страница →",Bounds(Next));control(Launch,u"Открыть",Bounds(Launch));
            char count[80];std::snprintf(count,sizeof(count),"%zu / %zu",entries.empty()?0:browser_.Selected()+1,entries.size());
            Text(s,{242,278,404,298},Wide(count),0x42576B,12);
            control(Copy,u"Копировать",Bounds(Copy));control(Paste,u"Вставить…",Bounds(Paste));
        } else {
            Icon(s,256,184,shell_art::computer);
            Text(s,{312,184,1120,220},u"Switch Recompiler",0x123D75,26,true);
            Text(s,{256,254,1120,288},u"Native-оболочка для поддерживаемых Win32-приложений.",0x24354A,19);
            Text(s,{256,306,1120,336},u"Постоянный C: — SD / switch-recompiler / drive_c",0x24354A,17);
            Text(s,{256,346,1120,376},u"S: — реальная SD-карта    •    R: — ROMFS, только чтение",0x24354A,17);
            Text(s,{256,410,1120,442},u"Иконки Tango: Public Domain. Шрифты Liberation: OFL.",0x526477,16);
            Text(s,{256,454,1120,484},u"После выхода из приложения вы вернётесь в прежнюю папку.",0x526477,16);
        }
    }
    Gradient(s,{0,680,Width,720},0x397FE5,0x1955BA);Fill(s,{0,680,Width,682},0x70ACFA);
    Gradient(s,Bounds(Start),(startOpen_ || pressed_==Start)?0x1B7435:0x5BB464,(startOpen_ || pressed_==Start)?0x195A2F:0x248337);
    Frame(s,Bounds(Start),0x1C6332);
    if(focus_==Start || hover_==Start)Frame(s,{3,683,139,717},0xADDEAB);
    Fill(s,{14,691,32,709},0xE8FAEB);Text(s,{16,689,34,712},u"S",0x21713D,14,true);
    Text(s,{45,685,137,717},u"Пуск",0xFFFFFF,22,true);
    if(guestActive_) {
        const auto task=Bounds(GuestTask);
        Gradient(s,task,guestMinimized_?0x5E9CF0:0x174DAA,guestMinimized_?0x397DE0:0x2562BB);
        Frame(s,task,guestMinimized_?0x91B8F2:0x103C8C);
        Icon(s,task.left+3,task.top,shell_art::programs);
        Text(s,{task.left+40,task.top+7,task.right-8,task.bottom},(guestTaskPending_?u"Ожидание · ":u"")+Wide(guestLabel_),0xFFFFFF,14,true);
    }
    Gradient(s,{1158,682,1280,720},0x2598DE,0x127BBD);
    Text(s,{1180,689,1272,716},clock,0xFFFFFF,17);
    if(startOpen_) {
        Fill(s,{4,288,484,680},0x163B64);Fill(s,{0,284,480,680},0xFFFFFF);
        Fill(s,{242,352,478,630},0xD8E8F7);Frame(s,{0,284,480,680},0x1555AD);
        Gradient(s,{2,286,478,352},0x4D98EB,0x1C63BD);
        Frame(s,{14,298,54,340},0xE7F1FF);Icon(s,18,302,shell_art::computer);Text(s,{66,306,465,342},u"Player",0xFFFFFF,23,true);
        for(int t:{MenuPrograms,MenuComputer,MenuDocuments,MenuRun,About}) {
            const auto r=Bounds(t);const bool hot=Enabled(t) && (focus_==t || hover_==t);
            if(hot){Fill(s,r,pressed_==t?0x1D4C99:0x316AC5);Frame(s,r,0x204DA0);}
            Icon(s,r.left+5,r.top+7,t==MenuComputer?shell_art::computer:t==MenuDocuments?shell_art::folder:shell_art::programs);
            Text(s,{r.left+43,r.top+12,r.right-4,r.bottom-2},t==MenuPrograms?u"Все программы →":t==MenuComputer?u"Мой компьютер":t==MenuDocuments?u"Мои документы":t==MenuRun?u"Выполнить…":u"О программе",!Enabled(t)?0x8195A9:hot?0xFFFFFF:0x173250,15,t==MenuPrograms);
        }
        Text(s,{258,538,466,602},guestActive_?u"Одно приложение за раз":browser_.Ready()?u"Выполнить: путь и аргументы":u"Файлы недоступны",0x8397AB,13);
        Gradient(s,{2,622,478,678},0x3282D6,0x1C61B5);control(Exit,u"Выйти в Homebrew",Bounds(Exit));
    }
    if(dialog_!=Dialog::None) {
        Fill(s,{402,202,1034,552},0x13385B);Fill(s,{396,196,1028,546},0xECE9D8);
        Frame(s,{396,196,1028,546},0x0646A4);Gradient(s,{398,198,1026,236},0x0868F4,0x0347BD);
        Text(s,{410,204,1010,232},dialog_==Dialog::DeleteConfirm?u"Подтверждение удаления":dialog_==Dialog::CopyProgress?u"Копирование":u"Свойства",0xFFFFFF,19,true);
        const auto slash=operationPath_.find_last_of('\\');
        Text(s,{416,250,1006,278},Wide(operationPath_.substr(slash==std::string::npos?0:slash+1)),0x123D75,18,true);
        Text(s,{416,282,1006,304},Wide(operationPath_),0x42576B,13);
        if(dialog_==Dialog::CopyProgress) {
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
