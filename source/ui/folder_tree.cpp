#include "folder_tree.h"
#include <algorithm>
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>
namespace ui {
void FolderTree::Reset(const rt::Vfs& vfs) {
    vfs_=vfs;nodes_.clear();error_=0;truncated_=false;
    for(char c='A';c<='Z';++c)if(vfs_.DriveMask()&(1u<<(c-'A')))nodes_.push_back({std::string(1,c)+":\\",std::string(1,c)+":",0,false});
}
bool FolderTree::Expand(size_t index) {
    error_=0;truncated_=false;
    if(index>=nodes_.size()){error_=EINVAL;return false;}
    const auto node=nodes_[index];if(node.expanded)return true;
    if(node.depth==MaxDepth || nodes_.size()==MaxNodes){truncated_=true;error_=EOVERFLOW;return false;}
    size_t budget=MaxScanned;const auto host=vfs_.ToHost(node.path,false,&budget);
    if(host.empty()){error_=errno;return false;}DIR* dir=opendir(host.c_str());if(!dir){error_=errno;return false;}
    std::vector<Node> children;size_t scanned=0;
    for(;;) {
        errno=0;auto* entry=readdir(dir);if(!entry){if(errno)error_=errno;break;}
        std::string name=entry->d_name;if(name=="." || name=="..")continue;
        if(scanned++==MaxScanned){truncated_=true;break;}
        if(name.find_first_of("\\:")!=std::string::npos)continue;
        const auto path=node.path+(node.path.back()=='\\'?"":"\\")+name;
        if(path.size()>4096){truncated_=true;continue;}
        const auto native=vfs_.ToHost(path,false,&budget);struct stat st{};
        if(native.empty()){if(errno==EOVERFLOW){truncated_=true;break;}continue;}
        if(stat(native.c_str(),&st)!=0 || !S_ISDIR(st.st_mode))continue;
        children.push_back({path,name,node.depth+1,false});
        if(children.size()+nodes_.size()>MaxNodes) {
            truncated_=true;auto worst=std::max_element(children.begin(),children.end(),[](const Node& a,const Node& b){return a.name<b.name;});children.erase(worst);
        }
    }
    if(closedir(dir)!=0 && !error_)error_=errno;
    if(error_)return false;
    std::sort(children.begin(),children.end(),[](const Node& a,const Node& b){return a.name<b.name;});
    nodes_[index].expanded=true;nodes_.insert(nodes_.begin()+index+1,children.begin(),children.end());return true;
}
void FolderTree::Collapse(size_t index) {
    if(index>=nodes_.size())return;
    size_t end=index+1;while(end<nodes_.size() && nodes_[end].depth>nodes_[index].depth)++end;
    nodes_.erase(nodes_.begin()+index+1,nodes_.begin()+end);nodes_[index].expanded=false;error_=0;truncated_=false;
}
} // namespace ui
