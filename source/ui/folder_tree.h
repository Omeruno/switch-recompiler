#pragma once
#include "../runtime/files.h"
namespace ui {
class FolderTree {
public:
    static constexpr size_t MaxNodes=128,MaxScanned=4096,MaxDepth=32;
    struct Node {std::string path,name;unsigned depth=0;bool expanded=false;};
    void Reset(const rt::Vfs&);
    bool Expand(size_t);
    void Collapse(size_t);
    const std::vector<Node>& Nodes() const {return nodes_;}
    bool Truncated() const {return truncated_;}
    int Error() const {return error_;}
private:
    rt::Vfs vfs_;
    std::vector<Node> nodes_;
    bool truncated_=false;int error_=0;
};
} // namespace ui
