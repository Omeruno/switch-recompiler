#include "shell_browser.h"
#include <algorithm>
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>

namespace ui {
namespace {
char Lower(char c) {return c>='A' && c<='Z'?char(c+'a'-'A'):c;}
int FoldCompare(const std::string& a,const std::string& b) {
    for(size_t i=0;i<std::min(a.size(),b.size());++i) {
        const auto x=static_cast<unsigned char>(Lower(a[i])),y=static_cast<unsigned char>(Lower(b[i]));
        if(x!=y)return x<y?-1:1;
    }
    return a.size()==b.size()?0:a.size()<b.size()?-1:1;
}
bool NameLess(const ShellBrowser::Entry& a,const ShellBrowser::Entry& b) {
    if(a.kind!=b.kind)return int(a.kind)<int(b.kind); // Drives/directories before files.
    const int cmp=FoldCompare(a.name,b.name);return cmp?cmp<0:a.name<b.name;
}
std::string Join(const std::string& parent,const std::string& name) {
    return parent+(parent.back()=='\\'?"":"\\")+name;
}
}
ShellBrowser::ShellBrowser(size_t items,size_t scanned)
    :itemLimit_(std::max(size_t(1),std::min(items,MaxItems))),scanLimit_(std::max(size_t(1),std::min(scanned,MaxScanned))) {}
bool ShellBrowser::Configure(const std::string& system,const std::string& sd,const std::string& rom,std::string& error) {
    if(!vfs_.ConfigurePersistent(system,sd,rom,error))return Fail(Error::NotReady);
    ready_=true;path_.clear();history_.clear();entries_.clear();selected_=0;
    return Refresh();
}
bool ShellBrowser::Mounted(char drive) const {
    if(drive>='a' && drive<='z')drive=char(drive-'a'+'A');
    return ready_ && drive>='A' && drive<='Z' && (vfs_.DriveMask()&(1u<<(drive-'A')));
}
std::string ShellBrowser::DocumentsPath() const {return vfs_.ProfileDirectory()+"\\My Documents";}
const char16_t* ShellBrowser::ErrorText(Error error) {
    switch(error) {
    case Error::None:return u"";
    case Error::ResolutionLimit:return u"Лимит поиска пути. Укажите точное имя папки или файла.";
    case Error::NotReady:return u"Файловая система недоступна.";
    case Error::InvalidPath:return u"Недопустимый путь или неподключённый диск.";
    case Error::NotFound:return u"Файл или папка больше не существует. Обновите список.";
    case Error::NotDirectory:return u"Укажите путь к папке.";
    case Error::AccessDenied:return u"Нет доступа к пути.";
    case Error::ReadFailed:return u"Не удалось прочитать каталог.";
    case Error::UnsupportedFile:return u"Для этого файла пока нет приложения. Поддержан запуск EXE.";
    }
    return u"Ошибка файловой системы.";
}
bool ShellBrowser::FailErrno() {
    return Fail(errno==EOVERFLOW?Error::ResolutionLimit:errno==ENOENT?Error::NotFound:errno==ENOTDIR?Error::NotDirectory:
                errno==EACCES || errno==EPERM?Error::AccessDenied:
                errno==EINVAL || errno==ENODEV?Error::InvalidPath:Error::ReadFailed);
}
bool ShellBrowser::IsExecutable(const std::string& path) {
    const auto n=path.size();return n>4 && path[n-4]=='.' && Lower(path[n-3])=='e' && Lower(path[n-2])=='x' && Lower(path[n-1])=='e';
}
bool ShellBrowser::Read(const std::string& path,Listing& out) {
    if(!ready_)return Fail(Error::NotReady);
    if(path.empty()) {
        for(char d='A';d<='Z';++d)if(Mounted(d)) {
            Entry e;e.name=std::string(1,d)+":";e.path=e.name+"\\";e.kind=Kind::Drive;
            rt::VolumeInfo volume;
            if(vfs_.QueryVolume(e.path,volume)){e.readOnly=volume.readOnly;e.size=volume.availableBytes;}
            else {++out.errors;e.readOnly=vfs_.ToHost(e.path,true).empty() && errno==EROFS;}
            out.entries.push_back(e);
        }
        return true;
    }
    size_t caseBudget=scanLimit_;
    const auto host=vfs_.ToHost(path,false,&caseBudget);if(host.empty())return FailErrno();
    DIR* dir=opendir(host.c_str());if(!dir)return FailErrno();
    const auto writable=vfs_.ToHost(path,true,&caseBudget);
    if(writable.empty() && errno!=EROFS){const int failure=errno;closedir(dir);errno=failure;return FailErrno();}
    out.readOnly=writable.empty();
    for(;;) {
        errno=0;const auto* entry=readdir(dir);
        if(!entry){if(errno)++out.errors;break;}
        const std::string name=entry->d_name;if(name=="." || name=="..")continue;
        if(out.scanned==scanLimit_){out.truncated=true;break;}
        ++out.scanned;
        // Backslashes/colons are native Unix filename characters but cannot be
        // represented as one component in this Windows namespace. Never alias.
        if(name.find_first_of("\\:")!=std::string::npos){++out.errors;continue;}
        const auto guest=Join(path,name);
        if(guest.size()>4096){out.truncated=true;continue;}
        const auto native=vfs_.ToHost(guest,false,&caseBudget);struct stat st{};
        if(native.empty() || stat(native.c_str(),&st)!=0){++out.errors;if(native.empty() && errno==EOVERFLOW){out.truncated=true;break;}continue;}
        if(!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)){++out.errors;continue;}
        Entry e;e.name=name;e.path=guest;e.kind=S_ISDIR(st.st_mode)?Kind::Directory:Kind::File;
        e.size=st.st_size>0?uint64_t(st.st_size):0;e.readOnly=out.readOnly;
        // Keep the lexically earliest bounded subset, independent of readdir
        // order within the scanned prefix. No recursive scan or retained DIR.
        if(out.entries.size()<itemLimit_)out.entries.push_back(std::move(e));
        else {
            out.truncated=true;auto worst=std::max_element(out.entries.begin(),out.entries.end(),NameLess);
            if(NameLess(e,*worst))*worst=std::move(e);
        }
    }
    closedir(dir);return true;
}
std::string ShellBrowser::SelectedPath() const {return selected_<entries_.size()?entries_[selected_].path:std::string();}
void ShellBrowser::SortEntries(const std::string& old) {
    std::sort(entries_.begin(),entries_.end(),[&](const Entry& a,const Entry& b) {
        if(a.kind!=b.kind)return int(a.kind)<int(b.kind);
        if(sort_==Sort::SizeDescending && a.size!=b.size)return a.size>b.size;
        if(sort_==Sort::NameDescending)return NameLess(b,a);
        return NameLess(a,b);
    });
    selected_=0;
    for(size_t i=0;i<entries_.size();++i)if(entries_[i].path==old){selected_=i;break;}
}
bool ShellBrowser::Load(const std::string& path,const std::string& old) {
    Listing next;if(!Read(path,next))return false;
    size_t caseBudget=scanLimit_;
    if(!path.empty() && !vfs_.SetCurrentDirectory(path,&caseBudget))return FailErrno();
    path_=path;entries_=std::move(next.entries);readOnly_=next.readOnly;truncated_=next.truncated;
    errors_=next.errors;scanned_=next.scanned;error_=Error::None;SortEntries(old);return true;
}
bool ShellBrowser::Navigate(const std::string& input) {
    if(!ready_)return Fail(Error::NotReady);
    const auto next=input.empty()?std::string():vfs_.FullGuestPath(input);
    if(!input.empty() && next.empty())return FailErrno();
    if(next==path_)return Refresh();
    const Location old{path_,SelectedPath()};
    if(!Load(next,{}))return false;
    if(history_.size()==MaxHistory)history_.erase(history_.begin());
    history_.push_back(old);return true;
}
bool ShellBrowser::Refresh() {return Load(path_,SelectedPath());}
bool ShellBrowser::Back() {
    if(history_.empty())return false;
    const auto last=history_.back();if(!Load(last.path,last.selectedPath))return false;
    history_.pop_back();return true;
}
bool ShellBrowser::Up() {
    if(path_.empty())return false;
    const auto slash=path_.find_last_of('\\');
    return Navigate(path_.size()==3?"":slash<=2?path_.substr(0,3):path_.substr(0,slash));
}
void ShellBrowser::Select(size_t index) {if(index<entries_.size()){selected_=index;error_=Error::None;}}
void ShellBrowser::Move(int delta) {
    if(entries_.empty())return;
    const int count=int(entries_.size());Select(size_t((int(selected_)+delta%count+count)%count));
}
void ShellBrowser::Page(int delta,size_t rows) {
    if(entries_.empty() || !rows)return;
    const auto page=std::max(int64_t(0),std::min(int64_t(selected_/rows)+delta,int64_t((entries_.size()-1)/rows)));
    Select(std::min(size_t(page)*rows+selected_%rows,entries_.size()-1));
}
void ShellBrowser::CycleSort() {
    const auto selected=SelectedPath();sort_=Sort((int(sort_)+1)%3);SortEntries(selected);
}
bool ShellBrowser::ResolveExecutable(const std::string& path,ShellLaunchRequest& out) {
    const auto input=path;out.Clear();if(!ready_)return Fail(Error::NotReady);
    const auto full=vfs_.FullGuestPath(input);if(full.empty())return FailErrno();
    size_t caseBudget=scanLimit_;
    const auto native=vfs_.ToHost(full,false,&caseBudget);if(native.empty())return FailErrno();
    struct stat st{};if(stat(native.c_str(),&st)!=0)return FailErrno();
    if(!S_ISREG(st.st_mode) || !IsExecutable(full))return Fail(Error::UnsupportedFile);
    // Recheck readability now; a later load failure still returns through F0.
    FILE* file=std::fopen(native.c_str(),"rb");if(!file)return FailErrno();std::fclose(file);
    out.guestPath=full;out.nativePath=native;error_=Error::None;return true;
}
bool ShellBrowser::ResolveCatalogExecutable(const std::string& native,ShellLaunchRequest& out) {
    const auto input=native;out.Clear();if(!ready_)return Fail(Error::NotReady);
    size_t caseBudget=scanLimit_;
    const auto guest=vfs_.ToGuest(input,&caseBudget);if(guest.empty())return FailErrno();
    return ResolveExecutable(guest,out);
}
bool ShellBrowser::CanOpenSelected() const {
    return selected_<entries_.size() && (entries_[selected_].kind!=Kind::File || IsExecutable(entries_[selected_].path));
}
bool ShellBrowser::OpenPath(const std::string& path,ShellLaunchRequest& out) {
    const auto input=path;out.Clear();if(!ready_)return Fail(Error::NotReady);
    if(input.empty())return Fail(Error::InvalidPath);
    const auto full=vfs_.FullGuestPath(input);if(full.empty())return FailErrno();
    size_t caseBudget=scanLimit_;const auto native=vfs_.ToHost(full,false,&caseBudget);
    if(native.empty())return FailErrno();
    struct stat st{};if(stat(native.c_str(),&st)!=0)return FailErrno();
    return S_ISDIR(st.st_mode)?Navigate(full):ResolveExecutable(full,out);
}
bool ShellBrowser::OpenSelected(ShellLaunchRequest& out) {
    out.Clear();if(selected_>=entries_.size())return false;
    return OpenPath(entries_[selected_].path,out);
}
} // namespace ui
