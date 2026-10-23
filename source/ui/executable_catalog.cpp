#include "executable_catalog.h"
#include <algorithm>
#include <dirent.h>
#include <sys/stat.h>

namespace ui {
namespace {
bool IsExe(const std::string& name) {
    if(name.size()<5)return false;
    const auto n=name.size();
    auto lower=[](char c){return c>='A' && c<='Z'?char(c-'A'+'a'):c;};
    return name[n-4]=='.' && lower(name[n-3])=='e' && lower(name[n-2])=='x' && lower(name[n-1])=='e';
}
}
void ExecutableCatalog::ScanRoot(const std::string& root,int depth) {
    if(root.empty()){++errors_;return;}
    std::string dir=root;
    if(dir.back()!='/')dir+='/';
    if(dir.size()>MaxPathBytes){truncated_=true;return;}
    const auto first=files_.size();
    ScanDirectory(dir,std::max(0,std::min(depth,MaxDepth)));
    std::sort(files_.begin()+first,files_.end()); // preserve caller's root priority
}
void ExecutableCatalog::ScanDirectory(const std::string& dir,int depth) {
    if(files_.size()>=MaxFiles || entries_>=MaxEntries){truncated_=true;return;}
    DIR* handle=opendir(dir.c_str());if(!handle){++errors_;return;}
    while(dirent* entry=readdir(handle)) {
        const std::string name=entry->d_name;if(name=="." || name=="..")continue;
        if(files_.size()>=MaxFiles || entries_>=MaxEntries){truncated_=true;break;}
        ++entries_;
        if(name.size()>MaxPathBytes-dir.size()){truncated_=true;continue;}
        const std::string full=dir+name;
        struct stat info;
        if(stat(full.c_str(),&info)!=0){++errors_;continue;}
        if(S_ISDIR(info.st_mode)) {
            if(depth>0) {
                if(full.size()==MaxPathBytes){truncated_=true;continue;}
                ScanDirectory(full+"/",depth-1);
            }
        } else if(S_ISREG(info.st_mode) && IsExe(name))files_.push_back(full);
    }
    closedir(handle);
}
} // namespace ui
